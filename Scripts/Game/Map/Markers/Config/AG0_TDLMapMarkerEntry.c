//------------------------------------------------------------------------------------------------
//! TDL Radio Map Marker Entry
//! Shows network members on the map based on connectivity data

modded enum SCR_EMapMarkerType
{
	TDL_RADIO	// TDL_DEVICE marker 
}

[BaseContainerProps(), SCR_MapMarkerTitle()]
class AG0_TDLMapMarkerEntry : SCR_MapMarkerEntryDynamic
{
    [Attribute("", UIWidgets.Object, "Visual configuration")]
    protected ref SCR_MarkerSimpleConfig m_EntryConfig;
	
	protected ref map<RplId, SCR_MapMarkerEntity> m_mRadioMarkers = new map<RplId, SCR_MapMarkerEntity>();

	void RegisterMarker(AG0_MapMarkerTDL marker, RplId radioRplId)
	{
	    m_mRadioMarkers.Insert(radioRplId, marker);
	}
	
	void UnregisterMarker(RplId radioRplId)
	{
	    m_mRadioMarkers.Remove(radioRplId);
	}
	
	void RefreshAllMarkerVisibility()
	{
	    SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!pc) return;
	    
	    foreach (RplId deviceId, SCR_MapMarkerEntity marker : m_mRadioMarkers)
	    {
	        AG0_MapMarkerTDL tdlMarker = AG0_MapMarkerTDL.Cast(marker);
	        if (!tdlMarker) continue;
	        
	        bool shouldShow = pc.CanSeeDevice(deviceId);
	        tdlMarker.SetLocalVisible(shouldShow);
	    }
	}
	
    //------------------------------------------------------------------------------------------------
    override SCR_EMapMarkerType GetMarkerType()
    {
        return SCR_EMapMarkerType.TDL_RADIO;
    }

    //------------------------------------------------------------------------------------------------
    // Server-side initialization - create markers for all TDL radios when they spawn
    override void InitServerLogic()
    {
        super.InitServerLogic();

        if (!Replication.IsServer())
            return;

        AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
        if (tdlSystem)
        {
            tdlSystem.RegisterMarkerCallback(this);
        }

		auto config = SCR_MapMarkerManagerComponent.GetInstance().GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.TDL_RADIO);

		// Compare with a known working type
		auto squadConfig = SCR_MapMarkerManagerComponent.GetInstance().GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.SQUAD_LEADER);
    }
    
    //------------------------------------------------------------------------------------------------
    // Called by TDL System when a radio is registered
    void OnDeviceRegistered(AG0_TDLDeviceComponent device)
	{
	    if (!device || !m_MarkerMgr) return;
	    
	    
	    // Create marker (same logic as OnRadioRegistered)
	    AG0_MapMarkerTDL tdlMarker = AG0_MapMarkerTDL.Cast(
	        m_MarkerMgr.InsertDynamicMarker(SCR_EMapMarkerType.TDL_RADIO, device.GetOwner()));
	    
	    if (!tdlMarker) return;
	    
	    tdlMarker.SetGlobalVisible(true);
	    tdlMarker.SetType(SCR_EMapMarkerType.TDL_RADIO);
	    
	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId != RplId.Invalid()) {
	        tdlMarker.SetDeviceRplId(deviceRplId);
	        RegisterMarker(tdlMarker, deviceRplId);
	    }
 	}
	
	void OnDeviceUnregistered(AG0_TDLDeviceComponent device)
	{
	    if (!device || !m_MarkerMgr) return;
	    
	    SCR_MapMarkerEntity markerEnt = m_MarkerMgr.GetDynamicMarkerByTarget(GetMarkerType(), device.GetOwner());
	    if (markerEnt)
	        m_MarkerMgr.RemoveDynamicMarker(markerEnt);
	}
    
    //------------------------------------------------------------------------------------------------
    override void InitClientSettingsDynamic(notnull SCR_MapMarkerEntity marker, notnull SCR_MapMarkerDynamicWComponent widgetComp)
    {
        super.InitClientSettingsDynamic(marker, widgetComp);

		auto config = SCR_MapMarkerManagerComponent.GetInstance().GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.TDL_RADIO);

        ResourceName imgset = "{B365115DCD2C393A}UI/Textures/Icons/icons_wrapperUI-64TDL.imageset";
		string icon = "tdl_device";
        m_EntryConfig.GetIconResource(imgset, icon);
        
        widgetComp.SetImage(imgset, icon);
        widgetComp.SetColor(m_EntryConfig.GetColor());
        widgetComp.SetText(m_EntryConfig.GetText());
    }
}