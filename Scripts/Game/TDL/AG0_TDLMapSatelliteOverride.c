//------------------------------------------------------------------------------------------------
// AG0_TDLMapSatelliteOverride.c
// Client-side cache of the satellite raster the server told us to draw.
//
// The two authoritative sources — the server's api_config.json and the tdl-api map record —
// are both server-side, while every consumer of the raster (CanvasWidget.LoadTexture in the
// 2D view, the drape pass in the 3D view) is client-side. The server resolves the two into
// one string and pushes it down; this holder is where it lands.
//
// Static rather than a component because the value is per-world, not per-entity: the menu
// map, the world-space device screen and the 3D view are separate widget trees that must
// all draw the same picture, and threading one string through three ownership chains buys
// nothing over a world-scoped lookup.
//------------------------------------------------------------------------------------------------
class AG0_TDLMapSatelliteOverride
{
    protected static string s_sResourceName = string.Empty;

    //! Exposure trim in stops the 3D map applies for this world. Travels with the raster
    //! because it exists to compensate for that particular image's albedo.
    protected static float s_fExposureBias;

    //! Bumped on every accepted change so a view that already loaded a texture can notice a
    //! late arrival — the push routinely lands after the player has opened the map once.
    protected static int s_iRevision;

    //------------------------------------------------------------------------------------------------
    //! @param resourceName Server-resolved path, or empty to hand resolution back to the
    //!        client's own prefab/config chain.
    //! @param exposureBias Stops of trim for the 3D map; negative darkens, 0 means none.
    static void SetFromServer(string resourceName, float exposureBias)
    {
        string trimmed = resourceName;
        trimmed.TrimInPlace();

        if (trimmed == s_sResourceName && exposureBias == s_fExposureBias)
            return;

        s_sResourceName = trimmed;
        s_fExposureBias = exposureBias;
        s_iRevision = s_iRevision + 1;

        Print(string.Format(
            "[TDL_SATELLITE] Server satellite raster set to '%1', exposure bias %2 (rev %3)",
            s_sResourceName, s_fExposureBias, s_iRevision), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    static float GetExposureBias()
    {
        return s_fExposureBias;
    }

    //------------------------------------------------------------------------------------------------
    //! Returned as a plain string because that is what crossed the wire; callers that need a
    //! ResourceName assign it into one at the point of use.
    static string Get()
    {
        return s_sResourceName;
    }

    //------------------------------------------------------------------------------------------------
    static bool HasValue()
    {
        return !s_sResourceName.IsEmpty();
    }

    //------------------------------------------------------------------------------------------------
    static int GetRevision()
    {
        return s_iRevision;
    }
}
