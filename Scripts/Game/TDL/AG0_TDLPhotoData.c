//------------------------------------------------------------------------------------------------
// AG0_TDLPhotoData.c
//
// Palette-indexed image pipeline for the TDL custom-image API:
//
//   Server (tdl-api)            Wire                        Mod
//   ─────────────────           ─────                       ─────
//   sharp + image-q  ──► JSON { w, h, p, rgz|r|d } ──►  JsonLoadContext
//                           (base64, optionally gzipped)       │
//                                                              ▼
//                                                     AG0_Base64.Decode
//                                                     AG0_TDLGzip.Gunzip  (rgz only)
//                                                     DecodeRectsFromBytes
//                                                              │
//                                                              ▼
//                                               AG0_TDLPhotoRenderer.Draw()
//                                                CanvasWidget TriMesh quads
//
// Stays inside the CanvasWidget / TriMeshDrawCommand primitives so it runs
// on every platform Reforger ships on (no LoadImageTexture, no $profile file I/O).
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! How to fit a non-square image into a canvas of a different aspect ratio.
//!
//!   CONTAIN — preserve aspect, shrink to fit, letterbox (safe default for photos/slides)
//!   COVER   — preserve aspect, scale up, crop overflow (good for thumbnails)
//!   FILL    — stretch each axis independently (ignores source aspect)
//------------------------------------------------------------------------------------------------
enum AG0_TDLPhotoFitMode
{
    CONTAIN,
    COVER,
    FILL
}

//------------------------------------------------------------------------------------------------
// Base64 decoder - lookup table for performance
//------------------------------------------------------------------------------------------------
class AG0_Base64
{
    static ref array<int> s_Lookup;  // public so AG0_TDLBase64Decoder can share the table
    
    static void InitLookup()
    {
        if (s_Lookup)
            return;
        
        s_Lookup = {};
        s_Lookup.Resize(128);
        
        string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++)
            s_Lookup[chars.ToAscii(i)] = i;
    }
    
    static array<int> Decode(string input)
    {
        InitLookup();

        int len = input.Length();
        // Compute exact output size from padding. We cache the trailing
        // ToAscii calls below so each char position is asked at most once.
        int maxOut = (len / 4) * 3;
        int last1Ascii = -1;
        int last2Ascii = -1;
        if (len >= 1)
        {
            last1Ascii = input.ToAscii(len - 1);
            if (last1Ascii == 61) maxOut = maxOut - 1;
        }
        if (len >= 2)
        {
            last2Ascii = input.ToAscii(len - 2);
            if (last2Ascii == 61) maxOut = maxOut - 1;
        }

        array<int> output = new array<int>();
        output.Resize(maxOut);
        int outIdx = 0;

        // ToAscii is a script-to-engine proto call (~50us/call observed),
        // so the dominant cost in this loop is the # of ToAscii calls,
        // not the math. Cache every char position once per iteration —
        // 4 calls per 4 chars instead of 6.
        for (int i = 0; i < len; i += 4)
        {
            int charA = input.ToAscii(i);
            int charB = input.ToAscii(i + 1);
            int charC = input.ToAscii(i + 2);
            int charD = input.ToAscii(i + 3);

            int a = s_Lookup[charA];
            int b = s_Lookup[charB];
            int c = s_Lookup[charC];
            int d = s_Lookup[charD];

            output[outIdx] = (a << 2) | (b >> 4);
            outIdx = outIdx + 1;
            if (charC != 61)  // '=' is ASCII 61
            {
                output[outIdx] = ((b & 0xF) << 4) | (c >> 2);
                outIdx = outIdx + 1;
            }
            if (charD != 61)
            {
                output[outIdx] = ((c & 0x3) << 6) | d;
                outIdx = outIdx + 1;
            }
        }
        return output;
    }
}

//------------------------------------------------------------------------------------------------
//! Resumable base64 decoder. The static AG0_Base64.Decode does the entire
//! string in one call, which blocks the main thread for ~10s on a 240KB
//! payload because every ToAscii(i) is a script→engine proto call costing
//! roughly 50us. There's no bulk string-to-bytes API in Enfusion, so the
//! only way to keep the game responsive is to spread the work across
//! frames via CallqueueCallLater.
//!
//! Usage:
//!   AG0_TDLBase64Decoder d = new AG0_TDLBase64Decoder();
//!   d.Init(b64string);
//!   while (d.Step(charsPerBatch)) { /* yield to next frame */ }
//!   array<int> bytes = d.GetOutput();
//!
//! charsPerBatch ~800 keeps each frame around ~30ms hitch on the observed
//! ToAscii cost. Tunable per call site.
//------------------------------------------------------------------------------------------------
class AG0_TDLBase64Decoder
{
    protected string m_sInput;
    protected int    m_iLen;
    protected int    m_iInPos;
    protected ref array<int> m_aOutput;
    protected int    m_iOutPos;

    void Init(string input)
    {
        AG0_Base64.InitLookup();
        m_sInput = input;
        m_iLen   = input.Length();
        m_iInPos = 0;

        int maxOut = (m_iLen / 4) * 3;
        if (m_iLen >= 1 && input.ToAscii(m_iLen - 1) == 61) maxOut = maxOut - 1;
        if (m_iLen >= 2 && input.ToAscii(m_iLen - 2) == 61) maxOut = maxOut - 1;

        m_aOutput = new array<int>();
        m_aOutput.Resize(maxOut);
        m_iOutPos = 0;
    }

    //! Process input chars until either timeBudgetMs elapses or the input
    //! is exhausted. Time-based budgeting auto-adapts to hardware speed:
    //! a fast machine processes more per call, a slow one processes less,
    //! both stay under frame budget.
    //!
    //! Returns true if more remains (caller should re-schedule), false
    //! when fully decoded.
    //!
    //! Internal: checks elapsed time every 256 chars (64 base64 quanta).
    //! At ~50us/ToAscii × 4 calls/quantum × 64 quanta ≈ 12.8ms per check
    //! cycle on the observed hardware, so the granularity overshoots the
    //! budget by at most one check cycle (~13ms worst case).
    bool Step(int timeBudgetMs)
    {
        int startTick = System.GetTickCount();
        int charsSinceCheck = 0;

        while (m_iInPos + 4 <= m_iLen)
        {
            int charA = m_sInput.ToAscii(m_iInPos);
            int charB = m_sInput.ToAscii(m_iInPos + 1);
            int charC = m_sInput.ToAscii(m_iInPos + 2);
            int charD = m_sInput.ToAscii(m_iInPos + 3);

            int a = AG0_Base64.s_Lookup[charA];
            int b = AG0_Base64.s_Lookup[charB];
            int c = AG0_Base64.s_Lookup[charC];
            int d = AG0_Base64.s_Lookup[charD];

            m_aOutput[m_iOutPos] = (a << 2) | (b >> 4);
            m_iOutPos = m_iOutPos + 1;
            if (charC != 61)
            {
                m_aOutput[m_iOutPos] = ((b & 0xF) << 4) | (c >> 2);
                m_iOutPos = m_iOutPos + 1;
            }
            if (charD != 61)
            {
                m_aOutput[m_iOutPos] = ((c & 0x3) << 6) | d;
                m_iOutPos = m_iOutPos + 1;
            }

            m_iInPos = m_iInPos + 4;
            charsSinceCheck = charsSinceCheck + 4;

            if (charsSinceCheck >= 256)
            {
                if (System.GetTickCount() - startTick >= timeBudgetMs)
                    return m_iInPos < m_iLen;
                charsSinceCheck = 0;
            }
        }
        return m_iInPos < m_iLen;
    }

    array<int> GetOutput()
    {
        m_aOutput.Resize(m_iOutPos);
        return m_aOutput;
    }

    int GetProgress()  { return m_iInPos; }
    int GetTotal()     { return m_iLen; }
}

// AG0_TDLImageCallback moved to AG0_TDLPhotoManager.c — it now holds an int requestId
// instead of a component back-ref, dispatching results to the manager via
// AG0_TDLSystem.GetInstance().GetPhotoManager(). This removes the ref cycle that
// existed between the callback and the component, and lets a single REST callback
// shape serve any number of concurrent fetches.

//------------------------------------------------------------------------------------------------
// Serializable image data - designed for network transfer
//
// Two render paths are supported:
//   * m_aPixels  — one palette index per pixel, width*height entries (legacy)
//   * m_aRects   — run/rectangle list, 5 ints per rect [colorIdx, x, y, w, h]
//                  (preferred; server-side coalesced, drops draw-call count
//                  by 10-300x for flat/UI content and still 4-8x for photos)
//
// The renderer prefers m_aRects when non-empty and falls back to pixels.
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoData
{
    int m_iWidth;
    int m_iHeight;
    ref array<int> m_aPalette;      // ARGB colors
    ref array<int> m_aPixels;       // Indices into palette (W*H entries) — legacy path
    ref array<int> m_aRects;        // Rectangles, 5 ints per rect: [colorIdx, x, y, w, h]

    void AG0_TDLPhotoData()
    {
        m_aPalette = {};
        m_aPixels = {};
        m_aRects = {};
    }

    //------------------------------------------------------------------------------------------------
    int GetSerializedSize()
    {
        return 8
            + (m_aPalette.Count() * 4)
            + (m_aPixels.Count() * 4)
            + (m_aRects.Count() * 4);
    }

    //------------------------------------------------------------------------------------------------
    int GetRectCount()
    {
        return m_aRects.Count() / 5;
    }

    //------------------------------------------------------------------------------------------------
    bool HasRects()
    {
        return m_aRects.Count() >= 5;
    }

    //------------------------------------------------------------------------------------------------
    //! Decode the 8-byte-per-rect binary layout from an already-decoded
    //! byte array into the [c,x,y,w,h] flat format used by m_aRects.
    //!
    //! Wire layout per rect (little-endian):
    //!   [0]     colorIdx : u8
    //!   [1..2]  x        : u16
    //!   [3..4]  y        : u16
    //!   [5..6]  w        : u16
    //!   [7]     h        : u8
    static array<int> DecodeRectsFromBytes(array<int> bytes)
    {
        int rectCount = bytes.Count() / 8;

        // Pre-Resize and indexed-write avoids per-Insert function call
        // overhead. For a 55K-rect photo this drops 275K Inserts to one
        // Resize + 275K direct stores.
        array<int> rects = new array<int>();
        rects.Resize(rectCount * 5);

        int outIdx = 0;
        for (int i = 0; i < rectCount; i++)
        {
            int off = i * 8;
            rects[outIdx]     = bytes[off + 0] & 0xFF;
            rects[outIdx + 1] = (bytes[off + 1] & 0xFF) | ((bytes[off + 2] & 0xFF) << 8);
            rects[outIdx + 2] = (bytes[off + 3] & 0xFF) | ((bytes[off + 4] & 0xFF) << 8);
            rects[outIdx + 3] = (bytes[off + 5] & 0xFF) | ((bytes[off + 6] & 0xFF) << 8);
            rects[outIdx + 4] = bytes[off + 7] & 0xFF;
            outIdx = outIdx + 5;
        }
        return rects;
    }

    //------------------------------------------------------------------------------------------------
    //! Decode the server's "r" field (base64-encoded rect records) into m_aRects.
    static array<int> DecodeRectsBase64(string base64Input)
    {
        return DecodeRectsFromBytes(AG0_Base64.Decode(base64Input));
    }

    //------------------------------------------------------------------------------------------------
    //! Decode the server's "rgz" field — base64(gzip(rect records)). Cuts
    //! payload another 20-50% on top of the already-dense rect encoding
    //! because run indices and repeated (x,w) columns compress well.
    //!
    //! The double-wrap (gzip then base64) is deliberate: RestContext returns
    //! responses as strings, which aren't binary-safe (null bytes truncate),
    //! so the gzip bytes have to ride inside a printable ASCII envelope.
    static array<int> DecodeRectsBase64Gzip(string base64Input)
    {
        array<int> gz = AG0_Base64.Decode(base64Input);
        array<int> raw = AG0_TDLGzip.Gunzip(gz);
        return DecodeRectsFromBytes(raw);
    }

    //------------------------------------------------------------------------------------------------
    //! Decode the server's "dgz" field — base64(gzip(pixel bytes)). Companion
    //! to "rgz" for the legacy per-pixel path, for parity.
    static array<int> DecodePixelsBase64Gzip(string base64Input)
    {
        array<int> gz = AG0_Base64.Decode(base64Input);
        return AG0_TDLGzip.Gunzip(gz);
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDLPhotoData CreateTestGradient(int size = 64)
    {
        AG0_TDLPhotoData data = new AG0_TDLPhotoData();
        data.m_iWidth = size;
        data.m_iHeight = size;
        
        // 16-color rainbow palette
        data.m_aPalette = {
            0xFFFF0000, 0xFFFF4000, 0xFFFF8000, 0xFFFFBF00,
            0xFFFFFF00, 0xFFBFFF00, 0xFF00FF00, 0xFF00FFBF,
            0xFF00FFFF, 0xFF00BFFF, 0xFF0080FF, 0xFF0000FF,
            0xFF8000FF, 0xFFBF00FF, 0xFFFF00FF, 0xFFFF0080
        };
        
        data.m_aPixels.Reserve(size * size);
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                int idx = ((x + y) * 16 / (size * 2)) % 16;
                data.m_aPixels.Insert(idx);
            }
        }
        
        return data;
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDLPhotoData CreateTestCheckerboard(int size = 64, int cellSize = 8)
    {
        AG0_TDLPhotoData data = new AG0_TDLPhotoData();
        data.m_iWidth = size;
        data.m_iHeight = size;

        data.m_aPalette = { 0xFF202020, 0xFFE0E0E0 };

        data.m_aPixels.Reserve(size * size);
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                int idx = ((x / cellSize) + (y / cellSize)) % 2;
                data.m_aPixels.Insert(idx);
            }
        }

        return data;
    }

    //------------------------------------------------------------------------------------------------
    //! Same checkerboard pattern as CreateTestCheckerboard, but expressed directly
    //! as rectangles (one rect per cell, full cell height). Exercises the rects
    //! render path without needing the API.
    static AG0_TDLPhotoData CreateTestCheckerboardRects(int size = 64, int cellSize = 8)
    {
        AG0_TDLPhotoData data = new AG0_TDLPhotoData();
        data.m_iWidth = size;
        data.m_iHeight = size;
        data.m_aPalette = { 0xFF202020, 0xFFE0E0E0 };

        int cellsPerSide = size / cellSize;
        data.m_aRects.Reserve(cellsPerSide * cellsPerSide * 5);
        for (int cy = 0; cy < cellsPerSide; cy++)
        {
            for (int cx = 0; cx < cellsPerSide; cx++)
            {
                int colorIdx = (cx + cy) % 2;
                data.m_aRects.Insert(colorIdx);
                data.m_aRects.Insert(cx * cellSize);
                data.m_aRects.Insert(cy * cellSize);
                data.m_aRects.Insert(cellSize);
                data.m_aRects.Insert(cellSize);
            }
        }
        return data;
    }

    //------------------------------------------------------------------------------------------------
    //! Horizontal bands of solid color — tiny rect count, good for rendering
    //! path smoke tests and for comparing legacy/single-pass/rects output.
    static AG0_TDLPhotoData CreateTestBandsRects(int size = 64, int bandCount = 16)
    {
        AG0_TDLPhotoData data = new AG0_TDLPhotoData();
        data.m_iWidth = size;
        data.m_iHeight = size;
        data.m_aPalette = {
            0xFFFF0000, 0xFFFF4000, 0xFFFF8000, 0xFFFFBF00,
            0xFFFFFF00, 0xFFBFFF00, 0xFF00FF00, 0xFF00FFBF,
            0xFF00FFFF, 0xFF00BFFF, 0xFF0080FF, 0xFF0000FF,
            0xFF8000FF, 0xFFBF00FF, 0xFFFF00FF, 0xFFFF0080
        };

        int bandHeight = size / bandCount;
        data.m_aRects.Reserve(bandCount * 5);
        for (int i = 0; i < bandCount; i++)
        {
            data.m_aRects.Insert(i % data.m_aPalette.Count());
            data.m_aRects.Insert(0);
            data.m_aRects.Insert(i * bandHeight);
            data.m_aRects.Insert(size);
            data.m_aRects.Insert(bandHeight);
        }
        return data;
    }
}

//------------------------------------------------------------------------------------------------
// Renders AG0_TDLPhotoData to a CanvasWidget using batched draw commands
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoRenderer
{
    protected CanvasWidget m_wCanvas;
    protected ref AG0_TDLPhotoData m_PhotoData;
    protected ref array<ref CanvasWidgetCommand> m_aDrawCommands = {};
    
    protected float m_fCanvasWidth;
    protected float m_fCanvasHeight;
    protected float m_fPixelSizeX;             // image-pixel → canvas-unit scale, X axis
    protected float m_fPixelSizeY;             // image-pixel → canvas-unit scale, Y axis
    protected float m_fOffsetX;
    protected float m_fOffsetY;

    protected AG0_TDLPhotoFitMode m_eFitMode = AG0_TDLPhotoFitMode.CONTAIN;

    protected int m_iCommandCount;
    protected int m_iVertexCount;

    // --- incremental rendering state ---
    //
    // For images with many rects (real photos at 32+ colors easily hit
    // 50K+), building draw commands inline blocks the main thread for
    // hundreds of ms. Instead we kick off a coroutine-style state machine:
    // each frame consumes m_iIncRectsPerBatch rects from the queue, then
    // re-schedules itself via CallqueueCallLater. The previous frame's
    // draw commands stay live until the new batch completes, so visually
    // it's an atomic swap with no flicker — just a slight delay.
    //
    // Threshold is the rect count above which we switch to incremental.
    // Tunable per-component if a small high-priority surface needs the
    // sync path even for big content.
    protected int m_iIncRectsPerBatch = 2000;
    protected int m_iIncThreshold     = 5000;
    protected bool m_bIncActive;
    protected int m_iIncCursor;
    protected int m_iIncVertexCount;
    protected ref array<int> m_aIncRectsSnapshot;       // snapshot of m_PhotoData.m_aRects at kickoff
    protected ref array<int> m_aIncPaletteSnapshot;     // snapshot of m_PhotoData.m_aPalette
    protected ref array<ref array<float>> m_aIncVerts;
    protected ref array<ref array<int>>   m_aIncIdx;
    protected ref array<int>              m_aIncQuadCounts;
    protected ref array<ref CanvasWidgetCommand> m_aIncCommands;

    //------------------------------------------------------------------------------------------------
    //! Change the aspect-fit policy. Has no effect until the next SetPhotoData
    //! (or until ApplyFitMode() is called). Defaults to CONTAIN.
    void SetFitMode(AG0_TDLPhotoFitMode mode)
    {
        m_eFitMode = mode;
        if (m_PhotoData)
            ApplyFitMode();
    }

    //------------------------------------------------------------------------------------------------
    AG0_TDLPhotoFitMode GetFitMode() { return m_eFitMode; }
    
    //------------------------------------------------------------------------------------------------
    bool Init(CanvasWidget canvas)
    {
        if (!canvas)
            return false;

        m_wCanvas = canvas;
        // GetScreenSize returns actual visual pixels — the same shape AG0_TDLMapView uses
        // (line 118 of AG0_TDLMapView.c). GetSizeInUnits returns the canvas's internal
        // coordinate space (typically 1000x1000 regardless of layout-assigned dims), which
        // makes draw commands compute against the wrong scale and overflow the visual area.
        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);

        Print(string.Format("[TDLPhotoRenderer] Canvas size: %1x%2", m_fCanvasWidth, m_fCanvasHeight), LogLevel.DEBUG);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetPhotoData(AG0_TDLPhotoData data)
    {
        m_PhotoData = data;
        if (!data)
            return;

        ApplyFitMode();

        Print(string.Format("[TDLPhotoRenderer] Photo %1x%2, %3 colors, pixel size: %4x%5 (fit=%6)",
            data.m_iWidth, data.m_iHeight, data.m_aPalette.Count(),
            m_fPixelSizeX, m_fPixelSizeY,
            typename.EnumToString(AG0_TDLPhotoFitMode, m_eFitMode)), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    //! Recompute m_fPixelSizeX/Y and the centering offsets from the current
    //! canvas size, photo dimensions, and fit mode. Called from SetPhotoData
    //! and whenever the fit mode changes at runtime.
    protected void ApplyFitMode()
    {
        if (!m_PhotoData || m_PhotoData.m_iWidth <= 0 || m_PhotoData.m_iHeight <= 0)
            return;

        float scaleX = m_fCanvasWidth  / m_PhotoData.m_iWidth;
        float scaleY = m_fCanvasHeight / m_PhotoData.m_iHeight;

        switch (m_eFitMode)
        {
            case AG0_TDLPhotoFitMode.CONTAIN:
                // Preserve aspect, shrink to fit — smaller axis dominates.
                // Letterboxes (offsets ≥ 0).
                m_fPixelSizeX = Math.Min(scaleX, scaleY);
                m_fPixelSizeY = m_fPixelSizeX;
                break;

            case AG0_TDLPhotoFitMode.COVER:
                // Preserve aspect, grow to fill — larger axis dominates.
                // Overflows the canvas on one axis (offsets ≤ 0); CanvasWidget
                // clips to its own bounds so the overflow is hidden.
                m_fPixelSizeX = Math.Max(scaleX, scaleY);
                m_fPixelSizeY = m_fPixelSizeX;
                break;

            case AG0_TDLPhotoFitMode.FILL:
                // Independent per-axis scale. Distorts the image but fills
                // the canvas exactly.
                m_fPixelSizeX = scaleX;
                m_fPixelSizeY = scaleY;
                break;
        }

        float imageWidth  = m_PhotoData.m_iWidth  * m_fPixelSizeX;
        float imageHeight = m_PhotoData.m_iHeight * m_fPixelSizeY;
        m_fOffsetX = (m_fCanvasWidth  - imageWidth)  * 0.5;
        m_fOffsetY = (m_fCanvasHeight - imageHeight) * 0.5;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Picks the best render path the current m_PhotoData supports:
    //!   1. m_aRects populated  -> DrawRects              (1 quad per rect)
    //!   2. m_aPixels populated -> DrawPixelsSinglePass   (1 quad per horizontal run)
    //!   3. neither             -> no-op
    //! The original DrawBatchedByColor is kept for reference/back-compat but is
    //! never selected here because DrawPixelsSinglePass strictly dominates it.
    void Draw()
    {
        if (!m_wCanvas || !m_PhotoData)
            return;

        // Re-read screen size — canvas may have been resized between SetPhotoData and Draw
        // (e.g. layout pass after a SizeLayoutWidget Min/Max change). Same call as Init.
        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);
        ApplyFitMode();

        // Incremental path keeps the previous frame's commands live until
        // the new batch completes, so we DON'T clear m_aDrawCommands here.
        // Sync paths clear right before they refill.
        if (m_PhotoData.HasRects() && m_PhotoData.GetRectCount() > m_iIncThreshold)
        {
            BeginIncrementalDraw();
            return;
        }

        // Cancel any in-flight incremental draw — a sync render supersedes it.
        if (m_bIncActive)
            CancelIncrementalDraw();

        m_aDrawCommands.Clear();
        m_iCommandCount = 0;
        m_iVertexCount = 0;

        if (m_PhotoData.HasRects())
        {
            DrawRects();
        }
        else if (m_PhotoData.m_aPixels.Count() > 0)
        {
            DrawPixelsSinglePass();
        }

        m_wCanvas.SetDrawCommands(m_aDrawCommands);

        Print(string.Format("[TDLPhotoRenderer] %1 commands, %2 vertices (sync)", m_iCommandCount, m_iVertexCount), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    //! Tune the incremental thresholds. `rectsPerBatch` is how many rects
    //! to process per frame; `threshold` is the rect count at which Draw()
    //! switches from sync to incremental. Set threshold to a very large
    //! number to effectively disable incremental.
    void SetIncrementalParams(int rectsPerBatch, int threshold)
    {
        m_iIncRectsPerBatch = Math.Max(1, rectsPerBatch);
        m_iIncThreshold     = Math.Max(0, threshold);
    }

    //------------------------------------------------------------------------------------------------
    //! Initialises incremental-render state and schedules the first batch.
    //! Snapshots the rect/palette arrays by reference so SetPhotoData()
    //! mid-render doesn't corrupt the in-flight image.
    protected void BeginIncrementalDraw()
    {
        // Replace any existing in-flight render with a fresh one.
        if (m_bIncActive)
            CancelIncrementalDraw();

        int paletteSize = m_PhotoData.m_aPalette.Count();
        if (paletteSize == 0)
            return;

        m_aIncRectsSnapshot   = m_PhotoData.m_aRects;
        m_aIncPaletteSnapshot = m_PhotoData.m_aPalette;
        m_iIncCursor          = 0;
        m_iIncVertexCount     = 0;

        m_aIncVerts      = new array<ref array<float>>();
        m_aIncIdx        = new array<ref array<int>>();
        m_aIncQuadCounts = new array<int>();
        m_aIncCommands   = new array<ref CanvasWidgetCommand>();
        m_aIncVerts.Resize(paletteSize);
        m_aIncIdx.Resize(paletteSize);
        m_aIncQuadCounts.Resize(paletteSize);
        for (int p = 0; p < paletteSize; p++)
        {
            // Pre-Reserve to the per-flush cap (399 quads = 3192 floats / 2394 ints).
            // Eliminates almost all reallocations during Insert.
            array<float> vb = new array<float>();
            array<int>   ib = new array<int>();
            vb.Reserve(399 * 8);
            ib.Reserve(399 * 6);
            m_aIncVerts[p] = vb;
            m_aIncIdx[p]   = ib;
            m_aIncQuadCounts[p] = 0;
        }

        m_bIncActive = true;
        Print(string.Format("[TDLPhotoRenderer] Incremental draw: %1 rects @ %2/frame",
            m_aIncRectsSnapshot.Count() / 5, m_iIncRectsPerBatch), LogLevel.DEBUG);

        GetGame().GetCallqueue().CallLater(ProcessIncrementalBatch, 0, false);
    }

    //------------------------------------------------------------------------------------------------
    //! One frame's worth of rect emission. Re-schedules itself until the
    //! snapshot is exhausted, then calls FinalizeIncrementalDraw().
    protected void ProcessIncrementalBatch()
    {
        if (!m_bIncActive)
            return;  // a CancelIncrementalDraw or new BeginIncrementalDraw superseded us

        int totalRects = m_aIncRectsSnapshot.Count() / 5;
        int paletteSize = m_aIncPaletteSnapshot.Count();
        int end = Math.Min(m_iIncCursor + m_iIncRectsPerBatch, totalRects);

        for (int r = m_iIncCursor; r < end; r++)
        {
            int ri = r * 5;
            int c  = m_aIncRectsSnapshot[ri + 0];
            int rx = m_aIncRectsSnapshot[ri + 1];
            int ry = m_aIncRectsSnapshot[ri + 2];
            int rw = m_aIncRectsSnapshot[ri + 3];
            int rh = m_aIncRectsSnapshot[ri + 4];

            if (c < 0 || c >= paletteSize || rw <= 0 || rh <= 0)
                continue;

            EmitQuadIncremental(c, rx, ry, rw, rh);
        }
        m_iIncCursor = end;

        if (m_iIncCursor >= totalRects)
        {
            FinalizeIncrementalDraw();
        }
        else
        {
            GetGame().GetCallqueue().CallLater(ProcessIncrementalBatch, 0, false);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Variant of EmitQuadToBucket that writes into the incremental
    //! state fields and accumulates finished commands in m_aIncCommands.
    protected void EmitQuadIncremental(int c, int cellX, int cellY, int cellW, int cellH)
    {
        float px = m_fOffsetX + cellX * m_fPixelSizeX;
        float py = m_fOffsetY + cellY * m_fPixelSizeY;
        float pw = cellW * m_fPixelSizeX;
        float ph = cellH * m_fPixelSizeY;

        array<float> verts = m_aIncVerts[c];
        array<int>   idxs  = m_aIncIdx[c];
        int qc = m_aIncQuadCounts[c];
        int baseVert = qc * 4;

        verts.Insert(px);       verts.Insert(py);
        verts.Insert(px + pw);  verts.Insert(py);
        verts.Insert(px + pw);  verts.Insert(py + ph);
        verts.Insert(px);       verts.Insert(py + ph);

        idxs.Insert(baseVert + 0); idxs.Insert(baseVert + 1); idxs.Insert(baseVert + 2);
        idxs.Insert(baseVert + 0); idxs.Insert(baseVert + 2); idxs.Insert(baseVert + 3);

        qc = qc + 1;
        m_aIncQuadCounts[c] = qc;

        m_iIncVertexCount = m_iIncVertexCount + 4;

        if (qc >= 399)
        {
            TriMeshDrawCommand cmd = new TriMeshDrawCommand();
            cmd.m_iColor    = m_aIncPaletteSnapshot[c];
            cmd.m_Vertices  = verts;
            cmd.m_Indices   = idxs;
            m_aIncCommands.Insert(cmd);

            array<float> vb = new array<float>();
            array<int>   ib = new array<int>();
            vb.Reserve(399 * 8);
            ib.Reserve(399 * 6);
            m_aIncVerts[c] = vb;
            m_aIncIdx[c]   = ib;
            m_aIncQuadCounts[c] = 0;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Flush remaining buckets, atomically swap m_aDrawCommands, push to
    //! the canvas. Clears all incremental state.
    protected void FinalizeIncrementalDraw()
    {
        int paletteSize = m_aIncPaletteSnapshot.Count();
        for (int p = 0; p < paletteSize; p++)
        {
            if (m_aIncQuadCounts[p] > 0)
            {
                TriMeshDrawCommand cmd = new TriMeshDrawCommand();
                cmd.m_iColor    = m_aIncPaletteSnapshot[p];
                cmd.m_Vertices  = m_aIncVerts[p];
                cmd.m_Indices   = m_aIncIdx[p];
                m_aIncCommands.Insert(cmd);
            }
        }

        // Atomic swap. Old commands stay live up to this point.
        m_aDrawCommands.Clear();
        int n = m_aIncCommands.Count();
        m_iCommandCount = n;
        m_iVertexCount  = m_iIncVertexCount;
        for (int i = 0; i < n; i++)
        {
            m_aDrawCommands.Insert(m_aIncCommands[i]);
        }
        m_wCanvas.SetDrawCommands(m_aDrawCommands);

        Print(string.Format("[TDLPhotoRenderer] %1 commands, %2 vertices (incremental)",
            m_iCommandCount, m_iVertexCount), LogLevel.NORMAL);

        ClearIncrementalState();
    }

    //------------------------------------------------------------------------------------------------
    //! Abandon an in-flight incremental render without flushing. Pending
    //! ProcessIncrementalBatch calls will see m_bIncActive=false and bail.
    protected void CancelIncrementalDraw()
    {
        ClearIncrementalState();
    }

    //------------------------------------------------------------------------------------------------
    protected void ClearIncrementalState()
    {
        m_bIncActive          = false;
        m_iIncCursor          = 0;
        m_iIncVertexCount     = 0;
        m_aIncRectsSnapshot   = null;
        m_aIncPaletteSnapshot = null;
        m_aIncVerts           = null;
        m_aIncIdx             = null;
        m_aIncQuadCounts      = null;
        m_aIncCommands        = null;
    }

    //------------------------------------------------------------------------------------------------
    //! Server-coalesced rectangle path. Iterates m_aRects once, buckets quads
    //! by palette color into per-color vert/index arrays, and flushes a
    //! TriMeshDrawCommand per bucket (splitting at 399 quads to stay under
    //! the 2400-index limit). One draw call per color group, not per pixel.
    protected void DrawRects()
    {
        int rectCount = m_PhotoData.GetRectCount();
        int paletteSize = m_PhotoData.m_aPalette.Count();
        if (rectCount == 0 || paletteSize == 0)
            return;

        // Pre-sized per-color buckets (paletteSize is small — up to 256 — and
        // indexing by int is cheaper than a map lookup per rect).
        ref array<ref array<float>> vertsByColor = new array<ref array<float>>();
        ref array<ref array<int>>   idxByColor   = new array<ref array<int>>();
        array<int> quadCountByColor = {};
        vertsByColor.Resize(paletteSize);
        idxByColor.Resize(paletteSize);
        quadCountByColor.Resize(paletteSize);
        for (int p = 0; p < paletteSize; p++)
        {
            vertsByColor[p] = new array<float>();
            idxByColor[p]   = new array<int>();
            quadCountByColor[p] = 0;
        }

        for (int r = 0; r < rectCount; r++)
        {
            int ri = r * 5;
            int c  = m_PhotoData.m_aRects[ri + 0];
            int rx = m_PhotoData.m_aRects[ri + 1];
            int ry = m_PhotoData.m_aRects[ri + 2];
            int rw = m_PhotoData.m_aRects[ri + 3];
            int rh = m_PhotoData.m_aRects[ri + 4];

            if (c < 0 || c >= paletteSize || rw <= 0 || rh <= 0)
                continue;

            EmitQuadToBucket(c, rx, ry, rw, rh,
                vertsByColor, idxByColor, quadCountByColor);
        }

        // Flush any remaining quads in each bucket.
        for (int pf = 0; pf < paletteSize; pf++)
        {
            if (quadCountByColor[pf] > 0)
                EmitTriMeshCommand(m_PhotoData.m_aPalette[pf], vertsByColor[pf], idxByColor[pf]);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Pixel path with inline horizontal RLE. One pass over pixels, merges
    //! adjacent same-color pixels in each row into a single wide quad before
    //! bucketing by color. Strictly faster than DrawBatchedByColor:
    //!   * O(W*H) instead of O(W*H*P) match scans
    //!   * typical 3-50x quad reduction depending on content
    //! Still slower than server-coalesced DrawRects, but requires no wire
    //! format changes — drop-in improvement.
    protected void DrawPixelsSinglePass()
    {
        int W = m_PhotoData.m_iWidth;
        int H = m_PhotoData.m_iHeight;
        int paletteSize = m_PhotoData.m_aPalette.Count();
        if (W <= 0 || H <= 0 || paletteSize == 0)
            return;

        ref array<ref array<float>> vertsByColor = new array<ref array<float>>();
        ref array<ref array<int>>   idxByColor   = new array<ref array<int>>();
        array<int> quadCountByColor = {};
        vertsByColor.Resize(paletteSize);
        idxByColor.Resize(paletteSize);
        quadCountByColor.Resize(paletteSize);
        for (int p = 0; p < paletteSize; p++)
        {
            vertsByColor[p] = new array<float>();
            idxByColor[p]   = new array<int>();
            quadCountByColor[p] = 0;
        }

        for (int y = 0; y < H; y++)
        {
            int rowBase = y * W;
            int x = 0;
            while (x < W)
            {
                int c = m_PhotoData.m_aPixels[rowBase + x];
                int x2 = x + 1;
                while (x2 < W && m_PhotoData.m_aPixels[rowBase + x2] == c)
                    x2++;

                if (c >= 0 && c < paletteSize)
                    EmitQuadToBucket(c, x, y, x2 - x, 1,
                        vertsByColor, idxByColor, quadCountByColor);

                x = x2;
            }
        }

        for (int pf = 0; pf < paletteSize; pf++)
        {
            if (quadCountByColor[pf] > 0)
                EmitTriMeshCommand(m_PhotoData.m_aPalette[pf], vertsByColor[pf], idxByColor[pf]);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Append one quad (in image-space cell coords) to the per-color bucket.
    //! Flushes the bucket when it hits the 399-quad split threshold so no
    //! single TriMeshDrawCommand exceeds the 2400-index limit.
    protected void EmitQuadToBucket(int c, int cellX, int cellY, int cellW, int cellH,
        array<ref array<float>> vertsByColor,
        array<ref array<int>> idxByColor,
        array<int> quadCountByColor)
    {
        float px = m_fOffsetX + cellX * m_fPixelSizeX;
        float py = m_fOffsetY + cellY * m_fPixelSizeY;
        float pw = cellW * m_fPixelSizeX;
        float ph = cellH * m_fPixelSizeY;

        array<float> verts = vertsByColor[c];
        array<int>   idxs  = idxByColor[c];
        int qc = quadCountByColor[c];
        int baseVert = qc * 4;

        // Quad verts: TL, TR, BR, BL
        verts.Insert(px);       verts.Insert(py);
        verts.Insert(px + pw);  verts.Insert(py);
        verts.Insert(px + pw);  verts.Insert(py + ph);
        verts.Insert(px);       verts.Insert(py + ph);

        // Two triangles: TL-TR-BR, TL-BR-BL
        idxs.Insert(baseVert + 0); idxs.Insert(baseVert + 1); idxs.Insert(baseVert + 2);
        idxs.Insert(baseVert + 0); idxs.Insert(baseVert + 2); idxs.Insert(baseVert + 3);

        qc++;
        quadCountByColor[c] = qc;

        if (qc >= 399)
        {
            EmitTriMeshCommand(m_PhotoData.m_aPalette[c], verts, idxs);
            vertsByColor[c] = new array<float>();
            idxByColor[c]   = new array<int>();
            quadCountByColor[c] = 0;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawBatchedByColor()
    {
        int width = m_PhotoData.m_iWidth;
        int height = m_PhotoData.m_iHeight;
        int paletteSize = m_PhotoData.m_aPalette.Count();
        
        for (int colorIdx = 0; colorIdx < paletteSize; colorIdx++)
        {
            int color = m_PhotoData.m_aPalette[colorIdx];
            ref array<float> verts = {};
            ref array<int> indices = {};
            int quadCount = 0;
            
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    int pixelIdx = y * width + x;
                    if (m_PhotoData.m_aPixels[pixelIdx] != colorIdx)
                        continue;
                    
                    float px = m_fOffsetX + x * m_fPixelSizeX;
                    float py = m_fOffsetY + y * m_fPixelSizeY;

                    int baseVert = quadCount * 4;

                    // Quad verts: TL, TR, BR, BL
                    verts.Insert(px);
                    verts.Insert(py);
                    verts.Insert(px + m_fPixelSizeX);
                    verts.Insert(py);
                    verts.Insert(px + m_fPixelSizeX);
                    verts.Insert(py + m_fPixelSizeY);
                    verts.Insert(px);
                    verts.Insert(py + m_fPixelSizeY);
                    
                    // Two triangles: TL-TR-BR, TL-BR-BL
                    indices.Insert(baseVert + 0);
                    indices.Insert(baseVert + 1);
                    indices.Insert(baseVert + 2);
                    indices.Insert(baseVert + 0);
                    indices.Insert(baseVert + 2);
                    indices.Insert(baseVert + 3);
                    
                    quadCount++;
                    
                    // Split at 399 quads (2394 indices, under 2400 limit)
                    if (quadCount >= 399)
                    {
                        EmitTriMeshCommand(color, verts, indices);
                        verts = {};
                        indices = {};
                        quadCount = 0;
                    }
                }
            }
            
            if (verts.Count() > 0)
                EmitTriMeshCommand(color, verts, indices);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void EmitTriMeshCommand(int color, array<float> verts, array<int> indices)
    {
        if (verts.Count() < 8)
            return;
        
        TriMeshDrawCommand cmd = new TriMeshDrawCommand();
        cmd.m_iColor = color;
        cmd.m_Vertices = verts;
        cmd.m_Indices = indices;
        
        m_aDrawCommands.Insert(cmd);
        m_iCommandCount++;
        m_iVertexCount += verts.Count() / 2;
    }
    
    //------------------------------------------------------------------------------------------------
    int GetCommandCount() { return m_iCommandCount; }
    int GetVertexCount() { return m_iVertexCount; }
}

// AG0_TDLPhotoComponentClass, AG0_TDLPhotoComponent, and AG0_TDLPhotoAction were removed.
// The fetch + decode pipeline they owned now lives on AG0_TDLPhotoManager (see
// AG0_TDLPhotoManager.c). The HUD-attached presenter component is gone — image display
// is handled via the message UI, with a per-message AG0_TDLPhotoRenderer instantiated
// against the message's CanvasWidget.
