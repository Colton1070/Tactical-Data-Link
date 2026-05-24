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

enum AG0_ETDLWaveform
{
    NONE        = 0,    // No waveform assigned — device cannot form links
    LEGACY      = 1,    // Generic/untyped RF — interops only with other LEGACY devices
    MPU5        = 2,    // Silvus MPU5 / StreamScape MANET waveform
    TRELLISWARE = 4,    // TrellisWare TWave / TW-950 waveform
    BFT2        = 8,    // Iridium L-band SBD satellite waveform — BFT-2 / JBC-P vehicle SATCOM
    LINK16      = 16,   // MIDS/JTIDS UHF TDMA — joint air/fires picture, AH-64E Apache and similar
    // Use bits 32, 64, 128 … for future or third-party waveforms
}

// Single source of truth for which waveform bits represent satellite-transport
// tech. Networks created over these waveforms are satellite-mode (range-free,
// hub-and-spoke abstraction); networks over other waveforms are terrestrial
// (LOS distance falloff). Per-bit rather than per-device because real radios
// can carry multiple waveforms with different transports — an AH-64E Apache
// with BFT2 + LINK16 operates the BFT2 leg via Iridium SATCOM and the LINK16
// leg via UHF TDMA simultaneously. Putting the flag on the device wouldn't
// model that. Update SATCOM_WAVEFORMS when adding new satellite-tier
// waveforms; every downstream satellite branch derives from this constant.
class AG0_TDLWaveformInfo
{
    static const int SATCOM_WAVEFORMS = AG0_ETDLWaveform.BFT2;

    // True if `waveformMask` contains any satellite-tier waveform bit.
    // For multi-bit masks the answer is "any satellite path exists" — the
    // caller decides what to do with that information.
    static bool IsSatellite(int waveformMask)
    {
        return (SATCOM_WAVEFORMS & waveformMask) != 0;
    }

    // Human-readable label for a single waveform bit. Used by the vehicle
    // action's GetActionNameScript so "Join TDL Network" becomes
    // "Join BFT-2 Network" / "Join LINK-16 Network" when the action authors a
    // designated waveform. For multi-bit masks (no override path) the generic
    // "TDL" label is returned — the action only specializes when authoring
    // pins to a single waveform.
    static string GetDisplayName(int waveformBit)
    {
        switch (waveformBit)
        {
            case AG0_ETDLWaveform.LEGACY: return "Legacy";
            case AG0_ETDLWaveform.MPU5: return "MPU5";
            case AG0_ETDLWaveform.TRELLISWARE: return "TrellisWare";
            case AG0_ETDLWaveform.BFT2: return "BFT-2";
            case AG0_ETDLWaveform.LINK16: return "LINK-16";
        }
        return "TDL";
    }
}

enum AG0_ETDLDeviceCapability
{
    NETWORK_ACCESS = 1,
    GPS_PROVIDER = 2, 
    DISPLAY_OUTPUT = 4,
    VIDEO_SOURCE = 8,
    POWER_PROVIDER = 16,
	INFORMATION = 32,
	ATAK_DEVICE = 64,
	BRIDGE = 128
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
//
// UPDATE: Dialog handling has been moved to PlayerController to fix gamepad/console input issues.
// The two-dialog sequential flow (Name -> Password) is now managed by SCR_PlayerController.


class AG0_TDLDeviceComponent : ScriptGameComponent
{
	// Networks this device has joined. Multi-network membership is mandatory
	// infrastructure for waveform-bridging entities (e.g. a ground station with
	// LINK16 + BFT2 + BRIDGE that needs to sit on both networks simultaneously).
	// RplProp so late-joiners get the current set without depending on the RPC
	// stream; the per-network member/connectivity payloads themselves flow via
	// RPCs in m_mConnectedMembersByNetwork / m_mLocalNetworkMembersByNetwork.
	[RplProp(onRplName: "OnJoinedNetworksReplicated")]
	protected ref array<int> m_aJoinedNetworkIDs = new array<int>();

	// Per-network connected-member RplIds, RPC-driven not RplProp — the system
	// re-emits on every connectivity tick so the cost of not snapshotting is
	// bounded. Late-joiners pick up data on the next tick.
	protected ref map<int, ref array<RplId>> m_mConnectedMembersByNetwork = new map<int, ref array<RplId>>();

	// Per-network local member buffer for INFORMATION devices. Same RPC-driven
	// pattern as above. The buffer survives the unheld/crewless state because
	// INFORMATION is the "snapshot persists" semantic.
	protected ref map<int, ref AG0_TDLNetworkMembers> m_mLocalNetworkMembersByNetwork = new map<int, ref AG0_TDLNetworkMembers>();

	// Bitmask of waveforms this device currently has at least one network
	// membership for. Server-maintained as networks are joined/left, replicated
	// so client-side action visibility (the vehicle action's IsInNetworkOfWaveform
	// gate) doesn't need to round-trip to look up per-network waveforms. Slight
	// lag vs the m_aJoinedNetworkIDs RplProp is acceptable for UI gates — at
	// worst Create blinks visible for one tick after a Join.
	[RplProp()]
	protected int m_iJoinedWaveformsMask = 0;
	
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
	
	[Attribute(
		category: "TDL Device Config",
		desc: "Waveform(s) this device supports. Two devices connect only if their masks share at least one bit. Satellite-mode connectivity (range-free) is intrinsic to the waveform — see AG0_TDLWaveformInfo.SATCOM_WAVEFORMS for which bits carry satellite transport.",
		uiwidget: UIWidgets.Flags,
		enums: ParamEnumArray.FromEnum(AG0_ETDLWaveform),
		defvalue: AG0_ETDLWaveform.LEGACY.ToString()
	)]
	protected AG0_ETDLWaveform m_eWaveform;
	
	[Attribute("", UIWidgets.Auto, category: "ATAK Plugin Support")]
	protected ref array<string> m_aSupportedATAKPlugins;
	
	[Attribute("", UIWidgets.Auto, category: "ATAK Configuration")]
	protected ref array<ref AG0_ATAKPluginBase> m_aAvailablePlugins;
	
	[Attribute("1", UIWidgets.CheckBox, category: "Network Config")]
	protected bool m_bUseTDLRadio;

	[Attribute("250.0", UIWidgets.EditBox, category: "Network Config")]
	protected float m_fNetworkRange;

	[Attribute("", UIWidgets.ResourceNamePicker,
		"Marker imageset for this device on the dynamic map. Leave empty to fall back to the default TDL imageset.",
		"imageset", category: "TDL Device Config")]
	protected ResourceName m_sMarkerImageset;

	[Attribute("", UIWidgets.EditBox,
		"Quad name within the marker imageset (e.g. 'tdl_device', 'tdl_dot). Leave empty for the default.",
		category: "TDL Device Config")]
	protected string m_sMarkerIconQuad;

	[Attribute("0", UIWidgets.CheckBox,
		"Override the default marker color with the value below. Unchecked = use the shared TDL entry config color (yellow today).",
		category: "TDL Device Config")]
	protected bool m_bOverrideMarkerColor;

	[Attribute("1 1 0 1", UIWidgets.ColorPicker,
		"Marker color when override is enabled. RGBA 0-1.",
		category: "TDL Device Config")]
	protected ref Color m_MarkerColor;
	
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
	
	protected RplId m_LocalActiveVideoSource = RplId.Invalid();
	
	protected bool m_bWasHeldPreviously = false;
    protected bool m_bCapabilitiesActive = true;

	//Replication hack - trying to prevent VME...
	protected bool m_bLeavingNetwork = false;

	// Owner-derived power source references — mutually exclusive; whichever
	// applies first in WireOwnerPowerSource wins. Cached so OnDelete can
	// unsubscribe symmetrically without re-resolving the owner.
	protected VehicleControllerComponent m_PowerSrcVehicleController;
	protected BaseRadioComponent m_PowerSrcRadio;

	override protected bool OnTicksOnRemoteProxy() { return true; } //This is DUMB
	
	override void OnPostInit(IEntity owner)
	{
	    super.OnPostInit(owner);

	    SetEventMask(owner, EntityEvent.INIT | EntityEvent.POSTFRAME);

	}
	
	override void EOnInit(IEntity owner)
	{
	    super.EOnInit(owner);
	    rplComp = RplComponent.Cast(owner.FindComponent(RplComponent));

	    // Register with TDL system if we can access network
	    if (CanAccessNetwork() && Replication.IsServer()) {
	        GetGame().GetCallqueue().CallLater(RegisterWithTDLSystem, 2500, false);
	    }

		// Owner-driven power on the server. Without this, m_bIsPowered stays at
		// its default-true and the device never reflects engine-off or
		// radio-toggled-off — a parked vehicle or a stowed-and-off handheld
		// would otherwise stay on the network indefinitely.
		if (Replication.IsServer())
			WireOwnerPowerSource(owner);

		if (HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE) && m_CameraAttachment)
		{
		    m_CameraAttachment.Init(owner);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Resolves the owner's power-state source and seeds m_bIsPowered from it.
	// Two paths, mutually exclusive: vehicles (engine on/off) and radio gadgets
	// (BaseRadioComponent toggle). Anything else leaves m_bIsPowered at its
	// default-true so non-owner-driven devices keep working as before.
	protected void WireOwnerPowerSource(IEntity owner)
	{
		VehicleControllerComponent vc = VehicleControllerComponent.Cast(owner.FindComponent(VehicleControllerComponent));
		if (vc)
		{
			m_PowerSrcVehicleController = vc;
			ScriptInvokerVoid onStart = vc.GetOnEngineStart();
			if (onStart)
				onStart.Insert(OnOwnerVehicleEngineStart);
			ScriptInvokerVoid onStop = vc.GetOnEngineStop();
			if (onStop)
				onStop.Insert(OnOwnerVehicleEngineStop);
			SetPowered(vc.IsEngineOn());
			return;
		}

		BaseRadioComponent rc = BaseRadioComponent.Cast(owner.FindComponent(BaseRadioComponent));
		if (rc)
		{
			m_PowerSrcRadio = rc;
			if (rc.m_OnPowerChanged)
				rc.m_OnPowerChanged.Insert(OnOwnerRadioPowerChanged);
			SetPowered(rc.IsPowered());
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOwnerVehicleEngineStart()
	{
		SetPowered(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOwnerVehicleEngineStop()
	{
		SetPowered(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnOwnerRadioPowerChanged(bool powered)
	{
		SetPowered(powered);
	}
	
	override void EOnPostFrame(IEntity owner, float timeSlice)
	{
	    super.EOnPostFrame(owner, timeSlice);

	    SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerController()
	    );
	    if (!controller || !controller.IsHoldingDevice(owner))
	        return;

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

		// Symmetric to WireOwnerPowerSource. The cached refs are non-null only
		// on the server (the wire is server-gated), so the null guards also act
		// as a side-of-replication filter — no need for an explicit IsServer.
		if (m_PowerSrcVehicleController)
		{
			ScriptInvokerVoid onStart = m_PowerSrcVehicleController.GetOnEngineStart();
			if (onStart)
				onStart.Remove(OnOwnerVehicleEngineStart);
			ScriptInvokerVoid onStop = m_PowerSrcVehicleController.GetOnEngineStop();
			if (onStop)
				onStop.Remove(OnOwnerVehicleEngineStop);
		}
		if (m_PowerSrcRadio && m_PowerSrcRadio.m_OnPowerChanged)
			m_PowerSrcRadio.m_OnPowerChanged.Remove(OnOwnerRadioPowerChanged);

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
    
    protected void DeactivateAllCapabilities()
    {
        m_bCapabilitiesActive = false;
        // Specific capability shutdown logic here
    }
	
	array<string> GetSupportedATAKPlugins() 
	{ 
	    if (!m_aSupportedATAKPlugins) return {};
	    return m_aSupportedATAKPlugins; 
	}
	
	array<ref AG0_ATAKPluginBase> GetAvailablePlugins() 
	{ 
	    if (!m_aAvailablePlugins) return {};
	    return m_aAvailablePlugins; 
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
	    Print(string.Format("TDL_VIDEO: OnCameraBroadcastingChanged called - state is now %1 on device %2", 
	        m_bCameraBroadcasting, GetOwner()), LogLevel.DEBUG);
	    
	    // Guard: On client, only process if this is a genuine state change
	    if (!System.IsConsoleApp())
	    {
	        SCR_PlayerController controller = SCR_PlayerController.Cast(
	            GetGame().GetPlayerController()
	        );
	        if (!controller)
	        {
	            Print("TDL_VIDEO: No player controller, skipping registration update", LogLevel.DEBUG);
	            return;
	        }
	        
	        RplId myId = GetDeviceRplId();
	        if (myId == RplId.Invalid())
	        {
	            Print("TDL_VIDEO: Invalid device RplId, skipping registration update", LogLevel.DEBUG);
	            return;
	        }
	        
	        // Always register/unregister based on actual broadcast state
	        // The controller's methods are idempotent - they check before modifying
	        if (m_bCameraBroadcasting)
	        {
	            controller.RegisterBroadcastingDevice(myId);
	            Print(string.Format("TDL_VIDEO: Registered broadcasting device %1", myId), LogLevel.DEBUG);
	        }
	        else
	        {
	            controller.UnregisterBroadcastingDevice(myId);
	            Print(string.Format("TDL_VIDEO: Unregistered broadcasting device %1", myId), LogLevel.DEBUG);
	        }
	    }
	}
	
	protected void OnVideoSourcesChanged()
	{
	}

	protected void OnActiveVideoSourceChanged()
	{
	    // Called when the server replicates a change; safe to ignore because
	    // display is managed locally.
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
		
		// HDR for broadcast must be done with a post-process effect, not via SetCameraHDRBrightness.

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
	
	// Applies effects to display cameras showing our broadcast. Same implementation
	// as ApplyBroadcastEffects, but semantically different.
	void ApplyDisplayEffects(int displayCameraIndex)
	{
		ApplyBroadcastEffects(displayCameraIndex);
	}
	
	protected void ApplyNightVisionEffects(int cameraIndex)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world) return;
		
		Print("TDL_VIDEO: Applied night vision effects", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// TRANSFORM UPDATES - Clean separation of broadcast vs display
	//------------------------------------------------------------------------------------------------

	protected IEntity m_CachedBroadcaster;
	protected float m_fTransformUpdateTimer = 0;
	
	protected bool UpdateDisplayTransform()
	{
	    // Use local active source instead of replicated one.
	    RplId activeSource = GetActiveVideoSource();

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

// TDL NETWORK DIALOG - delegated to PlayerController for gamepad/console support


	//------------------------------------------------------------------------------------------------
	// Public methods to be called by the User Action.
	// Routed through PlayerController for proper gamepad input handling.
	//------------------------------------------------------------------------------------------------
	// waveformOverride: 0 means use the device's full waveform mask (existing
	// behavior). Non-zero pins the operation to a single waveform bit, which
	// the vehicle action's m_eForceWaveform path passes through so a multi-
	// waveform device can target one specific network type.
	void CreateNetworkDialog(IEntity userEntity, int waveformOverride = 0)
	{
	    RequestNetworkDialogViaPlayerController(userEntity, true, waveformOverride);
	}

	void JoinNetworkDialog(IEntity userEntity, int waveformOverride = 0)
	{
	    RequestNetworkDialogViaPlayerController(userEntity, false, waveformOverride);
	}
	
	bool LeaveNetworkTDL()
	{
		// No-arg variant — leaves every network the device is currently in.
		// Vehicle actions with a designated waveform should call
		// LeaveNetworkTDLById(networkId) instead so they affect only the network
		// matching the action's waveform, leaving the others untouched.
		//
		// The user action that fires this runs on both client and server (it's
		// not LocalEffectOnly). Server-side state is owned by AG0_TDLSystem;
		// the client-only branch below is purely an optimistic UI clear so the
		// menu reacts instantly without waiting for the RPC round-trip.
		if (!Replication.IsServer())
		{
			// Leaving every network → mask becomes empty. The byID and
			// byWaveform variants skip this because the client doesn't know
			// the targeted network's waveform and server replication will
			// correct the mask within a tick anyway.
			m_aJoinedNetworkIDs.Clear();
			m_iJoinedWaveformsMask = 0;
			m_mConnectedMembersByNetwork.Clear();
			m_mLocalNetworkMembersByNetwork.Clear();
		}

		if(!System.IsConsoleApp()) {
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (pc)
				pc.RequestLeaveNetwork(GetDeviceRplId());
		}
		else {
			AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
			if (!system) return false;
			AG0_TDLDeviceComponent device = system.GetDeviceByRplId(GetDeviceRplId());
			if (!device) return false;
			system.LeaveNetwork(device);
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Waveform-targeted leave. Server-side: looks up the first joined network
	// whose waveform mask shares a bit with `waveform` and detaches from that
	// one. Client-side: defers to the server via PC RPC because the client
	// doesn't have authoritative per-network waveform data — the m_iJoinedWaveformsMask
	// tells the client whether ANY relevant network exists but not which
	// specific networkID to target.
	bool LeaveNetworkTDLByWaveform(int waveform)
	{
		if (waveform == 0) return false;

		if (!System.IsConsoleApp()) {
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (pc)
				pc.RequestLeaveNetworkByWaveform(GetDeviceRplId(), waveform);
		}
		else {
			AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
			if (!system) return false;
			AG0_TDLDeviceComponent device = system.GetDeviceByRplId(GetDeviceRplId());
			if (!device) return false;
			system.LeaveNetworkByWaveform(device, waveform);
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Targeted-leave variant for multi-network devices. Removes the device from
	// the single named network and leaves any others untouched. Caller is
	// responsible for choosing which network to leave (typically the action
	// layer discriminating by waveform).
	bool LeaveNetworkTDLById(int networkId)
	{
		if (networkId <= 0) return false;

		if (!Replication.IsServer())
		{
			int idx = m_aJoinedNetworkIDs.Find(networkId);
			if (idx >= 0)
				m_aJoinedNetworkIDs.Remove(idx);
			m_mConnectedMembersByNetwork.Remove(networkId);
			m_mLocalNetworkMembersByNetwork.Remove(networkId);
		}

		if (!System.IsConsoleApp()) {
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (pc)
				pc.RequestLeaveNetworkById(GetDeviceRplId(), networkId);
		}
		else {
			AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
			if (!system) return false;
			AG0_TDLDeviceComponent device = system.GetDeviceByRplId(GetDeviceRplId());
			if (!device) return false;
			system.LeaveNetworkById(device, networkId);
		}
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Routes network dialog request through PlayerController
	//! This solves the gamepad/console input issue by letting PlayerController manage dialogs
	protected void RequestNetworkDialogViaPlayerController(IEntity userEntity, bool createMode, int waveformOverride)
	{
	    if (!Replication.IsServer())
	        return;

	    PlayerManager playerManager = GetGame().GetPlayerManager();
	    if (!playerManager)
	        return;

	    int userPlayerId = playerManager.GetPlayerIdFromControlledEntity(userEntity);
	    SCR_PlayerController playerController = SCR_PlayerController.Cast(
	        playerManager.GetPlayerController(userPlayerId));

	    if (!playerController)
	    {
	        Print("TDL_DIALOG: Could not find PlayerController for user", LogLevel.WARNING);
	        return;
	    }

	    RplId deviceRplId = GetDeviceRplId();
	    if (deviceRplId == RplId.Invalid())
	    {
	        Print("TDL_DIALOG: Invalid device RplId", LogLevel.WARNING);
	        return;
	    }

	    Print(string.Format("TDL_DIALOG: Requesting %1 dialog via PlayerController for device %2 (waveform %3)",
	        createMode, deviceRplId, waveformOverride), LogLevel.DEBUG);

	    // Public method internally RPCs to the owning client. Waveform override
	    // is preserved through the dialog flow so the eventual Create/Join RPC
	    // back to the server carries the same constraint the action authored.
	    playerController.AskOpenNetworkDialog(deviceRplId, createMode, waveformOverride);
	}
	
	//RPCs

	// Every per-network RPC carries networkID so the same handler resolves
	// which slot in the per-network maps to write. The RplProp on
	// m_aJoinedNetworkIDs is the late-joiner safety net; for already-connected
	// clients these RPCs are the authoritative update path.

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_NotifyNetworkJoined(int networkID, array<RplId> members)
	{
		if (m_aJoinedNetworkIDs.Find(networkID) == -1)
			m_aJoinedNetworkIDs.Insert(networkID);
		SetConnectedMembersForNetworkLocal(networkID, members);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_NotifyNetworkLeft(int networkID)
	{
		int idx = m_aJoinedNetworkIDs.Find(networkID);
		if (idx >= 0)
			m_aJoinedNetworkIDs.Remove(idx);
		m_mConnectedMembersByNetwork.Remove(networkID);
		m_mLocalNetworkMembersByNetwork.Remove(networkID);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_NotifyConnectivityUpdated(int networkID, array<RplId> members)
	{
		SetConnectedMembersForNetworkLocal(networkID, members);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_SetLocalNetworkMembers(int networkID, array<ref AG0_TDLNetworkMember> members)
	{
		AG0_TDLNetworkMembers buffer = m_mLocalNetworkMembersByNetwork.Get(networkID);
		if (!buffer)
		{
			buffer = new AG0_TDLNetworkMembers();
			m_mLocalNetworkMembersByNetwork.Set(networkID, buffer);
		}

		buffer.Clear();
		foreach (AG0_TDLNetworkMember member : members)
		{
			buffer.Add(member);
		}

		Print(string.Format("TDL_DEVICE_INFO: Received %1 network members for network %2 on device %3",
			members.Count(), networkID, GetOwner()), LogLevel.DEBUG);
	}

	// Defensive copy on store so the RPC's transient buffer doesn't leak into
	// the map. The same routine is used by the server-side OnNetworkConnectivityUpdated
	// path and by the broadcast RPC handler, so both paths converge on the same
	// invariant (one independent array per network slot).
	protected void SetConnectedMembersForNetworkLocal(int networkID, array<RplId> members)
	{
		array<RplId> copy = new array<RplId>();
		foreach (RplId id : members)
			copy.Insert(id);
		m_mConnectedMembersByNetwork.Set(networkID, copy);
	}
	
	//------------------------------------------------------------------------------------------------
	// System callback methods (called by AG0_TDLSystem)
	//------------------------------------------------------------------------------------------------
	// networkWaveform: the joined network's waveform bitmask, OR'd into
	// m_iJoinedWaveformsMask so client-side action visibility (IsInNetworkOfWaveform)
	// answers without needing per-network lookups. Caller in AG0_TDLSystem
	// already has the network object in scope so passing through is trivial.
	void OnNetworkJoined(int networkID, int networkWaveform, array<RplId> radioIDs)
	{
		if (m_aJoinedNetworkIDs.Find(networkID) == -1)
			m_aJoinedNetworkIDs.Insert(networkID);
		m_iJoinedWaveformsMask = m_iJoinedWaveformsMask | networkWaveform;
		SetConnectedMembersForNetworkLocal(networkID, radioIDs);
		Replication.BumpMe();

		string ownerName = "UNKNOWN";
		if (GetOwner())
			ownerName = GetOwner().ToString();

		Print(string.Format("TDL_NETWORK_JOIN: %1 (%2) joined network %3 (waveform %4) with %5 members",
			ownerName, GetOwnerPlayerName(), networkID, networkWaveform, radioIDs.Count()), LogLevel.DEBUG);

		foreach (RplId radioId : radioIDs)
		{
			Print(string.Format("  Network member RplId: %1", radioId), LogLevel.DEBUG);
		}

		Rpc(RpcDo_NotifyNetworkJoined, networkID, radioIDs);
	}

	// networkID was previously implicit (read from m_iCurrentNetworkID), now
	// explicit so the caller (AG0_TDLSystem.LeaveNetwork / LeaveNetworkById)
	// names the network being departed. Required for multi-network devices —
	// leaving one network must not blast state for the others.
	void OnNetworkLeft(int networkID)
	{
		string ownerName = "UNKNOWN";
		if (GetOwner())
			ownerName = GetOwner().ToString();

		Print(string.Format("TDL_NETWORK_LEAVE: %1 (%2) left network %3",
			ownerName, GetOwnerPlayerName(), networkID), LogLevel.DEBUG);

		m_mLocalNetworkMembersByNetwork.Remove(networkID);
		m_mConnectedMembersByNetwork.Remove(networkID);

		// Send the id BEFORE removing from local array so the broadcast carries
		// a meaningful payload. The RPC handler does its own array fix-up on
		// every client.
		Rpc(RpcDo_NotifyNetworkLeft, networkID);

		int idx = m_aJoinedNetworkIDs.Find(networkID);
		if (idx >= 0)
			m_aJoinedNetworkIDs.Remove(idx);

		// Recompute the waveforms mask from remaining networks. The waveform
		// bit may still apply if another joined network shares it, so we
		// can't just XOR-out the leaving network's waveform — do a full pass.
		RecomputeJoinedWaveformsMask();
		Replication.BumpMe();
	}

	// Walks remaining joined networks and ORs their waveforms into a fresh
	// mask. Server-only — depends on AG0_TDLSystem.FindNetworkByID which only
	// has populated state on the server. Clients receive the recomputed mask
	// via the m_iJoinedWaveformsMask RplProp.
	protected void RecomputeJoinedWaveformsMask()
	{
		m_iJoinedWaveformsMask = 0;
		AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
		if (!system) return;
		foreach (int netId : m_aJoinedNetworkIDs)
		{
			AG0_TDLNetwork network = system.FindNetworkByID(netId);
			if (!network) continue;
			m_iJoinedWaveformsMask = m_iJoinedWaveformsMask | network.GetWaveform();
		}
	}

	//------------------------------------------------------------------------------------------------
	// Server-side: Receive network members from system and broadcast to all clients.
	// Called by AG0_TDLSystem for devices with INFORMATION capability, per-network.
	//------------------------------------------------------------------------------------------------
	void SetLocalNetworkMembers(int networkID, array<ref AG0_TDLNetworkMember> members)
	{
		if (!Replication.IsServer()) return;

		Print(string.Format("TDL_DEVICE_INFO: Server sending %1 members for network %2 to device %3",
			members.Count(), networkID, GetOwner()), LogLevel.DEBUG);

		Rpc(RpcDo_SetLocalNetworkMembers, networkID, members);
	}

	void OnNetworkConnectivityUpdated(int networkID, array<RplId> members)
	{
		SetConnectedMembersForNetworkLocal(networkID, members);

		string ownerName = "UNKNOWN";
		if (GetOwner())
			ownerName = GetOwner().ToString();

		foreach (RplId memberId : members)
		{
			Print(string.Format("  Connected to RplId: %1 (network %2)", memberId, networkID), LogLevel.DEBUG);
		}

		Rpc(RpcDo_NotifyConnectivityUpdated, networkID, members);
	}

	// RplProp replication callback for m_aJoinedNetworkIDs. Empty for now —
	// the body was empty under the old single-int field too and no consumer
	// has needed a hook. Kept as a named target for future per-network UI
	// invalidation.
	protected void OnJoinedNetworksReplicated()
	{
	}
	
	
//------------------------------------------------------------------------------------------------
//
// PUBLIC API BELOW
//
//------------------------------------------------------------------------------------------------
	
	bool HasCapability(AG0_ETDLDeviceCapability cap) { return (m_eCapabilities & cap) != 0; }

	int GetWaveform() { return m_eWaveform; }

	// Per-device map marker authoring. Empty / unset values fall through to
	// the AG0_TDLMapMarkerEntry default at render time — existing prefabs
	// without these attributes set still get the legacy MPU5 icon and the
	// shared entry config color (yellow today).
	ResourceName GetMarkerImageset() { return m_sMarkerImageset; }
	string GetMarkerIconQuad() { return m_sMarkerIconQuad; }
	bool ShouldOverrideMarkerColor() { return m_bOverrideMarkerColor; }
	Color GetMarkerColor() { return m_MarkerColor; }
	
	// Returns true if this device shares at least one waveform bit with 'other'.
	// Used by AreDevicesConnected in AG0_TDLSystem.
	bool IsWaveformCompatible(AG0_TDLDeviceComponent other)
	{
	    if (!other) return false;
	    return (m_eWaveform & other.GetWaveform()) != 0;
	}
	
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
	
	// "In any network" — boolean kept identical to the prior single-network
	// semantic. All existing call sites continue to work unchanged.
	bool IsInNetwork() { return m_aJoinedNetworkIDs.Count() > 0; }

	// Membership predicates for the multi-network world. The vehicle action
	// uses IsInNetworkById/Of to decide Join vs Leave when a specific waveform
	// or specific network is being targeted.
	bool IsInNetworkById(int networkID) { return m_aJoinedNetworkIDs.Find(networkID) >= 0; }

	// "Primary" network — first joined, kept as a compat shim for UI callers
	// (AG0_TDLMenuController, AG0_VonDisplay_TDL, AG0_ATAKPlugin_MPU5,
	// AG0_TDLSystem.UpdateBridgeLinks). All of those still want a single int
	// to drive a display label or a network-keyed lookup; they accept the
	// "primary == first" pick until a network selector exists.
	int GetCurrentNetworkID()
	{
		if (m_aJoinedNetworkIDs.Count() == 0)
			return -1;
		return m_aJoinedNetworkIDs[0];
	}

	// Full list of joined networks. Use this when iterating per-network state
	// (e.g. UpdateBridgeLinks collecting unique netIds across a player's
	// devices, or any code that should not arbitrarily prefer "primary").
	array<int> GetJoinedNetworkIDs() { return m_aJoinedNetworkIDs; }

	// Bitmask of waveforms across all joined networks. Used by vehicle actions
	// with a designated waveform to decide Join vs Leave label without per-
	// network introspection.
	int GetJoinedWaveformsMask() { return m_iJoinedWaveformsMask; }

	// True when the device is currently a member of any network that shares
	// at least one waveform bit with `waveform`.
	bool IsInNetworkOfWaveform(int waveform) { return (m_iJoinedWaveformsMask & waveform) != 0; }

	// Union of all connected RplIds across every joined network. Kept as the
	// no-arg shape for backward compat — existing callers (player controller
	// aggregator, UpdateVideoStreaming) want "everyone I can reach across all
	// my networks" semantics, which the union faithfully provides.
	array<RplId> GetConnectedMembers()
	{
		array<RplId> result = new array<RplId>();
		foreach (int netID, array<RplId> ids : m_mConnectedMembersByNetwork)
		{
			if (!ids) continue;
			foreach (RplId id : ids)
			{
				if (result.Find(id) == -1)
					result.Insert(id);
			}
		}
		return result;
	}

	// Per-network connected members — needed by any caller that must scope
	// reachability to a single network (e.g. message propagation deciding
	// what gets relayed inside a single network's clique).
	array<RplId> GetConnectedMembersForNetwork(int networkID)
	{
		array<RplId> stored = m_mConnectedMembersByNetwork.Get(networkID);
		if (!stored)
			return new array<RplId>();
		return stored;
	}
	
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
	// Public API: does this device have any network member data available to it,
	// from either source (own local copy OR the controlling player's cache)?
	//
	// NOTE: this is NOT the same as HasLocalNetworkData() below, despite the
	// implementations looking similar at first glance — this delegates to
	// GetNetworkMembersData() which transparently falls through from local
	// storage to the controller cache, so it returns true for any device that
	// can serve member data, including handheld radios. HasLocalNetworkData() is
	// specifically asking "does this device hold its own copy" and is the right
	// check for code that needs to know if the device is INFORMATION-class.
	//------------------------------------------------------------------------------------------------
	bool HasNetworkMemberData()
	{
	    AG0_TDLNetworkMembers data = GetNetworkMembersData();
	    return data && data.Count() > 0;
	}
		
	AG0_TDLNetworkMembers GetNetworkMembersData()
	{
	    // INFORMATION capability is about which DATA SOURCE serves a device, not
	    // about whether a device may read network member data at all. Two cases:
	    //
	    //   1. INFORMATION device (TOC computer, vehicle CDU, fixed installation):
	    //      Holds its own per-network copies of member data so it stays
	    //      useful even when no player is in possession. Server populates
	    //      m_mLocalNetworkMembersByNetwork via RpcDo_SetLocalNetworkMembers
	    //      (gated on INFORMATION at the server emission site).
	    //      Multi-network devices return the union across all their joined
	    //      networks here — a ground station with LINK16 + BFT2 sees its
	    //      combined picture in one buffer for downstream rendering.
	    //
	    //   2. Personal handheld (legacy radio, Wave Relay, etc. without
	    //      INFORMATION): The data lives on the controlling player's
	    //      m_mTDLNetworkMembersMap because exactly one player owns the
	    //      device and follows it. We fall through to the controller cache.
	    //
	    // The local-storage check below is naturally self-gating because the
	    // per-network buffers are never populated on a device without
	    // INFORMATION — the server simply never fires the RPC for it. So no
	    // explicit capability gate is needed at this layer. The previous gate
	    // here ('if (!HasCapability(INFORMATION)) return null') broke handheld
	    // radios entirely: the menu UI would open, the contact list / callsign
	    // / message rendering would all silently see null data, and the user
	    // would observe "can join a network but can't do anything in it." This
	    // is what made legacy radios appear broken on dedicated servers.
	    if (m_mLocalNetworkMembersByNetwork.Count() > 0)
	    {
	        AG0_TDLNetworkMembers union = BuildLocalMembersUnion();
	        if (union && union.Count() > 0)
	            return union;
	    }

	    SCR_PlayerController controller = SCR_PlayerController.Cast(
		    GetGame().GetPlayerController()
		);
	    if (!controller) return null;

	    if (m_aJoinedNetworkIDs.Count() == 0)
	        return controller.GetAggregatedTDLMembers();

	    // Single-network fast path matches the prior behavior exactly. Multi-
	    // network unions the per-network member lists, deduplicating by RplId
	    // so a member that appears in two of this device's networks isn't
	    // double-counted in the UI.
	    if (m_aJoinedNetworkIDs.Count() == 1)
	        return controller.GetTDLNetworkMembers(m_aJoinedNetworkIDs[0]);

	    AG0_TDLNetworkMembers union = new AG0_TDLNetworkMembers();
	    foreach (int netID : m_aJoinedNetworkIDs)
	    {
	        AG0_TDLNetworkMembers nm = controller.GetTDLNetworkMembers(netID);
	        if (!nm) continue;
	        map<RplId, ref AG0_TDLNetworkMember> netMap = nm.ToMap();
	        foreach (RplId id, AG0_TDLNetworkMember m : netMap)
	        {
	            if (!union.GetByRplId(id))
	                union.Add(m);
	        }
	    }
	    return union;
	}

	// Helper: union of per-network local buffers, dedup by RplId. Returns null
	// when no buffers are populated so callers can fall through to the
	// controller-cache path.
	protected AG0_TDLNetworkMembers BuildLocalMembersUnion()
	{
	    if (m_mLocalNetworkMembersByNetwork.Count() == 0)
	        return null;

	    AG0_TDLNetworkMembers union = new AG0_TDLNetworkMembers();
	    foreach (int netID, AG0_TDLNetworkMembers buffer : m_mLocalNetworkMembersByNetwork)
	    {
	        if (!buffer) continue;
	        map<RplId, ref AG0_TDLNetworkMember> bufferMap = buffer.ToMap();
	        foreach (RplId id, AG0_TDLNetworkMember m : bufferMap)
	        {
	            if (!union.GetByRplId(id))
	                union.Add(m);
	        }
	    }
	    return union;
	}
	
	//------------------------------------------------------------------------------------------------
	// CLIENT-SIDE NETWORK MEMBER DATA ACCESS HELPERS
	//------------------------------------------------------------------------------------------------
	
	
	//------------------------------------------------------------------------------------------------
	// Check if this device has locally stored network data (INFORMATION-class
	// device that holds its own per-network member buffers, populated by
	// AG0_TDLSystem's INFORMATION fan-out).
	//------------------------------------------------------------------------------------------------
	bool HasLocalNetworkData()
	{
	    foreach (int netID, AG0_TDLNetworkMembers buffer : m_mLocalNetworkMembersByNetwork)
	    {
	        if (buffer && buffer.Count() > 0)
	            return true;
	    }
	    return false;
	}
	
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

	        // Notify system to update member data across the network. Do NOT
	        // gate on IsInNetwork() — the device's joined-networks list can be
	        // transiently empty server-side during client-server transitions
	        // (see the comment on AG0_TDLSystem.NotifyNetworkConnectivity for
	        // the exact mechanism). OnDeviceCallsignChanged iterates m_aNetworks
	        // and bails harmlessly if the device isn't in any network, so the
	        // IsInNetwork() guard was redundant — and it caused the "callsign
	        // doesn't propagate on the next tick" symptom whenever the device
	        // state happened to be in its briefly-cleared form at save time.
	        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
	        if (system) {
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