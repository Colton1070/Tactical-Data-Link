//! Focus mode state machine for TDL_WorldSpaceDisplayComponent.
//! IDLE      → not focused; cursor driven by camera raycast against screen quad.
//! ENTERING  → focus camera spawned + active; transform tweening from saved player-camera pose to pivot pose.
//! ACTIVE    → focus camera parented to pivot bone; cursor driven by TDLCursor* action deltas.
//! EXITING   → focus camera detached; transform tweening back to current player-camera pose. Restore + despawn at end.
enum TDL_EFocusState
{
    IDLE,
    ENTERING,
    ACTIVE,
    EXITING
}

class TDL_WorldSpaceDisplayComponentClass : ScriptGameComponentClass {}

class TDL_WorldSpaceDisplayComponent : ScriptGameComponent
{
    // ============================================
    // SCREEN & RENDERING
    // ============================================
    
    // Screen entity from slot
    protected IEntity m_ScreenEntity;
    
    // Root container widget (for cleanup)
    protected Widget m_wRTContainer;
    
    // RTTextureWidget that renders to the screen
    protected RTTextureWidget m_RTWidget;
    
    // Container frame inside RT widget
    protected Widget m_wContentFrame;
    
    // Root widget containing the ATAK layout
    protected Widget m_wRoot;
    
    // Display controller - handles all the ATAK logic
    protected ref AG0_TDLDisplayController m_DisplayController;

    // Shared menu controller — drives panel state machine, plugin lifecycle,
    // chat, callsign save, camera broadcast toggle, and marker tool panel.
    //
    // Lifecycle is gated on the local player holding *this* device, not on
    // the component's mount: there's at most one active world-space
    // controller per local player, even though every device in the world has
    // a TDL_WorldSpaceDisplayComponent on every client. Constructed when the
    // device becomes held (EnsureControllerForHeld); torn down when it stops
    // being held (TeardownControllerForUnheld). Other players' devices never
    // construct one on this client, so:
    //   * no N× fan-out on SCR_PlayerController message invokers
    //   * no plugin overlays spawning into remote devices' RT layouts
    //   * no chat-card or marker-tool widgets allocated for them
    // The RT widget + m_DisplayController are still set up eagerly so remote
    // players see a basic map on the screen mesh.
    protected ref AG0_TDLMenuController m_MenuController;
    
    // Layout paths
    [Attribute("{A13D983933B16A90}UI/layouts/Menus/TDL/TDLMenuRenderTarget.layout", UIWidgets.ResourceNamePicker, "RT Container layout", "layout")]
    protected ResourceName m_RTContainerLayout;
    
    [Attribute("{DF6A0F6906E0F330}UI/layouts/Menus/TDL/TDLMenuUI.layout", UIWidgets.ResourceNamePicker, "ATAK UI layout", "layout")]
    protected ResourceName m_ATAKLayout;
    
    // Slot name to find screen entity
    [Attribute("Screen", UIWidgets.EditBox, "Name of slot containing screen mesh")]
    protected string m_sScreenSlotName;
    
    // ============================================
    // INTERACTION CONFIG
    // ============================================
    
    [Attribute("0.001 0.075 0.10", UIWidgets.Coords, "Screen physical size (X=depth, Y=height, Z=width) in meters")]
    protected vector m_vScreenWorldSize;
    
    [Attribute("0.0 0.0 0.0", UIWidgets.Coords, "Screen offset from slot entity origin")]
    protected vector m_vScreenWorldOffset;
    
    [Attribute("0 0 0", UIWidgets.Coords, "Screen rotation offset (yaw, pitch, roll) in degrees")]
    protected vector m_vScreenRotationOffset;
    
    [Attribute("1.5", UIWidgets.Slider, "Maximum interaction distance in meters", "0.3 3.0 0.1")]
    protected float m_fMaxInteractionDistance;

    // ============================================
    // FOCUS MODE CONFIG
    // ============================================

    // Pivot the focus camera attaches to (bone or slot on this device).
    // Authored per-prefab so each device variant can frame its screen its own way.
    // Initialized against the device owner in SetupRenderTarget(), mirroring
    // AG0_TDLDeviceComponent.m_CameraAttachment's setup.
    [Attribute("", UIWidgets.Auto, "Focus camera pivot — bone/slot on this device the focus camera parents to", category: "Focus Mode")]
    ref PointInfo m_FocusCameraPivot;

    [Attribute("0.3", UIWidgets.Slider, "Focus enter/exit tween duration in seconds", "0.05 1.0 0.01", category: "Focus Mode")]
    protected float m_fFocusTweenDuration;

    [Attribute("2.0", UIWidgets.Slider, "Hard exit distance — focus drops if pivot moves further than this from saved entry camera origin (meters)", "0.5 10.0 0.1", category: "Focus Mode")]
    protected float m_fFocusMaxRange;

    //! PC mouse cursor sensitivity — multiplier on raw mouse:[xy]_rel deltas.
    //! Now normalised against CURSOR_REF_FRAME_WIDTH inside DriveCursorFromInput
    //! so the same value feels the same regardless of the RT's internal
    //! resolution (a higher-resolution RT no longer needs proportionally
    //! more cursor pixels to traverse the screen).
    //!
    //! Bumped from the old 15.0 default after live-server feedback (the
    //! cursor required multiple mouse swipes to cross the ATAK display).
    [Attribute("80.0", UIWidgets.EditBox, "PC mouse cursor sensitivity (pixels per raw delta unit, normalised to 1920px reference RT width)", category: "Focus Mode")]
    protected float m_fCursorSensitivityMouse;

    //! Gamepad cursor speed — same normalisation as mouse. Bumped from the
    //! old 1200 default for the same reason; full stick deflection now
    //! traverses the cursor across the screen in ~0.5s at the reference
    //! frame width.
    [Attribute("3500.0", UIWidgets.EditBox, "Gamepad cursor speed (pixels per second at full stick deflection, normalised to 1920px reference RT width)", category: "Focus Mode")]
    protected float m_fCursorSensitivityGamepad;

    [Attribute("0.15", UIWidgets.Slider, "Gamepad stick deadzone (0-1)", "0.0 0.5 0.01", category: "Focus Mode")]
    protected float m_fGamepadDeadzone;

    [Attribute("2.0", UIWidgets.Slider, "Focus eligibility hit-zone expansion — raycast box multiplied by this for the focus prompt, so players can trigger focus looking at the bezel/edge of the device, not just the screen surface itself. 1.0 = exact screen, 2.0 = double-size zone.", "1.0 5.0 0.1", category: "Focus Mode")]
    protected float m_fFocusEligibilityScale;

    // ============================================
    // DEBUG
    // ============================================
    
    [Attribute("0", UIWidgets.CheckBox, "Draw debug visualization of screen bounds")]
    protected bool m_bDrawDebug;
    
    [Attribute("0 1 0 1", UIWidgets.ColorPicker, "Debug box color")]
    protected ref Color m_DebugColor;
    
    // ============================================
    // INTERACTION STATE
    // ============================================
    
    protected Widget m_wCursor;
    protected Widget m_wCursorHighlight;
    protected float m_fCursorX;
    protected float m_fCursorY;
    protected bool m_bLookingAtScreen;             //!< Raycast hits the exact screen surface — drives cursor visibility.
    protected bool m_bNearScreen;                  //!< Raycast hits the expanded eligibility zone (screen + bezel) — drives focus prompt only.
    protected bool m_bInteractionEnabled;
    protected Widget m_wHoveredWidget;
    protected InputManager m_InputManager;
    
    // Drag state
    protected bool m_bDragging;
    protected bool m_bClickHandled;
    protected float m_fLastDragX;
    protected float m_fLastDragY;
    //! Sticky flag set while a drag has had IsFreehandDrawActive true at any
    //! point. Used at release to suppress the click-place path so a user
    //! who pressed TDLDraw without moving the cursor (or with TDLDraw bound
    //! to the same key as TDLScreenClick / MenuSelect) doesn't drop a stray
    //! marker. Cleared on every drag-start so each drag is evaluated fresh.
    protected bool m_bDragWasFreehandDraw;
    //! Set on first frame of a click that lands on the brightness slider.
    //! While non-null, HandleClickAndDrag drives the slider value from
    //! m_fCursorX every frame (instead of single-fire TriggerClick), since
    //! SliderWidget needs continuous mouse-move events to update its thumb
    //! and the custom cursor doesn't synthesize those. Cleared on release.
    protected Widget m_wSliderDragTarget;
    //! Cursor position at the moment HandleClickAndDrag started a drag on the
    //! map surface. Used on release to distinguish a real pan (significant
    //! displacement) from a click-without-drag — the latter is the KBM marker-
    //! placement path on world-space, mirroring the menu's drag handler's
    //! m_OnClick event. Threshold below.
    protected float m_fDragStartX;
    protected float m_fDragStartY;
    //! Pixel threshold below which a press+release counts as a click rather
    //! than a drag. Tuned for an RT-rendered cursor at the world-space's
    //! ContentFrame pixel density — small enough to catch deliberate clicks,
    //! large enough to ignore sub-pixel jitter from gamepad-stick cursor
    //! movement during press.
    protected const float MAP_CLICK_DRAG_THRESHOLD = 4.0;
    protected Widget m_wMapDragSurface;

    // ============================================
    // FOCUS MODE STATE
    // ============================================

    //! Reuses the same camera prefab the remote-feed flow uses — a bare CameraBase.
    //! No PIP wiring needed; we just make it the active render camera via CameraManager.SetCamera.
    protected const ResourceName FOCUS_CAMERA_PREFAB = "{F3CDC6E4F329E496}Prefabs/Characters/Core/TDLDevicePlayerCamera.et";

    protected TDL_EFocusState m_eFocusState = TDL_EFocusState.IDLE;
    protected CameraBase m_OriginalCamera;
    protected IEntity m_SpawnedFocusCamera;
    protected vector m_StartCameraTransform[4];   //!< Saved player-camera transform at the moment of entry (also used as exit target start frame)
    protected float m_fTweenElapsed;
    protected vector m_EntryCameraOrigin;          //!< For the FOCUS_MAX_RANGE auto-exit check
    protected bool m_bFocusCameraAttachedToPivot;  //!< Tracks whether AddChild has fired so we only detach if we attached
    protected IEntity m_EntryControlledEntity;     //!< Snapshot of GetControlledEntity at focus entry — exits if it changes mid-focus
    protected bool m_bDOFWasEnabledOnEntry;        //!< Tracks whether we toggled the DOF info display, so we only restore if we suppressed
    protected float m_fEntryPlayerFOV;             //!< Player camera vertical FOV at the moment of focus entry — start frame of ENTERING tween, end frame target of EXITING tween (re-sampled live during exit)
    protected float m_fFocusNativeFOV;             //!< Focus camera prefab's natural vertical FOV — end frame target of ENTERING tween, start frame of EXITING tween
    protected float m_fCachedFrameW;               //!< ContentFrame screen width captured once after layout settles — used for cursor clamp + raycast UV scaling. Per-frame GetScreenSize fluctuates when the MapCanvas re-lays out on zoom, which made the cursor speed feel tied to map zoom.
    protected float m_fCachedFrameH;               //!< ContentFrame screen height, same caching reason as above.
    
    // ============================================
    // LIFECYCLE
    // ============================================
    override bool OnTicksOnRemoteProxy() { return true; }
	
    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);

        // Visuals only on the local machine.
        if (!System.IsConsoleApp())
        {
            SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
            m_InputManager = GetGame().GetInputManager();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        
		if (owner.GetWorld() != GetGame().GetWorld()) //Bacon fix until I polish this
        	return;
        // Defer setup to ensure slots are populated
        GetGame().GetCallqueue().CallLater(SetupRenderTarget, 100, false, owner);
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnFrame(IEntity owner, float timeSlice)
    {
		if (owner.GetWorld() != GetGame().GetWorld()) //Bacon fix?
        	return;
		
        // MULTIPLAYER OPTIMIZATION: Only process if this device is held by local player.
        // Other players' devices simulate locally but we skip expensive raycast/UI work.
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        bool isHeld = pc && pc.IsHeldDevice(owner);

        // Held-state transition: spin the menu controller up when the local
        // player picks this device up, tear it down when they drop it. There
        // should be at most one active controller per local player — and
        // here, since every device in the world hosts a
        // TDL_WorldSpaceDisplayComponent on every client, we gate construction
        // on hold rather than on component mount.
        //
        // Gate is `isHeld vs controller-existence` rather than tracking the
        // previous frame's isHeld separately: this auto-retries Ensure if
        // m_wRoot wasn't ready yet (CreateRenderTarget is deferred 100ms via
        // CallLater on EOnInit, so a fast pickup before that callback fires
        // would otherwise lose the transition).
        bool controllerActive = m_MenuController != null;
        if (isHeld && !controllerActive)
            EnsureControllerForHeld(owner);
        else if (!isHeld && controllerActive)
            TeardownControllerForUnheld();

        if (!isHeld)
        {
            // Not our device - ensure clean state and bail.
            // Includes auto-cleanup of focus if we were focused when the device
            // left our possession (dropped, swapped, taken on death). Synchronous
            // exit so we never leave a stranded focus camera as the active render
            // target on a device we no longer hold.
            if (m_eFocusState != TDL_EFocusState.IDLE)
                ExitFocus(true);

            if (m_bLookingAtScreen)
                SetLookingAtScreen(false);
            m_bNearScreen = false;
            return;
        }

        // Focus state machine tick — runs every frame the device is held, regardless
        // of whether interaction is enabled. The tween must keep advancing even if
        // the player tabs out / opens a menu, otherwise the camera gets stuck mid-blend.
        UpdateFocus(timeSlice);
        
        // Update the display controller each frame
        if (m_DisplayController)
            m_DisplayController.Update(timeSlice);

        // Bloodhound — push this frame's cursor world position to the
        // controller BEFORE its Tick() runs so UpdateBloodhound sees a
        // fresh override. On world-space the cursor's world position is
        // m_fCursorX/Y (in ContentFrame screen pixels) projected through
        // the MapView's ScreenToWorld after subtracting the MapCanvas's
        // screen offset. Menu doesn't push — its UpdateBloodhound falls
        // back to mapView.GetCenter().
        if (m_MenuController)
        {
            if (AG0_TDLMenuController.GetBloodhoundEnabled())
            {
                vector cursorWorld;
                if (TryGetBloodhoundCursorWorld(cursorWorld))
                    m_MenuController.SetBloodhoundCursorWorld(cursorWorld);
                else
                    m_MenuController.ClearBloodhoundCursorWorld();
            }
            else
            {
                m_MenuController.ClearBloodhoundCursorWorld();
            }

            // Shape draw ghost + delete-sweep cursor — push the cursor
            // world position so the shape session's rubber-band tracks
            // between clicks and so the delete sweep has a position to
            // test against. OnShapeCursorWorldFromWorldSpace gates
            // internally on shape OR delete sub-mode, so calling
            // unconditionally is safe and keeps the hot path the same
            // shape on both frontends. Reuses the MapCanvas →
            // ScreenToWorld plumbing as the bloodhound path; the
            // function name is a holdover from when it had a single
            // caller.
            vector shapeCursorWorld;
            if (TryGetBloodhoundCursorWorld(shapeCursorWorld))
                m_MenuController.OnShapeCursorWorldFromWorldSpace(shapeCursorWorld);
        }

        // Tick the shared menu controller — drives plugins' OnMenuUpdate,
        // image-card rendering, periodic canvas redraw, scroll-to-bottom.
        // Marker tool action poll + camera button state are folded in too
        // (post Phase 4/5). InputManager is the local player's; safe even
        // when the player isn't focused on the device (Tick is cheap).
        if (m_MenuController)
            m_MenuController.Tick(timeSlice, m_InputManager);

        // Re-apply NOFOCUS to catch widgets spawned this frame by the display
        // controller (member cards on 1Hz refresh, markers, etc.) or by plugins
        // (overlays, panel content). DisableFocusRecursive at init only covers
        // the static layout; dynamic widgets get added bare-focusable, which
        // leaks gamepad focus to other menus (vanilla, Bacon, etc.). SetFlags
        // is idempotent — re-setting NOFOCUS on already-flagged widgets is a
        // no-op, so the cost is just the tree walk.
        if (m_wRTContainer)
            DisableFocusRecursive(m_wRTContainer);

        // Update interaction (raycast, cursor, hover)
        if (m_bInteractionEnabled && m_ScreenEntity)
            UpdateInteraction(timeSlice);
        
        // Debug visualization
        if (m_bDrawDebug)
            DrawDebug();
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnDelete(IEntity owner)
    {
        Cleanup();
        super.OnDelete(owner);
    }
    
    // ============================================
    // RENDER TARGET SETUP
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected void SetupRenderTarget(IEntity owner)
    {
        // Find the screen entity from slot
        SlotManagerComponent slotMgr = SlotManagerComponent.Cast(owner.FindComponent(SlotManagerComponent));
        if (!slotMgr)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No SlotManagerComponent found", LogLevel.ERROR);
            return;
        }
        
        // Get screen entity from named slot
        EntitySlotInfo screenSlot = slotMgr.GetSlotByName(m_sScreenSlotName);
        if (!screenSlot)
        {
            Print(string.Format("[TDL_WorldSpaceDisplay] FAIL: No '%1' slot found", m_sScreenSlotName), LogLevel.ERROR);
            return;
        }
        
        m_ScreenEntity = screenSlot.GetAttachedEntity();
        if (!m_ScreenEntity)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No entity in Screen slot", LogLevel.ERROR);
            return;
        }

        // Resolve the focus-camera pivot against the device owner. Matches the
        // pattern AG0_TDLDeviceComponent uses for m_CameraAttachment.Init(owner)
        // — the authored PointInfo references a bone/slot on the device mesh,
        // and Init resolves the bone NodeId so GetWorldTransform/GetLocalTransform
        // work later. Safe no-op if the pivot wasn't authored on this prefab;
        // we just won't be focus-eligible until it is.
        if (m_FocusCameraPivot)
            m_FocusCameraPivot.Init(owner);

        // Create the RTTextureWidget and bind to screen
        CreateRenderTarget();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CreateRenderTarget()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No workspace", LogLevel.ERROR);
            return;
        }
        
        // Create RT container from layout
        m_wRTContainer = workspace.CreateWidgets(m_RTContainerLayout);
        if (!m_wRTContainer)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Could not create RT container layout", LogLevel.ERROR);
            return;
        }
        
        // Find the RTTextureWidget inside the layout (it's a child, not the root)
        Widget rtWidget = m_wRTContainer.FindAnyWidget("RTTexture0");
        if (!rtWidget)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No RTTexture0 found in layout", LogLevel.ERROR);
            m_wRTContainer.RemoveFromHierarchy();
            m_wRTContainer = null;
            return;
        }
        
        m_RTWidget = RTTextureWidget.Cast(rtWidget);
        if (!m_RTWidget)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: RTTexture0 is not RTTextureWidget", LogLevel.ERROR);
            m_wRTContainer.RemoveFromHierarchy();
            m_wRTContainer = null;
            return;
        }
        
        // Find the content frame inside the RT widget
        m_wContentFrame = m_RTWidget.FindAnyWidget("ContentFrame");
        if (!m_wContentFrame)
        {
            // If no content frame, use the RT widget directly as parent
            Print("[TDL_WorldSpaceDisplay] No ContentFrame, using RTWidget directly", LogLevel.WARNING);
            m_wContentFrame = m_RTWidget;
        }
        
        // Load ATAK layout as child of content frame
        m_wRoot = workspace.CreateWidgets(m_ATAKLayout, m_wContentFrame);
        if (!m_wRoot)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Could not create ATAK layout", LogLevel.ERROR);
            return;
        }

        // If we fell back to the RT widget as our content frame, re-point at the
        // spawned ATAK layout root now that it exists. m_wRoot is a proper
        // FrameWidget; the RT widget is not. FrameSlot.SetPos behavior on a
        // non-Frame parent is unreliable — observed symptom: cursor speed scaled
        // by map-zoom-driven layout reflows. Routing both the cursor's parent
        // AND TransformWorldToUI's screen-size source through a real FrameWidget
        // gives the cursor a stable coordinate space.
        if (m_wContentFrame == m_RTWidget)
            m_wContentFrame = m_wRoot;

        // CRITICAL: Disable focus on all widgets to prevent interference with other menus
        // We handle interaction via raycasting, not the normal focus system
        DisableFocusRecursive(m_wRTContainer);
        
        // Bind to the screen mesh
        m_RTWidget.SetRenderTarget(m_ScreenEntity);
        
        Print("[TDL_WorldSpaceDisplay] Render target bound, initializing display controller...", LogLevel.DEBUG);
        
        // Initialize the display controller with the ATAK layout
        m_DisplayController = new AG0_TDLDisplayController();
        if (!m_DisplayController.Init(m_wRoot))
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Display controller init failed", LogLevel.ERROR);
            m_DisplayController = null;
            return;
        }

        // Menu controller construction is held-state-gated — see
        // EnsureControllerForHeld(). The display controller above is eagerly
        // set up so the screen mesh has *something* to render (basic map)
        // even on remote players' devices that this client doesn't drive,
        // but plugins / chat subscriptions / callsign / marker tool wiring
        // only spin up when the local player picks the device up.

        // Setup interaction system
        SetupInteraction();
        
        Print("[TDL_WorldSpaceDisplay] World-space ATAK display initialized successfully", LogLevel.DEBUG);
    }
    
    // ============================================
    // INTERACTION SETUP
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected void SetupInteraction()
    {
        if (!m_wRoot)
            return;

        // Create cursor widgets
        CreateCursor();

        // Hook button callbacks on this layout instance
        HookButtonHandlers();

        // Find drag surface for map panning
        m_wMapDragSurface = m_wRoot.FindAnyWidget("MapDragSurface");

        m_bInteractionEnabled = true;
    }

    // ============================================
    // CONTROLLER LIFECYCLE — held-state-gated
    // ============================================

    //------------------------------------------------------------------------------------------------
    //! Spin up the menu controller for this device. Idempotent: if a
    //! controller already exists, this is a no-op. Bails silently if the RT
    //! widget tree isn't ready yet (CreateRenderTarget is deferred 100ms on
    //! EOnInit) — the next held-transition check will retry.
    //!
    //! Owner is the device entity. We use it to find this gadget's own ATAK
    //! device component for SetActiveDevice / SetNetworkDevice — the world-
    //! space controller represents *this* device, unlike the menu which
    //! picks the player's currently-active held ATAK device.
    protected void EnsureControllerForHeld(IEntity owner)
    {
        if (m_MenuController)
            return;
        if (!m_wRoot)
            return;

        m_MenuController = new AG0_TDLMenuController();
        if (!m_MenuController.Init(m_wRoot))
        {
            // Init failed — drop the ref so the next transition can retry
            // from scratch instead of holding a half-built controller.
            m_MenuController = null;
            return;
        }

        AG0_TDLDeviceComponent ownDevice = AG0_TDLDeviceComponent.Cast(owner.FindComponent(AG0_TDLDeviceComponent));
        m_MenuController.SetActiveDevice(ownDevice);
        m_MenuController.SetNetworkDevice(ownDevice);
        m_MenuController.SetDisplayController(m_DisplayController);
        // World-space frontend owns the device screen — opt into the
        // brightness overlay so the slider in the settings panel actually
        // dims this RT layout. Fullscreen menu deliberately doesn't opt in:
        // it's already its own dark UI and dimming it would just obscure the
        // controls. SetApplyBrightness applies the current persisted value
        // immediately, so swapping to a held device picks up the last
        // brightness the player set.
        m_MenuController.SetApplyBrightness(true);
        m_MenuController.RefreshPlugins();
        m_MenuController.RestoreState();
        // Subscribe to message updates so chat / badge state stays live for
        // the duration of the player holding this device. Per-instance
        // subscription — paired with TeardownControllerForUnheld below.
        m_MenuController.SubscribeToMessageUpdates();
    }

    //------------------------------------------------------------------------------------------------
    //! Tear down the menu controller when the local player drops / swaps off
    //! this device. Idempotent: no-op if no controller exists. Plugins fire
    //! OnMenuClosed via DisablePlugins inside Cleanup so any spawned overlays
    //! (PTT etc.) are removed from this device's RT layout cleanly.
    protected void TeardownControllerForUnheld()
    {
        if (!m_MenuController)
            return;

        m_MenuController.UnsubscribeFromMessageUpdates();
        m_MenuController.Cleanup();
        m_MenuController = null;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Register button callbacks on this layout's widget instances
    protected void HookButtonHandlers()
    {
        if (!m_wRoot)
            return;
        
        // Zoom controls
        Widget zoomIn = m_wRoot.FindAnyWidget("ZoomInButton");
        if (zoomIn)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                zoomIn.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnZoomInClicked);
        }
        
        Widget zoomOut = m_wRoot.FindAnyWidget("ZoomOutButton");
        if (zoomOut)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                zoomOut.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnZoomOutClicked);
        }
        
        // Compass button
        Widget compass = m_wRoot.FindAnyWidget("CompassButton");
        if (compass)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                compass.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnCompassClicked);
        }
        
        // Track button
        Widget track = m_wRoot.FindAnyWidget("TrackButton");
        if (track)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                track.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnTrackClicked);
        }
        
        // NetworkButton, BackButton, SettingsButton, SettingsBackButton, and
        // MarkerToolButton are wired by AG0_TDLMenuController during its Init
        // — don't double-hook here. Their click handlers go through the
        // shared panel state machine.
    }
    
    //------------------------------------------------------------------------------------------------
    // BUTTON CALLBACKS - must match SCR_ModularButtonComponent invoker signature
    //------------------------------------------------------------------------------------------------
    
    protected void OnZoomInClicked(SCR_ModularButtonComponent comp)
    {
        if (!m_DisplayController)
            return;
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
            mapView.ZoomIn(0.05);
    }
    
    protected void OnZoomOutClicked(SCR_ModularButtonComponent comp)
    {
        if (!m_DisplayController)
            return;
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
            mapView.ZoomOut(0.05);
    }
    
    protected void OnCompassClicked(SCR_ModularButtonComponent comp)
    {
        AG0_TDLDisplayController.SetTrackUp(!AG0_TDLDisplayController.GetTrackUp());
    }
    
    protected void OnTrackClicked(SCR_ModularButtonComponent comp)
    {
        AG0_TDLDisplayController.SetPlayerTracking(!AG0_TDLDisplayController.GetPlayerTracking());
    }
    
    // OnNetworkClicked and OnBackButtonClicked moved to AG0_TDLMenuController
    // — both frontends share the same panel state machine, so these no longer
    // exist as bespoke world-space handlers.
    
    //------------------------------------------------------------------------------------------------
    protected void CreateCursor()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace || !m_wContentFrame)
            return;
        
        // Create cursor container
        m_wCursor = workspace.CreateWidget(
            WidgetType.FrameWidgetTypeID,
            WidgetFlags.VISIBLE,
            Color.White,
            0,
            m_wContentFrame
        );
        
        if (!m_wCursor)
            return;
        
        // Cursor dot
        float cursorSize = 8;
        FrameSlot.SetSize(m_wCursor, cursorSize, cursorSize);
        FrameSlot.SetAlignment(m_wCursor, 0.5, 0.5);
        
        // Inner dot image
        ImageWidget cursorDot = ImageWidget.Cast(workspace.CreateWidget(
            WidgetType.ImageWidgetTypeID,
            WidgetFlags.VISIBLE | WidgetFlags.STRETCH,
            Color.FromRGBA(255, 255, 255, 220),
            0,
            m_wCursor
        ));
        
        if (cursorDot)
        {
            FrameSlot.SetSize(cursorDot, cursorSize, cursorSize);
            FrameSlot.SetPos(cursorDot, 0, 0);
        }
        
        // Highlight ring (shows when hovering clickable)
        m_wCursorHighlight = workspace.CreateWidget(
            WidgetType.ImageWidgetTypeID,
            WidgetFlags.VISIBLE,
            Color.FromRGBA(100, 200, 255, 120),
            0,
            m_wCursor
        );
        
        if (m_wCursorHighlight)
        {
            float highlightSize = 20;
            FrameSlot.SetSize(m_wCursorHighlight, highlightSize, highlightSize);
            FrameSlot.SetPos(m_wCursorHighlight, -(highlightSize - cursorSize) * 0.5, -(highlightSize - cursorSize) * 0.5);
            m_wCursorHighlight.SetVisible(false);
        }
        
        // Start hidden until looking at screen
        m_wCursor.SetVisible(false);
    }
    
    // ============================================
    // INTERACTION UPDATE (per-frame)
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateInteraction(float timeSlice)
    {
		MenuManager menuManager = GetGame().GetMenuManager();
        if (menuManager && menuManager.GetTopMenu())
        {
            if (m_bLookingAtScreen)
                SetLookingAtScreen(false);
            m_bNearScreen = false;
            m_bDragging = false;
            m_bClickHandled = false;
            return;
        }

        // Focus mode short-circuits the camera-raycast path. While ACTIVE, the
        // cursor is driven by TDLCursor* action deltas (mouse + right stick) and
        // the screen-look state is forced true so all the downstream hover/click/
        // drag code in this function continues to operate against m_fCursorX/Y
        // exactly as it does in the raycast path.
        //
        // We intentionally skip this branch during ENTERING/EXITING — the camera
        // is mid-tween and the user can't usefully click during the ~0.3 s blend,
        // so showing the cursor would be visually noisy.
        if (m_eFocusState == TDL_EFocusState.ACTIVE)
        {
            if (m_InputManager)
            {
                // TDLFocusContext (priority 100, Exclusive) owns the TDLCursor* actions.
                // It must be activated every frame focus is ACTIVE — its Exclusive flag
                // consumes the same physical mouse/stick input the player camera would
                // otherwise grab, which is what gives focus mode its "the screen is the
                // cursor surface" feel without locking the player's body.
                m_InputManager.ActivateContext("TDLFocusContext");
                // TDLScreenContext (priority 100, non-exclusive) keeps MenuSelect /
                // MenuBack / TDLScreenClick live so HandleClickAndDrag below behaves
                // identically to the raycast path.
                m_InputManager.ActivateContext("TDLScreenContext");
            }
            // Use EOnFrame's actual timeSlice (plumbed through UpdateInteraction)
            // instead of the previous hardcoded 0.016 — that hardcode made the
            // gamepad cursor speed frame-rate-dependent: too slow at 144fps, too
            // fast at 30fps. Mouse path doesn't read ts (it's already per-frame
            // delta), so this only affects the stick branch.
            DriveCursorFromInput(timeSlice);
            UpdateFocusCursorWidget();
            SetLookingAtScreen(true);
            UpdateHoveredWidget();
            HandleClickAndDrag();
            return;
        }

        // Suppress cursor / clicks during the focus tween — the camera is in
        // motion and any clicks the user fires would land on the wrong widget.
        if (m_eFocusState == TDL_EFocusState.ENTERING || m_eFocusState == TDL_EFocusState.EXITING)
        {
            if (m_wCursor)
                m_wCursor.SetVisible(false);
            m_bDragging = false;
            m_bClickHandled = false;
            return;
        }

        // Get camera ray
        vector camOrigin, camDir;
        if (!GetCameraRay(camOrigin, camDir))
        {
            SetLookingAtScreen(false);
            m_bNearScreen = false;
            m_bDragging = false;
            m_bClickHandled = false;
            return;
        }

        // Test intersection with screen — exact size for cursor / click logic.
        float hitFraction = TraceToScreen(camOrigin, camDir);

        if (hitFraction >= 0 && hitFraction <= 1)
        {
            // Calculate world hit point
            vector hitPoint = camOrigin + camDir * (hitFraction * m_fMaxInteractionDistance);

            // Transform to UI coordinates
            TransformWorldToUI(hitPoint, m_fCursorX, m_fCursorY);

            // Update cursor position
            if (m_wCursor)
            {
                // m_fCursorX/Y are in screen pixels for hit detection
                // FrameSlot.SetPos expects layout coordinates, so DPIUnscale here
                WorkspaceWidget workspace = GetGame().GetWorkspace();
                float layoutX = workspace.DPIUnscale(m_fCursorX);
                float layoutY = workspace.DPIUnscale(m_fCursorY);
                FrameSlot.SetPos(m_wCursor, layoutX, layoutY);
                m_wCursor.SetVisible(true);
            }

            SetLookingAtScreen(true);
            // Inner hit always implies near hit — same physical region.
            m_bNearScreen = true;
            UpdateHoveredWidget();

            // Click/drag input handling — extracted to HandleClickAndDrag so the
            // focus-mode branch can reuse it. Behavior is identical in both paths:
            // mouse0 / right-trigger drives clicks and map pan; cursor state is
            // m_fCursorX/Y regardless of whether that came from raycast or input.
            if (m_InputManager)
            {
                m_InputManager.ActivateContext("TDLScreenContext");
                HandleClickAndDrag();
            }
        }
        else
        {
            SetLookingAtScreen(false);
            m_bDragging = false;
            m_bClickHandled = false;

            // Second trace against the expanded eligibility box. Only used for
            // the focus prompt — players can trigger focus when looking at the
            // device bezel without needing pixel-accurate aim on the screen
            // itself. No cursor here, so the cost is just an AABB test.
            // Skip the second trace if the scale is effectively 1.0 (no expansion).
            if (m_fFocusEligibilityScale > 1.001)
            {
                float nearHit = TraceToScreen(camOrigin, camDir, m_fFocusEligibilityScale);
                m_bNearScreen = (nearHit >= 0 && nearHit <= 1);
            }
            else
            {
                m_bNearScreen = false;
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void SetLookingAtScreen(bool looking)
    {
        if (m_bLookingAtScreen == looking)
            return;
        
        m_bLookingAtScreen = looking;
        
        if (!looking)
        {
            if (m_wCursor)
                m_wCursor.SetVisible(false);
            
            ClearHoveredWidget();
        }
    }
    
    // ============================================
    // RAYCAST & COORDINATE TRANSFORMATION
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected bool GetCameraRay(out vector origin, out vector direction)
    {
        CameraManager camMgr = GetGame().GetCameraManager();
        if (!camMgr)
            return false;
        
        CameraBase camera = camMgr.CurrentCamera();
        if (!camera)
            return false;
        
        vector camMat[4];
        camera.GetTransform(camMat);
        
        origin = camMat[3];
        direction = camMat[2]; // Forward
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Returns hit fraction (0-1) if ray intersects screen box, -1 otherwise.
    //! Default behavior unchanged — scale==1.0 tests against the exact screen size.
    //! Larger scales expand the height + width of the test box for the focus
    //! eligibility check, letting the player trigger focus when looking at the
    //! device's bezel rather than only the screen surface. Depth is NOT scaled —
    //! we still want a thin slab so glancing rays parallel to the screen plane
    //! don't register.
    protected float TraceToScreen(vector origin, vector direction, float scale = 1.0)
    {
        // Get adjusted screen transform with rotation offset applied
        vector screenMat[4];
        GetAdjustedScreenTransform(screenMat);

        // Convert to quaternion for rotation
        float screenQuat[4];
        Math3D.MatrixToQuat(screenMat, screenQuat);

        // Inverse quaternion for world-to-local
        float screenQuatInv[4];
        Math3D.QuatInverse(screenQuatInv, screenQuat);

        vector screenOrigin = screenMat[3];

        // Transform ray to screen local space
        vector rayStart = origin - screenOrigin;
        vector rayEnd = (origin + direction * m_fMaxInteractionDistance) - screenOrigin;

        rayStart = SCR_Math3D.QuatMultiply(screenQuatInv, rayStart);
        rayEnd = SCR_Math3D.QuatMultiply(screenQuatInv, rayEnd);

        // Apply screen offset (in local space)
        rayStart = rayStart - m_vScreenWorldOffset;
        rayEnd = rayEnd - m_vScreenWorldOffset;

        // Screen bounding box (X = normal/depth, Y = height, Z = width)
        vector boxMin = Vector(
            -0.001,
            -m_vScreenWorldSize[1] * 0.5 * scale,
            -m_vScreenWorldSize[2] * 0.5 * scale
        );
        vector boxMax = Vector(
            0.001,
            m_vScreenWorldSize[1] * 0.5 * scale,
            m_vScreenWorldSize[2] * 0.5 * scale
        );

        return Math3D.IntersectionRayBox(rayStart, rayEnd, boxMin, boxMax);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Gets the screen entity transform with rotation offset applied
    protected void GetAdjustedScreenTransform(out vector mat[4])
    {
        // Start with screen entity transform
        m_ScreenEntity.GetTransform(mat);
        
        // Apply rotation offset if any
        if (m_vScreenRotationOffset != vector.Zero)
        {
            // Convert offset angles to rotation matrix, then to quaternion
            vector offsetMat[3];
            Math3D.AnglesToMatrix(m_vScreenRotationOffset, offsetMat);
            
            float offsetQuat[4];
            Math3D.MatrixToQuat(offsetMat, offsetQuat);
            
            // Get current rotation as quaternion
            float currentQuat[4];
            Math3D.MatrixToQuat(mat, currentQuat);
            
            // Combine rotations (offset is applied in local space)
            float combinedQuat[4];
            Math3D.QuatMultiply(combinedQuat, currentQuat, offsetQuat);
            
            // Convert back to matrix, preserving position
            vector pos = mat[3];
            Math3D.QuatToMatrix(combinedQuat, mat);
            mat[3] = pos;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Transforms world hit point to UI pixel coordinates
    protected void TransformWorldToUI(vector worldHit, out float uiX, out float uiY)
    {
        // Get adjusted screen transform
        vector screenMat[4];
        GetAdjustedScreenTransform(screenMat);
        
        float screenQuat[4];
        Math3D.MatrixToQuat(screenMat, screenQuat);
        
        float screenQuatInv[4];
        Math3D.QuatInverse(screenQuatInv, screenQuat);
        
        // Transform to local space
        vector localHit = worldHit - screenMat[3];
        localHit = SCR_Math3D.QuatMultiply(screenQuatInv, localHit);
        localHit = localHit - m_vScreenWorldOffset;
        
        // Get ContentFrame size for pixel scaling (cursor lives in ContentFrame's coordinate space).
        // Cached — see GetCachedFrameSize doc for why we don't query GetScreenSize per-frame.
        float frameW, frameH;
        GetCachedFrameSize(frameW, frameH);
        
        // Normalize to 0-1 UV (Z = horizontal, Y = vertical)
        float u = (localHit[2] / m_vScreenWorldSize[2]) + 0.5;
        float v = 1.0 - ((localHit[1] / m_vScreenWorldSize[1]) + 0.5); // Flip Y
        
        // Clamp to valid range
        u = Math.Clamp(u, 0, 1);
        v = Math.Clamp(v, 0, 1);
        
        // Scale to screen pixels
        uiX = u * frameW;
        uiY = v * frameH;
    }
    
    // ============================================
    // DEBUG VISUALIZATION
    // ============================================
    
#ifdef WORKBENCH
    //------------------------------------------------------------------------------------------------
    //! Workbench editor update - runs in prefab editor
    override void _WB_AfterWorldUpdate(IEntity owner, float timeSlice)
    {
        if (m_bDrawDebug)
            DrawDebug();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawDebug()
    {
        // Get transform - use adjusted if screen entity available, otherwise owner
        vector mat[4];
        
        if (m_ScreenEntity)
        {
            GetAdjustedScreenTransform(mat);
        }
        else
        {
            // In editor, try to find screen entity via slot
            IEntity owner = GetOwner();
            if (!owner)
                return;
            
            SlotManagerComponent slotMgr = SlotManagerComponent.Cast(owner.FindComponent(SlotManagerComponent));
            if (slotMgr)
            {
                EntitySlotInfo screenSlot = slotMgr.GetSlotByName(m_sScreenSlotName);
                if (screenSlot)
                {
                    IEntity screenEnt = screenSlot.GetAttachedEntity();
                    if (screenEnt)
                    {
                        screenEnt.GetTransform(mat);
                        
                        // Apply rotation offset
                        if (m_vScreenRotationOffset != vector.Zero)
                        {
                            vector offsetMat[3];
                            Math3D.AnglesToMatrix(m_vScreenRotationOffset, offsetMat);
                            
                            float offsetQuat[4];
                            Math3D.MatrixToQuat(offsetMat, offsetQuat);
                            
                            float currentQuat[4];
                            Math3D.MatrixToQuat(mat, currentQuat);
                            
                            float combinedQuat[4];
                            Math3D.QuatMultiply(combinedQuat, currentQuat, offsetQuat);
                            
                            vector pos = mat[3];
                            Math3D.QuatToMatrix(combinedQuat, mat);
                            mat[3] = pos;
                        }
                    }
                    else
                    {
                        // No screen entity, use owner transform
                        owner.GetTransform(mat);
                    }
                }
                else
                {
                    owner.GetTransform(mat);
                }
            }
            else
            {
                owner.GetTransform(mat);
            }
        }
        
        // Calculate origin with offset applied in local space
        vector origin = mat[3] + mat[0] * m_vScreenWorldOffset[0] + mat[1] * m_vScreenWorldOffset[1] + mat[2] * m_vScreenWorldOffset[2];
        
        // Screen half-extents
        float halfDepth = 0.001;
        float halfHeight = m_vScreenWorldSize[1] * 0.5;
        float halfWidth = m_vScreenWorldSize[2] * 0.5;
        
        // Calculate 8 corners of the box in world space
        // Box is oriented with X = normal, Y = height, Z = width
        vector corners[8];
        
        // Front face (positive X / toward viewer)
        corners[0] = origin + mat[0] * halfDepth - mat[1] * halfHeight - mat[2] * halfWidth; // bottom-left
        corners[1] = origin + mat[0] * halfDepth - mat[1] * halfHeight + mat[2] * halfWidth; // bottom-right
        corners[2] = origin + mat[0] * halfDepth + mat[1] * halfHeight + mat[2] * halfWidth; // top-right
        corners[3] = origin + mat[0] * halfDepth + mat[1] * halfHeight - mat[2] * halfWidth; // top-left
        
        // Back face (negative X)
        corners[4] = origin - mat[0] * halfDepth - mat[1] * halfHeight - mat[2] * halfWidth;
        corners[5] = origin - mat[0] * halfDepth - mat[1] * halfHeight + mat[2] * halfWidth;
        corners[6] = origin - mat[0] * halfDepth + mat[1] * halfHeight + mat[2] * halfWidth;
        corners[7] = origin - mat[0] * halfDepth + mat[1] * halfHeight - mat[2] * halfWidth;
        
        int shapeFlags = ShapeFlags.ONCE | ShapeFlags.NOZBUFFER;
        int color = m_DebugColor.PackToInt();
        
        // Build line array for box wireframe (12 edges * 2 points = 24 points)
        vector lines[24];
        
        // Front face
        lines[0] = corners[0]; lines[1] = corners[1];
        lines[2] = corners[1]; lines[3] = corners[2];
        lines[4] = corners[2]; lines[5] = corners[3];
        lines[6] = corners[3]; lines[7] = corners[0];
        
        // Back face
        lines[8] = corners[4]; lines[9] = corners[5];
        lines[10] = corners[5]; lines[11] = corners[6];
        lines[12] = corners[6]; lines[13] = corners[7];
        lines[14] = corners[7]; lines[15] = corners[4];
        
        // Connecting edges
        lines[16] = corners[0]; lines[17] = corners[4];
        lines[18] = corners[1]; lines[19] = corners[5];
        lines[20] = corners[2]; lines[21] = corners[6];
        lines[22] = corners[3]; lines[23] = corners[7];
        
        Shape.CreateLines(color, shapeFlags, lines, 24);
        
        // Draw center point
        Shape.CreateSphere(ARGB(255, 255, 255, 0), shapeFlags, origin, 0.005);
        
        // Draw normal arrow (X direction = red)
        vector normalEnd = origin + mat[0] * 0.05;
        Shape.CreateArrow(origin, normalEnd, 0.01, ARGB(255, 255, 0, 0), shapeFlags);
        
        // Draw up arrow (Y direction = green)
        vector upEnd = origin + mat[1] * 0.03;
        Shape.CreateArrow(origin, upEnd, 0.008, ARGB(255, 0, 255, 0), shapeFlags);
        
        // Draw right arrow (Z direction = blue)  
        vector rightEnd = origin + mat[2] * 0.03;
        Shape.CreateArrow(origin, rightEnd, 0.008, ARGB(255, 0, 0, 255), shapeFlags);
        
        // If looking at screen, draw cursor ray
        if (m_bLookingAtScreen)
        {
            vector camOrigin, camDir;
            if (GetCameraRay(camOrigin, camDir))
            {
                vector rayEnd = camOrigin + camDir * m_fMaxInteractionDistance;
                vector rayLine[2];
                rayLine[0] = camOrigin;
                rayLine[1] = rayEnd;
                Shape.CreateLines(ARGB(255, 255, 255, 255), shapeFlags, rayLine, 2);
            }
        }
    }
#else
    protected void DrawDebug() {}
#endif
    
    // ============================================
    // WIDGET HIT DETECTION
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateHoveredWidget()
    {
        Widget newHovered = FindClickableAt(m_fCursorX, m_fCursorY);

        if (newHovered != m_wHoveredWidget)
        {
            // Fire mouse-leave + focus-lost on the old widget. Order matches
            // the engine's hover-exit pair so components that drive
            // animation state off either event (e.g. SCR_ButtonBaseComponent
            // .OnMouseLeave de-tints the background, OnFocusLost clears the
            // border) update consistently.
            if (m_wHoveredWidget)
            {
                ScriptedWidgetEventHandler handler = m_wHoveredWidget.FindHandler(ScriptedWidgetEventHandler);
                if (handler)
                {
                    handler.OnMouseLeave(m_wHoveredWidget, newHovered, 0, 0);
                    handler.OnFocusLost(m_wHoveredWidget, 0, 0);
                }
            }

            m_wHoveredWidget = newHovered;

            // Fire mouse-enter + focus on the new widget. SCR_PagingButtonComponent
            // (spinbox arrows) and other WLib button components track a
            // "is hovered" state via OnMouseEnter / OnMouseLeave, and some
            // bail out of OnClick when they don't think they're hovered —
            // firing both events keeps the world-space cursor behaviourally
            // equivalent to the engine's normal mouse pointer.
            if (m_wHoveredWidget)
            {
                ScriptedWidgetEventHandler handler = m_wHoveredWidget.FindHandler(ScriptedWidgetEventHandler);
                if (handler)
                {
                    handler.OnFocus(m_wHoveredWidget, m_fCursorX, m_fCursorY);
                    handler.OnMouseEnter(m_wHoveredWidget, m_fCursorX, m_fCursorY);
                }
            }
        }

        // Update highlight visibility
        if (m_wCursorHighlight)
            m_wCursorHighlight.SetVisible(m_wHoveredWidget != null);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ClearHoveredWidget()
    {
        if (m_wHoveredWidget)
        {
            ScriptedWidgetEventHandler handler = m_wHoveredWidget.FindHandler(ScriptedWidgetEventHandler);
            if (handler)
            {
                handler.OnMouseLeave(m_wHoveredWidget, null, 0, 0);
                handler.OnFocusLost(m_wHoveredWidget, 0, 0);
            }
        }

        m_wHoveredWidget = null;

        if (m_wCursorHighlight)
            m_wCursorHighlight.SetVisible(false);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Find topmost clickable widget at pixel coordinates
    protected Widget FindClickableAt(float x, float y)
    {
        if (!m_wRoot)
            return null;
        
        return FindClickableRecursive(m_wRoot, x, y);
    }
    
    //------------------------------------------------------------------------------------------------
    protected Widget FindClickableRecursive(Widget parent, float x, float y)
    {
        if (!parent || !parent.IsVisible())
            return null;
        
        // Collect children into array for reverse traversal (front-to-back)
        ref array<Widget> children = {};
        Widget child = parent.GetChildren();
        while (child)
        {
            children.Insert(child);
            child = child.GetSibling();
        }
        
        // Check children in reverse order (topmost first)
        for (int i = children.Count() - 1; i >= 0; i--)
        {
            Widget result = FindClickableRecursive(children[i], x, y);
            if (result)
                return result;
        }
        
        // Check this widget
        if (IsClickable(parent) && IsPointInWidget(parent, x, y))
            return parent;
        
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool IsClickable(Widget w)
    {
        if (!w)
            return false;

        // ButtonWidget
        if (ButtonWidget.Cast(w))
            return true;

        // SCR_ModularButtonComponent
        if (w.FindHandler(SCR_ModularButtonComponent))
            return true;

        // AG0_TDLMemberCardHandler (for member cards)
        if (w.FindHandler(AG0_TDLMemberCardHandler))
            return true;

        // AG0_TDLMarkerEntryHandler (for placed-marker scroll list cards).
        // Click deletes the marker via AskRemoveStaticMarker RPC.
        if (w.FindHandler(AG0_TDLMarkerEntryHandler))
            return true;

        // SCR_PagingButtonComponent — spinbox left/right arrow buttons. The
        // recursive walk returns the arrow widget directly as target; the
        // dispatch in TriggerClick fires the paging button's OnClick.
        if (w.FindHandler(SCR_PagingButtonComponent))
            return true;

        // Brightness slider — the outer ButtonWidget that wraps the inner
        // SliderWidget. FindClickableRecursive returns this widget (not the
        // inner SliderWidget) because the SCR_SliderComponent handler lives
        // on the outer button. HandleClickAndDrag branches on this to drive
        // the slider value from cursor X every frame instead of single-fire
        // TriggerClick — the engine's normal slider-drag pathway depends on
        // continuous mouse-move events that this component doesn't emit.
        if (m_MenuController && m_MenuController.IsBrightnessSlider(w))
            return true;

        // Spinbox arrow buttons / inner widgets — the SCR_SpinBoxComponent
        // handler lives on the spinbox container, not the arrow buttons, so
        // walk up the parent chain. Same for combo boxes (vanilla picker
        // widgets used by the marker tool panel for type / faction / dim).
        if (FindSpinBoxOwner(w))
            return true;
        if (FindComboBoxOwner(w))
            return true;

        return false;
    }

    //! Walk up the widget tree from `start` looking for a widget whose handler
    //! is SCR_SpinBoxComponent. Returns the owner widget (the one carrying the
    //! handler) so the caller can grab both the component and its screen
    //! bounds for left/right-half hit-testing.
    protected Widget FindSpinBoxOwner(Widget start)
    {
        Widget w = start;
        while (w)
        {
            if (w.FindHandler(SCR_SpinBoxComponent))
                return w;
            w = w.GetParent();
        }
        return null;
    }

    //! Same walk for combo boxes.
    protected Widget FindComboBoxOwner(Widget start)
    {
        Widget w = start;
        while (w)
        {
            if (w.FindHandler(SCR_ComboBoxComponent))
                return w;
            w = w.GetParent();
        }
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool IsPointInWidget(Widget w, float x, float y)
    {
        float wX, wY, wW, wH;
        w.GetScreenPos(wX, wY);
        w.GetScreenSize(wW, wH);
        
        return (x >= wX && x <= wX + wW && y >= wY && y <= wY + wH);
    }
    
    // ============================================
    // INPUT HANDLERS
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    protected void OnClickAction()
    {
        if (!m_bLookingAtScreen || !m_bInteractionEnabled || m_bDragging)
            return;
        
        Widget target = m_wHoveredWidget;
        if (!target)
            target = FindClickableAt(m_fCursorX, m_fCursorY);
        
        if (target)
            TriggerClick(target);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnBackAction()
    {
        if (!m_bLookingAtScreen || !m_bInteractionEnabled)
            return;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void TriggerClick(Widget target)
    {
        if (!target)
            return;

        // Spinbox dispatch — determine direction and call SetCurrentItem
        // directly with manual bounds/cycling. Bypasses the vanilla
        // EnableArrows path that disables a paging button widget at the
        // index edges (non-cycling spinboxes like the marker tool's type
        // selector were getting stuck: at index 0 LEFT is disabled, and
        // even after moving to index 1 the disabled state on the now-
        // valid LEFT arrow wasn't always cleanly cleared, so subsequent
        // LEFT clicks via the engine OnClick path no-op'd).
        //
        // SetCurrentItem fires m_OnChanged regardless of EnableArrows, so
        // panel-side handlers (OnTypeSpinBoxChanged, OnPlacedIconChanged,
        // OnMilitarySelectorChanged) all run as expected.
        Widget spinOwner = FindSpinBoxOwner(target);
        if (spinOwner)
        {
            SCR_SpinBoxComponent spinBox = SCR_SpinBoxComponent.Cast(
                spinOwner.FindHandler(SCR_SpinBoxComponent));
            if (spinBox)
            {
                // Decide direction: if click target is a paging button, use
                // its widget name (most reliable); otherwise compute from
                // cursor X vs spinbox center (fallback for clicks that
                // land in the container's body).
                bool decrement;
                if (target.FindHandler(SCR_PagingButtonComponent))
                {
                    decrement = (target.GetName() == "ButtonLeft");
                }
                else
                {
                    float sX, sY, sW, sH;
                    spinOwner.GetScreenPos(sX, sY);
                    spinOwner.GetScreenSize(sW, sH);
                    decrement = (m_fCursorX < sX + sW * 0.5);
                }

                int numItems = spinBox.GetNumItems();
                if (numItems > 0)
                {
                    int currentIdx = spinBox.GetCurrentIndex();
                    int newIdx;
                    if (decrement)
                        newIdx = currentIdx - 1;
                    else
                        newIdx = currentIdx + 1;
                    // Manual wrap. The panel-side type spinbox doesn't set
                    // cycle mode (only icon / color / faction / dimension
                    // do) so we cycle ourselves — visually consistent with
                    // gamepad-style spinbox interaction across all of them.
                    if (newIdx < 0)
                        newIdx = numItems - 1;
                    else if (newIdx >= numItems)
                        newIdx = 0;
                    spinBox.SetCurrentItem(newIdx);
                }
                return;
            }
        }

        // Combo box dispatch — combo boxes don't share the spinbox's
        // paging-button substructure (they open a dropdown on click via
        // SCR_ComboBoxComponent.OnClick). Fire OnClick directly so the
        // engine's dropdown opens. The dropdown's own item selection then
        // closes it normally.
        Widget comboOwner = FindComboBoxOwner(target);
        if (comboOwner)
        {
            SCR_ComboBoxComponent combo = SCR_ComboBoxComponent.Cast(comboOwner.FindHandler(SCR_ComboBoxComponent));
            if (combo)
            {
                // Fall back to index cycle since the dropdown overlay
                // depends on workspace-level rendering that the world-
                // space RT can't host.
                int currentIdx = combo.GetCurrentIndex();
                int numItems = combo.GetNumItems();
                if (numItems > 0)
                {
                    float cX, cY, cW, cH;
                    comboOwner.GetScreenPos(cX, cY);
                    comboOwner.GetScreenSize(cW, cH);
                    float centerX = cX + cW * 0.5;

                    int newIdx;
                    if (m_fCursorX < centerX)
                        newIdx = currentIdx - 1;
                    else
                        newIdx = currentIdx + 1;
                    // Manual cycle — combo doesn't always honour cycle mode
                    // and the dropdown UX isn't available on world-space.
                    if (newIdx < 0)
                        newIdx = numItems - 1;
                    else if (newIdx >= numItems)
                        newIdx = 0;
                    combo.SetCurrentItem(newIdx);
                }
                return;
            }
        }

        // Member-card click — drives ShowDetailView via the controller.
        AG0_TDLMemberCardHandler cardHandler = AG0_TDLMemberCardHandler.Cast(
            target.FindHandler(AG0_TDLMemberCardHandler)
        );
        if (cardHandler)
        {
            cardHandler.OnClick(target, m_fCursorX, m_fCursorY, 0);
            return;
        }

        // Marker-entry click — AskRemoveStaticMarker RPC via the panel.
        AG0_TDLMarkerEntryHandler markerHandler = AG0_TDLMarkerEntryHandler.Cast(
            target.FindHandler(AG0_TDLMarkerEntryHandler)
        );
        if (markerHandler)
        {
            markerHandler.OnClick(target, m_fCursorX, m_fCursorY, 0);
            return;
        }

        // SCR_ModularButtonComponent — generic toolbar / nav / panel buttons.
        SCR_ModularButtonComponent modBtn = SCR_ModularButtonComponent.Cast(
            target.FindHandler(SCR_ModularButtonComponent)
        );
        if (modBtn)
        {
            if (modBtn.m_OnClicked)
                modBtn.m_OnClicked.Invoke(modBtn);
            return;
        }
    }
    
    // ============================================
    // PUBLIC INTERFACE
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    bool IsLookingAtScreen()
    {
        return m_bLookingAtScreen;
    }
    
    //------------------------------------------------------------------------------------------------
    void GetCursorPosition(out float x, out float y)
    {
        x = m_fCursorX;
        y = m_fCursorY;
    }
    
    //------------------------------------------------------------------------------------------------
    Widget GetRootWidget()
    {
        return m_wRoot;
    }
    
    //------------------------------------------------------------------------------------------------
    AG0_TDLDisplayController GetDisplayController()
    {
        return m_DisplayController;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetInteractionEnabled(bool enabled)
    {
        m_bInteractionEnabled = enabled;
        
        if (!enabled)
        {
            SetLookingAtScreen(false);
        }
    }
    
    // ============================================
    // FOCUS CONTROL
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    //! Recursively disable focus on all widgets in hierarchy
    //! Prevents these widgets from being candidates in focus navigation (fixes Bacon Loadout Editor conflict)
    protected void DisableFocusRecursive(Widget w)
    {
        if (!w)
            return;
        
        // Set NOFOCUS flag to exclude from focus navigation
        w.SetFlags(WidgetFlags.NOFOCUS);
        
        // Process all children
        Widget child = w.GetChildren();
        while (child)
        {
            DisableFocusRecursive(child);
            child = child.GetSibling();
        }
    }
    
    // ============================================
    // FOCUS MODE
    // ============================================

    //------------------------------------------------------------------------------------------------
    //! True when the local player can press TDLFocusToggle to enter focus mode.
    //! Caller responsibility: also gate on IsHeldDevice — this is queried by the
    //! PC's action listener which already iterates the held-device cache.
    bool IsFocusEligible()
    {
        return GetFocusIneligibleReason() == string.Empty;
    }

    //------------------------------------------------------------------------------------------------
    //! Returns empty string when eligible, otherwise a short reason. Separated from
    //! IsFocusEligible so the toggle handler can log WHY the press was rejected
    //! without us having to spam during the hint-condition's per-frame eligibility checks.
    string GetFocusIneligibleReason()
    {
        if (m_eFocusState != TDL_EFocusState.IDLE)
            return "already in focus state " + typename.EnumToString(TDL_EFocusState, m_eFocusState);

        if (!m_FocusCameraPivot)
            return "m_FocusCameraPivot attribute is null — author it in the prefab";

        // Don't validate against GetNodeId() — it legitimately returns -1 for
        // named .xob pivots (the "v_*" convention), and AddChild(child, -1, ...)
        // handles that by attaching at the entity root with the PointInfo offset
        // applied via SetLocalTransform. This is the same path AG0_TDLMenuUI's
        // AttachCameraToDevice relies on for the remote-feed camera.

        // Eligibility uses m_bNearScreen, not m_bLookingAtScreen. The wider zone
        // is more forgiving for the focus prompt — players can trigger when looking
        // at the bezel without pixel-perfect aim on the screen surface. The cursor
        // raycast and click logic still gate on m_bLookingAtScreen, which is the
        // exact-screen hit.
        if (!m_bNearScreen)
            return "raycast not currently hitting screen eligibility zone (m_bNearScreen == false)";

        return string.Empty;
    }

    //------------------------------------------------------------------------------------------------
    //! True if focus mode is in any non-IDLE state (entering, active, or exiting).
    bool IsFocusActive()
    {
        return m_eFocusState != TDL_EFocusState.IDLE;
    }

    //------------------------------------------------------------------------------------------------
    //! Single entry point invoked by the player-controller TDLFocusToggle listener.
    //! Routes to Enter or Exit depending on current state. ENTERING/EXITING tweens
    //! are non-cancelable — pressing toggle mid-tween is a no-op (prevents jitter).
    void ToggleFocus()
    {
        switch (m_eFocusState)
        {
            case TDL_EFocusState.IDLE:
                if (IsFocusEligible())
                    EnterFocus();
                break;

            case TDL_EFocusState.ACTIVE:
                ExitFocus(false);
                break;

            // ENTERING / EXITING: ignore the press. The tween is short (~0.3s)
            // so the user just re-presses if they really want the toggle.
            default:
                break;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void EnterFocus()
    {
        CameraManager camMgr = GetGame().GetCameraManager();
        if (!camMgr)
            return;

        m_OriginalCamera = camMgr.CurrentCamera();
        if (!m_OriginalCamera)
            return;

        // Snapshot the current player-camera pose. This is the start frame of
        // the tween AND the spawn transform — spawning the focus camera there
        // means the SetCamera swap is visually invisible.
        m_OriginalCamera.GetTransform(m_StartCameraTransform);
        m_EntryCameraOrigin = m_StartCameraTransform[3];
        m_EntryControlledEntity = GetGame().GetPlayerController().GetControlledEntity();

        EntitySpawnParams params = new EntitySpawnParams();
        params.TransformMode = ETransformMode.WORLD;
        params.Transform = m_StartCameraTransform;

        Resource res = Resource.Load(FOCUS_CAMERA_PREFAB);
        if (!res || !res.IsValid())
        {
            Print("[TDL_WorldSpaceDisplay] FOCUS: focus camera prefab failed to load", LogLevel.ERROR);
            m_OriginalCamera = null;
            return;
        }

        m_SpawnedFocusCamera = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
        if (!m_SpawnedFocusCamera)
        {
            m_OriginalCamera = null;
            return;
        }

        CameraBase focusCam = CameraBase.Cast(m_SpawnedFocusCamera);
        if (!focusCam)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFocusCamera);
            m_SpawnedFocusCamera = null;
            m_OriginalCamera = null;
            return;
        }

        // Capture FOVs for the tween BEFORE swapping the camera. Order matters:
        //  - m_fFocusNativeFOV = prefab's natural FOV (read from the just-spawned camera)
        //  - m_fEntryPlayerFOV = player's current FOV (start frame of ENTERING tween)
        // Then set the focus camera to the player's FOV so the SetCamera swap is
        // visually identical in both position AND FOV — the tween eases purely in the
        // ENTERING state from that frame forward.
        m_fFocusNativeFOV = focusCam.GetVerticalFOV();
        m_fEntryPlayerFOV = m_OriginalCamera.GetVerticalFOV();
        focusCam.SetVerticalFOV(m_fEntryPlayerFOV);

        camMgr.SetCamera(focusCam);

        // Suppress vanilla DOF for the duration of focus. Reforger's
        // SCR_DepthOfFieldEffect is an HUD info display that reads the character's
        // weapon/aim state and applies blur regardless of which camera is active —
        // so swapping cameras alone doesn't fix the out-of-focus device when the
        // weapon is raised. Disabling the info display kills DOF entirely until
        // we restore it in FinishExit.
        SuppressDOFEffect();

        m_fTweenElapsed = 0;
        m_bFocusCameraAttachedToPivot = false;
        m_eFocusState = TDL_EFocusState.ENTERING;

        // Initialize the cursor to the screen center as a sensible starting
        // position. The raycast path would have set this implicitly; in focus
        // mode there's no raycast so we seed it ourselves.
        float frameW, frameH;
        GetCachedFrameSize(frameW, frameH);
        if (frameW > 0 && frameH > 0)
        {
            m_fCursorX = frameW * 0.5;
            m_fCursorY = frameH * 0.5;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Begin (or finish) exit. If instant=true, skip the tween — used for involuntary
    //! exits (life-state change, device dropped) where we want to bail synchronously
    //! without leaving a hanging spawned camera.
    protected void ExitFocus(bool instant)
    {
        if (m_eFocusState == TDL_EFocusState.IDLE)
            return;

        // Detach from pivot bone if we'd parented to it during ACTIVE.
        // SetWorldTransform after RemoveChild keeps the camera where it currently
        // visually is, so the exit tween starts from the right pose.
        if (m_bFocusCameraAttachedToPivot && m_SpawnedFocusCamera)
        {
            IEntity owner = GetOwner();
            vector currentWorld[4];
            m_SpawnedFocusCamera.GetWorldTransform(currentWorld);
            if (owner)
                owner.RemoveChild(m_SpawnedFocusCamera, true);
            m_SpawnedFocusCamera.SetWorldTransform(currentWorld);
            m_bFocusCameraAttachedToPivot = false;
        }

        if (instant)
        {
            FinishExit();
            return;
        }

        // Capture the start frame of the exit tween from wherever the camera is right now.
        if (m_SpawnedFocusCamera)
            m_SpawnedFocusCamera.GetWorldTransform(m_StartCameraTransform);

        m_fTweenElapsed = 0;
        m_eFocusState = TDL_EFocusState.EXITING;
    }

    //------------------------------------------------------------------------------------------------
    //! Final teardown — restore the player camera, despawn the focus camera, clear state.
    //! Called at the end of the EXITING tween or directly by ExitFocus(true).
    protected void FinishExit()
    {
        CameraManager camMgr = GetGame().GetCameraManager();
        if (camMgr && m_OriginalCamera)
            camMgr.SetCamera(m_OriginalCamera);

        if (m_SpawnedFocusCamera)
        {
            SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFocusCamera);
            m_SpawnedFocusCamera = null;
        }

        // Restore vanilla DOF if we suppressed it on entry. Gated on the
        // entry-time flag so we never accidentally re-enable a DOF effect that
        // was already disabled by the user / game settings.
        RestoreDOFEffect();

        m_OriginalCamera = null;
        m_EntryControlledEntity = null;
        m_fTweenElapsed = 0;
        m_bFocusCameraAttachedToPivot = false;
        m_eFocusState = TDL_EFocusState.IDLE;
    }

    //------------------------------------------------------------------------------------------------
    //! Disable the SCR_DepthOfFieldEffect info display so weapon-raised DOF doesn't
    //! blur the device. Records whether the effect was enabled at entry so we only
    //! flip it back on if we were the ones who disabled it.
    protected void SuppressDOFEffect()
    {
        m_bDOFWasEnabledOnEntry = false;

        SCR_HUDManagerComponent hudMgr = SCR_HUDManagerComponent.GetHUDManager();
        if (!hudMgr)
            return;

        SCR_DepthOfFieldEffect dofDisplay = SCR_DepthOfFieldEffect.Cast(
            hudMgr.FindInfoDisplay(SCR_DepthOfFieldEffect));
        if (!dofDisplay)
            return;

        // FindInfoDisplay can return a display even if it's currently disabled
        // (the HUD manager keeps registered displays around regardless). Only
        // suppress + remember-to-restore when it was actually enabled.
        m_bDOFWasEnabledOnEntry = true;
        dofDisplay.SetEnabled(false);
    }

    //------------------------------------------------------------------------------------------------
    //! Re-enable the DOF info display, only if SuppressDOFEffect succeeded on entry.
    protected void RestoreDOFEffect()
    {
        if (!m_bDOFWasEnabledOnEntry)
            return;

        SCR_HUDManagerComponent hudMgr = SCR_HUDManagerComponent.GetHUDManager();
        if (!hudMgr)
            return;

        SCR_DepthOfFieldEffect dofDisplay = SCR_DepthOfFieldEffect.Cast(
            hudMgr.FindInfoDisplay(SCR_DepthOfFieldEffect));
        if (!dofDisplay)
            return;

        dofDisplay.SetEnabled(true);
        m_bDOFWasEnabledOnEntry = false;
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame update for the focus state machine. Runs the tween while ENTERING
    //! or EXITING; while ACTIVE, the focus camera is parented to the pivot bone and
    //! its transform updates automatically with the device.
    //! Returns true if focus mode is in any non-IDLE state.
    protected bool UpdateFocus(float timeSlice)
    {
        if (m_eFocusState == TDL_EFocusState.IDLE)
            return false;

        // Hard safety: if the spawned camera went away (entity culled, etc.) bail.
        if (!m_SpawnedFocusCamera)
        {
            FinishExit();
            return false;
        }

        switch (m_eFocusState)
        {
            case TDL_EFocusState.ENTERING:
                UpdateEnterTween(timeSlice);
                break;

            case TDL_EFocusState.EXITING:
                UpdateExitTween(timeSlice);
                break;

            case TDL_EFocusState.ACTIVE:
                // While active the camera is bone-parented to the pivot, so the
                // engine handles transform tracking. We just guard the auto-exit
                // conditions that depend on world state.
                CheckActiveAutoExit();
                break;
        }

        return m_eFocusState != TDL_EFocusState.IDLE;
    }

    //------------------------------------------------------------------------------------------------
    protected void UpdateEnterTween(float timeSlice)
    {
        m_fTweenElapsed += timeSlice;
        float t = Math.Clamp(m_fTweenElapsed / m_fFocusTweenDuration, 0, 1);
        float eased = EaseInOutCubic(t);

        // Re-sample the pivot's world transform every frame — the device is in
        // the player's hand and the hand-bone animates. Tweening against a fixed
        // entry-time snapshot would look like the camera was chasing a moving
        // target.
        vector pivotWorld[4];
        m_FocusCameraPivot.GetWorldTransform(pivotWorld);

        vector blended[4];
        TweenTransform(m_StartCameraTransform, pivotWorld, eased, blended);
        m_SpawnedFocusCamera.SetWorldTransform(blended);

        // Lerp FOV alongside the transform so the zoom-in feels continuous with
        // the dolly-in motion. Linear is fine in FOV-space; the cubic ease on
        // the transform already provides the perceived smoothness.
        CameraBase focusCam = CameraBase.Cast(m_SpawnedFocusCamera);
        if (focusCam)
            focusCam.SetVerticalFOV(m_fEntryPlayerFOV + (m_fFocusNativeFOV - m_fEntryPlayerFOV) * eased);

        if (t >= 1.0)
        {
            // Snap to pivot and switch to bone-parented tracking. AddChild with
            // AUTO_TRANSFORM positions the child by the slot's local transform,
            // so we follow up with SetLocalTransform to apply the authored offset
            // — same pattern AG0_TDLMenuUI.AttachCameraToDevice uses for the feed.
            IEntity owner = GetOwner();
            if (owner)
            {
                int boneIndex = m_FocusCameraPivot.GetNodeId();
                owner.AddChild(m_SpawnedFocusCamera, boneIndex, EAddChildFlags.AUTO_TRANSFORM);

                vector localTransform[4];
                m_FocusCameraPivot.GetLocalTransform(localTransform);
                m_SpawnedFocusCamera.SetLocalTransform(localTransform);

                m_bFocusCameraAttachedToPivot = true;
            }

            m_eFocusState = TDL_EFocusState.ACTIVE;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void UpdateExitTween(float timeSlice)
    {
        m_fTweenElapsed += timeSlice;
        float t = Math.Clamp(m_fTweenElapsed / m_fFocusTweenDuration, 0, 1);
        float eased = EaseInOutCubic(t);

        // Sample the player's camera target each frame — it's tracking the head
        // pose and we want to land exactly on it regardless of how far the head
        // has moved during the tween.
        vector targetWorld[4];
        m_OriginalCamera.GetTransform(targetWorld);

        vector blended[4];
        TweenTransform(m_StartCameraTransform, targetWorld, eased, blended);
        m_SpawnedFocusCamera.SetWorldTransform(blended);

        // Lerp FOV back toward the player camera. Re-sample the target each frame
        // because the player FOV is not constant — raising/lowering a weapon, leaning,
        // and free-look all shift it. Linear blend in FOV-space.
        CameraBase focusCam = CameraBase.Cast(m_SpawnedFocusCamera);
        if (focusCam)
        {
            float targetFOV = m_OriginalCamera.GetVerticalFOV();
            focusCam.SetVerticalFOV(m_fFocusNativeFOV + (targetFOV - m_fFocusNativeFOV) * eased);
        }

        if (t >= 1.0)
            FinishExit();
    }

    //------------------------------------------------------------------------------------------------
    //! Hard-stop conditions checked while ACTIVE. Voluntary exit (toggle key) is
    //! routed via ToggleFocus from the player controller.
    protected void CheckActiveAutoExit()
    {
        IEntity owner = GetOwner();
        if (!owner)
        {
            ExitFocus(true);
            return;
        }

        // (Was: distance-from-entry-camera-origin check. Removed — the pivot
        // moves with the player so the distance grew with normal walking, which
        // kicked players out of focus at anything faster than a crawl. The real
        // device-loss cases are caught by the IsHeldDevice gate at the top of
        // EOnFrame, so this guard was both redundant and harmful.)

        // Controlled-entity change — respawn into a new body, jumping into a
        // vehicle, going to spectator. The held-device cache would catch this on
        // its 1 s tick, but the player camera is already pointing at the new
        // entity. Instant-exit so we don't tween back to a stale m_OriginalCamera.
        IEntity controlled = GetGame().GetPlayerController().GetControlledEntity();
        if (controlled != m_EntryControlledEntity)
        {
            ExitFocus(true);
            return;
        }

        // Life-state check — if the character is no longer ALIVE, drop focus
        // immediately. ChimeraCharacter is the controlled-entity type for
        // playable characters; GetCharacterController gives us the life state.
        ChimeraCharacter character = ChimeraCharacter.Cast(controlled);
        if (character)
        {
            CharacterControllerComponent ctrl = character.GetCharacterController();
            if (ctrl && ctrl.GetLifeState() != ECharacterLifeState.ALIVE)
            {
                ExitFocus(true);
                return;
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Shared click + drag handler used by both the raycast path (player looking
    //! at world device) and the focus-mode path. Both paths produce m_fCursorX/Y
    //! in screen pixels; from this function's perspective the source is irrelevant.
    //! Caller is responsible for ActivateContext("TDLScreenContext") so MenuSelect
    //! and TDLScreenClick stay live.
    protected void HandleClickAndDrag()
    {
        if (!m_InputManager)
            return;

        // Check if click is held (not just triggered)
        bool clickHeld = m_InputManager.GetActionValue("TDLScreenClick") > 0 ||
                         m_InputManager.GetActionValue("MenuSelect") > 0;

        if (clickHeld)
        {
            // Slider drag has its own continuation branch — checked first so
            // it pre-empts the map-pan and button-click paths once a slider
            // grab has started. m_wSliderDragTarget is set only on the first
            // frame the click landed on the brightness slider.
            if (m_wSliderDragTarget)
            {
                if (m_MenuController)
                    m_MenuController.DriveBrightnessSliderFromCursorX(m_fCursorX);
            }
            else if (!m_bClickHandled && !m_bDragging)
            {
                // First frame of click - decide what to do
                Widget clickable = FindClickableAt(m_fCursorX, m_fCursorY);

                // Brightness slider — enter slider-drag mode. Drive the
                // value now (so a tap on the track immediately jumps the
                // thumb without needing a drag) and on every subsequent
                // frame the button is held. We don't set m_bClickHandled,
                // because the continuation branch above looks at
                // m_wSliderDragTarget instead.
                if (m_MenuController && m_MenuController.IsBrightnessSlider(clickable))
                {
                    m_wSliderDragTarget = clickable;
                    m_MenuController.DriveBrightnessSliderFromCursorX(m_fCursorX);
                }
                // If we hit the drag surface (or nothing), start dragging
                else if (!clickable || clickable == m_wMapDragSurface)
                {
                    if (m_wMapDragSurface && IsPointInWidget(m_wMapDragSurface, m_fCursorX, m_fCursorY))
                    {
                        // Start dragging on map surface. Record entry pos so
                        // we can distinguish a real drag from a click-and-
                        // release-without-movement on release — the latter
                        // is the marker placement path (KBM map-click place).
                        m_bDragging = true;
                        m_fLastDragX = m_fCursorX;
                        m_fLastDragY = m_fCursorY;
                        m_fDragStartX = m_fCursorX;
                        m_fDragStartY = m_fCursorY;
                        // Fresh drag — reset the sticky freehand flag. If
                        // TDLDraw is already held this frame the continuation
                        // branch will re-raise it next tick.
                        m_bDragWasFreehandDraw = false;
                        AG0_TDLDisplayController.SetPlayerTracking(false);
                    }
                    else
                    {
                        // Clicked outside everything
                        m_bClickHandled = true;
                    }
                }
                else
                {
                    // Click on a real button - fire once
                    TriggerClick(clickable);
                    m_bClickHandled = true;
                }
            }
            else if (m_bDragging)
            {
                // Freehand draw suppresses map pan so the cursor movement
                // belongs entirely to the stroke. Sticky flag keeps the
                // release-path click-place suppressed too — handles the
                // case where TDLDraw is bound to the same key as
                // TDLScreenClick / MenuSelect (releasing the key ends the
                // "drag" without ever having been a real pan).
                bool drawingNow = m_MenuController && m_MenuController.IsAnyDrawInteractionActive(m_InputManager);
                if (drawingNow)
                    m_bDragWasFreehandDraw = true;

                float deltaX = m_fCursorX - m_fLastDragX;
                float deltaY = m_fCursorY - m_fLastDragY;

                if (!drawingNow && m_DisplayController && (deltaX != 0 || deltaY != 0))
                {
                    AG0_TDLMapView mapView = m_DisplayController.GetMapView();
                    if (mapView)
                        mapView.Pan(deltaX, -deltaY);
                }

                m_fLastDragX = m_fCursorX;
                m_fLastDragY = m_fCursorY;
            }
        }
        else
        {
            // Click released — if we were dragging on the map surface and the
            // total displacement is below the click threshold, this was a
            // click-without-drag → route to controller's marker placement
            // path. The controller gates internally on MARKER_TOOL being
            // the active panel, so calls outside that mode are no-ops.
            //
            // Mirrors AG0_TDLMapCanvasDragHandler.m_OnClick on the menu's
            // drag handler — same KBM placement behaviour, just sourced
            // from this component's cursor instead of the workspace mouse.
            if (m_bDragging && m_MenuController && !m_bDragWasFreehandDraw)
            {
                float dx = m_fCursorX - m_fDragStartX;
                float dy = m_fCursorY - m_fDragStartY;
                float distSq = dx * dx + dy * dy;
                if (distSq <= MAP_CLICK_DRAG_THRESHOLD * MAP_CLICK_DRAG_THRESHOLD)
                {
                    // Route to both tool handlers — each internally gates
                    // on whether its tool is active. Drag threshold above
                    // already excluded pans, so this only fires on a real
                    // click-without-drag. Skipped entirely when the drag
                    // was a freehand draw — otherwise releasing TDLDraw
                    // without moving the cursor would drop a stray marker.
                    m_MenuController.OnMapClickedForMarkerPlacement(m_fCursorX, m_fCursorY);
                    m_MenuController.OnMapClickedForBloodhound(m_fCursorX, m_fCursorY);
                }
            }

            m_bDragging = false;
            m_bDragWasFreehandDraw = false;
            m_bClickHandled = false;
            // End of slider drag — the next click starts a fresh decision.
            m_wSliderDragTarget = null;
        }

        if (m_InputManager.GetActionTriggered("MenuBack"))
        {
            OnBackAction();
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Project the cursor's ContentFrame pixel position to a world position by
    //! way of MapCanvas-local pixels + AG0_TDLMapView.ScreenToWorld. Mirrors
    //! AG0_TDLMenuController.OnMapClickedForMarkerPlacement's KBM placement
    //! conversion exactly — same offset subtraction, same call into ScreenToWorld
    //! — so cursor → bloodhound world position uses identical math to cursor →
    //! marker placement. Returns false if any prerequisite widget/component is
    //! missing (defensive — layouts can lose widgets mid-session if e.g. the
    //! device's RT widget recreates during a level restart).
    protected bool TryGetBloodhoundCursorWorld(out vector worldPos)
    {
        if (!m_DisplayController || !m_wRoot)
            return false;
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return false;
        // Reject cursor resolution before the map view has been sized.
        // ScreenToWorld would fall back to m_vCenterWorld in that state,
        // which the freehand sample stream would latch as a legitimate
        // first sample at the map centre — manifesting as a bogus "first
        // sample from an invalid position" on the very first frame of a
        // freehand draw. Skipping the push entirely keeps the session's
        // m_bCursorWorldKnown false until the map is actually ready.
        if (!mapView.IsReady())
            return false;
        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return false;

        float canvasScreenX, canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);
        float localX = m_fCursorX - canvasScreenX;
        float localY = m_fCursorY - canvasScreenY;
        mapView.ScreenToWorld(localX, localY, worldPos);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Returns the ContentFrame screen size, lazily cached on first valid read.
    //!
    //! Why cached: querying m_wContentFrame.GetScreenSize() every frame ties the
    //! cursor's clamp + the raycast's UV scaling to whatever the layout reports
    //! right now. Internal layout passes (notably MapCanvas resizing when the
    //! map zooms) shift the reported size, which manifests as cursor sensitivity
    //! that scales with map zoom — sluggish when zoomed out, normal when zoomed in.
    //!
    //! Caching freezes the reference resolution at the first time the cursor is
    //! actually used (by which point the layout has settled), so everything
    //! downstream operates against a fixed coordinate space regardless of what
    //! the map widget is doing.
    protected void GetCachedFrameSize(out float w, out float h)
    {
        if (m_fCachedFrameW <= 0 || m_fCachedFrameH <= 0)
        {
            if (m_wContentFrame)
                m_wContentFrame.GetScreenSize(m_fCachedFrameW, m_fCachedFrameH);
        }
        w = m_fCachedFrameW;
        h = m_fCachedFrameH;
    }

    //------------------------------------------------------------------------------------------------
    //! Position + show the cursor widget at the current m_fCursorX/Y. Used by the
    //! focus-mode path; the raycast path inlines the same logic (kept inline there
    //! because it computes cursor pos and updates the widget in one pass).
    protected void UpdateFocusCursorWidget()
    {
        if (!m_wCursor)
            return;

        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
            return;

        float layoutX = workspace.DPIUnscale(m_fCursorX);
        float layoutY = workspace.DPIUnscale(m_fCursorY);
        FrameSlot.SetPos(m_wCursor, layoutX, layoutY);
        m_wCursor.SetVisible(true);
    }

    //! Reference RT width the cursor sensitivity attributes are calibrated
    //! against. The per-frame motion scales by (actualFrameW / REFERENCE) so
    //! the cursor feels the same regardless of whether the RT renders at
    //! 1080p, 1440p, 4K, or a non-standard resolution.
    protected const float CURSOR_REF_FRAME_WIDTH = 1920.0;

    //------------------------------------------------------------------------------------------------
    //! Read the cursor-axis actions and integrate into m_fCursorX/Y.
    //! PC actions are AnalogRelative against mouse:[xy]_rel± so they're already
    //! per-frame deltas — multiply by sensitivity only, NOT by timeSlice.
    //! Gamepad actions are stick deflection in [0,1] — these ARE velocities, so
    //! multiply by sensitivity AND timeSlice.
    //!
    //! Both axes are then scaled by (frameW / CURSOR_REF_FRAME_WIDTH) so the
    //! sensitivity attributes are RT-resolution-independent. Without this,
    //! a higher-resolution RT would need proportionally more cursor pixels
    //! to traverse the visible screen, making sensitivity feel sluggish on
    //! 4K RTs and twitchy on low-res ones.
    protected void DriveCursorFromInput(float timeSlice)
    {
        if (!m_InputManager || !m_wContentFrame)
            return;

        // Cap large frame-time spikes — server hitches, alt-tab returns, and
        // long replication stalls can produce timeSlice values of 100ms+. With
        // sensitivity tuned for normal frame times, those spikes teleport the
        // cursor. 50ms cap keeps motion bounded without affecting normal play
        // (60fps = 16ms, 30fps = 33ms, both well under the cap).
        if (timeSlice > 0.05)
            timeSlice = 0.05;

        float mouseRight = m_InputManager.GetActionValue("TDLCursorRight");
        float mouseLeft  = m_InputManager.GetActionValue("TDLCursorLeft");
        float mouseDown  = m_InputManager.GetActionValue("TDLCursorDown");
        float mouseUp    = m_InputManager.GetActionValue("TDLCursorUp");

        float stickRight = m_InputManager.GetActionValue("TDLCursorGamepadRight");
        float stickLeft  = m_InputManager.GetActionValue("TDLCursorGamepadLeft");
        float stickDown  = m_InputManager.GetActionValue("TDLCursorGamepadDown");
        float stickUp    = m_InputManager.GetActionValue("TDLCursorGamepadUp");

        // Radial deadzone + linear response.
        //
        // Compute the true stick magnitude (the vector radius), apply a single
        // deadzone-aware linear rescale to that magnitude, then scale the
        // original direction vector to the rescaled magnitude. Per-axis
        // curving (what this used to do) made diagonals travel sqrt(2)x
        // faster than cardinal directions because each axis got its own
        // sensitivity multiplier and the resulting deltas combined as
        // sqrt(dx² + dy²) — the speed ended up depending on which way the
        // stick pointed, not just how far it was pushed.
        //
        // Linear response (no t² / smoothstep) means cursor speed is directly
        // proportional to stick radius above the deadzone. Combined with the
        // timeSlice cap above, the cursor speed is predictable: framerate-
        // independent and direction-independent.
        float stickX = stickRight - stickLeft;
        float stickY = stickDown - stickUp;
        float magnitude = Math.Sqrt(stickX * stickX + stickY * stickY);
        if (magnitude < m_fGamepadDeadzone)
        {
            stickX = 0;
            stickY = 0;
        }
        else
        {
            // Linear remap of the radius from [deadzone..1] to [0..1].
            float t = (magnitude - m_fGamepadDeadzone) / (1.0 - m_fGamepadDeadzone);
            if (t > 1.0)
                t = 1.0;
            // Replace the radius while preserving the direction vector.
            float scale = t / magnitude;
            stickX = stickX * scale;
            stickY = stickY * scale;
        }

        // Cached frame size — see GetCachedFrameSize doc. Querying GetScreenSize
        // each frame here tied cursor speed to map-zoom-driven layout reflows.
        float frameW, frameH;
        GetCachedFrameSize(frameW, frameH);

        // Sensitivity scale factor — keeps the perceived cursor speed constant
        // across RT resolutions. At the reference width (1920), scale = 1.
        float sensScale = 1.0;
        if (frameW > 0)
            sensScale = frameW / CURSOR_REF_FRAME_WIDTH;

        float deltaX = ((mouseRight - mouseLeft) * m_fCursorSensitivityMouse
                     + stickX * m_fCursorSensitivityGamepad * timeSlice) * sensScale;
        float deltaY = ((mouseDown - mouseUp) * m_fCursorSensitivityMouse
                     + stickY * m_fCursorSensitivityGamepad * timeSlice) * sensScale;

        m_fCursorX = m_fCursorX + deltaX;
        m_fCursorY = m_fCursorY + deltaY;

        m_fCursorX = Math.Clamp(m_fCursorX, 0, frameW);
        m_fCursorY = Math.Clamp(m_fCursorY, 0, frameH);
    }

    //------------------------------------------------------------------------------------------------
    //! Cubic ease-in-out for camera transitions. Smoother than linear without
    //! the overshoot of spring damping.
    protected float EaseInOutCubic(float t)
    {
        if (t < 0.5)
            return 4 * t * t * t;

        float f = 2 * t - 2;
        return 0.5 * f * f * f + 1;
    }

    //------------------------------------------------------------------------------------------------
    //! Linear-interpolate position, normalized-lerp rotation (via quaternion).
    //! Manual nlerp instead of slerp — for the small per-frame angle deltas in a
    //! 0.3 s camera transition the visual result is indistinguishable, and we
    //! avoid depending on a Math3D.QuatLerp helper that isn't in the public API.
    //! Naive matrix lerp would produce non-orthonormal frames and roll-wobble
    //! mid-tween, which is why we go through quaternions at all.
    protected void TweenTransform(vector startMat[4], vector endMat[4], float t, out vector outMat[4])
    {
        float startQuat[4], endQuat[4], outQuat[4];
        Math3D.MatrixToQuat(startMat, startQuat);
        Math3D.MatrixToQuat(endMat, endQuat);

        // Quaternions q and -q represent the same rotation; if the dot product
        // is negative, flip one so we lerp along the short arc rather than the long way.
        float dot = startQuat[0] * endQuat[0]
                  + startQuat[1] * endQuat[1]
                  + startQuat[2] * endQuat[2]
                  + startQuat[3] * endQuat[3];
        float sign = 1.0;
        if (dot < 0)
            sign = -1.0;

        float oneMinusT = 1.0 - t;
        outQuat[0] = startQuat[0] * oneMinusT + endQuat[0] * sign * t;
        outQuat[1] = startQuat[1] * oneMinusT + endQuat[1] * sign * t;
        outQuat[2] = startQuat[2] * oneMinusT + endQuat[2] * sign * t;
        outQuat[3] = startQuat[3] * oneMinusT + endQuat[3] * sign * t;

        float magSq = outQuat[0] * outQuat[0]
                    + outQuat[1] * outQuat[1]
                    + outQuat[2] * outQuat[2]
                    + outQuat[3] * outQuat[3];
        if (magSq > 0.0001)
        {
            float invMag = 1.0 / Math.Sqrt(magSq);
            outQuat[0] = outQuat[0] * invMag;
            outQuat[1] = outQuat[1] * invMag;
            outQuat[2] = outQuat[2] * invMag;
            outQuat[3] = outQuat[3] * invMag;
        }

        Math3D.QuatToMatrix(outQuat, outMat);
        outMat[3] = startMat[3] * oneMinusT + endMat[3] * t;
    }

    // ============================================
    // CLEANUP
    // ============================================

    //------------------------------------------------------------------------------------------------
    void Cleanup()
    {
        // Drop focus before tearing the rest down — restores the player camera
        // synchronously so component deletion doesn't strand a focus camera as
        // the active render target.
        if (m_eFocusState != TDL_EFocusState.IDLE)
            ExitFocus(true);

        m_bInteractionEnabled = false;
        m_bLookingAtScreen = false;
        m_bNearScreen = false;
        m_bDragging = false;
        m_bClickHandled = false;
        m_fCachedFrameW = 0;
        m_fCachedFrameH = 0;
        
        // Safety net for the case where the component is destroyed while the
        // local player is still holding the device (e.g. server kicks the
        // entity from the world mid-session). Normal drop/swap teardown runs
        // earlier via TeardownControllerForUnheld on the held-state
        // transition. Idempotent if already torn down.
        TeardownControllerForUnheld();

        if (m_DisplayController)
        {
            m_DisplayController.Cleanup();
            m_DisplayController = null;
        }
        
        // Remove render target binding
        if (m_RTWidget && m_ScreenEntity)
            m_RTWidget.RemoveRenderTarget(m_ScreenEntity);
        
        // Remove the root container (this removes all children too)
        if (m_wRTContainer)
        {
            m_wRTContainer.RemoveFromHierarchy();
            m_wRTContainer = null;
        }
        
        m_RTWidget = null;
        m_wContentFrame = null;
        m_wRoot = null;
        m_wCursor = null;
        m_wCursorHighlight = null;
        m_wHoveredWidget = null;
        m_wMapDragSurface = null;
    }
}