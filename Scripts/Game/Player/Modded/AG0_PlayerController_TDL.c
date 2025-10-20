modded class SCR_PlayerController
{
    // TDL state tracking
    protected ref array<RplId> m_aVisibleTDLDevices = {};
    protected float m_fTDLUpdateTimer = 0;
    protected const float TDL_UPDATE_INTERVAL = 1.0;
	
	protected ref array<int> m_aTDLConnectedPlayerIDs = {};
	protected ref map<int, ref AG0_TDLNetworkMembers> m_mTDLNetworkMembersMap = new map<int, ref AG0_TDLNetworkMembers>();
	
	// Video streaming state
	
    protected ref set<RplId> m_StreamedBroadcastingDevices = new set<RplId>();
    protected ref array<RplId> m_NetworkBroadcastingSources = {};
    protected ref array<RplId> m_AvailableVideoSources = {}; // Cached intersection
	protected ref set<RplId> m_AvailableVideoSourcesSet = new set<RplId>();

    protected bool m_bVideoSourcesDirty = true;
    
    // Add listener tracking flag
    protected bool m_bTDLListenerRegistered = false;
    
    override void OnUpdate(float timeSlice)
    {
        super.OnUpdate(timeSlice);
        
        if (m_bIsLocalPlayerController)
        {
            UpdateTDLNetworkState(timeSlice);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RPC_SetTDLConnectedPlayers(array<int> connectedPlayerIDs)
	{
	    m_aTDLConnectedPlayerIDs = connectedPlayerIDs;
	    
	    // Debug logging
	    Print(string.Format("TDL_CONNECTIVITY: Player %1 received connectivity update", GetPlayerId()), LogLevel.NORMAL);
	    Print(string.Format("  Connected player count: %1", connectedPlayerIDs.Count()), LogLevel.NORMAL);
	    
	    foreach (int playerID : connectedPlayerIDs)
	    {
	        Print(string.Format("  - Connected to player ID: %1", playerID), LogLevel.NORMAL);
	    }
	    
	    if (connectedPlayerIDs.IsEmpty())
	        Print("  - No connected players", LogLevel.NORMAL);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    void RPC_SetTDLNetworkMembers(int networkId, array<ref AG0_TDLNetworkMember> members)
    {
        // Convert array to our storage format
        AG0_TDLNetworkMembers membersData = new AG0_TDLNetworkMembers();
        foreach (AG0_TDLNetworkMember member : members)
        {
            membersData.Add(member);
        }
        
        m_mTDLNetworkMembersMap.Set(networkId, membersData);
        
        Print(string.Format("TDL_PLAYER_CONTROLLER: Received network %1 member update with %2 members", 
            networkId, members.Count()), LogLevel.DEBUG);
		
    }
	
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    void RPC_ClearTDLNetwork(int networkId)
    {
        m_mTDLNetworkMembersMap.Remove(networkId);
        Print(string.Format("TDL_PLAYER_CONTROLLER: Cleared network %1 data", networkId), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    array<int> GetTDLConnectedPlayers()
    {
        return m_aTDLConnectedPlayerIDs;
    }
	
	AG0_TDLNetworkMembers GetTDLNetworkMembers(int networkId)
    {
        return m_mTDLNetworkMembersMap.Get(networkId);
    }
	
	AG0_TDLNetworkMembers GetAggregatedTDLMembers()
	{
	    AG0_TDLNetworkMembers aggregate = new AG0_TDLNetworkMembers();
	    
	    foreach (int networkId, AG0_TDLNetworkMembers networkData : m_mTDLNetworkMembersMap)
	    {
	        if (!networkData) continue;
	        
	        // Just merge all members - no deduplication needed since 
	        // devices can only be in one network at a time
	        for (int i = 0; i < networkData.Count(); i++)
	        {
	            AG0_TDLNetworkMember member = networkData.Get(i);
	            if (member)
	                aggregate.Add(member);
	        }
	    }
	    
	    return aggregate;
	}
    
    map<int, ref AG0_TDLNetworkMembers> GetAllTDLNetworks()
    {
        return m_mTDLNetworkMembersMap;
    }
	
	
    protected void UpdateTDLNetworkState(float timeSlice)
    {
        m_fTDLUpdateTimer += timeSlice;
        if (m_fTDLUpdateTimer < TDL_UPDATE_INTERVAL) return;
        
        m_fTDLUpdateTimer = 0;
        
        Print("TDL_PLAYER_CONTROLLER: Updating TDL network state", LogLevel.DEBUG);
        
        // Aggregate visible devices from all player's TDL devices
        array<RplId> newVisibleDevices = {};
        array<AG0_TDLDeviceComponent> playerDevices = GetPlayerTDLDevices();
        
        Print(string.Format("TDL_PLAYER_CONTROLLER: Found %1 player TDL devices", playerDevices.Count()), LogLevel.DEBUG);
        
        foreach (AG0_TDLDeviceComponent device : playerDevices)
        {
            if (!device.IsInNetwork()) 
            {
                string deviceName = "UNKNOWN";
                if (device.GetOwner())
                    deviceName = device.GetOwner().ToString();
                Print(string.Format("TDL_PLAYER_CONTROLLER: Device %1 not in network, skipping", deviceName), LogLevel.DEBUG);
                continue;
            }
            
            array<RplId> connectedDevices = device.GetConnectedMembers();
            string deviceName = "UNKNOWN";
            if (device.GetOwner())
                deviceName = device.GetOwner().ToString();
            Print(string.Format("TDL_PLAYER_CONTROLLER: Device %1 connected to %2 members", 
                deviceName, connectedDevices.Count()), LogLevel.DEBUG);
            
            foreach (RplId deviceId : connectedDevices)
            {
                if (newVisibleDevices.Find(deviceId) == -1)
                {
                    newVisibleDevices.Insert(deviceId);
                    Print(string.Format("TDL_PLAYER_CONTROLLER: Added visible device RplId: %1", deviceId), LogLevel.DEBUG);
                }
            }
        }
        
        Print(string.Format("TDL_PLAYER_CONTROLLER: Total visible devices: %1", newVisibleDevices.Count()), LogLevel.DEBUG);
        
        // Update markers if visibility changed
        if (!RplIdArraysEqual(newVisibleDevices, m_aVisibleTDLDevices))
        {
            Print("TDL_PLAYER_CONTROLLER: Visible devices changed.", LogLevel.DEBUG);
            m_aVisibleTDLDevices = newVisibleDevices;
        }
        else
        {
            Print("TDL_PLAYER_CONTROLLER: No changes in visible devices", LogLevel.DEBUG);
        }
    }
    
    array<AG0_TDLDeviceComponent> GetPlayerTDLDevices()
    {
        array<AG0_TDLDeviceComponent> devices = {};
        IEntity controlled = GetControlledEntity();
        if (!controlled) 
        {
            Print("TDL_PLAYER_CONTROLLER: No controlled entity", LogLevel.DEBUG);
            return devices;
        }
        
        Print(string.Format("TDL_PLAYER_CONTROLLER: Searching for TDL devices on %1", controlled.ToString()), LogLevel.DEBUG);
        
        // Check held gadget
        SCR_GadgetManagerComponent gadgetMgr = SCR_GadgetManagerComponent.Cast(
            controlled.FindComponent(SCR_GadgetManagerComponent));
        if (gadgetMgr)
        {
            IEntity heldGadget = gadgetMgr.GetHeldGadget();
            if (heldGadget)
            {
                AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
                    heldGadget.FindComponent(AG0_TDLDeviceComponent));
                if (deviceComp && deviceComp.CanAccessNetwork())
                {
                    devices.Insert(deviceComp);
                    Print(string.Format("TDL_PLAYER_CONTROLLER: Found held TDL device: %1", heldGadget.ToString()), LogLevel.DEBUG);
                }
            }
        }
        
        // Check inventory for network-capable devices
        InventoryStorageManagerComponent storage = InventoryStorageManagerComponent.Cast(
            controlled.FindComponent(InventoryStorageManagerComponent));
        if (storage)
        {
            array<IEntity> items = {};
            storage.GetItems(items);
            
            Print(string.Format("TDL_PLAYER_CONTROLLER: Checking %1 inventory items", items.Count()), LogLevel.DEBUG);
            
            foreach (IEntity item : items)
            {
                AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
                    item.FindComponent(AG0_TDLDeviceComponent));
                if (deviceComp && deviceComp.CanAccessNetwork())
                {
                    devices.Insert(deviceComp);
                    Print(string.Format("TDL_PLAYER_CONTROLLER: Found inventory TDL device: %1", item.ToString()), LogLevel.DEBUG);
                }
            }
        }
        
        Print(string.Format("TDL_PLAYER_CONTROLLER: Total TDL devices found: %1", devices.Count()), LogLevel.DEBUG);
        return devices;
    }
    
    protected bool RplIdArraysEqual(array<RplId> a, array<RplId> b)
    {
        if (a.Count() != b.Count()) return false;
        foreach (RplId id : a)
            if (b.Find(id) == -1) return false;
        return true;
    }
    
    bool CanSeeDevice(RplId deviceId)
    {
        return m_aVisibleTDLDevices.Contains(deviceId);
    }
    
    protected bool HasTDLMenuCapableDevice()
    {
        array<AG0_TDLDeviceComponent> devices = GetPlayerTDLDevices();
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.IsPowered() && 
                device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION) &&
                device.HasCapability(AG0_ETDLDeviceCapability.DISPLAY_OUTPUT))
            {
                return true;
            }
        }
        return false;
    }
	
	bool IsHoldingDevice(IEntity device)
	{
	    if (!device) return false;
	    
	    IEntity controlled = GetControlledEntity();
	    if (!controlled) return false;
	    
	    // Quick check - held gadget?
	    SCR_GadgetManagerComponent gadgetMgr = SCR_GadgetManagerComponent.Cast(
	        controlled.FindComponent(SCR_GadgetManagerComponent));
	    if (gadgetMgr && gadgetMgr.GetHeldGadget() == device)
	        return true;
	    
	    // Check inventory storage
	    InventoryStorageManagerComponent storage = InventoryStorageManagerComponent.Cast(
	        controlled.FindComponent(InventoryStorageManagerComponent));
	    if (storage && storage.Contains(device))
	        return true;
	    
        ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
        if (character) {
            EquipedLoadoutStorageComponent loadoutStorage = 
                EquipedLoadoutStorageComponent.Cast(character.FindComponent(EquipedLoadoutStorageComponent));
            if (loadoutStorage) {
                // Same equipment area logic as the system
                array<typename> equipmentAreas = {
                    LoadoutHeadCoverArea, LoadoutArmoredVestSlotArea, 
                    LoadoutVestArea, LoadoutJacketArea, LoadoutBackpackArea
                };
                
                foreach (typename area : equipmentAreas) {
                    IEntity container = loadoutStorage.GetClothFromArea(area);
                    if (!container) continue;
                    
                    ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
                        container.FindComponent(ClothNodeStorageComponent));
                    if (!clothStorage) continue;
                    
                    array<IEntity> clothItems = {};
                    clothStorage.GetAll(clothItems);
                    
                    if (clothItems.Contains(device)) {
                        return true;
                        break;
                    }
                }
            }
	    }
	    return false;
	}
	
	// Called by devices when they stream in/out while broadcasting
    void RegisterBroadcastingDevice(RplId deviceId)
    {
        if (!m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.Insert(deviceId);
            m_bVideoSourcesDirty = true;
            Print(string.Format("TDL_VIDEO_PC: Registered streamed broadcaster %1", deviceId), LogLevel.DEBUG);
        }
    }
    
    void UnregisterBroadcastingDevice(RplId deviceId)
    {
        if (m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.RemoveItem(deviceId);
            m_bVideoSourcesDirty = true;
            Print(string.Format("TDL_VIDEO_PC: Unregistered streamed broadcaster %1", deviceId), LogLevel.DEBUG);
        }
    }
    
    // RPC from system about network broadcasting sources
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    void RPC_SetNetworkBroadcastingSources(array<RplId> sources)
    {
        m_NetworkBroadcastingSources = sources;
        m_bVideoSourcesDirty = true;
        Print(string.Format("TDL_VIDEO_PC: Received %1 network broadcasting sources", sources.Count()), LogLevel.DEBUG);
    }
    
    // Get available sources (intersection of network + streamed)
    array<RplId> GetAvailableVideoSources()
	{
	    if (m_bVideoSourcesDirty)
	    {
	        m_AvailableVideoSources.Clear();
	        m_AvailableVideoSourcesSet.Clear();
	        
	        foreach (RplId networkSource : m_NetworkBroadcastingSources)
	        {
	            if (m_StreamedBroadcastingDevices.Contains(networkSource))
	            {
	                m_AvailableVideoSources.Insert(networkSource);
	                m_AvailableVideoSourcesSet.Insert(networkSource);
	            }
	        }
	        
	        m_bVideoSourcesDirty = false;
	    }
	    
	    return m_AvailableVideoSources;
	}
	
	bool IsVideoSourceAvailable(RplId sourceId)
	{
	    if (m_bVideoSourcesDirty)
	        GetAvailableVideoSources(); // Forces recalc
	        
	    return m_AvailableVideoSourcesSet.Contains(sourceId);
	}
	
}