[BaseContainerProps()]
class AG0_PostProcessEffect
{
    [Attribute("0", UIWidgets.ComboBox, "", "", ParamEnumArray.FromEnum(PostProcessEffectType))]
    PostProcessEffectType m_eEffectType;
    
    [Attribute("0", UIWidgets.Slider, "Priority (0-19)", "0 19 1")]
    int m_iPriority;
    
    [Attribute("", UIWidgets.ResourceNamePicker, "Material", "emat")]
    ResourceName m_sMaterialPath;
}


enum AG0_ETDLDeviceCapability
{
    NETWORK_ACCESS = 1,
    GPS_PROVIDER = 2, 
    DISPLAY_OUTPUT = 4,
    VIDEO_SOURCE = 8,
    POWER_PROVIDER = 16,
	INFORMATION = 32
}

[EntityEditorProps(category: "GameScripted/Gadgets", description: "TDL radio gadget", color: "0 0 255 255")]
class AG0_TDLDeviceComponentClass : ScriptGameComponentClass
{
}

//IF you are reading this sadness. There are a couple things I would like to write in my pity diary.
//1. Players holding a radio do not own the radio.
//2. Dialogs must be rendered on client ONLY, otherwise will hang players/crash server.
//3. User Actions are done on client and server, but client can't tell the server to do shit.
//In light of the above, we have to execute user action on server, give ownership to calling user entity (RplIdentity NOT RplId)
//Then when they have ownership, they can RPC input back to server.
//Server can regain ownership and replicate radio props.
//This is so much pain for a simple feature.


class AG0_TDLDeviceComponent : ScriptGameComponent
{
	[RplProp(onRplName: "OnNetworkIDReplicated")]
	protected int m_iCurrentNetworkID = -1;
	
	[RplProp()]
	protected ref array<RplId> m_mConnectedMembers = new array<RplId>();
	
	[RplProp()]
    protected bool m_bIsPowered = true;
	
	// Video source state (for broadcasters)
	[RplProp(onRplName: "OnCameraBroadcastingChanged")]
	protected bool m_bCameraBroadcasting = false;
	
	[RplProp(onRplName: "OnActiveVideoSourceChanged")]  
	protected RplId m_ActiveVideoSourceRplId = RplId.Invalid();
	
	[RplProp()]
    protected RplId m_PreferredVideoSource = RplId.Invalid();
	
	[RplProp(onRplName: "OnCustomCallsignChanged")]
	protected string m_sCustomCallsign = "";
	
	[Attribute
    (
        category: "TDL Device Config",
        desc: "",
        uiwidget: UIWidgets.Flags,
        enums: ParamEnumArray.FromEnum(AG0_ETDLDeviceCapability),
        defvalue: AG0_ETDLDeviceCapability.NETWORK_ACCESS.ToString()
    )]
	protected AG0_ETDLDeviceCapability m_eCapabilities;
	
	[Attribute("1", UIWidgets.CheckBox, category: "Network Config")]
	protected bool m_bUseTDLRadio;
	
	[Attribute("250.0", UIWidgets.EditBox, category: "Network Config")]
	protected float m_fNetworkRange;
	
	[Attribute("", UIWidgets.Auto, category: "Video Source Config")]
	ref PointInfo m_CameraAttachment; //unprotected for a test
	
	[Attribute("60", UIWidgets.Slider, "Camera FOV", "10 120 1", category: "Video Source Config")]
	protected float m_fCameraFOV;
	
	[Attribute("15", UIWidgets.Slider, "Camera Index", "0 31 1", category: "Video Source Config")]
	protected int m_iCameraIndex;
	
	[Attribute("0.05", UIWidgets.EditBox, "Near plane clip", category: "Video Source Config")]
	protected float m_fCameraNearPlane;
	
	[Attribute("4000", UIWidgets.EditBox, "Far plane clip", category: "Video Source Config")]
	protected float m_fCameraFarPlane;
	
	[Attribute("", UIWidgets.Auto, category: "Video Source Config")]
	protected ref array<ref AG0_PostProcessEffect> m_aCameraEffects;
	
	[Attribute("1", UIWidgets.CheckBox, category: "Video Source Config")]
	protected bool m_bEnableNightVisionMode;
	
	[Attribute("15", UIWidgets.Slider, "Display Camera Index", "0 31 1", category: "Video Display Config")]
	protected int m_iDisplayCameraIndex;
	
	// RPC infrastructure
    protected RplComponent rplComp;
    protected RplIdentity mfkerRplIdentity;
    protected RplIdentity serverRplIdentity;
	
	
	protected RplId m_LocalActiveVideoSource = RplId.Invalid();
	
    // Dialog state
    protected ref AG0_TDL_LoginDialog m_networkDialog;
    protected EditBoxWidget m_networkNameEdit;
    protected EditBoxWidget m_networkPasswordEdit;
    protected bool m_bCreateNetworkMode = false;
	
	protected bool m_bWasHeldPreviously = false;
    protected bool m_bCapabilitiesActive = true;
	
	
	//Replication hack - trying to prevent VME...
	protected bool m_bLeavingNetwork = false;



	
	override protected bool OnTicksOnRemoteProxy() { return true; } //This is DUMB
	
	override void OnPostInit(IEntity owner)
	{
	    super.OnPostInit(owner);
	    
	    // Register for events your component actually needs
	    SetEventMask(owner, EntityEvent.INIT | EntityEvent.POSTFRAME);
	    
	}
	
	override void EOnInit(IEntity owner)
	{
	    super.EOnInit(owner);
	    rplComp = RplComponent.Cast(owner.FindComponent(RplComponent));
	    
	    if (rplComp && Replication.IsServer()) {
	        serverRplIdentity = RplIdentity.Local();
	    }
		
	    // Register with TDL system if we can access network
	    if (CanAccessNetwork() && Replication.IsServer()) {
	        GetGame().GetCallqueue().CallLater(RegisterWithTDLSystem, 2500, false);
	    }
		
		if (HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE) && m_CameraAttachment)
		{
		    m_CameraAttachment.Init(owner);
		}
	}
	
	override void EOnPostFrame(IEntity owner, float timeSlice)
	{
	    super.EOnPostFrame(owner, timeSlice);
	    
	    // Ask controller if we're held
	    SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
	    if (!controller || !controller.IsHoldingDevice(owner))
	        return;
	    
	    // Use GetActiveVideoSource() which returns local source
	    RplId activeSource = GetActiveVideoSource();
	    if (activeSource == RplId.Invalid())
	        return;
	    
	    UpdateDisplayTransform();
	}
	
	override void OnDelete(IEntity owner)
	{
		
		// Unregister from PC if broadcasting
        if (!System.IsConsoleApp() && m_bCameraBroadcasting)
        {
            SCR_PlayerController controller = SCR_PlayerController.Cast(
			    GetGame().GetPlayerController()
			);
            if (controller)
            {
                RplId myId = GetDeviceRplId();
                if (myId != RplId.Invalid())
                    controller.UnregisterBroadcastingDevice(myId);
            }
        }
	    
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system)
	    {
	        system.UnregisterDevice(this);
	    }
	    super.OnDelete(owner);
	}
	
	void RegisterWithTDLSystem()
	{
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system) system.RegisterDevice(this);
	}
	
	protected void OnOpenTDLMenu()
	{
	    // Only open if device has required capabilities
	    if (!IsPowered() || !HasCapability(AG0_ETDLDeviceCapability.INFORMATION))
	        return;
	    
	    if (!HasCapability(AG0_ETDLDeviceCapability.DISPLAY_OUTPUT))
	        return;
	    
	    // Open the TDL menu
	    GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.AG0_TDLMenu);
	}
    
    protected void OnDeviceDropped()
    {
        // Power providers stay active when dropped
        if (HasCapability(AG0_ETDLDeviceCapability.POWER_PROVIDER)) return;
        
        // Everything else gets deactivated
        DeactivateAllCapabilities();
        
        // Network providers leave the network entirely
        if (HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS) && IsInNetwork()) {
            LeaveNetworkTDL();
        }
    }
    
    protected void DeactivateAllCapabilities()
    {
        m_bCapabilitiesActive = false;
        // Specific capability shutdown logic here
    }
	
	//------------------------------------------------------------------------------------------------
	// Camera Broadcasting API (Server-side)
	//------------------------------------------------------------------------------------------------
	void SetCameraBroadcasting(bool broadcasting)
	{
	    if (!Replication.IsServer()) return;
	    
	    // Only allow broadcasting if device has VIDEO_SOURCE capability
	    if (broadcasting && !HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
	    {
	        PrintFormat("TDL_VIDEO: Device %1 cannot broadcast - no VIDEO_SOURCE capability", GetOwner(), LogLevel.DEBUG);
	        return;
	    }
	    
	    if (m_bCameraBroadcasting != broadcasting)
	    {
	        m_bCameraBroadcasting = broadcasting;
			if(Replication.IsServer() && !System.IsConsoleApp()) // In singleplayer specifically because client is the server.
				OnCameraBroadcastingChanged();
	        
	        Replication.BumpMe();
	        
	        PrintFormat("TDL_VIDEO: Device %1 broadcasting: %2", GetOwner(), broadcasting, LogLevel.DEBUG);
	        
	        // Notify system of state change
	        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	        if (system)
	            system.OnVideoBroadcastChanged(this);
	    }
	}
	
	bool IsCameraBroadcasting() { return m_bCameraBroadcasting; }
	
	//------------------------------------------------------------------------------------------------
	// Video Source Management API (Server-side)
	
	void SetActiveVideoSource(RplId broadcasterRplId)
	{
	    // This now works locally on client for display purposes
	    m_LocalActiveVideoSource = broadcasterRplId;
	    m_PreferredVideoSource = broadcasterRplId; // Track preference locally
	    
	    Print(string.Format("TDL_VIDEO: Client set active video source to %1", broadcasterRplId), LogLevel.DEBUG);
	    
	    // Clear cache to force fresh lookup
	    m_CachedBroadcaster = null;
	    
	    // Immediately set up display if we're holding this device
	    SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
	    if (controller && controller.IsHoldingDevice(GetOwner()))
	    {
	        SetupDisplayOutput();
	    }
	}
	
	array<RplId> GetAvailableVideoSources() 
    { 
        // Ask PC instead of maintaining local array
        SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
        if (controller)
            return controller.GetAvailableVideoSources();
        return {};
    }
	
	RplId GetActiveVideoSource() 
	{ 
	    // Use local selection if set, otherwise first available
	    if (m_LocalActiveVideoSource != RplId.Invalid())
	        return m_LocalActiveVideoSource;
	    
	    // Fallback to first available
	   	SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
	    if (controller)
	    {
	        array<RplId> sources = controller.GetAvailableVideoSources();
	        if (!sources.IsEmpty())
	            return sources[0];
	    }
	    
	    return RplId.Invalid();
	}
	
	RplId GetPreferredVideoSource() { return m_PreferredVideoSource; }
	
	//------------------------------------------------------------------------------------------------
	// DISPLAY SIDE - Receiving broadcast feeds
	//------------------------------------------------------------------------------------------------
	protected void OnCameraBroadcastingChanged()
    {
        Print(string.Format("TDL_VIDEO: Broadcasting changed to %1 on device %2", m_bCameraBroadcasting, GetOwner()), LogLevel.DEBUG);
        
        // On client, register/unregister with PC
        if (!System.IsConsoleApp())
        {
            SCR_PlayerController controller = SCR_PlayerController.Cast(
			    GetGame().GetPlayerController()
			);
            if (controller)
            {
                RplId myId = GetDeviceRplId();
                if (myId != RplId.Invalid())
                {
                    if (m_bCameraBroadcasting)
                        controller.RegisterBroadcastingDevice(myId);
                    else
                        controller.UnregisterBroadcastingDevice(myId);
                }
            }
        }
    }
	
	protected void OnVideoSourcesChanged()
	{
	    //Print(string.Format("TDL_VIDEO: Video sources changed - now have %1 sources", m_aAvailableVideoSources.Count()), LogLevel.DEBUG);
	}
	
	protected void OnActiveVideoSourceChanged()
	{
	    // This is called when the server replicates a change
	    // We can ignore it since we're managing display locally
	    Print(string.Format("TDL_VIDEO: Server video source changed to %1 (ignored for local display)", m_ActiveVideoSourceRplId), LogLevel.DEBUG);
	}
	
	protected void SetupDisplayOutput()
	{
	    if (System.IsConsoleApp()) return;
	    Print("TDL_VIDEO: SetupDisplayOutput", LogLevel.DEBUG);
	    
	    BaseWorld world = GetGame().GetWorld();
	    if (!world) return;
	    
	    int displayCameraIndex = m_iDisplayCameraIndex;
	    
	    // Use local active source
	    RplId activeSource = GetActiveVideoSource();
	    if (activeSource == RplId.Invalid())
	    {
	        Print("TDL_VIDEO: SetupDisplayOutput - No active source", LogLevel.WARNING);
	        return;
	    }
	    
	    RplComponent videoRplComp = RplComponent.Cast(Replication.FindItem(activeSource));
	    if (!videoRplComp) return;
	    
	    IEntity broadcasterEntity = videoRplComp.GetEntity();
	    if (!broadcasterEntity) return;
	    
	    AG0_TDLDeviceComponent broadcasterDevice = AG0_TDLDeviceComponent.Cast(
	        broadcasterEntity.FindComponent(AG0_TDLDeviceComponent));
	    
	    Print(string.Format("TDL_VIDEO: Broadcaster device is %1", broadcasterDevice), LogLevel.DEBUG);
	    
	    if (!broadcasterDevice) return;
	    
	    // Configure display properties from broadcaster
	    world.SetCameraType(displayCameraIndex, CameraType.PERSPECTIVE);
	    world.SetCameraVerticalFOV(displayCameraIndex, broadcasterDevice.m_fCameraFOV);
	    world.SetCameraFarPlane(displayCameraIndex, broadcasterDevice.m_fCameraFarPlane);
	    world.SetCameraNearPlane(displayCameraIndex, broadcasterDevice.m_fCameraNearPlane);
	    world.SetCameraLensFlareSet(displayCameraIndex, CameraLensFlareSetType.FirstPerson, string.Empty);
	    
	    // Apply broadcaster's post-process effects to our display
	    broadcasterDevice.ApplyDisplayEffects(displayCameraIndex);
	    
	    Print(string.Format("TDL_VIDEO: Display output configured on camera %1 for source %2", displayCameraIndex, activeSource), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// BROADCAST SIDE - Camera management for broadcasting
	//------------------------------------------------------------------------------------------------
	
	void ApplyBroadcastEffects(int cameraIndex)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world) return;
		
		// Clear existing effects first
		for (int i = 0; i < 20; i++) {
			world.SetCameraPostProcessEffect(cameraIndex, i, PostProcessEffectType.None, "");
		}
		
		// Configure HDR for broadcast
		//world.SetCameraHDRBrightness(cameraIndex, 5);
		//This should be done with post process effect not with script.
		
		// Apply configured effects
		foreach (AG0_PostProcessEffect effect : m_aCameraEffects) {
			if (!effect.m_sMaterialPath.IsEmpty()) {
				world.SetCameraPostProcessEffect(cameraIndex, effect.m_iPriority, effect.m_eEffectType, effect.m_sMaterialPath);
				Print(string.Format("TDL_VIDEO: Applied broadcast effect %1 at priority %2", effect.m_eEffectType, effect.m_iPriority), LogLevel.DEBUG);
			}
		}
		
		// Apply preset effects
		if (m_bEnableNightVisionMode)
			ApplyNightVisionEffects(cameraIndex);
	}
	
	// Renamed for clarity - this applies effects to display cameras showing our broadcast
	void ApplyDisplayEffects(int displayCameraIndex)
	{
		// Same implementation as ApplyBroadcastEffects, but semantically different
		ApplyBroadcastEffects(displayCameraIndex);
	}
	
	protected void ApplyNightVisionEffects(int cameraIndex)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world) return;
		
		// Basic NVG stack
		//world.SetCameraPostProcessEffect(cameraIndex, 15, PostProcessEffectType.ColorGrading, "");
		//world.SetCameraPostProcessEffect(cameraIndex, 16, PostProcessEffectType.FilmGrain, "");
		
		Print("TDL_VIDEO: Applied night vision effects", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// TRANSFORM UPDATES - Clean separation of broadcast vs display
	//------------------------------------------------------------------------------------------------

	protected IEntity m_CachedBroadcaster;
	protected float m_fTransformUpdateTimer = 0;
	
	protected bool UpdateDisplayTransform()
	{
	    // Use local active source instead of replicated one
	    RplId activeSource = GetActiveVideoSource(); // This now returns m_LocalActiveVideoSource
	    
	    if (activeSource == RplId.Invalid())
	    {
	        Print("TDL_VIDEO: UpdateDisplayTransform - No active source", LogLevel.DEBUG);
	        return false;
	    }
	    
	    if (m_CachedBroadcaster && !m_CachedBroadcaster.IsDeleted())
	    {
	        AG0_TDLDeviceComponent broadcasterDevice = AG0_TDLDeviceComponent.Cast(
	            m_CachedBroadcaster.FindComponent(AG0_TDLDeviceComponent));
	        if (broadcasterDevice && broadcasterDevice.m_CameraAttachment)
	        {
	            // Use cached entity for transform
	            vector displayTransform[4];
	            vector entityTransform[4], localOffset[4];
	            m_CachedBroadcaster.GetWorldTransform(entityTransform);
	            broadcasterDevice.m_CameraAttachment.GetLocalTransform(localOffset);
	            Math3D.MatrixMultiply4(entityTransform, localOffset, displayTransform);
	            GetGame().GetWorld().SetCameraEx(m_iDisplayCameraIndex, displayTransform);
	            return true;
	        }
	    }
	    
	    // Cache miss or invalid - do fresh lookup
	    Print(string.Format("TDL_VIDEO: UpdateDisplayTransform looking for RplId %1", activeSource), LogLevel.DEBUG);
	    
	    RplComponent videoRplComp = RplComponent.Cast(Replication.FindItem(activeSource));
	    if (!videoRplComp) {
	        Print("TDL_VIDEO: UpdateDisplayTransform - No RplComponent found", LogLevel.WARNING);
	        m_CachedBroadcaster = null;
	        return false;
	    }
	    
	    m_CachedBroadcaster = videoRplComp.GetEntity();
	    if (!m_CachedBroadcaster) {
	        Print("TDL_VIDEO: UpdateDisplayTransform - No entity from RplComponent", LogLevel.WARNING);
	        return false;
	    }
	    
	    Print(string.Format("TDL_VIDEO: UpdateDisplayTransform found broadcaster entity %1", m_CachedBroadcaster), LogLevel.DEBUG);
	    
	    // Fresh component lookup
	    AG0_TDLDeviceComponent broadcasterDevice = AG0_TDLDeviceComponent.Cast(
	        m_CachedBroadcaster.FindComponent(AG0_TDLDeviceComponent));
	    if (!broadcasterDevice || !broadcasterDevice.m_CameraAttachment) {
	        Print("TDL_VIDEO: UpdateDisplayTransform - No broadcaster device or camera attachment", LogLevel.WARNING);
	        return false;
	    }
	    
	    // Get transform
	    vector displayTransform[4];
	    vector entityTransform[4], localOffset[4];
	    m_CachedBroadcaster.GetWorldTransform(entityTransform);
	    broadcasterDevice.m_CameraAttachment.GetLocalTransform(localOffset);
	    Math3D.MatrixMultiply4(entityTransform, localOffset, displayTransform);
	    GetGame().GetWorld().SetCameraEx(m_iDisplayCameraIndex, displayTransform);
	    
	    Print("TDL_VIDEO: UpdateDisplayTransform - Transform applied successfully", LogLevel.DEBUG);
	    return true;
	}
	
//
//
// TDL LOGIN BELOW
//
//
	
	
	//------------------------------------------------------------------------------------------------
	// Public methods to be called by the User Action
	//------------------------------------------------------------------------------------------------
	void CreateNetworkDialog(IEntity userEntity)
	{
	    m_bCreateNetworkMode = true;
	    OpenNetworkDialog(userEntity);
	}
	
	void JoinNetworkDialog(IEntity userEntity)
	{
	    m_bCreateNetworkMode = false;
	    OpenNetworkDialog(userEntity);
	}
	
	//------------------------------------------------------------------------------------------------
	void OpenNetworkDialog(IEntity userEntity)
	{
	    if (!rplComp || !Replication.IsServer())
	    {
	        //Print("AG0_TDLRadioComponent: Cannot open network dialog - not the server.", LogLevel.WARNING);
	        return;
	    }
	    
	    PlayerManager playerManager = GetGame().GetPlayerManager();
	    if (playerManager)
	    {
	        int userPlayerId = playerManager.GetPlayerIdFromControlledEntity(userEntity);
	        SCR_PlayerController playerController = SCR_PlayerController.Cast(playerManager.GetPlayerController(userPlayerId));
	        if(!playerController) {
	            return;
	        }
	        
	        mfkerRplIdentity = playerController.GetRplIdentity();
	        
	        //PrintFormat("Giving ownership from %1 to %2", serverRplIdentity, mfkerRplIdentity);
	        rplComp.GiveExt(mfkerRplIdentity, true);
	        
	        Replication.BumpMe();
	        
	        GetGame().GetCallqueue().CallLater(WaitForNetworkDialogOwnership, 1000, false);
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	void WaitForNetworkDialogOwnership()
	{
	    Rpc(RpcDo_OpenNetworkDialog, m_bCreateNetworkMode);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_OpenNetworkDialog(bool createMode)
	{
	    if(System.IsConsoleApp())
	        return; // Skip on dedicated server
	        
	    //Print(string.Format("AG0_TDLRadioComponent: Opening network dialog in %1 mode", createMode), LogLevel.DEBUG);
	    
	    // Store the mode locally
	    m_bCreateNetworkMode = createMode;
	    
	    // Create dialog
	    string dialogTitle = "JOIN TDL NETWORK";
	    if(createMode) {
			dialogTitle = "CREATE TDL NETWORK";
		}
		m_networkDialog = AG0_TDL_LoginDialog.CreateLoginDialog("Enter network credentials", dialogTitle);
	    
	    // Check if dialog creation failed
	    if (!m_networkDialog || !m_networkDialog.GetRootWidget())
	    {
	        Print("AG0_TDLRadioComponent: Failed to create network dialog", LogLevel.ERROR);
	        m_networkDialog = null;
	        return;
	    }
	    
	    // Hook up callbacks
	    m_networkDialog.m_OnConfirm.Insert(OnNetworkDialogConfirm);
	    m_networkDialog.m_OnCancel.Insert(OnNetworkDialogCancel);
	    
	    // Get the input fields
	    m_networkNameEdit = EditBoxWidget.Cast(m_networkDialog.GetRootWidget().FindAnyWidget("NetworkNameInput"));
	    m_networkPasswordEdit = EditBoxWidget.Cast(m_networkDialog.GetRootWidget().FindAnyWidget("NetworkPasswordInput"));
	    
	    if (!m_networkNameEdit || !m_networkPasswordEdit)
	    {
	        //Print("AG0_TDLRadioComponent: Could not find input fields", LogLevel.ERROR);
	        m_networkDialog.Close();
	        m_networkDialog = null;
	        return;
	    }
	    
	    // Focus the name input
	    GetGame().GetWorkspace().SetFocusedWidget(m_networkNameEdit);
	    m_networkNameEdit.ActivateWriteMode();
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnNetworkDialogConfirm(SCR_ConfigurableDialogUi dialog)
	{
	    Print("OnNetworkDialogConfirm FIRED", LogLevel.NORMAL);
    
	    AG0_TDL_LoginDialog loginDialog = AG0_TDL_LoginDialog.Cast(dialog);
	    if (!loginDialog) {
	        Print("CAST FAILED - dialog is not AG0_TDL_LoginDialog!", LogLevel.ERROR);
	        return;
	    }
	    
	    string networkName = loginDialog.GetNetworkName();
	    string networkPassword = loginDialog.GetNetworkPassword();
	    
	    Print(string.Format("Got values: name='%1' pass='%2'", networkName, networkPassword), LogLevel.NORMAL);

	    
	    if (networkName.IsEmpty()) return;  // Only check you probably want
	    
	    if (m_bCreateNetworkMode)
	        Rpc(RpcAsk_CreateNetworkTDL, networkName, networkPassword);
	    else
	        Rpc(RpcAsk_JoinNetworkTDL, networkName, networkPassword);
	    
	    CleanupNetworkDialogRefs();
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnNetworkDialogCancel(SCR_ConfigurableDialogUi dialog)
	{
	    // Should already be on client
	    if (!GetGame().GetPlayerController()) return;
	    
	    //Print("AG0_TDLRadioComponent: Network dialog cancelled (Client).", LogLevel.DEBUG);
	    CleanupNetworkDialogRefs();
	    Rpc(RpcInform_NetworkDialogCancel);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcInform_NetworkDialogCancel()
	{
	    // Return ownership to server
	    if (rplComp && serverRplIdentity)
	    {
	        //Print(string.Format("AG0_TDLRadioComponent: Returning ownership to server %1", serverRplIdentity), LogLevel.DEBUG);
	        rplComp.GiveExt(serverRplIdentity, true);
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	protected void CleanupNetworkDialogRefs()
	{
	    if (m_networkDialog)
	    {
	        m_networkDialog = null;
	    }
	    m_networkNameEdit = null;
	    m_networkPasswordEdit = null;
	}
	
	
	
	//------------------------------------------------------------------------------------------------
	// Network management methods
	//------------------------------------------------------------------------------------------------
	bool CreateNetworkTDL(string networkName, string password)
	{
	    if (!Replication.IsServer())
	    {
	        Rpc(RpcAsk_CreateNetworkTDL, networkName, password);
	        return true;
	    }
	    
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system)
	    {
	        int networkID = system.CreateNetwork(this, networkName, password);
	        return networkID > 0;
	    }
	    
	    return false;
	}
	
	bool JoinNetworkTDL(string networkName, string password)
	{
	    if (!Replication.IsServer())
	    {
	        Rpc(RpcAsk_JoinNetworkTDL, networkName, password);
	        return true;
	    }
	    
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system)
	    {
	        return system.JoinNetwork(this, networkName, password);
	    }
	    
	    return false;
	}
	
	bool LeaveNetworkTDL()
	{
	    if (!Replication.IsServer())
	    {
	        Rpc(RpcAsk_LeaveNetworkTDL);
	        return true;
	    }
	    
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system)
	    {
	        system.LeaveNetwork(this);
	        return true;
	    }
	    
	    return false;
	}
	
	//RPCs
	
	//------------------------------------------------------------------------------------------------
	// RPC Methods - Client to Server
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_CreateNetworkTDL(string networkName, string password)
	{
	    CreateNetworkTDL(networkName, password);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_JoinNetworkTDL(string networkName, string password)
	{
	    JoinNetworkTDL(networkName, password);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_LeaveNetworkTDL()
	{
	    LeaveNetworkTDL();
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_NotifyNetworkJoined(int networkID, array<RplId> members) { 
	    m_iCurrentNetworkID = networkID;
	    m_mConnectedMembers = members;
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]  
	protected void RpcDo_NotifyNetworkLeft(int networkID) {
	    m_iCurrentNetworkID = -1;
	    m_mConnectedMembers.Clear();
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_NotifyConnectivityUpdated(array<RplId> members) {
	    m_mConnectedMembers = members;
	}
	
	//------------------------------------------------------------------------------------------------
	// System callback methods (called by AG0_TDLSystem)
	//------------------------------------------------------------------------------------------------
	void OnNetworkJoined(int networkID, array<RplId> radioIDs)
	{
	    m_iCurrentNetworkID = networkID;
	    
	    // Debug logging
	    string ownerName = "UNKNOWN";
	    if (GetOwner())
	        ownerName = GetOwner().ToString();
	    
	    Print(string.Format("TDL_NETWORK_JOIN: %1 (%2) joined network %3 with %4 members", 
	        ownerName, GetOwnerPlayerName(), networkID, radioIDs.Count()), LogLevel.DEBUG);
	    
	    foreach (RplId radioId : radioIDs)
	    {
	        Print(string.Format("  Network member RplId: %1", radioId), LogLevel.DEBUG);
	    }
	    
	    // RPC to client with serializable array
	    Rpc(RpcDo_NotifyNetworkJoined, networkID, radioIDs);
	}
	
	void OnNetworkLeft()
	{   
		m_bLeavingNetwork = true;
	    // Debug logging
	    string ownerName = "UNKNOWN";
	    if (GetOwner())
	        ownerName = GetOwner().ToString();
	    
	    Print(string.Format("TDL_NETWORK_LEAVE: %1 (%2) left network %3", 
	        ownerName, GetOwnerPlayerName(), m_iCurrentNetworkID), LogLevel.DEBUG);
	    
	    // RPC to client if needed
	    if (!m_bLeavingNetwork)
        	Rpc(RpcDo_NotifyNetworkLeft, m_iCurrentNetworkID);
	    m_iCurrentNetworkID = -1;
		m_bLeavingNetwork = false;
	}
	
	void OnNetworkConnectivityUpdated(array<RplId> members)
	{
	    m_mConnectedMembers = members;
	    // Debug logging
	    string ownerName = "UNKNOWN";
	    if (GetOwner())
	        ownerName = GetOwner().ToString();
	    
	    Print(string.Format("TDL_CONNECTIVITY_UPDATE: %1 now connected to %2 members", 
	        ownerName, members.Count()), LogLevel.DEBUG);
	    
	    foreach (RplId memberId : members)
	    {
	        Print(string.Format("  Connected to RplId: %1", memberId), LogLevel.DEBUG);
	    }
	    
	    Rpc(RpcDo_NotifyConnectivityUpdated, members);
	}
	
	protected void OnNetworkIDReplicated()
	{
	    if (m_iCurrentNetworkID > 0)
	    {
	        //Print(string.Format("AG0_TDLRadioComponent: Network ID updated to %1", m_iCurrentNetworkID), LogLevel.DEBUG);
	    }
	    else
	    {
	        //Print("AG0_TDLRadioComponent: Not in a network", LogLevel.DEBUG);
	    }
	}
	
	
//------------------------------------------------------------------------------------------------
//
// PUBLIC API BELOW
//
//------------------------------------------------------------------------------------------------
	
	bool HasCapability(AG0_ETDLDeviceCapability cap) { return (m_eCapabilities & cap) != 0; }
	
	float GetEffectiveNetworkRange()
    {
        if (m_bUseTDLRadio) {
            AG0_TDLRadioComponent radio = FindRadioInInventory();
            if(!radio)
				return 0;
			return radio.GetNetworkRange();
        }
        return m_fNetworkRange;
    }
    
    bool CanAccessNetwork() { return GetEffectiveNetworkRange() > 0; }
	
	
	AG0_TDLRadioComponent FindRadioInInventory()
	{
	    if (!HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS)) return null;
	    
		AG0_TDLRadioComponent selfRadio = AG0_TDLRadioComponent.Cast(GetOwner().FindComponent(AG0_TDLRadioComponent));
	    if (selfRadio) return selfRadio;
		
	    IEntity owner = GetOwner();
	    SCR_InventoryStorageManagerComponent storage = SCR_InventoryStorageManagerComponent.Cast(owner.FindComponent(SCR_InventoryStorageManagerComponent));
	    if (!storage) return null;
	    
	    array<IEntity> radios = {};
	    AG0_TDLRadioPredicate predicate = new AG0_TDLRadioPredicate();
	    storage.FindItems(radios, predicate);
	    
		if(radios.Count() > 0)
			return AG0_TDLRadioComponent.Cast(radios[0].FindComponent(AG0_TDLRadioComponent));
		
		return null;
	}
	
	string GetOwnerPlayerName()
	{
	    // Walk up ownership chain to find player
	    IEntity owner = GetOwner();
	    while (owner)
	    {
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(owner);
	        
	        #ifdef WORKBENCH
	            // In Workbench, player ID 0 is valid
	            if (playerId >= 0)
	                return playerMgr.GetPlayerName(playerId);
	        #else
	            // In game, player ID 0 is invalid
	            if (playerId != 0)
	                return playerMgr.GetPlayerName(playerId);
	        #endif
	        
	        owner = owner.GetParent();
	    }
	    return "Unknown";
	}
    
	bool IsHeldByAnyPlayer()
	{
	    IEntity owner = GetOwner();
	    
	    while (owner) {
	        PlayerManager playerMgr = GetGame().GetPlayerManager();
	        int playerId = playerMgr.GetPlayerIdFromControlledEntity(owner);
	        
	        #ifdef WORKBENCH
	            if (playerId >= 0) return true;
	        #else
	            if (playerId != 0) return true;
	        #endif
	        
	        owner = owner.GetParent();
	    }
	    return false;
	}
	
	// Updated capability logic
	int GetActiveCapabilities()
	{
	    if (!IsPowered()) return 0;
	    
	    // Server/client agnostic check
	    if (!IsHeldByAnyPlayer() && !HasCapability(AG0_ETDLDeviceCapability.POWER_PROVIDER)) 
	        return 0;
	        
	    return m_eCapabilities;
	}
    
    bool IsPowered() { return m_bIsPowered; }
    
    void SetPowered(bool powered)
    {
        if (!Replication.IsServer()) return;
        m_bIsPowered = powered;
        Replication.BumpMe();
    }
	
	bool IsInNetwork() { return m_iCurrentNetworkID > 0; }
	int GetCurrentNetworkID() { return m_iCurrentNetworkID; }
	array<RplId> GetConnectedMembers() { return m_mConnectedMembers; }
	
	RplId GetDeviceRplId()
	{
		if(!rplComp)
			return RplId.Invalid();
	    return rplComp.Id();
	}
	
	RplId GetActiveFeedBroadcaster() { return m_ActiveVideoSourceRplId; }
	
	int GetDisplayCameraIndex() { return m_iDisplayCameraIndex; }
	
	void SetDisplayCameraIndex(int index) { 
		m_iDisplayCameraIndex = index; 
		// If we have active feed, reconfigure display
		if (HasActiveVideoFeed()) {
			SetupDisplayOutput();
		}
	}
	
	// Broadcast Query Methods (for camera-equipped devices)
	// Simplified capability check - remove circular logic
	bool IsBroadcastCapable() 
	{ 
	    return HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE) && 
	           IsPowered() &&
	           m_CameraAttachment; 
	}
	
	// Clear active video feed check
	bool HasActiveVideoFeed() 
	{
	    if (m_ActiveVideoSourceRplId == RplId.Invalid()) 
	        return false;
	        
	    SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
	    if (!controller) return false;
	    
	    return controller.IsVideoSourceAvailable(m_ActiveVideoSourceRplId);
	}
	
	float GetCameraFOV() { return m_fCameraFOV; }
	float GetCameraNearPlane() { return m_fCameraNearPlane; }
	float GetCameraFarPlane() { return m_fCameraFarPlane; }
	
	
	//------------------------------------------------------------------------------------------------
	// Public API to check if device has network member data
	//------------------------------------------------------------------------------------------------
	bool HasNetworkMemberData()
	{
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    return data && data.Count() > 0;
	}
		
	AG0_TDLNetworkMembers GetNetworkMembersData()
	{
	    if (!HasCapability(AG0_ETDLDeviceCapability.INFORMATION))
	        return null;
	        
	    SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
	    if (!controller) return null;
	    
	    // Hybrid approach - specific network or aggregate
	    if (m_iCurrentNetworkID > 0)
	        return controller.GetTDLNetworkMembers(m_iCurrentNetworkID);
	    else
	        return controller.GetAggregatedTDLMembers();
	}
	
	//------------------------------------------------------------------------------------------------
	// CLIENT-SIDE NETWORK MEMBER DATA ACCESS HELPERS
	//------------------------------------------------------------------------------------------------
	
	// Get all network members as a convenient map
	map<RplId, ref AG0_TDLNetworkMember> GetNetworkMembersMap()
	{
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) 
	        return new map<RplId, ref AG0_TDLNetworkMember>();
	    return data.ToMap();
	}
	
	// Get a specific member by RplId
	AG0_TDLNetworkMember GetNetworkMember(RplId rplId)
	{
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) return null;
	    return data.GetByRplId(rplId);
	}
	
	// Get all members with a specific capability
	array<ref AG0_TDLNetworkMember> GetMembersWithCapability(AG0_ETDLDeviceCapability capability)
	{
	    array<ref AG0_TDLNetworkMember> result = {};
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) return result;
	    
	    map<RplId, ref AG0_TDLNetworkMember> members = data.ToMap();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : members)
	    {
	        if ((member.GetCapabilities() & capability) != 0)
	            result.Insert(member);
	    }
	    return result;
	}
	
	// Get members sorted by distance from a position
	array<ref AG0_TDLNetworkMember> GetMembersSortedByDistance(vector fromPosition)
	{
	    array<ref AG0_TDLNetworkMember> result = {};
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) return result;
	    
	    map<RplId, ref AG0_TDLNetworkMember> members = data.ToMap();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : members)
	    {
	        result.Insert(member);
	    }
	    
	    // Simple bubble sort by distance (ascending)
	    for (int i = 0; i < result.Count() - 1; i++)
	    {
	        for (int j = 0; j < result.Count() - i - 1; j++)
	        {
	            float distJ = vector.Distance(fromPosition, result[j].GetPosition());
	            float distJ1 = vector.Distance(fromPosition, result[j + 1].GetPosition());
	            
	            if (distJ > distJ1)
	            {
	                AG0_TDLNetworkMember temp = result[j];
	                result[j] = result[j + 1];
	                result[j + 1] = temp;
	            }
	        }
	    }
	    
	    return result;
	}
	
	// Get members sorted by signal strength (strongest first)
	array<ref AG0_TDLNetworkMember> GetMembersSortedBySignal()
	{
	    array<ref AG0_TDLNetworkMember> result = {};
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) return result;
	    
	    map<RplId, ref AG0_TDLNetworkMember> members = data.ToMap();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : members)
	    {
	        result.Insert(member);
	    }
	    
	    // Simple bubble sort by signal strength (descending)
	    for (int i = 0; i < result.Count() - 1; i++)
	    {
	        for (int j = 0; j < result.Count() - i - 1; j++)
	        {
	            if (result[j].GetSignalStrength() < result[j + 1].GetSignalStrength())
	            {
	                AG0_TDLNetworkMember temp = result[j];
	                result[j] = result[j + 1];
	                result[j + 1] = temp;
	            }
	        }
	    }
	    
	    return result;
	}
	
	// Get network statistics for UI display
	void GetNetworkStatistics(out int totalMembers, out int gpsProviders, out int videoSources, 
                         out int displays, out float avgSignalStrength)
	{
	    totalMembers = 0;
	    gpsProviders = 0;
	    videoSources = 0;
	    displays = 0;
	    avgSignalStrength = 0;
	    
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) return;
	    
	    map<RplId, ref AG0_TDLNetworkMember> members = data.ToMap();
	    totalMembers = members.Count();
	    
	    if (totalMembers == 0) return;
	    
	    float totalSignal = 0;
	    foreach (RplId rplId, AG0_TDLNetworkMember member : members)
	    {
	        totalSignal += member.GetSignalStrength();
	        
	        int caps = member.GetCapabilities();
	        if ((caps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0)
	            gpsProviders++;
	        if ((caps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0)
	            videoSources++;
	        if ((caps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0)
	            displays++;
	    }
	    
	    avgSignalStrength = totalSignal / totalMembers;
	}
	
	// Get formatted member info for UI display
	string GetMemberDisplayInfo(RplId rplId, bool includeCapabilities = true)
	{
	    AG0_TDLNetworkMember member = GetNetworkMember(rplId);
	    if (!member) return "Unknown Member";
	    
	    string info = string.Format("%1 (IP: %2)", 
	        member.GetPlayerName(), 
	        member.GetNetworkIP());
	    
	    // Add signal strength indicator
	    float signal = member.GetSignalStrength();
	    string signalBar = "";
	    if (signal >= 80) signalBar = "████";
	    else if (signal >= 60) signalBar = "███░";
	    else if (signal >= 40) signalBar = "██░░";
	    else if (signal >= 20) signalBar = "█░░░";
	    else signalBar = "░░░░";
	    
	    info += string.Format(" %1 %.0f%%", signalBar, signal);
	    
	    if (includeCapabilities)
	    {
	        string capStr = "";
	        int caps = member.GetCapabilities();
	        
	        if ((caps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0) capStr += "[GPS]";
	        if ((caps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0) capStr += "[CAM]";
	        if ((caps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0) capStr += "[DISP]";
	        if ((caps & AG0_ETDLDeviceCapability.INFORMATION) != 0) capStr += "[INFO]";
	        
	        if (!capStr.IsEmpty())
	            info += " " + capStr;
	    }
	    
	    return info;
	}
	
	// Get distance to a member
	float GetDistanceToMember(RplId rplId)
	{
	    AG0_TDLNetworkMember member = GetNetworkMember(rplId);
	    if (!member) return -1;
	    
	    return vector.Distance(GetOwner().GetOrigin(), member.GetPosition());
	}
	
	// Check if a member has a specific capability
	bool MemberHasCapability(RplId rplId, AG0_ETDLDeviceCapability capability)
	{
	    AG0_TDLNetworkMember member = GetNetworkMember(rplId);
	    if (!member) return false;
	    
	    return (member.GetCapabilities() & capability) != 0;
	}
	
	// Get all video source members (for feed switching UI)
	array<ref AG0_TDLNetworkMember> GetVideoSourceMembers()
	{
	    return GetMembersWithCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE);
	}
	
	// Get the closest GPS provider
	AG0_TDLNetworkMember GetClosestGPSProvider()
	{
	    array<ref AG0_TDLNetworkMember> gpsProviders = GetMembersWithCapability(AG0_ETDLDeviceCapability.GPS_PROVIDER);
	    if (gpsProviders.IsEmpty()) return null;
	    
	    vector myPos = GetOwner().GetOrigin();
	    AG0_TDLNetworkMember closest = null;
	    float closestDist = float.MAX;
	    
	    foreach (AG0_TDLNetworkMember provider : gpsProviders)
	    {
	        float dist = vector.Distance(myPos, provider.GetPosition());
	        if (dist < closestDist)
	        {
	            closestDist = dist;
	            closest = provider;
	        }
	    }
	    
	    return closest;
	}
	
	// Get network health summary for status displays
	string GetNetworkHealthSummary()
	{
	    if (!HasNetworkMemberData()) 
	        return "No Network Data";
	    
	    int total, gps, video, displays;
	    float avgSignal;
	    GetNetworkStatistics(total, gps, video, displays, avgSignal);
	    
	    string health = "GOOD";
	    if (avgSignal < 50) health = "POOR";
	    else if (avgSignal < 75) health = "FAIR";
	    
	    return string.Format("Network: %1 members, Avg Signal: %.0f%% (%2)", 
	        total, avgSignal, health);
	}
	
	// Check if we can reach a specific member (above minimum signal threshold)
	bool CanReachMember(RplId rplId, float minSignalThreshold = 25.0)
	{
	    AG0_TDLNetworkMember member = GetNetworkMember(rplId);
	    if (!member) return false;
	    
	    return member.GetSignalStrength() >= minSignalThreshold;
	}
	
	// Get all reachable members above signal threshold
	array<ref AG0_TDLNetworkMember> GetReachableMembers(float minSignalThreshold = 25.0)
	{
	    array<ref AG0_TDLNetworkMember> result = {};
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    if (!data) return result;
	    
	    map<RplId, ref AG0_TDLNetworkMember> members = data.ToMap();
	    foreach (RplId rplId, AG0_TDLNetworkMember member : members)
	    {
	        if (member.GetSignalStrength() >= minSignalThreshold)
	            result.Insert(member);
	    }
	    
	    return result;
	}
	
	//------------------------------------------------------------------------------------------------
	// CUSTOM CALLSIGN API
	//------------------------------------------------------------------------------------------------
	
	// Get member display name with callsign indicator
	string GetMemberDisplayNameEx(RplId rplId)
	{
	    AG0_TDLNetworkMember member = GetNetworkMember(rplId);
	    if (!member) return "Unknown Member";
	    
	    // Try to get the actual device to check if it has custom callsign
	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	    if (system) {
	        AG0_TDLDeviceComponent device = system.GetDeviceByRplId(rplId);
	        if (device) {
	            if (device.HasCustomCallsign()) {
	                // Show callsign with indicator
	                return string.Format("%1 [CS]", member.GetPlayerName());
	            }
	        }
	    }
	    
	    return member.GetPlayerName();
	}
	
	// Get formatted member info with callsign status
	string GetMemberDisplayInfoEx(RplId rplId, bool includeCapabilities = true)
	{
	    AG0_TDLNetworkMember member = GetNetworkMember(rplId);
	    if (!member) return "Unknown Member";
	    
	    string displayName = GetMemberDisplayNameEx(rplId);
	    
	    string info = string.Format("%1 (IP: %2)", displayName, member.GetNetworkIP());
	    
	    // Add signal strength indicator
	    float signal = member.GetSignalStrength();
	    string signalBar = "";
	    if (signal >= 80) signalBar = "████";
	    else if (signal >= 60) signalBar = "███░";
	    else if (signal >= 40) signalBar = "██░░";
	    else if (signal >= 20) signalBar = "█░░░";
	    else signalBar = "░░░░";
	    
	    info += string.Format(" %1 %.0f%%", signalBar, signal);
	    
	    if (includeCapabilities)
	    {
	        string capStr = "";
	        int caps = member.GetCapabilities();
	        
	        if ((caps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0) capStr += "[GPS]";
	        if ((caps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0) capStr += "[CAM]";
	        if ((caps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0) capStr += "[DISP]";
	        if ((caps & AG0_ETDLDeviceCapability.INFORMATION) != 0) capStr += "[INFO]";
	        
	        if (!capStr.IsEmpty())
	            info += " " + capStr;
	    }
	    
	    return info;
	}
	
	// Set a custom callsign for this device (server-side)
	void SetCustomCallsign(string callsign)
	{
	    if (!Replication.IsServer()) {
	        Rpc(RpcAsk_SetCustomCallsign, callsign);
	        return;
	    }
	    
	    // Validate callsign (optional - add your own rules)
	    string cleanCallsign = callsign.Trim();
	    if (cleanCallsign.Length() > 16) {
	        cleanCallsign = cleanCallsign.Substring(0, 16); // Max 16 chars
	    }
	    
	    if (m_sCustomCallsign != cleanCallsign) {
	        m_sCustomCallsign = cleanCallsign;
	        Replication.BumpMe();
	        
	        Print(string.Format("TDL_CALLSIGN: Device %1 callsign changed to '%2'", 
	            GetOwner(), cleanCallsign), LogLevel.DEBUG);
	        
	        // Notify system to update member data across network
	        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	        if (system && IsInNetwork()) {
	            system.OnDeviceCallsignChanged(this);
	        }
	    }
	}
	
	// Clear custom callsign (revert to player name)
	void ClearCustomCallsign()
	{
	    SetCustomCallsign("");
	}
	
	// Get the display name (custom callsign if set, otherwise player name)
	string GetDisplayName()
	{
	    if (!m_sCustomCallsign.IsEmpty())
	        return m_sCustomCallsign;
	    return GetOwnerPlayerName();
	}
	
	// Get the custom callsign (returns empty string if not set)
	string GetCustomCallsign()
	{
	    return m_sCustomCallsign;
	}
	
	// Check if device has a custom callsign set
	bool HasCustomCallsign()
	{
	    return !m_sCustomCallsign.IsEmpty();
	}
	
	//------------------------------------------------------------------------------------------------
	// RPC for callsign changes
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetCustomCallsign(string callsign)
	{
	    SetCustomCallsign(callsign);
	}
	
	//------------------------------------------------------------------------------------------------
	// Client-side callback when callsign replicates
	//------------------------------------------------------------------------------------------------
	protected void OnCustomCallsignChanged()
	{
	    Print(string.Format("TDL_CALLSIGN: Device %1 received callsign update: '%2'", 
	        GetOwner(), m_sCustomCallsign), LogLevel.DEBUG);
	}

}