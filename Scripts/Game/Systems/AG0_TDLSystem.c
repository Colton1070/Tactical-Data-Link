// AG0_TDLSystem.c - Device-Centric TDL Network Management

//------------------------------------------------------------------------------------------------
// Bridge link between two networks with incompatible waveforms.
// Registered each update cycle by UpdateBridgeLinks() and consumed by
// AppendBridgedMembers() during connectivity distribution.
//------------------------------------------------------------------------------------------------
class AG0_TDLBridgeLink
{
    int m_iNetworkA;
    int m_iNetworkB;
    RplId m_BridgeDeviceRplId;

    void AG0_TDLBridgeLink(int netA, int netB, RplId bridgeDevice)
    {
        m_iNetworkA = netA;
        m_iNetworkB = netB;
        m_BridgeDeviceRplId = bridgeDevice;
    }

    bool InvolvesNetwork(int networkId)
    {
        return m_iNetworkA == networkId || m_iNetworkB == networkId;
    }

    int GetOtherNetwork(int networkId)
    {
        if (networkId == m_iNetworkA) return m_iNetworkB;
        if (networkId == m_iNetworkB) return m_iNetworkA;
        return -1;
    }
}

class AG0_TDLNetwork
{
    protected int m_iNetworkID;
    protected string m_sNetworkName;
    protected string m_sNetworkPassword;
    protected int m_eWaveform;
    protected ref array<AG0_TDLDeviceComponent> m_aNetworkDevices = {};
    protected ref map<RplId, ref AG0_TDLNetworkMember> m_mDeviceData = new map<RplId, ref AG0_TDLNetworkMember>();
    protected int m_iNextNetworkIP = 1;
	
	
	// Message storage
    protected ref array<ref AG0_TDLMessage> m_aMessages = {};
    protected int m_iNextMessageId = 1;
    
    // Message retention settings
    protected const int MAX_MESSAGES = 100;
    protected const int MESSAGE_EXPIRY_SECONDS = 3600;
	
    
    void AG0_TDLNetwork(int networkID, string name, string password, int waveform = AG0_ETDLWaveform.LEGACY)
    {
        m_iNetworkID = networkID;
        m_sNetworkName = name;
        m_sNetworkPassword = password;
        m_eWaveform = waveform;
    }
    
    int GetNetworkID() { return m_iNetworkID; }
    string GetNetworkName() { return m_sNetworkName; }
    string GetNetworkPassword() { return m_sNetworkPassword; }
    int GetWaveform() { return m_eWaveform; }
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
	// Shutdown guard to prevent access during cleanup
    protected static bool s_bShuttingDown = false;
	
	protected ref AG0_TDLApiManager m_ApiManager;
	// API sync intervals (in seconds)
	protected const float API_HEARTBEAT_INTERVAL = 60.0;
	protected float m_fApiStateSyncInterval = 5.0;
	protected float m_fTimeSinceApiHeartbeat = 0;
	protected float m_fTimeSinceApiStateSync = 0;
	protected const float API_SHAPES_POLL_INTERVAL = 5.0;
    protected float m_fTimeSinceShapesPoll = 0;

	// Lazy-registered handler for SCR_BaseGameMode.GetOnPlayerAuditSuccess.
	// Used to deliver the terrain structures dataset to every player on session join,
	// independent of TDL network membership — so the data is always there/available.
	protected bool m_bPlayerAuditHandlerRegistered = false;
	


    // Networks storage
    protected ref array<ref AG0_TDLNetwork> m_aNetworks = {};
    protected int m_iNextNetworkID = 1;
    
    // Active bridge links — rebuilt every UpdateNetworks() cycle
    protected ref array<ref AG0_TDLBridgeLink> m_aBridgeLinks = {};
    
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
    
    protected float m_fGridCellSize = 2000.0;
    protected ref map<string, ref array<AG0_TDLDeviceComponent>> m_mSpatialGrid = new map<string, ref array<AG0_TDLDeviceComponent>>();
    protected float m_fTimeSinceGridRebuild = 999.0;
    protected float m_fGridRebuildInterval = 5.0;
	protected float m_fMaxDeviceRange = 1000.0;
	protected bool m_bCellSizeNeedsUpdate = false;

	
    //------------------------------------------------------------------------------------------------
    override static void InitInfo(WorldSystemInfo outInfo)
	{
		super.InitInfo(outInfo);
	    
	    Print("TDL_SYSTEM_INIT: InitInfo called", LogLevel.DEBUG);
	    outInfo
	        .SetAbstract(false)
	        .SetLocation(WorldSystemLocation.Server)
	        .AddPoint(WorldSystemPoint.Frame);
	        
	    Print("AG0_TDLSystem: Device-centric system initialized", LogLevel.DEBUG);
	}
    
    //--------------------------------------------------------------------------
    // Static instance getter for easy access from controller
    //--------------------------------------------------------------------------
    
    static AG0_TDLSystem GetInstance()
	{
	    if (s_bShuttingDown)
	        return null;
	    
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
	//! Get persistent player identity UUID from session player ID
	string GetPlayerIdentityId(int playerId)
	{
	    if (playerId < 0)
	        return "";
	    
	    return SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get player platform kind (Steam, Xbox, PlayStation)
	PlatformKind GetPlayerPlatform(int playerId)
	{
	    if (playerId <= 0)
	        return PlatformKind.NONE;
	    
	    BackendApi api = GetGame().GetBackendApi();
	    if (!api)
	        return PlatformKind.NONE;
	    
	    return api.GetPlayerPlatformKind(playerId);
	}

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
	        
	        owner = owner.GetParent();
	    }
	    return null;
	}
    
	override protected void OnInit()
	{
	    super.OnInit();
	    
		s_bShuttingDown = false;
		
	    if (!Replication.IsServer())
	    {
	        Print("TDL_SYSTEM: Running on client/proxy - skipping API initialization", LogLevel.DEBUG);
	        return;
	    }
	    
	    m_ApiManager = new AG0_TDLApiManager();
	    if (m_ApiManager.Initialize())
	    {
			m_fApiStateSyncInterval = m_ApiManager.GetStateSyncInterval();
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

        // The game mode is not necessarily live during our OnInit (system-init
        // ordering is not guaranteed). Register lazily on the first tick where
        // it's available. Cheap once-only check after the flag flips.
        if (!m_bPlayerAuditHandlerRegistered)
            EnsurePlayerAuditHandlerRegistered();

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
	        
	        m_fTimeSinceApiHeartbeat += timeSlice;
	        if (m_fTimeSinceApiHeartbeat >= API_HEARTBEAT_INTERVAL)
	        {
	            ApiSendHeartbeat();
	            m_fTimeSinceApiHeartbeat = 0;
	        }
	        
	        m_fTimeSinceApiStateSync += timeSlice;
	        if (m_fTimeSinceApiStateSync >= m_fApiStateSyncInterval)
	        {
	            ApiSyncFullState();
	            m_fTimeSinceApiStateSync = 0;
	        }
			
			m_fTimeSinceShapesPoll += timeSlice;
			if (m_fTimeSinceShapesPoll >= API_SHAPES_POLL_INTERVAL)
			{
				m_ApiManager.PollShapes();
				m_fTimeSinceShapesPoll = 0;
			}
	    }
    }
	
	//------------------------------------------------------------------------------------------------
	void ~AG0_TDLSystem()
	{
	    s_bShuttingDown = true;
	    
	    if (m_ApiManager)
	        m_ApiManager = null;
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
    }
	
	protected void UpdateMaxDeviceRange()
	{
	    float previousMaxRange = m_fMaxDeviceRange;
	    float currentMaxRange = 0.0;
	    
	    foreach (AG0_TDLDeviceComponent device : m_aRegisteredNetworkDevices)
	    {
	        float deviceRange = device.GetEffectiveNetworkRange();
	        if (deviceRange > currentMaxRange)
	            currentMaxRange = deviceRange;
	    }
	    
	    float rangeDifference = Math.AbsFloat(currentMaxRange - previousMaxRange);
	    if (rangeDifference > 100.0 || rangeDifference > (previousMaxRange * 0.1))
	    {
	        m_fMaxDeviceRange = currentMaxRange;
	        m_fGridCellSize = 2.0 * m_fMaxDeviceRange;
	        m_fTimeSinceGridRebuild = 999.0;
	    }
	}
    
    array<AG0_TDLDeviceComponent> GetNearbyDevices(vector pos, AG0_TDLNetwork network)
    {
        array<AG0_TDLDeviceComponent> nearby = {};
        array<AG0_TDLDeviceComponent> networkDevices = network.GetNetworkDevices();
        
        int cx = Math.Floor(pos[0] / m_fGridCellSize);
        int cy = Math.Floor(pos[1] / m_fGridCellSize);
        int cz = Math.Floor(pos[2] / m_fGridCellSize);
        
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
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkDevices().Contains(device))
	        {
	            Print(string.Format("TDL_NETWORK_CLEANUP: Removing device from network %1", network.GetNetworkName()), LogLevel.DEBUG);
	            network.RemoveDevice(device);
	        }
	    }
	    
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
	    
	    // Check for existing network with same credentials AND compatible waveform.
	    // Same name+password on a different waveform is a distinct network — allow creation.
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkName() == networkName && network.GetNetworkPassword() == password
	            && (creator.GetWaveform() & network.GetWaveform()) != 0)
	        {
	            Print(string.Format("TDL_NETWORK_CREATE: Network '%1' already exists with compatible waveform, joining instead", networkName), LogLevel.DEBUG);
	            JoinNetwork(creator, networkName, password);
	            return network.GetNetworkID();
	        }
	    }
	    
	    // Create new network — inherits waveform from the creating device
	    AG0_ETDLWaveform creatorWaveform = creator.GetWaveform();
	    AG0_TDLNetwork newNetwork = new AG0_TDLNetwork(m_iNextNetworkID++, networkName, password, creatorWaveform);
	    newNetwork.AddDevice(creator, deviceRplId, creator.GetDisplayName(), position);
	    m_aNetworks.Insert(newNetwork);
	    
	    Print(string.Format("TDL_NETWORK_CREATE: Successfully created network '%1' (ID: %2, Waveform: %3)", 
	        networkName, newNetwork.GetNetworkID(), creatorWaveform), LogLevel.DEBUG);
	    
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
	    
	    // Check if in range of any existing network device — waveform must be compatible
	    foreach (AG0_TDLNetwork network : matchingNetworks)
	    {
	        // Waveform gate: joining device must share at least one waveform bit with the network
	        if ((device.GetWaveform() & network.GetWaveform()) == 0)
	        {
	            Print(string.Format("TDL_NETWORK_JOIN: Device waveform %1 incompatible with network waveform %2, skipping",
	                device.GetWaveform(), network.GetWaveform()), LogLevel.DEBUG);
	            continue;
	        }
	        
	        if (IsDeviceInNetworkRange(device, network))
	        {
	            Print(string.Format("TDL_NETWORK_JOIN: Device in range of network '%1', joining", network.GetNetworkName()), LogLevel.DEBUG);
	            network.AddDevice(device, deviceRplId, device.GetDisplayName(), position);
	            NotifyNetworkMembersUpdated(network);
				ApiNotifyDeviceJoined(network, device);
	            return true;
	        }
	    }
	    
	    Print(string.Format("TDL_NETWORK_JOIN: Device not in range of any compatible matching networks"), LogLevel.DEBUG);
	    return false;
	}
    
    void LeaveNetwork(AG0_TDLDeviceComponent device)
    {
        if (!Replication.IsServer()) return;

        foreach (AG0_TDLNetwork network : m_aNetworks)
        {
            if (network.GetNetworkDevices().Contains(device))
            {
                // Capture the id from the network — never from device.GetCurrentNetworkID(),
                // which may have been pre-cleared to -1 by the user action's server-side run.
                int leftNetworkId = network.GetNetworkID();
                string leftNetworkName = network.GetNetworkName();

                network.RemoveDevice(device);
                NotifyNetworkLeft(device, leftNetworkId);
				ApiNotifyDeviceLeft(leftNetworkId, leftNetworkName, device.GetDisplayName());

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
	    
	    // Waveform gate: devices must share at least one waveform bit to link at the RF layer
	    if ((deviceA.GetWaveform() & deviceB.GetWaveform()) == 0)
	        return false;
	    
	    vector posA = deviceA.GetOwner().GetOrigin();
	    vector posB = deviceB.GetOwner().GetOrigin();
	    
	    float rangeA = deviceA.GetEffectiveNetworkRange();
	    float rangeB = deviceB.GetEffectiveNetworkRange();
	    float maxPossibleRange = Math.Max(rangeA, rangeB);
	    
	    // OPTIMIZATION: Early rejection using axis-aligned bounding box (AABB)
	    if (Math.AbsFloat(posA[0] - posB[0]) > maxPossibleRange) return false;
	    if (Math.AbsFloat(posA[1] - posB[1]) > maxPossibleRange) return false;
	    if (Math.AbsFloat(posA[2] - posB[2]) > maxPossibleRange) return false;
	    
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
	    
	    UpdateMaxDeviceRange();
	    
	    m_fTimeSinceGridRebuild += GetWorld().GetFixedTimeSlice();
	    if (m_fTimeSinceGridRebuild >= m_fGridRebuildInterval)
	    {
	        RebuildSpatialGrid();
	        m_fTimeSinceGridRebuild = 0;
	    }
	    
	    CheckNetworkMerges();
	    UpdateBridgeLinks();
	    
	    // Track which networks each player is actually in this cycle
	    map<int, ref set<int>> playerActiveNetworks = new map<int, ref set<int>>();
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        UpdateNetworkConnectivity(network);
	        
	        // Record which players have devices in this network
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	        {
	            IEntity player = GetPlayerFromDevice(device);
	            if (!player) continue;
	            
	            int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	            if (playerId < 0) continue;
	            
	            if (!playerActiveNetworks.Contains(playerId))
	                playerActiveNetworks.Set(playerId, new set<int>());
	            
	            playerActiveNetworks.Get(playerId).Insert(network.GetNetworkID());
	        }
	    }
	    
	    // Clear stale network caches from controllers
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    array<int> allPlayers = {};
	    playerMgr.GetPlayers(allPlayers);
	    
	    foreach (int playerId : allPlayers)
	    {
	        SCR_PlayerController controller = SCR_PlayerController.Cast(
	            playerMgr.GetPlayerController(playerId)
	        );
	        if (!controller) continue;
	        
	        array<int> activeNetsArray = {};
			if (playerActiveNetworks.Contains(playerId))
			{
			    foreach (int netId : playerActiveNetworks.Get(playerId))
			        activeNetsArray.Insert(netId);
			}
			
			controller.ClearStaleNetworks(activeNetsArray);
	    }
	    
	    UpdateVideoStreaming();
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
                
                // Only merge networks that share at least one waveform bit.
                // Incompatible-waveform networks with matching credentials are bridged,
                // not merged — bridging is handled separately by UpdateBridgeLinks().
                if ((networkA.GetWaveform() & networkB.GetWaveform()) == 0)
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
    
    //------------------------------------------------------------------------------------------------
    // Scan all player entities for bridge conditions and register AG0_TDLBridgeLink instances.
    // A bridge exists when one player entity has devices in 2+ distinct networks AND at least one
    // of their held devices carries the BRIDGE capability.
    // Bridge links are transient — cleared and rebuilt every update cycle.
    //------------------------------------------------------------------------------------------------
    protected void UpdateBridgeLinks()
    {
        if (!Replication.IsServer()) return;
        
        m_aBridgeLinks.Clear();
        
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        if (!playerMgr) return;
        
        array<int> playerIds = {};
        playerMgr.GetPlayers(playerIds);
        
        foreach (int playerId : playerIds)
        {
            IEntity player = playerMgr.GetPlayerControlledEntity(playerId);
            if (!player) continue;
            
            array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(player);
            
            // Collect distinct network IDs this player's powered devices are members of,
            // and check for BRIDGE capability across all devices (powered or not).
            array<int> playerNetworkIds = {};
            bool hasBridgeCapability = false;
            RplId bridgeDeviceRplId = RplId.Invalid();
            
            foreach (AG0_TDLDeviceComponent device : playerDevices)
            {
                // BRIDGE capability check doesn't require the device to be powered —
                // a dedicated bridge box might have no RF of its own.
                if (device.HasCapability(AG0_ETDLDeviceCapability.BRIDGE))
                {
                    hasBridgeCapability = true;
                    bridgeDeviceRplId = device.GetDeviceRplId();
                }
                
                if (!device.IsPowered()) continue;
                
                int netId = device.GetCurrentNetworkID();
                if (netId > 0 && playerNetworkIds.Find(netId) == -1)
                    playerNetworkIds.Insert(netId);
            }
            
            // Need at least 2 distinct networks and BRIDGE capability to form a link
            if (!hasBridgeCapability || playerNetworkIds.Count() < 2) continue;
            
            // Register a bridge link for every pair of networks this player bridges
            for (int i = 0; i < playerNetworkIds.Count() - 1; i++)
            {
                for (int j = i + 1; j < playerNetworkIds.Count(); j++)
                {
                    int netA = playerNetworkIds[i];
                    int netB = playerNetworkIds[j];
                    
                    // Deduplicate — don't register the same pair twice
                    bool alreadyLinked = false;
                    foreach (AG0_TDLBridgeLink existingLink : m_aBridgeLinks)
                    {
                        if ((existingLink.m_iNetworkA == netA && existingLink.m_iNetworkB == netB) ||
                            (existingLink.m_iNetworkA == netB && existingLink.m_iNetworkB == netA))
                        {
                            alreadyLinked = true;
                            break;
                        }
                    }
                    
                    if (!alreadyLinked)
                    {
                        m_aBridgeLinks.Insert(new AG0_TDLBridgeLink(netA, netB, bridgeDeviceRplId));
                        Print(string.Format("TDL_BRIDGE: Active link Network %1 <-> Network %2 via player %3",
                            netA, netB, playerMgr.GetPlayerName(playerId)), LogLevel.DEBUG);
                    }
                }
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Append members from bridged networks into a device's connectedMembers map.
    // Called from UpdateNetworkConnectivity before NotifyNetworkConnectivity so that
    // bridged SA flows through the existing notification path transparently.
    //------------------------------------------------------------------------------------------------
    protected void AppendBridgedMembers(AG0_TDLNetwork network, AG0_TDLDeviceComponent device,
                                        inout map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
    {
        int myNetworkId = network.GetNetworkID();
        
        foreach (AG0_TDLBridgeLink link : m_aBridgeLinks)
        {
            if (!link.InvolvesNetwork(myNetworkId)) continue;
            
            int foreignNetworkId = link.GetOtherNetwork(myNetworkId);
            AG0_TDLNetwork foreignNetwork = FindNetworkByID(foreignNetworkId);
            if (!foreignNetwork) continue;
            
            foreach (AG0_TDLDeviceComponent foreignDevice : foreignNetwork.GetNetworkDevices())
            {
                RplId foreignRplId = foreignDevice.GetDeviceRplId();
                if (foreignRplId == RplId.Invalid()) continue;
                
                // Don't duplicate a member already visible on this network
                if (connectedMembers.Contains(foreignRplId)) continue;
                
                AG0_TDLNetworkMember foreignMemberData = foreignNetwork.GetDeviceData().Get(foreignRplId);
                if (!foreignMemberData) continue;
                
                IEntity foreignEntity = foreignDevice.GetOwner();
                if (!foreignEntity) continue;
                
                // Build a bridged member entry using live position
                AG0_TDLNetworkMember bridgedData = new AG0_TDLNetworkMember();
                bridgedData.SetRplId(foreignMemberData.GetRplId());
                bridgedData.SetPlayerName(foreignDevice.GetDisplayName());
                
                vector foreignPos = foreignEntity.GetOrigin();
                foreignNetwork.UpdateDevicePosition(foreignRplId, foreignPos);
                bridgedData.SetPosition(foreignPos);
                
                bridgedData.SetNetworkIP(foreignMemberData.GetNetworkIP());
                
                // Aggregate capabilities from the foreign player's all devices
                IEntity foreignPlayer = GetPlayerFromDevice(foreignDevice);
                int aggregatedCaps = 0;
                int foreignOwnerPlayerId = -1;
                if (foreignPlayer)
                {
                    PlayerManager playerMgr = GetGame().GetPlayerManager();
                    foreignOwnerPlayerId = playerMgr.GetPlayerIdFromControlledEntity(foreignPlayer);
                    
                    array<AG0_TDLDeviceComponent> foreignPlayerDevices = GetPlayerAllTDLDevices(foreignPlayer);
                    foreach (AG0_TDLDeviceComponent dev : foreignPlayerDevices)
                    {
                        if (dev.IsCameraBroadcasting() && dev.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
                            bridgedData.SetVideoSourceRplId(dev.GetDeviceRplId());
                        if (dev.IsPowered())
                            aggregatedCaps |= dev.GetActiveCapabilities();
                    }
                }
                else
                {
                    aggregatedCaps = foreignMemberData.GetCapabilities();
                }
                bridgedData.SetCapabilities(aggregatedCaps);
                bridgedData.SetOwnerPlayerId(foreignOwnerPlayerId);
                
                // Signal not meaningful across a bridge — use 100 to indicate active bridge link
                bridgedData.SetSignalStrength(100.0);
                
                // Tag as bridged so UI can visually distinguish foreign-network members
                bridgedData.SetIsBridged(true);
                bridgedData.SetSourceNetworkId(foreignNetworkId);
                
                connectedMembers.Set(foreignRplId, bridgedData);
            }
        }
    }
    
    protected void UpdateNetworkConnectivity(AG0_TDLNetwork network)
	{
	    if (!network || !network.HasDevices()) return;
	    
	    foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	    {
	        if (!device.CanAccessNetwork()) continue;
	        
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
	        
	        // Append SA from any networks bridged to this one
	        AppendBridgedMembers(network, device, connectedMembers);

	        // Pass network.GetNetworkID() explicitly — see NotifyNetworkConnectivity doc.
	        NotifyNetworkConnectivity(device, network.GetNetworkID(), connectedMembers);
			PropagateMessagesForDevice(this, network, device, connectedMembers);
	    }
		// After all devices processed, derive player connectivity
	    map<int, ref set<int>> playerConnections = new map<int, ref set<int>>();
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    
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
	    
	    foreach (int playerID, set<int> connections : playerConnections)
		{
		    array<int> connArray = {};
		    foreach (int id : connections)
		        connArray.Insert(id);
		    
		    SCR_PlayerController controller = SCR_PlayerController.Cast(
		        GetGame().GetPlayerManager().GetPlayerController(playerID)
		    );
		    
		    if (!controller) 
		    {
		        Print(string.Format("TDL_System: Controller not found for player %1", playerID), LogLevel.DEBUG);
		        continue;
		    }
		    
		    controller.NotifyConnectedPlayers(connArray);
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
	//! Push aggregated shape data to a single player across all their network memberships.
	protected void PushPlayerShapes(SCR_PlayerController controller, int playerId)
	{
		if (!m_ApiManager || !controller) return;
		
		AG0_TDLMapShapeManager shapeMgr = m_ApiManager.GetShapeManager();
		if (!shapeMgr) return;
		
		PlayerManager playerMgr = GetGame().GetPlayerManager();
		if (!playerMgr) return;
		
		IEntity playerEntity = playerMgr.GetPlayerControlledEntity(playerId);
		if (!playerEntity)
		{
			controller.ReceiveTDLShapes("", "");
			return;
		}
		
		set<int> playerNetworkIds = new set<int>();
		array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(playerEntity);
		
		foreach (AG0_TDLDeviceComponent device : playerDevices)
		{
			foreach (AG0_TDLNetwork network : m_aNetworks)
			{
				if (network.GetNetworkDevices().Contains(device))
				{
					playerNetworkIds.Insert(network.GetNetworkID());
					break;
				}
			}
		}
		
		string packedShapes = shapeMgr.GetPackedShapeDataForNetworks(playerNetworkIds);
		string syncHash = shapeMgr.GetLastSyncHash();
		
		controller.ReceiveTDLShapes(packedShapes, syncHash);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Distribute current shape data to all networked players.
	void DistributeShapesToClients()
	{
		if (!Replication.IsServer()) return;

		PlayerManager playerMgr = GetGame().GetPlayerManager();
		if (!playerMgr) return;

		set<int> pushedPlayers = new set<int>();

		foreach (AG0_TDLNetwork network : m_aNetworks)
		{
			foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
			{
				IEntity player = GetPlayerFromDevice(device);
				if (!player) continue;

				int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
				if (playerId < 0 || pushedPlayers.Contains(playerId))
					continue;
				pushedPlayers.Insert(playerId);

				SCR_PlayerController controller = SCR_PlayerController.Cast(
					playerMgr.GetPlayerController(playerId)
				);
				if (controller)
					PushPlayerShapes(controller, playerId);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Per-chunk wire-data budget for terrain structure delivery.
	//! Reforger Reliable RPCs forced into fragmented mode above ~14 KB per packet,
	//! which Colton wants to avoid — keep individual chunks well below that with
	//! headroom for the syncHash / index / totalChunks params and RPC envelope.
	protected static const int TERRAIN_STRUCTURES_CHUNK_BYTES = 12000;

	//------------------------------------------------------------------------------------------------
	//! Push the current terrain structures dataset to a single player as a sequence
	//! of <14 KB Reliable RPCs. Clients buffer keyed on syncHash and parse only after
	//! all `totalChunks` arrive — see RpcDo_ReceiveTDLTerrainStructuresChunk.
	//!
	//! Empty payload + any (including empty) hash is a valid "clear local state"
	//! signal, sent as one zero-length chunk so the client's reassembly bookkeeping
	//! stays consistent.
	protected void PushPlayerTerrainStructures(SCR_PlayerController controller, int playerId)
	{
		if (!m_ApiManager || !controller) return;

		AG0_TDLTerrainStructureManager mgr = m_ApiManager.GetTerrainStructureManager();
		if (!mgr)
		{
			// No manager → tell client we have nothing.
			controller.ReceiveTDLTerrainStructuresChunk(string.Empty, 1, 0, string.Empty);
			return;
		}

		string raw = mgr.GetLastRawJson();
		string hash = mgr.GetLastSyncHash();

		int totalLen = raw.Length();
		if (totalLen == 0)
		{
			// Empty dataset — single empty chunk so the client transitions cleanly.
			controller.ReceiveTDLTerrainStructuresChunk(hash, 1, 0, string.Empty);
			return;
		}

		int chunkBytes = TERRAIN_STRUCTURES_CHUNK_BYTES;
		int totalChunks = (totalLen + chunkBytes - 1) / chunkBytes;

		for (int i = 0; i < totalChunks; i = i + 1)
		{
			int start = i * chunkBytes;
			int len = Math.Min(chunkBytes, totalLen - start);
			string chunk = raw.Substring(start, len);
			controller.ReceiveTDLTerrainStructuresChunk(hash, totalChunks, i, chunk);
		}

		Print(string.Format("[TDL_STRUCTURES] Sent %1 chunks (%2 bytes) to player %3, hash=%4",
			totalChunks, totalLen, playerId, hash), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Distribute the current terrain structures dataset to all connected players.
	//! Unlike shapes, structures are global to the world — not network-scoped — so
	//! we iterate every player the PlayerManager knows about. The client RPC handler
	//! short-circuits when its local hash already matches, so unchanged repeats are cheap.
	void DistributeTerrainStructuresToClients()
	{
		if (!Replication.IsServer()) return;

		PlayerManager playerMgr = GetGame().GetPlayerManager();
		if (!playerMgr) return;

		array<int> playerIds = {};
		playerMgr.GetPlayers(playerIds);

		foreach (int playerId : playerIds)
		{
			if (playerId <= 0) continue;

			SCR_PlayerController controller = SCR_PlayerController.Cast(
				playerMgr.GetPlayerController(playerId)
			);
			if (!controller) continue;

			PushPlayerTerrainStructures(controller, playerId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Register the OnPlayerAuditSuccess handler with SCR_BaseGameMode if not yet.
	//! Idempotent — safe to call every tick.
	protected void EnsurePlayerAuditHandlerRegistered()
	{
		if (m_bPlayerAuditHandlerRegistered)
			return;

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gameMode)
			return;

		ScriptInvokerBase<SCR_BaseGameMode_PlayerId> invoker = gameMode.GetOnPlayerAuditSuccess();
		if (!invoker)
			return;

		invoker.Insert(OnPlayerAuditSuccessHandler);
		m_bPlayerAuditHandlerRegistered = true;
		Print("[TDL_STRUCTURES] Registered OnPlayerAuditSuccess handler", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Fires once per session-joining player AFTER the audit succeeds — by which
	//! point the player's controller is RPC-addressable. Push the current terrain
	//! structures dataset so the player has it cached locally before they ever
	//! open the TDL map. Independent of TDL network membership.
	//!
	//! If the API hasn't completed its initial fetch yet, the manager's raw JSON
	//! is empty and we send (empty, empty) — perfectly fine. When the fetch
	//! eventually lands, DistributeTerrainStructuresToClients() pushes again to
	//! every connected player and this one will get the real data then.
	protected void OnPlayerAuditSuccessHandler(int playerId)
	{
		if (playerId <= 0) return;

		PlayerManager playerMgr = GetGame().GetPlayerManager();
		if (!playerMgr) return;

		SCR_PlayerController controller = SCR_PlayerController.Cast(
			playerMgr.GetPlayerController(playerId)
		);
		if (!controller) return;

		PushPlayerTerrainStructures(controller, playerId);
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
		PlayerManager playerMgr = GetGame().GetPlayerManager();
		set<int> shapePushedPlayers = new set<int>();

        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            NotifyNetworkJoined(device, network.GetNetworkID(), network.GetDeviceData());

			if (playerMgr)
			{
				IEntity player = GetPlayerFromDevice(device);
				if (player)
				{
					int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
					if (playerId >= 0 && !shapePushedPlayers.Contains(playerId))
					{
						shapePushedPlayers.Insert(playerId);
						SCR_PlayerController controller = SCR_PlayerController.Cast(
							playerMgr.GetPlayerController(playerId)
						);
						if (controller)
							PushPlayerShapes(controller, playerId);
					}
				}
			}
        }
		NotifyNetworkBroadcastingChange(network);
    }
    
    //! Notify the owning player's controller that a device left a specific network.
    //! The caller MUST pass the actual networkId because device.GetCurrentNetworkID() may
    //! have been pre-cleared to -1 on the server side (e.g. the user action's
    //! LeaveNetworkTDL() runs on both client and server, and optimistically resets
    //! m_iCurrentNetworkID before this path runs). Relying on the device's ID causes the
    //! NotifyClearNetwork RPC to silently skip, which leaves ghost members in the
    //! player's client-side m_mTDLNetworkMembersMap until the next stale-sweep tick.
    protected void NotifyNetworkLeft(AG0_TDLDeviceComponent device, int networkId)
	{
	    device.OnNetworkLeft();

	    IEntity player = GetPlayerFromDevice(device);
	    if (!player) return;

	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	    if (playerId < 0) return;

	    SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerManager().GetPlayerController(playerId)
	    );
	    if (!controller) return;

	    if (networkId > 0)
	    {
	        controller.NotifyClearNetwork(networkId);
	    }

	    PushPlayerShapes(controller, playerId);
	}

    //! Push connectivity snapshot to the owning player's controller.
    //! networkId MUST be passed from the caller (the network being iterated in
    //! UpdateNetworkConnectivity) — never read from device.GetCurrentNetworkID(),
    //! which may be pre-cleared to -1 on the server side when the user action's
    //! LeaveNetworkTDL() runs server-side optimistically. If that read returned -1
    //! the early-return below would silently skip controller.NotifyNetworkMembers,
    //! killing tick-based callsign/position updates for that player until something
    //! resets the id — which is exactly the "stale icons / callsign doesn't update
    //! on next tick" symptom.
    protected void NotifyNetworkConnectivity(AG0_TDLDeviceComponent device, int networkId, map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
	{
	    array<RplId> deviceIDs = new array<RplId>();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
	    {
	        deviceIDs.Insert(rplId);
	    }

	    device.OnNetworkConnectivityUpdated(deviceIDs);

	    array<ref AG0_TDLNetworkMember> membersArray = {};
	    foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
	    {
	        membersArray.Insert(member);
	    }

	    if (device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION))
	    {
	        device.SetLocalNetworkMembers(membersArray);
	        Print(string.Format("TDL_SYSTEM: Sent %1 members directly to INFORMATION device %2",
	            membersArray.Count(), device.GetOwner()), LogLevel.DEBUG);
	    }

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
    protected void NotifyNetworkBroadcastingChange(AG0_TDLNetwork network)
    {
        array<RplId> broadcastingDevices = {};
        
        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
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
	    
	    RplId videoSourceRplId = RplId.Invalid();
	    if (device.IsCameraBroadcasting())
	        videoSourceRplId = device.GetDeviceRplId();
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(player);
	        
	        foreach (AG0_TDLDeviceComponent playerDevice : playerDevices)
	        {
	            if (network.GetNetworkDevices().Contains(playerDevice))
	            {
	                RplId memberRplId = playerDevice.GetDeviceRplId();
	                AG0_TDLNetworkMember memberData = network.GetDeviceData().Get(memberRplId);
	                
	                if (memberData)
	                {
	                    memberData.SetVideoSourceRplId(videoSourceRplId);
	                    Print(string.Format("TDL_VIDEO_SYSTEM: Set VideoSourceRplId=%1 on member %2", 
	                        videoSourceRplId, memberData.GetPlayerName()), LogLevel.DEBUG);
	                }
	                
	                NotifyNetworkBroadcastingChange(network);
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
	void OnDeviceCallsignChanged(AG0_TDLDeviceComponent device)
	{
	    if (!Replication.IsServer()) return;

	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId == RplId.Invalid()) return;

	    Print(string.Format("TDL_SYSTEM_CALLSIGN: Processing callsign change for device %1",
	        device.GetOwner()), LogLevel.DEBUG);

	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkDevices().Contains(device))
	        {
	            AG0_TDLNetworkMember memberData = network.GetDeviceData().Get(deviceRplId);
	            if (memberData)
	            {
	                string newDisplayName = device.GetDisplayName();
	                memberData.SetPlayerName(newDisplayName);

	                Print(string.Format("TDL_SYSTEM_CALLSIGN: Updated member data for %1 to '%2'",
	                    deviceRplId, newDisplayName), LogLevel.DEBUG);

	                // NotifyNetworkMembersUpdated only re-broadcasts RplIds to devices — it does
	                // NOT push fresh member data (with the new callsign) through the primary
	                // PlayerController snapshot channel. Force an immediate connectivity rebuild
	                // so the updated GetDisplayName() flows to every member's
	                // m_mTDLNetworkMembersMap this frame, not on the next UpdateNetworks tick.
	                NotifyNetworkMembersUpdated(network);
	                UpdateNetworkConnectivity(network);
	            }
	            break;
	        }
	    }
	}
	
	//------------------------------------------------------------------------------------------------
    // MAIN ENTRY POINT: Send a message from a device
    //------------------------------------------------------------------------------------------------
    static void SendMessage(AG0_TDLSystem system, RplId senderDeviceRplId, string content,
                           ETDLMessageType messageType, RplId recipientRplId = RplId.Invalid())
    {
        if (!Replication.IsServer()) return;
        
        if (content.IsEmpty())
        {
            Print("TDL_MESSAGE_SYSTEM: Empty message content, ignoring", LogLevel.DEBUG);
            return;
        }
        
        AG0_TDLDeviceComponent senderDevice = system.GetDeviceByRplId(senderDeviceRplId);
        if (!senderDevice)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: Sender device %1 not found", senderDeviceRplId), LogLevel.DEBUG);
            return;
        }
        
        AG0_TDLNetwork network = FindNetworkForDevice(system, senderDevice);
        if (!network)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: Sender device %1 not in any network", senderDeviceRplId), LogLevel.DEBUG);
            return;
        }
        
        string senderCallsign = senderDevice.GetDisplayName();
        
        int messageId;
        if (messageType == ETDLMessageType.NETWORK_BROADCAST)
        {
            messageId = AddBroadcastToNetwork(network, senderDeviceRplId, senderCallsign, content);
			system.ApiNotifyMessageSent(network, senderCallsign, "broadcast");
        }
        else if (messageType == ETDLMessageType.DIRECT)
        {
            string recipientCallsign = "Unknown";
            AG0_TDLNetworkMember recipientMember = network.GetDeviceData().Get(recipientRplId);
            if (recipientMember)
                recipientCallsign = recipientMember.GetPlayerName();
            
            messageId = AddDirectToNetwork(network, senderDeviceRplId, senderCallsign, 
                                          content, recipientRplId, recipientCallsign);
			system.ApiNotifyMessageSent(network, senderCallsign, "direct");
        }
        
        PropagateMessagesInNetwork(system, network);
    }
    
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
            NotifySenderOfReadReceipt(system, network, msg, readerDeviceRplId);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    static void PropagateMessagesForDevice(AG0_TDLSystem system, AG0_TDLNetwork network,
                                          AG0_TDLDeviceComponent device,
                                          map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
    {
        if (!Replication.IsServer()) return;
        if (!network || !device) return;
        
        RplId deviceRplId = device.GetDeviceRplId();
        if (deviceRplId == RplId.Invalid()) return;
        
        set<RplId> connectedRplIds = new set<RplId>();
        foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
        {
            connectedRplIds.Insert(rplId);
        }
        
        array<ref AG0_TDLMessage> deliverable = GetDeliverableMessages(network, deviceRplId, connectedRplIds);
        
        if (deliverable.Count() > 0)
        {
            Print(string.Format("TDL_MESSAGE_PROPAGATION: %1 new messages can be delivered to %2",
                deliverable.Count(), device.GetDisplayName()), LogLevel.DEBUG);
            
            foreach (AG0_TDLMessage msg : deliverable)
            {
                msg.MarkDeliveredTo(deviceRplId);
            }
            
            SendMessagesToClient(system, network, device);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    static void PropagateMessagesInNetwork(AG0_TDLSystem system, AG0_TDLNetwork network)
    {
        if (!Replication.IsServer()) return;
        if (!network) return;
        
        bool anyDelivered = true;
        int iterations = 0;
        const int MAX_ITERATIONS = 10;
        
        while (anyDelivered && iterations < MAX_ITERATIONS)
        {
            anyDelivered = false;
            iterations++;
            
            foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
            {
                if (!device.CanAccessNetwork()) continue;
                
                RplId deviceRplId = device.GetDeviceRplId();
                if (deviceRplId == RplId.Invalid()) continue;
                
                set<RplId> connectedRplIds = GetDeviceConnectedRplIds(system, device, network);
                
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
        
        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            SendMessagesToClient(system, network, device);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    static set<RplId> GetDeviceConnectedRplIds(AG0_TDLSystem system, AG0_TDLDeviceComponent device, 
                                               AG0_TDLNetwork network)
    {
        set<RplId> result = new set<RplId>();
        
        RplId selfRplId = device.GetDeviceRplId();
        if (selfRplId != RplId.Invalid())
            result.Insert(selfRplId);
        
        foreach (AG0_TDLDeviceComponent otherDevice : network.GetNetworkDevices())
        {
            if (otherDevice == device) continue;
            if (!otherDevice.CanAccessNetwork()) continue;
            
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
    static void SendMessagesToClient(AG0_TDLSystem system, AG0_TDLNetwork network, 
                                    AG0_TDLDeviceComponent device)
    {
        if (!device) return;
        
        RplId deviceRplId = device.GetDeviceRplId();
        
        IEntity player = system.GetPlayerFromDevice(device);
        if (!player) return;
        
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
        if (playerId < 0) return;
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            playerMgr.GetPlayerController(playerId)
        );
        if (!controller) return;
        
        array<ref AG0_TDLMessageClient> clientMessages = BuildClientMessages(network, deviceRplId);
        
        controller.ReceiveTDLMessages(network.GetNetworkID(), clientMessages);
    }
    
    //------------------------------------------------------------------------------------------------
    static void NotifySenderOfReadReceipt(AG0_TDLSystem system, AG0_TDLNetwork network,
                                         AG0_TDLMessage msg, RplId readerRplId)
    {
        RplId senderRplId = msg.GetSenderRplId();
        
        AG0_TDLDeviceComponent senderDevice = system.GetDeviceByRplId(senderRplId);
        if (!senderDevice) return;
        
        IEntity senderPlayer = system.GetPlayerFromDevice(senderDevice);
        if (!senderPlayer) return;
        
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        int playerId = playerMgr.GetPlayerIdFromControlledEntity(senderPlayer);
        if (playerId < 0) return;
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            playerMgr.GetPlayerController(playerId)
        );
        if (!controller) return;
        
        set<RplId> senderConnected = GetDeviceConnectedRplIds(system, senderDevice, network);
        if (!senderConnected.Contains(readerRplId))
            return;
        
        controller.ReceiveTDLReadReceipt(network.GetNetworkID(), msg.GetMessageId(), readerRplId);
    }
    
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
    static int AddBroadcastToNetwork(AG0_TDLNetwork network, RplId senderRplId, 
                                    string senderCallsign, string content)
    {
        array<ref AG0_TDLMessage> messages = network.GetMessages();
        int nextId = network.GetNextMessageId();
        
        return network.AddBroadcastMessage(
            network, senderRplId, senderCallsign, content, messages, nextId
        );
    }
    
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
    static AG0_TDLMessage GetNetworkMessage(AG0_TDLNetwork network, int messageId)
    {
        return network.GetMessageById(network.GetMessages(), messageId);
    }
    
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessage> GetNetworkMessages(AG0_TDLNetwork network)
    {
        return network.GetMessages();
    }
    
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessage> GetDeliverableMessages(AG0_TDLNetwork network, RplId targetRplId,
                                                           set<RplId> connectedDevices)
    {
        return network.GetDeliverableMessages(
            network.GetMessages(), targetRplId, connectedDevices
        );
    }
    
    //------------------------------------------------------------------------------------------------
    static array<ref AG0_TDLMessageClient> BuildClientMessages(AG0_TDLNetwork network, RplId viewerRplId)
    {
        return network.BuildClientMessages(network.GetMessages(), viewerRplId);
    }
	
	//------------------------------------------------------------------------------------------------
	//! Get shape manager for rendering
	AG0_TDLMapShapeManager GetShapeManager()
	{
		if (!m_ApiManager)
			return null;
		return m_ApiManager.GetShapeManager();
	}
	
	//------------------------------------------------------------------------------------------------
	// API INTEGRATION METHODS
	//------------------------------------------------------------------------------------------------
	
	protected void ApiSendHeartbeat()
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "heartbeat");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("worldFile", GetGame().GetWorldFile());
	    json.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());
	    json.WriteValue("networkCount", m_aNetworks.Count());
	    json.WriteValue("deviceCount", m_aRegisteredNetworkDevices.Count());
	    json.WriteValue("playerCount", GetConnectedPlayerCount());

	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	protected void ApiSyncFullState()
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
	    json.WriteValue("type", "state_sync");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("worldFile", GetGame().GetWorldFile());
	    json.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());

	    array<ref AG0_TDLNetworkState> networkStates = {};
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        AG0_TDLNetworkState netState = new AG0_TDLNetworkState();
	        netState.networkId = network.GetNetworkID();
	        netState.networkName = network.GetNetworkName();
			netState.waveform = network.GetWaveform();
	        netState.deviceCount = network.GetNetworkDevices().Count();
	        netState.messageCount = network.GetMessages().Count();
	        
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
				
			    IEntity player = GetPlayerFromDevice(device);
			    if (player)
			    {
			        PlayerManager playerMgr = GetGame().GetPlayerManager();
			        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
			        if (playerId > 0)
			        {
			            devState.playerName = playerMgr.GetPlayerName(playerId);
			            string identityId = GetPlayerIdentityId(playerId);
			            if (!identityId.IsEmpty())
			            {
			                devState.playerIdentityId = identityId;
			                devState.playerPlatform = GetPlayerPlatform(playerId);
			            }
			            else
			            {
			                Print(string.Format("[TDL_API] Identity empty for player %1 (id: %2)", devState.playerName, playerId), LogLevel.DEBUG);
			            }
			        }
			    }
			    else
			    {
			        Print(string.Format("[TDL_API] No player found for device %1", device.GetDisplayName()), LogLevel.DEBUG);
			    }
	            
	            deviceStates.Insert(devState);
	        }
	        netState.devices = deviceStates;
	        networkStates.Insert(netState);
	    }
	    
	    json.WriteValue("networks", networkStates);
	    json.WriteValue("totalDevices", m_aRegisteredNetworkDevices.Count());
	    
		array<ref AG0_TDLMapMarkerState> markerStates = {};
	    
	    SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
	    if (markerMgr)
	    {
	        array<SCR_MapMarkerBase> staticMarkers = markerMgr.GetStaticMarkers();
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        
	        foreach (SCR_MapMarkerBase marker : staticMarkers)
	        {
	            if (!marker || !marker.IsTDLMarker())
	                continue;
	            
	            string quad = marker.GetTDLMarkerQuad();
	            if (quad.IsEmpty())
	                continue;
	            
	            AG0_TDLMapMarkerState ms = new AG0_TDLMapMarkerState();
	            ms.markerType = quad;
				ms.markerId = marker.GetMarkerID();
	            
	            int worldPos[2];
	            marker.GetWorldPos(worldPos);
	            ms.posX = worldPos[0];
	            ms.posZ = worldPos[1];
	            
	            ms.ownerPlayerId = marker.GetMarkerOwnerID();
	            ms.customText = marker.GetCustomText();
	            ms.colorIndex = marker.GetColorEntry();
	            
	            if (ms.ownerPlayerId > 0 && playerMgr)
	                ms.ownerPlayerName = playerMgr.GetPlayerName(ms.ownerPlayerId);
	            else
	                ms.ownerPlayerName = "";
	            
	            markerStates.Insert(ms);
	        }
	    }
	    
	    json.WriteValue("markers", markerStates);
		
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
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
	    
	    IEntity player = GetPlayerFromDevice(device);
	    if (player)
	    {
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	        if (playerId > 0)
	        {
	            json.WriteValue("playerName", playerMgr.GetPlayerName(playerId));
	            json.WriteValue("playerId", playerId);
				string identityId = GetPlayerIdentityId(playerId);
	            if (!identityId.IsEmpty())
	            {
	                json.WriteValue("playerIdentityId", identityId);
	                json.WriteValue("playerPlatform", GetPlayerPlatform(playerId));
	            }
	        }
	    }
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
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
	    
	    m_ApiManager.SubmitData(json.ExportToString());
	}
	
	protected int GetConnectedPlayerCount()
	{
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return 0;
	    
	    array<int> playerIds = {};
	    playerMgr.GetPlayers(playerIds);
	    return playerIds.Count();
	}
	
	AG0_TDLApiManager GetApiManager()
	{
	    return m_ApiManager;
	}
	
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
	    Print(string.Format("  Active Bridge Links: %1", m_aBridgeLinks.Count()), LogLevel.DEBUG);
	    
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        Print(string.Format("  Network: %1 (ID: %2, Waveform: %3)", network.GetNetworkName(), network.GetNetworkID(), network.GetWaveform()), LogLevel.DEBUG);
	        Print(string.Format("    Password: %1", network.GetNetworkPassword()), LogLevel.DEBUG);
	        Print(string.Format("    Devices: %1", network.GetNetworkDevices().Count()), LogLevel.DEBUG);
	        
	        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	        {
	            string deviceName = "UNKNOWN";
	            if (device.GetOwner())
	                deviceName = device.GetOwner().ToString();
	            
	            Print(string.Format("      Device: %1 (Player: %2, Waveform: %3)", deviceName, device.GetOwnerPlayerName(), device.GetWaveform()), LogLevel.DEBUG);
	        }
	        
	        Print(string.Format("    Device Data Entries: %1", network.GetDeviceData().Count()), LogLevel.DEBUG);
	        foreach (RplId rplId, AG0_TDLNetworkMember member : network.GetDeviceData())
	        {
	            Print(string.Format("      Data: %1 -> %2 (IP: %3, Caps: %4, Bridged: %5)", 
	                rplId, member.GetPlayerName(), member.GetNetworkIP(), member.GetCapabilities(), member.IsBridged()), LogLevel.DEBUG);
	        }
	    }
	    
	    foreach (AG0_TDLBridgeLink link : m_aBridgeLinks)
	    {
	        Print(string.Format("  Bridge: Network %1 <-> Network %2", link.m_iNetworkA, link.m_iNetworkB), LogLevel.DEBUG);
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
	    Print(string.Format("  Waveform: %1", device.GetWaveform()), LogLevel.DEBUG);
	    Print(string.Format("  Network Range: %1m", device.GetEffectiveNetworkRange()), LogLevel.DEBUG);
	    Print(string.Format("  Total Registered Devices: %1", m_aRegisteredNetworkDevices.Count()), LogLevel.DEBUG);
	}
	
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
	    Print(string.Format("    - BRIDGE: %1", (aggregatedCaps & AG0_ETDLDeviceCapability.BRIDGE) != 0), LogLevel.DEBUG);
	    
	    array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(player);
	    Print(string.Format("  Player TDL Devices: %1", playerDevices.Count()), LogLevel.DEBUG);
	    
	    foreach (AG0_TDLDeviceComponent device : playerDevices)
	    {
	        string deviceName = "UNKNOWN_DEVICE";
	        if (device.GetOwner())
	            deviceName = device.GetOwner().ToString();
	        
	        Print(string.Format("    Device: %1 (Caps: %2, Waveform: %3, Powered: %4, In Network: %5)", 
	            deviceName, device.GetActiveCapabilities(), device.GetWaveform(), device.IsPowered(), device.IsInNetwork()), LogLevel.DEBUG);
	    }
	}
}