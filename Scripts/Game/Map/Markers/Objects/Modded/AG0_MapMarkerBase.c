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
    //! PUBLIC GETTERS - Used by AG0_TDLSystem for API marker sync
    //------------------------------------------------------------------------------------------------
    
    bool IsTDLMarker()
    {
        if (m_bIsTDLMarker)
            return true;
        
        // On dedicated server, OnCreateMarker() never runs so m_bIsTDLMarker is never set.
        // Resolve on-demand from global config instead.
        if (m_eType != SCR_EMapMarkerType.PLACED_CUSTOM)
            return false;
        
        string quad = ResolveTDLQuad();
        if (!quad.IsEmpty() && quad.Contains("tdl_"))
        {
            m_bIsTDLMarker = true;
            return true;
        }
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    string GetTDLMarkerQuad()
    {
        if (m_eType != SCR_EMapMarkerType.PLACED_CUSTOM)
            return string.Empty;
        
        return ResolveTDLQuad();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Resolve quad name from icon entry index
    //! Uses cached m_ConfigEntry on clients, falls back to global config on dedicated server
    protected string ResolveTDLQuad()
    {
        SCR_MapMarkerEntryPlaced placedEntry;
        
        // Client path — m_ConfigEntry is set during OnCreateMarker()
        if (m_ConfigEntry)
        {
            placedEntry = SCR_MapMarkerEntryPlaced.Cast(m_ConfigEntry);
        }
        else
        {
            // Dedicated server path — look up from marker manager's global config
            SCR_MapMarkerManagerComponent mgr = SCR_MapMarkerManagerComponent.GetInstance();
            if (!mgr || !mgr.GetMarkerConfig())
                return string.Empty;
            
            placedEntry = SCR_MapMarkerEntryPlaced.Cast(
                mgr.GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_CUSTOM));
        }
        
        if (!placedEntry)
            return string.Empty;
        
        ResourceName imageset, imagesetGlow;
        string quad;
        if (placedEntry.GetIconEntry(m_iIconEntry, imageset, imagesetGlow, quad))
            return quad;
        
        return string.Empty;
    }
    
    //------------------------------------------------------------------------------------------------
  	override bool OnUpdate(vector visibleMin = vector.Zero, vector visibleMax = vector.Zero)
	{
	    // CRITICAL: Only apply TDL logic to actual TDL markers
	    if (!m_bIsTDLMarker)
	        return super.OnUpdate(visibleMin, visibleMax);
	    
	    // TDL markers need connectivity check, but only for player-owned markers
	    if (m_iMarkerOwnerID > 0)  // Skip server markers (-1) and invalid (0)
	    {
	        SCR_PlayerController controller = SCR_PlayerController.Cast(
	            GetGame().GetPlayerController()
	        );
	        
	        // On dedicated servers, GetPlayerController might be null
	        if (!controller)
	            return super.OnUpdate(visibleMin, visibleMax);
	        
	        // Always show our own markers
	        if (m_iMarkerOwnerID == controller.GetPlayerId())
	            return super.OnUpdate(visibleMin, visibleMax);
	        
	        // Check connectivity for other players' markers
	        array<int> connectedPlayers = controller.GetTDLConnectedPlayers();
	        bool isConnected = connectedPlayers.Contains(m_iMarkerOwnerID);
	        
	        if (!isConnected)
	        {
	            if (m_wRoot)
	                m_wRoot.SetVisible(false);
	            return false;
	        }
	        else
	        {
	            if (m_wRoot)
	                m_wRoot.SetVisible(true);
	        }
	    }
	    
	    return super.OnUpdate(visibleMin, visibleMax);
	}
	
	//------------------------------------------------------------------------------------------------
    override static bool Extract(SCR_MapMarkerBase instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        return super.Extract(instance, ctx, snapshot);
    }

    //------------------------------------------------------------------------------------------------
    override static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, SCR_MapMarkerBase instance)
    {
        return super.Inject(snapshot, ctx, instance);
    }

    //------------------------------------------------------------------------------------------------
    override static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        super.Encode(snapshot, ctx, packet);
    }

    //------------------------------------------------------------------------------------------------
    override static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        return super.Decode(packet, ctx, snapshot);
    }

    //------------------------------------------------------------------------------------------------
    override static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return super.SnapCompare(lhs, rhs, ctx);
    }

    //------------------------------------------------------------------------------------------------
    override static bool PropCompare(SCR_MapMarkerBase instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        return super.PropCompare(instance, snapshot, ctx);
    }
}