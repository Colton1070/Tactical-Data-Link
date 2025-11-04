class AG0_TDLController : WorldController
{
    //------------------------------------------------------------------------------------------------
    override static void InitInfo(WorldControllerInfo outInfo)
    {
        outInfo.SetPublic(true);
    }
	
	void AG0_TDLController()
	{
	    string context = "CLIENT";
	    if (Replication.IsServer())
	        context = "SERVER";
	    
	    Print(string.Format("TDL_CONTROLLER: Constructor called on %1", context), LogLevel.DEBUG);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	private void RPC_RegisterWithPlayerID(int realPlayerId)
	{
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system)
	    {
	        system.RegisterController(this, realPlayerId);
	        Print(string.Format("TDL_CONTROLLER: Registered with player ID %1", realPlayerId), LogLevel.DEBUG);
	    }
	}
	
	override protected void OnAuthorityReady()
	{
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    PlayerId localPlayerID = SCR_PlayerController.GetLocalPlayerId();
	    if (localPlayerID != 0)
	    {
	        Rpc(RPC_RegisterWithPlayerID, localPlayerID);
	        Print(string.Format("TDL_CONTROLLER: Sending registration for player %1", localPlayerID), LogLevel.DEBUG);
	    }
		else {
			PrintFormat("TDL_CONTROLLER: PlayerID is 0, therefor disregarding.");
			//Unless:
			#ifdef WORKBENCH
				Rpc(RPC_RegisterWithPlayerID, 1);
	        Print(string.Format("TDL_CONTROLLER: Sending registration for player %1", 1), LogLevel.DEBUG);
			#endif
		}
	}
	
	void ~AG0_TDLController()
    {
        // Unregister ourselves from the system
        int playerId = GetOwnerPlayerId();
        
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (system)
        {
            system.UnregisterController(playerId);
        }
    }
    
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
	    
	    if (!IsMyOwn())
	        return devices;
	    
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
		#ifdef WORKBENCH
	    	PlayerController pc = playerMgr.GetPlayerController(1);
	    #else
			PlayerController pc = playerMgr.GetPlayerController(GetOwnerPlayerId());
		#endif
		
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
        IEntity controlled = playerMgr.GetPlayerControlledEntity(GetOwnerPlayerId());
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
}