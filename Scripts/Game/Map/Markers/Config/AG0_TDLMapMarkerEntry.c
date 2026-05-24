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
    }
    
    //------------------------------------------------------------------------------------------------
    // Called by TDL System when a radio is registered
    void OnDeviceRegistered(AG0_TDLDeviceComponent device)
	{
	    if (!device || !m_MarkerMgr) return;

	    AG0_MapMarkerTDL tdlMarker = AG0_MapMarkerTDL.Cast(
	        m_MarkerMgr.InsertDynamicMarker(SCR_EMapMarkerType.TDL_RADIO, device.GetOwner()));

	    if (!tdlMarker) return;

	    tdlMarker.SetGlobalVisible(true);
	    tdlMarker.SetType(SCR_EMapMarkerType.TDL_RADIO);

	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId != RplId.Invalid())
	    {
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

        // Defaults preserved for prefabs that don't author the per-device
        // marker attributes — they still get the legacy MPU5 icon and the
        // shared entry-config color.
        //
        // GetIconResource declares `out ResourceName imageset, out string imageQuad`
        // on SCR_MarkerSimpleConfig — out-params unconditionally overwrite,
        // so calling it directly on imgset/icon would blow away our hardcoded
        // fallbacks if the entry config doesn't have its own values authored.
        // Read into temps, then promote only when non-empty. Matches the
        // base-game pattern for optional config-driven asset resolution.
        ResourceName imgset = "{B365115DCD2C393A}UI/Textures/Icons/icons_wrapperUI-64TDL.imageset";
		string icon = "tdl_device";
        ResourceName cfgImgset;
        string cfgIcon;
        m_EntryConfig.GetIconResource(cfgImgset, cfgIcon);
        if (!cfgImgset.IsEmpty())
            imgset = cfgImgset;
        if (!cfgIcon.IsEmpty())
            icon = cfgIcon;
        Color color = m_EntryConfig.GetColor();

        // Per-device override: resolve the device this marker represents and
        // read its authored imageset / quad / color. Any field unset falls
        // through to the default above, so partial authoring works cleanly.
        AG0_MapMarkerTDL tdlMarker = AG0_MapMarkerTDL.Cast(marker);
        if (tdlMarker)
        {
            RplId deviceRplId = tdlMarker.GetDeviceRplId();
            if (deviceRplId != RplId.Invalid())
            {
                RplComponent rpl = RplComponent.Cast(Replication.FindItem(deviceRplId));
                if (rpl && rpl.GetEntity())
                {
                    AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
                        rpl.GetEntity().FindComponent(AG0_TDLDeviceComponent));
                    if (device)
                    {
                        ResourceName deviceImgset = device.GetMarkerImageset();
                        string deviceIcon = device.GetMarkerIconQuad();
                        if (!deviceImgset.IsEmpty())
                            imgset = deviceImgset;
                        if (!deviceIcon.IsEmpty())
                            icon = deviceIcon;
                        if (device.ShouldOverrideMarkerColor())
                            color = device.GetMarkerColor();
                    }
                }
            }
        }

        widgetComp.SetImage(imgset, icon);
        widgetComp.SetColor(color);
        widgetComp.SetText(m_EntryConfig.GetText());
    }
}