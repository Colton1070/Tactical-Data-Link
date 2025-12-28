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
        
        // Defer setup to ensure slots are populated
        GetGame().GetCallqueue().CallLater(SetupRenderTarget, 100, false, owner);
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnFrame(IEntity owner, float timeSlice)
    {
        // MULTIPLAYER OPTIMIZATION: Only process if this device is held by local player
        // Other players' devices simulate locally but we skip expensive raycast/UI work
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc || !pc.IsHeldDevice(owner))
        {
            // Not our device - ensure clean state and bail
            if (m_bLookingAtScreen)
                SetLookingAtScreen(false);
            return;
        }
        
        // Update the display controller each frame
        if (m_DisplayController)
            m_DisplayController.Update(timeSlice);
        
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
        
        // Bind to the screen mesh
        m_RTWidget.SetRenderTarget(m_ScreenEntity);
        
        Print("[TDL_WorldSpaceDisplay] Render target bound, initializing display controller...", LogLevel.NORMAL);
        
        // Initialize the display controller with the ATAK layout
        m_DisplayController = new AG0_TDLDisplayController();
        if (!m_DisplayController.Init(m_wRoot))
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Display controller init failed", LogLevel.ERROR);
            m_DisplayController = null;
            return;
        }
        
        // Setup interaction system
        SetupInteraction();
        
        Print("[TDL_WorldSpaceDisplay] World-space ATAK display initialized successfully", LogLevel.NORMAL);
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
        
        // Network button
        Widget network = m_wRoot.FindAnyWidget("NetworkButton");
        if (network)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                network.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnNetworkClicked);
        }
        
        // Back button
        Widget back = m_wRoot.FindAnyWidget("BackButton");
        if (back)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                back.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnBackButtonClicked);
        }
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
    
    protected void OnNetworkClicked(SCR_ModularButtonComponent comp)
    {
        bool currentlyVisible = AG0_TDLDisplayController.GetSidePanelVisible();
        if (currentlyVisible)
            AG0_TDLDisplayController.SetPanelState(false, false, false, false, "");
        else
            AG0_TDLDisplayController.SetPanelState(true, true, false, false, "CONTACTS");
    }
    
    protected void OnBackButtonClicked(SCR_ModularButtonComponent comp)
    {
        AG0_TDLDisplayController.SetPanelState(true, true, false, false, "CONTACTS");
    }
    
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
                FrameSlot.SetPos(m_wCursor, m_fCursorX, m_fCursorY);
                m_wCursor.SetVisible(true);
            }
            
            SetLookingAtScreen(true);
            UpdateHoveredWidget();
            
            // Activate context and poll input
            if (m_InputManager)
            {
                m_InputManager.ActivateContext("TDLScreenContext");
                
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
        
        // Get RT widget size for pixel scaling
        float rtW, rtH;
        m_RTWidget.GetScreenSize(rtW, rtH);
        
        // Normalize to 0-1 UV (Z = horizontal, Y = vertical)
        float u = (localHit[2] / m_vScreenWorldSize[2]) + 0.5;
        float v = 1.0 - ((localHit[1] / m_vScreenWorldSize[1]) + 0.5); // Flip Y
        
        // Clamp to valid range
        u = Math.Clamp(u, 0, 1);
        v = Math.Clamp(v, 0, 1);
        
        // Scale to pixels
        uiX = u * rtW;
        uiY = v * rtH;
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
    // CLEANUP
    // ============================================
    
    //------------------------------------------------------------------------------------------------
    void Cleanup()
    {
        m_bInteractionEnabled = false;
        m_bLookingAtScreen = false;
        m_bDragging = false;
        m_bClickHandled = false;
        
        // Cleanup display controller first
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