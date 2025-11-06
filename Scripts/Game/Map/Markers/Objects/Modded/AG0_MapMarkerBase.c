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
	        SCR_PlayerController controller = SCR_PlayerController.Cast(
			    GetGame().GetPlayerController()
			);
	        
	        if (!controller)
	            return false;
	        
	        // Always show our own markers
	        if (m_iMarkerOwnerID == controller.GetPlayerId())
	            return super.OnUpdate(visibleMin, visibleMax);
	        
	        // Otherwise check connectivity
	        array<int> connectedPlayers = controller.GetTDLConnectedPlayers();
	        if (!connectedPlayers.Contains(m_iMarkerOwnerID))
	        {
	            SetUpdateDisabled(true);
	            return false;
	        }
	    }
	    
	    return super.OnUpdate(visibleMin, visibleMax);
	}
	
	//------------------------------------------------------------------------------------------------
    // Serialization pass-through methods (I've got NO additional data to store here...)
    //------------------------------------------------------------------------------------------------
    override static bool Extract(SCR_MapMarkerBase instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        return SCR_MapMarkerBase.Extract(instance, ctx, snapshot);
    }

    //------------------------------------------------------------------------------------------------
    override static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, SCR_MapMarkerBase instance)
    {
        return SCR_MapMarkerBase.Inject(snapshot, ctx, instance);
    }

    //------------------------------------------------------------------------------------------------
    override static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        SCR_MapMarkerBase.Encode(snapshot, ctx, packet);
    }

    //------------------------------------------------------------------------------------------------
    override static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        return SCR_MapMarkerBase.Decode(packet, ctx, snapshot);
    }

    //------------------------------------------------------------------------------------------------
    override static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return SCR_MapMarkerBase.SnapCompare(lhs, rhs, ctx);
    }

    //------------------------------------------------------------------------------------------------
    override static bool PropCompare(SCR_MapMarkerBase instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        return SCR_MapMarkerBase.PropCompare(instance, snapshot, ctx);
    }
}