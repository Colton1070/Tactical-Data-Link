//------------------------------------------------------------------------------------------------
// AG0_TDLTerrainHeightmapManager.c
// Streamed terrain elevation grid for the TDL 3D view.
//
// Server flow:
//   AG0_TDLApiManager fetches /api/mod/terrain/heightmap, hands the JSON body to
//   ParseColumnarPayload(). The raw JSON is retained verbatim so it can be
//   redistributed to clients via RPC without a second encode pass.
//
// Client flow:
//   AG0_PlayerController_TDL reassembles the chunked payload, calls
//   ParseColumnarPayload() locally, and mesh builders sample via SampleWorld().
//
// Wire format (from app/lib/terrain-heightmap-mod.ts):
//   {
//     v: 1, hash: "<opaque>",
//     n: <cols>, m: <rows>,
//     csx: <float>, csz: <float>,   // metres between adjacent samples
//     ox: <float>,  oz: <float>,    // world X/Z of column 0 / row 0
//     lo: <float>,  hi: <float>,    // retained height range, metres
//     nd: <int>,                    // samples substituted for missing source data
//     h: [ n*m ints ]
//   }
//
// h is delta-encoded DECIMETRE integers, row-major, SOUTH-TO-NORTH (row 0 sits at
// oz, the minimum world Z). Decimetre integers rather than floats because terrain is
// spatially smooth: the delta stream runs 1-3 characters per sample where floats run
// 3-4x that, and every kilobyte saved is one fewer ~6 KB RPC chunk per client.
// South-to-north because that matches Reforger world Z, so no consumer has to flip.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Parsed elevation grid for one world.
//! Heights are held in decimetres as ints for the same reason they ship that way —
//! a 513x513 grid is 263k samples, and float storage costs a megabyte for precision
//! no terrain mesh can express.
//------------------------------------------------------------------------------------------------
class AG0_TDLTerrainHeightmapManager
{
    protected static const int SUPPORTED_VERSION = 1;

    // Guards against a malformed or hostile payload allocating unbounded memory.
    // 513x513 is the API's own ceiling; anything larger is a corrupt header.
    protected static const int MAX_SAMPLES = 263169;

    protected ref array<int> m_aHeightsDm = {};

    protected int m_iCols;
    protected int m_iRows;
    protected float m_fCellX;
    protected float m_fCellZ;
    protected float m_fOriginX;
    protected float m_fOriginZ;
    protected float m_fMinHeight;
    protected float m_fMaxHeight;
    protected int m_iNoDataCount;

    protected int m_iVersion;
    protected string m_sLastSyncHash;
    protected string m_sLastRawJson;

    //------------------------------------------------------------------------------------------------
    void AG0_TDLTerrainHeightmapManager()
    {
        m_aHeightsDm = new array<int>();
    }

    //------------------------------------------------------------------------------------------------
    //! Parse a columnar response body.
    //! @return Number of samples parsed (0 on parse failure or empty payload).
    int ParseColumnarPayload(string jsonBody)
    {
        if (jsonBody.IsEmpty())
        {
            Print("[TDL_HEIGHTMAP] ParseColumnarPayload: empty body", LogLevel.DEBUG);
            return 0;
        }

        JsonLoadContext json = new JsonLoadContext();
        if (!json.LoadFromString(jsonBody))
        {
            // Head/tail sample makes it obvious whether we got gzip bytes (no leading
            // '{'), an HTML error page, or a payload truncated by chunk reassembly.
            int bodyLen = jsonBody.Length();
            string head = jsonBody.Substring(0, Math.Min(64, bodyLen));
            string tail;
            if (bodyLen > 64)
                tail = jsonBody.Substring(Math.Max(0, bodyLen - 64), Math.Min(64, bodyLen));
            else
                tail = string.Empty;

            Print(string.Format(
                "[TDL_HEIGHTMAP] ParseColumnarPayload: invalid JSON (len=%1)\n  head: %2\n  tail: %3",
                bodyLen, head, tail), LogLevel.WARNING);
            return 0;
        }

        int v = 0;
        json.ReadValue("v", v);
        if (v != SUPPORTED_VERSION)
        {
            Print(string.Format("[TDL_HEIGHTMAP] Unsupported wire version: %1 (expected %2)",
                v, SUPPORTED_VERSION), LogLevel.WARNING);
            return 0;
        }

        string hash;
        json.ReadValue("hash", hash);

        int cols = 0;
        json.ReadValue("n", cols);
        int rows = 0;
        json.ReadValue("m", rows);

        if (cols < 2 || rows < 2)
        {
            Print(string.Format("[TDL_HEIGHTMAP] Degenerate grid (n=%1, m=%2)", cols, rows),
                LogLevel.WARNING);
            return 0;
        }

        int expected = cols * rows;
        if (expected > MAX_SAMPLES)
        {
            Print(string.Format("[TDL_HEIGHTMAP] Grid too large: %1 samples (cap %2)",
                expected, MAX_SAMPLES), LogLevel.WARNING);
            return 0;
        }

        float cellX = 0;
        json.ReadValue("csx", cellX);
        float cellZ = 0;
        json.ReadValue("csz", cellZ);
        if (cellX <= 0 || cellZ <= 0)
        {
            Print(string.Format("[TDL_HEIGHTMAP] Invalid cell size (csx=%1, csz=%2)", cellX, cellZ),
                LogLevel.WARNING);
            return 0;
        }

        float originX = 0;
        json.ReadValue("ox", originX);
        float originZ = 0;
        json.ReadValue("oz", originZ);
        float lo = 0;
        json.ReadValue("lo", lo);
        float hi = 0;
        json.ReadValue("hi", hi);
        int noData = 0;
        json.ReadValue("nd", noData);

        array<int> h = {};
        json.ReadValue("h", h);

        if (h.Count() != expected)
        {
            Print(string.Format("[TDL_HEIGHTMAP] Sample count mismatch (expected %1, got %2)",
                expected, h.Count()), LogLevel.WARNING);
            return 0;
        }

        // Subscript-increment (array[i]++) does not compile in Enfusion, so the running
        // sum that expands the delta encoding is written out explicitly.
        for (int i = 1; i < expected; i = i + 1)
        {
            h[i] = h[i] + h[i - 1];
        }

        m_aHeightsDm = h;
        m_iCols = cols;
        m_iRows = rows;
        m_fCellX = cellX;
        m_fCellZ = cellZ;
        m_fOriginX = originX;
        m_fOriginZ = originZ;
        m_fMinHeight = lo;
        m_fMaxHeight = hi;
        m_iNoDataCount = noData;
        m_iVersion = v;
        m_sLastSyncHash = hash;
        m_sLastRawJson = jsonBody;

        Print(string.Format(
            "[TDL_HEIGHTMAP] Parsed %1x%2 grid (cell %3x%4 m, range %5..%6 m, nodata=%7, hash=%8)",
            cols, rows, cellX, cellZ, lo, hi, noData, hash), LogLevel.DEBUG);

        return expected;
    }

    //------------------------------------------------------------------------------------------------
    void Clear()
    {
        m_aHeightsDm.Clear();
        m_iCols = 0;
        m_iRows = 0;
        m_fCellX = 0;
        m_fCellZ = 0;
        m_fOriginX = 0;
        m_fOriginZ = 0;
        m_fMinHeight = 0;
        m_fMaxHeight = 0;
        m_iNoDataCount = 0;
        m_iVersion = 0;
        m_sLastSyncHash = string.Empty;
        m_sLastRawJson = string.Empty;
    }

    //------------------------------------------------------------------------------------------------
    bool IsReady()
    {
        return m_iCols >= 2 && m_iRows >= 2 && m_aHeightsDm.Count() == m_iCols * m_iRows;
    }

    int GetCols() { return m_iCols; }
    int GetRows() { return m_iRows; }
    float GetCellSizeX() { return m_fCellX; }
    float GetCellSizeZ() { return m_fCellZ; }
    float GetOriginX() { return m_fOriginX; }
    float GetOriginZ() { return m_fOriginZ; }
    float GetMinHeight() { return m_fMinHeight; }
    float GetMaxHeight() { return m_fMaxHeight; }
    int GetNoDataCount() { return m_iNoDataCount; }
    int GetVersion() { return m_iVersion; }
    string GetLastSyncHash() { return m_sLastSyncHash; }
    string GetLastRawJson() { return m_sLastRawJson; }

    //------------------------------------------------------------------------------------------------
    //! World-space span covered by the grid, in metres.
    float GetSpanX() { return (m_iCols - 1) * m_fCellX; }
    float GetSpanZ() { return (m_iRows - 1) * m_fCellZ; }

    //------------------------------------------------------------------------------------------------
    //! Height at an exact grid node, metres. Out-of-range indices clamp to the edge
    //! rather than returning a sentinel: callers are mesh builders walking a lattice,
    //! and an edge clamp keeps the border row continuous instead of punching a hole.
    float GetHeightAt(int col, int row)
    {
        if (!IsReady())
            return 0;

        int c = col;
        if (c < 0)
            c = 0;
        if (c > m_iCols - 1)
            c = m_iCols - 1;

        int r = row;
        if (r < 0)
            r = 0;
        if (r > m_iRows - 1)
            r = m_iRows - 1;

        return m_aHeightsDm[r * m_iCols + c] * 0.1;
    }

    //------------------------------------------------------------------------------------------------
    //! Bilinear height at a world XZ position, metres.
    //! Interpolating rather than snapping to the nearest node matters because the grid
    //! is far coarser than the terrain: nearest-node sampling puts visible stair steps
    //! on any geometry seated against this surface.
    float SampleWorld(float worldX, float worldZ)
    {
        if (!IsReady())
            return 0;

        float fc = (worldX - m_fOriginX) / m_fCellX;
        float fr = (worldZ - m_fOriginZ) / m_fCellZ;

        int c0 = Math.Floor(fc);
        int r0 = Math.Floor(fr);

        float tx = fc - c0;
        float tz = fr - r0;

        // Clamping the fractional weights alongside the indices keeps samples outside
        // the grid pinned to the edge height instead of extrapolating off a cliff.
        if (c0 < 0)
        {
            c0 = 0;
            tx = 0;
        }
        if (c0 > m_iCols - 2)
        {
            c0 = m_iCols - 2;
            tx = 1;
        }
        if (r0 < 0)
        {
            r0 = 0;
            tz = 0;
        }
        if (r0 > m_iRows - 2)
        {
            r0 = m_iRows - 2;
            tz = 1;
        }

        float h00 = m_aHeightsDm[r0 * m_iCols + c0] * 0.1;
        float h10 = m_aHeightsDm[r0 * m_iCols + c0 + 1] * 0.1;
        float h01 = m_aHeightsDm[(r0 + 1) * m_iCols + c0] * 0.1;
        float h11 = m_aHeightsDm[(r0 + 1) * m_iCols + c0 + 1] * 0.1;

        float top = h00 + (h10 - h00) * tx;
        float bottom = h01 + (h11 - h01) * tx;
        return top + (bottom - top) * tz;
    }
}
