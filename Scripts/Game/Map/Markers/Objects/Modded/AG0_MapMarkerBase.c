modded class SCR_MapMarkerBase
{
    protected bool m_bIsTDLMarker = false;
    
    //------------------------------------------------------------------------------------------------
    override void OnCreateMarker(bool skipProfanityFilter = false)
    {
        super.OnCreateMarker(skipProfanityFilter);
        
        // Check if this is a TDL marker
        if (m_eType == SCR_EMapMarkerType.PLACED_CUSTOM && m_ConfigEntry)
        {
            SCR_MapMarkerEntryPlaced placedEntry = SCR_MapMarkerEntryPlaced.Cast(m_ConfigEntry);
            if (placedEntry)
            {
                ResourceName imageset, imagesetGlow;
                string quad;
                if (placedEntry.GetIconEntry(m_iIconEntry, imageset, imagesetGlow, quad))
                {
                    m_bIsTDLMarker = quad.Contains("tdl_");
                }
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
  	override bool OnUpdate(vector visibleMin = vector.Zero, vector visibleMax = vector.Zero)
	{
	    // TDL markers need connectivity check
	    if (m_bIsTDLMarker && m_iMarkerOwnerID > 0)  // Skip server markers (-1)
	    {
	        SCR_PlayerController localController = SCR_PlayerController.Cast(
	            GetGame().GetPlayerController()
	        );
	        
	        if (!localController)
	            return false;
	        
	        // Always show our own markers
	        if (m_iMarkerOwnerID == localController.GetPlayerId())
	            return super.OnUpdate(visibleMin, visibleMax);
	        
	        // Otherwise check connectivity
	        array<int> connectedPlayers = localController.GetTDLConnectedPlayers();
	        if (!connectedPlayers.Contains(m_iMarkerOwnerID))
	        {
	            SetUpdateDisabled(true);
	            return false;
	        }
	    }
	    
	    return super.OnUpdate(visibleMin, visibleMax);
	}
}