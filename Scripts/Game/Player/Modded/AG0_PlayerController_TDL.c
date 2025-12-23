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
	    
	    // Only open if player has ATAK device
	    if (!HasATAKDevice())
	        return;
	    
	    menuManager.OpenMenu(ChimeraMenuPreset.AG0_TDLMenu);
	}
	
	bool HasATAKDevice()
	{
	    array<AG0_TDLDeviceComponent> devices = GetPlayerTDLDevices();
	    foreach (AG0_TDLDeviceComponent device : devices)
	    {
	        if (device.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
	            return true;
	    }
	    return false;
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
    // Public interface for Controller (owner-side calls)
    //------------------------------------------------------------------------------------------------
	
	void RequestKickDevice(RplId targetDeviceId)
	{
	    Rpc(RpcAsk_KickDevice, targetDeviceId);
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
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_KickDevice(RplId targetDeviceId)
	{
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (!system)
	        return;
	    
	    AG0_TDLDeviceComponent device = system.GetDeviceByRplId(targetDeviceId);
	    if (!device)
	        return;
	    
	    system.LeaveNetwork(device);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetCameraBroadcasting(RplId deviceRplId, bool broadcasting)
	{   
	    RplComponent rpl = RplComponent.Cast(Replication.FindItem(deviceRplId));
		if (!rpl) return;
		IEntity entity = rpl.GetEntity();
		if (!entity) return;
		AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
		    entity.FindComponent(AG0_TDLDeviceComponent));
	    
	    device.SetCameraBroadcasting(broadcasting);
	    Print(string.Format("[TDL Controller] Set camera broadcast to %1 for device %2", broadcasting, deviceRplId), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// Callsign Management
	//------------------------------------------------------------------------------------------------
	void RequestSetDeviceCallsign(RplId deviceRplId, string callsign)
	{
	    Rpc(RpcAsk_SetDeviceCallsign, deviceRplId, callsign);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetDeviceCallsign(RplId deviceRplId, string callsign)
	{
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (!system)
	        return;
	    
	    AG0_TDLDeviceComponent device = system.GetDeviceByRplId(deviceRplId);
	    if (!device)
	        return;
	    
	    // Future: verify player actually has access to this device
	    // Could check IsHoldingDevice or inventory containment
	    
	    // Server-side call - SetCustomCallsign handles the logic + bump + system notify
	    device.SetCustomCallsign(callsign);
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
	// Camera Broadcast Management
	//------------------------------------------------------------------------------------------------
	void RequestSetCameraBroadcasting(RplId deviceRplId, bool broadcasting)
	{
	    Rpc(RpcAsk_SetCameraBroadcasting, deviceRplId, broadcasting);
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
	    
	    IEntity playerEntity = GetControlledEntity();
	    if (!playerEntity)
	        return devices;
	    
	    // Check held gadgets
	    SCR_GadgetManagerComponent gadgetMgr = SCR_GadgetManagerComponent.Cast(
	        playerEntity.FindComponent(SCR_GadgetManagerComponent));
	    if (gadgetMgr)
	    {
	        IEntity heldGadget = gadgetMgr.GetHeldGadget();
	        if (heldGadget)
	        {
	            AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
	                heldGadget.FindComponent(AG0_TDLDeviceComponent));
	            if (deviceComp && devices.Find(deviceComp) == -1)
	                devices.Insert(deviceComp);
	        }
	    }
	    
	    // Check inventory
	    InventoryStorageManagerComponent storage = InventoryStorageManagerComponent.Cast(
	        playerEntity.FindComponent(InventoryStorageManagerComponent));
	    if (storage)
	    {
	        array<IEntity> items = {};
	        storage.GetItems(items);
	        foreach (IEntity item : items)
	        {
	            AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
	                item.FindComponent(AG0_TDLDeviceComponent));
	            if (deviceComp && devices.Find(deviceComp) == -1)
	                devices.Insert(deviceComp);
	        }
	    }
	    
	    // Check equipment slots (vest, backpack, etc.)
	    ChimeraCharacter character = ChimeraCharacter.Cast(playerEntity);
	    if (character)
	    {
	        EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
	            character.FindComponent(EquipedLoadoutStorageComponent));
	        if (loadoutStorage)
	        {
	            array<typename> equipmentAreas = {
	                LoadoutHeadCoverArea, LoadoutArmoredVestSlotArea, 
	                LoadoutVestArea, LoadoutJacketArea, LoadoutBackpackArea
	            };
	            
	            foreach (typename area : equipmentAreas)
	            {
	                IEntity container = loadoutStorage.GetClothFromArea(area);
	                if (!container)
	                    continue;
	                
	                // Check the container itself (e.g., vest with built-in TDL device)
	                AG0_TDLDeviceComponent containerDevice = AG0_TDLDeviceComponent.Cast(
	                    container.FindComponent(AG0_TDLDeviceComponent));
	                if (containerDevice && devices.Find(containerDevice) == -1)
	                    devices.Insert(containerDevice);
	                
	                // Check items stored in the container
	                ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
	                    container.FindComponent(ClothNodeStorageComponent));
	                if (!clothStorage)
	                    continue;
	                
	                array<IEntity> clothItems = {};
	                clothStorage.GetAll(clothItems);
	                
	                foreach (IEntity clothItem : clothItems)
	                {
	                    AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
	                        clothItem.FindComponent(AG0_TDLDeviceComponent));
	                    if (deviceComp && devices.Find(deviceComp) == -1)
	                        devices.Insert(deviceComp);
	                }
	            }
	        }
	    }
	    
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
	
	//------------------------------------------------------------------------------------------------
	// Get callsign for a player in a specific network
	// Returns empty string if not found
	string GetCallsignForPlayerInNetwork(int playerId, int networkId)
	{
	    AG0_TDLNetworkMembers members = m_mTDLNetworkMembersMap.Get(networkId);
	    if (!members)
	        return "";
	    
	    foreach (AG0_TDLNetworkMember member : members.m_aMembers)
	    {
	        if (member.GetOwnerPlayerId() == playerId)
	            return member.GetPlayerName();
	    }
	    
	    return "";
	}
}