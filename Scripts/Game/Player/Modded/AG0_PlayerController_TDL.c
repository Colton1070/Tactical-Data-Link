modded class SCR_PlayerController
{
    // Client-side visibility tracking for map markers
    protected ref array<RplId> m_aVisibleTDLDevices = {};
    protected float m_fTDLUpdateTimer = 0;
    protected const float TDL_UPDATE_INTERVAL = 1.0;
	
    // Replicated state
    protected ref array<int> m_aTDLConnectedPlayerIDs = {};
    protected ref map<int, ref AG0_TDLNetworkMembers> m_mTDLNetworkMembersMap = new map<int, ref AG0_TDLNetworkMembers>();
    protected ref array<RplId> m_NetworkBroadcastingSources = {};
    protected ref set<RplId> m_AvailableVideoSourcesSet = new set<RplId>();
    
    // Video streaming tracking (client-side)
    protected ref set<RplId> m_StreamedBroadcastingDevices = new set<RplId>();
    protected ref array<RplId> m_AvailableVideoSources = {};
    protected bool m_bVideoSourcesDirty = true;
    
    // TDL Menu input handling
    protected InputManager m_TDLInputManager;
    
    //------------------------------------------------------------------------------------------------
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        
        // Cache input manager and register listener on clients only
        if (!System.IsConsoleApp())
        {
            m_TDLInputManager = GetGame().GetInputManager();
            if (m_TDLInputManager)
                m_TDLInputManager.AddActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnTDLMenuToggle);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void ~SCR_PlayerController()
    {
        if (m_TDLInputManager)
            m_TDLInputManager.RemoveActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnTDLMenuToggle);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnTDLMenuToggle()
	{
	    MenuManager menuManager = GetGame().GetMenuManager();
	    MenuBase topMenu = menuManager.GetTopMenu();
	    
	    // If TDL menu is already open, close it
	    if (topMenu && AG0_TDLMenuUI.Cast(topMenu))
	    {
	        topMenu.Close();
	        return;
	    }
	    
	    // Otherwise open it
	    menuManager.OpenMenu(ChimeraMenuPreset.AG0_TDLMenu);
	}
    
    //------------------------------------------------------------------------------------------------
    override void OnUpdate(float timeSlice)
    {
        super.OnUpdate(timeSlice);
        
        if (m_bIsLocalPlayerController)
        {
            UpdateTDLNetworkState(timeSlice);
        }
    }
	
	//------------------------------------------------------------------------------------------------
    // Public interface for System (server-side calls)
    //------------------------------------------------------------------------------------------------
    void NotifyConnectedPlayers(array<int> connectedPlayerIDs)
    {
		Print("Recieved connected players on controller, role is server: ", Replication.IsServer());
        Rpc(RPC_SetTDLConnectedPlayers, connectedPlayerIDs);
    }
    
    void NotifyNetworkMembers(int networkId, array<ref AG0_TDLNetworkMember> members)
    {
        Rpc(RPC_SetTDLNetworkMembers, networkId, members);
    }
    
    void NotifyClearNetwork(int networkId)
    {
        Rpc(RPC_ClearTDLNetwork, networkId);
    }
    
    void NotifyBroadcastingSources(array<RplId> broadcastingSources)
    {
        Rpc(RPC_SetNetworkBroadcastingSources, broadcastingSources);
    }
    
    //------------------------------------------------------------------------------------------------
    // RPCs (protected)
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetTDLConnectedPlayers(array<int> connectedPlayerIDs)
    {
        m_aTDLConnectedPlayerIDs = connectedPlayerIDs;
        Print(string.Format("TDL_PLAYERCONTROLLER: Updated connected players: %1", connectedPlayerIDs), LogLevel.DEBUG);
    }
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetTDLNetworkMembers(int networkId, array<ref AG0_TDLNetworkMember> members)
    {
        AG0_TDLNetworkMembers membersData = new AG0_TDLNetworkMembers();
        foreach (AG0_TDLNetworkMember member : members)
            membersData.Add(member);
        
        m_mTDLNetworkMembersMap.Set(networkId, membersData);
        Print(string.Format("TDL_PLAYERCONTROLLER: Received network %1 member update with %2 members", networkId, members.Count()), LogLevel.DEBUG);
    }
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_ClearTDLNetwork(int networkId)
    {
        m_mTDLNetworkMembersMap.Remove(networkId);
        Print(string.Format("TDL_PLAYERCONTROLLER: Cleared network %1 data", networkId), LogLevel.DEBUG);
    }
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetNetworkBroadcastingSources(array<RplId> broadcastingSources)
    {
        m_NetworkBroadcastingSources = broadcastingSources;
        m_AvailableVideoSourcesSet.Clear();
        foreach (RplId sourceId : broadcastingSources)
            m_AvailableVideoSourcesSet.Insert(sourceId);
        
        m_bVideoSourcesDirty = true;
        Print(string.Format("TDL_PLAYERCONTROLLER: Updated broadcasting sources: %1", broadcastingSources.Count()), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    // Video source management
    //------------------------------------------------------------------------------------------------
    void RegisterBroadcastingDevice(RplId deviceId)
    {
        if (!m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.Insert(deviceId);
            m_bVideoSourcesDirty = true;
        }
    }
    
    void UnregisterBroadcastingDevice(RplId deviceId)
    {
        if (m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.Remove(deviceId);
            m_bVideoSourcesDirty = true;
        }
    }
    
    array<RplId> GetAvailableVideoSources()
    {
        if (m_bVideoSourcesDirty)
        {
            m_AvailableVideoSources.Clear();
            foreach (RplId sourceId : m_StreamedBroadcastingDevices)
                m_AvailableVideoSources.Insert(sourceId);
            
            m_bVideoSourcesDirty = false;
        }
        return m_AvailableVideoSources;
    }
    
    bool IsVideoSourceAvailable(RplId sourceId)
    {
        return m_AvailableVideoSourcesSet.Contains(sourceId);
    }
    
    //------------------------------------------------------------------------------------------------
    // Public getters
    //------------------------------------------------------------------------------------------------
    array<int> GetTDLConnectedPlayers() { return m_aTDLConnectedPlayerIDs; }
    AG0_TDLNetworkMembers GetTDLNetworkMembers(int networkId) { return m_mTDLNetworkMembersMap.Get(networkId); }
    map<int, ref AG0_TDLNetworkMembers> GetAllTDLNetworks() { return m_mTDLNetworkMembersMap; }
    array<RplId> GetNetworkBroadcastingSources() { return m_NetworkBroadcastingSources; }
    bool IsSourceBroadcasting(RplId sourceId) { return m_AvailableVideoSourcesSet.Contains(sourceId); }
    
    AG0_TDLNetworkMembers GetAggregatedTDLMembers()
    {
        AG0_TDLNetworkMembers aggregate = new AG0_TDLNetworkMembers();
        foreach (int networkId, AG0_TDLNetworkMembers networkData : m_mTDLNetworkMembersMap)
        {
            if (!networkData) continue;
            for (int i = 0; i < networkData.Count(); i++)
            {
                AG0_TDLNetworkMember member = networkData.Get(i);
                if (member) aggregate.Add(member);
            }
        }
        return aggregate;
    }
    
    //------------------------------------------------------------------------------------------------
    // Client-side visibility aggregation (for map markers) and menu context management
    //------------------------------------------------------------------------------------------------
    protected void UpdateTDLNetworkState(float timeSlice)
    {
        m_fTDLUpdateTimer += timeSlice;
        if (m_fTDLUpdateTimer < TDL_UPDATE_INTERVAL) return;
        
        m_fTDLUpdateTimer = 0;
        
        array<AG0_TDLDeviceComponent> playerDevices = GetPlayerTDLDevices();
        
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
	
	bool IsConnectedTDLPlayer(int playerId)
	{
	    return m_aTDLConnectedPlayerIDs.Contains(playerId);
	}
	
	//------------------------------------------------------------------------------------------------
	// Device management
	//------------------------------------------------------------------------------------------------
	array<AG0_TDLDeviceComponent> GetPlayerTDLDevices()
	{
	    array<AG0_TDLDeviceComponent> devices = {};
	    
	    IEntity controlledEntity = GetControlledEntity();
	    if (!controlledEntity)
	    {
	        Print("TDL_CTRL_DEVICES: No controlled entity", LogLevel.DEBUG);
	        return devices;
	    }
	    
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (!system)
	    {
	        Print("TDL_CTRL_DEVICES: No TDL system", LogLevel.DEBUG);
	        return devices;
	    }
	    
	    devices = system.GetPlayerAllTDLDevices(controlledEntity);
	    Print(string.Format("TDL_CTRL_DEVICES: Returning %1 devices", devices.Count()), LogLevel.DEBUG);
	    return devices;
	}
	
	bool IsHoldingDevice(IEntity device)
	{
	    if (!device)
	        return false;
	    
	    IEntity controlledEntity = GetControlledEntity();
	    if (!controlledEntity)
	        return false;
	    
	    // Check inventory for the device
	    InventoryStorageManagerComponent invManager = InventoryStorageManagerComponent.Cast(
	        controlledEntity.FindComponent(InventoryStorageManagerComponent));
	    if (!invManager)
	        return false;
	    
	    return invManager.Contains(device);
	}
}