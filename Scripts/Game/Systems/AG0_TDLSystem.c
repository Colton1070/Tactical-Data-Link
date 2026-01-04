// AG0_TDLSystem.c - Device-Centric TDL Network Management

class AG0_TDLNetwork
{
    protected int m_iNetworkID;
    protected string m_sNetworkName;
    protected string m_sNetworkPassword;
    protected ref array<AG0_TDLDeviceComponent> m_aNetworkDevices = {};
    protected ref map<RplId, ref AG0_TDLNetworkMember> m_mDeviceData = new map<RplId, ref AG0_TDLNetworkMember>();
    protected int m_iNextNetworkIP = 1;
	
	
	// Message storage
    protected ref array<ref AG0_TDLMessage> m_aMessages = {};
    protected int m_iNextMessageId = 1;
    
    // Message retention settings
    protected const int MAX_MESSAGES = 100;           // Maximum messages to retain
    protected const int MESSAGE_EXPIRY_SECONDS = 3600; // Messages expire after 1 hour
	
    
    void AG0_TDLNetwork(int networkID, string name, string password)
    {
        m_iNetworkID = networkID;
        m_sNetworkName = name;
        m_sNetworkPassword = password;
    }
    
    int GetNetworkID() { return m_iNetworkID; }
    string GetNetworkName() { return m_sNetworkName; }
    string GetNetworkPassword() { return m_sNetworkPassword; }
    array<AG0_TDLDeviceComponent> GetNetworkDevices() { return m_aNetworkDevices; }
    map<RplId, ref AG0_TDLNetworkMember> GetDeviceData() { return m_mDeviceData; }
	array<ref AG0_TDLMessage> GetMessages() { return m_aMessages; }
	int GetNextMessageId() { return m_iNextMessageId++; }
    
    void AddDevice(AG0_TDLDeviceComponent device, RplId deviceRplId, string playerName, vector position, int ownerPlayerId = -1)
    {
        if (!m_aNetworkDevices.Contains(device))
        {
            m_aNetworkDevices.Insert(device);
            
            AG0_TDLNetworkMember memberData = new AG0_TDLNetworkMember();
            memberData.SetRplId(deviceRplId);
            memberData.SetPlayerName(playerName);
            memberData.SetPosition(position);
            memberData.SetCapabilities(device.GetActiveCapabilities());
            memberData.SetNetworkIP(m_iNextNetworkIP++);
			memberData.SetOwnerPlayerId(ownerPlayerId);
            
            m_mDeviceData.Set(deviceRplId, memberData);
        }
    }
    
    void RemoveDevice(AG0_TDLDeviceComponent device)
    {
        int idx = m_aNetworkDevices.Find(device);
        if (idx != -1)
        {
            RplId deviceRplId = device.GetDeviceRplId();
            if (deviceRplId != RplId.Invalid())
                m_mDeviceData.Remove(deviceRplId);
            
            m_aNetworkDevices.Remove(idx);
        }
    }
    
    void UpdateDevicePosition(RplId deviceRplId, vector position)
    {
        AG0_TDLNetworkMember data = m_mDeviceData.Get(deviceRplId);
        if (data)
            data.SetPosition(position);
    }
    
    void UpdateDeviceCapabilities(RplId deviceRplId, int capabilities)
    {
        AG0_TDLNetworkMember data = m_mDeviceData.Get(deviceRplId);
        if (data)
            data.SetCapabilities(capabilities);
    }
    
    bool HasDevices()
    {
        return m_aNetworkDevices.Count() > 0;
    }
	
	//------------------------------------------------------------------------------------------------
    // Add a broadcast message to the network
    // Returns the message ID
    //------------------------------------------------------------------------------------------------
    static int AddBroadcastMessage(AG0_TDLNetwork network, RplId senderRplId, 
                                   string senderCallsign, string content,
                                   inout array<ref AG0_TDLMessage> messages, inout int nextMessageId)
    {
        AG0_TDLMessage msg = AG0_TDLMessage.CreateBroadcast(
            nextMessageId,
            network.GetNetworkID(),
            senderRplId,
            senderCallsign,
            content
        );
        
        messages.Insert(msg);
        nextMessageId++;
        
        // Prune old messages if over limit
        PruneMessages(messages);
        
        Print(string.Format("TDL_MESSAGE: Broadcast message %1 added from %2: '%3'", 
            msg.GetMessageId(), senderCallsign, content), LogLevel.DEBUG);
        
        return msg.GetMessageId();
    }
    
    //------------------------------------------------------------------------------------------------
    // Add a direct message to the network
    // Returns the message ID
    //------------------------------------------------------------------------------------------------
    static int AddDirectMessage(AG0_TDLNetwork network, RplId senderRplId, string senderCallsign,
                                string content, RplId recipientRplId, string recipientCallsign,
                                inout array<ref AG0_TDLMessage> messages, inout int nextMessageId)
    {
        AG0_TDLMessage msg = AG0_TDLMessage.CreateDirect(
            nextMessageId,
            network.GetNetworkID(),
            senderRplId,
            senderCallsign,
            content,
            recipientRplId,
            recipientCallsign
        );
        
        messages.Insert(msg);
        nextMessageId++;
        
        // Prune old messages if over limit
        PruneMessages(messages);
        
        Print(string.Format("TDL_MESSAGE: Direct message %1 added from %2 to %3: '%4'", 
            msg.GetMessageId(), senderCallsign, recipientCallsign, content), LogLevel.DEBUG);
        
        return msg.GetMessageId();
    }
    
    //------------------------------------------------------------------------------------------------
    // Get a message by ID
    //------------------------------------------------------------------------------------------------
    static AG0_TDLMessage GetMessageById(array<ref AG0_TDLMessage> messages, int messageId)
    {
        foreach (AG0_TDLMessage msg : messages)
        {
            if (msg.GetMessageId() == messageId)
                return msg;
        }
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    // Get all messages relevant to a device
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessage> GetMessagesForDevice(array<ref AG0_TDLMessage> messages, 
                                                          RplId deviceRplId)
    {
        array<ref AG0_TDLMessage> result = {};
        
        foreach (AG0_TDLMessage msg : messages)
        {
            if (msg.IsRelevantTo(deviceRplId) && msg.IsDeliveredTo(deviceRplId))
                result.Insert(msg);
        }
        
        return result;
    }
    
    //------------------------------------------------------------------------------------------------
    // Get undelivered messages that CAN be delivered to a device given its connectivity
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessage> GetDeliverableMessages(array<ref AG0_TDLMessage> messages,
                                                            RplId targetRplId, 
                                                            set<RplId> connectedDevices)
    {
        array<ref AG0_TDLMessage> result = {};
        
        foreach (AG0_TDLMessage msg : messages)
        {
            if (msg.CanDeliverTo(targetRplId, connectedDevices))
                result.Insert(msg);
        }
        
        return result;
    }
    
    //------------------------------------------------------------------------------------------------
    // Mark a message as read by a device
    //------------------------------------------------------------------------------------------------
    static void MarkMessageRead(array<ref AG0_TDLMessage> messages, int messageId, RplId readerRplId)
    {
        AG0_TDLMessage msg = GetMessageById(messages, messageId);
        if (msg)
        {
            msg.MarkReadBy(readerRplId);
            Print(string.Format("TDL_MESSAGE: Message %1 marked read by %2", 
                messageId, readerRplId), LogLevel.DEBUG);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Prune old messages to prevent unbounded growth
    //------------------------------------------------------------------------------------------------
    static void PruneMessages(inout array<ref AG0_TDLMessage> messages, 
                              int maxMessages = 100, int expirySeconds = 3600)
    {
        int currentTime = System.GetUnixTime();
        
        // Remove expired messages
        for (int i = messages.Count() - 1; i >= 0; i--)
        {
            if (currentTime - messages[i].GetTimestamp() > expirySeconds)
            {
                Print(string.Format("TDL_MESSAGE: Pruning expired message %1", 
                    messages[i].GetMessageId()), LogLevel.DEBUG);
                messages.Remove(i);
            }
        }
        
        // If still over limit, remove oldest
        while (messages.Count() > maxMessages)
        {
            Print(string.Format("TDL_MESSAGE: Pruning oldest message %1 (over limit)", 
                messages[0].GetMessageId()), LogLevel.DEBUG);
            messages.Remove(0);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Build client message array for a specific device
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessageClient> BuildClientMessages(array<ref AG0_TDLMessage> messages,
                                                                RplId viewerRplId)
    {
        array<ref AG0_TDLMessageClient> result = {};
        
        foreach (AG0_TDLMessage msg : messages)
        {
            if (msg.IsRelevantTo(viewerRplId) && msg.IsDeliveredTo(viewerRplId))
            {
                result.Insert(AG0_TDLMessageClient.FromServerMessage(msg, viewerRplId));
            }
        }
        
        return result;
    }
	
}

class AG0_TDLSystem : WorldSystem
{
	protected ref AG0_TDLApiManager m_ApiManager;
	// API sync intervals (in seconds)
	protected const float API_HEARTBEAT_INTERVAL = 60.0;      // Server heartbeat every 60s
	protected const float API_STATE_SYNC_INTERVAL = 30.0;     // Full state sync every 30s
	protected float m_fTimeSinceApiHeartbeat = 0;
	protected float m_fTimeSinceApiStateSync = 0;

    // Networks storage
    protected ref array<ref AG0_TDLNetwork> m_aNetworks = {};
    protected int m_iNextNetworkID = 1;
    
    // Configuration
    protected float m_fUpdateInterval = 5.0;
    protected float m_fTimeSinceLastUpdate = 0;
    
    // All registered network devices
    protected ref array<AG0_TDLDeviceComponent> m_aRegisteredNetworkDevices = {};
    
    // Temporary arrays for connectivity calculations
    protected ref array<AG0_TDLDeviceComponent> m_aProcessedDevices = {};
    protected ref array<AG0_TDLDeviceComponent> m_aConnectedDevices = {};
    
    // System callbacks for map markers
    protected ref array<AG0_TDLMapMarkerEntry> m_MarkerCallbacks = {};
	
	protected ref map<RplId, AG0_TDLDeviceComponent> m_mDeviceCache = new map<RplId, AG0_TDLDeviceComponent>();
    
    protected float m_fGridCellSize = 2000.0; // Set to 2x your maximum device range
    protected ref map<string, ref array<AG0_TDLDeviceComponent>> m_mSpatialGrid = new map<string, ref array<AG0_TDLDeviceComponent>>();
    protected float m_fTimeSinceGridRebuild = 999.0; // Force rebuild on first update
    protected float m_fGridRebuildInterval = 5.0; // Rebuild every 5 seconds
	protected float m_fMaxDeviceRange = 1000.0;  // Track maximum range
	protected bool m_bCellSizeNeedsUpdate = false; // Flag for cell size change

	
    //------------------------------------------------------------------------------------------------
    override static void InitInfo(WorldSystemInfo outInfo)
	{
		super.InitInfo(outInfo);
	    
	    Print("TDL_SYSTEM_INIT: InitInfo called", LogLevel.DEBUG);
	    outInfo
	        .SetAbstract(false)
	        .SetLocation(WorldSystemLocation.Server)
	        .AddPoint(WorldSystemPoint.Frame);
	        //.AddController(AG0_TDLController);
	        
	    Print("AG0_TDLSystem: Device-centric system initialized", LogLevel.DEBUG);
	}
    
    //--------------------------------------------------------------------------
    // Static instance getter for easy access from controller
    //--------------------------------------------------------------------------
    
    static AG0_TDLSystem GetInstance()
    {
        World world = GetGame().GetWorld();
        if (!world)
            return null;
        
        return AG0_TDLSystem.Cast(world.FindSystem(AG0_TDLSystem));
    }
    
    //------------------------------------------------------------------------------------------------
    // Public helper methods for PlayerController and other systems
    //------------------------------------------------------------------------------------------------
	array<ref AG0_TDLNetwork> GetNetworks() { return m_aNetworks; }

	//------------------------------------------------------------------------------------------------
	// Message API - called from PlayerController
	//------------------------------------------------------------------------------------------------
	void SendTDLMessage(RplId senderDeviceRplId, string content, ETDLMessageType messageType, RplId recipientRplId = RplId.Invalid())
	{
	    SendMessage(this, senderDeviceRplId, content, messageType, recipientRplId);
	}
	
	void MarkTDLMessageRead(RplId readerDeviceRplId, int messageId)
	{
	    MarkMessageRead(this, readerDeviceRplId, messageId);
	}
	
    AG0_TDLDeviceComponent GetDeviceByRplId(RplId deviceId)
    {
        return m_mDeviceCache.Get(deviceId);
    }
    
    IEntity GetPlayerFromDevice(AG0_TDLDeviceComponent device)
	{
	    IEntity owner = device.GetOwner();
	    while (owner)
	    {
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(owner);
	        
            if (playerId > 0)
               return owner;
			//TODO: just return the playerId when found... since that's what we need to use anyways.
	        
	        owner = owner.GetParent();
	    }
	    return null;
	}
    
	override protected void OnInit()
	{
	    super.OnInit();
	    
	    // CRITICAL: Only initialize API on the server
	    if (!Replication.IsServer())
	    {
	        Print("TDL_SYSTEM: Running on client/proxy - skipping API initialization", LogLevel.DEBUG);
	        return;
	    }
	    
	    // Initialize API Manager
	    m_ApiManager = new AG0_TDLApiManager();
	    if (m_ApiManager.Initialize())
	    {
	        Print("TDL_SYSTEM: API Manager initialized successfully", LogLevel.DEBUG);
	    }
	    else
	    {
	        Print("TDL_SYSTEM: API Manager initialization failed", LogLevel.DEBUG);
	        m_ApiManager = null;
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    override protected void OnUpdatePoint(WorldUpdatePointArgs args)
    {
        if (!Replication.IsServer()) return;
        
        float timeSlice = GetWorld().GetFixedTimeSlice();
        m_fTimeSinceLastUpdate += timeSlice;
        
        if (m_fTimeSinceLastUpdate >= m_fUpdateInterval)
        {
            UpdateNetworks();
            m_fTimeSinceLastUpdate = 0;
        }
		
		if (m_ApiManager)
	    {
	        m_ApiManager.Update(timeSlice);
	        
	        // Heartbeat
	        m_fTimeSinceApiHeartbeat += timeSlice;
	        if (m_fTimeSinceApiHeartbeat >= API_HEARTBEAT_INTERVAL)
	        {
	            ApiSendHeartbeat();
	            m_fTimeSinceApiHeartbeat = 0;
	        }
	        
	        // Periodic state sync
	        m_fTimeSinceApiStateSync += timeSlice;
	        if (m_fTimeSinceApiStateSync >= API_STATE_SYNC_INTERVAL)
	        {
	            ApiSyncFullState();
	            m_fTimeSinceApiStateSync = 0;
	        }
	    }
    }
    
    int GetAggregatedPlayerCapabilities(IEntity player)
    {
        if (!player) return 0;
        
        int aggregated = 0;
        array<AG0_TDLDeviceComponent> devices = GetPlayerAllTDLDevices(player);
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.IsPowered())
                aggregated |= device.GetActiveCapabilities();
        }
        return aggregated;
    }
	
	array<AG0_TDLDeviceComponent> GetPlayerAllTDLDevices(IEntity playerEntity)
	{
	    array<AG0_TDLDeviceComponent> allDevices = {};
	    if (!playerEntity) return allDevices;
	    
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
	            if (deviceComp)
	                allDevices.Insert(deviceComp);
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
	            if (deviceComp)
	                allDevices.Insert(deviceComp);
	        }
	    }
	    
	    // Check equipment slots using RHS pattern
	    ChimeraCharacter character = ChimeraCharacter.Cast(playerEntity);
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
	                
	                foreach (IEntity clothItem : clothItems)
	                {
	                    AG0_TDLDeviceComponent deviceComp = AG0_TDLDeviceComponent.Cast(
	                        clothItem.FindComponent(AG0_TDLDeviceComponent));
	                    if (deviceComp)
	                        allDevices.Insert(deviceComp);
	                }
	            }
	        }
	    }
	    
	    return allDevices;
	}
	
	string GetGridCellKey(vector pos)
    {
        int x = Math.Floor(pos[0] / m_fGridCellSize);
        int y = Math.Floor(pos[1] / m_fGridCellSize);
        int z = Math.Floor(pos[2] / m_fGridCellSize);
        return string.Format("%1_%2_%3", x, y, z);
    }
    
    void RebuildSpatialGrid()
    {
        m_mSpatialGrid.Clear();
        
        foreach (AG0_TDLDeviceComponent device : m_aRegisteredNetworkDevices)
        {
            string cellKey = GetGridCellKey(device.GetOwner().GetOrigin());
            
            if (!m_mSpatialGrid.Contains(cellKey))
                m_mSpatialGrid.Set(cellKey, new array<AG0_TDLDeviceComponent>);
            
            m_mSpatialGrid.Get(cellKey).Insert(device);
        }
        
        Print(string.Format("TDL_SPATIAL_GRID: Rebuilt with %1 devices in %2 cells", 
            m_aRegisteredNetworkDevices.Count(), m_mSpatialGrid.Count()), LogLevel.DEBUG);
    }
	
	protected void UpdateMaxDeviceRange()
	{
	    float previousMaxRange = m_fMaxDeviceRange;
	    float currentMaxRange = 0.0;
	    
	    // Find the device with the longest range
	    foreach (AG0_TDLDeviceComponent device : m_aRegisteredNetworkDevices)
	    {
	        float deviceRange = device.GetEffectiveNetworkRange();
	        if (deviceRange > currentMaxRange)
	            currentMaxRange = deviceRange;
	    }
	    
	    // Only update if range changed significantly (>10% or >100m)
	    float rangeDifference = Math.AbsFloat(currentMaxRange - previousMaxRange);
	    if (rangeDifference > 100.0 || rangeDifference > (previousMaxRange * 0.1))
	    {
	        m_fMaxDeviceRange = currentMaxRange;
	        m_fGridCellSize = 2.0 * m_fMaxDeviceRange;  // Always 2x max range
	        m_fTimeSinceGridRebuild = 999.0;  // Force immediate rebuild
	        
	        Print(string.Format("TDL_SPATIAL_GRID: Max device range updated to %1m, cell size now %2m", 
	            m_fMaxDeviceRange, m_fGridCellSize), LogLevel.DEBUG);
	    }
	}
    
    array<AG0_TDLDeviceComponent> GetNearbyDevices(vector pos, AG0_TDLNetwork network)
    {
        array<AG0_TDLDeviceComponent> nearby = {};
        array<AG0_TDLDeviceComponent> networkDevices = network.GetNetworkDevices();
        
        int cx = Math.Floor(pos[0] / m_fGridCellSize);
        int cy = Math.Floor(pos[1] / m_fGridCellSize);
        int cz = Math.Floor(pos[2] / m_fGridCellSize);
        
        // Check 3x3x3 cube of cells (27 cells total in 3D space)
        for (int dx = -1; dx <= 1; dx++)
        {
            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dz = -1; dz <= 1; dz++)
                {
                    string cellKey = string.Format("%1_%2_%3", cx + dx, cy + dy, cz + dz);
                    array<AG0_TDLDeviceComponent> cellDevices = m_mSpatialGrid.Get(cellKey);
                    
                    if (cellDevices)
                    {
                        foreach (AG0_TDLDeviceComponent device : cellDevices)
                        {
                            // Only include devices from this specific network
                            if (networkDevices.Contains(device))
                                nearby.Insert(device);
                        }
                    }
                }
            }
        }
        
        return nearby;
    }
    
    //------------------------------------------------------------------------------------------------
    void RegisterMarkerCallback(AG0_TDLMapMarkerEntry markerEntry)
    {
        if (!m_MarkerCallbacks.Contains(markerEntry))
            m_MarkerCallbacks.Insert(markerEntry);
    }
    
    //------------------------------------------------------------------------------------------------
    // Device registration methods
    //------------------------------------------------------------------------------------------------
    void RegisterDevice(AG0_TDLDeviceComponent device)
	{
	    if (!Replication.IsServer()) return;
	    
	    if (!device)
	    {
	        Print("TDL_DEVICE_REGISTRATION: Device is null in RegisterDevice", LogLevel.DEBUG);
	        return;
	    }
	    
	    IEntity owner = device.GetOwner();
	    if (!owner)
	    {
	        Print("TDL_DEVICE_REGISTRATION: Device owner is null in RegisterDevice", LogLevel.DEBUG);
	        return;
	    }
	    
	    // Just register immediately - no delay needed
	    DelayedDeviceRegistration(device);
	}
	
	void DelayedDeviceRegistration(AG0_TDLDeviceComponent device)
	{
	    if (!device)
	    {
	        Print("TDL_DEVICE_REGISTRATION: Device is null", LogLevel.DEBUG);
	        return;
	    }
	    
	    IEntity owner = device.GetOwner();
	    if (!owner || owner.IsDeleted()) 
	    {
	        Print("TDL_DEVICE_REGISTRATION: Device owner was deleted before registration", LogLevel.DEBUG);
	        return;
	    }
	    
	    if (m_aRegisteredNetworkDevices.Contains(device))
	    {
	        Print("TDL_DEVICE_REGISTRATION: Device already registered", LogLevel.DEBUG);
	        return;
	    }
	    
	    // Only register devices with network capability
	    if (!device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS))
	    {
	        Print("TDL_DEVICE_REGISTRATION: Device lacks NETWORK_ACCESS capability", LogLevel.DEBUG);
	        return;
	    }
	    
	    m_aRegisteredNetworkDevices.Insert(device);
    
	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId != RplId.Invalid())
	        m_mDeviceCache.Set(deviceRplId, device);
	    
	    LogDeviceRegistration(device, true);
	    
	    m_fTimeSinceGridRebuild = 999.0;
	    
	    foreach (AG0_TDLMapMarkerEntry markerEntry : m_MarkerCallbacks)
	    {
	        markerEntry.OnDeviceRegistered(device);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    void UnregisterDevice(AG0_TDLDeviceComponent device)
	{
	    if (!Replication.IsServer()) return;
	    
	    int idx = m_aRegisteredNetworkDevices.Find(device);
	    if (idx == -1)
	    {
	        Print("TDL_DEVICE_UNREGISTRATION: Device was not registered", LogLevel.DEBUG);
	        return;
	    }
	    
	    m_aRegisteredNetworkDevices.Remove(idx);
    
	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId != RplId.Invalid())
	        m_mDeviceCache.Remove(deviceRplId);
	    
	    LogDeviceRegistration(device, false);
	    
	    m_fTimeSinceGridRebuild = 999.0;
	    
	    // Remove from any networks
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkDevices().Contains(device))
	        {
	            Print(string.Format("TDL_NETWORK_CLEANUP: Removing device from network %1", network.GetNetworkName()), LogLevel.DEBUG);
	            network.RemoveDevice(device);
	        }
	    }
	    
	    // Clean up empty networks
	    int networksRemoved = 0;
	    for (int i = m_aNetworks.Count() - 1; i >= 0; i--)
	    {
	        if (!m_aNetworks[i].HasDevices())
	        {
	            Print(string.Format("TDL_NETWORK_CLEANUP: Removing empty network %1", m_aNetworks[i].GetNetworkName()), LogLevel.DEBUG);
	            ApiNotifyNetworkDeleted(m_aNetworks[i].GetNetworkID(), m_aNetworks[i].GetNetworkName());
				m_aNetworks.Remove(i);
	            networksRemoved++;
	        }
	    }
	    
	    if (networksRemoved > 0)
	        Print(string.Format("TDL_NETWORK_CLEANUP: Removed %1 empty networks", networksRemoved), LogLevel.DEBUG);
	    
	    foreach (AG0_TDLMapMarkerEntry markerEntry : m_MarkerCallbacks)
	    {
	        markerEntry.OnDeviceUnregistered(device);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    // Network creation and management
    //------------------------------------------------------------------------------------------------
    int CreateNetwork(AG0_TDLDeviceComponent creator, string networkName, string password)
	{
	    if (!Replication.IsServer()) return -1;
	    if (!creator || !creator.CanAccessNetwork()) 
	    {
	        Print("TDL_NETWORK_CREATE: Invalid creator device", LogLevel.DEBUG);
	        return -1;
	    }
	    
	    RplId deviceRplId = creator.GetDeviceRplId();
	    string playerName = creator.GetOwnerPlayerName();
	    vector position = creator.GetOwner().GetOrigin();
	    
	    if (deviceRplId == RplId.Invalid())
	    {
	        Print("TDL_NETWORK_CREATE: Invalid device RplId", LogLevel.DEBUG);
	        return -1;
	    }
	    
	    Print(string.Format("TDL_NETWORK_CREATE: Attempting to create network '%1' by %2", networkName, playerName), LogLevel.DEBUG);
	    
	    // Check for existing network with same credentials
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkName() == networkName && network.GetNetworkPassword() == password)
	        {
	            Print(string.Format("TDL_NETWORK_CREATE: Network '%1' already exists, joining instead", networkName), LogLevel.DEBUG);
	            JoinNetwork(creator, networkName, password);
	            return network.GetNetworkID();
	        }
	    }
	    
	    // Create new network
	    AG0_TDLNetwork newNetwork = new AG0_TDLNetwork(m_iNextNetworkID++, networkName, password);
	    newNetwork.AddDevice(creator, deviceRplId, creator.GetDisplayName(), position);
	    m_aNetworks.Insert(newNetwork);
	    
	    Print(string.Format("TDL_NETWORK_CREATE: Successfully created network '%1' (ID: %2)", networkName, newNetwork.GetNetworkID()), LogLevel.DEBUG);
	    
	    NotifyNetworkJoined(creator, newNetwork.GetNetworkID(), newNetwork.GetDeviceData());
		ApiNotifyNetworkCreated(newNetwork, playerName);
	    
	    return newNetwork.GetNetworkID();
	}
    
    bool JoinNetwork(AG0_TDLDeviceComponent device, string networkName, string password)
	{
	    if (!Replication.IsServer()) return false;
	    if (!device || !device.CanAccessNetwork())
	    {
	        Print("TDL_NETWORK_JOIN: Invalid device", LogLevel.DEBUG);
	        return false;
	    }
	    
	    RplId deviceRplId = device.GetDeviceRplId();
	    string playerName = device.GetOwnerPlayerName();
	    vector position = device.GetOwner().GetOrigin();
	    
	    if (deviceRplId == RplId.Invalid())
	    {
	        Print("TDL_NETWORK_JOIN: Invalid device RplId", LogLevel.DEBUG);
	        return false;
	    }
	    
	    Print(string.Format("TDL_NETWORK_JOIN: %1 attempting to join network '%2'", playerName, networkName), LogLevel.DEBUG);
	    
	    // Find matching networks
	    array<AG0_TDLNetwork> matchingNetworks = new array<AG0_TDLNetwork>();
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkName() == networkName && network.GetNetworkPassword() == password)
	            matchingNetworks.Insert(network);
	    }
	    
	    if (matchingNetworks.IsEmpty())
	    {
	        Print(string.Format("TDL_NETWORK_JOIN: No matching networks found for '%1'", networkName), LogLevel.DEBUG);
	        return false;
	    }
	    
	    Print(string.Format("TDL_NETWORK_JOIN: Found %1 matching networks", matchingNetworks.Count()), LogLevel.DEBUG);
	    
	    // Check if in range of any existing network device
	    foreach (AG0_TDLNetwork network : matchingNetworks)
	    {
	        if (IsDeviceInNetworkRange(device, network))
	        {
	            Print(string.Format("TDL_NETWORK_JOIN: Device in range of network '%1', joining", network.GetNetworkName()), LogLevel.DEBUG);
	            network.AddDevice(device, deviceRplId, device.GetDisplayName(), position);
	            NotifyNetworkMembersUpdated(network);
				ApiNotifyDeviceJoined(network, device);
	            return true;
	        }
	    }
	    
	    Print(string.Format("TDL_NETWORK_JOIN: Device not in range of any matching networks"), LogLevel.DEBUG);
	    return false;
	}
    
    void LeaveNetwork(AG0_TDLDeviceComponent device)
    {
        if (!Replication.IsServer()) return;
        
        foreach (AG0_TDLNetwork network : m_aNetworks)
        {
            if (network.GetNetworkDevices().Contains(device))
            {
                network.RemoveDevice(device);
                NotifyNetworkLeft(device);
				ApiNotifyDeviceLeft(network.GetNetworkID(), network.GetNetworkName(), device.GetDisplayName());
                
                if (network.HasDevices())
                {
                    NotifyNetworkMembersUpdated(network);
                }
                else
                {
                    m_aNetworks.RemoveItem(network);
                }
                
                break;
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Device connectivity checks
    //------------------------------------------------------------------------------------------------
    bool IsDeviceInNetworkRange(AG0_TDLDeviceComponent device, AG0_TDLNetwork network)
    {
        if (!device || !network) return false;
        
        m_aProcessedDevices.Clear();
        m_aConnectedDevices.Clear();
        
        foreach (AG0_TDLDeviceComponent networkDevice : network.GetNetworkDevices())
        {
            if (networkDevice == device) return true;
            
            FindConnectedDevices(networkDevice, device);
            
            if (m_aConnectedDevices.Contains(device))
                return true;
        }
        
        return false;
    }
    
    bool AreDevicesConnected(AG0_TDLDeviceComponent deviceA, AG0_TDLDeviceComponent deviceB)
	{
	    if (!deviceA || !deviceB) return false;
	    if (!deviceA.CanAccessNetwork() || !deviceB.CanAccessNetwork()) return false;
	    
	    vector posA = deviceA.GetOwner().GetOrigin();
	    vector posB = deviceB.GetOwner().GetOrigin();
	    
	    float rangeA = deviceA.GetEffectiveNetworkRange();
	    float rangeB = deviceB.GetEffectiveNetworkRange();
	    float maxPossibleRange = Math.Max(rangeA, rangeB);
	    
	    // OPTIMIZATION: Early rejection using axis-aligned bounding box (AABB)
	    // This is much cheaper than distance calculation (no sqrt)
	    if (Math.AbsFloat(posA[0] - posB[0]) > maxPossibleRange) return false;
	    if (Math.AbsFloat(posA[1] - posB[1]) > maxPossibleRange) return false;
	    if (Math.AbsFloat(posA[2] - posB[2]) > maxPossibleRange) return false;
	    
	    // Now do the actual distance calculation
	    float distance = vector.Distance(posA, posB);
	    float maxRange = Math.Min(rangeA, rangeB);
	    
	    bool connected = distance <= maxRange;
	    
	    LogConnectivityCheck(deviceA, deviceB, connected, distance, maxRange);
	    
	    return connected;
	}
    
    protected void FindConnectedDevices(AG0_TDLDeviceComponent source, AG0_TDLDeviceComponent target)
    {
        if (m_aProcessedDevices.Contains(source)) return;
        
        m_aProcessedDevices.Insert(source);
        
        if (!source.CanAccessNetwork()) return;
        
        if (AreDevicesConnected(source, target))
        {
            m_aConnectedDevices.Insert(target);
            return;
        }
        
        foreach (AG0_TDLDeviceComponent device : m_aRegisteredNetworkDevices)
        {
            if (device == source || m_aProcessedDevices.Contains(device)) continue;
            if (!device.CanAccessNetwork()) continue;
            
            if (AreDevicesConnected(source, device))
            {
                FindConnectedDevices(device, target);
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Network update logic with player capability aggregation
    //------------------------------------------------------------------------------------------------
   	protected void UpdateNetworks()
	{
	    if (!Replication.IsServer()) return;
	    
	    //Print("TDL_SYSTEM_UPDATE: Starting network update cycle", LogLevel.DEBUG);
	    //LogNetworkState("UpdateNetworks");
		
		UpdateMaxDeviceRange();
	    
	    // OPTIMIZATION: Rebuild spatial grid periodically
	    m_fTimeSinceGridRebuild += GetWorld().GetFixedTimeSlice();
	    if (m_fTimeSinceGridRebuild >= m_fGridRebuildInterval)
	    {
	        RebuildSpatialGrid();
	        m_fTimeSinceGridRebuild = 0;
	    }
	    
	    CheckNetworkMerges();
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        UpdateNetworkConnectivity(network);
	    }
	    
	    // Update video streaming after network connectivity changes
	    UpdateVideoStreaming();
	    
	    //Print("TDL_SYSTEM_UPDATE: Network update cycle complete", LogLevel.DEBUG);
	}
    
    protected void CheckNetworkMerges()
    {
        for (int i = 0; i < m_aNetworks.Count() - 1; i++)
        {
            AG0_TDLNetwork networkA = m_aNetworks[i];
            
            for (int j = i + 1; j < m_aNetworks.Count(); j++)
            {
                AG0_TDLNetwork networkB = m_aNetworks[j];
                
                if (networkA.GetNetworkName() != networkB.GetNetworkName() || 
                    networkA.GetNetworkPassword() != networkB.GetNetworkPassword())
                    continue;
                
                bool canMerge = false;
                foreach (AG0_TDLDeviceComponent deviceB : networkB.GetNetworkDevices())
                {
                    if (IsDeviceInNetworkRange(deviceB, networkA))
                    {
                        canMerge = true;
                        break;
                    }
                }
                
                if (canMerge)
                {
                    array<AG0_TDLDeviceComponent> devicesToMove = {};
                    devicesToMove.Copy(networkB.GetNetworkDevices());
                    
                    foreach (AG0_TDLDeviceComponent device : devicesToMove)
                    {
                        networkB.RemoveDevice(device);
                        
                        RplId deviceRplId = device.GetDeviceRplId();
                        string playerName = device.GetOwnerPlayerName();
                        vector position = device.GetOwner().GetOrigin();
                        
                        if (deviceRplId != RplId.Invalid())
                        {
                            networkA.AddDevice(device, deviceRplId, playerName, position);
                        }
                    }
                    
                    m_aNetworks.Remove(j);
                    j--;
                    
                    NotifyNetworkMembersUpdated(networkA);
                }
            }
        }
    }
    
    protected void UpdateNetworkConnectivity(AG0_TDLNetwork network)
	{
	    if (!network || !network.HasDevices()) return;
	    
	    foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	    {
	        if (!device.CanAccessNetwork()) continue;
	        
	        // First, update this device's position in the authoritative network data
	        RplId deviceRplId = device.GetDeviceRplId();
	        if (deviceRplId != RplId.Invalid())
	        {
	            IEntity deviceEntity = device.GetOwner();
	            if (deviceEntity)
	            {
	                vector currentPos = deviceEntity.GetOrigin();
	                network.UpdateDevicePosition(deviceRplId, currentPos);
	            }
	        }
	        
	        m_aProcessedDevices.Clear();
	        m_aConnectedDevices.Clear();
	        
	        FindConnectedDevicesInNetwork(device, network);
	        
	        if (!m_aConnectedDevices.Contains(device))
	            m_aConnectedDevices.Insert(device);
	        
	        ref map<RplId, ref AG0_TDLNetworkMember> connectedMembers = new map<RplId, ref AG0_TDLNetworkMember>();
	        
	        foreach (AG0_TDLDeviceComponent connectedDevice : m_aConnectedDevices)
	        {
	            RplId connectedRplId = connectedDevice.GetDeviceRplId();
	            
	            if (connectedRplId != RplId.Invalid())
	            {
	                // Update the connected device's position in authoritative data too
	                IEntity connectedEntity = connectedDevice.GetOwner();
	                if (connectedEntity)
	                {
	                    vector connectedCurrentPos = connectedEntity.GetOrigin();
	                    network.UpdateDevicePosition(connectedRplId, connectedCurrentPos);
	                }
	                
	                AG0_TDLNetworkMember memberData = network.GetDeviceData().Get(connectedRplId);
	                if (memberData && connectedEntity)
	                {
	                    IEntity deviceEntity = device.GetOwner();
	                    
	                    if (deviceEntity)
	                    {
	                        vector devicePos = deviceEntity.GetOrigin();
	                        vector connectedPos = connectedEntity.GetOrigin();
	                        float distance = vector.Distance(devicePos, connectedPos);
	                        
	                        AG0_TDLNetworkMember connectedData = new AG0_TDLNetworkMember();
	                        connectedData.SetRplId(memberData.GetRplId());
	                        connectedData.SetPlayerName(connectedDevice.GetDisplayName());
	                        // Use LIVE position, not stored position!
	                        connectedData.SetPosition(connectedPos);
	                        connectedData.SetNetworkIP(memberData.GetNetworkIP());
							IEntity connectedPlayer = GetPlayerFromDevice(connectedDevice);
							int ownerPlayerId = -1;
							if (connectedPlayer)
							{
							    PlayerManager playerMgr = GetGame().GetPlayerManager();
							    ownerPlayerId = playerMgr.GetPlayerIdFromControlledEntity(connectedPlayer);
								array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(connectedPlayer);
								int aggregatedCaps = 0;
							    foreach (AG0_TDLDeviceComponent dev : playerDevices)
							    {
							        if (dev.IsCameraBroadcasting() && dev.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
							        {
							            connectedData.SetVideoSourceRplId(dev.GetDeviceRplId());
							        }
									if (dev.IsPowered())
                						aggregatedCaps |= dev.GetActiveCapabilities();
							    }
								connectedData.SetCapabilities(aggregatedCaps);
							}
							else {
								connectedData.SetCapabilities(memberData.GetCapabilities());
							}
							connectedData.SetOwnerPlayerId(ownerPlayerId);
	                        
	                        float effectiveRange = Math.Min(device.GetEffectiveNetworkRange(), 
							                                connectedDevice.GetEffectiveNetworkRange());
							float signalStrength = Math.Clamp(100.0 * (1.0 - (distance / effectiveRange)), 0.0, 100.0);
							connectedData.SetSignalStrength(signalStrength);
	                        
	                        connectedMembers.Set(connectedRplId, connectedData);
	                    }
	                }
	            }
	        }
	        
	        // Use the fixed NotifyNetworkConnectivity which handles everything
	        NotifyNetworkConnectivity(device, connectedMembers);
			// Propagate messages based on new connectivity
    		PropagateMessagesForDevice(this, network, device, connectedMembers);
	    }
		// After all devices processed, derive player connectivity
	    map<int, ref set<int>> playerConnections = new map<int, ref set<int>>();
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    
	    // Walk through all devices and their connections
	    foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	    {
	        IEntity playerEntity = GetPlayerFromDevice(device);
	        if (!playerEntity) continue;
	        
	        int ownerID = playerMgr.GetPlayerIdFromControlledEntity(playerEntity);
			PlayerController pc = playerMgr.GetPlayerController(ownerID);
			Print(string.Format("DEBUG_PLAYERID: Entity %1 -> PlayerId %2 -> PC PlayerId %3", playerEntity, ownerID, pc.GetPlayerId()), LogLevel.DEBUG);
			
	        if (!ownerID) continue;
	        
	        if (!playerConnections.Contains(ownerID))
	            playerConnections.Insert(ownerID, new set<int>());
	        
	        // Check device's connected members
	        array<RplId> connectedRplIds = device.GetConnectedMembers();
	        foreach (RplId connectedRplId : connectedRplIds)
	        {
	            AG0_TDLDeviceComponent connectedDevice = GetDeviceByRplId(connectedRplId);
	            if (!connectedDevice) continue;
	            
	            IEntity connectedPlayerEntity = GetPlayerFromDevice(connectedDevice);
	            if (!connectedPlayerEntity) continue;
	            
	            int connectedOwnerID = playerMgr.GetPlayerIdFromControlledEntity(connectedPlayerEntity);
	            if (connectedOwnerID > 0)
    				playerConnections[ownerID].Insert(connectedOwnerID);
	        }
	    }
	    
	    // Convert and RPC
	    foreach (int playerID, set<int> connections : playerConnections)
		{
		    array<int> connArray = {};
		    foreach (int id : connections)
		        connArray.Insert(id);
		    
		    // Get the specific player's controller
		    SCR_PlayerController controller = SCR_PlayerController.Cast(
		        GetGame().GetPlayerManager().GetPlayerController(playerID)
		    );
		    
		    if (!controller) 
		    {
		        Print(string.Format("TDL_System: Controller not found for player %1", playerID), LogLevel.DEBUG);
		        continue; // Changed from return to continue so other players still get notified
		    }
		    
		    //PrintFormat("TDL_System: Notifying player controller for player %1, %2", playerID, controller, LogLevel.DEBUG);
		    controller.NotifyConnectedPlayers(connArray);
		    //PrintFormat("Notified connected players: %1", connArray, LogLevel.DEBUG);
		}
	}
    
   	protected void FindConnectedDevicesInNetwork(AG0_TDLDeviceComponent source, AG0_TDLNetwork network)
	{
	    if (m_aProcessedDevices.Contains(source)) return;
	    
	    m_aProcessedDevices.Insert(source);
	    
	    if (!source.CanAccessNetwork()) return;
	    
	    IEntity sourceEntity = source.GetOwner();
	    if (!sourceEntity) return;
	    
	    vector sourcePos = sourceEntity.GetOrigin();
	    
	    // OPTIMIZATION: Only check nearby devices using spatial grid
	    array<AG0_TDLDeviceComponent> nearbyDevices = GetNearbyDevices(sourcePos, network);
	    
	    foreach (AG0_TDLDeviceComponent device : nearbyDevices)
	    {
	        if (device == source || m_aProcessedDevices.Contains(device)) continue;
	        if (!device.CanAccessNetwork()) continue;
	        
	        if (AreDevicesConnected(source, device))
	        {
	            if (!m_aConnectedDevices.Contains(device))
	                m_aConnectedDevices.Insert(device);
	            FindConnectedDevicesInNetwork(device, network);
	        }
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    // RPC notifications
    //------------------------------------------------------------------------------------------------
    protected void NotifyNetworkJoined(AG0_TDLDeviceComponent device, int networkID, map<RplId, ref AG0_TDLNetworkMember> memberData)
    {
        array<RplId> deviceIDs = new array<RplId>();
        foreach (RplId rplId, AG0_TDLNetworkMember member : memberData) 
        { 
            deviceIDs.Insert(rplId); 
        }
        device.OnNetworkJoined(networkID, deviceIDs);
    }
    
    protected void NotifyNetworkMembersUpdated(AG0_TDLNetwork network)
    {
        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            NotifyNetworkJoined(device, network.GetNetworkID(), network.GetDeviceData());
        }
		NotifyNetworkBroadcastingChange(network);
    }
    
    protected void NotifyNetworkLeft(AG0_TDLDeviceComponent device)
    {
		int networkId = device.GetCurrentNetworkID();
        device.OnNetworkLeft();
		
		IEntity player = GetPlayerFromDevice(device);
	    if (!player) return;
	    
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	    if (playerId < 0) return;
	    
	    SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerManager().GetPlayerController(playerId)
	    );
        if (!controller) 
        {
            Print(string.Format("TDL_System: Controller not found for player %1", playerId), LogLevel.DEBUG);
            return;
        }
	    if (controller && networkId > 0)
	    {
	        controller.NotifyClearNetwork(networkId);
	    }
    }

    protected void NotifyNetworkConnectivity(AG0_TDLDeviceComponent device, map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
	{
	    // Build array of connected device IDs for the connectivity update
	    array<RplId> deviceIDs = new array<RplId>();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers) 
	    { 
	        deviceIDs.Insert(rplId); 
	    }
	    
	    // Always notify the network device about connectivity (existing behavior)
	    device.OnNetworkConnectivityUpdated(deviceIDs);
	    
	    // Convert map to array once (used for both paths)
	    array<ref AG0_TDLNetworkMember> membersArray = {};
	    foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
	    {
	        membersArray.Insert(member);
	    }
	    
	    // ========================================================================
	    // Direct device notification for INFORMATION capability devices
	    // This enables shared displays (vehicles, fixed installations) to work
	    // by giving them their own copy of network data that any player can access
	    // ========================================================================
	    if (device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION))
	    {
	        device.SetLocalNetworkMembers(membersArray);
	        Print(string.Format("TDL_SYSTEM: Sent %1 members directly to INFORMATION device %2", 
	            membersArray.Count(), device.GetOwner()), LogLevel.DEBUG);
	    }
	    
	    // ========================================================================
	    // Player controller path for player-held devices
	    // This maintains the efficient aggregation for personal devices
	    // ========================================================================
	    IEntity player = GetPlayerFromDevice(device);
	    if (!player) return;
	    
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	    if (playerId < 0) return;
	    
	   	SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerManager().GetPlayerController(playerId)
	    );
        if (!controller) 
        {
            Print(string.Format("TDL_System: Controller not found for player %1", playerId), LogLevel.DEBUG);
            return;
        }
	    
	    // Get the network ID from the device
	    int networkId = device.GetCurrentNetworkID();
	    if (networkId <= 0) return;
	    
	    controller.NotifyNetworkMembers(networkId, membersArray);
	}
    
    //------------------------------------------------------------------------------------------------
    AG0_TDLNetwork FindNetworkByID(int networkID)
    {
        foreach (AG0_TDLNetwork network : m_aNetworks)
        {
            if (network.GetNetworkID() == networkID)
                return network;
        }
        return null;
    }
	
	//------------------------------------------------------------------------------------------------
	// Video Broadcasting Coordination
	//------------------------------------------------------------------------------------------------
	// Add broadcasting tracking per network
    protected void NotifyNetworkBroadcastingChange(AG0_TDLNetwork network)
    {
        // Collect all broadcasting devices in this network
        array<RplId> broadcastingDevices = {};
        
        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            // Check all devices owned by this player
            IEntity player = GetPlayerFromDevice(device);
            if (!player) continue;
            
            array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(player);
            foreach (AG0_TDLDeviceComponent playerDevice : playerDevices)
            {
                if (playerDevice.IsCameraBroadcasting())
                {
                    RplId broadcastId = playerDevice.GetDeviceRplId();
                    if (broadcastId != RplId.Invalid() && broadcastingDevices.Find(broadcastId) == -1)
                        broadcastingDevices.Insert(broadcastId);
                }
            }
        }
		PrintFormat("TDL_SYSTEM: Broadcasting devices count for network %1 is %2", network.GetNetworkID(), broadcastingDevices.Count(), LogLevel.DEBUG);
        
        // Notify all players in network
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        set<int> notifiedPlayers = new set<int>();
        
        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            IEntity player = GetPlayerFromDevice(device);
            if (!player) continue;
            
            int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
            if (playerId > -1 && !notifiedPlayers.Contains(playerId))
            {
                notifiedPlayers.Insert(playerId);
                
			    SCR_PlayerController controller = SCR_PlayerController.Cast(
			        GetGame().GetPlayerManager().GetPlayerController(playerId)
			    );
	            if (controller)
	            {
	                controller.NotifyBroadcastingSources(broadcastingDevices);
	                Print(string.Format("TDL_VIDEO_SYSTEM: Notified player %1 of %2 broadcasting sources", 
	                    playerId, broadcastingDevices.Count()), LogLevel.DEBUG);
	            }
	            else
	            {
	                Print(string.Format("TDL_VIDEO_SYSTEM: ERROR - No controller found for player %1", 
	                    playerId), LogLevel.ERROR);
	            }
            }
        }
    }
	
	void OnVideoBroadcastChanged(AG0_TDLDeviceComponent device)
	{
	    if (!Replication.IsServer()) return;
	    
	    Print(string.Format("TDL_VIDEO_SYSTEM: OnVideoBroadcastChanged for device %1", device.GetOwner()), LogLevel.DEBUG);
	    
	    IEntity player = GetPlayerFromDevice(device);
	    if (!player)
	    {
	        Print("TDL_VIDEO_SYSTEM: ERROR - No player found for broadcasting device!", LogLevel.DEBUG);
	        return;
	    }
	    
	    // Get the broadcasting device's RplId (or Invalid if stopping)
	    RplId videoSourceRplId = RplId.Invalid();
	    if (device.IsCameraBroadcasting())
	        videoSourceRplId = device.GetDeviceRplId();
	    
	    // Find which network this player is in and update their member entry
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(player);
	        
	        foreach (AG0_TDLDeviceComponent playerDevice : playerDevices)
	        {
	            if (network.GetNetworkDevices().Contains(playerDevice))
	            {
	                // Found the network - update the member data for this player's network device
	                RplId memberRplId = playerDevice.GetDeviceRplId();
	                AG0_TDLNetworkMember memberData = network.GetDeviceData().Get(memberRplId);
	                
	                if (memberData)
	                {
	                    memberData.SetVideoSourceRplId(videoSourceRplId);
	                    Print(string.Format("TDL_VIDEO_SYSTEM: Set VideoSourceRplId=%1 on member %2", 
	                        videoSourceRplId, memberData.GetPlayerName()), LogLevel.DEBUG);
	                }
	                
	                // Notify network of change (existing logic)
	                NotifyNetworkBroadcastingChange(network);
	                
	                // Also trigger member data replication
	                NotifyNetworkMembersUpdated(network);
	                return;
	            }
	        }
	    }
	    
	    Print("TDL_VIDEO_SYSTEM: WARNING - Broadcasting device's player not in any network!", LogLevel.DEBUG);
	}
	
	protected void UpdateVideoStreaming()
	{
	    if (!Replication.IsServer()) return;
	    
	    // Same logic, just collect first then apply atomically
	    foreach (AG0_TDLDeviceComponent networkDevice : m_aRegisteredNetworkDevices)
	    {
	        if (!networkDevice.IsInNetwork()) continue;
	        
	        array<RplId> connectedMemberIds = networkDevice.GetConnectedMembers();
	        array<RplId> reachableBroadcasters = {};
	        
	        foreach (RplId memberId : connectedMemberIds)
	        {
	            AG0_TDLDeviceComponent memberDevice = GetDeviceByRplId(memberId);
	            if (memberDevice)
	            {
	                IEntity memberPlayer = GetPlayerFromDevice(memberDevice);
	                array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(memberPlayer);
	                
	                foreach (AG0_TDLDeviceComponent device : playerDevices)
	                {
	                    if (device.IsCameraBroadcasting() && 
	                        device.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
	                    {
	                        if(reachableBroadcasters.Find(device.GetDeviceRplId()) == -1) {
            					reachableBroadcasters.Insert(device.GetDeviceRplId());
							}
	                    }
	                }
	            }
	        }
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	// Handle callsign changes and propagate to network
	//------------------------------------------------------------------------------------------------
	void OnDeviceCallsignChanged(AG0_TDLDeviceComponent device)
	{
	    if (!Replication.IsServer()) return;
	    
	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId == RplId.Invalid()) return;
	    
	    Print(string.Format("TDL_SYSTEM_CALLSIGN: Processing callsign change for device %1", 
	        device.GetOwner()), LogLevel.DEBUG);
	    
	    // Find the network this device belongs to
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkDevices().Contains(device))
	        {
	            // Update the member data with new display name
	            AG0_TDLNetworkMember memberData = network.GetDeviceData().Get(deviceRplId);
	            if (memberData)
	            {
	                string newDisplayName = device.GetDisplayName();
	                memberData.SetPlayerName(newDisplayName);
	                
	                Print(string.Format("TDL_SYSTEM_CALLSIGN: Updated member data for %1 to '%2'", 
	                    deviceRplId, newDisplayName), LogLevel.DEBUG);
	                
	                // Trigger update to all network members
	                NotifyNetworkMembersUpdated(network);
	            }
	            break;
	        }
	    }
	}
	
	//------------------------------------------------------------------------------------------------
    // MAIN ENTRY POINT: Send a message from a device
    // Called via RPC from player controller
    //------------------------------------------------------------------------------------------------
    static void SendMessage(AG0_TDLSystem system, RplId senderDeviceRplId, string content,
                           ETDLMessageType messageType, RplId recipientRplId = RplId.Invalid())
    {
        if (!Replication.IsServer()) return;
        
        // Validate content
        if (content.IsEmpty())
        {
            Print("TDL_MESSAGE_SYSTEM: Empty message content, ignoring", LogLevel.DEBUG);
            return;
        }
        
        // Find the sender device
        AG0_TDLDeviceComponent senderDevice = system.GetDeviceByRplId(senderDeviceRplId);
        if (!senderDevice)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: Sender device %1 not found", senderDeviceRplId), LogLevel.DEBUG);
            return;
        }
        
        // Find which network the sender is in
        AG0_TDLNetwork network = FindNetworkForDevice(system, senderDevice);
        if (!network)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: Sender device %1 not in any network", senderDeviceRplId), LogLevel.DEBUG);
            return;
        }
        
        string senderCallsign = senderDevice.GetDisplayName();
        
        // Create the message based on type
        int messageId;
        if (messageType == ETDLMessageType.NETWORK_BROADCAST)
        {
            messageId = AddBroadcastToNetwork(network, senderDeviceRplId, senderCallsign, content);
			system.ApiNotifyMessageSent(network, senderCallsign, "broadcast");
        }
        else if (messageType == ETDLMessageType.DIRECT)
        {
            // Get recipient callsign
            string recipientCallsign = "Unknown";
            AG0_TDLNetworkMember recipientMember = network.GetDeviceData().Get(recipientRplId);
            if (recipientMember)
                recipientCallsign = recipientMember.GetPlayerName();
            
            messageId = AddDirectToNetwork(network, senderDeviceRplId, senderCallsign, 
                                          content, recipientRplId, recipientCallsign);
			system.ApiNotifyMessageSent(network, senderCallsign, "direct");
        }
        
        // Immediately propagate to connected devices
        PropagateMessagesInNetwork(system, network);
    }
    
    //------------------------------------------------------------------------------------------------
    // Mark a message as read
    //------------------------------------------------------------------------------------------------
    static void MarkMessageRead(AG0_TDLSystem system, RplId readerDeviceRplId, int messageId)
    {
        if (!Replication.IsServer()) return;
        
        AG0_TDLDeviceComponent readerDevice = system.GetDeviceByRplId(readerDeviceRplId);
        if (!readerDevice) return;
        
        AG0_TDLNetwork network = FindNetworkForDevice(system, readerDevice);
        if (!network) return;
        
        AG0_TDLMessage msg = GetNetworkMessage(network, messageId);
        if (msg)
        {
            msg.MarkReadBy(readerDeviceRplId);
            
            // Notify the sender that their message was read
            NotifySenderOfReadReceipt(system, network, msg, readerDeviceRplId);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Called during connectivity updates to propagate messages to newly reachable devices
    // This is the key to mesh network message behavior
    //------------------------------------------------------------------------------------------------
    static void PropagateMessagesForDevice(AG0_TDLSystem system, AG0_TDLNetwork network,
                                          AG0_TDLDeviceComponent device,
                                          map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
    {
        if (!Replication.IsServer()) return;
        if (!network || !device) return;
        
        RplId deviceRplId = device.GetDeviceRplId();
        if (deviceRplId == RplId.Invalid()) return;
        
        // Build set of connected device RplIds
        set<RplId> connectedRplIds = new set<RplId>();
        foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
        {
            connectedRplIds.Insert(rplId);
        }
        
        // Find messages that can now be delivered to this device
        array<ref AG0_TDLMessage> deliverable = GetDeliverableMessages(network, deviceRplId, connectedRplIds);
        
        if (deliverable.Count() > 0)
        {
            Print(string.Format("TDL_MESSAGE_PROPAGATION: %1 new messages can be delivered to %2",
                deliverable.Count(), device.GetDisplayName()), LogLevel.DEBUG);
            
            // Mark messages as delivered and notify client
            foreach (AG0_TDLMessage msg : deliverable)
            {
                msg.MarkDeliveredTo(deviceRplId);
            }
            
            // Send all relevant messages to client
            SendMessagesToClient(system, network, device);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Propagate messages across entire network (called after new message or connectivity change)
    //------------------------------------------------------------------------------------------------
    static void PropagateMessagesInNetwork(AG0_TDLSystem system, AG0_TDLNetwork network)
    {
        if (!Replication.IsServer()) return;
        if (!network) return;
        
        // Iterate until no more propagation happens
        bool anyDelivered = true;
        int iterations = 0;
        const int MAX_ITERATIONS = 10; // Prevent infinite loops
        
        while (anyDelivered && iterations < MAX_ITERATIONS)
        {
            anyDelivered = false;
            iterations++;
            
            foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
            {
                if (!device.CanAccessNetwork()) continue;
                
                RplId deviceRplId = device.GetDeviceRplId();
                if (deviceRplId == RplId.Invalid()) continue;
                
                // Get this device's current connectivity
                // We need to get the connected members for this specific device
                // This requires accessing the system's connectivity data
                set<RplId> connectedRplIds = GetDeviceConnectedRplIds(system, device, network);
                
                // Check each message
                array<ref AG0_TDLMessage> messages = GetNetworkMessages(network);
                foreach (AG0_TDLMessage msg : messages)
                {
                    if (msg.CanDeliverTo(deviceRplId, connectedRplIds))
                    {
                        msg.MarkDeliveredTo(deviceRplId);
                        anyDelivered = true;
                        
                        Print(string.Format("TDL_MESSAGE_PROPAGATION: Message %1 delivered to %2",
                            msg.GetMessageId(), device.GetDisplayName()), LogLevel.DEBUG);
                    }
                }
            }
        }
        
        // After propagation, notify all devices of their current message state
        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            SendMessagesToClient(system, network, device);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Get a device's connected RplIds by running connectivity check
    //------------------------------------------------------------------------------------------------
    static set<RplId> GetDeviceConnectedRplIds(AG0_TDLSystem system, AG0_TDLDeviceComponent device, 
                                               AG0_TDLNetwork network)
    {
        set<RplId> result = new set<RplId>();
        
        // Always include self
        RplId selfRplId = device.GetDeviceRplId();
        if (selfRplId != RplId.Invalid())
            result.Insert(selfRplId);
        
        // Check direct connectivity to other devices
        foreach (AG0_TDLDeviceComponent otherDevice : network.GetNetworkDevices())
        {
            if (otherDevice == device) continue;
            if (!otherDevice.CanAccessNetwork()) continue;
            
            // Use the system's connectivity check (simplified - direct range check)
            if (system.AreDevicesConnected(device, otherDevice))
            {
                RplId otherRplId = otherDevice.GetDeviceRplId();
                if (otherRplId != RplId.Invalid())
                    result.Insert(otherRplId);
            }
        }
        
        return result;
    }
    
    //------------------------------------------------------------------------------------------------
    // Send current message state to a client
    //------------------------------------------------------------------------------------------------
    static void SendMessagesToClient(AG0_TDLSystem system, AG0_TDLNetwork network, 
                                    AG0_TDLDeviceComponent device)
    {
        if (!device) return;
        
        RplId deviceRplId = device.GetDeviceRplId();
        
        // Get player controller for this device
        IEntity player = system.GetPlayerFromDevice(device);
        if (!player) return;
        
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
        if (playerId < 0) return;
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            playerMgr.GetPlayerController(playerId)
        );
        if (!controller) return;
        
        // Build client message array
        array<ref AG0_TDLMessageClient> clientMessages = BuildClientMessages(network, deviceRplId);
        
        // Send via RPC
        controller.ReceiveTDLMessages(network.GetNetworkID(), clientMessages);
    }
    
    //------------------------------------------------------------------------------------------------
    // Notify sender that their message was read
    //------------------------------------------------------------------------------------------------
    static void NotifySenderOfReadReceipt(AG0_TDLSystem system, AG0_TDLNetwork network,
                                         AG0_TDLMessage msg, RplId readerRplId)
    {
        RplId senderRplId = msg.GetSenderRplId();
        
        // Find sender's device
        AG0_TDLDeviceComponent senderDevice = system.GetDeviceByRplId(senderRplId);
        if (!senderDevice) return;
        
        // Get sender's player controller
        IEntity senderPlayer = system.GetPlayerFromDevice(senderDevice);
        if (!senderPlayer) return;
        
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        int playerId = playerMgr.GetPlayerIdFromControlledEntity(senderPlayer);
        if (playerId < 0) return;
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            playerMgr.GetPlayerController(playerId)
        );
        if (!controller) return;
        
        // Check if sender is currently connected to reader (can receive the receipt)
        set<RplId> senderConnected = GetDeviceConnectedRplIds(system, senderDevice, network);
        if (!senderConnected.Contains(readerRplId))
        {
            // Sender not connected to reader - receipt will be delivered later
            // (The status update will come with the next message sync)
            return;
        }
        
        // Send read receipt update
        controller.ReceiveTDLReadReceipt(network.GetNetworkID(), msg.GetMessageId(), readerRplId);
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Find network containing a device
    //------------------------------------------------------------------------------------------------
    static AG0_TDLNetwork FindNetworkForDevice(AG0_TDLSystem system, AG0_TDLDeviceComponent device)
    {
        array<ref AG0_TDLNetwork> networks = system.GetNetworks();
        foreach (AG0_TDLNetwork network : networks)
        {
            if (network.GetNetworkDevices().Contains(device))
                return network;
        }
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Add broadcast message to network storage
    //------------------------------------------------------------------------------------------------
    static int AddBroadcastToNetwork(AG0_TDLNetwork network, RplId senderRplId, 
                                    string senderCallsign, string content)
    {
        // Access network's message storage (assuming these are added to AG0_TDLNetwork)
        array<ref AG0_TDLMessage> messages = network.GetMessages();
        int nextId = network.GetNextMessageId();
        
        return network.AddBroadcastMessage(
            network, senderRplId, senderCallsign, content, messages, nextId
        );
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Add direct message to network storage
    //------------------------------------------------------------------------------------------------
    static int AddDirectToNetwork(AG0_TDLNetwork network, RplId senderRplId, string senderCallsign,
                                 string content, RplId recipientRplId, string recipientCallsign)
    {
        array<ref AG0_TDLMessage> messages = network.GetMessages();
        int nextId = network.GetNextMessageId();
        
        return network.AddDirectMessage(
            network, senderRplId, senderCallsign, content, recipientRplId, recipientCallsign,
            messages, nextId
        );
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Get message from network
    //------------------------------------------------------------------------------------------------
    static AG0_TDLMessage GetNetworkMessage(AG0_TDLNetwork network, int messageId)
    {
        return network.GetMessageById(network.GetMessages(), messageId);
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Get all network messages
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessage> GetNetworkMessages(AG0_TDLNetwork network)
    {
        return network.GetMessages();
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Get deliverable messages
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessage> GetDeliverableMessages(AG0_TDLNetwork network, RplId targetRplId,
                                                           set<RplId> connectedDevices)
    {
        return network.GetDeliverableMessages(
            network.GetMessages(), targetRplId, connectedDevices
        );
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPER: Build client messages
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessageClient> BuildClientMessages(AG0_TDLNetwork network, RplId viewerRplId)
    {
        return network.BuildClientMessages(network.GetMessages(), viewerRplId);
    }
	
	
	//------------------------------------------------------------------------------------------------
	// API INTEGRATION METHODS
	//------------------------------------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	//! Send server heartbeat to API
	protected void ApiSendHeartbeat()
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "heartbeat");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkCount", m_aNetworks.Count());
	    json.WriteValue("deviceCount", m_aRegisteredNetworkDevices.Count());
	    json.WriteValue("playerCount", GetConnectedPlayerCount());
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Sync full server state to API
	protected void ApiSyncFullState()
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "state_sync");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    
	    // Build networks array
	    array<ref AG0_TDLNetworkState> networkStates = {};
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        AG0_TDLNetworkState netState = new AG0_TDLNetworkState();
	        netState.networkId = network.GetNetworkID();
	        netState.networkName = network.GetNetworkName();
	        netState.deviceCount = network.GetNetworkDevices().Count();
	        netState.messageCount = network.GetMessages().Count();
	        
	        // Build device list for this network
	        array<ref AG0_TDLDeviceState> deviceStates = {};
	        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	        {
	            AG0_TDLDeviceState devState = new AG0_TDLDeviceState();
	            if (device.GetDeviceRplId().IsValid())
				    devState.rplId = 1;
				else
				    devState.rplId = 0;
	            devState.callsign = device.GetDisplayName();
	            devState.capabilities = device.GetActiveCapabilities();
	            devState.isPowered = device.IsPowered();
	            
	            IEntity owner = device.GetOwner();
	            if (owner)
	            {
	                vector pos = owner.GetOrigin();
	                devState.posX = pos[0];
	                devState.posY = pos[1];
	                devState.posZ = pos[2];
	            }
	            
	            deviceStates.Insert(devState);
	        }
	        netState.devices = deviceStates;
	        networkStates.Insert(netState);
	    }
	    
	    json.WriteValue("networks", networkStates);
	    json.WriteValue("totalDevices", m_aRegisteredNetworkDevices.Count());
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Notify API of network creation
	protected void ApiNotifyNetworkCreated(AG0_TDLNetwork network, string creatorName)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "network_created");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkName", network.GetNetworkName());
	    json.WriteValue("creatorName", creatorName);
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Notify API of network deletion
	protected void ApiNotifyNetworkDeleted(int networkId, string networkName)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "network_deleted");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", networkId);
	    json.WriteValue("networkName", networkName);
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Notify API of device joining network
	protected void ApiNotifyDeviceJoined(AG0_TDLNetwork network, AG0_TDLDeviceComponent device)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "device_joined");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkName", network.GetNetworkName());
	    json.WriteValue("deviceCallsign", device.GetDisplayName());
	    json.WriteValue("deviceCapabilities", device.GetActiveCapabilities());
	    
	    // Include player info if available
	    IEntity player = GetPlayerFromDevice(device);
	    if (player)
	    {
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	        if (playerId > 0)
	        {
	            json.WriteValue("playerName", playerMgr.GetPlayerName(playerId));
	            json.WriteValue("playerId", playerId);
	        }
	    }
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Notify API of device leaving network
	protected void ApiNotifyDeviceLeft(int networkId, string networkName, string deviceCallsign)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "device_left");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", networkId);
	    json.WriteValue("networkName", networkName);
	    json.WriteValue("deviceCallsign", deviceCallsign);
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Notify API of TDL message sent
	protected void ApiNotifyMessageSent(AG0_TDLNetwork network, string senderCallsign, 
	                                     string messageType, string recipientCallsign = "")
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "message_sent");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkName", network.GetNetworkName());
	    json.WriteValue("senderCallsign", senderCallsign);
	    json.WriteValue("messageType", messageType);
	    
	    if (!recipientCallsign.IsEmpty())
	        json.WriteValue("recipientCallsign", recipientCallsign);
	    
	    // Note: We intentionally do NOT send message content for privacy
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Helper: Get connected player count
	protected int GetConnectedPlayerCount()
	{
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return 0;
	    
	    array<int> playerIds = {};
	    playerMgr.GetPlayers(playerIds);
	    return playerIds.Count();
	}
	
	//------------------------------------------------------------------------------------------------
	//! Public getter for API manager (for external systems)
	AG0_TDLApiManager GetApiManager()
	{
	    return m_ApiManager;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Check if API is connected and ready
	bool IsApiConnected()
	{
	    return m_ApiManager && m_ApiManager.CanCommunicate();
	}
	
	
	void LogNetworkState(string context = "")
	{
	    Print(string.Format("TDL_SYSTEM_DEBUG [%1]: Network State", context), LogLevel.DEBUG);
	    Print(string.Format("  Total Networks: %1", m_aNetworks.Count()), LogLevel.DEBUG);
	    Print(string.Format("  Registered Devices: %1", m_aRegisteredNetworkDevices.Count()), LogLevel.DEBUG);
	    Print(string.Format("  Server Mode: %1", Replication.IsServer()), LogLevel.DEBUG);
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        Print(string.Format("  Network: %1 (ID: %2)", network.GetNetworkName(), network.GetNetworkID()), LogLevel.DEBUG);
	        Print(string.Format("    Password: %1", network.GetNetworkPassword()), LogLevel.DEBUG);
	        Print(string.Format("    Devices: %1", network.GetNetworkDevices().Count()), LogLevel.DEBUG);
	        
	        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	        {
	            string deviceName = "UNKNOWN";
	            if (device.GetOwner())
	                deviceName = device.GetOwner().ToString();
	            
	            Print(string.Format("      Device: %1 (Player: %2)", deviceName, device.GetOwnerPlayerName()), LogLevel.DEBUG);
	        }
	        
	        Print(string.Format("    Device Data Entries: %1", network.GetDeviceData().Count()), LogLevel.DEBUG);
	        foreach (RplId rplId, AG0_TDLNetworkMember member : network.GetDeviceData())
	        {
	            Print(string.Format("      Data: %1 -> %2 (IP: %3, Caps: %4)", 
	                rplId, member.GetPlayerName(), member.GetNetworkIP(), member.GetCapabilities()), LogLevel.DEBUG);
	        }
	    }
	}
	
	void LogDeviceRegistration(AG0_TDLDeviceComponent device, bool isRegistering)
	{
	    string ownerName = "UNKNOWN";
	    if (device.GetOwner())
	        ownerName = device.GetOwner().ToString();
	    
	    string action = "UNREGISTERED";
	    if (isRegistering)
	        action = "REGISTERED";
	    
	    Print(string.Format("TDL_DEVICE_%1: %2 (Player: %3)", action, ownerName, device.GetOwnerPlayerName()), LogLevel.DEBUG);
	    Print(string.Format("  RplId: %1", device.GetDeviceRplId()), LogLevel.DEBUG);
	    Print(string.Format("  Capabilities: %1", device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS)), LogLevel.DEBUG);
	    Print(string.Format("  Network Range: %1m", device.GetEffectiveNetworkRange()), LogLevel.DEBUG);
	    Print(string.Format("  Total Registered Devices: %1", m_aRegisteredNetworkDevices.Count()), LogLevel.DEBUG);
	}
	
	// Debug logging for connectivity checks
	void LogConnectivityCheck(AG0_TDLDeviceComponent deviceA, AG0_TDLDeviceComponent deviceB, bool connected, float distance, float maxRange)
	{
	    string nameA = "UNKNOWN_A";
	    string nameB = "UNKNOWN_B";
	    
	    if (deviceA.GetOwner())
	        nameA = deviceA.GetOwner().ToString();
	    if (deviceB.GetOwner())
	        nameB = deviceB.GetOwner().ToString();
	    
	    Print(string.Format("TDL_CONNECTIVITY_CHECK: %1 <-> %2", nameA, nameB), LogLevel.DEBUG);
	    Print(string.Format("  Distance: %1, Max Range: %2, Connected: %3", distance, maxRange, connected), LogLevel.DEBUG);
	    Print(string.Format("  Device A Range: %1, Device B Range: %2", 
	        deviceA.GetEffectiveNetworkRange(), deviceB.GetEffectiveNetworkRange()), LogLevel.DEBUG);
	}
	
	// Debug logging for player capabilities
	void LogPlayerCapabilities(IEntity player, int aggregatedCaps)
	{
	    string playerName = "UNKNOWN_PLAYER";
	    
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (playerMgr)
	    {
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	        if (playerId != 0)
	            playerName = playerMgr.GetPlayerName(playerId);
	    }
	    
	    Print(string.Format("TDL_PLAYER_CAPABILITIES: %1", playerName), LogLevel.DEBUG);
	    Print(string.Format("  Aggregated Capabilities: %1", aggregatedCaps), LogLevel.DEBUG);
	    Print(string.Format("    - NETWORK_ACCESS: %1", (aggregatedCaps & AG0_ETDLDeviceCapability.NETWORK_ACCESS) != 0), LogLevel.DEBUG);
	    Print(string.Format("    - GPS_PROVIDER: %1", (aggregatedCaps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0), LogLevel.DEBUG);
	    Print(string.Format("    - DISPLAY_OUTPUT: %1", (aggregatedCaps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0), LogLevel.DEBUG);
	    Print(string.Format("    - VIDEO_SOURCE: %1", (aggregatedCaps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0), LogLevel.DEBUG);
	    Print(string.Format("    - POWER_PROVIDER: %1", (aggregatedCaps & AG0_ETDLDeviceCapability.POWER_PROVIDER) != 0), LogLevel.DEBUG);
	    
	    array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(player);
	    Print(string.Format("  Player TDL Devices: %1", playerDevices.Count()), LogLevel.DEBUG);
	    
	    foreach (AG0_TDLDeviceComponent device : playerDevices)
	    {
	        string deviceName = "UNKNOWN_DEVICE";
	        if (device.GetOwner())
	            deviceName = device.GetOwner().ToString();
	        
	        Print(string.Format("    Device: %1 (Caps: %2, Powered: %3, In Network: %4)", 
	            deviceName, device.GetActiveCapabilities(), device.IsPowered(), device.IsInNetwork()), LogLevel.DEBUG);
	    }
	}
}