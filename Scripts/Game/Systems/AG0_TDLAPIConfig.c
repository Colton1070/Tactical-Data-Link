//------------------------------------------------------------------------------------------------
// AG0_TDLApiConfig.c
// TDL API Configuration and Communication System
// Handles API key storage in $profile folder and REST communication with tdl.blufor.info
// Server-side only - never runs on clients or proxies
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// API Configuration Data Class
// Uses JsonApiStruct for clean serialization to/from JSON files
//------------------------------------------------------------------------------------------------
class AG0_TDLApiConfigData : JsonApiStruct
{
    string apiKey;
    string serverName;
    bool enabled;
    int pollIntervalSeconds;
	int stateSyncIntervalSeconds;

    
    //------------------------------------------------------------------------------------------------
    void AG0_TDLApiConfigData()
    {
        // Register variables for JSON serialization
        RegV("apiKey");
        RegV("serverName");
        RegV("enabled");
        RegV("pollIntervalSeconds");
		RegV("stateSyncIntervalSeconds");
        
        // Set defaults
        apiKey = "";
        serverName = "Unnamed Server";
        enabled = true;
        pollIntervalSeconds = 5;
		stateSyncIntervalSeconds = 5; // default 5s for sync worker
    }
    
    //------------------------------------------------------------------------------------------------
    bool HasValidApiKey()
    {
        return !apiKey.IsEmpty() && apiKey.Length() > 10;
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDLApiConfigData CreateDefault()
    {
        AG0_TDLApiConfigData config = new AG0_TDLApiConfigData();
        config.apiKey = "";
        config.serverName = "Unnamed Server";
        config.enabled = true;
        config.pollIntervalSeconds = 5;
        return config;
    }
}

//------------------------------------------------------------------------------------------------
// REST Callback for API Submit endpoint
//------------------------------------------------------------------------------------------------
class AG0_TDLApiSubmitCallback : RestCallback
{
    protected AG0_TDLApiManager m_Manager;
    
    //------------------------------------------------------------------------------------------------
    void AG0_TDLApiSubmitCallback(AG0_TDLApiManager manager)
    {
        m_Manager = manager;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnSuccessHandler(RestCallback cb)
    {
        string data = cb.GetData();
        if (m_Manager)
            m_Manager.OnSubmitSuccess(data);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnErrorHandler(RestCallback cb)
    {
        if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
        {
            Print("[TDL_API] Submit request timed out", LogLevel.DEBUG);
            if (m_Manager)
                m_Manager.OnSubmitTimeout();
            return;
        }
        
        int errorCode = cb.GetHttpCode();
        if (m_Manager)
            m_Manager.OnSubmitError(errorCode);
    }
}

//------------------------------------------------------------------------------------------------
// REST Callback for API Queue polling endpoint
//------------------------------------------------------------------------------------------------
class AG0_TDLApiQueueCallback : RestCallback
{
    protected AG0_TDLApiManager m_Manager;
    
    //------------------------------------------------------------------------------------------------
    void AG0_TDLApiQueueCallback(AG0_TDLApiManager manager)
    {
        m_Manager = manager;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnSuccessHandler(RestCallback cb)
    {
        string data = cb.GetData();
        if (m_Manager)
            m_Manager.OnQueuePollSuccess(data);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnErrorHandler(RestCallback cb)
    {
        if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
        {
            if (m_Manager)
                m_Manager.OnQueuePollTimeout();
            return;
        }
        
        int errorCode = cb.GetHttpCode();
        if (m_Manager)
            m_Manager.OnQueuePollError(errorCode);
    }
}

//------------------------------------------------------------------------------------------------
// REST Callback for Shapes polling endpoint
//------------------------------------------------------------------------------------------------
class AG0_TDLApiShapesCallback : RestCallback
{
	protected AG0_TDLApiManager m_Manager;
	
	//------------------------------------------------------------------------------------------------
	void AG0_TDLApiShapesCallback(AG0_TDLApiManager manager)
	{
		m_Manager = manager;
		SetOnSuccess(OnSuccessHandler);
		SetOnError(OnErrorHandler);
	}
	
	//------------------------------------------------------------------------------------------------
	void OnSuccessHandler(RestCallback cb)
	{
		string data = cb.GetData();
		if (m_Manager)
			m_Manager.OnShapesPollSuccess(data);
	}
	
	//------------------------------------------------------------------------------------------------
	void OnErrorHandler(RestCallback cb)
	{
		if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
		{
			if (m_Manager)
				m_Manager.OnShapesPollTimeout();
			return;
		}
		
		int errorCode = cb.GetHttpCode();
		if (m_Manager)
			m_Manager.OnShapesPollError(errorCode);
	}
}

//------------------------------------------------------------------------------------------------
// REST Callback for POST /shapes (mod-originated shape create).
// Carries the local_<hex> ID so the manager can reconcile the canonical
// shape_<hex> ID from the response back to the in-memory entry. Failure
// paths leave the local-origin entry intact — clients keep seeing the
// shape, and the retry queue picks it up later.
//------------------------------------------------------------------------------------------------
class AG0_TDLApiSubmitShapeCallback : RestCallback
{
	protected AG0_TDLApiManager m_Manager;
	protected string m_sLocalShapeId;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLApiSubmitShapeCallback(AG0_TDLApiManager manager, string localShapeId)
	{
		m_Manager = manager;
		m_sLocalShapeId = localShapeId;
		SetOnSuccess(OnSuccessHandler);
		SetOnError(OnErrorHandler);
	}

	//------------------------------------------------------------------------------------------------
	void OnSuccessHandler(RestCallback cb)
	{
		if (m_Manager)
			m_Manager.OnSubmitShapeSuccess(m_sLocalShapeId, cb.GetData());
	}

	//------------------------------------------------------------------------------------------------
	void OnErrorHandler(RestCallback cb)
	{
		int errorCode = cb.GetHttpCode();
		if (m_Manager)
			m_Manager.OnSubmitShapeError(m_sLocalShapeId, errorCode);
	}
}

//------------------------------------------------------------------------------------------------
// REST Callback for DELETE /shapes/[shapeId] (mod-originated shape delete).
// Fire-and-forget — the local-side removal already happened before this
// fires, so success/error here is purely about whether the API mirror got
// the memo. Errors get logged but don't trigger retries (a stuck DELETE
// in a retry queue could resurrect a deleted shape on next reconcile,
// which is worse than letting the API copy linger until admin cleanup).
//------------------------------------------------------------------------------------------------
class AG0_TDLApiDeleteShapeCallback : RestCallback
{
	protected AG0_TDLApiManager m_Manager;
	protected string m_sShapeId;

	//------------------------------------------------------------------------------------------------
	//! Manager ref is required so the delete path can release the hot-gate
	//! slot it claimed in SubmitShapeDelete and feed the shapes breaker.
	//! Without it a sustained outage of delete traffic would slowly leak
	//! gate slots until the manager could no longer send anything.
	void AG0_TDLApiDeleteShapeCallback(AG0_TDLApiManager manager, string shapeId)
	{
		m_Manager = manager;
		m_sShapeId = shapeId;
		SetOnSuccess(OnSuccessHandler);
		SetOnError(OnErrorHandler);
	}

	//------------------------------------------------------------------------------------------------
	void OnSuccessHandler(RestCallback cb)
	{
		if (m_Manager)
			m_Manager.OnDeleteShapeCompleted(true, 0);
		Print(string.Format("[TDL_SHAPES] DeleteShape API ok for %1", m_sShapeId), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	void OnErrorHandler(RestCallback cb)
	{
		int errorCode = cb.GetHttpCode();
		if (m_Manager)
			m_Manager.OnDeleteShapeCompleted(false, errorCode);
		// 404 is fine — the shape might be LOCAL-origin (never persisted)
		// or already gone server-side via another path.
		LogLevel level = LogLevel.WARNING;
		if (errorCode == 404)
			level = LogLevel.DEBUG;
		Print(string.Format("[TDL_SHAPES] DeleteShape API error %1 for %2", errorCode, m_sShapeId), level);
	}
}

//------------------------------------------------------------------------------------------------
// REST Callback for Terrain Structures polling endpoint
// Notes:
//   * 200 → success path; body is the columnar JSON dataset.
//   * 304 → arrives via OnError (any non-2xx is routed there); treated as "no change".
//   * Per the API contract, the mod must use ?since=<hash>, NOT If-None-Match.
//------------------------------------------------------------------------------------------------
class AG0_TDLApiTerrainStructuresCallback : RestCallback
{
    protected AG0_TDLApiManager m_Manager;

    //------------------------------------------------------------------------------------------------
    void AG0_TDLApiTerrainStructuresCallback(AG0_TDLApiManager manager)
    {
        m_Manager = manager;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }

    //------------------------------------------------------------------------------------------------
    void OnSuccessHandler(RestCallback cb)
    {
        string data = cb.GetData();
        if (m_Manager)
            m_Manager.OnTerrainStructuresPollSuccess(data);
    }

    //------------------------------------------------------------------------------------------------
    void OnErrorHandler(RestCallback cb)
    {
        if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
        {
            if (m_Manager)
                m_Manager.OnTerrainStructuresPollTimeout();
            return;
        }

        int errorCode = cb.GetHttpCode();
        if (m_Manager)
            m_Manager.OnTerrainStructuresPollError(errorCode);
    }
}

//------------------------------------------------------------------------------------------------
// REST Callback for Terrain Roads polling endpoint. Same 200/304/error split
// as the structures callback — 304 arrives via OnError per Reforger's REST stack.
//------------------------------------------------------------------------------------------------
class AG0_TDLApiTerrainRoadsCallback : RestCallback
{
    protected AG0_TDLApiManager m_Manager;

    void AG0_TDLApiTerrainRoadsCallback(AG0_TDLApiManager manager)
    {
        m_Manager = manager;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }

    void OnSuccessHandler(RestCallback cb)
    {
        string data = cb.GetData();
        if (m_Manager)
            m_Manager.OnTerrainRoadsPollSuccess(data);
    }

    void OnErrorHandler(RestCallback cb)
    {
        if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
        {
            if (m_Manager)
                m_Manager.OnTerrainRoadsPollTimeout();
            return;
        }

        int errorCode = cb.GetHttpCode();
        if (m_Manager)
            m_Manager.OnTerrainRoadsPollError(errorCode);
    }
}

//------------------------------------------------------------------------------------------------
// REST Callback for API Key Validation
//------------------------------------------------------------------------------------------------
class AG0_TDLApiValidateCallback : RestCallback
{
    protected AG0_TDLApiManager m_Manager;
    
    //------------------------------------------------------------------------------------------------
    void AG0_TDLApiValidateCallback(AG0_TDLApiManager manager)
    {
        m_Manager = manager;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnSuccessHandler(RestCallback cb)
    {
        string data = cb.GetData();
        if (m_Manager)
            m_Manager.OnApiKeyValidated(true, data);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnErrorHandler(RestCallback cb)
    {
        if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
        {
            if (m_Manager)
                m_Manager.OnApiKeyValidated(false, "");
            return;
        }
        
        int errorCode = cb.GetHttpCode();
        
        // 401 = invalid key, other errors might be network issues
        bool isInvalidKey = (errorCode == 401);
        
        if (isInvalidKey)
            Print("[TDL_API] API key is invalid (401 Unauthorized)", LogLevel.WARNING);
        else
            Print(string.Format("[TDL_API] API key validation failed with error: %1", errorCode), LogLevel.WARNING);
        
        if (m_Manager)
            m_Manager.OnApiKeyValidated(false, "");
    }
}

//------------------------------------------------------------------------------------------------
//! Fetch-completion callback for the image_deliver queue handler. Captures the queue
//! item's parsed fields so that, on photo manager fetch success, we can call into the
//! system's SendImageTDLMessage entry point with the right sender/message-type/recipient.
//!
//! Holds NO back-ref to the manager; settlement happens via AG0_TDLSystem.GetInstance().
//------------------------------------------------------------------------------------------------
class AG0_TDLImageDeliverFetchSink : AG0_TDLPhotoFetchCallback
{
    protected RplId           m_SenderDeviceRplId;
    protected string          m_sCaption;
    protected ETDLMessageType m_eMessageType;
    protected string          m_sDeliveryId;
    protected string          m_sFingerprint;
    protected int             m_iSizeBytes;
    protected RplId           m_RecipientRplId;
    protected string          m_sCorrelationId;

    void AG0_TDLImageDeliverFetchSink(RplId senderDeviceRplId, string caption,
                                      ETDLMessageType messageType, string deliveryId,
                                      string fingerprint, int sizeBytes,
                                      RplId recipientRplId, string correlationId)
    {
        m_SenderDeviceRplId = senderDeviceRplId;
        m_sCaption          = caption;
        m_eMessageType      = messageType;
        m_sDeliveryId       = deliveryId;
        m_sFingerprint      = fingerprint;
        m_iSizeBytes        = sizeBytes;
        m_RecipientRplId    = recipientRplId;
        m_sCorrelationId    = correlationId;
    }

    override void OnPhotoReady(int requestId, AG0_TDLPhotoData photo)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system) return;

        Print(string.Format("[TDL_API] image_deliver corr=%1 fetch ready — sending image-message (deliveryId=%2)",
            m_sCorrelationId, m_sDeliveryId), LogLevel.NORMAL);

        // The photo is sitting in AG0_TDLPhotoManager.m_Cache under m_sDeliveryId by now
        // (OnFetchSuccess populates the cache before BeginDecode fires). SendImageTDLMessage
        // creates the AG0_TDLMessage, propagates metadata to clients, and kicks the chunk
        // sender targeting the resolved recipients.
        int messageId = system.SendImageTDLMessage(m_SenderDeviceRplId, m_sCaption, m_eMessageType,
            m_sDeliveryId, m_sFingerprint, m_iSizeBytes, m_RecipientRplId);

        if (messageId < 0)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 SendImageTDLMessage rejected the message",
                m_sCorrelationId), LogLevel.WARNING);
        }
    }

    override void OnPhotoFailed(int requestId, string reason)
    {
        Print(string.Format("[TDL_API] image_deliver corr=%1 fetch failed: %2 (deliveryId=%3)",
            m_sCorrelationId, reason, m_sDeliveryId), LogLevel.WARNING);

        // Surface the failure to the API so the web UI can flip the compose to rejected.
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (system)
            system.ApiNotifyImageDeliverFailedPublic(m_sCorrelationId, m_sDeliveryId, reason);
    }
}

//------------------------------------------------------------------------------------------------
//! In-flight slot tracker for a single RestContext. The engine's HTTP layer
//! rejects new requests once 64 are outstanding on a context (confirmed by
//! BI 2025-07-24; surfaces in logs as "Failed to queue up request, request
//! limit reached for host ..."). That rejection is silent at the script
//! layer — RestCallback.OnError does NOT fire, so callers think their POST
//! went out when it never made it past the queue check. Each context wraps
//! a gate with a soft cap well below 64; acquire on every call site, release
//! in both success and error handlers.
//!
//! Acquire failure increments a drop counter and Tick() emits one aggregate
//! "saturated" line per ~10s window, so a sustained host-side outage produces
//! a handful of summary lines instead of one per dropped call.
//------------------------------------------------------------------------------------------------
class AG0_TDLOutboundGate
{
	protected string m_sLabel;
	protected int m_iSoftCap;
	protected int m_iInFlight;

	protected int m_iDroppedSinceLog;
	protected float m_fTimeSinceSatLog;
	protected const float SATURATION_LOG_INTERVAL = 10.0;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLOutboundGate(string label, int softCap)
	{
		m_sLabel = label;
		m_iSoftCap = softCap;
		m_iInFlight = 0;
		m_iDroppedSinceLog = 0;
		m_fTimeSinceSatLog = SATURATION_LOG_INTERVAL;
	}

	//------------------------------------------------------------------------------------------------
	//! Reserve a slot. Returns false when the soft cap is hit; caller is
	//! expected to bail without touching the RestContext. Pairs with
	//! Release() on the callback side — both success and error paths must
	//! release or the gate leaks and eventually wedges shut.
	bool TryAcquire()
	{
		if (m_iInFlight >= m_iSoftCap)
		{
			m_iDroppedSinceLog = m_iDroppedSinceLog + 1;
			return false;
		}
		m_iInFlight = m_iInFlight + 1;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	void Release()
	{
		if (m_iInFlight > 0)
			m_iInFlight = m_iInFlight - 1;
	}

	//------------------------------------------------------------------------------------------------
	int GetInFlight() { return m_iInFlight; }

	//------------------------------------------------------------------------------------------------
	//! Wall-clock-independent on purpose — driven by the caller's Update
	//! timeSlice, so a stalled engine simply extends the log window rather
	//! than firing a backlog of "saturated" lines the moment ticking resumes.
	void Tick(float timeSlice)
	{
		m_fTimeSinceSatLog = m_fTimeSinceSatLog + timeSlice;
		if (m_iDroppedSinceLog == 0)
			return;
		if (m_fTimeSinceSatLog < SATURATION_LOG_INTERVAL)
			return;
		Print(string.Format("[TDL_API] %1 outbound saturated — dropped %2 call(s) since last log (in-flight=%3 cap=%4)",
			m_sLabel, m_iDroppedSinceLog, m_iInFlight, m_iSoftCap), LogLevel.WARNING);
		m_iDroppedSinceLog = 0;
		m_fTimeSinceSatLog = 0;
	}
}

//------------------------------------------------------------------------------------------------
//! Per-endpoint circuit breaker. After TRIP_THRESHOLD consecutive failures,
//! refuses to send for an exponentially growing window (1s → 2s → 4s → … →
//! 30s plateau). Sized for the outage shape observed 2026-05-23 where the
//! host's outbound link flapped for ~30s windows and the mod fired hundreds
//! of doomed /submit calls into the engine's 64-slot queue, starving even
//! BI's own lobby heartbeat in the process.
//!
//! Independent of [[AG0_TDLOutboundGate]] — the gate stops "too many in
//! flight right now"; the breaker stops "endpoint is known broken, stop
//! pestering it for a while". A single failure outside an outage is fine;
//! only consecutive failures trip.
//------------------------------------------------------------------------------------------------
class AG0_TDLEndpointBreaker
{
	protected string m_sLabel;
	protected int m_iConsecutiveFailures;
	protected float m_fBackoffRemaining;
	protected float m_fCurrentBackoff;

	protected const int TRIP_THRESHOLD = 5;
	protected const float BACKOFF_BASE = 1.0;
	protected const float BACKOFF_CAP = 30.0;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLEndpointBreaker(string label)
	{
		m_sLabel = label;
		m_iConsecutiveFailures = 0;
		m_fBackoffRemaining = 0;
		m_fCurrentBackoff = 0;
	}

	//------------------------------------------------------------------------------------------------
	bool AllowSend()
	{
		return m_fBackoffRemaining <= 0;
	}

	//------------------------------------------------------------------------------------------------
	void Tick(float timeSlice)
	{
		if (m_fBackoffRemaining <= 0)
			return;
		m_fBackoffRemaining = m_fBackoffRemaining - timeSlice;
		if (m_fBackoffRemaining < 0)
			m_fBackoffRemaining = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! First success after a trip clears the whole breaker state. A handful
	//! of in-flight callbacks finishing successfully during a partial outage
	//! is enough to declare the endpoint healthy again — the breaker is
	//! optimistic by design, so the cost of a wrong "recovered" call is at
	//! most TRIP_THRESHOLD more failures before it re-trips.
	void OnSuccess()
	{
		if (m_iConsecutiveFailures >= TRIP_THRESHOLD)
		{
			Print(string.Format("[TDL_API] %1 recovered after %2 consecutive failure(s)",
				m_sLabel, m_iConsecutiveFailures), LogLevel.NORMAL);
		}
		m_iConsecutiveFailures = 0;
		m_fCurrentBackoff = 0;
		m_fBackoffRemaining = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Anything that didn't round-trip cleanly is a failure: timeout, 5xx,
	//! transport error, all count. 401 is a separate concern handled by the
	//! caller (it kills the api key); from the breaker's perspective the
	//! endpoint just isn't healthy.
	//!
	//! Logging strategy: emit one WARNING when the breaker transitions from
	//! "open" to "tripped" (or doubles its backoff at the end of a retry
	//! window), not on every failure. In-flight callbacks that fail after
	//! the breaker is already in backoff stay silent — they're just the
	//! tail of the burst that tripped us in the first place.
	void OnFailure()
	{
		m_iConsecutiveFailures = m_iConsecutiveFailures + 1;
		if (m_iConsecutiveFailures < TRIP_THRESHOLD)
			return;

		bool wasInBackoff = m_fBackoffRemaining > 0;

		if (m_fCurrentBackoff <= 0)
			m_fCurrentBackoff = BACKOFF_BASE;
		else if (!wasInBackoff)
			m_fCurrentBackoff = m_fCurrentBackoff * 2;

		if (m_fCurrentBackoff > BACKOFF_CAP)
			m_fCurrentBackoff = BACKOFF_CAP;

		m_fBackoffRemaining = m_fCurrentBackoff;

		if (!wasInBackoff)
		{
			Print(string.Format("[TDL_API] %1 backing off %2s (%3 consecutive failures)",
				m_sLabel, m_fCurrentBackoff, m_iConsecutiveFailures), LogLevel.WARNING);
		}
	}
}

//------------------------------------------------------------------------------------------------
// TDL API Manager
// Handles config loading/saving and API communication
// SERVER-SIDE ONLY
//------------------------------------------------------------------------------------------------
class AG0_TDLApiManager
{
    // API Configuration
    protected static const string CONFIG_FOLDER = "$profile:TDL";
    protected static const string CONFIG_FILE = "$profile:TDL/api_config.json";
    protected static const string API_BASE_URL = "https://tdl.blufor.info/api/mod";
    // Terrain endpoints live on their own RestContext so a slow/large terrain
    // GET can't burn slots out from under the chatty /submit + /queue traffic.
    // Each context has its own 64-slot pool — see [[AG0_TDLOutboundGate]].
    protected static const string API_TERRAIN_BASE_URL = "https://tdl.blufor.info/api/mod/terrain";

    // Soft caps below the engine's 64-pending hard ceiling. 24 leaves
    // ample headroom on the hot path for any incidental traffic; 8 is
    // enough for the at-most-two-concurrent terrain GETs plus headroom.
    protected static const int HOT_GATE_SOFT_CAP = 24;
    protected static const int TERRAIN_GATE_SOFT_CAP = 8;

    // State
    protected ref AG0_TDLApiConfigData m_Config;
    protected bool m_bInitialized = false;
    protected bool m_bApiKeyValid = false;
    protected bool m_bValidationPending = false;
    
    // REST Callbacks (must be kept as references)
    protected ref AG0_TDLApiSubmitCallback m_SubmitCallback;
    protected ref AG0_TDLApiQueueCallback m_QueueCallback;
    protected ref AG0_TDLApiValidateCallback m_ValidateCallback;
    
    // Polling state
    protected float m_fTimeSinceLastPoll = 0;
    protected bool m_bPollInProgress = false;
    
    // Shape
    protected ref AG0_TDLApiShapesCallback m_ShapesCallback;
    protected ref AG0_TDLMapShapeManager m_ShapeManager;
    protected bool m_bShapesPollInProgress = false;
    protected int m_iSuccessfulShapePolls = 0;
    protected int m_iFailedShapePolls = 0;

    // Terrain structures (building footprints, streamed from /api/mod/terrain/structures)
    // Populated once after key-validation and on terrain_structures_refresh queue commands.
    // The manager retains the raw JSON so AG0_TDLSystem can forward it to clients verbatim.
    protected ref AG0_TDLApiTerrainStructuresCallback m_TerrainStructuresCallback;
    protected ref AG0_TDLTerrainStructureManager m_TerrainStructureManager;
    protected bool m_bTerrainStructuresPollInProgress = false;
    protected bool m_bTerrainStructuresInitialFetchDone = false;
    protected int m_iSuccessfulTerrainStructuresPolls = 0;
    protected int m_iFailedTerrainStructuresPolls = 0;

    // Terrain roads (road network, streamed from /api/mod/terrain/roads)
    // Same lifecycle as structures: one fetch on key validation + on terrain_roads_refresh.
    protected ref AG0_TDLApiTerrainRoadsCallback m_TerrainRoadsCallback;
    protected ref AG0_TDLTerrainRoadManager m_TerrainRoadManager;
    protected bool m_bTerrainRoadsPollInProgress = false;
    protected bool m_bTerrainRoadsInitialFetchDone = false;
    protected int m_iSuccessfulTerrainRoadsPolls = 0;
    protected int m_iFailedTerrainRoadsPolls = 0;

    // Statistics
    protected int m_iSuccessfulSubmits = 0;
    protected int m_iFailedSubmits = 0;
    protected int m_iSuccessfulPolls = 0;
    protected int m_iFailedPolls = 0;

    // Per-context in-flight gates. One per RestContext base URL because the
    // engine's 64-pending ceiling is enforced per context, not globally.
    protected ref AG0_TDLOutboundGate m_HotGate;
    protected ref AG0_TDLOutboundGate m_TerrainGate;

    // Per-endpoint circuit breakers. Independent so a wedged /shapes endpoint
    // doesn't suppress /submit traffic and vice versa. Shape POST/DELETE
    // share the poll breaker because they fail together on host-level
    // outages (which is the case we care about).
    protected ref AG0_TDLEndpointBreaker m_BreakerSubmit;
    protected ref AG0_TDLEndpointBreaker m_BreakerQueue;
    protected ref AG0_TDLEndpointBreaker m_BreakerShapes;
    protected ref AG0_TDLEndpointBreaker m_BreakerTerrain;

    //------------------------------------------------------------------------------------------------
    void AG0_TDLApiManager()
    {
        m_SubmitCallback = new AG0_TDLApiSubmitCallback(this);
        m_QueueCallback = new AG0_TDLApiQueueCallback(this);
        m_ValidateCallback = new AG0_TDLApiValidateCallback(this);
		m_ShapesCallback = new AG0_TDLApiShapesCallback(this);
		m_ShapeManager = new AG0_TDLMapShapeManager();
		m_TerrainStructuresCallback = new AG0_TDLApiTerrainStructuresCallback(this);
		m_TerrainStructureManager = new AG0_TDLTerrainStructureManager();
		m_TerrainRoadsCallback = new AG0_TDLApiTerrainRoadsCallback(this);
		m_TerrainRoadManager = new AG0_TDLTerrainRoadManager();

		m_HotGate = new AG0_TDLOutboundGate("hot(/api/mod)", HOT_GATE_SOFT_CAP);
		m_TerrainGate = new AG0_TDLOutboundGate("terrain(/api/mod/terrain)", TERRAIN_GATE_SOFT_CAP);

		m_BreakerSubmit = new AG0_TDLEndpointBreaker("submit");
		m_BreakerQueue = new AG0_TDLEndpointBreaker("queue");
		m_BreakerShapes = new AG0_TDLEndpointBreaker("shapes");
		m_BreakerTerrain = new AG0_TDLEndpointBreaker("terrain");
    }
    
    //------------------------------------------------------------------------------------------------
    //! Initialize the API manager - call this from server-side system OnInit
    //! @return true if initialization was successful
    bool Initialize()
    {
        // CRITICAL: Server-only check
        if (!Replication.IsServer())
        {
            Print("[TDL_API] ERROR: AG0_TDLApiManager must only run on the server!", LogLevel.WARNING);
            return false;
        }
        
        Print("[TDL_API] Initializing API Manager...", LogLevel.NORMAL);
        
        // Load or create config
        if (!LoadOrCreateConfig())
        {
            Print("[TDL_API] Failed to load or create config", LogLevel.ERROR);
            return false;
        }
        
        m_bInitialized = true;
        
        // If we have an API key, validate it
        if (m_Config.HasValidApiKey())
        {
            Print("[TDL_API] Found API key, validating...", LogLevel.NORMAL);
            ValidateApiKey();
        }
        else
        {
            Print("[TDL_API] No API key configured. Please add your API key to: " + CONFIG_FILE, LogLevel.WARNING);
        }
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Load config from file or create default if it doesn't exist
    protected bool LoadOrCreateConfig()
    {
        // Ensure the TDL folder exists
        if (!FileIO.FileExists(CONFIG_FOLDER))
        {
            Print(string.Format("[TDL_API] Creating config folder: %1", CONFIG_FOLDER), LogLevel.DEBUG);
            if (!FileIO.MakeDirectory(CONFIG_FOLDER))
            {
                Print("[TDL_API] Failed to create config folder", LogLevel.ERROR);
                return false;
            }
        }
        
        // Check if config file exists
        if (FileIO.FileExists(CONFIG_FILE))
        {
            Print(string.Format("[TDL_API] Loading config from: %1", CONFIG_FILE), LogLevel.DEBUG);
            return LoadConfig();
        }
        else
        {
            Print(string.Format("[TDL_API] Config not found, creating default: %1", CONFIG_FILE), LogLevel.DEBUG);
            return CreateDefaultConfig();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Load configuration from JSON file
    protected bool LoadConfig()
    {
        m_Config = new AG0_TDLApiConfigData();
        
        // Use JsonApiStruct's built-in file loading
        if (!m_Config.LoadFromFile(CONFIG_FILE))
        {
            Print("[TDL_API] Failed to parse config file, creating fresh config", LogLevel.WARNING);
            return CreateDefaultConfig();
        }
		
		// Backfill fields added in newer versions
	    bool needsSave = false;
	    
	    if (m_Config.stateSyncIntervalSeconds <= 0)
	    {
	        m_Config.stateSyncIntervalSeconds = 5;
	        needsSave = true;
	    }
	    
	    if (needsSave)
	    {
	        SaveConfig();
	        Print("[TDL_API] Config updated with new default fields", LogLevel.DEBUG);
	    }
        
        Print(string.Format("[TDL_API] Config loaded - Server: %1, Enabled: %2, Poll Interval: %3s, Has Key: %4",
            m_Config.serverName,
            m_Config.enabled,
            m_Config.pollIntervalSeconds,
            m_Config.HasValidApiKey()), LogLevel.DEBUG);
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Create and save default configuration
    protected bool CreateDefaultConfig()
    {
        m_Config = AG0_TDLApiConfigData.CreateDefault();
        
        // Pack the config data and save to file
        m_Config.PackToFile(CONFIG_FILE);
        
        Print(string.Format("[TDL_API] Default config created at: %1", CONFIG_FILE), LogLevel.NORMAL);
        Print("[TDL_API] Please edit the config file to add your API key from tdl.blufor.info", LogLevel.NORMAL);
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Save current configuration to file
    bool SaveConfig()
    {
        if (!m_Config)
            return false;
        
        m_Config.PackToFile(CONFIG_FILE);
        Print("[TDL_API] Config saved", LogLevel.DEBUG);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Validate the API key with the server
    protected void ValidateApiKey()
    {
        if (!m_Config || !m_Config.HasValidApiKey())
            return;
        
        if (m_bValidationPending)
            return;
        
        m_bValidationPending = true;
        
        // Use the submit endpoint with minimal data to validate the key
        // The server will return 401 if the key is invalid
        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
        {
            Print("[TDL_API] Failed to get REST context for validation", LogLevel.DEBUG);
            m_bValidationPending = false;
            return;
        }
        
        // Set authorization header - format: "key1,value1,key2,value2"
        string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", m_Config.apiKey);
        ctx.SetHeaders(headers);
        
        // Send minimal validation payload
        // POST(callback, request_path, data)
        JsonSaveContext validateJson = new JsonSaveContext();
        validateJson.WriteValue("type", "validate");
        validateJson.WriteValue("serverName", m_Config.serverName);
        validateJson.WriteValue("worldFile", GetGame().GetWorldFile());
        validateJson.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());
        string payload = validateJson.SaveToString();

        ctx.POST(m_ValidateCallback, "/submit", payload);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Called when API key validation completes
    void OnApiKeyValidated(bool valid, string responseData)
    {
        m_bValidationPending = false;
        m_bApiKeyValid = valid;
        
        if (valid)
        {
            Print("[TDL_API] API key is valid - API communication enabled", LogLevel.DEBUG);

            // Kick off the one-shot terrain structures fetch now that we know the key works.
            // After this initial pull, refreshes are driven by terrain_structures_refresh
            // queue commands from the web app — there is no periodic poll for this dataset.
            if (!m_bTerrainStructuresInitialFetchDone)
            {
                m_bTerrainStructuresInitialFetchDone = true;
                PollTerrainStructures();
            }

            // Same one-shot pattern for the road network.
            if (!m_bTerrainRoadsInitialFetchDone)
            {
                m_bTerrainRoadsInitialFetchDone = true;
                PollTerrainRoads();
            }
        }
        else
        {
            Print("[TDL_API] API key validation failed - API communication disabled", LogLevel.DEBUG);
            Print("[TDL_API] Please check your API key in: " + CONFIG_FILE, LogLevel.DEBUG);
        }
    }
	
	float GetStateSyncInterval()
	{
	    if (!m_Config || m_Config.stateSyncIntervalSeconds <= 0)
	        return 5.0;
	    
	    return Math.Clamp(m_Config.stateSyncIntervalSeconds, 1, 60);
	}
    
    //------------------------------------------------------------------------------------------------
    //! Update function - call from system's OnUpdatePoint
    //! @param timeSlice Delta time in seconds
    void Update(float timeSlice)
    {
        if (!m_bInitialized || !m_Config || !m_Config.enabled)
            return;

        // Gates and breakers tick regardless of validation state — the
        // saturation log and backoff windows are driven entirely by
        // timeSlice, and validation timeouts during a startup outage
        // would otherwise hold them frozen until the key validates.
        if (m_HotGate) m_HotGate.Tick(timeSlice);
        if (m_TerrainGate) m_TerrainGate.Tick(timeSlice);
        if (m_BreakerSubmit) m_BreakerSubmit.Tick(timeSlice);
        if (m_BreakerQueue) m_BreakerQueue.Tick(timeSlice);
        if (m_BreakerShapes) m_BreakerShapes.Tick(timeSlice);
        if (m_BreakerTerrain) m_BreakerTerrain.Tick(timeSlice);

        if (!m_bApiKeyValid)
            return;

        // Update poll timer
        m_fTimeSinceLastPoll += timeSlice;

        // Poll for queued commands. Effective interval drops to MIRROR_FAST_POLL_SECONDS
        // whenever at least one identity is actively mirrored on this server — web
        // inputs need to round-trip in under a second to feel live, and the default
        // 5s interval blows the latency budget by itself. Falls back to the configured
        // interval the moment the last mirror session disconnects.
        float effectivePollInterval = m_Config.pollIntervalSeconds;
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (system && system.GetActiveMirrorSessionCount() > 0)
            effectivePollInterval = MIRROR_FAST_POLL_SECONDS;

        if (!m_bPollInProgress && m_fTimeSinceLastPoll >= effectivePollInterval)
        {
            PollQueue();
            m_fTimeSinceLastPoll = 0;
        }
    }

    //! Poll cadence when the web mirror has at least one active subscriber.
    //! 0.5 s gives ~250 ms average inbound latency for web commands without
    //! materially adding to REST traffic — one extra GET per mirrored server
    //! per second vs the 1s baseline. Outbound mirror tick is 3 Hz so the
    //! visible round trip lands around 500 ms total.
    protected const float MIRROR_FAST_POLL_SECONDS = 0.5;
    
    //------------------------------------------------------------------------------------------------
    //! Submit data to the API
    //! @param jsonData JSON string to submit
    //! @return true if request was initiated
    bool SubmitData(string jsonData)
    {
        if (!CanCommunicate())
        {
            Print("[TDL_API] Cannot submit - API not ready", LogLevel.DEBUG);
            return false;
        }

        // Breaker first so a wedged endpoint stops burning gate slots on
        // requests we already know will fail. Gate next so a healthy
        // endpoint still can't overflow the engine's 64-pending queue.
        if (!m_BreakerSubmit.AllowSend())
            return false;
        if (!m_HotGate.TryAcquire())
            return false;

        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
        {
            m_HotGate.Release();
            Print("[TDL_API] Failed to get REST context for submit", LogLevel.DEBUG);
            return false;
        }

        // Set authorization header with Bearer token
        // Format: "key1,value1,key2,value2"
        string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", m_Config.apiKey);
        ctx.SetHeaders(headers);

        // Per-submit Print was drowning the log once the mirror tick ramped up
        // (atak_mirror_sync fires 2 Hz per active mirror). Now suppressed only
        // for atak_mirror_sync; everything else (state_sync, heartbeat, events)
        // continues to log normally so you can verify the mod is talking to
        // the API. Large-payload warning preserved as a separate path.
        bool isMirrorSync = false;
        int mirrorTagIdx = jsonData.IndexOf("\"atak_mirror_sync\"");
        if (mirrorTagIdx > 0 && mirrorTagIdx < 80)
            isMirrorSync = true;

        if (!isMirrorSync)
        {
            Print(string.Format("[TDL_API] Submitting data: %1 bytes", jsonData.Length()), LogLevel.DEBUG);
        }
        if (jsonData.Length() > 50000)
        {
            Print(string.Format("[TDL_API] Submitting data: %1 bytes (large)", jsonData.Length()), LogLevel.WARNING);
        }

        // POST(callback, request_path, data)
        ctx.POST(m_SubmitCallback, "/submit", jsonData);

        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Poll the queue for pending commands
    protected void PollQueue()
    {
        if (!CanCommunicate())
            return;

        if (m_bPollInProgress)
            return;

        if (!m_BreakerQueue.AllowSend())
            return;
        if (!m_HotGate.TryAcquire())
            return;

        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
        {
            m_HotGate.Release();
            Print("[TDL_API] Failed to get REST context for queue poll", LogLevel.ERROR);
            return;
        }

        // Set authorization header
        // Format: "key1,value1"
        string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
        ctx.SetHeaders(headers);

        m_bPollInProgress = true;

        // GET(callback, request_path)
        ctx.GET(m_QueueCallback, "/queue");
    }
    
    //------------------------------------------------------------------------------------------------
    //! Check if we can communicate with the API
    bool CanCommunicate()
    {
        return m_bInitialized && m_bApiKeyValid && m_Config && m_Config.enabled && m_Config.HasValidApiKey();
    }

    //------------------------------------------------------------------------------------------------
    //! Returns the configured Bearer api key, or empty if uninitialized / missing.
    //! Exposed so server-only siblings (e.g. AG0_TDLPhotoManager) can construct
    //! authenticated REST requests against endpoints outside the /api/mod path —
    //! e.g. /api/image/{deliveryId} which sits under /api, not /api/mod.
    string GetApiKey()
    {
        if (!m_Config)
            return "";
        return m_Config.apiKey;
    }
    
    //------------------------------------------------------------------------------------------------
    // Callback handlers
    //------------------------------------------------------------------------------------------------
    
    void OnSubmitSuccess(string data)
    {
        m_HotGate.Release();
        m_BreakerSubmit.OnSuccess();
        m_iSuccessfulSubmits++;

        // Parse response if needed
        if (!data.IsEmpty())
        {
            // Process any response data from the server
            ProcessSubmitResponse(data);
        }
    }

    void OnSubmitError(int errorCode)
    {
        m_HotGate.Release();
        m_BreakerSubmit.OnFailure();
        m_iFailedSubmits++;

        // Handle specific error codes
        if (errorCode == 401)
        {
            Print("[TDL_API] Submit returned 401 - API key may have been revoked", LogLevel.WARNING);
            m_bApiKeyValid = false;
        }
    }

    void OnSubmitTimeout()
    {
        m_HotGate.Release();
        m_BreakerSubmit.OnFailure();
        m_iFailedSubmits++;
    }

    void OnQueuePollSuccess(string data)
    {
        m_HotGate.Release();
        m_BreakerQueue.OnSuccess();
        m_bPollInProgress = false;
        m_iSuccessfulPolls++;

        // Process queued commands
        if (!data.IsEmpty())
        {
            ProcessQueuedCommands(data);
        }
    }

    void OnQueuePollError(int errorCode)
    {
        m_HotGate.Release();
        m_BreakerQueue.OnFailure();
        m_bPollInProgress = false;
        m_iFailedPolls++;

        if (errorCode == 401)
        {
            Print("[TDL_API] Queue poll returned 401 - API key may have been revoked", LogLevel.WARNING);
            m_bApiKeyValid = false;
        }
    }

    void OnQueuePollTimeout()
    {
        m_HotGate.Release();
        m_BreakerQueue.OnFailure();
        m_bPollInProgress = false;
        m_iFailedPolls++;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Process response from submit endpoint
    protected void ProcessSubmitResponse(string data)
    {
        // Parse JSON response
        JsonLoadContext json = new JsonLoadContext();
        if (!json.LoadFromString(data))
        {
            Print("[TDL_API] Failed to parse submit response", LogLevel.WARNING);
            return;
        }
        
        bool success;
        if (json.ReadValue("success", success))
        {
        }

        // Handle any additional response fields here
        // e.g., server might return commands or configuration updates
    }
    
    //------------------------------------------------------------------------------------------------
    //! Process commands received from queue endpoint
    protected void ProcessQueuedCommands(string data)
    {
        // Parse JSON response
        JsonLoadContext json = new JsonLoadContext();
        if (!json.LoadFromString(data))
        {
            Print("[TDL_API] Failed to parse queue response", LogLevel.WARNING);
            return;
        }
        
        // Check for commands array
        array<string> commands = {};
        if (json.ReadValue("commands", commands))
        {
            if (commands.Count() > 0)
            {
                Print(string.Format("[TDL_API] Received %1 queued commands", commands.Count()), LogLevel.DEBUG);
                
                foreach (string command : commands)
                {
                    ExecuteQueuedCommand(command);
                }
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Execute a single queued command from the API
    protected void ExecuteQueuedCommand(string commandJson)
    {
        Print(string.Format("[TDL_API] Processing command: %1", commandJson), LogLevel.DEBUG);
        
        // Parse the command
        JsonLoadContext cmdJson = new JsonLoadContext();
        if (!cmdJson.LoadFromString(commandJson))
        {
            Print("[TDL_API] Failed to parse command JSON", LogLevel.DEBUG);
            return;
        }
        
        string cmdType;
        if (!cmdJson.ReadValue("type", cmdType))
        {
            Print("[TDL_API] Command missing 'type' field", LogLevel.DEBUG);
            return;
        }
        
        // Route command to appropriate handler
        // Extend this switch statement for new command types
        switch (cmdType)
        {
            case "broadcast":
                HandleBroadcastCommand(cmdJson);
                break;
                
            case "config_update":
                HandleConfigUpdateCommand(cmdJson);
                break;
                
			case "marker_delete":
                HandleMarkerDeleteCommand(cmdJson);
                break;
                
            case "marker_edit":
                HandleMarkerEditCommand(cmdJson);
                break;
			
			case "marker_add":
			    HandleMarkerAddCommand(cmdJson);
			    break;
			
			case "shapes_refresh":
                HandleShapesRefreshCommand();
                break;

			case "terrain_structures_refresh":
                HandleTerrainStructuresRefreshCommand();
                break;

			case "terrain_roads_refresh":
                HandleTerrainRoadsRefreshCommand();
                break;

            case "message_send":
                HandleMessageSendCommand(cmdJson);
                break;

            case "message_mark_read":
                HandleMessageMarkReadCommand(cmdJson);
                break;

            case "image_deliver":
                HandleImageDeliverCommand(cmdJson);
                break;

            case "mirror_subscribe":
                HandleMirrorSubscribeCommand(cmdJson);
                break;

            case "mirror_unsubscribe":
                HandleMirrorUnsubscribeCommand(cmdJson);
                break;

            case "mirror_set_panel":
            case "mirror_set_chat_contact":
            case "mirror_set_brightness":
            case "mirror_set_map_view":
            case "mirror_toggle_bloodhound":
            case "mirror_set_callsign":
            case "mirror_toggle_camera_broadcast":
                HandleMirrorCommandDispatch(cmdJson, commandJson);
                break;

            default:
                Print(string.Format("[TDL_API] Unknown command type: %1", cmdType), LogLevel.WARNING);
                break;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Mirror subscribe/unsubscribe — register / deregister an identity as
    //! actively mirrored on this server. The web layer enqueues these when
    //! its SSE channel opens / closes.
    //!
    //! Payload shape (both):
    //!   { "type": "mirror_subscribe" | "mirror_unsubscribe", "identityId": "<uuid>" }
    //! OR (fallback for API revisions that renamed the field):
    //!   { ..., "playerIdentityId": "<uuid>" }
    //! Accepting both names lets the API rename freely without bricking the
    //! mod every time the contract shifts during this iteration.
    protected void HandleMirrorSubscribeCommand(JsonLoadContext cmdJson)
    {
        string identityId = ReadIdentityIdField(cmdJson);
        if (identityId.IsEmpty())
        {
            Print("[TDL_API] mirror_subscribe missing identityId / playerIdentityId", LogLevel.WARNING);
            return;
        }
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        system.OnMirrorSubscribe(identityId);
    }

    protected void HandleMirrorUnsubscribeCommand(JsonLoadContext cmdJson)
    {
        string identityId = ReadIdentityIdField(cmdJson);
        if (identityId.IsEmpty())
        {
            Print("[TDL_API] mirror_unsubscribe missing identityId / playerIdentityId", LogLevel.WARNING);
            return;
        }
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        system.OnMirrorUnsubscribe(identityId);
    }

    //! Read the identity field from a mirror command payload. Accepts both
    //! `identityId` (original brief contract) and `playerIdentityId` (Cursor's
    //! later rename). Whichever fires, returns the trimmed UUID string.
    protected string ReadIdentityIdField(JsonLoadContext cmdJson)
    {
        string identityId;
        if (cmdJson.ReadValue("identityId", identityId) && !identityId.IsEmpty())
            return identityId;
        if (cmdJson.ReadValue("playerIdentityId", identityId) && !identityId.IsEmpty())
            return identityId;
        return string.Empty;
    }

    //------------------------------------------------------------------------------------------------
    //! All actionable mirror_* commands share the same plumbing: identity ->
    //! online player -> owner RPC carrying the original JSON. The client-side
    //! dispatcher (AG0_TDLMirrorCommandDispatcher.Dispatch) parses the JSON
    //! and routes by type. Keeping the API-side fan-out as one handler avoids
    //! duplicating identity resolution per command.
    //!
    //! Payload shape (all share these fields, plus per-type extras):
    //!   { "type": "mirror_set_<x>", "identityId": "<uuid>", ...command fields... }
    protected void HandleMirrorCommandDispatch(JsonLoadContext cmdJson, string rawJson)
    {
        string identityId = ReadIdentityIdField(cmdJson);
        if (identityId.IsEmpty())
        {
            Print("[TDL_API] mirror command missing identityId / playerIdentityId", LogLevel.WARNING);
            return;
        }
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        // The RPC on the owning client takes the original JSON unchanged so
        // type-specific fields don't need to be re-serialised here.
        bool delivered = system.DispatchMirrorCommandToIdentity(identityId, rawJson);
        if (!delivered)
        {
            Print(string.Format("[TDL_API] mirror command for identity %1 dropped — player offline or no controller",
                identityId), LogLevel.DEBUG);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Handle broadcast command from API
    protected void HandleBroadcastCommand(JsonLoadContext cmdJson)
    {
        string message;
        if (cmdJson.ReadValue("message", message))
        {
            Print(string.Format("[TDL_API] Broadcast: %1", message), LogLevel.DEBUG);
            // TODO: Route to TDL system for in-game broadcast
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Handle message_send command from web API.
    //!
    //! The web user composed a message in their inbox UI. We refuse to short-circuit hop
    //! logic: instead, we look up their currently-online in-game player, find that player's
    //! device on the named network, and call the EXISTING SendTDLMessage entry point. From
    //! there the message goes through the same propagation/relay path as an in-game compose,
    //! meaning hop graph traversal, MarkDeliveredTo, RPC fan-out, and read-receipts all
    //! work identically. This is also why the resulting message_sent event automatically
    //! mirrors back to the API — there's only ever one canonical send path.
    //!
    //! Failure modes (sender not linked / not online / no device on this network / target
    //! not on network) all surface as message_send_failed events with a correlationId so
    //! the web UI can mark the compose attempt as rejected and tell the user why.
    //!
    //! Payload shape (from API):
    //!   {
    //!     "type": "message_send",
    //!     "correlationId": "msgsend_<uuid>",     // echoed back on success/fail
    //!     "senderIdentityId": "<uuid>",          // persistent identity of the web user
    //!     "networkId": <int>,                    // target TDL network
    //!     "messageType": "broadcast" | "direct",
    //!     "content": "<string>",                 // <= 8000 chars (RPC param ceiling)
    //!     "recipientRplId": <int>                // direct only; mod resolves to network member
    //!   }
    protected void HandleMessageSendCommand(JsonLoadContext cmdJson)
    {
        string correlationId;
        if (!cmdJson.ReadValue("correlationId", correlationId))
            correlationId = "";

        string senderIdentityId;
        if (!cmdJson.ReadValue("senderIdentityId", senderIdentityId) || senderIdentityId.IsEmpty())
        {
            Print("[TDL_API] message_send missing 'senderIdentityId'", LogLevel.WARNING);
            return;
        }

        // Network identification: stableId is the preferred lookup key (restart-proof),
        // numeric networkId is fallback for queue commands enqueued before the API
        // shipped stableId. We also pass both into failure events so the API can match
        // either way.
        string networkStableId;
        cmdJson.ReadValue("networkStableId", networkStableId);

        int networkId = -1;
        cmdJson.ReadValue("networkId", networkId);

        if (networkStableId.IsEmpty() && networkId < 0)
        {
            Print("[TDL_API] message_send missing both 'networkStableId' and 'networkId'", LogLevel.WARNING);
            return;
        }

        string messageTypeStr;
        if (!cmdJson.ReadValue("messageType", messageTypeStr))
        {
            Print("[TDL_API] message_send missing 'messageType'", LogLevel.WARNING);
            return;
        }

        string content;
        if (!cmdJson.ReadValue("content", content) || content.IsEmpty())
        {
            Print("[TDL_API] message_send missing 'content'", LogLevel.WARNING);
            return;
        }

        AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
        if (!tdlSystem)
        {
            Print("[TDL_API] message_send: TDL system unavailable", LogLevel.WARNING);
            return;
        }

        // Resolve the network. stableId wins when available because it's the only id
        // that survives a restart — a numeric networkId from a queue row enqueued
        // before the dedicated server restart is meaningless after.
        AG0_TDLNetwork network = null;
        if (!networkStableId.IsEmpty())
            network = tdlSystem.GetNetworkByStableId(networkStableId);
        if (!network && networkId >= 0)
            network = tdlSystem.GetNetworkById(networkId);

        if (!network)
        {
            Print(string.Format("[TDL_API] message_send: network not found (stableId=%1, numericId=%2). Likely the server restarted between enqueue and processing.",
                networkStableId, networkId), LogLevel.DEBUG);
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "network_not_found", networkId, networkStableId);
            return;
        }

        // From here on, use the resolved network's live id/stableId (what we send back
        // to the API) instead of whatever the queue row carried.
        int liveNetworkId = network.GetNetworkID();
        string liveNetworkStableId = network.GetStableId();

        // Resolve web user → live session player. Empty senderPlayerId == not currently
        // online (lobby, mid-respawn, disconnected). We could persist the queue command
        // and replay on connect, but that introduces ordering bugs vs. in-game composes.
        // Rejecting now and letting the API surface the failure is the right shape.
        int senderPlayerId = tdlSystem.GetPlayerIdFromIdentityId(senderIdentityId);
        if (senderPlayerId <= 0)
        {
            Print(string.Format("[TDL_API] message_send: sender %1 not online", senderIdentityId), LogLevel.DEBUG);
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "player_offline", liveNetworkId, liveNetworkStableId);
            return;
        }

        // Resolve player → device on the resolved network. This is the hop logic's
        // entry gate — without a device on the network, the relay graph has nowhere
        // to start, so the compose can't proceed.
        AG0_TDLDeviceComponent senderDevice = tdlSystem.GetDeviceInNetworkForPlayer(senderPlayerId, liveNetworkId);
        if (!senderDevice)
        {
            Print(string.Format("[TDL_API] message_send: player %1 has no device on network %2",
                senderPlayerId, liveNetworkId), LogLevel.DEBUG);
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "no_device_in_network", liveNetworkId, liveNetworkStableId);
            return;
        }

        RplId senderDeviceRplId = senderDevice.GetDeviceRplId();
        if (senderDeviceRplId == RplId.Invalid())
        {
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "device_not_replicated", liveNetworkId, liveNetworkStableId);
            return;
        }

        // Trim payload at the per-string-param RPC cap. The API enforces this on input
        // already (see implementation guide), but defense-in-depth — silent truncation
        // by the engine would corrupt the message mid-relay. Checked before the type
        // branch since both broadcast fan-out and direct path emit the same content.
        const int MAX_CONTENT_BYTES = 8000;
        if (content.Length() > MAX_CONTENT_BYTES)
        {
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "content_too_long", liveNetworkId, liveNetworkStableId);
            return;
        }

        if (messageTypeStr == "broadcast")
        {
            // Web-originated broadcast → mod-side fan-out into per-recipient DIRECT
            // messages, one per device on the resolved network (skipping the sender's
            // own device). Each direct flows through SendTDLMessage normally:
            // AddDirectToNetwork → PropagateMessagesInNetwork → recipient's
            // RpcDo_ReceiveTDLMessages. The result is each player seeing the message
            // in their direct conversation with the web sender, including notification
            // badges via the contact-card unread count.
            //
            // Multi-device players will receive duplicates if they happen to have more
            // than one device on the same network — accepted edge case (rare in
            // practice; dedupe by senderPlayerId is a future refinement).
            //
            // The API will receive N message_sent events (one per fanned-out direct).
            // Web inbox can either show N sent rows or aggregate by (sender, content,
            // timestamp window) — see message-handling docs for the tradeoff.
            array<AG0_TDLDeviceComponent> members = network.GetNetworkDevices();
            if (!members || members.Count() == 0)
            {
                tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "no_recipients", liveNetworkId, liveNetworkStableId);
                return;
            }

            int fanCount = 0;
            foreach (AG0_TDLDeviceComponent recipientDevice : members)
            {
                if (!recipientDevice)
                    continue;
                RplId recipientRpl = recipientDevice.GetDeviceRplId();
                if (recipientRpl == RplId.Invalid())
                    continue;
                if (recipientRpl == senderDeviceRplId)
                    continue;  // skip self-delivery — sender doesn't message themselves

                tdlSystem.SendTDLMessage(senderDeviceRplId, content, ETDLMessageType.DIRECT, recipientRpl);
                fanCount = fanCount + 1;
            }

            Print(string.Format("[TDL_API] message_send corr=%1 broadcast → fanned to %2 directs on network %3 [%4]",
                correlationId, fanCount, liveNetworkId, liveNetworkStableId), LogLevel.DEBUG);

            if (fanCount == 0)
                tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "no_recipients", liveNetworkId, liveNetworkStableId);

            return;
        }

        if (messageTypeStr != "direct")
        {
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "invalid_message_type", liveNetworkId, liveNetworkStableId);
            return;
        }

        // Direct case: single recipient required.
        RplId recipientRplId = RplId.Invalid();
        int recipientRplIdInt;
        if (!cmdJson.ReadValue("recipientRplId", recipientRplIdInt))
        {
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "missing_recipient", liveNetworkId, liveNetworkStableId);
            return;
        }
        recipientRplId = recipientRplIdInt;

        Print(string.Format("[TDL_API] message_send: routing web compose from %1 (player %2) on network %3 [%4] (direct)",
            senderIdentityId, senderPlayerId, liveNetworkId, liveNetworkStableId), LogLevel.DEBUG);

        // Single canonical send path — reuses hop graph, replication, RPC delivery,
        // pruning, and the existing ApiNotifyMessageSent fan-out. No bypass.
        tdlSystem.SendTDLMessage(senderDeviceRplId, content, ETDLMessageType.DIRECT, recipientRplId);
    }

    //------------------------------------------------------------------------------------------------
    //! Handle image_deliver command from API.
    //!
    //! Two-phase orchestration: (1) fetch the pre-rendered image from the API by deliveryId,
    //! (2) on fetch success the AG0_TDLImageDeliverFetchSink callback fires SendImageTDLMessage,
    //! which creates the AG0_TDLMessage with image fields populated, propagates metadata to
    //! recipient clients via the existing message-replication path, and kicks chunked
    //! distribution of the image bytes via AG0_TDLPhotoManager's chunk sender.
    //!
    //! The mod never originates an image-message in phase 2 — only the API can. This handler
    //! is the only entry point.
    //!
    //! Payload shape (from API):
    //!   {
    //!     "type": "image_deliver",
    //!     "correlationId": "imgdel_<uuid>",
    //!     "deliveryId":    "<string>",          // unique per delivery; cache key
    //!     "fetchUrl":      "https://...",       // server hits this to get the rgz JSON
    //!     "fingerprint":   "<string>",          // FNV / SHA-ish identifier for blocklist lookups
    //!     "sizeBytes":     <int>,               // expected payload size after fetch
    //!     "networkId":     <int>,               // numeric, may be stale across restarts
    //!     "networkStableId": "<string>",        // preferred lookup key
    //!     "messageType":   "broadcast" | "direct",
    //!     "senderIdentityId": "<uuid>",         // web user identity → online player → device
    //!     "caption":       "<optional string>", // shown alongside the image (may be empty)
    //!     "recipientRplId": <int>               // direct only
    //!   }
    protected void HandleImageDeliverCommand(JsonLoadContext cmdJson)
    {
        string correlationId;
        if (!cmdJson.ReadValue("correlationId", correlationId))
            correlationId = "";

        string deliveryId;
        if (!cmdJson.ReadValue("deliveryId", deliveryId) || deliveryId.IsEmpty())
        {
            Print("[TDL_API] image_deliver missing 'deliveryId'", LogLevel.WARNING);
            return;
        }

        string fetchUrl;
        if (!cmdJson.ReadValue("fetchUrl", fetchUrl) || fetchUrl.IsEmpty())
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 missing 'fetchUrl'", correlationId), LogLevel.WARNING);
            AG0_TDLSystem failSys = AG0_TDLSystem.GetInstance();
            if (failSys)
                failSys.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "missing_fetch_url");
            return;
        }

        string fingerprint;
        cmdJson.ReadValue("fingerprint", fingerprint);  // optional — empty is OK

        int sizeBytes = 0;
        cmdJson.ReadValue("sizeBytes", sizeBytes);  // optional hint, used for cache sizing

        // Network resolution — same dual-id pattern as message_send.
        string networkStableId;
        cmdJson.ReadValue("networkStableId", networkStableId);

        int networkIdNumeric = -1;
        cmdJson.ReadValue("networkId", networkIdNumeric);

        if (networkStableId.IsEmpty() && networkIdNumeric < 0)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 missing both 'networkStableId' and 'networkId'",
                correlationId), LogLevel.WARNING);
            return;
        }

        string senderIdentityId;
        if (!cmdJson.ReadValue("senderIdentityId", senderIdentityId) || senderIdentityId.IsEmpty())
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 missing 'senderIdentityId'", correlationId), LogLevel.WARNING);
            return;
        }

        string messageTypeStr;
        if (!cmdJson.ReadValue("messageType", messageTypeStr))
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 missing 'messageType'", correlationId), LogLevel.WARNING);
            return;
        }

        string caption;
        cmdJson.ReadValue("caption", caption);  // optional

        AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
        if (!tdlSystem)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 TDL system unavailable", correlationId), LogLevel.WARNING);
            return;
        }

        AG0_TDLPhotoManager photoMgr = tdlSystem.GetPhotoManager();
        if (!photoMgr)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 photo manager unavailable", correlationId), LogLevel.WARNING);
            return;
        }

        // Resolve network (stable id wins).
        AG0_TDLNetwork network = null;
        if (!networkStableId.IsEmpty())
            network = tdlSystem.GetNetworkByStableId(networkStableId);
        if (!network && networkIdNumeric >= 0)
            network = tdlSystem.GetNetworkById(networkIdNumeric);

        if (!network)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 network not found (stableId=%2 numericId=%3)",
                correlationId, networkStableId, networkIdNumeric), LogLevel.DEBUG);
            tdlSystem.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "network_not_found");
            return;
        }

        int liveNetworkId = network.GetNetworkID();

        // Resolve sender identity → online player → device on this network.
        int senderPlayerId = tdlSystem.GetPlayerIdFromIdentityId(senderIdentityId);
        if (senderPlayerId <= 0)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 sender %2 not online",
                correlationId, senderIdentityId), LogLevel.DEBUG);
            tdlSystem.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "sender_offline");
            return;
        }

        AG0_TDLDeviceComponent senderDevice = tdlSystem.GetDeviceInNetworkForPlayer(senderPlayerId, liveNetworkId);
        if (!senderDevice)
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 player %2 has no device on network %3",
                correlationId, senderPlayerId, liveNetworkId), LogLevel.DEBUG);
            tdlSystem.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "no_device_in_network");
            return;
        }

        RplId senderDeviceRplId = senderDevice.GetDeviceRplId();
        if (senderDeviceRplId == RplId.Invalid())
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 sender device not replicated", correlationId), LogLevel.DEBUG);
            tdlSystem.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "device_not_replicated");
            return;
        }

        // Decode messageType.
        ETDLMessageType messageType;
        RplId recipientRplId = RplId.Invalid();
        if (messageTypeStr == "broadcast")
        {
            messageType = ETDLMessageType.NETWORK_BROADCAST;
        }
        else if (messageTypeStr == "direct")
        {
            messageType = ETDLMessageType.DIRECT;
            int recipientRplIdInt;
            if (!cmdJson.ReadValue("recipientRplId", recipientRplIdInt))
            {
                Print(string.Format("[TDL_API] image_deliver corr=%1 direct without recipientRplId", correlationId), LogLevel.WARNING);
                tdlSystem.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "missing_recipient");
                return;
            }
            recipientRplId = recipientRplIdInt;
        }
        else
        {
            Print(string.Format("[TDL_API] image_deliver corr=%1 invalid messageType: %2",
                correlationId, messageTypeStr), LogLevel.WARNING);
            tdlSystem.ApiNotifyImageDeliverFailedPublic(correlationId, deliveryId, "invalid_message_type");
            return;
        }

        // Fire the fetch. The sink captures the parsed state and on success calls
        // SendImageTDLMessage which orchestrates message creation + chunk distribution.
        AG0_TDLImageDeliverFetchSink sink = new AG0_TDLImageDeliverFetchSink(
            senderDeviceRplId, caption, messageType, deliveryId, fingerprint, sizeBytes,
            recipientRplId, correlationId);

        Print(string.Format("[TDL_API] image_deliver corr=%1 fetching (deliveryId=%2 url=%3)",
            correlationId, deliveryId, fetchUrl), LogLevel.NORMAL);

        photoMgr.FetchByDeliveryId(deliveryId, fetchUrl, fingerprint, sizeBytes, sink);
    }

    //------------------------------------------------------------------------------------------------
    //! Handle message_mark_read command from web API.
    //!
    //! Web user opened a conversation; we mirror that to the in-game read state through
    //! the same MarkTDLMessageRead path that an in-game device would use. Read-receipt
    //! RPCs to the original sender are emitted by the existing logic — the web user
    //! showing up in the inbox triggers the same downstream signal as opening it on a CDU.
    //!
    //! Payload shape:
    //!   {
    //!     "type": "message_mark_read",
    //!     "readerIdentityId": "<uuid>",
    //!     "networkId": <int>,
    //!     "messageId": <int>
    //!   }
    protected void HandleMessageMarkReadCommand(JsonLoadContext cmdJson)
    {
        string readerIdentityId;
        if (!cmdJson.ReadValue("readerIdentityId", readerIdentityId) || readerIdentityId.IsEmpty())
        {
            Print("[TDL_API] message_mark_read missing 'readerIdentityId'", LogLevel.WARNING);
            return;
        }

        // Same dual-id pattern as message_send — stableId preferred, networkId fallback.
        // Read-mark has no failure event back to the API (no correlationId concept),
        // so we just silently no-op on lookup miss; the API will see the in-game
        // message_read event (or not) as the source of truth either way.
        string networkStableId;
        cmdJson.ReadValue("networkStableId", networkStableId);

        int networkId = -1;
        cmdJson.ReadValue("networkId", networkId);

        if (networkStableId.IsEmpty() && networkId < 0)
        {
            Print("[TDL_API] message_mark_read missing both 'networkStableId' and 'networkId'", LogLevel.WARNING);
            return;
        }

        int messageId;
        if (!cmdJson.ReadValue("messageId", messageId))
        {
            Print("[TDL_API] message_mark_read missing 'messageId'", LogLevel.WARNING);
            return;
        }

        AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
        if (!tdlSystem) return;

        AG0_TDLNetwork network = null;
        if (!networkStableId.IsEmpty())
            network = tdlSystem.GetNetworkByStableId(networkStableId);
        if (!network && networkId >= 0)
            network = tdlSystem.GetNetworkById(networkId);
        if (!network)
        {
            // Network gone — likely server restarted between enqueue and processing.
            // No event to fire, just drop. The web inbox's READ state for this row
            // will stay DELIVERED until reconciliation via state_sync.
            return;
        }

        int readerPlayerId = tdlSystem.GetPlayerIdFromIdentityId(readerIdentityId);
        if (readerPlayerId <= 0)
        {
            // Reader offline. Read-state will reconcile next time they connect via
            // state_sync — we don't queue here for the same reasons as message_send.
            return;
        }

        AG0_TDLDeviceComponent readerDevice = tdlSystem.GetDeviceInNetworkForPlayer(readerPlayerId, network.GetNetworkID());
        if (!readerDevice) return;

        RplId readerDeviceRplId = readerDevice.GetDeviceRplId();
        if (readerDeviceRplId == RplId.Invalid()) return;

        // Routes through MarkTDLMessageRead which (a) flips the in-game read bit,
        // (b) fires the in-game read-receipt RPC to the sender, (c) emits the
        // message_read API event. All in one path.
        tdlSystem.MarkTDLMessageRead(readerDeviceRplId, messageId);
    }

    //------------------------------------------------------------------------------------------------
    //! Handle config update command from API
    protected void HandleConfigUpdateCommand(JsonLoadContext cmdJson)
    {
        // Remote config updates (optional feature)
        int newSyncInterval;
		if (cmdJson.ReadValue("stateSyncIntervalSeconds", newSyncInterval))
		{
		    if (newSyncInterval >= 1 && newSyncInterval <= 60)
		    {
		        m_Config.stateSyncIntervalSeconds = newSyncInterval;
		        SaveConfig();
		    }
		}
    }
	
	//------------------------------------------------------------------------------------------------
    //! Handle marker delete command from web API
    protected void HandleMarkerDeleteCommand(JsonLoadContext cmdJson)
    {
        int markerId;
        if (!cmdJson.ReadValue("markerId", markerId))
        {
            Print("[TDL_API] marker_delete missing 'markerId'", LogLevel.WARNING);
            return;
        }
        
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
        {
            Print("[TDL_API] marker_delete: Marker manager not available", LogLevel.WARNING);
            return;
        }
        
        SCR_MapMarkerBase marker = markerMgr.GetStaticMarkerByID(markerId);
        if (!marker)
        {
            Print(string.Format("[TDL_API] marker_delete: Marker %1 not found", markerId), LogLevel.DEBUG);
            return;
        }
        
        if (!marker.IsTDLMarker())
        {
            Print(string.Format("[TDL_API] marker_delete: Marker %1 is not a TDL marker", markerId), LogLevel.WARNING);
            return;
        }
        
        markerMgr.OnRemoveSynchedMarker(markerId);
        markerMgr.OnAskRemoveStaticMarker(markerId);
        
        Print(string.Format("[TDL_API] marker_delete: Removed marker %1", markerId), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Handle marker edit command from web API
    //! Uses delete-and-recreate to broadcast changes — static markers have no update RPC
    //! Preserves original player ownership through the recreate
    protected void HandleMarkerEditCommand(JsonLoadContext cmdJson)
    {
        int markerId;
        if (!cmdJson.ReadValue("markerId", markerId))
        {
            Print("[TDL_API] marker_edit missing 'markerId'", LogLevel.WARNING);
            return;
        }
        
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
        {
            Print("[TDL_API] marker_edit: Marker manager not available", LogLevel.WARNING);
            return;
        }
        
        SCR_MapMarkerBase oldMarker = markerMgr.GetStaticMarkerByID(markerId);
        if (!oldMarker)
        {
            Print(string.Format("[TDL_API] marker_edit: Marker %1 not found", markerId), LogLevel.DEBUG);
            return;
        }
        
        if (!oldMarker.IsTDLMarker())
        {
            Print(string.Format("[TDL_API] marker_edit: Marker %1 is not a TDL marker", markerId), LogLevel.WARNING);
            return;
        }
        
        // Snapshot all fields from old marker
        int pos[2];
        oldMarker.GetWorldPos(pos);
        int savedType = oldMarker.GetType();
        int savedIconEntry = oldMarker.GetIconEntry();
        int savedColorEntry = oldMarker.GetColorEntry();
        string savedCustomText = oldMarker.GetCustomText();
        int savedOwnerID = oldMarker.GetMarkerOwnerID();
        int savedFlags = oldMarker.GetFlags();
        int savedConfigID = oldMarker.GetMarkerConfigID();
        int savedFactionFlags = oldMarker.GetMarkerFactionFlags();
        int savedRotation = oldMarker.GetRotation();
        
        // Override only fields present in the command payload
        string newCustomText;
        if (cmdJson.ReadValue("customText", newCustomText))
            savedCustomText = newCustomText;
        
        int newColorIndex;
        if (cmdJson.ReadValue("colorIndex", newColorIndex))
            savedColorEntry = newColorIndex;
		
		// Override position if both coordinates provided
        float newPosX, newPosZ;
        if (cmdJson.ReadValue("posX", newPosX) && cmdJson.ReadValue("posZ", newPosZ))
        {
            pos[0] = (int)newPosX;
            pos[1] = (int)newPosZ;
        }
        
        // Delete old marker — server-side removal + broadcast to clients
        markerMgr.OnRemoveSynchedMarker(markerId);
        markerMgr.OnAskRemoveStaticMarker(markerId);
        
        // Recreate with edited fields
        SCR_MapMarkerBase newMarker = new SCR_MapMarkerBase();
        newMarker.SetType(savedType);
        newMarker.SetWorldPos(pos[0], pos[1]);
        newMarker.SetIconEntry(savedIconEntry);
        newMarker.SetColorEntry(savedColorEntry);
        newMarker.SetCustomText(savedCustomText);
        newMarker.SetFlags(savedFlags);
        newMarker.SetMarkerConfigID(savedConfigID);
        newMarker.SetMarkerFactionFlags(savedFactionFlags);
        newMarker.SetRotation(savedRotation);
        
        // Assign new UID, preserve original owner, broadcast to clients
        markerMgr.AssignMarkerUID(newMarker);
        newMarker.SetMarkerOwnerID(savedOwnerID);
        markerMgr.OnAddSynchedMarker(newMarker);
        markerMgr.OnAskAddStaticMarker(newMarker);
        
        Print(string.Format("[TDL_API] marker_edit: Replaced marker %1 -> %2",
            markerId, newMarker.GetMarkerID()), LogLevel.DEBUG);
    }
	
	//------------------------------------------------------------------------------------------------
	//! Resolve a TDL icon quad name (e.g. "tdl_pin") to its icon entry index
	//! Returns -1 if not found
	protected int ResolveIconEntryFromQuad(string targetQuad)
	{
	    SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
	    if (!markerMgr || !markerMgr.GetMarkerConfig())
	        return -1;
	    
	    SCR_MapMarkerEntryPlaced placedEntry = SCR_MapMarkerEntryPlaced.Cast(
	        markerMgr.GetMarkerConfig().GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_CUSTOM));
	    
	    if (!placedEntry)
	        return -1;
	    
	    // Iterate icon entries until GetIconEntry returns false (end of list)
	    ResourceName imageset, imagesetGlow;
	    string quad;
	    for (int i = 0; i < 100; i++)  // Safety cap
	    {
	        if (!placedEntry.GetIconEntry(i, imageset, imagesetGlow, quad))
	            break;
	        
	        if (quad == targetQuad)
	            return i;
	    }
	    
	    return -1;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Handle marker_add command from web API
	//! Creates a new PLACED_CUSTOM marker with TDL icon, assigned to the linked player
	protected void HandleMarkerAddCommand(JsonLoadContext cmdJson)
	{
	    // --- Read payload fields ---
	    string customText;
	    if (!cmdJson.ReadValue("customText", customText))
	        customText = "";
	    
	    string markerType;
	    if (!cmdJson.ReadValue("markerType", markerType))
	    {
	        Print("[TDL_API] marker_add missing 'markerType'", LogLevel.WARNING);
	        return;
	    }
	    
	    int colorIndex;
	    if (!cmdJson.ReadValue("colorIndex", colorIndex))
	        colorIndex = 0;
	    
	    float posX, posZ;
	    if (!cmdJson.ReadValue("posX", posX) || !cmdJson.ReadValue("posZ", posZ))
	    {
	        Print("[TDL_API] marker_add missing position (posX/posZ)", LogLevel.WARNING);
	        return;
	    }
	    
	    // --- Validate icon quad is a TDL type ---
	    if (!markerType.Contains("tdl_"))
	    {
	        Print(string.Format("[TDL_API] marker_add: '%1' is not a TDL marker type", markerType), LogLevel.WARNING);
	        return;
	    }
	    
	    // --- Resolve quad name to icon entry index ---
	    int iconEntry = ResolveIconEntryFromQuad(markerType);
	    if (iconEntry < 0)
	    {
	        Print(string.Format("[TDL_API] marker_add: Could not resolve icon entry for '%1'", markerType), LogLevel.WARNING);
	        return;
	    }
	    
	    // --- Get marker manager ---
	    SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
	    if (!markerMgr)
	    {
	        Print("[TDL_API] marker_add: Marker manager not available", LogLevel.WARNING);
	        return;
	    }
	    
	    // --- Resolve owner player ID from payload (web session linked player) ---
	    int ownerPlayerId = -1;  // Default: server-owned (visible to all connected)
	    int payloadPlayerId;
	    if (cmdJson.ReadValue("playerId", payloadPlayerId) && payloadPlayerId > 0)
	        ownerPlayerId = payloadPlayerId;
	    
	    // --- Create the marker ---
	    SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
	    marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);
	    marker.SetWorldPos((int)posX, (int)posZ);
	    marker.SetIconEntry(iconEntry);
	    marker.SetColorEntry(colorIndex);
	    marker.SetCustomText(customText);
	    
	    // Assign UID, set ownership, broadcast to all clients
	    markerMgr.AssignMarkerUID(marker);
	    marker.SetMarkerOwnerID(ownerPlayerId);
	    markerMgr.OnAddSynchedMarker(marker);
	    markerMgr.OnAskAddStaticMarker(marker);
	    
	    Print(string.Format("[TDL_API] marker_add: Created marker %1 ('%2', icon=%3, color=%4) at [%5, %6] owner=%7",
	        marker.GetMarkerID(), customText, markerType, colorIndex, posX, posZ, ownerPlayerId), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
    //! Handle shapes_refresh command from web API
	//! Immediately triggers a shapes poll so in-game shapes stay in sync with the web map
    protected void HandleShapesRefreshCommand()
    {
        Print("[TDL_API] shapes_refresh command received, triggering immediate shapes poll", LogLevel.DEBUG);
		PollShapes();
    }
	
	//------------------------------------------------------------------------------------------------
	//! Submit account link request with external callback
	//! Used by AG0_TDLLinkCommand for async handling via OnUpdate()
	//! @param callback External RestCallback to receive result (e.g., StateBackendCallback)
	//! @param linkCode Code from web app (4-16 alphanumeric)
	//! @param identityId Player's persistent identity UUID from BackendApi
	//! @param playerName Player's display name
	//! @param playerId Session player ID (for logging)
	//! @param platform Player's platform (Steam/Xbox/PSN)
	//! @return true if request was initiated
	bool SubmitAccountLink(RestCallback callback, string linkCode, string identityId, 
	                       string playerName, int playerId, PlatformKind platform)
	{
	    if (!CanCommunicate())
	    {
	        Print("[TDL_API] Cannot submit link - API not ready", LogLevel.DEBUG);
	        return false;
	    }
	    
	    if (!callback || linkCode.IsEmpty() || identityId.IsEmpty())
	        return false;
	    
	    RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
	    if (!ctx)
	    {
	        Print("[TDL_API] Failed to get REST context for account link", LogLevel.DEBUG);
	        return false;
	    }
	    
	    // Set authorization header
	    string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", m_Config.apiKey);
	    ctx.SetHeaders(headers);
	    
	    // Build payload using JsonSaveContext for proper escaping
	    JsonSaveContext json = new JsonSaveContext();
	    json.WriteValue("type", "account_link");
	    json.WriteValue("linkCode", linkCode);
	    json.WriteValue("playerIdentityId", identityId);
	    json.WriteValue("playerName", playerName);
	    json.WriteValue("playerId", playerId);
	    json.WriteValue("platform", platform);
	    json.WriteValue("serverName", m_Config.serverName);
	    json.WriteValue("worldFile", GetGame().GetWorldFile());
	    json.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());
	    json.WriteValue("timestamp", System.GetUnixTime());
	    
	    string payload = json.SaveToString();
	    
	    Print(string.Format("[TDL_API] Submitting account link for %1 (identity: %2...)", 
	        playerName, identityId.Substring(0, 8)), LogLevel.DEBUG);
	    
	    ctx.POST(callback, "/link", payload);
	    return true;
	}
    
    //------------------------------------------------------------------------------------------------
    // Getters
    //------------------------------------------------------------------------------------------------
    
    bool IsInitialized() { return m_bInitialized; }
    bool IsApiKeyValid() { return m_bApiKeyValid; }
    bool IsEnabled() { return m_Config && m_Config.enabled; }
    
    string GetServerName()
    {
        if (m_Config)
            return m_Config.serverName;
        return "Unknown";
    }
    
    int GetPollInterval()
    {
        if (m_Config)
            return m_Config.pollIntervalSeconds;
        return 5;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get statistics string for debugging
    string GetStatsString()
    {
        return string.Format("Submits: %1/%2 (ok/fail), Polls: %3/%4 (ok/fail)",
            m_iSuccessfulSubmits, m_iFailedSubmits,
            m_iSuccessfulPolls, m_iFailedPolls);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Reload configuration from file
    bool ReloadConfig()
    {
        Print("[TDL_API] Reloading configuration...", LogLevel.DEBUG);
        
        if (LoadConfig())
        {
            // Re-validate API key if it changed
            if (m_Config.HasValidApiKey())
            {
                m_bApiKeyValid = false;
                ValidateApiKey();
            }
            return true;
        }
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Set API key programmatically (also saves to config)
    void SetApiKey(string newKey)
    {
        if (m_Config)
        {
            m_Config.apiKey = newKey;
            SaveConfig();
            
            // Re-validate with new key
            m_bApiKeyValid = false;
            if (m_Config.HasValidApiKey())
                ValidateApiKey();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Set server name (also saves to config)
    void SetServerName(string name)
    {
        if (m_Config)
        {
            m_Config.serverName = name;
            SaveConfig();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Enable or disable API communication
    void SetEnabled(bool enabled)
    {
        if (m_Config)
        {
            m_Config.enabled = enabled;
            SaveConfig();
        }
    }
	
	//------------------------------------------------------------------------------------------------
	//! Poll the shapes endpoint for current drawing overlay state
	//! Called on its own timer, separate from queue polling
	void PollShapes()
	{
		if (!CanCommunicate())
			return;

		if (m_bShapesPollInProgress)
			return;

		if (!m_BreakerShapes.AllowSend())
			return;
		if (!m_HotGate.TryAcquire())
			return;

		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
		{
			m_HotGate.Release();
			Print("[TDL_API] Failed to get REST context for shapes poll", LogLevel.ERROR);
			return;
		}

		// Set authorization header
		string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
		ctx.SetHeaders(headers);

		m_bShapesPollInProgress = true;

		// Build query path — include syncHash for server-side short-circuit.
		// Use the raw API epoch (no local-mutation suffix) so the API can
		// still 304-equivalent when nothing has changed on its side.
		string path = "/shapes";
		string lastHash = m_ShapeManager.GetApiPollSyncHash();
		if (!lastHash.IsEmpty())
			path = string.Format("/shapes?since=%1", lastHash);

		ctx.GET(m_ShapesCallback, path);
	}

	//------------------------------------------------------------------------------------------------
	//! Called when shapes poll succeeds
	void OnShapesPollSuccess(string data)
	{
		m_HotGate.Release();
		m_BreakerShapes.OnSuccess();
		m_bShapesPollInProgress = false;
		m_iSuccessfulShapePolls++;

		if (data.IsEmpty())
			return;

		// Check for "no changes" short-circuit response
		JsonLoadContext quickCheck = new JsonLoadContext();
		if (quickCheck.LoadFromString(data))
		{
			bool changed = true;
			if (quickCheck.ReadValue("changed", changed) && !changed)
			{
				// No changes since last poll — skip full parse
				return;
			}
		}

		// Remember previous hash to detect actual changes
		string prevHash = m_ShapeManager.GetLastSyncHash();

		// Full parse (also stores raw JSON strings for redistribution)
		int updated = m_ShapeManager.ParseShapesResponse(data);

		// Prune any expired shapes
		m_ShapeManager.PruneStale();

		// If sync hash changed, distribute to all networked clients.
		// Coalesced — a poll that lands on the same tick as a local
		// create/delete folds into one broadcast.
		string newHash = m_ShapeManager.GetLastSyncHash();
		if (newHash != prevHash)
		{
			AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
			if (tdlSystem)
				tdlSystem.MarkShapesDirtyForBroadcast();
		}

		if (updated > 0)
		{
			Print(string.Format("[TDL_API] Shapes poll: %1 shapes updated, %2 total",
				updated, m_ShapeManager.GetShapeCount()), LogLevel.DEBUG);
		}
	}

	//------------------------------------------------------------------------------------------------
	void OnShapesPollError(int errorCode)
	{
		m_HotGate.Release();
		// 404 means the endpoint isn't deployed yet rather than the host
		// being down, so it counts as a "successful" call from the breaker's
		// perspective — the round-trip completed cleanly, just with a known
		// not-yet-implemented status. Otherwise treat as a failure.
		if (errorCode == 404)
			m_BreakerShapes.OnSuccess();
		else
			m_BreakerShapes.OnFailure();

		m_bShapesPollInProgress = false;
		m_iFailedShapePolls++;

		if (errorCode == 401)
		{
			Print("[TDL_API] Shapes poll returned 401 - API key may have been revoked", LogLevel.WARNING);
			m_bApiKeyValid = false;
		}
		else if (errorCode == 404)
		{
			// Endpoint not implemented yet — silently ignore
			// This allows the mod to ship before the web API is ready
			Print("[TDL_API] Shapes endpoint not found (404) - feature not yet available on server", LogLevel.DEBUG);
		}
	}

	//------------------------------------------------------------------------------------------------
	void OnShapesPollTimeout()
	{
		m_HotGate.Release();
		m_BreakerShapes.OnFailure();
		m_bShapesPollInProgress = false;
		m_iFailedShapePolls++;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get the shape manager for reading shape data (used by map renderer)
	AG0_TDLMapShapeManager GetShapeManager()
	{
		return m_ShapeManager;
	}

	//------------------------------------------------------------------------------------------------
	// SHAPE SUBMIT (mod-originated → API mirror)
	//------------------------------------------------------------------------------------------------

	// In-memory retry queue for mod-originated shape POSTs. Holds local
	// shape IDs whose first submit failed; periodic retry kicks in via
	// OnSubmitRetryTick. Cap at 64 so a long API outage doesn't grow this
	// unbounded — older entries get dropped (logged) when the cap is hit.
	protected ref array<string> m_aSubmitRetryQueue = {};
	protected float m_fSubmitRetryAccum = 0;
	protected static const int SUBMIT_RETRY_CAP = 64;
	protected static const float SUBMIT_RETRY_INTERVAL = 15.0;

	//------------------------------------------------------------------------------------------------
	//! POST a mod-originated shape to /api/mod/shapes. Best-effort: a failure
	//! does NOT remove the shape from the local store. Caller is responsible
	//! for having already inserted the shape as LOCAL-origin so clients see
	//! it the same tick — this is the persistence mirror on top of that.
	bool SubmitShape(AG0_TDLMapShape shape)
	{
		if (!shape || shape.m_sId.IsEmpty())
			return false;

		if (!CanCommunicate())
		{
			// Distinguish "API is off / not configured" from "API is on
			// but temporarily unreachable". Retry only makes sense for
			// the second — for the first the shape would churn through
			// the retry queue forever, each 15s tick dequeueing and
			// re-queueing on the same failed check. Most servers run
			// unlinked (no API key), so the common case is the disabled
			// path — discard and let the LOCAL-origin shape live out
			// its life in mod memory only.
			if (IsEnabled())
				QueueShapeRetry(shape.m_sId);
			return false;
		}

		// Breaker rejection re-queues so the shape persists past the
		// outage rather than vanishing — the local-origin copy already
		// lives in clients' shape stores either way.
		if (!m_BreakerShapes.AllowSend())
		{
			QueueShapeRetry(shape.m_sId);
			return false;
		}
		if (!m_HotGate.TryAcquire())
		{
			QueueShapeRetry(shape.m_sId);
			return false;
		}

		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
		{
			m_HotGate.Release();
			QueueShapeRetry(shape.m_sId);
			return false;
		}

		string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", m_Config.apiKey);
		ctx.SetHeaders(headers);

		string payload = shape.ToJsonString();
		if (payload.IsEmpty())
		{
			m_HotGate.Release();
			return false;
		}

		AG0_TDLApiSubmitShapeCallback cb = new AG0_TDLApiSubmitShapeCallback(this, shape.m_sId);
		ctx.POST(cb, "/shapes", payload);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Best-effort DELETE /api/mod/shapes/[shapeId]?byIdentityId=<uuid>.
	//! Local-side removal has already happened by the time this is called —
	//! this just syncs the API store. Skipped entirely when API is disabled
	//! (most servers). LOCAL-origin shapes (id starts with "local_") are
	//! also skipped because the API never had them in the first place.
	bool SubmitShapeDelete(string shapeId, string identityId)
	{
		if (shapeId.IsEmpty() || identityId.IsEmpty())
			return false;
		if (!IsEnabled())
			return false;
		// LOCAL-origin shapes were never persisted to the API — skip the
		// round-trip rather than burn a guaranteed 404.
		if (shapeId.IndexOf("local_") == 0)
			return false;

		if (!CanCommunicate())
			return false;

		// Deletes are fire-and-forget; if the breaker or gate refuses we
		// just skip — the local-side removal already happened and the
		// next successful poll reconcile will catch the lingering API row.
		if (!m_BreakerShapes.AllowSend())
			return false;
		if (!m_HotGate.TryAcquire())
			return false;

		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
		{
			m_HotGate.Release();
			return false;
		}

		string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", m_Config.apiKey);
		ctx.SetHeaders(headers);

		AG0_TDLApiDeleteShapeCallback cb = new AG0_TDLApiDeleteShapeCallback(this, shapeId);
		string path = string.Format("/shapes/%1?byIdentityId=%2", shapeId, identityId);
		ctx.DELETE(cb, path, string.Empty);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! POST /shapes succeeded. Parse the response (canonical shape with the
	//! API-assigned id), remove the local placeholder, insert the canonical
	//! entry as LOCAL-origin until the next API poll reconciles it to API-
	//! origin. Then re-fan-out so clients swap to the canonical id.
	void OnSubmitShapeSuccess(string localShapeId, string responseBody)
	{
		m_HotGate.Release();
		m_BreakerShapes.OnSuccess();

		if (localShapeId.IsEmpty() || !m_ShapeManager)
			return;

		// API response is the full canonical shape. ParseSingleShape handles
		// the same JSON shape the GET feed emits.
		AG0_TDLMapShape canonical = m_ShapeManager.ParseSingleShape(responseBody);
		if (!canonical || canonical.m_sId.IsEmpty())
		{
			// Bad response — leave the local entry alone and try again later.
			QueueShapeRetry(localShapeId);
			Print(string.Format("[TDL_SHAPES] SubmitShape ok but canonical parse failed for %1", localShapeId), LogLevel.WARNING);
			return;
		}

		// Drop the local placeholder and insert the canonical entry. Keep
		// it as LOCAL-origin so the next poll's full-replace doesn't wipe
		// it before the API response includes it in its own listing.
		m_ShapeManager.RemoveShapeById(localShapeId);
		m_ShapeManager.InsertLocalShape(canonical, responseBody);

		Print(string.Format("[TDL_SHAPES] SubmitShape: %1 -> %2", localShapeId, canonical.m_sId), LogLevel.DEBUG);

		// Drop any pending retry for this id — submit succeeded.
		int idx = m_aSubmitRetryQueue.Find(localShapeId);
		if (idx != -1)
			m_aSubmitRetryQueue.Remove(idx);

		// Redistribute so clients see the canonical id this tick.
		// Coalesced broadcast + targeted push so a burst of POST
		// successes (multiple LOCAL shapes posting back-to-back)
		// collapses on the wire instead of multiplying.
		AG0_TDLSystem tdl = AG0_TDLSystem.GetInstance();
		if (tdl)
		{
			tdl.MarkShapesDirtyForBroadcast();

			// Targeted push to the creator on top of the network fan-out.
			// DistributeShapesToClients walks m_aNetworks and only pushes
			// to network members — a no-network creator (orphan draw)
			// would otherwise never receive the canonical update, leaving
			// their client showing the original local_<hex> sample data
			// from CreateLocalShape's first targeted push (visible as the
			// "in-game looks smoother than web" mismatch: in-game still
			// has the unsimplified local samples while web reads the
			// simplified canonical from the API store).
			if (!canonical.m_sCreatedByPlayerIdentityId.IsEmpty())
			{
				int creatorPlayerId = tdl.GetPlayerIdFromIdentityId(canonical.m_sCreatedByPlayerIdentityId);
				if (creatorPlayerId > 0)
					tdl.QueueTargetedShapePush(creatorPlayerId);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! POST /shapes failed. 404 = endpoint not yet deployed; queue for retry
	//! so the local entry persists and reconciles whenever the API endpoint
	//! lands. Other 4xx (incl. 422 validation) likely won't fix themselves —
	//! still queue so a server admin can fix server-side state and retry
	//! later without losing the in-game draft.
	void OnSubmitShapeError(string localShapeId, int errorCode)
	{
		m_HotGate.Release();
		// 404 means the endpoint isn't deployed yet — round-trip succeeded,
		// so don't count it against the breaker (mirrors poll-side logic).
		if (errorCode == 404)
			m_BreakerShapes.OnSuccess();
		else
			m_BreakerShapes.OnFailure();

		LogLevel level = LogLevel.WARNING;
		if (errorCode == 404)
			level = LogLevel.DEBUG;
		Print(string.Format("[TDL_SHAPES] SubmitShape error %1 for %2", errorCode, localShapeId), level);

		if (errorCode == 401)
			m_bApiKeyValid = false;

		QueueShapeRetry(localShapeId);
	}

	//------------------------------------------------------------------------------------------------
	//! Settlement hook for AG0_TDLApiDeleteShapeCallback. Delete is fire-
	//! and-forget at the data layer, but we still need to release the gate
	//! slot and feed the breaker so a wedged endpoint backs off and we
	//! stop trying. 404 is a healthy round-trip (the shape was already
	//! gone — common for LOCAL-origin entries) so don't trip the breaker.
	void OnDeleteShapeCompleted(bool success, int errorCode)
	{
		m_HotGate.Release();
		if (success || errorCode == 404)
			m_BreakerShapes.OnSuccess();
		else
			m_BreakerShapes.OnFailure();
	}

	//------------------------------------------------------------------------------------------------
	//! Append a local shape ID to the retry queue. De-duplicates so a burst
	//! of failures for the same shape doesn't fill the cap with duplicates.
	//! Drops the oldest entry when the cap is hit so the queue stays bounded.
	protected void QueueShapeRetry(string localShapeId)
	{
		if (localShapeId.IsEmpty())
			return;
		if (m_aSubmitRetryQueue.Find(localShapeId) != -1)
			return;

		if (m_aSubmitRetryQueue.Count() >= SUBMIT_RETRY_CAP)
		{
			string dropped = m_aSubmitRetryQueue[0];
			m_aSubmitRetryQueue.Remove(0);
			Print(string.Format("[TDL_SHAPES] Retry queue full; dropping %1", dropped), LogLevel.WARNING);
		}

		m_aSubmitRetryQueue.Insert(localShapeId);
	}

	//------------------------------------------------------------------------------------------------
	//! Periodic retry tick — drains the queue at SUBMIT_RETRY_INTERVAL. Each
	//! tick re-submits one queued shape (so a burst of failures doesn't
	//! produce a burst of retries; spaces them out). Caller wires this into
	//! the API manager's existing update loop.
	void OnSubmitRetryTick(float timeSlice)
	{
		m_fSubmitRetryAccum = m_fSubmitRetryAccum + timeSlice;
		if (m_fSubmitRetryAccum < SUBMIT_RETRY_INTERVAL)
			return;
		m_fSubmitRetryAccum = 0;

		if (m_aSubmitRetryQueue.IsEmpty() || !m_ShapeManager || !CanCommunicate())
			return;

		string localId = m_aSubmitRetryQueue[0];
		m_aSubmitRetryQueue.Remove(0);

		AG0_TDLMapShape shape = m_ShapeManager.GetShape(localId);
		if (!shape)
			return;

		SubmitShape(shape);
	}

	//------------------------------------------------------------------------------------------------
	// Terrain structures (building footprints)
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Server-side: GET /api/mod/terrain/structures (with ?since=<lastHash> when known).
	//! Triggered once after key validation, and again on terrain_structures_refresh
	//! queue commands from the web app. There is no periodic timer for this dataset
	//! because building data is effectively static per world.
	void PollTerrainStructures()
	{
		if (!CanCommunicate())
			return;

		if (m_bTerrainStructuresPollInProgress)
			return;

		if (!m_BreakerTerrain.AllowSend())
			return;
		if (!m_TerrainGate.TryAcquire())
			return;

		// Dedicated terrain context — base URL includes /terrain so the
		// engine buckets these calls into a separate 64-slot pool from the
		// chatty /submit + /queue traffic on API_BASE_URL. A slow terrain
		// GET can't starve heartbeat/state-sync anymore.
		RestContext ctx = GetGame().GetRestApi().GetContext(API_TERRAIN_BASE_URL);
		if (!ctx)
		{
			m_TerrainGate.Release();
			Print("[TDL_API] Failed to get REST context for terrain structures poll", LogLevel.ERROR);
			return;
		}

		// Auth only — do NOT send Accept-Encoding: gzip here.
		// The Reforger REST stack does not transparently decompress for this
		// endpoint, so requesting gzip causes LoadFromString to fail with
		// "invalid JSON" on the (still-compressed) bytes. The dataset is small
		// enough uncompressed that this isn't a problem in practice; if/when
		// payloads grow we can add a manual gunzip step using AG0_TDLGzip.
		string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
		ctx.SetHeaders(headers);

		m_bTerrainStructuresPollInProgress = true;

		// Path is now relative to API_TERRAIN_BASE_URL (which ends in /terrain),
		// so the endpoint suffix is /structures rather than /terrain/structures.
		string path = "/structures";
		if (m_TerrainStructureManager)
		{
			string lastHash = m_TerrainStructureManager.GetLastSyncHash();
			if (!lastHash.IsEmpty())
				path = string.Format("/structures?since=%1", lastHash);
		}

		Print(string.Format("[TDL_API] Fetching terrain structures: GET %1", path), LogLevel.DEBUG);
		ctx.GET(m_TerrainStructuresCallback, path);
	}

	//------------------------------------------------------------------------------------------------
	//! Called when /terrain/structures returns 200 with a body.
	void OnTerrainStructuresPollSuccess(string data)
	{
		m_TerrainGate.Release();
		m_BreakerTerrain.OnSuccess();
		m_bTerrainStructuresPollInProgress = false;
		m_iSuccessfulTerrainStructuresPolls++;

		if (data.IsEmpty())
		{
			Print("[TDL_API] Terrain structures: 200 with empty body — ignoring", LogLevel.DEBUG);
			return;
		}

		string prevHash;
		if (m_TerrainStructureManager)
			prevHash = m_TerrainStructureManager.GetLastSyncHash();

		int parsed = m_TerrainStructureManager.ParseColumnarPayload(data);
		string newHash = m_TerrainStructureManager.GetLastSyncHash();

		// Only fan out to clients when the dataset actually changed.
		if (newHash != prevHash)
		{
			AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
			if (tdlSystem)
				tdlSystem.DistributeTerrainStructuresToClients();
		}

		Print(string.Format("[TDL_API] Terrain structures poll: %1 buildings, hash=%2",
			parsed, newHash), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Called when /terrain/structures returns a non-2xx HTTP status.
	//! 304 is the "no change" short-circuit and is expected when ?since= matches.
	void OnTerrainStructuresPollError(int errorCode)
	{
		m_TerrainGate.Release();
		// 304 (cache hit) and 404 (no dataset for this world) are both
		// clean round-trips from the breaker's perspective — host link
		// is healthy, server just had nothing new to send back. Other
		// codes (401, 5xx, transport) trip toward backoff.
		if (errorCode == 304 || errorCode == 404)
			m_BreakerTerrain.OnSuccess();
		else
			m_BreakerTerrain.OnFailure();

		m_bTerrainStructuresPollInProgress = false;

		if (errorCode == 304)
		{
			// Expected when our cached hash matches server-side. Not an error.
			Print("[TDL_API] Terrain structures: 304 Not Modified", LogLevel.DEBUG);
			m_iSuccessfulTerrainStructuresPolls++;
			return;
		}

		m_iFailedTerrainStructuresPolls++;

		if (errorCode == 401)
		{
			Print("[TDL_API] Terrain structures: 401 — API key may have been revoked", LogLevel.WARNING);
			m_bApiKeyValid = false;
		}
		else if (errorCode == 404)
		{
			// Either no map matched, no structures layer in R2, or all features
			// were filtered out. Ship-safe — log once at DEBUG and move on.
			Print("[TDL_API] Terrain structures: 404 — no dataset for this world", LogLevel.DEBUG);
		}
		else
		{
			Print(string.Format("[TDL_API] Terrain structures poll failed: HTTP %1", errorCode),
				LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	void OnTerrainStructuresPollTimeout()
	{
		m_TerrainGate.Release();
		m_BreakerTerrain.OnFailure();
		m_bTerrainStructuresPollInProgress = false;
		m_iFailedTerrainStructuresPolls++;
		Print("[TDL_API] Terrain structures poll timed out", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Handle terrain_structures_refresh queue command from the web API.
	//! Triggers an immediate refetch so the in-game dataset stays in sync after
	//! a web-side map upload / import.
	protected void HandleTerrainStructuresRefreshCommand()
	{
		Print("[TDL_API] terrain_structures_refresh command received, triggering immediate fetch",
			LogLevel.DEBUG);
		PollTerrainStructures();
	}

	//------------------------------------------------------------------------------------------------
	//! Get the terrain structure manager (used by AG0_TDLSystem for client distribution).
	AG0_TDLTerrainStructureManager GetTerrainStructureManager()
	{
		return m_TerrainStructureManager;
	}

	//------------------------------------------------------------------------------------------------
	// Terrain roads (road network)
	// Mirrors the terrain structures lifecycle exactly — see those methods for rationale.
	//------------------------------------------------------------------------------------------------

	void PollTerrainRoads()
	{
		if (!CanCommunicate())
			return;
		if (m_bTerrainRoadsPollInProgress)
			return;

		if (!m_BreakerTerrain.AllowSend())
			return;
		if (!m_TerrainGate.TryAcquire())
			return;

		// Dedicated terrain context (see PollTerrainStructures for rationale).
		RestContext ctx = GetGame().GetRestApi().GetContext(API_TERRAIN_BASE_URL);
		if (!ctx)
		{
			m_TerrainGate.Release();
			Print("[TDL_API] Failed to get REST context for terrain roads poll", LogLevel.ERROR);
			return;
		}

		// Auth only — no Accept-Encoding (REST stack does not transparently
		// decompress; matches structures path).
		string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
		ctx.SetHeaders(headers);

		m_bTerrainRoadsPollInProgress = true;

		// Relative to API_TERRAIN_BASE_URL — see PollTerrainStructures.
		string path = "/roads";
		if (m_TerrainRoadManager)
		{
			string lastHash = m_TerrainRoadManager.GetLastSyncHash();
			if (!lastHash.IsEmpty())
				path = string.Format("/roads?since=%1", lastHash);
		}

		Print(string.Format("[TDL_API] Fetching terrain roads: GET %1", path), LogLevel.DEBUG);
		ctx.GET(m_TerrainRoadsCallback, path);
	}

	void OnTerrainRoadsPollSuccess(string data)
	{
		m_TerrainGate.Release();
		m_BreakerTerrain.OnSuccess();
		m_bTerrainRoadsPollInProgress = false;
		m_iSuccessfulTerrainRoadsPolls++;

		if (data.IsEmpty())
		{
			Print("[TDL_API] Terrain roads: 200 with empty body — ignoring", LogLevel.DEBUG);
			return;
		}

		string prevHash;
		if (m_TerrainRoadManager)
			prevHash = m_TerrainRoadManager.GetLastSyncHash();

		int parsed = m_TerrainRoadManager.ParseColumnarPayload(data);
		string newHash = m_TerrainRoadManager.GetLastSyncHash();

		if (newHash != prevHash)
		{
			AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
			if (tdlSystem)
				tdlSystem.DistributeTerrainRoadsToClients();
		}

		Print(string.Format("[TDL_API] Terrain roads poll: %1 features, hash=%2",
			parsed, newHash), LogLevel.DEBUG);
	}

	void OnTerrainRoadsPollError(int errorCode)
	{
		m_TerrainGate.Release();
		// Same 304/404 = healthy round-trip rationale as structures path.
		if (errorCode == 304 || errorCode == 404)
			m_BreakerTerrain.OnSuccess();
		else
			m_BreakerTerrain.OnFailure();

		m_bTerrainRoadsPollInProgress = false;

		if (errorCode == 304)
		{
			Print("[TDL_API] Terrain roads: 304 Not Modified", LogLevel.DEBUG);
			m_iSuccessfulTerrainRoadsPolls++;
			return;
		}

		m_iFailedTerrainRoadsPolls++;

		if (errorCode == 401)
		{
			Print("[TDL_API] Terrain roads: 401 — API key may have been revoked", LogLevel.WARNING);
			m_bApiKeyValid = false;
		}
		else if (errorCode == 404)
		{
			Print("[TDL_API] Terrain roads: 404 — no dataset for this world", LogLevel.DEBUG);
		}
		else
		{
			Print(string.Format("[TDL_API] Terrain roads poll failed: HTTP %1", errorCode),
				LogLevel.WARNING);
		}
	}

	void OnTerrainRoadsPollTimeout()
	{
		m_TerrainGate.Release();
		m_BreakerTerrain.OnFailure();
		m_bTerrainRoadsPollInProgress = false;
		m_iFailedTerrainRoadsPolls++;
		Print("[TDL_API] Terrain roads poll timed out", LogLevel.DEBUG);
	}

	protected void HandleTerrainRoadsRefreshCommand()
	{
		Print("[TDL_API] terrain_roads_refresh command received, triggering immediate fetch",
			LogLevel.DEBUG);
		PollTerrainRoads();
	}

	AG0_TDLTerrainRoadManager GetTerrainRoadManager()
	{
		return m_TerrainRoadManager;
	}
}

class AG0_TDLDeviceState
{
    int rplId;
    string callsign;
    int capabilities;
    bool isPowered;
    // Device-global broadcast flag. Per-viewer "is the contact reachable as a
    // video source" lives in the mirror snapshot's contacts[] instead, since
    // bridging can hide a broadcast from some networks; this top-level bit is
    // the raw "the gadget is currently producing a feed" answer.
    bool isCameraBroadcasting;
    float posX;
    float posY;
    float posZ;
	string playerName;
    string playerIdentityId;
    int playerPlatform;
}

class AG0_TDLNetworkState
{
    int networkId;
    // Stable, restart-proof identifier. Always populated for state_sync emissions
    // from a mod that ships with the stableId rollout. The API should treat this
    // as the primary key for persistence; networkId is sidecar for human display
    // and backward compat with pre-rollout rows.
    string networkStableId;
    string networkName;
    int waveform;       // AG0_ETDLWaveform cast to int — identifies the RF technology of this network
    int deviceCount;
    int messageCount;
    ref array<ref AG0_TDLDeviceState> devices;
    
    void AG0_TDLNetworkState()
    {
        devices = {};
    }
}

class AG0_TDLMapMarkerState
{
	int markerId;
    string markerType;          // Quad name: "tdl_checkpoint", "tdl_pin", etc.
    float posX;
    float posZ;
    int ownerPlayerId;
    string ownerPlayerName;
    string customText;
    int colorIndex;
}