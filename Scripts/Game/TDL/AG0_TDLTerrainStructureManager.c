//------------------------------------------------------------------------------------------------
// AG0_TDLTerrainStructureManager.c
// Streamed terrain structure (building footprint) store for the TDL map.
//
// Server flow:
//   AG0_TDLApiManager fetches /api/mod/terrain/structures, hands the JSON body to
//   ParseColumnarPayload(). The raw JSON is also retained verbatim so it can be
//   redistributed to clients via RPC without a second encode pass.
//
// Client flow:
//   AG0_PlayerController_TDL receives the raw JSON over RPC, calls
//   ParseColumnarPayload() locally, and the map view reads the expanded records
//   directly via GetStructures().
//
// Wire format (from app/lib/terrain-structures-mod.ts, mode=rect):
//   {
//     v: 1, mode: "rect", hash: "<opaque>",
//     prefabs: [...string], types: [...string],
//     n: <int>,
//     x[n], z[n], r[n], h[n], t[n], p[n], w[n], d[n]
//   }
//
// x, z, t, p are delta-encoded — first value absolute, every subsequent value is
// the difference from the previous logical value. Running sum recovers absolutes.
// r, h, w, d are NOT delta-encoded.
//
// r is in radians. The web layer normalizes degree-valued source data into
// radians on the server side, so the mod can always treat r as radians.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! One building footprint, expanded from the columnar payload.
//! Fields mirror the per-row data from the wire format, kept in API-native units
//! (radians for rotation, full width/depth in meters) so renderers can choose
//! how to interpret them. Type / prefab are stored as indices; resolve through
//! the manager's string tables when needed.
//------------------------------------------------------------------------------------------------
class AG0_TDLTerrainStructureRecord
{
    float m_fCenterX;       // World X (meters)
    float m_fCenterZ;       // World Z (meters)
    float m_fRotation;      // Radians (CCW), per the web spec
    float m_fHeight;        // Building height (meters)
    float m_fWidth;         // Oriented-rect width (meters), rect mode only
    float m_fDepth;         // Oriented-rect depth (meters), rect mode only
    int   m_iTypeIndex;     // Index into manager's m_aTypes
    int   m_iPrefabIndex;   // Index into manager's m_aPrefabs
}

//------------------------------------------------------------------------------------------------
//! Stores a parsed structures dataset for a world load.
//! Single-instance on the server (owned by AG0_TDLApiManager) and per-client on
//! the player controller. The raw JSON received from /terrain/structures is
//! retained on the server so it can be forwarded to clients without re-encoding.
//------------------------------------------------------------------------------------------------
class AG0_TDLTerrainStructureManager
{
    // Format version we support. The web spec promises this is always 1 today.
    protected static const int SUPPORTED_VERSION = 1;

    // Expanded per-building rows — populated by ParseColumnarPayload()
    protected ref array<ref AG0_TDLTerrainStructureRecord> m_aStructures = {};

    // Shared string tables (indices in records refer into these)
    protected ref array<string> m_aPrefabs = {};
    protected ref array<string> m_aTypes = {};

    // Wire format echoes
    protected int m_iVersion;          // Always 1 currently
    protected string m_sMode;          // "rect" — only mode requested by mod today
    protected string m_sLastSyncHash;  // Opaque token, send back as ?since=

    // Server-side: keep the raw JSON so we can forward to clients verbatim
    protected string m_sLastRawJson;

    //------------------------------------------------------------------------------------------------
    void AG0_TDLTerrainStructureManager()
    {
        m_aStructures = new array<ref AG0_TDLTerrainStructureRecord>();
        m_aPrefabs = new array<string>();
        m_aTypes = new array<string>();
    }

    //------------------------------------------------------------------------------------------------
    //! Parse a columnar response body.
    //! @param jsonBody Full JSON string from a 200 response.
    //! @return Number of structures parsed (0 on parse failure or empty payload).
    int ParseColumnarPayload(string jsonBody)
    {
        if (jsonBody.IsEmpty())
        {
            Print("[TDL_STRUCTURES] ParseColumnarPayload: empty body", LogLevel.DEBUG);
            return 0;
        }

        SCR_JsonLoadContext json = new SCR_JsonLoadContext();
        if (!json.ImportFromString(jsonBody))
        {
            Print("[TDL_STRUCTURES] ParseColumnarPayload: invalid JSON", LogLevel.WARNING);
            return 0;
        }

        // --- Header ---
        int v = 0;
        json.ReadValue("v", v);
        if (v != SUPPORTED_VERSION)
        {
            Print(string.Format("[TDL_STRUCTURES] Unsupported wire version: %1 (expected %2)",
                v, SUPPORTED_VERSION), LogLevel.WARNING);
            return 0;
        }

        string mode;
        json.ReadValue("mode", mode);
        if (mode != "rect")
        {
            // The mod only requests rect today; if the server returns lossless
            // (mode echo will say so) we'd need an 8-float-per-building decoder.
            Print(string.Format("[TDL_STRUCTURES] Unsupported mode '%1' — expected 'rect'", mode), LogLevel.WARNING);
            return 0;
        }

        string hash;
        json.ReadValue("hash", hash);

        int n = 0;
        json.ReadValue("n", n);
        if (n <= 0)
        {
            // Valid empty dataset — clear and accept the hash.
            m_iVersion = v;
            m_sMode = mode;
            m_sLastSyncHash = hash;
            m_sLastRawJson = jsonBody;
            m_aStructures.Clear();
            m_aPrefabs.Clear();
            m_aTypes.Clear();
            Print("[TDL_STRUCTURES] Parsed empty dataset (n=0)", LogLevel.DEBUG);
            return 0;
        }

        // --- String tables ---
        array<string> prefabs = {};
        json.ReadValue("prefabs", prefabs);

        array<string> types = {};
        json.ReadValue("types", types);

        // --- Columnar arrays ---
        array<float> x = {};
        json.ReadValue("x", x);
        array<float> z = {};
        json.ReadValue("z", z);
        array<float> r = {};
        json.ReadValue("r", r);
        array<float> h = {};
        json.ReadValue("h", h);
        array<int> t = {};
        json.ReadValue("t", t);
        array<int> p = {};
        json.ReadValue("p", p);
        array<float> w = {};
        json.ReadValue("w", w);
        array<float> d = {};
        json.ReadValue("d", d);

        // Sanity: every column must have length n.
        if (x.Count() != n || z.Count() != n || r.Count() != n || h.Count() != n
            || t.Count() != n || p.Count() != n || w.Count() != n || d.Count() != n)
        {
            Print(string.Format(
                "[TDL_STRUCTURES] Column length mismatch (n=%1, x=%2 z=%3 r=%4 h=%5 t=%6 p=%7 w=%8 d=%9)",
                n, x.Count(), z.Count(), r.Count(), h.Count(),
                t.Count(), p.Count(), w.Count(), d.Count()), LogLevel.WARNING);
            return 0;
        }

        // --- Expand delta encoding for x, z, t, p (running sum) ---
        // Note: subscript-increment (array[i]++) does not compile in Enfusion;
        // explicit assignment is required.
        for (int i = 1; i < n; i = i + 1)
        {
            x[i] = x[i] + x[i - 1];
            z[i] = z[i] + z[i - 1];
            t[i] = t[i] + t[i - 1];
            p[i] = p[i] + p[i - 1];
        }

        // --- Materialize records ---
        m_aStructures.Clear();
        m_aStructures.Reserve(n);
        for (int j = 0; j < n; j = j + 1)
        {
            AG0_TDLTerrainStructureRecord rec = new AG0_TDLTerrainStructureRecord();
            rec.m_fCenterX = x[j];
            rec.m_fCenterZ = z[j];
            rec.m_fRotation = r[j];
            rec.m_fHeight = h[j];
            rec.m_fWidth = w[j];
            rec.m_fDepth = d[j];
            rec.m_iTypeIndex = t[j];
            rec.m_iPrefabIndex = p[j];
            m_aStructures.Insert(rec);
        }

        // --- Commit shared tables and metadata ---
        m_aPrefabs = prefabs;
        m_aTypes = types;
        m_iVersion = v;
        m_sMode = mode;
        m_sLastSyncHash = hash;
        m_sLastRawJson = jsonBody;

        Print(string.Format("[TDL_STRUCTURES] Parsed %1 structures (mode=%2, hash=%3, prefabs=%4, types=%5)",
            n, mode, hash, prefabs.Count(), types.Count()), LogLevel.DEBUG);

        return n;
    }

    //------------------------------------------------------------------------------------------------
    //! Drop all stored structures and clear the sync hash.
    //! Use when the world changes or the operator disables the feature.
    void Clear()
    {
        m_aStructures.Clear();
        m_aPrefabs.Clear();
        m_aTypes.Clear();
        m_iVersion = 0;
        m_sMode = string.Empty;
        m_sLastSyncHash = string.Empty;
        m_sLastRawJson = string.Empty;
    }

    //------------------------------------------------------------------------------------------------
    int GetCount()
    {
        return m_aStructures.Count();
    }

    int GetVersion()
    {
        return m_iVersion;
    }

    string GetMode()
    {
        return m_sMode;
    }

    string GetLastSyncHash()
    {
        return m_sLastSyncHash;
    }

    string GetLastRawJson()
    {
        return m_sLastRawJson;
    }

    //------------------------------------------------------------------------------------------------
    //! Direct access for renderers. Returned array is owned by the manager —
    //! treat as read-only. Stable across calls until the next ParseColumnarPayload.
    array<ref AG0_TDLTerrainStructureRecord> GetStructures()
    {
        return m_aStructures;
    }

    //------------------------------------------------------------------------------------------------
    //! Resolve a prefab table index to the prefab path (with leading {GUID} stripped per spec).
    //! Returns empty if the index is out of range.
    string GetPrefabName(int idx)
    {
        if (idx < 0 || idx >= m_aPrefabs.Count())
            return string.Empty;
        return m_aPrefabs[idx];
    }

    //------------------------------------------------------------------------------------------------
    //! Resolve a type table index to the lowercased type string (e.g. "house", "fortress").
    //! Returns empty if the index is out of range.
    string GetTypeName(int idx)
    {
        if (idx < 0 || idx >= m_aTypes.Count())
            return string.Empty;
        return m_aTypes[idx];
    }
}
