//------------------------------------------------------------------------------------------------
//! TDL Player Controller Extension
//! Handles client-side TDL state, device management, and server communication
//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
    // ============================================
    // HELD DEVICE CACHE
    // ============================================
    // Cached set for O(1) "is this mine?" lookups - used by world-space displays, device components, etc.
    protected ref set<IEntity> m_sHeldDeviceEntities = new set<IEntity>();
    protected ref array<AG0_TDLDeviceComponent> m_aHeldDevicesCache = {};
    protected float m_fHeldDeviceCacheTimer = 0;
    protected const float HELD_DEVICE_CACHE_INTERVAL = 1.0;
    
    // ============================================
    // CLIENT-SIDE VISIBILITY & STATE
    // ============================================
    protected ref array<RplId> m_aVisibleTDLDevices = {};
    protected float m_fTDLUpdateTimer = 0;
    protected const float TDL_UPDATE_INTERVAL = 1.0;
    
    // Replicated state from server
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
	
	// TDL Map View
	protected ref AG0_TDLMapShapeManager m_TDLShapeManager = new AG0_TDLMapShapeManager();
	protected string m_sShapeSyncHash;
	
	// ============================================
	// EUD SCREEN ADJUSTMENT
	// ============================================
	
	protected TDL_EUDBoneComponent m_CachedEUDBoneComp;
	protected const float EUD_ADJUST_STEP = 0.1;
	
	// Message storage per network
    protected ref map<int, ref AG0_TDLMessageStore> m_mNetworkMessages = new map<int, ref AG0_TDLMessageStore>();
    
    // Local tracking of read messages (client-side)
    protected ref set<int> m_LocallyReadMessages = new set<int>();
    
    // Callback for UI updates when messages change
    protected ref ScriptInvoker m_OnMessagesUpdated = new ScriptInvoker();  // (int networkId)
    protected ref ScriptInvoker m_OnNewMessageReceived = new ScriptInvoker();  // (int networkId, int messageId)
    protected ref ScriptInvoker m_OnReadReceiptReceived = new ScriptInvoker();  // (int networkId, int messageId)
	
	// ============================================
	// NETWORK DIALOG STATE (CLIENT-SIDE)
	// ============================================
	protected RplId m_PendingDialogDeviceRplId = RplId.Invalid();
	protected bool m_bPendingCreateNetworkMode = false;
	protected string m_sPendingNetworkName = "";
	
	// Dialog references
	protected ref AG0_TDL_NetworkNameDialog m_NetworkNameDialog;
	protected ref AG0_TDL_NetworkPasswordDialog m_NetworkPasswordDialog;
    
    // ============================================
    // LIFECYCLE
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        
        // Cache input manager and register listener on clients only
        if (!System.IsConsoleApp())
        {
            m_TDLInputManager = GetGame().GetInputManager();
            if (m_TDLInputManager) {
                m_TDLInputManager.AddActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnTDLMenuToggle);
				m_TDLInputManager.AddActionListener("TDLAdjustUp", EActionTrigger.DOWN, OnEUDAdjustUp);
	    		m_TDLInputManager.AddActionListener("TDLAdjustDown", EActionTrigger.DOWN, OnEUDAdjustDown);
			}
		}
    }
    
    //------------------------------------------------------------------------------------------------
    void ~SCR_PlayerController()
    {
        if (m_TDLInputManager) {
            m_TDLInputManager.RemoveActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnTDLMenuToggle);
			m_TDLInputManager.RemoveActionListener("TDLAdjustUp", EActionTrigger.DOWN, OnEUDAdjustUp);
		    m_TDLInputManager.RemoveActionListener("TDLAdjustDown", EActionTrigger.DOWN, OnEUDAdjustDown);
		}
	}
    
    //------------------------------------------------------------------------------------------------
    override void OnUpdate(float timeSlice)
    {
        super.OnUpdate(timeSlice);
        
        if (m_bIsLocalPlayerController)
        {
			if (HasATAKDevice() && ShouldActivateTDLContext())
		        m_TDLInputManager.ActivateContext("TDLMenuContext");
			
            UpdateTDLNetworkState(timeSlice);
            UpdateHeldDeviceCache(timeSlice);
        }
    }
	
	// ============================================
	// MENU STATE CHECK
	// ============================================
	
	//------------------------------------------------------------------------------------------------
	//! Returns true if TDL contexts should be active
	//! Blocks activation when non-TDL menus are open (pause, inventory, etc.)
	bool ShouldActivateTDLContext()
	{
	    MenuManager menuMgr = GetGame().GetMenuManager();
	    if (!menuMgr)
	        return true;
	    
	    if (!menuMgr.IsAnyMenuOpen())
	        return true;
	    
	    // A menu is open - check if it's OUR menu
	    MenuBase topMenu = menuMgr.GetTopMenu();
	    if (!topMenu)
	        return true;
	    
	    // Allow if the TDL menu is the active one
	    AG0_TDLMenuUI tdlMenu = AG0_TDLMenuUI.Cast(topMenu);
	    return tdlMenu != null;
	}
	
    
    // ============================================
    // HELD DEVICE CACHE - Core Implementation
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    //! Timer-based cache refresh - runs once per second on local controller
    protected void UpdateHeldDeviceCache(float timeSlice)
    {
        m_fHeldDeviceCacheTimer += timeSlice;
        if (m_fHeldDeviceCacheTimer < HELD_DEVICE_CACHE_INTERVAL)
            return;
        
        m_fHeldDeviceCacheTimer = 0;
        RebuildHeldDeviceCache();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Rebuilds both the entity set (for O(1) lookups) and component array (for iteration)
    protected void RebuildHeldDeviceCache()
    {
        m_sHeldDeviceEntities.Clear();
        m_aHeldDevicesCache.Clear();
        
        // Use existing full inventory search as source of truth
        array<AG0_TDLDeviceComponent> devices = GetPlayerTDLDevices();
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            m_aHeldDevicesCache.Insert(device);
            m_sHeldDeviceEntities.Insert(device.GetOwner());
        }
		UpdateEUDCache();
    }
    
    //------------------------------------------------------------------------------------------------
    //! O(1) check if an entity is a device held by this player
    //! Primary API for world-space displays and other per-frame queries
    bool IsHeldDevice(IEntity deviceEntity)
    {
        if (!deviceEntity)
            return false;
        return m_sHeldDeviceEntities.Contains(deviceEntity);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Returns cached device array - use when you need to iterate all held devices
    //! Note: May be up to 1 second stale. For authoritative list, use GetPlayerTDLDevices()
    array<AG0_TDLDeviceComponent> GetHeldDevicesCached()
    {
        return m_aHeldDevicesCache;
    }
    
    // ============================================
    // TDL MENU
    // ============================================
    
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
    
    //------------------------------------------------------------------------------------------------
    bool HasATAKDevice()
    {
        array<AG0_TDLDeviceComponent> devices = GetPlayerTDLDevices();
        int aggregatedCaps = 0;
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            aggregatedCaps |= device.GetActiveCapabilities();
        }
        
        int required = AG0_ETDLDeviceCapability.ATAK_DEVICE | AG0_ETDLDeviceCapability.DISPLAY_OUTPUT;
        return (aggregatedCaps & required) == required;
    }
    
    // ============================================
    // NETWORK STATE UPDATES
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateTDLNetworkState(float timeSlice)
    {
        m_fTDLUpdateTimer += timeSlice;
        if (m_fTDLUpdateTimer < TDL_UPDATE_INTERVAL)
            return;
        
        m_fTDLUpdateTimer = 0;
        
        array<AG0_TDLDeviceComponent> playerDevices = GetPlayerTDLDevices();
        
        // Aggregate visible devices from all player's TDL devices
        array<RplId> newVisibleDevices = {};
        
        foreach (AG0_TDLDeviceComponent device : playerDevices)
        {
            if (!device.IsInNetwork())
                continue;
            
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
    
    //------------------------------------------------------------------------------------------------
    protected bool RplIdArraysEqual(array<RplId> a, array<RplId> b)
    {
        if (a.Count() != b.Count())
            return false;
        foreach (RplId id : a)
        {
            if (b.Find(id) == -1)
                return false;
        }
        return true;
    }
    
    // ============================================
    // PUBLIC API - Map Markers & Connectivity
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    bool CanSeeDevice(RplId deviceId)
    {
        return m_aVisibleTDLDevices.Contains(deviceId);
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsConnectedTDLPlayer(int playerId)
    {
        return m_aTDLConnectedPlayerIDs.Contains(playerId);
    }
    
    // ============================================
    // DEVICE MANAGEMENT - Full Inventory Search
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    //! Full inventory search for TDL devices - authoritative but expensive
    //! For per-frame queries, use IsHeldDevice() or GetHeldDevicesCached() instead
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
    
    //------------------------------------------------------------------------------------------------
    //! Legacy method - checks if player is currently holding a specific device entity
    //! Uses cached data for performance
    bool IsHoldingDevice(IEntity device)
    {
        if (!device)
            return false;
        
        // Use cached lookup for performance
        return IsHeldDevice(device);
    }
    
    // ============================================
    // PUBLIC INTERFACE - System (Server-side calls)
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    void NotifyConnectedPlayers(array<int> connectedPlayerIDs)
    {
        Print("Recieved connected players on controller, role is server: ", Replication.IsServer());
        Rpc(RPC_SetTDLConnectedPlayers, connectedPlayerIDs);
    }
    
    //------------------------------------------------------------------------------------------------
    void NotifyNetworkMembers(int networkId, array<ref AG0_TDLNetworkMember> members)
    {
        Rpc(RPC_SetTDLNetworkMembers, networkId, members);
    }
    
    //------------------------------------------------------------------------------------------------
    void NotifyClearNetwork(int networkId)
    {
        Rpc(RPC_ClearTDLNetwork, networkId);
    }
    
    //------------------------------------------------------------------------------------------------
    void NotifyBroadcastingSources(array<RplId> broadcastingSources)
    {
        Rpc(RPC_SetNetworkBroadcastingSources, broadcastingSources);
    }
    
    // ============================================
    // PUBLIC INTERFACE - Controller (Owner-side calls)
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    void RequestKickDevice(RplId targetDeviceId)
    {
        Rpc(RpcAsk_KickDevice, targetDeviceId);
    }
    
    //------------------------------------------------------------------------------------------------
    void RequestSetDeviceCallsign(RplId deviceRplId, string callsign)
    {
        Rpc(RpcAsk_SetDeviceCallsign, deviceRplId, callsign);
    }
    
    //------------------------------------------------------------------------------------------------
    void RequestSetCameraBroadcasting(RplId deviceRplId, bool broadcasting)
    {
        Rpc(RpcAsk_SetCameraBroadcasting, deviceRplId, broadcasting);
    }
    
    // ============================================
    // VIDEO SOURCE MANAGEMENT
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    void RegisterBroadcastingDevice(RplId deviceId)
    {
        if (!m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.Insert(deviceId);
            m_bVideoSourcesDirty = true;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void UnregisterBroadcastingDevice(RplId deviceId)
    {
        if (m_StreamedBroadcastingDevices.Contains(deviceId))
        {
            m_StreamedBroadcastingDevices.RemoveItem(deviceId);
            m_bVideoSourcesDirty = true;
        }
    }
    
    //------------------------------------------------------------------------------------------------
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
    
    //------------------------------------------------------------------------------------------------
    bool IsVideoSourceAvailable(RplId sourceId)
	{
	    // Check local streaming devices first (client-registered)
	    if (m_StreamedBroadcastingDevices.Contains(sourceId))
	        return true;
	    
	    // Also check network-broadcast sources (server-replicated)
	    return m_AvailableVideoSourcesSet.Contains(sourceId);
	}
    
    // ============================================
    // PUBLIC GETTERS
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    array<int> GetTDLConnectedPlayers() { return m_aTDLConnectedPlayerIDs; }
    AG0_TDLNetworkMembers GetTDLNetworkMembers(int networkId) { return m_mTDLNetworkMembersMap.Get(networkId); }
    map<int, ref AG0_TDLNetworkMembers> GetAllTDLNetworks() { return m_mTDLNetworkMembersMap; }
    array<RplId> GetNetworkBroadcastingSources() { return m_NetworkBroadcastingSources; }
    bool IsSourceBroadcasting(RplId sourceId) { return m_AvailableVideoSourcesSet.Contains(sourceId); }
    
    //------------------------------------------------------------------------------------------------
    AG0_TDLNetworkMembers GetAggregatedTDLMembers()
    {
        AG0_TDLNetworkMembers aggregate = new AG0_TDLNetworkMembers();
        foreach (int networkId, AG0_TDLNetworkMembers networkData : m_mTDLNetworkMembersMap)
        {
            if (!networkData) continue;
            for (int i = 0; i < networkData.Count(); i++)
            {
                AG0_TDLNetworkMember member = networkData.Get(i);
                if (member)
                    aggregate.Add(member);
            }
        }
        return aggregate;
    }
    
    // ============================================
    // TDL NETWORK DIALOGS - Two-Dialog Sequential Flow
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    //! Public API - Called by device component to request network dialog
    //! This starts the two-dialog flow: Name → Password
    void RequestNetworkDialog(RplId deviceRplId, bool createMode)
    {
        if (System.IsConsoleApp())
            return;
        
        // Store pending state
        m_PendingDialogDeviceRplId = deviceRplId;
        m_bPendingCreateNetworkMode = createMode;
        m_sPendingNetworkName = "";
        
        // Show the first dialog (network name)
        ShowNetworkNameDialog();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ShowNetworkNameDialog()
    {
        // Clean up any existing dialog
        CleanupNetworkDialogs();
        
        string title = "JOIN TDL NETWORK";
		
		if(m_bPendingCreateNetworkMode)
			title = "CREATE TDL NETWORK";
        
        m_NetworkNameDialog = AG0_TDL_NetworkNameDialog.CreateDialog(title);
        if (!m_NetworkNameDialog)
        {
            Print("TDL_DIALOG: Failed to create network name dialog", LogLevel.ERROR);
            CleanupNetworkDialogState();
            return;
        }
        
        m_NetworkNameDialog.SetMessage("Enter network name");
        m_NetworkNameDialog.m_OnConfirm.Insert(OnNetworkNameConfirm);
        m_NetworkNameDialog.m_OnCancel.Insert(OnNetworkDialogCancel);
        
        Print("TDL_DIALOG: Opened network name dialog", LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnNetworkNameConfirm(SCR_ConfigurableDialogUi dialog)
    {
        AG0_TDL_NetworkNameDialog nameDialog = AG0_TDL_NetworkNameDialog.Cast(dialog);
        if (!nameDialog)
        {
            Print("TDL_DIALOG: Name dialog cast failed", LogLevel.ERROR);
            CleanupNetworkDialogState();
            return;
        }
        
        m_sPendingNetworkName = nameDialog.GetNetworkName();
        
        if (m_sPendingNetworkName.IsEmpty())
        {
            Print("TDL_DIALOG: Empty network name, cancelling", LogLevel.DEBUG);
            CleanupNetworkDialogState();
            return;
        }
        
        Print(string.Format("TDL_DIALOG: Network name entered: '%1'", m_sPendingNetworkName), LogLevel.DEBUG);
        
        // Clear the first dialog reference (it will close itself)
        m_NetworkNameDialog = null;
        
        // Show the password dialog
        ShowNetworkPasswordDialog();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ShowNetworkPasswordDialog()
    {
       	string title = "JOIN TDL NETWORK";
		
		if(m_bPendingCreateNetworkMode)
			title = "CREATE TDL NETWORK";
        
        m_NetworkPasswordDialog = AG0_TDL_NetworkPasswordDialog.CreateDialog(title);
        if (!m_NetworkPasswordDialog)
        {
            Print("TDL_DIALOG: Failed to create password dialog", LogLevel.ERROR);
            CleanupNetworkDialogState();
            return;
        }
        
        m_NetworkPasswordDialog.SetMessage(string.Format("Network: %1\nEnter password (optional)", m_sPendingNetworkName));
        m_NetworkPasswordDialog.m_OnConfirm.Insert(OnNetworkPasswordConfirm);
        m_NetworkPasswordDialog.m_OnCancel.Insert(OnNetworkDialogCancel);
        
        Print("TDL_DIALOG: Opened password dialog", LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnNetworkPasswordConfirm(SCR_ConfigurableDialogUi dialog)
    {
        AG0_TDL_NetworkPasswordDialog passDialog = AG0_TDL_NetworkPasswordDialog.Cast(dialog);
        string password = "";
        
        if (passDialog)
            password = passDialog.GetPassword();
        
        Print(string.Format("TDL_DIALOG: Submitting - Name='%1' Pass='%2' Create=%3 Device=%4", 
            m_sPendingNetworkName, password, m_bPendingCreateNetworkMode, m_PendingDialogDeviceRplId), LogLevel.DEBUG);
        
        // Send to server
        if (m_bPendingCreateNetworkMode)
            Rpc(RpcAsk_CreateNetworkForDevice, m_PendingDialogDeviceRplId, m_sPendingNetworkName, password);
        else
            Rpc(RpcAsk_JoinNetworkForDevice, m_PendingDialogDeviceRplId, m_sPendingNetworkName, password);
        
        // Clean up
        CleanupNetworkDialogState();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnNetworkDialogCancel(SCR_ConfigurableDialogUi dialog)
    {
        Print("TDL_DIALOG: Dialog cancelled", LogLevel.DEBUG);
        CleanupNetworkDialogState();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CleanupNetworkDialogs()
    {
        if (m_NetworkNameDialog)
        {
            m_NetworkNameDialog.Close();
            m_NetworkNameDialog = null;
        }
        if (m_NetworkPasswordDialog)
        {
            m_NetworkPasswordDialog.Close();
            m_NetworkPasswordDialog = null;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CleanupNetworkDialogState()
    {
        CleanupNetworkDialogs();
        m_PendingDialogDeviceRplId = RplId.Invalid();
        m_bPendingCreateNetworkMode = false;
        m_sPendingNetworkName = "";
    }
	
	//------------------------------------------------------------------------------------------------
	void RequestLeaveNetwork(RplId deviceRplId)
	{
	    Rpc(RpcAsk_LeaveNetwork, deviceRplId);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_LeaveNetwork(RplId deviceRplId)
	{
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (!system)
	        return;
	    
	    AG0_TDLDeviceComponent device = system.GetDeviceByRplId(deviceRplId);
	    if (!device)
	        return;
	    
	    system.LeaveNetwork(device);
	}
    
    // ============================================
    // RPCs - Owner (Client receives)
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetTDLConnectedPlayers(array<int> connectedPlayerIDs)
    {
        m_aTDLConnectedPlayerIDs = connectedPlayerIDs;
        //Print(string.Format("TDL_PLAYERCONTROLLER: Updated connected players: %1", connectedPlayerIDs), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetTDLNetworkMembers(int networkId, array<ref AG0_TDLNetworkMember> members)
    {
        AG0_TDLNetworkMembers membersData = new AG0_TDLNetworkMembers();
        foreach (AG0_TDLNetworkMember member : members)
            membersData.Add(member);
        
        m_mTDLNetworkMembersMap.Set(networkId, membersData);
        //Print(string.Format("TDL_PLAYERCONTROLLER: Received network %1 member update with %2 members", networkId, members.Count()), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_ClearTDLNetwork(int networkId)
    {
        m_mTDLNetworkMembersMap.Remove(networkId);
        //Print(string.Format("TDL_PLAYERCONTROLLER: Cleared network %1 data", networkId), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RPC_SetNetworkBroadcastingSources(array<RplId> broadcastingSources)
    {
        m_NetworkBroadcastingSources = broadcastingSources;
        m_AvailableVideoSourcesSet.Clear();
        foreach (RplId sourceId : broadcastingSources)
            m_AvailableVideoSourcesSet.Insert(sourceId);
        
        m_bVideoSourcesDirty = true;
        //Print(string.Format("TDL_PLAYERCONTROLLER: Updated broadcasting sources: %1", broadcastingSources.Count()), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Public method for server to request dialog on owning client
    //! Called by DeviceComponent when user action triggers network dialog
    void AskOpenNetworkDialog(RplId deviceRplId, bool createMode)
    {
        Rpc(RpcDo_OpenNetworkDialog, deviceRplId, createMode);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Server tells client to open network dialog - used when device initiates from user action
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    protected void RpcDo_OpenNetworkDialog(RplId deviceRplId, bool createMode)
    {
        if (System.IsConsoleApp())
            return;
        
        RequestNetworkDialog(deviceRplId, createMode);
    }
    
    // ============================================
    // RPCs - Server (Server receives)
    // ============================================
    
    //------------------------------------------------------------------------------------------------
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
    
    //------------------------------------------------------------------------------------------------
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
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SetDeviceCallsign(RplId deviceRplId, string callsign)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        
        AG0_TDLDeviceComponent device = system.GetDeviceByRplId(deviceRplId);
        if (!device)
            return;
        
        // Server-side call - SetCustomCallsign handles the logic + bump + system notify
        device.SetCustomCallsign(callsign);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Create network for a specific device
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_CreateNetworkForDevice(RplId deviceRplId, string networkName, string password)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        
        RplComponent rpl = RplComponent.Cast(Replication.FindItem(deviceRplId));
        if (!rpl) return;
        
        IEntity entity = rpl.GetEntity();
        if (!entity) return;
        
        AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
            entity.FindComponent(AG0_TDLDeviceComponent));
        
        if (!device)
            return;
        
        system.CreateNetwork(device, networkName, password);
        Print(string.Format("TDL_PC_RPC: Created network '%1' for device %2", networkName, deviceRplId), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Join network for a specific device
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_JoinNetworkForDevice(RplId deviceRplId, string networkName, string password)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        
        RplComponent rpl = RplComponent.Cast(Replication.FindItem(deviceRplId));
        if (!rpl) return;
        
        IEntity entity = rpl.GetEntity();
        if (!entity) return;
        
        AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
            entity.FindComponent(AG0_TDLDeviceComponent));
        
        if (!device)
            return;
        
        system.JoinNetwork(device, networkName, password);
        Print(string.Format("TDL_PC_RPC: Joined network '%1' for device %2", networkName, deviceRplId), LogLevel.DEBUG);
    }
	
	//------------------------------------------------------------------------------------------------
	protected void UpdateEUDCache()
	{
	    m_CachedEUDBoneComp = null;
	    
	    foreach (AG0_TDLDeviceComponent device : m_aHeldDevicesCache)
	    {
	        TDL_EUDBoneComponent boneComp = TDL_EUDBoneComponent.Cast(
	            device.GetOwner().FindComponent(TDL_EUDBoneComponent)
	        );
	        if (boneComp)
	        {
	            m_CachedEUDBoneComp = boneComp;
	            break;
	        }
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnEUDAdjustUp()
	{
	    if (!m_CachedEUDBoneComp)
	        return;
	    
	    AskServerAdjustEUD(EUD_ADJUST_STEP);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnEUDAdjustDown()
	{
	    if (!m_CachedEUDBoneComp)
	        return;
	    
	    AskServerAdjustEUD(-EUD_ADJUST_STEP);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void AskServerAdjustEUD(float delta)
	{
	    IEntity owner = m_CachedEUDBoneComp.GetOwner();
	    RplComponent rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
	    if (!rpl)
	        return;
	    
	    m_CachedEUDBoneComp.RequestAdjustment(delta);
	    Rpc(RpcAsk_AdjustEUD, rpl.Id(), delta);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_AdjustEUD(RplId eudRplId, float delta)
	{
	    RplComponent rpl = RplComponent.Cast(Replication.FindItem(eudRplId));
	    if (!rpl)
	        return;
	    
	    IEntity entity = rpl.GetEntity();
	    if (!entity)
	        return;
	    
	    TDL_EUDBoneComponent boneComp = TDL_EUDBoneComponent.Cast(
	        entity.FindComponent(TDL_EUDBoneComponent)
	    );
	    if (!boneComp)
	        return;
	    
	    boneComp.RequestAdjustment(delta);
	}
    
    //------------------------------------------------------------------------------------------------
    // Get callsign for a player in a specific network
    // Returns empty string if not found
    //------------------------------------------------------------------------------------------------
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
	
	//------------------------------------------------------------------------------------------------
    // Request to send a network broadcast message
    // Call this from UI when player sends a message to the network chat
    //------------------------------------------------------------------------------------------------
    static void RequestSendNetworkMessage(SCR_PlayerController controller, RplId senderDeviceRplId, string content)
    {
        // Validate on client
        if (content.IsEmpty()) return;
        if (content.Length() > 500)
        {
            content = content.Substring(0, 500); // Truncate
        }
        
        // Send to server
        controller.Rpc(controller.RpcAsk_SendTDLMessage, senderDeviceRplId, content, 
                      ETDLMessageType.NETWORK_BROADCAST, RplId.Invalid());
    }
    
    //------------------------------------------------------------------------------------------------
    // Request to send a direct message to a specific contact
    //------------------------------------------------------------------------------------------------
    static void RequestSendDirectMessage(SCR_PlayerController controller, RplId senderDeviceRplId, 
                                        string content, RplId recipientRplId)
    {
        // Validate on client
        if (content.IsEmpty()) return;
        if (recipientRplId == RplId.Invalid()) return;
        if (content.Length() > 500)
        {
            content = content.Substring(0, 500);
        }
        
        // Send to server
        controller.Rpc(controller.RpcAsk_SendTDLMessage, senderDeviceRplId, content,
                      ETDLMessageType.DIRECT, recipientRplId);
    }
    
    //------------------------------------------------------------------------------------------------
    // Notify server that player has read a message
    //------------------------------------------------------------------------------------------------
    static void RequestMarkMessageRead(SCR_PlayerController controller, RplId readerDeviceRplId, int messageId)
    {
        controller.Rpc(controller.RpcAsk_MarkTDLMessageRead, readerDeviceRplId, messageId);
    }
	
	//------------------------------------------------------------------------------------------------
    // CLIENT -> SERVER: Send a message
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_SendTDLMessage(RplId senderDeviceRplId, string content, 
                                         ETDLMessageType messageType, RplId recipientRplId)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system) return;
        
        system.SendTDLMessage(senderDeviceRplId, content, messageType, recipientRplId);
    }
    
    //------------------------------------------------------------------------------------------------
    // CLIENT -> SERVER: Mark message as read
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_MarkTDLMessageRead(RplId readerDeviceRplId, int messageId)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system) return;
        
        system.MarkTDLMessageRead(readerDeviceRplId, messageId);
    }
    
    //------------------------------------------------------------------------------------------------
    // SERVER -> CLIENT: Receive messages update
    // Called by system when messages change (new message, delivery status change, etc.)
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_ReceiveTDLMessages(int networkId, array<ref AG0_TDLMessageClient> messages)
	{
	    if (!m_mNetworkMessages.Contains(networkId))
	        m_mNetworkMessages.Set(networkId, new AG0_TDLMessageStore());
	    
	    AG0_TDLMessageStore store = m_mNetworkMessages.Get(networkId);
	    
	    array<int> newMessageIds = {};
	    
	    foreach (AG0_TDLMessageClient msg : messages)
	    {
	        if (!msg) continue;
	        
	        AG0_TDLMessageClient existing = store.GetByMessageId(msg.messageId);
	        if (!existing)
	            newMessageIds.Insert(msg.messageId);
	        
	        store.AddOrUpdateMessage(msg);
	    }
	    
	    Print(string.Format("TDL_MESSAGE_CLIENT: Received %1 messages for network %2 (%3 new)",
	        messages.Count(), networkId, newMessageIds.Count()), LogLevel.DEBUG);
	    
	    m_OnMessagesUpdated.Invoke(networkId);
	    
	    foreach (int newId : newMessageIds)
	    {
	        m_OnNewMessageReceived.Invoke(networkId, newId);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    // SERVER -> CLIENT: Receive read receipt notification
    //------------------------------------------------------------------------------------------------
    [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
    void RpcDo_ReceiveTDLReadReceipt(int networkId, int messageId, RplId readerRplId)
    {
        if (!m_mNetworkMessages.Contains(networkId)) return;
        
        AG0_TDLMessageStore store = m_mNetworkMessages.Get(networkId);
        AG0_TDLMessageClient msg = store.GetByMessageId(messageId);
        
        if (msg)
        {
            msg.status = ETDLMessageStatus.READ;
            
            Print(string.Format("TDL_MESSAGE_CLIENT: Message %1 read by %2",
                messageId, readerRplId), LogLevel.DEBUG);
            
            m_OnReadReceiptReceived.Invoke(networkId, messageId);
            m_OnMessagesUpdated.Invoke(networkId);
        }
    }
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_ReceiveTDLShapes(string packedShapes, string syncHash)
	{
		// Skip if we already have this version
		if (syncHash == m_sShapeSyncHash && !syncHash.IsEmpty())
			return;
		
		m_sShapeSyncHash = syncHash;
		
		if (!m_TDLShapeManager)
			m_TDLShapeManager = new AG0_TDLMapShapeManager();
		
		int count = m_TDLShapeManager.ParsePackedShapeData(packedShapes, syncHash);
		
		Print(string.Format("[TDL_SHAPES_CLIENT] Received %1 shapes (hash: %2)",
			count, syncHash), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get shape manager for map rendering
	AG0_TDLMapShapeManager GetTDLShapeManager()
	{
		return m_TDLShapeManager;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Server → Client: Send shape overlay data
	//! Called when shapes change or when player joins a network
	void ReceiveTDLShapes(string packedShapes, string syncHash)
	{
		Rpc(RpcDo_ReceiveTDLShapes, packedShapes, syncHash);
	}
    
    //------------------------------------------------------------------------------------------------
    // PUBLIC API: Called by server-side system to send messages to this client
    //------------------------------------------------------------------------------------------------
    void ReceiveTDLMessages(int networkId, array<ref AG0_TDLMessageClient> messages)
	{
	    Rpc(RpcDo_ReceiveTDLMessages, networkId, messages);
	}
    
    void ReceiveTDLReadReceipt(int networkId, int messageId, RplId readerRplId)
    {
        Rpc(RpcDo_ReceiveTDLReadReceipt, networkId, messageId, readerRplId);
    }
    
    //------------------------------------------------------------------------------------------------
    // CLIENT-SIDE API: Get messages for UI
    //------------------------------------------------------------------------------------------------
    
    // Get message store for a network
    AG0_TDLMessageStore GetTDLMessageStore(int networkId)
    {
        if (!m_mNetworkMessages.Contains(networkId))
            return null;
        return m_mNetworkMessages.Get(networkId);
    }
    
    // Get network broadcast messages
    array<ref AG0_TDLMessageClient> GetNetworkChatMessages(int networkId)
    {
        AG0_TDLMessageStore store = GetTDLMessageStore(networkId);
        if (!store) return new array<ref AG0_TDLMessageClient>();
        return store.GetNetworkMessages();
    }
    
    // Get direct messages with a contact
    array<ref AG0_TDLMessageClient> GetDirectMessages(int networkId, RplId myDeviceRplId, RplId contactRplId)
    {
        AG0_TDLMessageStore store = GetTDLMessageStore(networkId);
        if (!store) return new array<ref AG0_TDLMessageClient>();
        return store.GetDirectMessages(myDeviceRplId, contactRplId);
    }
    
    // Mark a message as locally read (client-side tracking)
    void MarkMessageLocallyRead(int messageId)
    {
        m_LocallyReadMessages.Insert(messageId);
    }
    
    // Check if message is locally read
    bool IsMessageLocallyRead(int messageId)
    {
        return m_LocallyReadMessages.Contains(messageId);
    }
    
    // Get unread count for network chat
    int GetNetworkChatUnreadCount(int networkId, RplId myDeviceRplId)
    {
        AG0_TDLMessageStore store = GetTDLMessageStore(networkId);
        if (!store) return 0;
        return store.CountUnreadInConversation(myDeviceRplId, "NETWORK", m_LocallyReadMessages);
    }
    
    // Get unread count for a direct conversation
    int GetDirectChatUnreadCount(int networkId, RplId myDeviceRplId, RplId contactRplId)
    {
        AG0_TDLMessageStore store = GetTDLMessageStore(networkId);
        if (!store) return 0;
        return store.CountUnreadInConversation(myDeviceRplId, contactRplId.ToString(), m_LocallyReadMessages);
    }
    
    // Get total unread count
    int GetTotalUnreadCount(int networkId, RplId myDeviceRplId)
    {
        AG0_TDLMessageStore store = GetTDLMessageStore(networkId);
        if (!store) return 0;
        return store.CountTotalUnread(myDeviceRplId, m_LocallyReadMessages);
    }
    
    // Subscribe to message updates
    ScriptInvoker GetOnMessagesUpdated() { return m_OnMessagesUpdated; }
    ScriptInvoker GetOnNewMessageReceived() { return m_OnNewMessageReceived; }
    ScriptInvoker GetOnReadReceiptReceived() { return m_OnReadReceiptReceived; }
	
}