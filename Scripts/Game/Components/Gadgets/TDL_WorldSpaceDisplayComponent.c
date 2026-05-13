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

    // Shared menu controller - drives panel state machine (Network/Detail/
    // Settings/MarkerTool/Plugin Tool switching). Same class the full-screen
    // menu uses, instantiated against this layout instance so the world-space
    // display gets the same panel interactivity.
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

    [Attribute("15.0", UIWidgets.EditBox, "PC mouse cursor sensitivity (pixels per raw delta unit)", category: "Focus Mode")]
    protected float m_fCursorSensitivityMouse;

    [Attribute("1200.0", UIWidgets.EditBox, "Gamepad cursor speed (pixels per second at full stick deflection)", category: "Focus Mode")]
    protected float m_fCursorSensitivityGamepad;

    [Attribute("0.15", UIWidgets.Slider, "Gamepad stick deadzone (0-1)", "0.0 0.5 0.01", category: "Focus Mode")]
    protected float m_fGamepadDeadzone;

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
    protected bool m_bLookingAtScreen;
    protected bool m_bInteractionEnabled;
    protected Widget m_wHoveredWidget;
    protected InputManager m_InputManager;
    
    // Drag state
    protected bool m_bDragging;
    protected bool m_bClickHandled;
    protected float m_fLastDragX;
    protected float m_fLastDragY;
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
    
    // ============================================
    // LIFECYCLE
    // ============================================
    override bool OnTicksOnRemoteProxy() { return true; }
	
    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        
        // Only setup on local machine where we need visuals
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
		
        // MULTIPLAYER OPTIMIZATION: Only process if this device is held by local player
        // Other players' devices simulate locally but we skip expensive raycast/UI work
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc || !pc.IsHeldDevice(owner))
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
            return;
        }

        // Focus state machine tick — runs every frame the device is held, regardless
        // of whether interaction is enabled. The tween must keep advancing even if
        // the player tabs out / opens a menu, otherwise the camera gets stuck mid-blend.
        UpdateFocus(timeSlice);
        
        // Update the display controller each frame
        if (m_DisplayController)
            m_DisplayController.Update(timeSlice);

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
            UpdateInteraction();
        
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

        // Initialize the shared menu controller so the panel state machine
        // works on this layout instance. The controller wires up the nav
        // button handlers (Network/Back/Settings/MarkerTool) — which is what
        // makes Contacts/Detail/Settings/Marker panels interactive on the
        // world-space display, matching the full-screen menu's behaviour.
        //
        // Plugins are menu-lifecycle-scoped (Enable/Disable happens via the
        // menu UI), so the world-space's RestoreState gets an empty plugin
        // array — PLUGIN_TOOL won't restore here, falls back to NETWORK_LIST.
        m_MenuController = new AG0_TDLMenuController();
        if (m_MenuController.Init(m_wRoot))
        {
            ref array<ref AG0_ATAKPluginBase> noPlugins = {};
            m_MenuController.RestoreState(noPlugins);
        }

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
    protected void UpdateInteraction()
    {
		MenuManager menuManager = GetGame().GetMenuManager();
        if (menuManager && menuManager.GetTopMenu())
        {
            if (m_bLookingAtScreen)
                SetLookingAtScreen(false);
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
            float ts = 0.016;  // approximate; EOnFrame's timeSlice would be more accurate,
                               // but UpdateInteraction is called without it. The mouse path
                               // doesn't use ts anyway (raw delta), and gamepad sensitivity
                               // is already low enough that frame-rate variance is invisible.
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
            DriveCursorFromInput(ts);
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
            m_bDragging = false;
            m_bClickHandled = false;
            return;
        }

        // Test intersection with screen
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
    //! Returns hit fraction (0-1) if ray intersects screen box, -1 otherwise
    protected float TraceToScreen(vector origin, vector direction)
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
            -m_vScreenWorldSize[1] * 0.5,
            -m_vScreenWorldSize[2] * 0.5
        );
        vector boxMax = Vector(
            0.001,
            m_vScreenWorldSize[1] * 0.5,
            m_vScreenWorldSize[2] * 0.5
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
        
        // Get ContentFrame size for pixel scaling (cursor lives in ContentFrame's coordinate space)
        float frameW, frameH;
        m_wContentFrame.GetScreenSize(frameW, frameH);
        
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
            // Fire focus lost on old widget
            if (m_wHoveredWidget)
            {
                ScriptedWidgetEventHandler handler = ScriptedWidgetEventHandler.Cast(
                    m_wHoveredWidget.FindHandler(ScriptedWidgetEventHandler)
                );
                if (handler)
                    handler.OnFocusLost(m_wHoveredWidget, 0, 0);
            }
            
            m_wHoveredWidget = newHovered;
            
            // Fire focus gained on new widget
            if (m_wHoveredWidget)
            {
                ScriptedWidgetEventHandler handler = ScriptedWidgetEventHandler.Cast(
                    m_wHoveredWidget.FindHandler(ScriptedWidgetEventHandler)
                );
                if (handler)
                    handler.OnFocus(m_wHoveredWidget, m_fCursorX, m_fCursorY);
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
            ScriptedWidgetEventHandler handler = ScriptedWidgetEventHandler.Cast(
                m_wHoveredWidget.FindHandler(ScriptedWidgetEventHandler)
            );
            if (handler)
                handler.OnFocusLost(m_wHoveredWidget, 0, 0);
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
        
        return false;
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
        
        // Could add back navigation logic here if needed
    }
    
    //------------------------------------------------------------------------------------------------
    protected void TriggerClick(Widget target)
    {
        if (!target)
            return;
        
        // Try SCR_ModularButtonComponent - invoke registered callbacks
        SCR_ModularButtonComponent modBtn = SCR_ModularButtonComponent.Cast(
            target.FindHandler(SCR_ModularButtonComponent)
        );
        if (modBtn)
        {
            if (modBtn.m_OnClicked)
                modBtn.m_OnClicked.Invoke(modBtn);
            return;
        }
        
        // Try AG0_TDLMemberCardHandler for member cards
        AG0_TDLMemberCardHandler cardHandler = AG0_TDLMemberCardHandler.Cast(
            target.FindHandler(AG0_TDLMemberCardHandler)
        );
        if (cardHandler)
        {
            cardHandler.OnClick(target, m_fCursorX, m_fCursorY, 0);
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

        if (!m_bLookingAtScreen)
            return "raycast not currently hitting screen (m_bLookingAtScreen == false)";

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
        if (m_wContentFrame)
        {
            float frameW, frameH;
            m_wContentFrame.GetScreenSize(frameW, frameH);
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

        // Out-of-range — player walked away from where they originally focused.
        // Compares current pivot world position to entry-time camera origin.
        // Uses pivot position (not character position) so it's geometry-aware:
        // if the device geometry says the pivot is 0.5 m in front of the head,
        // we measure from there.
        vector pivotWorld[4];
        m_FocusCameraPivot.GetWorldTransform(pivotWorld);
        if (vector.Distance(pivotWorld[3], m_EntryCameraOrigin) > m_fFocusMaxRange)
        {
            ExitFocus(false);
            return;
        }

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
            if (!m_bClickHandled && !m_bDragging)
            {
                // First frame of click - decide what to do
                Widget clickable = FindClickableAt(m_fCursorX, m_fCursorY);

                // If we hit the drag surface (or nothing), start dragging
                if (!clickable || clickable == m_wMapDragSurface)
                {
                    if (m_wMapDragSurface && IsPointInWidget(m_wMapDragSurface, m_fCursorX, m_fCursorY))
                    {
                        // Start dragging on map surface
                        m_bDragging = true;
                        m_fLastDragX = m_fCursorX;
                        m_fLastDragY = m_fCursorY;
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
                // Continue dragging - apply delta to map pan
                float deltaX = m_fCursorX - m_fLastDragX;
                float deltaY = m_fCursorY - m_fLastDragY;

                if (m_DisplayController && (deltaX != 0 || deltaY != 0))
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
            // Click released - reset state
            m_bDragging = false;
            m_bClickHandled = false;
        }

        if (m_InputManager.GetActionTriggered("MenuBack"))
        {
            OnBackAction();
        }
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

    //------------------------------------------------------------------------------------------------
    //! Read the cursor-axis actions and integrate into m_fCursorX/Y.
    //! PC actions are AnalogRelative against mouse:[xy]_rel± so they're already
    //! per-frame deltas — multiply by sensitivity only, NOT by timeSlice.
    //! Gamepad actions are stick deflection in [0,1] — these ARE velocities, so
    //! multiply by sensitivity AND timeSlice.
    protected void DriveCursorFromInput(float timeSlice)
    {
        if (!m_InputManager || !m_wContentFrame)
            return;

        float mouseRight = m_InputManager.GetActionValue("TDLCursorRight");
        float mouseLeft  = m_InputManager.GetActionValue("TDLCursorLeft");
        float mouseDown  = m_InputManager.GetActionValue("TDLCursorDown");
        float mouseUp    = m_InputManager.GetActionValue("TDLCursorUp");

        float stickRight = m_InputManager.GetActionValue("TDLCursorGamepadRight");
        float stickLeft  = m_InputManager.GetActionValue("TDLCursorGamepadLeft");
        float stickDown  = m_InputManager.GetActionValue("TDLCursorGamepadDown");
        float stickUp    = m_InputManager.GetActionValue("TDLCursorGamepadUp");

        // Gamepad deadzone — applied per-axis after combining ±. Avoids cursor
        // drift on stick-at-rest noise and gives the stick a snappier feel.
        float stickX = stickRight - stickLeft;
        float stickY = stickDown - stickUp;
        if (Math.AbsFloat(stickX) < m_fGamepadDeadzone) stickX = 0;
        if (Math.AbsFloat(stickY) < m_fGamepadDeadzone) stickY = 0;

        float deltaX = (mouseRight - mouseLeft) * m_fCursorSensitivityMouse
                     + stickX * m_fCursorSensitivityGamepad * timeSlice;
        float deltaY = (mouseDown - mouseUp) * m_fCursorSensitivityMouse
                     + stickY * m_fCursorSensitivityGamepad * timeSlice;

        m_fCursorX = m_fCursorX + deltaX;
        m_fCursorY = m_fCursorY + deltaY;

        float frameW, frameH;
        m_wContentFrame.GetScreenSize(frameW, frameH);
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
        m_bDragging = false;
        m_bClickHandled = false;
        
        // Cleanup menu controller first (it may want to fire OnPanelHidden
        // on the active plugin while widget tree is still alive). Then the
        // display controller.
        if (m_MenuController)
        {
            m_MenuController.Cleanup();
            m_MenuController = null;
        }

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