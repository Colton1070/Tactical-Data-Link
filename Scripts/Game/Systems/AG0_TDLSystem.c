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
    // BRIDGE-capable device on whichever network it happens to be joined to.
    // Informational — used for logging and any UI that wants to surface
    // "who's bridging." Consumer-side reachability gating uses the player ID
    // below so we can resolve a per-network device on demand.
    RplId m_BridgeDeviceRplId;
    // PlayerID of the user whose carried devices span the two networks. The
    // bridge is the player, not the device — to be usable from a given member
    // of network A, the member must be able to mesh-reach the PLAYER's
    // network-A device (which may be a different device than the one stored
    // in m_BridgeDeviceRplId, since a player carrying e.g. a PRC 161 on
    // LINK16 and an MPU5 on MPU5 has the BRIDGE bit on only one of them).
    int m_iBridgingPlayerId;

    void AG0_TDLBridgeLink(int netA, int netB, RplId bridgeDevice, int playerId)
    {
        m_iNetworkA = netA;
        m_iNetworkB = netB;
        m_BridgeDeviceRplId = bridgeDevice;
        m_iBridgingPlayerId = playerId;
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

    int GetBridgingPlayerId() { return m_iBridgingPlayerId; }
}

class AG0_TDLNetwork
{
    protected int m_iNetworkID;
    // Stable, globally-unique identifier for this network instance. Generated once
    // at construction, never reused, never persisted across server restarts.
    //
    // Why this exists: m_iNetworkID is a session-local auto-increment that resets
    // on every dedicated server restart. After a restart, the first network minted
    // gets ID 1 again, its first message gets messageId 1 again — the API's
    // (serverId, networkId, messageId) composite key would collide with historical
    // rows. Worse, even without an upsert collision, an "all messages on network 1"
    // query on the API silently unions every "network 1" the server ever had into
    // one frankenstein timeline.
    //
    // m_sStableId is included on every API emission alongside m_iNetworkID so the
    // API can key persistence rows by stableId (which never collides) while UI and
    // logs continue to show the human-friendly numeric ID. Format is
    // `net-{unixTime}-{rand32}-{rand32}` — collision probability across all TDL
    // servers in human history is effectively zero with two 32-bit randoms.
    protected string m_sStableId;
    protected string m_sNetworkName;
    protected string m_sNetworkPassword;
    protected int m_eWaveform;
    protected ref array<AG0_TDLDeviceComponent> m_aNetworkDevices = {};
    protected ref map<RplId, ref AG0_TDLNetworkMember> m_mDeviceData = new map<RplId, ref AG0_TDLNetworkMember>();
    protected int m_iNextNetworkIP = 1;

    // Set of playerIds that received a NotifyNetworkMembers RPC for this network on the
    // previous UpdateNetworkConnectivity tick. Used to detect departures: a player who
    // was in the previous-tick set but not in the current-tick set has dropped their
    // last device on this network (radio dropped, died with loot loss, slot swap,
    // disconnect, etc.) and needs a NotifyClearNetwork RPC so their client-side
    // m_mTDLNetworkMembersMap entry for this network gets removed. Without this, the
    // player's cache shows ghost members from a network they're no longer on.
    protected ref set<int> m_aLastNotifiedPlayerIds = new set<int>();


	// Message storage
    protected ref array<ref AG0_TDLMessage> m_aMessages = {};
    protected int m_iNextMessageId = 1;

    // Message retention settings
    protected const int MAX_MESSAGES = 100;
    protected const int MESSAGE_EXPIRY_SECONDS = 3600;


    void AG0_TDLNetwork(int networkID, string name, string password, int waveform = AG0_ETDLWaveform.LEGACY)
    {
        m_iNetworkID = networkID;
        m_sStableId = string.Format("net-%1-%2-%3",
            System.GetUnixTime(),
            Math.RandomInt(0, 2147483647),
            Math.RandomInt(0, 2147483647));
        m_sNetworkName = name;
        m_sNetworkPassword = password;
        m_eWaveform = waveform;
    }

    int GetNetworkID() { return m_iNetworkID; }
    string GetStableId() { return m_sStableId; }
    string GetNetworkName() { return m_sNetworkName; }
    string GetNetworkPassword() { return m_sNetworkPassword; }
    int GetWaveform() { return m_eWaveform; }

    // Derived from waveform — satellite-ness is intrinsic to the waveform
    // tech, not stored on the network. A network with SATCOM_WAVEFORMS bits
    // in its mask is a satellite network; otherwise terrestrial.
    bool IsSatellite() { return AG0_TDLWaveformInfo.IsSatellite(m_eWaveform); }
    array<AG0_TDLDeviceComponent> GetNetworkDevices() { return m_aNetworkDevices; }
    map<RplId, ref AG0_TDLNetworkMember> GetDeviceData() { return m_mDeviceData; }
    set<int> GetLastNotifiedPlayerIds() { return m_aLastNotifiedPlayerIds; }
    void SetLastNotifiedPlayerIds(set<int> ids) { m_aLastNotifiedPlayerIds = ids; }
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
    //------------------------------------------------------------------------------------------------
    //! Image-message variants — parallel to AddBroadcastMessage / AddDirectMessage but
    //! populating the image fields. The text `caption` is what shows alongside the image.
    //------------------------------------------------------------------------------------------------
    static int AddBroadcastImageMessage(AG0_TDLNetwork network, RplId senderRplId,
                                        string senderCallsign, string caption,
                                        string deliveryId, string fingerprint, int sizeBytes,
                                        inout array<ref AG0_TDLMessage> messages,
                                        inout int nextMessageId)
    {
        AG0_TDLMessage msg = AG0_TDLMessage.CreateBroadcastImage(
            nextMessageId,
            network.GetNetworkID(),
            senderRplId,
            senderCallsign,
            caption,
            deliveryId,
            fingerprint,
            sizeBytes
        );

        messages.Insert(msg);
        int messageId = nextMessageId;
        nextMessageId++;
        return messageId;
    }

    static int AddDirectImageMessage(AG0_TDLNetwork network, RplId senderRplId,
                                     string senderCallsign, string caption,
                                     RplId recipientRplId, string recipientCallsign,
                                     string deliveryId, string fingerprint, int sizeBytes,
                                     inout array<ref AG0_TDLMessage> messages,
                                     inout int nextMessageId)
    {
        AG0_TDLMessage msg = AG0_TDLMessage.CreateDirectImage(
            nextMessageId,
            network.GetNetworkID(),
            senderRplId,
            senderCallsign,
            caption,
            recipientRplId,
            recipientCallsign,
            deliveryId,
            fingerprint,
            sizeBytes
        );

        messages.Insert(msg);
        int messageId = nextMessageId;
        nextMessageId++;
        return messageId;
    }

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
	// Photo manager — runs on BOTH server and client (unlike m_ApiManager which is server-only).
	// Owns the rgz cache, in-flight HTTP fetches (server-only path inside the manager), and
	// the per-request decode pipeline (base64 → gunzip → rect parse).
	protected ref AG0_TDLPhotoManager m_PhotoManager;
	// API sync intervals (in seconds)
	protected const float API_HEARTBEAT_INTERVAL = 60.0;
	protected float m_fApiStateSyncInterval = 5.0;
	protected float m_fTimeSinceApiHeartbeat = 0;
	protected float m_fTimeSinceApiStateSync = 0;
	protected const float API_SHAPES_POLL_INTERVAL = 5.0;
    protected float m_fTimeSinceShapesPoll = 0;

	// ============================================
	// WEB MIRROR — per-player snapshot store + tick
	//
	// Snapshot map is keyed by session playerId because that's what the holding
	// client knows about itself. Mirrored-identity set is keyed by persistent
	// identityId because that's what the API uses; the tick translates between
	// the two via GetPlayerIdentityId. Sets stay empty when no web tab is open,
	// so the mirror tick is effectively free for vanilla / non-mirrored servers.
	//
	// Tick runs at 1 Hz to satisfy the <1 s latency target in the design without
	// drowning the API ingest. Higher than 1 Hz would be wasted work — the
	// client already rate-limits its uplink to 10 Hz, and the API SSE fan-out
	// to each web tab is cheap whether the publish rate is 1 or 10 per second.
	// ============================================
	protected ref map<int, string> m_mMirrorSnapshots = new map<int, string>();
	protected ref set<string> m_aMirroredIdentities = new set<string>();
	// 2 Hz — fast enough that a web user's pan/zoom round-trip lands in about
	// 800 ms total. Backed off from 3 Hz because the frontend gesture-protection
	// windows can't keep up with 3 Hz snapshot rate; the higher rate was
	// triggering more visible drift/fight events on the web mirror than the
	// extra responsiveness was worth. 2 Hz is the comfortable middle.
	protected const float MIRROR_TICK_INTERVAL = 0.5;
	protected float m_fTimeSinceMirrorTick = 0;

	// Lazy-registered handler for SCR_BaseGameMode.GetOnPlayerAuditSuccess.
	// Used to deliver the terrain structures dataset to every player on session join,
	// independent of TDL network membership — so the data is always there/available.
	protected bool m_bPlayerAuditHandlerRegistered = false;
	


    // Networks storage
    protected ref array<ref AG0_TDLNetwork> m_aNetworks = {};
    protected int m_iNextNetworkID = 1;

    // Coalesces shape broadcasts within a single tick. Sweep-delete can
    // remove N shapes in one frame; without coalescing each removal
    // would fire a full DistributeShapesToClients (N × M-players ×
    // chunks). Dirty bit + CallLater(0) collapses that to one broadcast
    // per tick regardless of how many state changes piled up.
    protected bool m_bShapeBroadcastDirty;
    protected ref set<int> m_aQueuedShapeTargetedPushes;
    
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
	//! Look up a network by its session-local numeric ID. Returns null if no match.
	//! Used by the message_send queue handler as a fallback when an inbound command
	//! lacks the newer networkStableId field (older queued rows from before the
	//! stableId rollout).
	AG0_TDLNetwork GetNetworkById(int networkId)
	{
	    foreach (AG0_TDLNetwork n : m_aNetworks)
	    {
	        if (n.GetNetworkID() == networkId)
	            return n;
	    }
	    return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Look up a network by its globally-unique stableId. Returns null if no live
	//! network has that stableId (network was destroyed, server restarted, etc.).
	//!
	//! This is the preferred lookup path for any inbound queue command (and any
	//! other persistence-driven flow) because it survives session restarts and
	//! never collides across networks. The numeric ID is fine for in-game RPC
	//! parameters (cheap, transient) but every cross-restart boundary should pass
	//! stableId.
	AG0_TDLNetwork GetNetworkByStableId(string stableId)
	{
	    if (stableId.IsEmpty())
	        return null;
	    foreach (AG0_TDLNetwork n : m_aNetworks)
	    {
	        if (n.GetStableId() == stableId)
	            return n;
	    }
	    return null;
	}

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
	//! Reverse lookup: given a persistent identity UUID (the same one we send to the API
	//! in state_sync), return the currently-online session playerId. Returns -1 if the
	//! identity isn't connected to this server right now.
	//!
	//! Used by the message_send queue handler to resolve a web user back to their live
	//! in-game player. PlayerManager doesn't expose this directly, so we walk the small
	//! online-player list — fine even at high concurrency since N is small (<= server cap).
	int GetPlayerIdFromIdentityId(string identityId)
	{
	    if (identityId.IsEmpty())
	        return -1;

	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return -1;

	    array<int> playerIds = {};
	    playerMgr.GetPlayers(playerIds);
	    foreach (int playerId : playerIds)
	    {
	        if (GetPlayerIdentityId(playerId) == identityId)
	            return playerId;
	    }
	    return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve a device RplId to the session playerId that owns the controlled entity.
	//! Returns -1 if the device has no live player (NPC, AI, dropped radio, between-respawn, etc.).
	//!
	//! Hot path: called from the propagation/delivery loop, so we keep allocations out and
	//! short-circuit on Invalid early.
	int GetPlayerIdFromDeviceRplId(RplId deviceRplId)
	{
	    if (deviceRplId == RplId.Invalid())
	        return -1;

	    AG0_TDLDeviceComponent device = GetDeviceByRplId(deviceRplId);
	    if (!device)
	        return -1;

	    IEntity player = GetPlayerFromDevice(device);
	    if (!player)
	        return -1;

	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return -1;

	    return playerMgr.GetPlayerIdFromControlledEntity(player);
	}

	//------------------------------------------------------------------------------------------------
	//! Find the device a given player owns within a specific network. Used by message_send
	//! to enforce: a web user can only post in a network they're actually a member of in-game.
	//! No device → no compose. This is the hop logic's first gate — without a device on the
	//! network, the whole graph traversal can't even start.
	AG0_TDLDeviceComponent GetDeviceInNetworkForPlayer(int playerId, int networkId)
	{
	    if (playerId <= 0)
	        return null;

	    AG0_TDLNetwork network = null;
	    foreach (AG0_TDLNetwork n : m_aNetworks)
	    {
	        if (n.GetNetworkID() == networkId)
	        {
	            network = n;
	            break;
	        }
	    }
	    if (!network)
	        return null;

	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return null;
	    IEntity controlled = playerMgr.GetPlayerControlledEntity(playerId);
	    if (!controlled)
	        return null;

	    foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	    {
	        if (GetPlayerFromDevice(device) == controlled)
	            return device;
	    }
	    return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Drains the (messageId, deviceRplId) pairs collected during PropagateMessagesInNetwork
	//! and fires one API delivery event per pair whose recipient is web-linked. Identity
	//! resolution is cached by deviceRplId across the batch so each device costs at most one
	//! SCR_PlayerIdentityUtils lookup, no matter how many messages targeted it.
	void FlushApiDeliveryEvents(AG0_TDLNetwork network, array<int> messageIds, array<RplId> deviceRplIds)
	{
	    if (!network) return;
	    if (messageIds.Count() != deviceRplIds.Count()) return;

	    // Per-batch identity cache — RplId → identity (empty string means "checked, not linked")
	    map<RplId, string> identityCache = new map<RplId, string>();
	    map<RplId, int> playerIdCache = new map<RplId, int>();
	    map<RplId, string> callsignCache = new map<RplId, string>();

	    for (int i = 0; i < messageIds.Count(); i++)
	    {
	        RplId deviceRplId = deviceRplIds[i];
	        string identity;
	        int playerId;
	        string callsign;

	        if (identityCache.Contains(deviceRplId))
	        {
	            identity = identityCache.Get(deviceRplId);
	            playerId = playerIdCache.Get(deviceRplId);
	            callsign = callsignCache.Get(deviceRplId);
	        }
	        else
	        {
	            playerId = GetPlayerIdFromDeviceRplId(deviceRplId);
	            if (playerId > 0)
	                identity = GetPlayerIdentityId(playerId);
	            else
	                identity = "";

	            AG0_TDLDeviceComponent device = GetDeviceByRplId(deviceRplId);
	            if (device)
	                callsign = device.GetDisplayName();

	            identityCache.Set(deviceRplId, identity);
	            playerIdCache.Set(deviceRplId, playerId);
	            callsignCache.Set(deviceRplId, callsign);
	        }

	        if (identity.IsEmpty())
	            continue;  // Not a web-linked recipient — skip the API event entirely.

	        ApiNotifyMessageDelivered(network, messageIds[i],
	            deviceRplId, callsign, identity, playerId);
	    }
	}

	//------------------------------------------------------------------------------------------------
	// Message API - called from PlayerController
	//------------------------------------------------------------------------------------------------
	void SendTDLMessage(RplId senderDeviceRplId, string content, ETDLMessageType messageType, RplId recipientRplId = RplId.Invalid())
	{
	    SendMessage(this, senderDeviceRplId, content, messageType, recipientRplId);
	}

	//------------------------------------------------------------------------------------------------
	//! Public entry point for image-message delivery. The caller (typically the
	//! image_deliver queue handler in AG0_TDLApiManager) has already fetched the image
	//! payload into AG0_TDLPhotoManager's cache by deliveryId. This call:
	//!   1. Creates the image AG0_TDLMessage on the resolved network (broadcast or direct)
	//!   2. Triggers the existing message replication path so clients see the metadata
	//!   3. Resolves the recipient player IDs from network membership and kicks chunk
	//!      distribution via the photo manager.
	//! Returns the new messageId, or -1 on failure (sender / network resolution failed).
	int SendImageTDLMessage(RplId senderDeviceRplId, string caption, ETDLMessageType messageType,
	                        string deliveryId, string fingerprint, int sizeBytes,
	                        RplId recipientRplId = RplId.Invalid())
	{
	    return SendImageMessage(this, senderDeviceRplId, caption, messageType,
	        deliveryId, fingerprint, sizeBytes, recipientRplId);
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve "all online players reachable on this network" — used by chunk distribution
	//! to address the broadcast recipient set. For DIRECT messages the caller passes a
	//! single recipientRplId; this helper handles the broadcast case.
	//!
	//! Returns a deduplicated list of player IDs whose owning device is on this network.
	//! Players whose devices aren't replicated, who aren't online, or who can't be resolved
	//! to a player controller are skipped silently.
	array<int> ResolveNetworkPlayerIds(AG0_TDLNetwork network)
	{
	    array<int> result = {};
	    if (!network)
	        return result;

	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return result;

	    array<AG0_TDLDeviceComponent> devices = network.GetNetworkDevices();
	    if (!devices)
	        return result;

	    foreach (AG0_TDLDeviceComponent device : devices)
	    {
	        if (!device)
	            continue;
	        IEntity player = GetPlayerFromDevice(device);
	        if (!player)
	            continue;
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	        if (playerId <= 0)
	            continue;
	        if (!result.Contains(playerId))
	            result.Insert(playerId);
	    }

	    return result;
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve a single recipient device → owning player ID. Returns -1 on failure.
	int ResolveRecipientPlayerId(RplId recipientDeviceRplId)
	{
	    AG0_TDLDeviceComponent device = GetDeviceByRplId(recipientDeviceRplId);
	    if (!device)
	        return -1;
	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    if (!playerMgr)
	        return -1;
	    IEntity player = GetPlayerFromDevice(device);
	    if (!player)
	        return -1;
	    int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	    if (playerId <= 0)
	        return -1;
	    return playerId;
	}

	//------------------------------------------------------------------------------------------------
	//! Public wrapper around the protected ApiNotifyMessageSendFailed event so the
	//! AG0_TDLApiManager queue handlers (different class scope) can surface compose
	//! failures back to the web user. Kept thin to avoid leaking the protected internal.
	//! networkStableId is passed through if known; empty string when the queue command
	//! failed before we could resolve the network at all.
	void ApiNotifyMessageSendFailedPublic(string correlationId, string reason, int networkId, string networkStableId)
	{
	    ApiNotifyMessageSendFailed(correlationId, reason, networkId, networkStableId);
	}

	//------------------------------------------------------------------------------------------------
	//! Public wrapper around ApiNotifyImageDeliverFailed so the image_deliver queue
	//! handler (in AG0_TDLApiManager) and the fetch-completion sink can surface failures
	//! back to the web user. Symmetric with ApiNotifyMessageSendFailedPublic.
	void ApiNotifyImageDeliverFailedPublic(string correlationId, string deliveryId, string reason)
	{
	    ApiNotifyImageDeliverFailed(correlationId, deliveryId, reason);
	}

	void MarkTDLMessageRead(RplId readerDeviceRplId, int messageId)
	{
	    MarkMessageRead(this, readerDeviceRplId, messageId);

	    // After the static MarkMessageRead has flipped the bit and dispatched the
	    // existing in-game read receipt RPC, mirror the state to the API — but only
	    // for web-linked readers, so the web inbox flips DELIVERED→READ. Non-linked
	    // readers (NPCs, unattended devices) need no API state.
	    AG0_TDLDeviceComponent readerDevice = GetDeviceByRplId(readerDeviceRplId);
	    if (!readerDevice) return;
	    AG0_TDLNetwork network = FindNetworkForDevice(this, readerDevice);
	    if (!network) return;
	    AG0_TDLMessage msg = GetNetworkMessage(network, messageId);
	    if (!msg) return;
	    int playerId = GetPlayerIdFromDeviceRplId(readerDeviceRplId);
	    if (playerId <= 0) return;
	    string identity = GetPlayerIdentityId(playerId);
	    if (identity.IsEmpty()) return;

	    ApiNotifyMessageRead(network, messageId, readerDeviceRplId,
	        readerDevice.GetDisplayName(), identity, playerId);
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

		// Photo manager initialized on BOTH server and client. The manager itself
		// gates HTTP-issuing methods to server-only internally (clients never hit REST).
		m_PhotoManager = new AG0_TDLPhotoManager();
		if (!m_PhotoManager.Initialize())
		{
			Print("TDL_SYSTEM: Photo Manager initialization failed", LogLevel.WARNING);
			m_PhotoManager = null;
		}

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
        float timeSlice = GetWorld().GetFixedTimeSlice();

        // Photo manager ticks on BOTH server and client (cache age sweep, fetch
        // timeout sweep, decode-step driver). Must run before the server-only gate.
        if (m_PhotoManager)
            m_PhotoManager.Update(timeSlice);

        if (!Replication.IsServer()) return;

        // The game mode is not necessarily live during our OnInit (system-init
        // ordering is not guaranteed). Register lazily on the first tick where
        // it's available. Cheap once-only check after the flag flips.
        if (!m_bPlayerAuditHandlerRegistered)
            EnsurePlayerAuditHandlerRegistered();

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

			// Drain the shape-submit retry queue. Self-throttles on its own
			// interval so the only cost here is the timeSlice push-through.
			m_ApiManager.OnSubmitRetryTick(timeSlice);

			// Mirror tick — no-op when nothing is mirrored (ApiSyncMirrorSnapshots
			// short-circuits on empty m_aMirroredIdentities).
			m_fTimeSinceMirrorTick += timeSlice;
			if (m_fTimeSinceMirrorTick >= MIRROR_TICK_INTERVAL)
			{
				ApiSyncMirrorSnapshots();
				m_fTimeSinceMirrorTick = 0;
			}
	    }
    }
	
	//------------------------------------------------------------------------------------------------
	void ~AG0_TDLSystem()
	{
	    s_bShuttingDown = true;

	    if (m_ApiManager)
	        m_ApiManager = null;

	    if (m_PhotoManager)
	        m_PhotoManager = null;

	    // World unload hook — drop the AG0_TDLMenuController live registry so
	    // strong-ref entries from this world don't leak into the next one.
	    // Without this, every world restart accumulates dead controllers in
	    // s_aLiveControllers (visible as ever-growing frontends= counts in
	    // the [TDL_MIRROR_PROP] logs); the propagator wastes work iterating
	    // them and the first non-null primary can be one of the dead entries
	    // that has stale widget refs.
	    AG0_TDLMenuController.ClearLiveRegistryOnWorldUnload();

	    // Also drop the mirror state so a fresh world starts with no
	    // identities considered "actively mirrored." Web subscribers will
	    // re-establish via their normal subscribe flow.
	    if (m_aMirroredIdentities)
	        m_aMirroredIdentities.Clear();
	    if (m_mMirrorSnapshots)
	        m_mMirrorSnapshots.Clear();
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
	    
	    // Check equipment slots (vest, backpack, etc.) for any TDL devices stored on the character
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

	    // Template-instance guard: GM spawn-catalog entries carry our component
	    // but never get a bound RplId (they're parented to GameMode_GameMaster_Full,
	    // origin <0,0,0>, used only as the prototype the GM clones from). Bringing
	    // them into the registry pollutes connectivity calcs, spatial grids, and
	    // the marker pipeline — marker creation downstream fails on the invalid
	    // RplId and leaves orphan markers floating. Real spawned-from-template
	    // instances arrive with a valid RplId by the time the 2500ms deferred
	    // RegisterWithTDLSystem fires, so this gate excludes templates without
	    // affecting normal devices.
	    if (device.GetDeviceRplId() == RplId.Invalid())
	    {
	        Print(string.Format("TDL_DEVICE_REGISTRATION: Skipping device with invalid RplId (likely GM catalog template) owner=%1",
	            device.GetOwner()), LogLevel.DEBUG);
	        return;
	    }

	    // Mod-conflict guard: if another AG0_TDLDeviceComponent already registered
	    // on the SAME owner entity, REJECT this duplicate registration. Why
	    // first-wins instead of last-writer-wins:
	    //
	    //   Client-side, the menu UI binds via entity.FindComponent(AG0_TDLDeviceComponent),
	    //   which is singular and returns the FIRST-declared component on the
	    //   entity (deterministic from prefab order — Colton's experience confirms).
	    //   If the server cache here did last-writer-wins, server lookups via
	    //   GetDeviceByRplId would return the LAST-registered component, which is
	    //   a different physical script object than the one client UI is bound to.
	    //   Same RplId, different objects → callsign and messaging fail silently
	    //   because the RPC handler operates on the "wrong" component.
	    //
	    //   Bailing on the second registration keeps server cache aligned with
	    //   client FindComponent — both pick the first-declared component every
	    //   time, no split-brain. The duplicate component is effectively inert
	    //   (won't appear in m_aRegisteredNetworkDevices, won't receive system-
	    //   driven RPCs), but its presence in the prefab is still surfaced via
	    //   the warning so the underlying mod conflict gets fixed.
	    //
	    //   Skipped predicate gates: existing.GetOwner() == device.GetOwner()
	    //   filters out cross-entity RplId reuse (preview spawns, destroyed-
	    //   then-respawned items) which are NOT mod conflicts and should let
	    //   the cache update normally below.
	    RplId deviceRplId = device.GetDeviceRplId();
	    if (deviceRplId != RplId.Invalid())
	    {
	        AG0_TDLDeviceComponent existing = m_mDeviceCache.Get(deviceRplId);
	        if (existing && existing != device && existing.GetOwner() == device.GetOwner())
	        {
	            Print(string.Format("[TDL_MOD_CONFLICT] Entity %1 already has an AG0_TDLDeviceComponent on RplId %2. This is bad! Remove the conflicting mod.",
	                device.GetOwner(), deviceRplId), LogLevel.WARNING);
	            return;
	        }
	    }

	    m_aRegisteredNetworkDevices.Insert(device);

	    if (deviceRplId != RplId.Invalid())
	        m_mDeviceCache.Set(deviceRplId, device);

	    LogDeviceRegistration(device, true);
	    
	    m_fTimeSinceGridRebuild = 999.0;
	    
	    // Shutdown order can null-out callback entries before device OnDelete drains
	    foreach (AG0_TDLMapMarkerEntry markerEntry : m_MarkerCallbacks)
	    {
	        if (!markerEntry) continue;
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
	            ApiNotifyNetworkDeleted(m_aNetworks[i].GetNetworkID(), m_aNetworks[i].GetStableId(), m_aNetworks[i].GetNetworkName());
				m_aNetworks.Remove(i);
	            networksRemoved++;
	        }
	    }
	    
	    if (networksRemoved > 0)
	        Print(string.Format("TDL_NETWORK_CLEANUP: Removed %1 empty networks", networksRemoved), LogLevel.DEBUG);
	    
	    // Shutdown order can null-out callback entries before device OnDelete drains
	    foreach (AG0_TDLMapMarkerEntry markerEntry : m_MarkerCallbacks)
	    {
	        if (!markerEntry) continue;
	        markerEntry.OnDeviceUnregistered(device);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    // Network creation and management
    //
    // waveformOverride: when 0 (default), the new network inherits the creator's
    // full waveform mask, preserving existing single-network-per-create
    // behavior. When non-zero, the network is born with ONLY that waveform bit
    // — used by the vehicle action's m_eForceWaveform path so a device with
    // BFT2 + LINK16 can author a LINK16-only network without dragging its
    // satellite bit into a terrestrial mesh. Caller is responsible for
    // validating that the override is a subset of the creator's mask.
    //------------------------------------------------------------------------------------------------
    int CreateNetwork(AG0_TDLDeviceComponent creator, string networkName, string password, int waveformOverride = 0)
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

	    int effectiveWaveform = waveformOverride;
	    if (effectiveWaveform == 0)
	        effectiveWaveform = creator.GetWaveform();

	    // Satellite-ness derives from the effective waveform, not the device.
	    // A device with mixed BFT2 + LINK16 waveforms creates satellite vs
	    // terrestrial networks depending on which bit the action targets via
	    // waveformOverride.
	    bool isSatellite = AG0_TDLWaveformInfo.IsSatellite(effectiveWaveform);

	    Print(string.Format("TDL_NETWORK_CREATE: Attempting to create network '%1' by %2 (waveform=%3, satellite=%4)",
	        networkName, playerName, effectiveWaveform, isSatellite), LogLevel.DEBUG);

	    // Check for existing network with same credentials AND compatible
	    // waveform. Network's IsSatellite is derived from its waveform mask
	    // so the satellite-ness compatibility check is folded into the
	    // waveform-bit intersection — a candidate network can only match if
	    // it shares a waveform bit with effectiveWaveform, and those bits
	    // intrinsically have matching satellite-ness.
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkName() == networkName && network.GetNetworkPassword() == password
	            && (effectiveWaveform & network.GetWaveform()) != 0
	            && network.IsSatellite() == isSatellite)
	        {
	            Print(string.Format("TDL_NETWORK_CREATE: Network '%1' already exists with compatible waveform, joining instead", networkName), LogLevel.DEBUG);
	            JoinNetwork(creator, networkName, password, waveformOverride);
	            return network.GetNetworkID();
	        }
	    }

	    AG0_TDLNetwork newNetwork = new AG0_TDLNetwork(m_iNextNetworkID++, networkName, password, effectiveWaveform);
	    newNetwork.AddDevice(creator, deviceRplId, creator.GetDisplayName(), position);
	    m_aNetworks.Insert(newNetwork);

	    Print(string.Format("TDL_NETWORK_CREATE: Successfully created network '%1' (ID: %2, Waveform: %3, Satellite: %4)",
	        networkName, newNetwork.GetNetworkID(), effectiveWaveform, isSatellite), LogLevel.DEBUG);

	    NotifyNetworkJoined(creator, newNetwork.GetNetworkID(), newNetwork.GetWaveform(), newNetwork.GetDeviceData());
		ApiNotifyNetworkCreated(newNetwork, playerName);

	    return newNetwork.GetNetworkID();
	}
    
    // waveformOverride: when 0, uses the device's full waveform mask for
    // compatibility checks. When non-zero, only that single bit is considered
    // — symmetric with CreateNetwork's override so a vehicle action's
    // designated-waveform path can target a specific network type even on a
    // multi-waveform device. Satellite create-on-join: when the joining device
    // is satellite-mode and no matching network exists, this falls through to
    // CreateNetwork. Terrestrial behavior is unchanged.
    bool JoinNetwork(AG0_TDLDeviceComponent device, string networkName, string password, int waveformOverride = 0)
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

	    int effectiveWaveform = waveformOverride;
	    if (effectiveWaveform == 0)
	        effectiveWaveform = device.GetWaveform();

	    // Satellite-ness derives from the effective waveform. A multi-waveform
	    // device targeting LINK16 (terrestrial) via override and the same
	    // device targeting BFT2 (satellite) via override produce categorically
	    // different operations — this gate ensures they're tested against the
	    // right kind of existing network.
	    bool isSatellite = AG0_TDLWaveformInfo.IsSatellite(effectiveWaveform);

	    Print(string.Format("TDL_NETWORK_JOIN: %1 attempting to join network '%2' (waveform=%3, satellite=%4)",
	        playerName, networkName, effectiveWaveform, isSatellite), LogLevel.DEBUG);

	    // Find matching networks
	    array<AG0_TDLNetwork> matchingNetworks = new array<AG0_TDLNetwork>();

	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        if (network.GetNetworkName() == networkName && network.GetNetworkPassword() == password)
	            matchingNetworks.Insert(network);
	    }

	    if (matchingNetworks.IsEmpty())
	    {
	        // Satellite create-on-join: a satellite-mode device joining a
	        // name+password that doesn't exist yet auto-creates it. This
	        // matches the real BFT-2 mental model where you don't really
	        // "create" or "join" a satellite network in the terrestrial sense
	        // — you just configure your terminal for a group ID and you're on
	        // it. Terrestrial joins continue to fail loudly so existing
	        // behavior is preserved.
	        if (isSatellite)
	        {
	            Print(string.Format("TDL_NETWORK_JOIN: Satellite create-on-join — no matching network '%1', creating one", networkName), LogLevel.DEBUG);
	            int newId = CreateNetwork(device, networkName, password, waveformOverride);
	            return newId > 0;
	        }

	        Print(string.Format("TDL_NETWORK_JOIN: No matching networks found for '%1'", networkName), LogLevel.DEBUG);
	        return false;
	    }

	    Print(string.Format("TDL_NETWORK_JOIN: Found %1 matching networks", matchingNetworks.Count()), LogLevel.DEBUG);

	    // Check if in range of any existing network device — waveform must be
	    // compatible AND satellite-ness must match (terrestrial radio can't
	    // physically uplink to a satellite network, and vice versa).
	    foreach (AG0_TDLNetwork network : matchingNetworks)
	    {
	        if ((effectiveWaveform & network.GetWaveform()) == 0)
	        {
	            Print(string.Format("TDL_NETWORK_JOIN: Device waveform %1 incompatible with network waveform %2, skipping",
	                effectiveWaveform, network.GetWaveform()), LogLevel.DEBUG);
	            continue;
	        }

	        if (network.IsSatellite() != isSatellite)
	        {
	            Print(string.Format("TDL_NETWORK_JOIN: Satellite-mode mismatch (device=%1, network=%2), skipping",
	                isSatellite, network.IsSatellite()), LogLevel.DEBUG);
	            continue;
	        }

	        // Satellite networks bypass spatial range — the hub abstraction
	        // means the device is always "in range" of the network as long as
	        // it has a satellite uplink (which it does by virtue of being
	        // satellite-mode and powered).
	        bool inRange = isSatellite || IsDeviceInNetworkRange(device, network);
	        if (inRange)
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
    
    // No-arg variant: removes the device from EVERY network it's currently in.
    // The old single-network world only ever had one membership so the loop
    // broke after the first hit; for the multi-network world the device may
    // appear in several networks (e.g. a ground station on LINK16 + BFT2) and
    // the no-arg call is the "drop everything" semantic. Targeted single-
    // network leaves use LeaveNetworkById below.
    void LeaveNetwork(AG0_TDLDeviceComponent device)
    {
        if (!Replication.IsServer()) return;

        array<AG0_TDLNetwork> networksToLeave = new array<AG0_TDLNetwork>();
        foreach (AG0_TDLNetwork network : m_aNetworks)
        {
            if (network.GetNetworkDevices().Contains(device))
                networksToLeave.Insert(network);
        }

        foreach (AG0_TDLNetwork network : networksToLeave)
        {
            DoLeaveNetwork(device, network);
        }
    }

    // Targeted variant: removes the device from a single specific network.
    // Used by the vehicle action with a designated waveform, and by anything
    // that needs to surgically detach without touching other memberships.
    void LeaveNetworkById(AG0_TDLDeviceComponent device, int networkId)
    {
        if (!Replication.IsServer()) return;
        if (networkId <= 0) return;

        AG0_TDLNetwork network = FindNetworkByID(networkId);
        if (!network) return;
        if (!network.GetNetworkDevices().Contains(device)) return;

        DoLeaveNetwork(device, network);
    }

    // Waveform-targeted variant: finds the first joined network whose waveform
    // mask shares a bit with `waveform` and removes the device from it. Used
    // by vehicle actions whose m_eForceWaveform pins the operation to a
    // specific waveform — the action layer doesn't know networkIDs, only the
    // waveform it cares about, and asks the system to resolve.
    void LeaveNetworkByWaveform(AG0_TDLDeviceComponent device, int waveform)
    {
        if (!Replication.IsServer()) return;
        if (!device) return;
        if (waveform == 0) return;

        array<int> joinedIds = device.GetJoinedNetworkIDs();
        foreach (int netId : joinedIds)
        {
            AG0_TDLNetwork network = FindNetworkByID(netId);
            if (!network) continue;
            if ((network.GetWaveform() & waveform) == 0) continue;
            DoLeaveNetwork(device, network);
            return;
        }
    }

    // Shared body for the two variants above. Pulled out so the API-event and
    // empty-network-cleanup logic stays in lockstep across both call paths;
    // the only difference between the no-arg and targeted variants is which
    // network(s) get fed into this helper.
    protected void DoLeaveNetwork(AG0_TDLDeviceComponent device, AG0_TDLNetwork network)
    {
        // Capture identifiers BEFORE removing the device, because the network
        // object may be destroyed in the empty-cleanup branch below and we
        // still need stableId for the ApiNotifyNetworkDeleted call.
        int leftNetworkId = network.GetNetworkID();
        string leftNetworkStableId = network.GetStableId();
        string leftNetworkName = network.GetNetworkName();

        network.RemoveDevice(device);
        NotifyNetworkLeft(device, leftNetworkId);
        ApiNotifyDeviceLeft(leftNetworkId, leftNetworkStableId, leftNetworkName, device.GetDisplayName());

        if (network.HasDevices())
        {
            NotifyNetworkMembersUpdated(network);
            return;
        }

        // Empty-network cleanup. Tell the web API the network is gone BEFORE
        // removing it locally — otherwise the web map keeps the now-empty
        // network as a zombie entry, and any device that subsequently joins
        // or has its callsign updated appears "in two networks" because the
        // zombie membership never gets refreshed or removed. Mirrors the
        // symmetric call in the empty-network cleanup loop (~line 793) which
        // already does this for the periodic-tick path.
        ApiNotifyNetworkDeleted(leftNetworkId, leftNetworkStableId, leftNetworkName);
        m_aNetworks.RemoveItem(network);
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

            FindConnectedDevices(networkDevice, device, network);

            if (m_aConnectedDevices.Contains(device))
                return true;
        }

        return false;
    }
    
    // context: when non-null, restricts the waveform-intersection to bits the
    // network actually operates on. Required so two helicopters with BFT2 +
    // LINK16 capability that are only joined to a LINK16 network don't get a
    // satellite-shortcut connection — the BFT2 bit is in both their masks
    // but the network they're on doesn't carry it, so the satellite path
    // isn't live. Caller passes the network being evaluated; the legacy
    // no-context overload preserves the old "any shared waveform" semantic
    // for the few call sites without a network in scope.
    bool AreDevicesConnected(AG0_TDLDeviceComponent deviceA, AG0_TDLDeviceComponent deviceB, AG0_TDLNetwork context = null)
	{
	    if (!deviceA || !deviceB) return false;
	    if (!deviceA.CanAccessNetwork() || !deviceB.CanAccessNetwork()) return false;

	    // Waveform gate: devices must share at least one waveform bit to link at the RF layer
	    int sharedWaveforms = deviceA.GetWaveform() & deviceB.GetWaveform();
	    if (sharedWaveforms == 0)
	        return false;

	    // Scope by network when provided. A capability-level shared waveform
	    // (BFT2 on both devices) is only an active link if it's also a
	    // waveform the current network carries. Without this, two helicopters
	    // with BFT2 capability that are only on a LINK16 network would still
	    // "connect" via the satellite shortcut despite neither being logged
	    // into a BFT2 network.
	    if (context)
	    {
	        sharedWaveforms = sharedWaveforms & context.GetWaveform();
	        if (sharedWaveforms == 0)
	            return false;
	    }

	    // If any in-scope shared waveform is satellite-tier (BFT2, etc.) the
	    // pair has a satellite path available — the implicit hub (Iridium
	    // constellation or equivalent NOC) makes range a non-factor for that
	    // link. Devices that share only terrestrial waveforms within scope
	    // fall through to the LOS distance check.
	    if (AG0_TDLWaveformInfo.IsSatellite(sharedWaveforms))
	    {
	        LogConnectivityCheck(deviceA, deviceB, true, 0, 0);
	        return true;
	    }

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
    
    // context: forwarded to AreDevicesConnected so the chain-walk only follows
    // links that the network actually carries. Without this a capability-level
    // satellite waveform on intermediate devices would let two terrestrial-
    // network helicopters "connect" through a satellite-shortcut they aren't
    // actually using.
    protected void FindConnectedDevices(AG0_TDLDeviceComponent source, AG0_TDLDeviceComponent target, AG0_TDLNetwork context = null)
    {
        if (m_aProcessedDevices.Contains(source)) return;

        m_aProcessedDevices.Insert(source);

        if (!source.CanAccessNetwork()) return;

        if (AreDevicesConnected(source, target, context))
        {
            m_aConnectedDevices.Insert(target);
            return;
        }

        foreach (AG0_TDLDeviceComponent device : m_aRegisteredNetworkDevices)
        {
            if (device == source || m_aProcessedDevices.Contains(device)) continue;
            if (!device.CanAccessNetwork()) continue;

            if (AreDevicesConnected(source, device, context))
            {
                FindConnectedDevices(device, target, context);
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

	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        UpdateNetworkConnectivity(network);
	    }

	    // NOTE: the per-tick stale-network cleanup loop that used to live here was
	    // removed deliberately. It walked every device in every network, derived
	    // playerId via GetPlayerFromDevice → GetPlayerIdFromControlledEntity, then
	    // for every connected player issued ClearStaleNetworks(activeNetsArray) to
	    // their controller — and the controller-side handler removed any cached
	    // network NOT in activeNetsArray. On a real dedicated server, transient
	    // lookup misses (mid-respawn, controlled-entity replication lag, brief
	    // GetPlayerFromDevice nulls) happen often enough that the active list was
	    // routinely incomplete or empty for a tick, which translated into wiping
	    // the controller's m_mTDLNetworkMembersMap. Symptoms: contact lists going
	    // empty, callsign updates flickering or reverting a tick after they
	    // landed, and incoming messages failing to render because the menu UI
	    // gates on the network being present in the cache. A safety-gate version
	    // (skip cleanup for players not seen this tick) helped the empty-active
	    // case but didn't eliminate the partial-active case (player on networks
	    // 1+2 but only network 1 recorded for one tick → cache for network 2 gets
	    // wiped → callsign on network 2 flickers).
	    //
	    // We accept that cache entries for networks a player has truly left may
	    // linger across a session if the explicit NotifyNetworkLeft path misses
	    // them. The explicit path covers normal leaves; this safety net was net
	    // negative. If a stale-cleanup safety net is reintroduced later, it must
	    // (a) compare against the SET OF NETWORKS THAT EXIST SERVER-SIDE, not
	    // per-player active sets, (b) require multiple consecutive ticks of
	    // confirmation before acting, and (c) be tested on a real dedicated
	    // server — listen-server reproduces none of the relevant races.

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

                // Satellite and terrestrial networks with matching credentials
                // are categorically distinct — never merge across that line.
                // A BFT-2 SATCOM network and a coincidentally-named LINK16
                // mesh stay independent even if some device authored both.
                if (networkA.IsSatellite() != networkB.IsSatellite())
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
                    // Capture id/stableId/name before we tear networkB down — needed for
                    // the ApiNotifyNetworkDeleted call below. stableId is the field the
                    // API actually keys on; the others are for human-readable logs and
                    // backward compat with pre-stableId rows.
                    int mergedAwayId = networkB.GetNetworkID();
                    string mergedAwayStableId = networkB.GetStableId();
                    string mergedAwayName = networkB.GetNetworkName();

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

                    // Web API needs to know networkB is gone, otherwise it stays as
                    // a zombie entry on the map. Same root cause as the leave-and-
                    // recreate "device appears in two networks" bug.
                    ApiNotifyNetworkDeleted(mergedAwayId, mergedAwayStableId, mergedAwayName);

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

                // Multi-network devices contribute every network they're on,
                // not just a "primary" — a single device with LINK16 + BFT2
                // memberships now correctly registers both for bridge-pair
                // enumeration in the inner double-loop below.
                array<int> deviceNetIds = device.GetJoinedNetworkIDs();
                foreach (int netId : deviceNetIds)
                {
                    if (netId > 0 && playerNetworkIds.Find(netId) == -1)
                        playerNetworkIds.Insert(netId);
                }
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
                        m_aBridgeLinks.Insert(new AG0_TDLBridgeLink(netA, netB, bridgeDeviceRplId, playerId));
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
    //
    // Reachability gate: the bridge is "the player and their two radios," not a
    // network-scope fact. For a member of our network to receive the foreign
    // picture they must be able to mesh-reach (via m_aConnectedDevices) one of
    // the bridging player's our-network devices. Without this, a far-network
    // member with no path to the bridging player would still see the bridged
    // picture, which is unphysical — you can't get SA from a gateway you can't
    // talk to.
    //------------------------------------------------------------------------------------------------
    protected void AppendBridgedMembers(AG0_TDLNetwork network, AG0_TDLDeviceComponent device,
                                        inout map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
    {
        int myNetworkId = network.GetNetworkID();
        PlayerManager playerMgr = GetGame().GetPlayerManager();

        foreach (AG0_TDLBridgeLink link : m_aBridgeLinks)
        {
            if (!link.InvolvesNetwork(myNetworkId)) continue;

            int foreignNetworkId = link.GetOtherNetwork(myNetworkId);
            AG0_TDLNetwork foreignNetwork = FindNetworkByID(foreignNetworkId);
            if (!foreignNetwork) continue;

            // Resolve the bridging player and find their device(s) joined to
            // OUR network. If none are mesh-reachable from `device` in this
            // network, the bridge isn't usable from here — skip this link.
            if (!playerMgr) continue;
            IEntity bridgingPlayer = playerMgr.GetPlayerControlledEntity(link.GetBridgingPlayerId());
            if (!bridgingPlayer) continue;

            array<AG0_TDLDeviceComponent> bridgingPlayerDevices = GetPlayerAllTDLDevices(bridgingPlayer);
            bool canReachBridge = false;
            foreach (AG0_TDLDeviceComponent bridgingPlayerDevice : bridgingPlayerDevices)
            {
                if (!bridgingPlayerDevice) continue;
                if (!bridgingPlayerDevice.IsInNetworkById(myNetworkId)) continue;
                if (m_aConnectedDevices.Contains(bridgingPlayerDevice))
                {
                    canReachBridge = true;
                    break;
                }
            }
            if (!canReachBridge) continue;

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
                if (foreignPlayer && playerMgr)
                {
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
	    if (!network) return;

	    // If the network is empty this tick, anyone we notified last tick has departed.
	    // Fire NotifyClearNetwork at each former member so their client-side cache for
	    // this network gets removed, then drop our bookkeeping. Without this, a network
	    // that fully empties out leaves stale entries in every former member's
	    // m_mTDLNetworkMembersMap.
	    if (!network.HasDevices())
	    {
	        set<int> oldNotifiedEmpty = network.GetLastNotifiedPlayerIds();
	        if (oldNotifiedEmpty && oldNotifiedEmpty.Count() > 0)
	        {
	            PlayerManager pmEmpty = GetGame().GetPlayerManager();
	            int emptyNetId = network.GetNetworkID();
	            foreach (int oldPidEmpty : oldNotifiedEmpty)
	            {
	                SCR_PlayerController pcEmpty = SCR_PlayerController.Cast(pmEmpty.GetPlayerController(oldPidEmpty));
	                if (pcEmpty) pcEmpty.NotifyClearNetwork(emptyNetId);
	            }
	            network.SetLastNotifiedPlayerIds(new set<int>());
	        }
	        return;
	    }

	    // Snapshot the set of playerIds we notified on the previous tick, and start a
	    // fresh set for this tick. The per-device foreach below populates newNotified
	    // from the return value of NotifyNetworkConnectivity. After the foreach, the
	    // diff (oldNotified - newNotified) gives us exactly the players who lost their
	    // last device on this network this tick (drop, death, slot swap, disconnect).
	    set<int> oldNotified = network.GetLastNotifiedPlayerIds();
	    set<int> newNotified = new set<int>();

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
	                        
	                        float signalStrength;
	                        // Satellite pegs only when the network we're
	                        // building connectivity for actually carries a
	                        // satellite waveform AND the pair shares that bit.
	                        // Capability-level satellite sharing without the
	                        // network carrying it is not an active link.
	                        int sharedWaveforms = device.GetWaveform() & connectedDevice.GetWaveform() & network.GetWaveform();
	                        if (AG0_TDLWaveformInfo.IsSatellite(sharedWaveforms))
	                        {
	                            signalStrength = 100.0;
	                        }
	                        else
	                        {
	                            float effectiveRange = Math.Min(device.GetEffectiveNetworkRange(),
	                                                            connectedDevice.GetEffectiveNetworkRange());
	                            signalStrength = Math.Clamp(100.0 * (1.0 - (distance / effectiveRange)), 0.0, 100.0);
	                        }
							connectedData.SetSignalStrength(signalStrength);
	                        
	                        connectedMembers.Set(connectedRplId, connectedData);
	                    }
	                }
	            }
	        }
	        
	        // Append SA from any networks bridged to this one
	        AppendBridgedMembers(network, device, connectedMembers);

	        // Pass network.GetNetworkID() explicitly — see NotifyNetworkConnectivity doc.
	        // Capture the returned playerId (-1 if no RPC was sent) so we can detect
	        // departures after the loop completes.
	        int notifiedPid = NotifyNetworkConnectivity(device, network.GetNetworkID(), connectedMembers);
	        if (notifiedPid >= 0)
	            newNotified.Insert(notifiedPid);
			PropagateMessagesForDevice(this, network, device, connectedMembers);
	    }

	    // Departure detection: anyone we notified last tick but not this tick has
	    // dropped their last device on this network. Fire NotifyClearNetwork so their
	    // client-side m_mTDLNetworkMembersMap entry for this network gets removed.
	    PlayerManager departurePm = GetGame().GetPlayerManager();
	    int currentNetId = network.GetNetworkID();
	    foreach (int oldPid : oldNotified)
	    {
	        if (newNotified.Contains(oldPid)) continue;
	        SCR_PlayerController departedPc = SCR_PlayerController.Cast(departurePm.GetPlayerController(oldPid));
	        if (departedPc) departedPc.NotifyClearNetwork(currentNetId);
	    }
	    network.SetLastNotifiedPlayerIds(newNotified);

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

	        if (AreDevicesConnected(source, device, network))
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

		// Orphan promotion — shapes drawn while the player had no active
		// network bear AG0_TDL_SHAPE_NETWORK_ORPHAN as their networkId. As
		// soon as they pick up a network we re-scope those shapes so other
		// network members start seeing them. Picks the first network in the
		// set as the promotion target; multi-network promotion is left to
		// follow-up if it ever matters in practice (single-network play is
		// the dominant case).
		string playerIdentityId = GetPlayerIdentityId(playerId);
		if (!playerNetworkIds.IsEmpty() && !playerIdentityId.IsEmpty())
			PromoteOrphanShapesForPlayer(shapeMgr, playerIdentityId, playerNetworkIds[0]);

		string packedShapes = shapeMgr.GetPackedShapeDataForPlayer(playerNetworkIds, playerIdentityId);
		string syncHash = shapeMgr.GetLastSyncHash();

		controller.ReceiveTDLShapes(packedShapes, syncHash);
	}

	//------------------------------------------------------------------------------------------------
	//! Promote a player's orphan shapes to their first active network. Each
	//! promoted shape stays LOCAL-origin so the next API poll's full-replace
	//! doesn't drop it before the API mirror catches up. The raw JSON cache
	//! is re-emitted so subsequent pack passes carry the new networkId.
	protected void PromoteOrphanShapesForPlayer(AG0_TDLMapShapeManager shapeMgr, string playerIdentityId, int targetNetworkId)
	{
		array<string> ids = shapeMgr.CollectLocalShapeIdsByCreator(playerIdentityId);
		if (!ids || ids.IsEmpty())
			return;

		foreach (string id : ids)
		{
			AG0_TDLMapShape shape = shapeMgr.GetShape(id);
			if (!shape)
				continue;
			if (shape.m_iNetworkId != AG0_TDL_SHAPE_NETWORK_ORPHAN)
				continue;
			if (!shapeMgr.SetShapeNetworkId(id, targetNetworkId))
				continue;

			// Refresh the cached raw JSON so the next packed-data pass
			// emits the updated networkId to clients. Without this the
			// pack would still ship the orphan-marked payload even though
			// the in-memory shape has been promoted.
			shapeMgr.UpdateRawShapeJson(id, shape.ToJsonString());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Create a mod-originated shape from a client RPC. Validates the draft
	//! JSON, stamps server-side fields (id / version / createdAt / creator /
	//! networkId), inserts into the shape manager as LOCAL-origin, fans out
	//! to clients, and queues the best-effort API mirror. Idempotent on
	//! failure — a parse miss returns without state mutation.
	void CreateLocalShape(int playerId, string draftJson)
	{
		if (!Replication.IsServer())
			return;
		if (playerId <= 0 || draftJson.IsEmpty())
			return;
		if (!m_ApiManager)
			return;

		AG0_TDLMapShapeManager shapeMgr = m_ApiManager.GetShapeManager();
		if (!shapeMgr)
			return;

		AG0_TDLMapShape shape = shapeMgr.ParseSingleShape(draftJson);
		if (!shape)
		{
			Print("[TDL_SHAPES] CreateLocalShape: failed to parse draft", LogLevel.WARNING);
			return;
		}

		string identityId = GetPlayerIdentityId(playerId);
		if (identityId.IsEmpty())
		{
			Print(string.Format("[TDL_SHAPES] CreateLocalShape: no identity for playerId=%1", playerId), LogLevel.WARNING);
			return;
		}

		PlayerManager playerMgr = GetGame().GetPlayerManager();
		string playerName = "";
		if (playerMgr)
			playerName = playerMgr.GetPlayerName(playerId);

		// Resolve the player's active network. Mirrors the per-device walk
		// in PushPlayerShapes so the network picked here matches the one a
		// teammate's PushPlayerShapes pass would consider this player on.
		int firstNetworkId = AG0_TDL_SHAPE_NETWORK_ORPHAN;
		IEntity playerEntity;
		if (playerMgr)
			playerEntity = playerMgr.GetPlayerControlledEntity(playerId);
		if (playerEntity)
		{
			array<AG0_TDLDeviceComponent> playerDevices = GetPlayerAllTDLDevices(playerEntity);
			foreach (AG0_TDLDeviceComponent device : playerDevices)
			{
				foreach (AG0_TDLNetwork network : m_aNetworks)
				{
					if (network.GetNetworkDevices().Contains(device))
					{
						firstNetworkId = network.GetNetworkID();
						break;
					}
				}
				if (firstNetworkId != AG0_TDL_SHAPE_NETWORK_ORPHAN)
					break;
			}
		}

		shape.m_sId = GenerateLocalShapeId();
		shape.m_iVersion = 1;
		shape.m_iCreatedAt = System.GetUnixTime();
		shape.m_iNetworkId = firstNetworkId;
		shape.m_sCreatedBy = playerName;
		shape.m_sCreatedByPlayerIdentityId = identityId;

		string canonicalJson = shape.ToJsonString();
		shapeMgr.InsertLocalShape(shape, canonicalJson);

		Print(string.Format("[TDL_SHAPES] CreateLocalShape: %1 by '%2' (%3) on network=%4",
			shape.m_sId, playerName, identityId, firstNetworkId), LogLevel.DEBUG);

		// Coalesced broadcast — flushes once at the next tick boundary
		// regardless of how many CreateLocalShape calls happen this tick
		// (sweep doesn't create, but freehand commits and any future
		// burst-create path benefits from the same coalescer).
		QueueTargetedShapePush(playerId);

		// Best-effort API mirror. Failure (incl. endpoint not yet deployed)
		// leaves the shape as LOCAL — players keep seeing it, just without
		// cross-server persistence until the API path lands.
		m_ApiManager.SubmitShape(shape);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side handler for a client's AskDeleteShape RPC. Looks up the
	//! shape, verifies the requesting player's identity matches the shape's
	//! creator, removes from the local shape manager, broadcasts the
	//! updated feed, and queues the API-mirror DELETE. Silent no-op when
	//! the caller doesn't own the shape — clients can't grief each other's
	//! drawings.
	void DeleteLocalShape(int playerId, string shapeId)
	{
		if (!Replication.IsServer())
			return;
		if (playerId <= 0 || shapeId.IsEmpty())
			return;
		if (!m_ApiManager)
			return;

		AG0_TDLMapShapeManager shapeMgr = m_ApiManager.GetShapeManager();
		if (!shapeMgr)
			return;

		AG0_TDLMapShape shape = shapeMgr.GetShape(shapeId);
		if (!shape)
			return;

		string callerIdentity = GetPlayerIdentityId(playerId);
		if (callerIdentity.IsEmpty())
			return;
		if (shape.m_sCreatedByPlayerIdentityId != callerIdentity)
		{
			Print(string.Format("[TDL_SHAPES] DeleteLocalShape: %1 not owned by player %2", shapeId, playerId), LogLevel.DEBUG);
			return;
		}

		shapeMgr.RemoveShapeById(shapeId);

		Print(string.Format("[TDL_SHAPES] DeleteLocalShape: %1 by '%2'", shapeId, callerIdentity), LogLevel.DEBUG);

		// Coalesced broadcast — sweep-delete over N of the player's own
		// shapes piles N DeleteLocalShape calls into one tick; the
		// coalescer collapses that to a single broadcast + a single
		// targeted push to the creator regardless of N.
		QueueTargetedShapePush(playerId);

		// API-mirror delete. Best-effort — gated on IsEnabled inside the
		// manager so unlinked servers silently skip it. Local removal
		// already happened, so failure here just means the API copy stays
		// behind until the next admin cleanup; the in-game state is
		// already correct.
		m_ApiManager.SubmitShapeDelete(shapeId, callerIdentity);
	}

	//------------------------------------------------------------------------------------------------
	//! Generate a transient `local_<8 hex>` id for a mod-originated shape.
	//! Reconciled to a canonical `shape_<8 hex>` once the API mirror POST
	//! succeeds. Random source is Math.RandomInt which is good enough — the
	//! id only needs to be unique within the running server process.
	protected string GenerateLocalShapeId()
	{
		int hi = Math.RandomInt(0, 0x10000);
		int lo = Math.RandomInt(0, 0x10000);
		return string.Format("local_%1%2", IntToHex4(hi), IntToHex4(lo));
	}

	//! Lowercase 4-digit hex with leading zeros. Enfusion doesn't ship a
	//! printf-style hex format, so the digits are unpacked by hand.
	protected string IntToHex4(int v)
	{
		string digits = "0123456789abcdef";
		string outStr = "";
		for (int i = 3; i >= 0; i--)
		{
			int nibble = (v >> (i * 4)) & 0xF;
			outStr = outStr + digits.Substring(nibble, 1);
		}
		return outStr;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Push the shape feed to a single player by id. Targeted alternative
	//! to DistributeShapesToClients for cases where the network walk would
	//! skip the player (no-network creators on orphan shapes, or as a
	//! latency win for any player who needs the freshest state for a
	//! state change they directly caused). Idempotent on the client side
	//! via the syncHash check in RpcDo_ReceiveTDLShapes.
	void PushShapesToPlayer(int playerId)
	{
		if (!Replication.IsServer() || playerId <= 0)
			return;

		PlayerManager playerMgr = GetGame().GetPlayerManager();
		if (!playerMgr)
			return;

		SCR_PlayerController controller = SCR_PlayerController.Cast(playerMgr.GetPlayerController(playerId));
		if (!controller)
			return;

		PushPlayerShapes(controller, playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Mark the shape feed dirty for a coalesced broadcast at next-tick
	//! boundary. Multiple inline calls within the same tick collapse to a
	//! single FlushShapeBroadcast — keeping sweep-delete and any other
	//! multi-shape mutation paths at O(1) broadcasts per tick instead of
	//! O(N) where N is the number of shapes touched.
	void MarkShapesDirtyForBroadcast()
	{
		if (!Replication.IsServer())
			return;
		if (m_bShapeBroadcastDirty)
			return;
		m_bShapeBroadcastDirty = true;
		GetGame().GetCallqueue().CallLater(FlushShapeBroadcast, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Queue a per-player targeted push for the next coalesced flush.
	//! Same reasoning as MarkShapesDirtyForBroadcast: sweeping N of one
	//! player's shapes shouldn't fire N targeted pushes back to that
	//! player on top of the network fan-out. Set semantics dedupe by
	//! playerId.
	void QueueTargetedShapePush(int playerId)
	{
		if (!Replication.IsServer() || playerId <= 0)
			return;
		if (!m_aQueuedShapeTargetedPushes)
			m_aQueuedShapeTargetedPushes = new set<int>();
		m_aQueuedShapeTargetedPushes.Insert(playerId);
		MarkShapesDirtyForBroadcast();
	}

	//------------------------------------------------------------------------------------------------
	//! Coalesced flush entrypoint. Runs at most once per tick — the
	//! dirty bit blocks duplicate CallLater queueing in
	//! MarkShapesDirtyForBroadcast. Performs the full network broadcast
	//! and drains any queued targeted pushes.
	protected void FlushShapeBroadcast()
	{
		m_bShapeBroadcastDirty = false;
		DistributeShapesToClients();
		if (m_aQueuedShapeTargetedPushes)
		{
			for (int i = 0; i < m_aQueuedShapeTargetedPushes.Count(); i++)
			{
				PushShapesToPlayer(m_aQueuedShapeTargetedPushes[i]);
			}
			m_aQueuedShapeTargetedPushes.Clear();
		}
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
	//!
	//! Two distinct Reforger RPC constraints to respect:
	//!   * Overall Reliable packet ceiling ~14 KB before forced fragmentation.
	//!   * Per-string-parameter cap of 8191 bytes (8 KB − 1). Strings longer than
	//!     this are silently truncated on the wire — observed empirically as
	//!     5 chunks of 12000 each arriving as 5 chunks of 8191 each.
	//!
	//! 6000 keeps us well clear of the per-param cap with comfortable headroom
	//! for the syncHash / index / totalChunks params and RPC envelope.
	protected static const int TERRAIN_STRUCTURES_CHUNK_BYTES = 6000;

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
	//! Push the current terrain roads dataset to a single player as a sequence of
	//! <14 KB Reliable RPCs (chunked at TERRAIN_STRUCTURES_CHUNK_BYTES, same budget).
	//! See PushPlayerTerrainStructures for the rationale; the road network can be
	//! larger than structures so chunking matters even more here.
	protected void PushPlayerTerrainRoads(SCR_PlayerController controller, int playerId)
	{
		if (!m_ApiManager || !controller) return;

		AG0_TDLTerrainRoadManager mgr = m_ApiManager.GetTerrainRoadManager();
		if (!mgr)
		{
			controller.ReceiveTDLTerrainRoadsChunk(string.Empty, 1, 0, string.Empty);
			return;
		}

		string raw = mgr.GetLastRawJson();
		string hash = mgr.GetLastSyncHash();

		int totalLen = raw.Length();
		if (totalLen == 0)
		{
			controller.ReceiveTDLTerrainRoadsChunk(hash, 1, 0, string.Empty);
			return;
		}

		int chunkBytes = TERRAIN_STRUCTURES_CHUNK_BYTES;
		int totalChunks = (totalLen + chunkBytes - 1) / chunkBytes;

		for (int i = 0; i < totalChunks; i = i + 1)
		{
			int start = i * chunkBytes;
			int len = Math.Min(chunkBytes, totalLen - start);
			string chunk = raw.Substring(start, len);
			controller.ReceiveTDLTerrainRoadsChunk(hash, totalChunks, i, chunk);
		}

		Print(string.Format("[TDL_ROADS] Sent %1 chunks (%2 bytes) to player %3, hash=%4",
			totalChunks, totalLen, playerId, hash), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Distribute the current terrain roads dataset to all connected players.
	//! Same fan-out semantics as DistributeTerrainStructuresToClients.
	void DistributeTerrainRoadsToClients()
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
			PushPlayerTerrainRoads(controller, playerId);
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
		PushPlayerTerrainRoads(controller, playerId);
	}
	
    //------------------------------------------------------------------------------------------------
    // RPC notifications
    //------------------------------------------------------------------------------------------------
    // Passes the network's waveform through so the device can maintain its
    // m_iJoinedWaveformsMask (used by client-side action visibility checks
    // without a server round-trip).
    protected void NotifyNetworkJoined(AG0_TDLDeviceComponent device, int networkID, int networkWaveform, map<RplId, ref AG0_TDLNetworkMember> memberData)
    {
        array<RplId> deviceIDs = new array<RplId>();
        foreach (RplId rplId, AG0_TDLNetworkMember member : memberData)
        {
            deviceIDs.Insert(rplId);
        }
        device.OnNetworkJoined(networkID, networkWaveform, deviceIDs);
    }

    protected void NotifyNetworkMembersUpdated(AG0_TDLNetwork network)
    {
		PlayerManager playerMgr = GetGame().GetPlayerManager();
		set<int> shapePushedPlayers = new set<int>();

        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
        {
            NotifyNetworkJoined(device, network.GetNetworkID(), network.GetWaveform(), network.GetDeviceData());

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
    //! The caller MUST pass the actual networkId because the device's joined-networks
    //! list may have been pre-cleared on the server side (e.g. the user action's
    //! LeaveNetworkTDL() runs on both client and server, and optimistically clears
    //! the local set before this path runs). Relying on the device's primary ID
    //! causes the NotifyClearNetwork RPC to silently skip, which leaves ghost
    //! members in the player's client-side m_mTDLNetworkMembersMap until the
    //! next stale-sweep tick. The networkId is now also propagated into the
    //! device's OnNetworkLeft so a multi-network device removes only the
    //! correct slot from its per-network state.
    protected void NotifyNetworkLeft(AG0_TDLDeviceComponent device, int networkId)
	{
	    device.OnNetworkLeft(networkId);

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
    //! Returns the playerId that was notified for this device's owning player,
    //! or -1 if no NotifyNetworkMembers RPC was sent (no owning player, no controller,
    //! invalid networkId). Caller in UpdateNetworkConnectivity uses the return value to
    //! build the set of "players we notified this tick" for departure-detection.
    protected int NotifyNetworkConnectivity(AG0_TDLDeviceComponent device, int networkId, map<RplId, ref AG0_TDLNetworkMember> connectedMembers)
	{
	    array<RplId> deviceIDs = new array<RplId>();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
	    {
	        deviceIDs.Insert(rplId);
	    }

	    device.OnNetworkConnectivityUpdated(networkId, deviceIDs);

	    array<ref AG0_TDLNetworkMember> membersArray = {};
	    foreach (RplId rplId, AG0_TDLNetworkMember member : connectedMembers)
	    {
	        membersArray.Insert(member);
	    }

	    if (device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION))
	    {
	        device.SetLocalNetworkMembers(networkId, membersArray);
	        Print(string.Format("TDL_SYSTEM: Sent %1 members for network %2 directly to INFORMATION device %3",
	            membersArray.Count(), networkId, device.GetOwner()), LogLevel.DEBUG);
	    }

	    IEntity player = GetPlayerFromDevice(device);
	    if (!player) return -1;

	    PlayerManager playerMgr = GetGame().GetPlayerManager();
	    int playerId = playerMgr.GetPlayerIdFromControlledEntity(player);
	    if (playerId < 0) return -1;

	   	SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerManager().GetPlayerController(playerId)
	    );
        if (!controller)
        {
            Print(string.Format("TDL_System: Controller not found for player %1", playerId), LogLevel.DEBUG);
            return -1;
        }

	    if (networkId <= 0) return -1;

	    controller.NotifyNetworkMembers(networkId, membersArray);
	    return playerId;
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
        }
        else if (messageType == ETDLMessageType.DIRECT)
        {
            string recipientCallsign = "Unknown";
            AG0_TDLNetworkMember recipientMember = network.GetDeviceData().Get(recipientRplId);
            if (recipientMember)
                recipientCallsign = recipientMember.GetPlayerName();

            messageId = AddDirectToNetwork(network, senderDeviceRplId, senderCallsign,
                                          content, recipientRplId, recipientCallsign);
        }

        // Notify API with the canonical message record. Single callsite covers both
        // broadcast and direct — the message itself carries its type, content, and
        // routing so the API doesn't need a parallel switch on messageType.
        AG0_TDLMessage canonical = GetNetworkMessage(network, messageId);
        if (canonical)
        {
            system.ApiNotifyMessageSent(network, canonical);

            // Sender auto-delivery: CreateBroadcast/CreateDirect both insert the
            // sender into m_DeliveredTo at construction (AG0_TDLMessage.c:76, :100),
            // which means PropagateMessagesInNetwork's CanDeliverTo short-circuits
            // for the sender on every subsequent pass — MarkDeliveredTo is never
            // called for them. The hop logic considers this correct (sender always
            // has the message), but my ApiNotifyMessageDelivered hook only fires
            // from MarkDeliveredTo callsites, so the API never learns the sender is
            // delivered. Result: direct self-messages stay PENDING forever in the
            // web UI, and the sender's own broadcast inbox row never gets a
            // recipient record. Mirror the implicit delivery here so the API state
            // matches the mod state at send-time.
            int senderPlayerId = system.GetPlayerIdFromDeviceRplId(senderDeviceRplId);
            if (senderPlayerId > 0)
            {
                string senderIdentity = system.GetPlayerIdentityId(senderPlayerId);
                if (!senderIdentity.IsEmpty())
                {
                    system.ApiNotifyMessageDelivered(network, canonical.GetMessageId(),
                        senderDeviceRplId, senderCallsign, senderIdentity, senderPlayerId);
                }
            }
        }

        PropagateMessagesInNetwork(system, network);
    }

    //------------------------------------------------------------------------------------------------
    //! Image-message variant of SendMessage. Same orchestration shape: resolve sender/network,
    //! create the message via the IMAGE factory (NOT text), notify API of message_sent,
    //! propagate metadata via the existing replication path, then kick chunk distribution
    //! to the resolved recipient set.
    //!
    //! The image payload MUST already be in AG0_TDLPhotoManager's cache under `deliveryId` —
    //! caller is responsible for FetchByDeliveryId completing successfully before invoking this.
    //!
    //! Returns the new messageId, or -1 on failure.
    static int SendImageMessage(AG0_TDLSystem system, RplId senderDeviceRplId, string caption,
                                ETDLMessageType messageType, string deliveryId, string fingerprint,
                                int sizeBytes, RplId recipientRplId = RplId.Invalid())
    {
        if (!Replication.IsServer()) return -1;

        if (deliveryId.IsEmpty())
        {
            Print("TDL_MESSAGE_SYSTEM: SendImageMessage with empty deliveryId — ignoring", LogLevel.WARNING);
            return -1;
        }

        AG0_TDLDeviceComponent senderDevice = system.GetDeviceByRplId(senderDeviceRplId);
        if (!senderDevice)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: image sender device %1 not found", senderDeviceRplId), LogLevel.DEBUG);
            return -1;
        }

        AG0_TDLNetwork network = FindNetworkForDevice(system, senderDevice);
        if (!network)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: image sender device %1 not in any network", senderDeviceRplId), LogLevel.DEBUG);
            return -1;
        }

        string senderCallsign = senderDevice.GetDisplayName();
        int messageId = -1;

        if (messageType == ETDLMessageType.NETWORK_BROADCAST)
        {
            array<ref AG0_TDLMessage> messages = network.GetMessages();
            int nextId = network.GetNextMessageId();
            messageId = network.AddBroadcastImageMessage(network, senderDeviceRplId, senderCallsign, caption,
                deliveryId, fingerprint, sizeBytes, messages, nextId);
        }
        else if (messageType == ETDLMessageType.DIRECT)
        {
            string recipientCallsign = "Unknown";
            AG0_TDLNetworkMember recipientMember = network.GetDeviceData().Get(recipientRplId);
            if (recipientMember)
                recipientCallsign = recipientMember.GetPlayerName();

            array<ref AG0_TDLMessage> messages = network.GetMessages();
            int nextId = network.GetNextMessageId();
            messageId = network.AddDirectImageMessage(network, senderDeviceRplId, senderCallsign, caption,
                recipientRplId, recipientCallsign, deliveryId, fingerprint, sizeBytes,
                messages, nextId);
        }
        else
        {
            return -1;
        }

        // Notify API of the canonical message and the implicit sender-side delivery, mirroring
        // the rationale block in SendMessage above (sender pre-marked DELIVERED at construction
        // means PropagateMessagesInNetwork won't fire ApiNotifyMessageDelivered for them).
        AG0_TDLMessage canonical = GetNetworkMessage(network, messageId);
        if (canonical)
        {
            system.ApiNotifyMessageSent(network, canonical);

            int senderPlayerId = system.GetPlayerIdFromDeviceRplId(senderDeviceRplId);
            if (senderPlayerId > 0)
            {
                string senderIdentity = system.GetPlayerIdentityId(senderPlayerId);
                if (!senderIdentity.IsEmpty())
                {
                    system.ApiNotifyMessageDelivered(network, canonical.GetMessageId(),
                        senderDeviceRplId, senderCallsign, senderIdentity, senderPlayerId);
                }
            }
        }

        // Replicate metadata (deliveryId / fingerprint / sizeBytes / transferState=TRANSFERRING)
        // via the existing message-replication path. Clients see the placeholder image-message
        // and start showing "incoming…" UI before any chunks arrive.
        PropagateMessagesInNetwork(system, network);

        // Resolve recipient player IDs and kick chunk distribution. For broadcast, every
        // network member is a recipient; for direct, just the one.
        AG0_TDLPhotoManager photoMgr = system.GetPhotoManager();
        if (!photoMgr)
        {
            Print("TDL_MESSAGE_SYSTEM: photo manager unavailable — image metadata sent but no chunks", LogLevel.WARNING);
            return messageId;
        }

        array<int> recipientPlayerIds = {};
        if (messageType == ETDLMessageType.NETWORK_BROADCAST)
        {
            // ResolveNetworkPlayerIds already includes the sender's player (their device
            // is on the network too), so the sender sees their own broadcast in chat.
            recipientPlayerIds = system.ResolveNetworkPlayerIds(network);
        }
        else
        {
            int rid = system.ResolveRecipientPlayerId(recipientRplId);
            if (rid > 0)
                recipientPlayerIds.Insert(rid);

            // Include the sender in the recipient list so their own outbox renders the
            // image. Without this, the sender sees the message metadata (replicated via
            // the regular message path) but no chunk bytes ever arrive at their client,
            // so the image-message card sits at "[image — incoming…]" indefinitely.
            int senderPid = system.ResolveRecipientPlayerId(senderDeviceRplId);
            if (senderPid > 0 && !recipientPlayerIds.Contains(senderPid))
                recipientPlayerIds.Insert(senderPid);
        }

        if (recipientPlayerIds.Count() == 0)
        {
            Print(string.Format("TDL_MESSAGE_SYSTEM: image deliveryId=%1 has no online recipients — chunks skipped",
                deliveryId), LogLevel.WARNING);
            return messageId;
        }

        photoMgr.BeginDistribute(deliveryId, network.GetNetworkID(), recipientPlayerIds, null);
        return messageId;
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

            // Resolve identity ONCE per device — only fire API delivery events for
            // web-linked players (saves quota; the API ignores deliveries it can't
            // surface anywhere). Empty identityId == not linked / NPC / AI.
            int devicePlayerId = system.GetPlayerIdFromDeviceRplId(deviceRplId);
            string deviceIdentity = "";
            if (devicePlayerId > 0)
                deviceIdentity = system.GetPlayerIdentityId(devicePlayerId);
            string deviceCallsign = device.GetDisplayName();

            foreach (AG0_TDLMessage msg : deliverable)
            {
                msg.MarkDeliveredTo(deviceRplId);

                if (!deviceIdentity.IsEmpty())
                {
                    system.ApiNotifyMessageDelivered(network, msg.GetMessageId(),
                        deviceRplId, deviceCallsign, deviceIdentity, devicePlayerId);
                }
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

        // Buffer (msgId, deviceRplId) pairs that flipped to delivered during this
        // multi-pass relay so we can fire API events ONCE at the end. Doing it
        // inside the inner loop would re-emit duplicates as later passes revisit
        // already-delivered messages, and would also slow the propagation loop with
        // per-pair JSON serialization. Identity resolution is also expensive enough
        // to want to amortize.
        array<int> newlyDeliveredMessageIds = {};
        array<RplId> newlyDeliveredDeviceIds = {};

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
                        newlyDeliveredMessageIds.Insert(msg.GetMessageId());
                        newlyDeliveredDeviceIds.Insert(deviceRplId);

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

        // After the relay settles, fan out delivery events to the API for each
        // (message, web-linked recipient) pair that flipped this pass. Resolve
        // identity per unique deviceRplId so we only call SCR_PlayerIdentityUtils once.
        if (newlyDeliveredMessageIds.Count() > 0)
            system.FlushApiDeliveryEvents(network, newlyDeliveredMessageIds, newlyDeliveredDeviceIds);
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

            if (system.AreDevicesConnected(device, otherDevice, network))
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
	    
	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "heartbeat");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("worldFile", GetGame().GetWorldFile());
	    json.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());
	    json.WriteValue("networkCount", m_aNetworks.Count());
	    json.WriteValue("deviceCount", m_aRegisteredNetworkDevices.Count());
	    json.WriteValue("playerCount", GetConnectedPlayerCount());

	    m_ApiManager.SubmitData(json.SaveToString());
	}
	
	protected void ApiSyncFullState()
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    
	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "state_sync");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("worldFile", GetGame().GetWorldFile());
	    json.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());

	    array<ref AG0_TDLNetworkState> networkStates = {};
	    foreach (AG0_TDLNetwork network : m_aNetworks)
	    {
	        AG0_TDLNetworkState netState = new AG0_TDLNetworkState();
	        netState.networkId = network.GetNetworkID();
	        netState.networkStableId = network.GetStableId();
	        netState.networkName = network.GetNetworkName();
			netState.waveform = network.GetWaveform();
	        netState.deviceCount = network.GetNetworkDevices().Count();
	        netState.messageCount = network.GetMessages().Count();

	        array<ref AG0_TDLDeviceState> deviceStates = {};
	        foreach (AG0_TDLDeviceComponent device : network.GetNetworkDevices())
	        {
	            AG0_TDLDeviceState devState = new AG0_TDLDeviceState();
	            // Send the actual replication id, not a presence flag. The web UI
	            // round-trips this value back to us in message_send.recipientRplId
	            // for direct traffic, so collapsing every valid device to "1" made
	            // every direct compose route to whichever device happened to hold
	            // RplId(1) in that frame — wrong recipient, or none if (1) was free.
	            // RplId converts to int natively for the JSON writer (same path
	            // ApiNotifyMessageSent uses for senderRplId).
	            RplId deviceRplId = device.GetDeviceRplId();
	            if (deviceRplId.IsValid())
				    devState.rplId = deviceRplId;
				else
				    devState.rplId = 0;
	            devState.callsign = device.GetDisplayName();
	            devState.capabilities = device.GetActiveCapabilities();
	            devState.isPowered = device.IsPowered();
	            devState.isCameraBroadcasting = device.IsCameraBroadcasting();
	            
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
		
	    m_ApiManager.SubmitData(json.SaveToString());
	}
	
	protected void ApiNotifyNetworkCreated(AG0_TDLNetwork network, string creatorName)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "network_created");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkStableId", network.GetStableId());
	    json.WriteValue("networkName", network.GetNetworkName());
	    json.WriteValue("creatorName", creatorName);

	    m_ApiManager.SubmitData(json.SaveToString());
	}

	//! Signature carries stableId because the API needs it to delete the right
	//! persistence row even when the network instance is already gone — callers
	//! must capture stableId from the live network before tearing it down.
	protected void ApiNotifyNetworkDeleted(int networkId, string networkStableId, string networkName)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "network_deleted");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", networkId);
	    json.WriteValue("networkStableId", networkStableId);
	    json.WriteValue("networkName", networkName);

	    m_ApiManager.SubmitData(json.SaveToString());
	}

	protected void ApiNotifyDeviceJoined(AG0_TDLNetwork network, AG0_TDLDeviceComponent device)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "device_joined");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkStableId", network.GetStableId());
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
	    
	    m_ApiManager.SubmitData(json.SaveToString());
	}
	
	//! Same stableId-as-arg pattern as ApiNotifyNetworkDeleted — caller captures
	//! stableId before the network ref disappears. Even when the network survives
	//! the leave (other devices still on it), passing stableId here keeps the
	//! payload shape consistent across all device_left events.
	protected void ApiNotifyDeviceLeft(int networkId, string networkStableId, string networkName, string deviceCallsign)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "device_left");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", networkId);
	    json.WriteValue("networkStableId", networkStableId);
	    json.WriteValue("networkName", networkName);
	    json.WriteValue("deviceCallsign", deviceCallsign);

	    m_ApiManager.SubmitData(json.SaveToString());
	}
	
	//------------------------------------------------------------------------------------------------
	//! Notify API of a freshly-created message. Sends the full canonical record so the
	//! web app can persist it and stream it to linked viewers via SSE.
	//!
	//! NOTE: this is NOT a delivery notification — the message's per-recipient delivery
	//! state lives in the mod's hop graph (see ApiNotifyMessageDelivered, fired from
	//! PropagateMessagesForDevice when MarkDeliveredTo lands on a web-linked player).
	//! The API must treat all recipients as PENDING until it receives a matching
	//! message_delivered event for them.
	//!
	//! Callsite: AG0_TDLSystem.SendMessage — runs once per message regardless of origin
	//! (in-game compose OR web compose routed through message_send queue command), so
	//! API persistence naturally covers both paths without special-casing.
	protected void ApiNotifyMessageSent(AG0_TDLNetwork network, AG0_TDLMessage msg)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    if (!network || !msg)
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "message_sent");
	    json.WriteValue("timestamp", System.GetUnixTime());

	    // Network context — networkStableId is the API's primary persistence key
	    // (restart-proof). networkId is sidecar for human display and pre-rollout
	    // backward compat.
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkStableId", network.GetStableId());
	    json.WriteValue("networkName", network.GetNetworkName());

	    // Canonical message fields — API uses (serverId, networkStableId, messageId)
	    // as the idempotency key. Resending the same triple is a no-op on the API side.
	    json.WriteValue("messageId", msg.GetMessageId());
	    // Stringify the enum explicitly — keeps wire format human-readable on the
	    // API side and avoids depending on enum int values matching across versions.
	    string messageTypeStr = "broadcast";
	    if (msg.GetMessageType() == ETDLMessageType.DIRECT)
	        messageTypeStr = "direct";
	    json.WriteValue("messageType", messageTypeStr);
	    json.WriteValue("messageTimestamp", msg.GetTimestamp());
	    json.WriteValue("content", msg.GetContent());

	    // Sender — RplId for in-game replication, identity for web linking
	    json.WriteValue("senderRplId", msg.GetSenderRplId());
	    json.WriteValue("senderCallsign", msg.GetSenderCallsign());
	    int senderPlayerId = GetPlayerIdFromDeviceRplId(msg.GetSenderRplId());
	    if (senderPlayerId > 0)
	    {
	        string senderIdentity = GetPlayerIdentityId(senderPlayerId);
	        if (!senderIdentity.IsEmpty())
	            json.WriteValue("senderIdentityId", senderIdentity);
	        json.WriteValue("senderPlayerId", senderPlayerId);
	    }

	    // Recipient — only present for DIRECT messages
	    if (msg.GetMessageType() == ETDLMessageType.DIRECT)
	    {
	        RplId recipientRpl = msg.GetDirectRecipientRplId();
	        json.WriteValue("recipientRplId", recipientRpl);
	        json.WriteValue("recipientCallsign", msg.GetDirectRecipientCallsign());
	        int recipientPlayerId = GetPlayerIdFromDeviceRplId(recipientRpl);
	        if (recipientPlayerId > 0)
	        {
	            string recipientIdentity = GetPlayerIdentityId(recipientPlayerId);
	            if (!recipientIdentity.IsEmpty())
	                json.WriteValue("recipientIdentityId", recipientIdentity);
	            json.WriteValue("recipientPlayerId", recipientPlayerId);
	        }
	    }

	    // Image-message fields — present iff this is an IMAGE message. Default-text
	    // messages omit these entirely (no contentType/imageDeliveryId/etc. in the JSON)
	    // so existing API consumers that don't know about images are unaffected.
	    //
	    // The API uses imageDeliveryId to reconcile this success event back to the
	    // originating image_deliver queue row (correlationId → deliveryId via the
	    // queue table). See docs/messaging-images.md "Reconciling success".
	    if (msg.IsImage())
	    {
	        json.WriteValue("contentType", "image");
	        json.WriteValue("imageDeliveryId", msg.GetImageDeliveryId());
	        json.WriteValue("imageFingerprint", msg.GetImageFingerprint());
	        json.WriteValue("imageSizeBytes", msg.GetImageSizeBytes());
	        // imageTransferState intentionally omitted — at message_sent time it's
	        // always TRANSFERRING; the API doesn't need to track per-message transfer
	        // progress (chunked distribution is mod-internal).
	    }

	    m_ApiManager.SubmitData(json.SaveToString());
	}

	//------------------------------------------------------------------------------------------------
	//! Fired from PropagateMessagesForDevice when MarkDeliveredTo lands on a device whose
	//! owner is a web-linked player. The mod's hop logic stays authoritative — this just
	//! mirrors the state change so the web inbox flips PENDING→DELIVERED in real time.
	//!
	//! We deliberately skip non-linked recipients: the API has no UI for them, so spamming
	//! events for every NPC/AI device wastes bandwidth and the API quota. The check is in
	//! the caller (PropagateMessagesForDevice) so this method just emits unconditionally.
	protected void ApiNotifyMessageDelivered(AG0_TDLNetwork network, int messageId,
	                                          RplId recipientRplId, string recipientCallsign,
	                                          string recipientIdentityId, int recipientPlayerId)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    if (!network)
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "message_delivered");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkStableId", network.GetStableId());
	    json.WriteValue("messageId", messageId);
	    json.WriteValue("recipientRplId", recipientRplId);
	    json.WriteValue("recipientCallsign", recipientCallsign);
	    if (!recipientIdentityId.IsEmpty())
	        json.WriteValue("recipientIdentityId", recipientIdentityId);
	    if (recipientPlayerId > 0)
	        json.WriteValue("recipientPlayerId", recipientPlayerId);

	    m_ApiManager.SubmitData(json.SaveToString());
	}

	//------------------------------------------------------------------------------------------------
	//! Fired from MarkMessageRead when a web-linked player views a delivered message.
	//! Same gating rule as message_delivered — caller checks for link before invoking.
	protected void ApiNotifyMessageRead(AG0_TDLNetwork network, int messageId,
	                                     RplId readerRplId, string readerCallsign,
	                                     string readerIdentityId, int readerPlayerId)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;
	    if (!network)
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "message_read");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("networkId", network.GetNetworkID());
	    json.WriteValue("networkStableId", network.GetStableId());
	    json.WriteValue("messageId", messageId);
	    json.WriteValue("readerRplId", readerRplId);
	    json.WriteValue("readerCallsign", readerCallsign);
	    if (!readerIdentityId.IsEmpty())
	        json.WriteValue("readerIdentityId", readerIdentityId);
	    if (readerPlayerId > 0)
	        json.WriteValue("readerPlayerId", readerPlayerId);

	    m_ApiManager.SubmitData(json.SaveToString());
	}

	//------------------------------------------------------------------------------------------------
	//! Fired when web-side compose fails (sender not linked, no device in network, etc.).
	//! The API surfaces this back to the originating web user so they don't see a silent
	//! drop. `correlationId` echoes the queue command's id so the API can route the failure
	//! to the exact compose attempt.
	//!
	//! networkStableId may be empty when the failure happened before we could resolve the
	//! network (e.g. invalid_message_type before any lookup). Pass through whatever we
	//! have; the API uses correlationId as the primary failure-routing key, not networkId.
	protected void ApiNotifyMessageSendFailed(string correlationId, string reason, int networkId, string networkStableId)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "message_send_failed");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("correlationId", correlationId);
	    json.WriteValue("reason", reason);
	    json.WriteValue("networkId", networkId);
	    if (!networkStableId.IsEmpty())
	        json.WriteValue("networkStableId", networkStableId);

	    m_ApiManager.SubmitData(json.SaveToString());
	}

	//------------------------------------------------------------------------------------------------
	//! Surface an image_deliver failure to the API so the web UI can flip the compose to
	//! a rejected state. Mirrors message_send_failed shape — `correlationId` is the queue
	//! command's id (echoed back), `deliveryId` is the per-image identifier, `reason` is
	//! a short machine-readable string (e.g. "fetch_failed", "no_recipients", etc.).
	protected void ApiNotifyImageDeliverFailed(string correlationId, string deliveryId, string reason)
	{
	    if (!m_ApiManager || !m_ApiManager.CanCommunicate())
	        return;

	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "event");
	    json.WriteValue("event", "image_deliver_failed");
	    json.WriteValue("timestamp", System.GetUnixTime());
	    json.WriteValue("correlationId", correlationId);
	    json.WriteValue("deliveryId", deliveryId);
	    json.WriteValue("reason", reason);

	    m_ApiManager.SubmitData(json.SaveToString());
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

	AG0_TDLPhotoManager GetPhotoManager()
	{
	    return m_PhotoManager;
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

	// ============================================
	// WEB MIRROR — snapshot intake, subscription tracking, periodic API push
	// ============================================

	//! RPC entry point — called from RpcAsk_PushATAKPanelState on the player's
	//! controller. We don't parse here; the JSON is forwarded verbatim in the
	//! next tick so a flapping web tab can't make the receive path expensive.
	//! Last write wins per playerId because the snapshot is a full state dump,
	//! not a delta.
	void AcceptMirrorSnapshotFromClient(int playerId, string snapshotJson)
	{
		if (playerId <= 0)
			return;
		if (snapshotJson.IsEmpty())
			return;
		m_mMirrorSnapshots.Set(playerId, snapshotJson);
	}

	//! API queue handler entry — register an identity as actively mirrored.
	//! The web tab opening the mirror page caused the API to enqueue this
	//! command for whichever server this identity is online on; the resolution
	//! happens on the API side via the player:servers:<identityNorm> cache.
	//!
	//! Idempotent: opening a second tab on the same identity is a no-op here
	//! (the API handles reference counting). The held-device indicator gets
	//! pushed only on the empty -> non-empty transition for this identity.
	//! Cross-system accessor for the API manager's poll-interval gate. Active
	//! mirror sessions tighten the poll to 1s so web inputs reach the holding
	//! client without the default 5s wait. Returns 0 when nothing is mirrored
	//! so the API manager falls back to the configured interval.
	int GetActiveMirrorSessionCount()
	{
		if (!m_aMirroredIdentities)
			return 0;
		return m_aMirroredIdentities.Count();
	}

	void OnMirrorSubscribe(string identityId)
	{
		if (identityId.IsEmpty())
			return;
		if (m_aMirroredIdentities.Contains(identityId))
			return;
		m_aMirroredIdentities.Insert(identityId);
		PushMirrorIndicatorToIdentity(identityId, true);
		// Tell the owner client to invalidate its s_LastSentMirrorSnapshot
		// baseline so the next TickMirrorUplink unconditionally pushes a
		// fresh snapshot. Without this, the client might not push for many
		// seconds (TickMirrorUplink only fires on state change), and the
		// server's mirror tick has no client snapshot to forward — meaning
		// the web mirror stays in "establishing" state until the player
		// touches the ATAK. Force-push closes that window to ~one tick.
		RequestClientMirrorSnapshotResend(identityId);
	}

	//! Fires RpcDo_InvalidateMirrorSnapshotBaseline on the owning client so
	//! its next TickMirrorUplink pushes regardless of whether state changed.
	protected void RequestClientMirrorSnapshotResend(string identityId)
	{
		int pid = GetPlayerIdFromIdentityId(identityId);
		if (pid <= 0)
			return;
		PlayerManager pmgr = GetGame().GetPlayerManager();
		if (!pmgr)
			return;
		SCR_PlayerController pc = SCR_PlayerController.Cast(pmgr.GetPlayerController(pid));
		if (!pc)
			return;
		pc.InvalidateMirrorSnapshotBaseline();
	}

	void OnMirrorUnsubscribe(string identityId)
	{
		if (identityId.IsEmpty())
			return;
		if (!m_aMirroredIdentities.Contains(identityId))
			return;
		m_aMirroredIdentities.RemoveItem(identityId);
		// Drop the stashed snapshot too — nothing reads it after the last
		// subscriber leaves, and the next subscribe will pull a fresh one
		// within ~100ms of the holding client's next state mutation.
		int pid = GetPlayerIdFromIdentityId(identityId);
		if (pid > 0)
			m_mMirrorSnapshots.Remove(pid);
		PushMirrorIndicatorToIdentity(identityId, false);
	}

	//! Dispatch one mirror command to the holding client. The API queue handler
	//! resolves identityId -> playerId here before calling; we just plumb the
	//! RPC. If the player isn't online or has no controller, the command is
	//! dropped — the queue handler emits a rejection event in that path.
	bool DispatchMirrorCommandToIdentity(string identityId, string commandJson)
	{
		if (identityId.IsEmpty() || commandJson.IsEmpty())
		{
			Print("[TDL_MIRROR_DISPATCH] empty identity or command", LogLevel.WARNING);
			return false;
		}
		int pid = GetPlayerIdFromIdentityId(identityId);
		if (pid <= 0)
		{
			Print(string.Format("[TDL_MIRROR_DISPATCH] no playerId for identity=%1", identityId), LogLevel.WARNING);
			return false;
		}
		PlayerManager pmgr = GetGame().GetPlayerManager();
		if (!pmgr)
		{
			Print("[TDL_MIRROR_DISPATCH] no PlayerManager", LogLevel.WARNING);
			return false;
		}
		SCR_PlayerController pc = SCR_PlayerController.Cast(pmgr.GetPlayerController(pid));
		if (!pc)
		{
			Print(string.Format("[TDL_MIRROR_DISPATCH] no SCR_PlayerController for playerId=%1", pid), LogLevel.WARNING);
			return false;
		}
		pc.ApplyMirrorCommand(commandJson);
		return true;
	}

	//! Fire the indicator RPC at the player matching this identity (if online).
	//! No-op on offline identities — next time they connect their static will
	//! be false by default; the API can re-emit mirror_subscribe after the join
	//! audit to bring the indicator back up.
	protected void PushMirrorIndicatorToIdentity(string identityId, bool active)
	{
		int pid = GetPlayerIdFromIdentityId(identityId);
		if (pid <= 0)
			return;
		PlayerManager pmgr = GetGame().GetPlayerManager();
		if (!pmgr)
			return;
		SCR_PlayerController pc = SCR_PlayerController.Cast(pmgr.GetPlayerController(pid));
		if (!pc)
			return;
		pc.SetMirrorIndicator(active);
	}

	//! Mirror tick — assembles the per-identity snapshot payload and submits a
	//! single atak_mirror_sync API event. No-op on empty subscriber set so the
	//! cost on a server with zero mirror activity is one tick-time accumulator
	//! and one bool check.
	//!
	//! The server augments each snapshot with player world position, heading,
	//! and MGRS before submission — these are server-known fields the holding
	//! client doesn't need to push, and recomputing them here keeps the
	//! client-side uplink small.
	protected void ApiSyncMirrorSnapshots()
	{
		if (!m_ApiManager || !m_ApiManager.CanCommunicate())
			return;
		if (m_aMirroredIdentities.Count() == 0)
			return;

		PlayerManager pmgr = GetGame().GetPlayerManager();
		array<ref AG0_TDLMirrorEntry> entries = {};

		foreach (string identityId : m_aMirroredIdentities)
		{
			int pid = GetPlayerIdFromIdentityId(identityId);
			if (pid <= 0)
				continue;

			// Skip identities whose client hasn't uplinked a snapshot yet.
			// Fabricating "fresh ATAK" defaults (tracking=true, zoom=0.15) was
			// causing AugmentSnapshotWithServerFields to overwrite mapCenter
			// with the player position and ship that to the API — which the
			// web mirror then rendered as "snap to player" mid-pan whenever a
			// subscribe cycle wiped the cached client snapshot. Better to
			// emit no envelope for this identity until the client pushes its
			// real state (web sees a brief "establishing mirror" state instead
			// of bogus tracking-on snapshots).
			string clientJson = m_mMirrorSnapshots.Get(pid);
			if (clientJson.IsEmpty())
				continue;
			AG0_TDLMirrorSnapshot snap = new AG0_TDLMirrorSnapshot();
			snap.FromJson(clientJson);
			AugmentSnapshotWithServerFields(pid, snap);

			AG0_TDLMirrorEntry entry = new AG0_TDLMirrorEntry();
			entry.identityId = identityId;
			entry.playerId = pid;
			if (pmgr)
				entry.playerName = pmgr.GetPlayerName(pid);
			entry.CopyFromSnapshot(snap);
			entries.Insert(entry);
		}

		if (entries.Count() == 0)
			return;

		JsonSaveContext json = new JsonSaveContext();
		json.WriteValue("type", "atak_mirror_sync");
		json.WriteValue("timestamp", System.GetUnixTime());
		json.WriteValue("snapshots", entries);

		m_ApiManager.SubmitData(json.SaveToString());
	}

	// ApplyMirrorSnapshotDefaults was removed: shipping "fresh ATAK" defaults
	// for identities with no client uplink yet was causing post-subscribe
	// snapshots to ship tracking=true (then AugmentSnapshotWithServerFields
	// overwrote mapCenter with player position), which the web mirror rendered
	// as "snap to player" mid-pan after every subscribe cycle. ApiSyncMirrorSnapshots
	// now skips those identities until the client has uplinked real state,
	// and OnMirrorSubscribe triggers RpcDo_InvalidateMirrorSnapshotBaseline on
	// the owner to force that first uplink immediately.

	//! Fill in the server-side fields on a snapshot — player world position,
	//! heading, MGRS. These are computed from the live player entity, not
	//! cached. Cheap per-tick: at most one foreach across PlayerManager and
	//! a couple of GetOrigin / GetYawPitchRoll calls per mirrored player.
	//!
	//! Also enforces the "tracking-on ⇒ mapCenter follows player" invariant
	//! the in-game ATAK enforces via CenterOnPlayer each frame. Without this,
	//! a stale client snapshot whose mapCenter was sampled before the user
	//! turned tracking on, or an empty just-subscribed snapshot whose mapCenter
	//! is zero, would render the web mirror's camera at the wrong location even
	//! when the holding client's screen shows the map correctly tracking.
	protected void AugmentSnapshotWithServerFields(int playerId, AG0_TDLMirrorSnapshot snap)
	{
		PlayerManager pmgr = GetGame().GetPlayerManager();
		if (!pmgr)
			return;
		IEntity player = pmgr.GetPlayerControlledEntity(playerId);
		if (!player)
			return;
		vector pos = player.GetOrigin();
		snap.playerWorldX = pos[0];
		snap.playerWorldY = pos[1];
		snap.playerWorldZ = pos[2];
		vector angles = player.GetYawPitchRoll();
		snap.playerHeadingDeg = angles[0];
		snap.mgrs = AG0_MGRSGridUtils.GetFullMGRS(pos, 5);

		if (snap.playerTracking)
		{
			snap.mapCenterX = pos[0];
			snap.mapCenterZ = pos[2];
		}

		PopulateMirrorContacts(snap);
	}

	//! Server-side fill for the snapshot's per-viewer contacts[] array. Uses the
	//! viewer's active networkId (already populated client-side from their
	//! held device) and walks the network's stored member map. Per-pair signal
	//! readings live only in transient connectivity ticks, so the network's
	//! own GetDeviceData() is the closest stable mirror of what
	//! PopulateDetailView reads in-game — the values are refreshed by the
	//! same connectivity passes that update the in-game contact list.
	//!
	//! No contacts emitted when the player isn't in a network (snap.networkId
	//! <= 0); the web mirror treats absence the same as an empty contacts
	//! array.
	protected void PopulateMirrorContacts(AG0_TDLMirrorSnapshot snap)
	{
		if (!snap.contacts)
			snap.contacts = new array<ref AG0_TDLMirrorContact>();
		else
			snap.contacts.Clear();

		if (snap.networkId <= 0)
			return;

		AG0_TDLNetwork network = FindNetworkByID(snap.networkId);
		if (!network)
			return;

		map<RplId, ref AG0_TDLNetworkMember> data = network.GetDeviceData();
		if (!data)
			return;

		foreach (RplId rplId, AG0_TDLNetworkMember member : data)
		{
			if (!member)
				continue;

			AG0_TDLMirrorContact c = new AG0_TDLMirrorContact();
			RplId memberRpl = member.GetRplId();
			if (memberRpl.IsValid())
				c.rplId = memberRpl;
			else
				c.rplId = 0;

			c.signalStrength = member.GetSignalStrength();
			c.networkIp = member.GetNetworkIP();
			c.isGpsActive = member.IsGPSActive();
			c.isBridged = member.IsBridged();
			c.sourceNetworkId = member.GetSourceNetworkId();

			RplId videoRpl = member.GetVideoSourceRplId();
			if (videoRpl.IsValid())
				c.videoSourceRplId = videoRpl;
			else
				c.videoSourceRplId = 0;

			snap.contacts.Insert(c);
		}
	}
}