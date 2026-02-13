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
        pollIntervalSeconds = 30;
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
        config.pollIntervalSeconds = 30;
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
        string payload = "{\"type\":\"validate\",\"serverName\":\"" + m_Config.serverName + "\"}";
        
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
        }
        else
        {
            Print("[TDL_API] API key validation failed - API communication disabled", LogLevel.DEBUG);
            Print("[TDL_API] Please check your API key in: " + CONFIG_FILE, LogLevel.DEBUG);
        }
    }
	
	float GetStateSyncInterval()
	{
	    if (!m_Config)
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
        return 30;
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
    string networkName;
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