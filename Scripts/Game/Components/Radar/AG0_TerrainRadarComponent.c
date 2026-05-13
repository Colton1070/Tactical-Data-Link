//------------------------------------------------------------------------------------------------
// AG0_TerrainRadarComponent.c
// Terrain depth-raster on a CanvasWidget.
//
// Drops on any entity. On init, instantiates a layout via component attribute,
// finds a named CanvasWidget inside it, and on a fixed-rate timer fires an
// N×M grid of rays through the player camera's frustum.
//
// Tracing is sphere-marched against BaseWorld.GetSurfaceY (heightmap lookup),
// not full physics TraceMove. Each ray steps along its direction with an
// adaptive step size — large when high above terrain, small when close —
// and bisects on crossover for a clean hit fraction. This is roughly an
// order of magnitude cheaper than TraceMove, which is what makes higher
// resolutions viable.
//
// Two render modes share the same trace pass:
//   SQUARES   — flat-shaded rectangles per cell (V1 look). Greyscale by
//               hit distance, modulated by a directional slope shade so
//               ridges and valleys read clearly.
//   WIREFRAME — horizontal+vertical line segments connecting cell centers.
//               Each segment colored by the avg distance of its endpoints.
//               Naturally smooth, no per-cell snapping.
//
// Sky cells (ray reached max range without hitting) stay black.
//------------------------------------------------------------------------------------------------
enum AG0_ETerrainRadarMode
{
    SQUARES = 0,
    WIREFRAME = 1
}

[ComponentEditorProps(category: "GameScripted/TDL", description: "Terrain depth-raster on CanvasWidget. Sphere-marches GetSurfaceY against camera FOV; renders as squares or wireframe.")]
class AG0_TerrainRadarComponentClass : ScriptGameComponentClass {}

class AG0_TerrainRadarComponent : ScriptGameComponent
{
    // ============================================
    // ATTRIBUTES
    // ============================================

    [Attribute("", UIWidgets.ResourceNamePicker, "Layout containing the canvas widget", "layout")]
    protected ResourceName m_LayoutResource;

    [Attribute("RadarCanvas", UIWidgets.EditBox, "Name of the CanvasWidget inside the layout")]
    protected string m_sCanvasName;

    [Attribute("128", UIWidgets.EditBox, "Horizontal ray count")]
    protected int m_iRayCols;

    [Attribute("96", UIWidgets.EditBox, "Vertical ray count")]
    protected int m_iRayRows;

    [Attribute("60", UIWidgets.Slider, "Horizontal FOV in degrees", "10 170 1")]
    protected float m_fHFovDeg;

    [Attribute("45", UIWidgets.Slider, "Vertical FOV in degrees", "10 170 1")]
    protected float m_fVFovDeg;

    [Attribute("5000", UIWidgets.Slider, "Max trace range (meters)", "100 30000 100")]
    protected float m_fMaxRange;

    [Attribute("10", UIWidgets.Slider, "Render rate (Hz)", "1 60 1")]
    protected float m_fUpdateHz;

    [Attribute("1", UIWidgets.CheckBox, "Include ocean surface as a terrain hit (water shows as bright)")]
    protected bool m_bIncludeOcean;

    [Attribute("1.0", UIWidgets.Slider, "Brightness gamma — <1 brightens far terrain, >1 darkens it", "0.1 4.0 0.1")]
    protected float m_fGamma;

    [Attribute("0.6", UIWidgets.Slider, "Mix between distance shading and slope shading (0=pure distance, 1=pure slope-modulated)", "0 1 0.05")]
    protected float m_fSlopeMix;

    [Attribute("60", UIWidgets.Slider, "Slope shading strength (higher = stronger contrast on hills/valleys)", "0 500 5")]
    protected float m_fSlopeStrength;

    [Attribute("0.0", UIWidgets.Slider, "Edge enhancement (0=off, ~0.4 highlights ridgelines) — squares mode only", "0 1 0.05")]
    protected float m_fEdgeBoost;

    [Attribute("0", UIWidgets.ComboBox, "Render mode", "", ParamEnumArray.FromEnum(AG0_ETerrainRadarMode))]
    protected AG0_ETerrainRadarMode m_eRenderMode;

    [Attribute("60", UIWidgets.Slider, "Ray-march step budget (more = more accurate at long range, slower)", "10 200 1")]
    protected int m_iMaxRaySteps;

    [Attribute("0", UIWidgets.Slider, "Ocean level (Y) — terrain below this clamps up to surface if 'include ocean' is on", "-100 200 1")]
    protected float m_fOceanLevel;

    [Attribute("1.5", UIWidgets.Slider, "Wireframe line width (pixels) — wireframe mode only", "0.5 4 0.1")]
    protected float m_fWireframeLineWidth;

    [Attribute("3000", UIWidgets.Slider, "Max realistic terrain altitude (m). Rays whose lowest point is above this skip the march entirely. Bump up if traces miss tall terrain.", "200 8000 100")]
    protected float m_fMaxTerrainY;

    [Attribute("4", UIWidgets.Slider, "Temporal amortization: trace 1/N rows per tick (N=1 = full update each tick). Higher = lower per-tick stall but rolling-shutter on fast motion.", "1 8 1")]
    protected int m_iFrameSlices;

    // ============================================
    // STATE
    // ============================================

    protected Widget m_wRoot;
    protected CanvasWidget m_wCanvas;
    protected ref array<ref CanvasWidgetCommand> m_aDrawCommands = {};
    protected float m_fCanvasWidth;
    protected float m_fCanvasHeight;
    protected bool m_bSetupComplete;

    // ----- Persistent render buffers (allocated once in SetupCanvas) -----
    // Re-used across renders to avoid 60+ allocations per tick. Cleared at
    // the start of each Render(); their TriMeshDrawCommand references die
    // when m_aDrawCommands.Clear() is called, so it's safe to reset them.
    protected ref array<float>             m_aFractions;
    protected ref array<ref array<float>>  m_aVertsByBucket;
    protected ref array<ref array<int>>    m_aIdxByBucket;
    protected ref array<int>               m_aQuadCountByBucket;
    protected ref array<int>               m_aColorByBucket;

    // Rotates 0..m_iFrameSlices-1 each tick — selects which row stripe gets
    // re-traced. The rest of m_aFractions retains values from prior ticks.
    protected int m_iCurrentFrameSlice;

    // ----- Render constants -----
    // QUAD_SPLIT keeps each TriMeshDrawCommand under the engine's 2400-index
    // ceiling (399 quads × 6 indices = 2394).
    protected const int QUAD_SPLIT         = 399;
    protected const int BRIGHTNESS_BUCKETS = 32;

    // ============================================
    // LIFECYCLE
    // ============================================

    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);

        // Headless servers don't render — bail
        if (System.IsConsoleApp())
            return;

        SetEventMask(owner, EntityEvent.INIT);
    }

    //------------------------------------------------------------------------------------------------
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);

        // Bacon fix — skip prefab editor / non-runtime worlds
        if (owner.GetWorld() != GetGame().GetWorld())
            return;

        // Defer one tick so the workspace and CameraManager are ready
        GetGame().GetCallqueue().CallLater(SetupCanvas, 100, false);
    }

    //------------------------------------------------------------------------------------------------
    override void OnDelete(IEntity owner)
    {
        if (GetGame() && GetGame().GetCallqueue())
            GetGame().GetCallqueue().Remove(Render);

        if (m_wRoot)
        {
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
        }

        m_wCanvas = null;
        if (m_aDrawCommands)
            m_aDrawCommands.Clear();

        super.OnDelete(owner);
    }

    // ============================================
    // SETUP
    // ============================================

    //------------------------------------------------------------------------------------------------
    protected void SetupCanvas()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
        {
            Print("[TDL_TERRAIN_RADAR] FAIL: No workspace", LogLevel.ERROR);
            return;
        }

        if (m_LayoutResource.IsEmpty())
        {
            Print("[TDL_TERRAIN_RADAR] FAIL: Layout resource not set on component", LogLevel.ERROR);
            return;
        }

        m_wRoot = workspace.CreateWidgets(m_LayoutResource);
        if (!m_wRoot)
        {
            Print("[TDL_TERRAIN_RADAR] FAIL: CreateWidgets returned null", LogLevel.ERROR);
            return;
        }

        Widget canvasWidget = m_wRoot.FindAnyWidget(m_sCanvasName);
        if (!canvasWidget)
        {
            Print(string.Format("[TDL_TERRAIN_RADAR] FAIL: No widget named '%1' inside layout", m_sCanvasName), LogLevel.ERROR);
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
            return;
        }

        m_wCanvas = CanvasWidget.Cast(canvasWidget);
        if (!m_wCanvas)
        {
            Print(string.Format("[TDL_TERRAIN_RADAR] FAIL: Widget '%1' is not a CanvasWidget", m_sCanvasName), LogLevel.ERROR);
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
            return;
        }

        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);

        // Pre-allocate persistent render buffers — sized for the configured grid
        // and bucket count. Avoids ~65 array allocations per render tick.
        int cellCount = m_iRayCols * m_iRayRows;
        m_aFractions = new array<float>;
        m_aFractions.Resize(cellCount);
        // Start as all-sky so untraced rows stay black during the first
        // m_iFrameSlices ticks of warmup.
        for (int seed = 0; seed < cellCount; seed++)
            m_aFractions.Set(seed, 1.0);

        m_aVertsByBucket     = new array<ref array<float>>;
        m_aIdxByBucket       = new array<ref array<int>>;
        m_aQuadCountByBucket = new array<int>;
        m_aColorByBucket     = new array<int>;
        m_aVertsByBucket.Resize(BRIGHTNESS_BUCKETS);
        m_aIdxByBucket.Resize(BRIGHTNESS_BUCKETS);
        m_aQuadCountByBucket.Resize(BRIGHTNESS_BUCKETS);
        m_aColorByBucket.Resize(BRIGHTNESS_BUCKETS);

        for (int b = 0; b < BRIGHTNESS_BUCKETS; b++)
        {
            m_aVertsByBucket[b]     = new array<float>;
            m_aIdxByBucket[b]       = new array<int>;
            m_aQuadCountByBucket[b] = 0;

            // Bucket b's representative shade is the midpoint of its range
            float bShade = (b + 0.5) / BRIGHTNESS_BUCKETS;
            int g = Math.Round(bShade * 255);
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            m_aColorByBucket[b] = ARGB(255, g, g, g);
        }

        m_bSetupComplete = true;

        // Schedule periodic render (clamped to ~60 Hz floor for safety)
        float intervalMs = 1000.0 / m_fUpdateHz;
        if (intervalMs < 16.0)
            intervalMs = 16.0;

        GetGame().GetCallqueue().CallLater(Render, intervalMs, true);

        Print(string.Format("[TDL_TERRAIN_RADAR] Initialized: %1x%2 grid, FOV %3°x%4°, range %5m, %6 Hz",
            m_iRayCols, m_iRayRows, m_fHFovDeg, m_fVFovDeg, m_fMaxRange, m_fUpdateHz), LogLevel.NORMAL);
    }

    // ============================================
    // RENDER
    // ============================================

    //------------------------------------------------------------------------------------------------
    protected void Render()
    {
        if (!m_bSetupComplete || !m_wCanvas)
            return;

        // Refresh canvas size each tick — covers resize / RT-binding cases
        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);
        if (m_fCanvasWidth <= 0 || m_fCanvasHeight <= 0)
            return;

        // Camera transform via CameraManager (matches AG0_TDLWorldSpaceDisplay pattern)
        CameraManager camMgr = GetGame().GetCameraManager();
        if (!camMgr)
            return;

        CameraBase camera = camMgr.CurrentCamera();
        if (!camera)
            return;

        vector camMat[4];
        camera.GetTransform(camMat);

        vector camOrigin = camMat[3];
        vector camRight  = camMat[0]; // X axis
        vector camUp     = camMat[1]; // Y axis
        vector camFwd    = camMat[2]; // Z axis (forward)

        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return;

        // Bail early if attributes are nonsense
        if (m_iRayCols <= 0 || m_iRayRows <= 0)
            return;

        // Reset persistent buffers — m_aDrawCommands first so the
        // TriMeshDrawCommand references to the bucket arrays are dropped
        // before we clear the underlying arrays.
        m_aDrawCommands.Clear();
        ResetBuckets();

        float pxW = m_fCanvasWidth / m_iRayCols;
        float pxH = m_fCanvasHeight / m_iRayRows;
        float tanH = Math.Tan(m_fHFovDeg * 0.5 * Math.DEG2RAD);
        float tanV = Math.Tan(m_fVFovDeg * 0.5 * Math.DEG2RAD);

        // ----------------------------------------------------------------
        // PASS 1 — Sphere-march each ray against the terrain heightmap.
        // Temporal amortization: only re-trace rows where j % slices == current
        // slice. The rest of m_aFractions retains values from prior ticks. Bucket
        // build still rebuilds the full canvas, just from a partially-updated
        // depth grid.
        // ----------------------------------------------------------------
        int slices = m_iFrameSlices;
        if (slices < 1) slices = 1;
        int sliceIdx = m_iCurrentFrameSlice % slices;

        for (int j = 0; j < m_iRayRows; j++)
        {
            // Skip rows not in the current temporal slice
            if (slices > 1 && (j % slices) != sliceIdx)
                continue;

            float yN = 1.0 - (2.0 * (j + 0.5) / m_iRayRows); // +1 top, -1 bottom

            for (int i = 0; i < m_iRayCols; i++)
            {
                float xN = (2.0 * (i + 0.5) / m_iRayCols) - 1.0; // -1 left, +1 right

                vector dirCam = Vector(xN * tanH, yN * tanV, 1).Normalized();
                vector dirWorld = (camRight * dirCam[0] + camUp * dirCam[1] + camFwd * dirCam[2]).Normalized();

                float frac = MarchRay(world, camOrigin, dirWorld, m_fMaxRange);
                m_aFractions.Set(j * m_iRayCols + i, frac);
            }
        }

        m_iCurrentFrameSlice = (sliceIdx + 1) % slices;

        // ----------------------------------------------------------------
        // PASS 2 — Dispatch to the configured render mode.
        // Both modes consume m_aFractions and write into the bucket
        // batchers; only the geometry differs.
        // ----------------------------------------------------------------
        if (m_eRenderMode == AG0_ETerrainRadarMode.WIREFRAME)
            RenderWireframe(pxW, pxH);
        else
            RenderSquares(pxW, pxH);

        FlushBuckets();
        m_wCanvas.SetDrawCommands(m_aDrawCommands);
    }

    // ============================================
    // RAY MARCHING
    // ============================================

    //------------------------------------------------------------------------------------------------
    //! Sphere-march a ray against the terrain heightmap.
    //! @return Hit fraction 0..1, or 1.0 for no hit within budget.
    //!
    //! Adaptive step size: when high above terrain, step at most a fraction
    //! of altitude-above-terrain (provably can't intersect terrain in that
    //! step unless slope > 1:SAFE_FACTOR, which is sharper than any real
    //! terrain). When close, step shrinks to MIN_STEP. After a crossover
    //! we bisect for sub-step accuracy.
    //!
    //! Cost per ray: roughly 10–30 GetSurfaceY calls under typical TFR
    //! viewing geometry, vs 1 TraceMove. GetSurfaceY is ~50× cheaper, so
    //! net per-ray cost is well below TraceMove and scales much better.
    //------------------------------------------------------------------------------------------------
    protected float MarchRay(BaseWorld world, vector origin, vector dir, float maxRange)
    {
        const float MIN_STEP    = 1.5;   // never step less than this (m)
        const float MAX_STEP    = 250.0; // never step more than this (m)
        const float SAFE_FACTOR = 0.55;  // step ≤ alt × SAFE_FACTOR
        const float GROW        = 1.05;  // geometric growth floor
        const int   BISECT_ITERS = 6;    // 6 → 1/64 of last-step precision

        // Sky cull: if the ray's lowest point along [0..maxRange] is above
        // any plausible terrain, skip the march entirely. Cheap test that
        // wipes out 30-60% of rays for a typical TFR forward view.
        float lowestY;
        if (dir[1] >= 0)
            lowestY = origin[1];                      // ray going up — start is lowest
        else
            lowestY = origin[1] + dir[1] * maxRange;  // ray going down — end is lowest

        if (lowestY > m_fMaxTerrainY)
            return 1.0;

        // Big initial step: when high above terrain, sphere-march logic says we
        // can safely step alt × SAFE_FACTOR. Doing this once at the start saves
        // several wasted small steps before adaptive growth catches up.
        float startSurfY = world.GetSurfaceY(origin[0], origin[2]);
        if (m_bIncludeOcean && startSurfY < m_fOceanLevel)
            startSurfY = m_fOceanLevel;
        float startAlt = origin[1] - startSurfY;

        float t = 1.0;
        float step = MIN_STEP;
        if (startAlt > MIN_STEP)
        {
            float bigStep = startAlt * SAFE_FACTOR;
            if (bigStep > MAX_STEP) bigStep = MAX_STEP;
            if (bigStep > t)
            {
                t = bigStep;
                step = bigStep;
            }
        }

        float prevT = 0;

        for (int s = 0; s < m_iMaxRaySteps; s++)
        {
            if (t >= maxRange)
                return 1.0;

            vector p = origin + dir * t;
            float surfY = world.GetSurfaceY(p[0], p[2]);
            if (m_bIncludeOcean && surfY < m_fOceanLevel)
                surfY = m_fOceanLevel;

            float alt = p[1] - surfY;

            if (alt <= 0)
            {
                // Crossover — bisect within [prevT, t]
                float lo = prevT;
                float hi = t;
                for (int b = 0; b < BISECT_ITERS; b++)
                {
                    float mid = (lo + hi) * 0.5;
                    vector pm = origin + dir * mid;
                    float sy = world.GetSurfaceY(pm[0], pm[2]);
                    if (m_bIncludeOcean && sy < m_fOceanLevel)
                        sy = m_fOceanLevel;
                    if (pm[1] - sy > 0)
                        lo = mid;
                    else
                        hi = mid;
                }
                return ((lo + hi) * 0.5) / maxRange;
            }

            // Adaptive next step: max of geometric growth or safe altitude step
            float nextStep = step * GROW;
            float altStep = alt * SAFE_FACTOR;
            if (altStep > nextStep)
                nextStep = altStep;
            if (nextStep < MIN_STEP) nextStep = MIN_STEP;
            if (nextStep > MAX_STEP) nextStep = MAX_STEP;

            prevT = t;
            t = t + nextStep;
            step = nextStep;
        }

        return 1.0; // step budget exhausted — treat as sky
    }

    // ============================================
    // RENDER MODES
    // ============================================
    //
    // Both modes funnel through a brightness-bucketed batcher: each cell's
    // shade is quantized to one of BRIGHTNESS_BUCKETS levels, and quads are
    // appended to the bucket's vertex/index arrays. At the end we emit one
    // TriMeshDrawCommand per bucket — drastically fewer draw calls than
    // one-command-per-cell, which is the FPS killer at 128×96.
    //
    // Cap at QUAD_SPLIT quads per command keeps each TriMeshDrawCommand
    // under the engine's 2400-index limit (399 quads × 6 indices = 2394).
    // ============================================

    //------------------------------------------------------------------------------------------------
    //! Squares mode — every lit cell becomes a quad in its brightness bucket.
    //! Reads m_aFractions, writes into the persistent bucket buffers.
    //------------------------------------------------------------------------------------------------
    protected void RenderSquares(float pxW, float pxH)
    {
        for (int j = 0; j < m_iRayRows; j++)
        {
            for (int i = 0; i < m_iRayCols; i++)
            {
                float f = m_aFractions[j * m_iRayCols + i];
                if (f >= 1.0)
                    continue; // sky — no quad

                float brightness = ShadeCell(i, j, f);
                int bucket = BrightnessToBucket(brightness);

                EmitQuadToBucket(bucket, i * pxW, j * pxH, pxW, pxH);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Wireframe mode — each edge between adjacent cell centers becomes a thin
    //! axis-aligned quad in the brightness bucket batcher. One TriMeshDrawCommand
    //! per brightness level handles the whole wireframe regardless of segment count.
    //------------------------------------------------------------------------------------------------
    protected void RenderWireframe(float pxW, float pxH)
    {
        float halfW = m_fWireframeLineWidth * 0.5;

        // Horizontal segments — thin quads (segment-long × line-width-tall)
        for (int j = 0; j < m_iRayRows; j++)
        {
            for (int i = 0; i < m_iRayCols - 1; i++)
            {
                float fA = m_aFractions[j * m_iRayCols + i];
                float fB = m_aFractions[j * m_iRayCols + i + 1];
                if (fA >= 1.0 && fB >= 1.0)
                    continue;

                float f = ResolveSegmentFraction(fA, fB);
                float brightness = ShadeCell(i, j, f);
                int bucket = BrightnessToBucket(brightness);

                float x1 = (i + 0.5) * pxW;
                float x2 = (i + 1.5) * pxW;
                float yc = (j + 0.5) * pxH;

                EmitQuadToBucket(bucket, x1, yc - halfW, x2 - x1, m_fWireframeLineWidth);
            }
        }

        // Vertical segments — thin quads (line-width-wide × segment-long)
        for (int i2 = 0; i2 < m_iRayCols; i2++)
        {
            for (int j2 = 0; j2 < m_iRayRows - 1; j2++)
            {
                float fA = m_aFractions[j2 * m_iRayCols + i2];
                float fB = m_aFractions[(j2 + 1) * m_iRayCols + i2];
                if (fA >= 1.0 && fB >= 1.0)
                    continue;

                float f = ResolveSegmentFraction(fA, fB);
                float brightness = ShadeCell(i2, j2, f);
                int bucket = BrightnessToBucket(brightness);

                float xc = (i2 + 0.5) * pxW;
                float y1 = (j2 + 0.5) * pxH;
                float y2 = (j2 + 1.5) * pxH;

                EmitQuadToBucket(bucket, xc - halfW, y1, m_fWireframeLineWidth, y2 - y1);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    //! For a wire segment with possibly-sky endpoints, pick the fraction we'll
    //! shade with: the valid endpoint when one is sky, the average otherwise.
    //------------------------------------------------------------------------------------------------
    protected float ResolveSegmentFraction(float fA, float fB)
    {
        if (fA >= 1.0)      return fB;
        if (fB >= 1.0)      return fA;
        return (fA + fB) * 0.5;
    }

    // ============================================
    // BUCKETED TRI-MESH BATCHING
    // ============================================
    // Pattern lifted from AG0_TDLPhotoData.c — proven against the 2400-index
    // ceiling on TriMeshDrawCommand. Bucket arrays are persistent members:
    // ResetBuckets() clears them at the start of each render, EmitQuadToBucket
    // only allocates a fresh array on auto-flush at QUAD_SPLIT.

    //------------------------------------------------------------------------------------------------
    //! Reset all bucket vert/index arrays. Call after m_aDrawCommands.Clear()
    //! so the previous frame's TriMeshDrawCommand references are dropped first.
    //------------------------------------------------------------------------------------------------
    protected void ResetBuckets()
    {
        for (int b = 0; b < BRIGHTNESS_BUCKETS; b++)
        {
            m_aVertsByBucket[b].Clear();
            m_aIdxByBucket[b].Clear();
            m_aQuadCountByBucket[b] = 0;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected int BrightnessToBucket(float brightness)
    {
        int bucket = Math.Floor(brightness * BRIGHTNESS_BUCKETS);
        if (bucket < 0) bucket = 0;
        if (bucket >= BRIGHTNESS_BUCKETS) bucket = BRIGHTNESS_BUCKETS - 1;
        return bucket;
    }

    //------------------------------------------------------------------------------------------------
    //! Append one quad (in canvas pixels) to the named bucket. Auto-flushes
    //! and re-allocates the bucket's arrays if it would exceed QUAD_SPLIT.
    //------------------------------------------------------------------------------------------------
    protected void EmitQuadToBucket(int bucket, float x, float y, float w, float h)
    {
        array<float> verts = m_aVertsByBucket[bucket];
        array<int>   idxs  = m_aIdxByBucket[bucket];
        int qc = m_aQuadCountByBucket[bucket];
        int baseVert = qc * 4;

        // Quad verts: TL, TR, BR, BL
        verts.Insert(x);     verts.Insert(y);
        verts.Insert(x + w); verts.Insert(y);
        verts.Insert(x + w); verts.Insert(y + h);
        verts.Insert(x);     verts.Insert(y + h);

        // Two triangles: TL-TR-BR, TL-BR-BL
        idxs.Insert(baseVert + 0); idxs.Insert(baseVert + 1); idxs.Insert(baseVert + 2);
        idxs.Insert(baseVert + 0); idxs.Insert(baseVert + 2); idxs.Insert(baseVert + 3);

        qc++;
        m_aQuadCountByBucket[bucket] = qc;

        if (qc >= QUAD_SPLIT)
        {
            // Hand the filled arrays off to a TriMeshDrawCommand and start
            // fresh ones for this bucket. The command holds the references
            // alive until next render's m_aDrawCommands.Clear().
            EmitTriMeshCommand(m_aColorByBucket[bucket], verts, idxs);
            m_aVertsByBucket[bucket]    = new array<float>;
            m_aIdxByBucket[bucket]      = new array<int>;
            m_aQuadCountByBucket[bucket] = 0;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Flush any non-empty buckets at the end of a render pass.
    //------------------------------------------------------------------------------------------------
    protected void FlushBuckets()
    {
        for (int b = 0; b < BRIGHTNESS_BUCKETS; b++)
        {
            if (m_aQuadCountByBucket[b] > 0)
                EmitTriMeshCommand(m_aColorByBucket[b], m_aVertsByBucket[b], m_aIdxByBucket[b]);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void EmitTriMeshCommand(int color, array<float> verts, array<int> indices)
    {
        if (verts.Count() < 8)
            return;

        TriMeshDrawCommand cmd = new TriMeshDrawCommand();
        cmd.m_iColor    = color;
        cmd.m_Vertices  = verts;
        cmd.m_Indices   = indices;
        m_aDrawCommands.Insert(cmd);
    }

    // ============================================
    // SHADING
    // ============================================

    //------------------------------------------------------------------------------------------------
    //! Per-cell brightness from m_aFractions, shared by both render modes.
    //! Combines distance fog, directional slope shading (light from upper-left),
    //! and optional edge boost. Sky neighbors are clamped to current cell so
    //! silhouettes don't generate halos.
    //------------------------------------------------------------------------------------------------
    protected float ShadeCell(int i, int j, float f)
    {
        int iL = i - 1; if (iL < 0) iL = 0;
        int iR = i + 1; if (iR >= m_iRayCols) iR = m_iRayCols - 1;
        int jU = j - 1; if (jU < 0) jU = 0;
        int jD = j + 1; if (jD >= m_iRayRows) jD = m_iRayRows - 1;

        float fL = m_aFractions[j * m_iRayCols + iL];
        float fR = m_aFractions[j * m_iRayCols + iR];
        float fU = m_aFractions[jU * m_iRayCols + i];
        float fD = m_aFractions[jD * m_iRayCols + i];

        if (fL >= 1.0) fL = f;
        if (fR >= 1.0) fR = f;
        if (fU >= 1.0) fU = f;
        if (fD >= 1.0) fD = f;

        float distShade = Math.Pow(1.0 - f, m_fGamma);

        float slopeH = fR - fL;
        float slopeV = fD - fU;
        float slope  = slopeV + slopeH * 0.5;
        float slopeShade = 0.5 + slope * m_fSlopeStrength;
        if (slopeShade < 0) slopeShade = 0;
        if (slopeShade > 1) slopeShade = 1;

        float brightness = distShade * ((1.0 - m_fSlopeMix) + m_fSlopeMix * slopeShade * 2.0);

        if (m_fEdgeBoost > 0)
        {
            float neighborMean = (fL + fR + fU + fD) * 0.25;
            float edge = neighborMean - f;
            if (edge < 0) edge = -edge;
            brightness = brightness + edge * m_fSlopeStrength * m_fEdgeBoost;
        }

        if (brightness < 0) brightness = 0;
        if (brightness > 1) brightness = 1;
        return brightness;
    }
}
