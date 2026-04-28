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
        SCR_JsonSaveContext validateJson = new SCR_JsonSaveContext();
        validateJson.WriteValue("type", "validate");
        validateJson.WriteValue("serverName", m_Config.serverName);
        validateJson.WriteValue("worldFile", GetGame().GetWorldFile());
        validateJson.WriteValue("worldId", AG0_MapSatelliteConfigHelper.GetCurrentWorldIdentifier());
        string payload = validateJson.ExportToString();

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
        
        if (!m_bApiKeyValid)
            return;
        
        // Update poll timer
        m_fTimeSinceLastPoll += timeSlice;
        
        // Poll for queued commands at configured interval
        if (!m_bPollInProgress && m_fTimeSinceLastPoll >= m_Config.pollIntervalSeconds)
        {
            PollQueue();
            m_fTimeSinceLastPoll = 0;
        }
    }
    
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
        
        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
        {
            Print("[TDL_API] Failed to get REST context for submit", LogLevel.DEBUG);
            return false;
        }
        
        // Set authorization header with Bearer token
        // Format: "key1,value1,key2,value2"
        string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", m_Config.apiKey);
        ctx.SetHeaders(headers);
        
        Print(string.Format("[TDL_API] Submitting data: %1 bytes", jsonData.Length()), LogLevel.DEBUG);
        
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
        
        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
        {
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
    // Callback handlers
    //------------------------------------------------------------------------------------------------
    
    void OnSubmitSuccess(string data)
    {
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
        m_iFailedSubmits++;
    }
    
    void OnQueuePollSuccess(string data)
    {
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
        m_bPollInProgress = false;
        m_iFailedPolls++;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Process response from submit endpoint
    protected void ProcessSubmitResponse(string data)
    {
        // Parse JSON response
        SCR_JsonLoadContext json = new SCR_JsonLoadContext();
        if (!json.ImportFromString(data))
        {
            Print("[TDL_API] Failed to parse submit response", LogLevel.WARNING);
            return;
        }
        
        bool success;
        if (json.ReadValue("success", success))
        {
//            if (success)
//                Print("[TDL_API] Server acknowledged submission", LogLevel.DEBUG);
        }
        
        // Handle any additional response fields here
        // e.g., server might return commands or configuration updates
    }
    
    //------------------------------------------------------------------------------------------------
    //! Process commands received from queue endpoint
    protected void ProcessQueuedCommands(string data)
    {
        // Parse JSON response
        SCR_JsonLoadContext json = new SCR_JsonLoadContext();
        if (!json.ImportFromString(data))
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
        SCR_JsonLoadContext cmdJson = new SCR_JsonLoadContext();
        if (!cmdJson.ImportFromString(commandJson))
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

            default:
                Print(string.Format("[TDL_API] Unknown command type: %1", cmdType), LogLevel.WARNING);
                break;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Handle broadcast command from API
    protected void HandleBroadcastCommand(SCR_JsonLoadContext cmdJson)
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
    protected void HandleMessageSendCommand(SCR_JsonLoadContext cmdJson)
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
                tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "missing_recipient", liveNetworkId, liveNetworkStableId);
                return;
            }
            recipientRplId = recipientRplIdInt;
        }
        else
        {
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "invalid_message_type", liveNetworkId, liveNetworkStableId);
            return;
        }

        // Trim payload at the per-string-param RPC cap. The API enforces this on input
        // already (see implementation guide), but defense-in-depth — silent truncation
        // by the engine would corrupt the message mid-relay.
        const int MAX_CONTENT_BYTES = 8000;
        if (content.Length() > MAX_CONTENT_BYTES)
        {
            tdlSystem.ApiNotifyMessageSendFailedPublic(correlationId, "content_too_long", liveNetworkId, liveNetworkStableId);
            return;
        }

        Print(string.Format("[TDL_API] message_send: routing web compose from %1 (player %2) on network %3 [%4] (%5)",
            senderIdentityId, senderPlayerId, liveNetworkId, liveNetworkStableId, messageTypeStr), LogLevel.DEBUG);

        // Single canonical send path — reuses hop graph, replication, RPC delivery,
        // pruning, and the existing ApiNotifyMessageSent fan-out. No bypass.
        tdlSystem.SendTDLMessage(senderDeviceRplId, content, messageType, recipientRplId);
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
    protected void HandleMessageMarkReadCommand(SCR_JsonLoadContext cmdJson)
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
    protected void HandleConfigUpdateCommand(SCR_JsonLoadContext cmdJson)
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
    protected void HandleMarkerDeleteCommand(SCR_JsonLoadContext cmdJson)
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
    protected void HandleMarkerEditCommand(SCR_JsonLoadContext cmdJson)
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
	protected void HandleMarkerAddCommand(SCR_JsonLoadContext cmdJson)
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
	    
	    // Build payload using SCR_JsonSaveContext for proper escaping
	    SCR_JsonSaveContext json = new SCR_JsonSaveContext();
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
	    
	    string payload = json.ExportToString();
	    
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
		
		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
		{
			Print("[TDL_API] Failed to get REST context for shapes poll", LogLevel.ERROR);
			return;
		}
		
		// Set authorization header
		string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
		ctx.SetHeaders(headers);
		
		m_bShapesPollInProgress = true;
		
		// Build query path — include syncHash for server-side short-circuit
		string path = "/shapes";
		string lastHash = m_ShapeManager.GetLastSyncHash();
		if (!lastHash.IsEmpty())
			path = string.Format("/shapes?since=%1", lastHash);
		
		ctx.GET(m_ShapesCallback, path);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Called when shapes poll succeeds
	void OnShapesPollSuccess(string data)
	{
		m_bShapesPollInProgress = false;
		m_iSuccessfulShapePolls++;
		
		if (data.IsEmpty())
			return;
		
		// Check for "no changes" short-circuit response
		SCR_JsonLoadContext quickCheck = new SCR_JsonLoadContext();
		if (quickCheck.ImportFromString(data))
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
		
		// If sync hash changed, distribute to all networked clients
		string newHash = m_ShapeManager.GetLastSyncHash();
		if (newHash != prevHash)
		{
			AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
			if (tdlSystem)
				tdlSystem.DistributeShapesToClients();
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

		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
		{
			Print("[TDL_API] Failed to get REST context for terrain structures poll", LogLevel.ERROR);
			return;
		}

		// Auth only — do NOT send Accept-Encoding: gzip here.
		// The Reforger REST stack does not transparently decompress for this
		// endpoint, so requesting gzip causes ImportFromString to fail with
		// "invalid JSON" on the (still-compressed) bytes. The dataset is small
		// enough uncompressed that this isn't a problem in practice; if/when
		// payloads grow we can add a manual gunzip step using AG0_TDLGzip.
		string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
		ctx.SetHeaders(headers);

		m_bTerrainStructuresPollInProgress = true;

		// Build path. Server short-circuits to 304 when ?since= matches the
		// current content hash; we treat that as "keep current dataset".
		string path = "/terrain/structures";
		if (m_TerrainStructureManager)
		{
			string lastHash = m_TerrainStructureManager.GetLastSyncHash();
			if (!lastHash.IsEmpty())
				path = string.Format("/terrain/structures?since=%1", lastHash);
		}

		Print(string.Format("[TDL_API] Fetching terrain structures: GET %1", path), LogLevel.DEBUG);
		ctx.GET(m_TerrainStructuresCallback, path);
	}

	//------------------------------------------------------------------------------------------------
	//! Called when /terrain/structures returns 200 with a body.
	void OnTerrainStructuresPollSuccess(string data)
	{
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

		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
		{
			Print("[TDL_API] Failed to get REST context for terrain roads poll", LogLevel.ERROR);
			return;
		}

		// Auth only — no Accept-Encoding (REST stack does not transparently
		// decompress; matches structures path).
		string headers = string.Format("Authorization,Bearer %1", m_Config.apiKey);
		ctx.SetHeaders(headers);

		m_bTerrainRoadsPollInProgress = true;

		string path = "/terrain/roads";
		if (m_TerrainRoadManager)
		{
			string lastHash = m_TerrainRoadManager.GetLastSyncHash();
			if (!lastHash.IsEmpty())
				path = string.Format("/terrain/roads?since=%1", lastHash);
		}

		Print(string.Format("[TDL_API] Fetching terrain roads: GET %1", path), LogLevel.DEBUG);
		ctx.GET(m_TerrainRoadsCallback, path);
	}

	void OnTerrainRoadsPollSuccess(string data)
	{
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