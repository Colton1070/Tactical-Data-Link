//------------------------------------------------------------------------------------------------
// AG0_TDLTerrainRoadManager.c
// Streamed terrain road network store for the TDL map.
//
// Mirrors AG0_TDLTerrainStructureManager. Differences:
//   * Variable-length features: each road is a polyline of N points.
//     Wire format uses a per-feature `len[]` column to slice the flat
//     point arrays (`x[]` and `z[]`) in feature order.
//   * No prefab string table — only `types` (highway/paved/road/trail).
//
// Wire format (from app/lib/terrain-roads-mod.ts):
//   {
//     v: 1, hash: "<opaque>",
//     types: [...string],
//     n: <featureCount>, m: <totalPointCount>,
//     t[n], w[n], pr[n], len[n],   // per-feature
//     x[m], z[m]                    // per-point, concatenated in feature order
//   }
//
// Delta-encoded (running-sum on read): t, x, z.
// Not delta-encoded:                   w, pr, len.
//
// Same float-decimal contract as structures: every value in x/z/w must have
// an explicit decimal point in the JSON, otherwise SCR_JsonLoadContext's
// array<float> parser stops at the first integer-shaped token. Encoder
// (terrain-roads-mod.ts) is responsible for that — the mod just trusts it
// and column-length-checks defensively.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One road segment, expanded from the columnar payload.
//! Geometry is a polyline in world XZ; vertices stored as a flat
//! [x0, z0, x1, z1, ...] array for cheap iteration.
//------------------------------------------------------------------------------------------------
class AG0_TDLTerrainRoadFeature
{
    int   m_iTypeIndex;             // Index into manager's m_aTypes
    float m_fWidth;                  // Road width in meters
    int   m_iPriority;              // 1=trail, 2=paved/road, 3=highway
    ref array<float> m_aPoints;     // [x0, z0, x1, z1, ...] in world meters

    // World-space AABB, precomputed at parse time and used by the map renderer
    // for cheap per-feature viewport rejection (no WorldToScreen needed).
    float m_fMinX;
    float m_fMaxX;
    float m_fMinZ;
    float m_fMaxZ;

    void AG0_TDLTerrainRoadFeature()
    {
        m_aPoints = {};
    }

    //------------------------------------------------------------------------------------------------
    //! Recompute the world-space AABB from m_aPoints. Call after the points
    //! array is fully populated.
    void UpdateAABB()
    {
        int n = m_aPoints.Count();
        if (n < 2)
        {
            m_fMinX = 0; m_fMaxX = 0;
            m_fMinZ = 0; m_fMaxZ = 0;
            return;
        }

        m_fMinX = m_aPoints[0]; m_fMaxX = m_aPoints[0];
        m_fMinZ = m_aPoints[1]; m_fMaxZ = m_aPoints[1];
        for (int i = 2; i + 1 < n; i = i + 2)
        {
            float px = m_aPoints[i];
            float pz = m_aPoints[i + 1];
            if (px < m_fMinX) m_fMinX = px;
            if (px > m_fMaxX) m_fMaxX = px;
            if (pz < m_fMinZ) m_fMinZ = pz;
            if (pz > m_fMaxZ) m_fMaxZ = pz;
        }
    }
}

//------------------------------------------------------------------------------------------------
//! Stores a parsed road network for a world load.
//! Single-instance on the server (owned by AG0_TDLApiManager) and per-client
//! on the player controller. Raw JSON retained server-side for chunked RPC
//! redistribution without re-encoding.
//------------------------------------------------------------------------------------------------
class AG0_TDLTerrainRoadManager
{
    protected static const int SUPPORTED_VERSION = 1;

    protected ref array<ref AG0_TDLTerrainRoadFeature> m_aFeatures = {};
    protected ref array<string> m_aTypes = {};

    protected int m_iVersion;
    protected string m_sLastSyncHash;
    protected string m_sLastRawJson;

    //------------------------------------------------------------------------------------------------
    void AG0_TDLTerrainRoadManager()
    {
        m_aFeatures = new array<ref AG0_TDLTerrainRoadFeature>();
        m_aTypes = new array<string>();
    }

    //------------------------------------------------------------------------------------------------
    //! Parse a columnar road payload.
    //! @return Number of features parsed (0 on failure or empty payload).
    int ParseColumnarPayload(string jsonBody)
    {
        if (jsonBody.IsEmpty())
        {
            Print("[TDL_ROADS] ParseColumnarPayload: empty body", LogLevel.DEBUG);
            return 0;
        }

        SCR_JsonLoadContext json = new SCR_JsonLoadContext();
        if (!json.ImportFromString(jsonBody))
        {
            int bodyLen = jsonBody.Length();
            string head = jsonBody.Substring(0, Math.Min(64, bodyLen));
            string tail;
            if (bodyLen > 64)
                tail = jsonBody.Substring(Math.Max(0, bodyLen - 64), Math.Min(64, bodyLen));
            else
                tail = string.Empty;

            Print(string.Format(
                "[TDL_ROADS] ParseColumnarPayload: invalid JSON (len=%1)\n  head: %2\n  tail: %3",
                bodyLen, head, tail), LogLevel.WARNING);
            return 0;
        }

        // --- Header ---
        int v = 0;
        json.ReadValue("v", v);
        if (v != SUPPORTED_VERSION)
        {
            Print(string.Format("[TDL_ROADS] Unsupported wire version: %1 (expected %2)",
                v, SUPPORTED_VERSION), LogLevel.WARNING);
            return 0;
        }

        string hash;
        json.ReadValue("hash", hash);

        int n = 0;
        int m = 0;
        json.ReadValue("n", n);
        json.ReadValue("m", m);

        if (n <= 0 || m <= 0)
        {
            // Valid empty dataset — accept the hash and clear state.
            m_iVersion = v;
            m_sLastSyncHash = hash;
            m_sLastRawJson = jsonBody;
            m_aFeatures.Clear();
            m_aTypes.Clear();
            Print("[TDL_ROADS] Parsed empty dataset (n=0)", LogLevel.DEBUG);
            return 0;
        }

        // --- String tables ---
        array<string> types = {};
        json.ReadValue("types", types);

        // --- Per-feature columns (length n) ---
        array<int> t = {};
        json.ReadValue("t", t);
        array<float> w = {};
        json.ReadValue("w", w);
        array<int> pr = {};
        json.ReadValue("pr", pr);
        array<int> len = {};
        json.ReadValue("len", len);

        // --- Per-point columns (length m) ---
        array<float> x = {};
        json.ReadValue("x", x);
        array<float> z = {};
        json.ReadValue("z", z);

        // Sanity: per-feature columns equal n; per-point columns equal m.
        if (t.Count() != n || w.Count() != n || pr.Count() != n || len.Count() != n)
        {
            Print(string.Format(
                "[TDL_ROADS] Per-feature column length mismatch (n=%1, t=%2 w=%3 pr=%4 len=%5)",
                n, t.Count(), w.Count(), pr.Count(), len.Count()), LogLevel.WARNING);
            return 0;
        }
        if (x.Count() != m || z.Count() != m)
        {
            Print(string.Format(
                "[TDL_ROADS] Per-point column length mismatch (m=%1, x=%2 z=%3)",
                m, x.Count(), z.Count()), LogLevel.WARNING);
            return 0;
        }

        // Verify sum(len) == m before we trust the slice walk.
        int lenSum = 0;
        for (int li = 0; li < n; li = li + 1)
            lenSum = lenSum + len[li];
        if (lenSum != m)
        {
            Print(string.Format("[TDL_ROADS] sum(len)=%1 != m=%2 — payload inconsistent",
                lenSum, m), LogLevel.WARNING);
            return 0;
        }

        // --- Expand delta encoding (t, x, z) ---
        for (int i = 1; i < n; i = i + 1)
            t[i] = t[i] + t[i - 1];
        for (int j = 1; j < m; j = j + 1)
        {
            x[j] = x[j] + x[j - 1];
            z[j] = z[j] + z[j - 1];
        }

        // --- Materialize features by walking len[] ---
        m_aFeatures.Clear();
        m_aFeatures.Reserve(n);

        int offset = 0;
        for (int f = 0; f < n; f = f + 1)
        {
            int count = len[f];
            AG0_TDLTerrainRoadFeature feat = new AG0_TDLTerrainRoadFeature();
            feat.m_iTypeIndex = t[f];
            feat.m_fWidth = w[f];
            feat.m_iPriority = pr[f];
            feat.m_aPoints.Reserve(count * 2);

            for (int p = 0; p < count; p = p + 1)
            {
                feat.m_aPoints.Insert(x[offset + p]);
                feat.m_aPoints.Insert(z[offset + p]);
            }
            offset = offset + count;
            feat.UpdateAABB();
            m_aFeatures.Insert(feat);
        }

        // --- Commit ---
        m_aTypes = types;
        m_iVersion = v;
        m_sLastSyncHash = hash;
        m_sLastRawJson = jsonBody;

        Print(string.Format(
            "[TDL_ROADS] Parsed %1 features, %2 points (hash=%3, types=%4)",
            n, m, hash, types.Count()), LogLevel.DEBUG);

        return n;
    }

    //------------------------------------------------------------------------------------------------
    void Clear()
    {
        m_aFeatures.Clear();
        m_aTypes.Clear();
        m_iVersion = 0;
        m_sLastSyncHash = string.Empty;
        m_sLastRawJson = string.Empty;
    }

    //------------------------------------------------------------------------------------------------
    int GetCount()             { return m_aFeatures.Count(); }
    int GetVersion()           { return m_iVersion; }
    string GetLastSyncHash()   { return m_sLastSyncHash; }
    string GetLastRawJson()    { return m_sLastRawJson; }

    array<ref AG0_TDLTerrainRoadFeature> GetFeatures()
    {
        return m_aFeatures;
    }

    string GetTypeName(int idx)
    {
        if (idx < 0 || idx >= m_aTypes.Count())
            return string.Empty;
        return m_aTypes[idx];
    }
}
