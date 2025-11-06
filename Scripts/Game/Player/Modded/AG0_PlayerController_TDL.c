modded class SCR_PlayerController
{
    // Client-side visibility tracking for map markers
    protected ref array<RplId> m_aVisibleTDLDevices = {};
    protected float m_fTDLUpdateTimer = 0;
    protected const float TDL_UPDATE_INTERVAL = 1.0;
	
	 //------------------------------------------------------------------------------------------------
    // Replicated state
    //------------------------------------------------------------------------------------------------
    protected ref array<int> m_aTDLConnectedPlayerIDs = {};
    protected ref map<int, ref AG0_TDLNetworkMembers> m_mTDLNetworkMembersMap = new map<int, ref AG0_TDLNetworkMembers>();
    protected ref array<RplId> m_NetworkBroadcastingSources = {};
    protected ref set<RplId> m_AvailableVideoSourcesSet = new set<RplId>();
    
    // Video streaming tracking (client-side)
    protected ref set<RplId> m_StreamedBroadcastingDevices = new set<RplId>();
    protected ref array<RplId> m_AvailableVideoSources = {};
    protected bool m_bVideoSourcesDirty = true;
    
    override void OnUpdate(float timeSlice)
    {
        super.OnUpdate(timeSlice);
        
        if (m_bIsLocalPlayerController)
        {
            UpdateTDLNetworkState(timeSlice);
			// Find my WorldController and tell it my player ID
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
        Print(string.Format("TDL_CONTROLLER: Updated connected players: %1", connectedPlayerIDs), LogLevel.DEBUG);

    }
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetTDLNetworkMembers(int networkId, array<ref AG0_TDLNetworkMember> members)
    {
        AG0_TDLNetworkMembers membersData = new AG0_TDLNetworkMembers();
        foreach (AG0_TDLNetworkMember member : members)
            membersData.Add(member);
        
        m_mTDLNetworkMembersMap.Set(networkId, membersData);
        Print(string.Format("TDL_CONTROLLER: Received network %1 member update with %2 members", networkId, members.Count()), LogLevel.DEBUG);
    }
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_ClearTDLNetwork(int networkId)
    {
        m_mTDLNetworkMembersMap.Remove(networkId);
        Print(string.Format("TDL_CONTROLLER: Cleared network %1 data", networkId), LogLevel.DEBUG);
    }
    
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetNetworkBroadcastingSources(array<RplId> sources)
    {
        m_NetworkBroadcastingSources = sources;
        m_bVideoSourcesDirty = true;
        Print(string.Format("TDL_CONTROLLER: Received %1 network broadcasting sources", sources.Count()), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    // Equipment queries
    //------------------------------------------------------------------------------------------------
    array<AG0_TDLDeviceComponent> GetPlayerTDLDevices()
	{
	    array<AG0_TDLDeviceComponent> devices = {};
	    
	    SCR_PlayerController pc = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);

		
		if (!pc) return devices;
	    
	    IEntity controlled = pc.GetControlledEntity();
	    if (!controlled) return devices;
	    
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
	                devices.Insert(deviceComp);
	        }
	    }
	    
	    // Check inventory
	    InventoryStorageManagerComponent storage = InventoryStorageManagerComponent.Cast(
	        controlled.FindComponent(InventoryStorageManagerComponent));
	    if (storage)
	    {
	        array<IEntity> items = {};
	        storage.GetItems(items);
	        
	        foreach (IEntity item : items)
	        {
	            AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
	                item.FindComponent(AG0_TDLDeviceComponent));
	            if (deviceComp && deviceComp.CanAccessNetwork())
	                devices.Insert(deviceComp);
	        }
	    }
	    
	    // Check loadout clothing slots
	    ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
	    if (character)
	    {
	        EquipedLoadoutStorageComponent loadoutStorage = 
	            EquipedLoadoutStorageComponent.Cast(character.FindComponent(EquipedLoadoutStorageComponent));
	        if (loadoutStorage)
	        {
	            array<typename> equipmentAreas = {
	                LoadoutHeadCoverArea, LoadoutArmoredVestSlotArea, 
	                LoadoutVestArea, LoadoutJacketArea, LoadoutBackpackArea
	            };
	            
	            foreach (typename area : equipmentAreas)
	            {
	                IEntity container = loadoutStorage.GetClothFromArea(area);
	                if (!container) continue;
	                
	                ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
	                    container.FindComponent(ClothNodeStorageComponent));
	                if (!clothStorage) continue;
	                
	                array<IEntity> clothItems = {};
	                clothStorage.GetAll(clothItems);
	                
	                foreach (IEntity item : clothItems)
	                {
	                    AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
	                        item.FindComponent(AG0_TDLDeviceComponent));
	                    if (deviceComp && deviceComp.CanAccessNetwork())
	                        devices.Insert(deviceComp);
	                }
	            }
	        }
	    }
	    
	    return devices;
	}
    
    bool IsHoldingDevice(IEntity device)
    {
        if (!device) return false;
        
        PlayerManager playerMgr = GetGame().GetPlayerManager();
		
		SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
		
        IEntity controlled = playerMgr.GetPlayerControlledEntity(controller.GetPlayerId());
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
        if (character)
        {
            EquipedLoadoutStorageComponent loadoutStorage = 
                EquipedLoadoutStorageComponent.Cast(character.FindComponent(EquipedLoadoutStorageComponent));
            if (loadoutStorage)
            {
                array<typename> equipmentAreas = {
                    LoadoutHeadCoverArea, LoadoutArmoredVestSlotArea, 
                    LoadoutVestArea, LoadoutJacketArea, LoadoutBackpackArea
                };
                
                foreach (typename area : equipmentAreas)
                {
                    IEntity container = loadoutStorage.GetClothFromArea(area);
                    if (!container) continue;
                    
                    ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
                        container.FindComponent(ClothNodeStorageComponent));
                    if (!clothStorage) continue;
                    
                    array<IEntity> clothItems = {};
                    clothStorage.GetAll(clothItems);
                    
                    if (clothItems.Contains(device))
                        return true;
                }
            }
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    // Video streaming registration (client-side tracking)
    //------------------------------------------------------------------------------------------------
    void RegisterBroadcastingDevice(RplId deviceId)
    {
        if (!m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.Insert(deviceId);
            m_bVideoSourcesDirty = true;
            Print(string.Format("TDL_CONTROLLER: Registered streamed broadcaster %1", deviceId), LogLevel.DEBUG);
        }
    }
    
    void UnregisterBroadcastingDevice(RplId deviceId)
    {
        if (m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.RemoveItem(deviceId);
            m_bVideoSourcesDirty = true;
            Print(string.Format("TDL_CONTROLLER: Unregistered streamed broadcaster %1", deviceId), LogLevel.DEBUG);
        }
    }
    
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
    // Client-side visibility aggregation (for map markers)
    //------------------------------------------------------------------------------------------------
    protected void UpdateTDLNetworkState(float timeSlice)
    {
        m_fTDLUpdateTimer += timeSlice;
        if (m_fTDLUpdateTimer < TDL_UPDATE_INTERVAL) return;
        
        m_fTDLUpdateTimer = 0;
        
        // Get devices from TDLController
        SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
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