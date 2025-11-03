modded class SCR_PlayerController
{
    // Client-side visibility tracking for map markers
    protected ref array<RplId> m_aVisibleTDLDevices = {};
    protected float m_fTDLUpdateTimer = 0;
    protected const float TDL_UPDATE_INTERVAL = 1.0;
    
    override void OnUpdate(float timeSlice)
    {
        super.OnUpdate(timeSlice);
        
        if (m_bIsLocalPlayerController)
        {
            UpdateTDLNetworkState(timeSlice);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Client-side visibility aggregation (for map markers)
    //------------------------------------------------------------------------------------------------
    protected void UpdateTDLNetworkState(float timeSlice)
    {
        m_fTDLUpdateTimer += timeSlice;
        if (m_fTDLUpdateTimer < TDL_UPDATE_INTERVAL) return;
        
        m_fTDLUpdateTimer = 0;
        
        // Get devices from TDLController
        AG0_TDLController controller = AG0_TDLController.Cast(
            GetGame().GetWorld().GetSystems().FindMyController(AG0_TDLController)
        );
		Print(controller, LogLevel.WARNING);
        if (!controller) return;
        
        array<AG0_TDLDeviceComponent> playerDevices = controller.GetPlayerTDLDevices();
        
        // Aggregate visible devices from all player's TDL devices
        array<RplId> newVisibleDevices = {};
        
        foreach (AG0_TDLDeviceComponent device : playerDevices)
        {
            if (!device.IsInNetwork()) continue;
            
            array<RplId> connectedDevices = device.GetConnectedMembers();
            
            foreach (RplId deviceId : connectedDevices)
            {
                if (newVisibleDevices.Find(deviceId) == -1)
                    newVisibleDevices.Insert(deviceId);
            }
        }
        
        // Update if changed
		if (!RplIdArraysEqual(newVisibleDevices, m_aVisibleTDLDevices))
		{
		    m_aVisibleTDLDevices = newVisibleDevices;
		    
		    // Notify marker system
		    SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
		    if (markerMgr)
		    {
		        AG0_TDLMapMarkerEntry entry = AG0_TDLMapMarkerEntry.Cast(
		            markerMgr.GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.TDL_RADIO)
		        );
		        if (entry)
		            entry.RefreshAllMarkerVisibility();
		    }
		}
    }
    
    protected bool RplIdArraysEqual(array<RplId> a, array<RplId> b)
    {
        if (a.Count() != b.Count()) return false;
        foreach (RplId id : a)
            if (b.Find(id) == -1) return false;
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    // Public API for map markers
    //------------------------------------------------------------------------------------------------
    bool CanSeeDevice(RplId deviceId)
    {
        return m_aVisibleTDLDevices.Contains(deviceId);
    }
}