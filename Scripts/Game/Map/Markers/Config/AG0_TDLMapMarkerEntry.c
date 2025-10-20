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
	
    //------------------------------------------------------------------------------------------------
    // Define our unique marker type for TDL radios
    override SCR_EMapMarkerType GetMarkerType()
    {
        return SCR_EMapMarkerType.TDL_RADIO; // You'll need to add this to your EMapMarkerType enum
    }
    
    //------------------------------------------------------------------------------------------------
    // Server-side initialization - create markers for all TDL radios when they spawn
    override void InitServerLogic()
    {
        super.InitServerLogic();
        
        // We should only create markers on the server
        if (!Replication.IsServer())
            return;
            
        // Register to AG0_TDLSystem for radio registration events
        AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
        if (tdlSystem)
        {
			//Print("AG0_TDLMapMarkerEntry: System exists and registering marker callback so that TDLRadioComps are registered here.", LogLevel.NORMAL);
            // You'll need to add these registration methods to AG0_TDLSystem
            tdlSystem.RegisterMarkerCallback(this);
        }
		
		auto config = SCR_MapMarkerManagerComponent.GetInstance().GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.TDL_RADIO);
		//Print(string.Format("AG0_TDLMapMarkerEntry: Config for TDL_RADIO exists: %1", config != null), LogLevel.NORMAL);
		
		// Compare with a known working type
		auto squadConfig = SCR_MapMarkerManagerComponent.GetInstance().GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.SQUAD_LEADER);
		//Print(string.Format("AG0_TDLMapMarkerEntry: Config for SQUAD_LEADER exists: %1", squadConfig != null), LogLevel.NORMAL);
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
    // Configure the marker appearance
    override void InitClientSettingsDynamic(notnull SCR_MapMarkerEntity marker, notnull SCR_MapMarkerDynamicWComponent widgetComp)
    {
		//Print("AG0_TDLMapMarkerEntry: CLIENT initializing marker display", LogLevel.NORMAL);
        super.InitClientSettingsDynamic(marker, widgetComp);
        
		auto config = SCR_MapMarkerManagerComponent.GetInstance().GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.TDL_RADIO);
		//Print(string.Format("AG0_TDLMapMarkerEntry: Config for TDL_RADIO exists: %1", config != null), LogLevel.NORMAL);
		
        ResourceName imgset = "{B365115DCD2C393A}UI/Textures/Icons/icons_wrapperUI-64TDL.imageset";  // Use our imageset
		string icon = "tdl_device"; //define icon manually
        m_EntryConfig.GetIconResource(imgset, icon);
        
        widgetComp.SetImage(imgset, icon);
        widgetComp.SetColor(m_EntryConfig.GetColor());
        widgetComp.SetText(m_EntryConfig.GetText());
    }
}