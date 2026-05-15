modded enum ChimeraMenuPreset
{
    AG0_TDLMenu
}

//------------------------------------------------------------------------------------------------
// Panel content types - what's showing in the side panel
enum ETDLPanelContent
{
    NONE,           // Panel hidden, map only
    NETWORK_LIST,   // Member list
    MEMBER_DETAIL,  // Selected member details
	DIRECT_CHAT,
    SETTINGS,       // Future: settings/config
    MARKER_TOOL,    // Marker placement tool — type/subtype/colour/text picker; placement via PC click or console pan+confirm
    PLUGIN_TOOL     // Plugin-owned side panel content (e.g. MPU5 management). Active plugin tracked by AG0_TDLMenuController.
}

//------------------------------------------------------------------------------------------------
//! TDL ATAK-style interface menu - Map-centric with overlay panels
//! Uses AG0_TDLDisplayController for display, handles interactions
class AG0_TDLMenuUI : ChimeraMenuBase
{
    // Persistent state and the panel state machine live on AG0_TDLMenuController
    // — shared with TDL_WorldSpaceDisplayComponent so both frontends drive the
    // same TDLMenuUI.layout from one source of truth.
    
    // ============================================
    // DISPLAY CONTROLLER - handles map, markers, self info, member cards
    // ============================================
    protected ref AG0_TDLDisplayController m_DisplayController;
    
    // ============================================
    // MENU-ONLY STATE (interaction, not display)
    // ============================================
    // Shared menu controller — owns panel state machine, navigation handlers,
    // and (post Phase 2 refactor) plugin lifecycle, chat infra, callsign save,
    // camera broadcast toggle, and marker tool panel. The menu pushes device
    // refs onto it and drives Tick from OnMenuUpdate.
    protected ref AG0_TDLMenuController m_MenuController;
    protected ref AG0_TDLMapCanvasDragHandler m_DragHandler;
    
    // Core references
    protected AG0_TDLDeviceComponent m_ActiveDevice;
    protected AG0_TDLDeviceComponent m_NetworkDevice;
    protected Widget m_wRoot;
    protected InputManager m_InputManager;
    
    // Side panel structure - menu manages visibility
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wNetworkContent;
    protected Widget m_wDetailContent;
    protected Widget m_wSettingsContent;
    protected Widget m_wScrollContainer;
    protected ScrollLayoutWidget m_wScrollLayout;
    
    // Detail content widgets
    protected TextWidget m_wDetailPlayerName;
    protected TextWidget m_wDetailSignalStrength;
    protected TextWidget m_wDetailNetworkIP;
    protected TextWidget m_wDetailGrid;
    protected TextWidget m_wDetailDistance;
    protected TextWidget m_wDetailCapabilities;
    protected Widget m_wViewFeedButton;
    protected Widget m_wViewLocationButton;
    protected Widget m_wBackButton;
    
    // SettingsBackButton kept for gamepad-fallback focus only; the real
    // CallsignSaveButton + CallsignEditBox now cached on the controller.
    protected Widget m_wSettingsButton;
    protected Widget m_wSettingsBackButton;

    // Marker tool widgets / panel instance / crosshair — all moved to
    // AG0_TDLMenuController. Menu only needs m_wMarkerToolBackButton for
    // gamepad-fallback focus.
    protected Widget m_wMarkerToolBackButton;
    
    // Toolbar widgets
    // m_wToolbar / m_wMenuButton / m_wCameraButton cached on controller —
    // menu still needs m_wNetworkButton for gamepad initial-focus.
    protected Widget m_wNetworkButton;
    
    // Zoom/compass controls - menu handles button clicks
    protected Widget m_wZoomInButton;
    protected Widget m_wZoomOutButton;
    protected Widget m_wCompassButton;
    protected Widget m_wTrackButton;
    
    // Member selection lives on m_MenuController.


    // Gamepad focused-card index — tracked via controller's m_OnMemberCardFocused
    // subscription so SetPanelFocus(NETWORK_LIST) can restore the last focused
    // card after navigating away and back. m_iLastCardCount moved to controller.
    protected int m_iFocusedCardIndex = -1;
    
    // Gamepad map pan settings
    // Top speed at full stick deflection — pixels/second of pan in screen space.
    // Pan() converts screen→world via worldUnitsPerPixel, so this stays
    // visually consistent across zoom levels (panning a satellite-tile width
    // takes the same wall time at any zoom). Bumped from the original 400 —
    // gamepad map-pan needs to *feel* responsive, not nudge.
    protected const float STICK_PAN_SPEED = 1400.0;
    protected const float STICK_DEADZONE = 0.15;
    // Quadratic response curve — multiply stick value by its absolute value so
    // small deflections stay precise (good for a couple metres of nudge) but
    // full deflection accelerates quickly. Sign preserved by the |x| factor.
    // Common gamepad-camera curve, chosen over a hard exponent so the response
    // ramps smoothly without a perceptible "kick" past the deadzone.
    
    // Remote feed viewing
    protected bool m_bViewingRemoteFeed = false;
    protected IEntity m_SpawnedFeedCamera;
    protected CameraBase m_OriginalCamera;
    protected Widget m_wFeedOverlay;
    protected Widget m_wFeedBackButton;
    protected TextWidget m_wFeedMemberName;
    
    // Pending feed attachment
    protected RplId m_PendingFeedSourceId;
    protected RplId m_AttachedFeedSourceId;
    protected vector m_PendingFeedPosition;
    protected float m_fFeedAttachTimer;
    protected const float FEED_ATTACH_TIMEOUT = 5.0;
	
	// Chat panel widgets / state / image-card rendering / MESSAGE_CARD_LAYOUT
	// — all moved to AG0_TDLMenuController. The menu's only remaining link
	// is the OpenDirectChat wrapper that AG0_TDLDetailButtonHandler still
	// calls into by name.
    
    protected const ResourceName FEED_CAMERA_PREFAB = "{F3CDC6E4F329E496}Prefabs/Characters/Core/TDLDevicePlayerCamera.et";
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen()
    {
        m_wRoot = GetRootWidget();
        m_InputManager = GetGame().GetInputManager();

        Widget dragSurface = m_wRoot.FindAnyWidget("MapDragSurface");
        if (dragSurface)
        {
            dragSurface.SetFlags(WidgetFlags.NOFOCUS);
            m_DragHandler = new AG0_TDLMapCanvasDragHandler();
            dragSurface.AddHandler(m_DragHandler);
            m_DragHandler.m_OnDragStart.Insert(OnMapDragStart);
            // Route map clicks to the controller — marker placement (KBM)
            // is shared between frontends.
            m_DragHandler.m_OnClick.Insert(OnMapClickedDelegated);
        }
        
        // Side panel structure
        m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
        m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
        m_wNetworkContent = m_wRoot.FindAnyWidget("NetworkContent");
        m_wDetailContent = m_wRoot.FindAnyWidget("DetailContent");
        m_wSettingsContent = m_wRoot.FindAnyWidget("SettingsContent");
        m_wScrollContainer = m_wRoot.FindAnyWidget("ScrollLayout");
        m_wScrollLayout = ScrollLayoutWidget.Cast(m_wScrollContainer);
        
        // Detail content widgets
        m_wDetailPlayerName = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailPlayerName"));
        m_wDetailSignalStrength = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailSignalStrength"));
        m_wDetailNetworkIP = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailNetworkIP"));
        m_wDetailGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailGrid"));
        m_wDetailDistance = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailDistance"));
        m_wDetailCapabilities = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailCapabilities"));
        m_wViewFeedButton = m_wRoot.FindAnyWidget("ViewFeedButton");
        m_wViewLocationButton = m_wRoot.FindAnyWidget("ViewLocationButton");
        m_wBackButton = m_wRoot.FindAnyWidget("BackButton");
        
        // Callsign / SettingsButton + CallsignSaveButton widgets cached by
        // AG0_TDLMenuController.Init. Menu still caches SettingsBackButton
        // for gamepad-fallback focus when the edit box isn't reachable.
        m_wSettingsButton = m_wRoot.FindAnyWidget("SettingsButton");
        m_wSettingsBackButton = m_wRoot.FindAnyWidget("SettingsBackButton");

        // Marker tool widgets cached by AG0_TDLMenuController.Init (which
        // also constructs the AG0_TDLMarkerToolPanel instance). Menu still
        // caches MarkerToolBackButton for gamepad-fallback focus.
        m_wMarkerToolBackButton = m_wRoot.FindAnyWidget("MarkerToolBackButton");
        
        // Toolbar widgets — most cached by controller. Menu only needs
        // NetworkButton for its OnMenuOpen initial-focus call below.
        m_wNetworkButton = m_wRoot.FindAnyWidget("NetworkButton");
        
        // Zoom/compass controls
        m_wZoomInButton = m_wRoot.FindAnyWidget("ZoomInButton");
        m_wZoomOutButton = m_wRoot.FindAnyWidget("ZoomOutButton");
        m_wCompassButton = m_wRoot.FindAnyWidget("CompassButton");
        m_wTrackButton = m_wRoot.FindAnyWidget("TrackButton");
        
        // Feed overlay
        m_wFeedOverlay = m_wRoot.FindAnyWidget("FeedOverlay");
        m_wFeedMemberName = TextWidget.Cast(m_wRoot.FindAnyWidget("FeedMemberName"));
        m_wFeedBackButton = m_wRoot.FindAnyWidget("FeedBackButton");
		
		// Chat widgets (ViewChat, ChatContent, MessageList, MessageEditBox,
		// ChatSendButton, scroll layout) cached by AG0_TDLMenuController.Init.
        
        if (m_wFeedBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wFeedBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnFeedBackClicked);
        }
        
        // ============================================
        // INITIALIZE DISPLAY CONTROLLER
        // ============================================
        m_DisplayController = new AG0_TDLDisplayController();
        if (!m_DisplayController.Init(m_wRoot))
        {
            Print("[TDLMenu] Failed to initialize display controller", LogLevel.ERROR);
        }

        // Marker tool panel construction moved to AG0_TDLMenuController.Init.


        // Get active devices — found by walking the player controller's held
        // devices. Cached on the menu (still referenced by chat/callsign code
        // that hasn't migrated yet) AND pushed onto the controller so the
        // shared plugin lifecycle / etc. can read them.
        FindActiveDevice();
        FindNetworkDevice();

        // Create the shared menu controller and have it drive panel state on
        // this layout. Subscribe to OnPanelChanged for menu-specific reactions
        // (marker tool sub-panel, crosshair, chat repopulate, gamepad focus)
        // and to OnDetailShown for view-feed button visibility refresh that
        // depends on remote feed state the controller can't see.
        //
        // Order matters: Init caches widgets and hooks nav handlers; we then
        // push devices in so RefreshPlugins has what it needs; then refresh
        // plugins so RestoreState's plugin lookup finds a live active list.
        m_MenuController = new AG0_TDLMenuController();
        if (m_MenuController.Init(m_wRoot))
        {
            m_MenuController.m_OnPanelChanged.Insert(OnControllerPanelChanged);
            m_MenuController.SetActiveDevice(m_ActiveDevice);
            m_MenuController.SetNetworkDevice(m_NetworkDevice);
            // Display controller ref — needed for marker placement
            // (mapView.GetCenter) and marker delete (DropVanillaMarkerWidgetById).
            m_MenuController.SetDisplayController(m_DisplayController);
            m_MenuController.RefreshPlugins();
            // RestoreState reads s_LastSelectedDeviceId / s_sLastPanelPluginID
            // / s_eLastPanel etc. and installs the active plugin if it's still
            // enabled, then drives SetPanelContent(s_eLastPanel).
            m_MenuController.RestoreState();
            // Subscribe via the controller — message infra now lives there so
            // both frontends own their subscribe lifecycle independently
            // (menu open/close vs world-space mount/unmount).
            m_MenuController.SubscribeToMessageUpdates();
        }
        
        // Hook button handlers
        HookButtonHandlers();
        // Card handler attachment is driven by controller.Tick — no explicit
        // call needed here. Subscribe to focus invoker so we can remember
        // the gamepad-focused card index for restore on panel return.
        if (m_MenuController)
            m_MenuController.m_OnMemberCardFocused.Insert(OnMemberCardFocused);
		
		if (m_wNetworkButton)
	    {
	        ButtonWidget btn = ButtonWidget.Cast(m_wNetworkButton);
	        if (btn)
	            GetGame().GetWorkspace().SetFocusedWidget(btn);
	    }
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuUpdate(float tDelta)
    {
        super.OnMenuUpdate(tDelta);

        // Image-card rendering / periodic canvas redraw / scroll-to-bottom
        // moved into AG0_TDLMenuController.Tick — see the m_MenuController.Tick
        // call near the bottom of this method.

        if (m_bViewingRemoteFeed)
	    {
	        if (m_InputManager && m_InputManager.GetActionTriggered("MenuBack"))
	        {
	            ExitRemoteFeedView();
	            return;
	        }
	        
	        // Poll for pending attachment (far distance - waiting for device to stream in)
	        if (m_PendingFeedSourceId != RplId.Invalid())
	        {
	            m_fFeedAttachTimer += tDelta;
	            
	            // Keep camera at member's last known position while waiting
	            if (m_SpawnedFeedCamera && m_PendingFeedPosition != vector.Zero)
	            {
	                vector pendingTransform[4];
	                Math3D.MatrixIdentity4(pendingTransform);
	                pendingTransform[3] = m_PendingFeedPosition;
	                m_SpawnedFeedCamera.SetWorldTransform(pendingTransform);
	            }
	            
	            RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_PendingFeedSourceId));
	            if (rpl)
	            {
	                IEntity remoteEntity = rpl.GetEntity();
	                if (remoteEntity)
	                {
	                    AG0_TDLDeviceComponent remoteDevice = AG0_TDLDeviceComponent.Cast(
	                        remoteEntity.FindComponent(AG0_TDLDeviceComponent));
	                    
	                    if (remoteDevice && remoteDevice.m_CameraAttachment)
	                    {
	                        AttachCameraToDevice(remoteEntity, remoteDevice);
	                        
	                        m_AttachedFeedSourceId = m_PendingFeedSourceId;
	                        m_PendingFeedSourceId = RplId.Invalid();
	                    }
	                }
	            }
	            
	            if (m_fFeedAttachTimer > FEED_ATTACH_TIMEOUT)
	            {
	                // Timed out waiting for device - exit feed view
	                ExitRemoteFeedView();
	            }
	            return;
	        }
	        
	        // Check if attached source is still valid (broadcasting and in range)
	        if (m_AttachedFeedSourceId != RplId.Invalid())
	        {
	            if (!IsVideoSourceStillValid(m_AttachedFeedSourceId))
	            {
	                ExitRemoteFeedView();
	                return;
	            }
	        }
	        
	        return;
	    }
        
        // Process mouse drag input - calls controller for pan, disables tracking
        if (m_DragHandler && m_DisplayController)
        {
            int deltaX, deltaY;
            if (m_DragHandler.GetDragDelta(deltaX, deltaY))
            {
                AG0_TDLMapView mapView = m_DisplayController.GetMapView();
                if (mapView)
                    mapView.Pan(deltaX, -deltaY);
                AG0_TDLDisplayController.SetPlayerTracking(false);
            }
        }
        
        // Marker tool action poll folded into controller.Tick.

        // Process gamepad right stick pan input
        if (m_DisplayController && m_InputManager)
        {
            float panX = m_InputManager.GetActionValue("TDLPanHorizontal");
            float panY = m_InputManager.GetActionValue("TDLPanVertical");
            
            if (Math.AbsFloat(panX) > STICK_DEADZONE || Math.AbsFloat(panY) > STICK_DEADZONE)
            {
                // Quadratic response — see STICK_PAN_SPEED comment. val * |val|
                // keeps the sign and squares the magnitude.
                float curvedX = panX * Math.AbsFloat(panX);
                float curvedY = panY * Math.AbsFloat(panY);

                float deltaX = -curvedX * STICK_PAN_SPEED * tDelta;
                float deltaY = curvedY * STICK_PAN_SPEED * tDelta;

                AG0_TDLMapView mapView = m_DisplayController.GetMapView();
                if (mapView)
                    mapView.Pan(deltaX, -deltaY);
                AG0_TDLDisplayController.SetPlayerTracking(false);
            }
        }
        
        // ============================================
        // UPDATE DISPLAY CONTROLLER
        // ============================================
        if (m_DisplayController)
            m_DisplayController.Update(tDelta);
        
        // Card handler re-attachment on rebuild folded into controller.Tick.
        // Detail-view refresh + camera button state likewise.
        
        // Handle input
        HandleInput();

        // Controller tick — drives plugin OnMenuUpdate (post Phase 2) and
        // later phases will fold image-card rendering / camera button /
        // marker action poll into this same call. Shared with world-space.
        if (m_MenuController)
            m_MenuController.Tick(tDelta, m_InputManager);
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuClose()
    {
        if (m_bViewingRemoteFeed)
            ExitRemoteFeedView();
        
        if (m_DragHandler)
        {
            m_DragHandler.m_OnDragStart.Remove(OnMapDragStart);
            m_DragHandler.CancelDrag();
        }
        
        // Save panel state via the controller — it captures the active plugin
        // ID (so a PLUGIN_TOOL panel can be restored if the plugin is still
        // enabled next open) and clears the panel cleanly so OnPanelHidden
        // fires while the plugin's still alive.
        if (m_MenuController)
        {
            m_MenuController.SaveState();
            // Disable plugins (fires OnMenuClosed + tears down toolbar buttons)
            // — controller owns plugin lifecycle now. Cleanup() below would
            // also do this, but doing it explicitly first keeps the close
            // ordering symmetric with OnMenuOpen.
            m_MenuController.DisablePlugins();
        }

        // Unsubscribe via controller — pairs with SubscribeToMessageUpdates
        // in OnMenuOpen. Other frontends (world-space) maintain their own
        // subscription independently so this unsubscribe is per-menu.
        if (m_MenuController)
            m_MenuController.UnsubscribeFromMessageUpdates();

        // Drop the menu controller's widget refs / plugin slot so it doesn't
        // hold dangling pointers if the same controller instance somehow
        // survives. DisablePlugins above ran the plugin-side teardown first;
        // Cleanup is idempotent on plugins (empty array no-op).
        if (m_MenuController)
            m_MenuController.Cleanup();

        // ============================================
        // CLEANUP DISPLAY CONTROLLER
        // ============================================
        if (m_DisplayController)
        {
            m_DisplayController.Cleanup();
            m_DisplayController = null;
        }

        super.OnMenuClose();
    }
    
    // AttachCardHandlers moved to AG0_TDLMenuController. Driven from
    // controller.Tick on card-list rebuild — no per-frontend wiring needed.
    
    //------------------------------------------------------------------------------------------------
    // BUTTON HANDLERS
    //------------------------------------------------------------------------------------------------
    protected void HookButtonHandlers()
    {
        // BackButton / NetworkButton / SettingsButton / SettingsBackButton /
        // MarkerToolButton are hooked by AG0_TDLMenuController during its Init
        // — don't double-hook here.

        // CameraButton hooked by AG0_TDLMenuController.HookButtonHandlers
        // — don't double-hook.

        if (m_wViewFeedButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wViewFeedButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnViewFeedClickedInternal);
        }
        
        if (m_wViewLocationButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wViewLocationButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnViewLocationClickedInternal);
        }
        
        if (m_wZoomInButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wZoomInButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnZoomInClickedInternal);
        }
        
        if (m_wZoomOutButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wZoomOutButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnZoomOutClickedInternal);
        }
        
        if (m_wCompassButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCompassButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnCompassClickedInternal);
        }
        
        if (m_wTrackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wTrackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnTrackClickedInternal);
        }
        
        // CallsignSaveButton hooked by AG0_TDLMenuController.HookButtonHandlers.

		// ChatSendButton / ViewChatButton hooked by AG0_TDLMenuController.HookButtonHandlers
		// — don't double-hook.

        // MarkerToolBackButton + MarkerToolPlaceButton hooked by controller.
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnMapDragStart()
    {
        AG0_TDLDisplayController.SetPlayerTracking(false);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnZoomInClickedInternal()
    {
        if (!m_DisplayController)
            return;
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
            mapView.ZoomIn(0.05);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnZoomOutClickedInternal()
    {
        if (!m_DisplayController)
            return;
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
            mapView.ZoomOut(0.05);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnCompassClickedInternal()
    {
        AG0_TDLDisplayController.SetTrackUp(!AG0_TDLDisplayController.GetTrackUp());
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnTrackClickedInternal()
    {
        AG0_TDLDisplayController.SetPlayerTracking(!AG0_TDLDisplayController.GetPlayerTracking());
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnViewLocationClickedInternal()
    {
        if (!m_MenuController.GetSelectedMember() || !m_DisplayController)
            return;
        
        vector pos = m_MenuController.GetSelectedMember().GetPosition();
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
        {
            mapView.SetCenter(pos);
            AG0_TDLDisplayController.SetPlayerTracking(false);
        }
    }
    
    // OnCallsignSaveClicked moved to AG0_TDLMenuController.
    
    // Marker tool handlers + crosshair update moved to AG0_TDLMenuController.

    //! Map click delegate — routes the drag-handler's click event into the
    //! controller's marker placement path AND the bloodhound pin/unpin path.
    //! Each handler internally gates on whether its tool is active, so only
    //! one will act per click. Drag-handler m_OnClick only fires on
    //! click-without-drag, so panning the map never reaches here.
    protected void OnMapClickedDelegated(int absMouseX, int absMouseY)
    {
        if (!m_MenuController)
            return;
        m_MenuController.OnMapClickedForMarkerPlacement(absMouseX, absMouseY);
        m_MenuController.OnMapClickedForBloodhound(absMouseX, absMouseY);
    }
    
    //------------------------------------------------------------------------------------------------
    // PANEL MANAGEMENT — controller owns the state machine. This wrapper
    // exists so internal callers and existing tests can keep using
    // SetPanelContent without knowing about the controller.
    //------------------------------------------------------------------------------------------------
    protected void SetPanelContent(ETDLPanelContent content)
    {
        if (m_MenuController)
            m_MenuController.SetPanelContent(content);
    }

    //------------------------------------------------------------------------------------------------
    //! Fired by AG0_TDLMenuController.m_OnPanelChanged after the panel state
    //! has settled. Menu-specific reactions live here: marker tool sub-panel
    //! lifecycle, crosshair visibility, chat repopulate, gamepad focus.
    protected void OnControllerPanelChanged()
    {
        if (!m_MenuController)
            return;

        ETDLPanelContent content = m_MenuController.GetActivePanel();

        // Marker tool sub-form lifecycle + crosshair / chat repopulate /
        // callsign pre-populate all moved into controller.SetPanelContent.
        // Menu only reacts here for gamepad focus.

        SetPanelFocus(content);
    }

    //------------------------------------------------------------------------------------------------
    protected void SetPanelFocus(ETDLPanelContent content)
    {
        switch (content)
        {
            case ETDLPanelContent.NETWORK_LIST:
                if (m_DisplayController)
                {
                    array<Widget> cards = m_DisplayController.GetMemberCards();
                    if (cards && !cards.IsEmpty())
                    {
                        int idx = Math.Max(0, m_iFocusedCardIndex);
                        if (idx < cards.Count())
                            GetGame().GetWorkspace().SetFocusedWidget(cards[idx]);
                    }
                }
                break;

            case ETDLPanelContent.MEMBER_DETAIL:
                if (m_wBackButton)
                    GetGame().GetWorkspace().SetFocusedWidget(m_wBackButton);
                break;

            case ETDLPanelContent.SETTINGS:
            {
                AG0_EditBoxComponent callsignBox = m_MenuController.GetCallsignEditBox();
                if (callsignBox)
                    callsignBox.Focus();
                else if (m_wSettingsBackButton)
                    GetGame().GetWorkspace().SetFocusedWidget(m_wSettingsBackButton);
                break;
            }

            case ETDLPanelContent.DIRECT_CHAT:
            {
                AG0_EditBoxComponent chatBox = m_MenuController.GetChatEditBox();
                if (chatBox)
                    chatBox.Focus();
                break;
            }

            case ETDLPanelContent.MARKER_TOOL:
                // Focus the back button as a safe default until the panel
                // controller provides a more specific first-focus widget
                // (e.g. the type picker once it's authored).
                if (m_wMarkerToolBackButton)
                    GetGame().GetWorkspace().SetFocusedWidget(m_wMarkerToolBackButton);
                break;

            case ETDLPanelContent.NONE:
                if (m_wNetworkButton)
                    GetGame().GetWorkspace().SetFocusedWidget(m_wNetworkButton);
                break;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Thin wrappers — controller owns the real logic. Kept so internal menu
    // call sites continue to use these short-form names. Plugin code routes
    // directly through m_Controller.RequestPluginPanel(this).
    //------------------------------------------------------------------------------------------------
    protected void ToggleSidePanel()
    {
        if (m_MenuController)
            m_MenuController.ToggleSidePanel();
    }

    protected void ShowDetailView(AG0_TDLNetworkMember member, RplId deviceId)
    {
        if (m_MenuController)
            m_MenuController.ShowDetailView(member, deviceId);
    }
    
    //------------------------------------------------------------------------------------------------
    // INPUT HANDLING
    //------------------------------------------------------------------------------------------------
    protected void HandleInput()
    {
        if (!m_InputManager)
            return;
        
        if (m_InputManager.GetActionTriggered("MenuBack"))
        {
            if (m_MenuController.GetActivePanel() == ETDLPanelContent.MEMBER_DETAIL)
            {
                SetPanelContent(ETDLPanelContent.NETWORK_LIST);
            }
			if (m_MenuController.GetActivePanel() == ETDLPanelContent.DIRECT_CHAT)
			{
				SetPanelContent(ETDLPanelContent.MEMBER_DETAIL);
			}
            else if (m_MenuController.GetActivePanel() == ETDLPanelContent.SETTINGS)
            {
                SetPanelContent(ETDLPanelContent.NETWORK_LIST);
            }
            else
            {
                Close();
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // PUBLIC BUTTON HANDLERS (for external callers like DetailButtonHandler)
    //------------------------------------------------------------------------------------------------
    void OnDetailBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnViewFeedClicked()
    {
        OnViewFeedClickedInternal();
    }
    
    //------------------------------------------------------------------------------------------------
    void OnViewLocationClicked()
    {
        OnViewLocationClickedInternal();
    }
    
    //------------------------------------------------------------------------------------------------
    void OnZoomInClicked()
    {
        OnZoomInClickedInternal();
    }
    
    //------------------------------------------------------------------------------------------------
    void OnZoomOutClicked()
    {
        OnZoomOutClickedInternal();
    }
    
    //------------------------------------------------------------------------------------------------
    void EnablePlayerTracking()
    {
        AG0_TDLDisplayController.SetPlayerTracking(true);
    }
    
    //------------------------------------------------------------------------------------------------
    // MAP MARKER CALLBACKS
    //------------------------------------------------------------------------------------------------
    void OnMapMarkerClicked(RplId memberId)
    {
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
        if (!member)
            return;
        
        ShowDetailView(member, memberId);
    }
    
    //------------------------------------------------------------------------------------------------
    void OnMapMarkerFocused(RplId memberId)
    {
        if (!m_DisplayController)
            return;
        
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
        if (!member)
            return;
        
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
        {
            mapView.SetCenter(member.GetPosition());
            AG0_TDLDisplayController.SetPlayerTracking(false);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void ClearMarkerFocus()
    {
        // Placeholder for compatibility
    }
    
    //------------------------------------------------------------------------------------------------
    // MEMBER CARD CALLBACKS
    //------------------------------------------------------------------------------------------------
    //! Subscribed to AG0_TDLMenuController.m_OnMemberCardFocused. Used only to
    //! remember the gamepad-focused card index so SetPanelFocus(NETWORK_LIST)
    //! can restore the same card on panel return. Click handling and detail
    //! navigation happen entirely on the controller side.
    protected void OnMemberCardFocused(RplId memberId)
    {
        if (!m_DisplayController)
            return;

        array<RplId> cardIds = m_DisplayController.GetMemberCardIds();
        if (!cardIds)
            return;

        for (int i = 0; i < cardIds.Count(); i++)
        {
            if (cardIds[i] == memberId)
            {
                m_iFocusedCardIndex = i;
                break;
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPERS
    //------------------------------------------------------------------------------------------------
    protected AG0_TDLNetworkMembers GetNetworkMembersFromController()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return null;
        return controller.GetAggregatedTDLMembers();
    }
    
    //------------------------------------------------------------------------------------------------
    protected AG0_TDLNetworkMember GetNetworkMemberById(RplId rplId)
    {
        AG0_TDLNetworkMembers data = GetNetworkMembersFromController();
        if (!data)
            return null;
        return data.GetByRplId(rplId);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void FindActiveDevice()
    {
        m_ActiveDevice = null;
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
            {
                m_ActiveDevice = device;
                return;
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void FindNetworkDevice()
    {
        m_NetworkDevice = null;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS) && device.IsInNetwork())
            {
                m_NetworkDevice = device;
                return;
            }
        }

        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS))
            {
                m_NetworkDevice = device;
                return;
            }
        }
    }
    
    // Plugin lifecycle / toolbar buttons / RequestPluginPanel moved to
    // AG0_TDLMenuController as part of the world-space parity refactor.
    // Plugins call m_Controller.RequestPluginPanel(this) directly now.

    // OnCameraButtonClicked, GetLocalCameraDevice, UpdateCameraButtonState
    // moved to AG0_TDLMenuController. Remote feed view (EnterRemoteFeedView,
    // OnViewFeedClickedInternal, m_SpawnedFeedCamera, render-camera swap)
    // intentionally remains on the menu — it stays menu-only.
    
    //------------------------------------------------------------------------------------------------
    // REMOTE FEED VIEWING
    //------------------------------------------------------------------------------------------------
    protected void OnViewFeedClickedInternal()
    {
        if (!m_MenuController.GetSelectedMember())
            return;
        
        RplId videoSourceId = m_MenuController.GetSelectedMember().GetVideoSourceRplId();
        if (videoSourceId == RplId.Invalid())
            return;
        
        EnterRemoteFeedView(videoSourceId);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnFeedBackClicked()
    {
        ExitRemoteFeedView();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void EnterRemoteFeedView(RplId sourceDeviceRplId)
	{
	    if (m_bViewingRemoteFeed)
	        return;
	    
	    CameraManager camMgr = GetGame().GetCameraManager();
	    if (!camMgr)
	        return;
	    
	    m_OriginalCamera = camMgr.CurrentCamera();
	    
	    IEntity player = GetGame().GetPlayerController().GetControlledEntity();
	    if (!player)
	        return;
	    
	    vector spawnTransform[4];
	    player.GetWorldTransform(spawnTransform);
	    
	    EntitySpawnParams params = new EntitySpawnParams();
	    params.TransformMode = ETransformMode.WORLD;
	    params.Transform = spawnTransform;
	    
	    Resource res = Resource.Load(FEED_CAMERA_PREFAB);
	    if (!res || !res.IsValid())
	        return;
	    
	    m_SpawnedFeedCamera = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
	    if (!m_SpawnedFeedCamera)
	        return;
	    
	    CameraBase feedCamera = CameraBase.Cast(m_SpawnedFeedCamera);
	    if (!feedCamera)
	    {
	        SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFeedCamera);
	        m_SpawnedFeedCamera = null;
	        return;
	    }
	    
	    RplComponent rpl = RplComponent.Cast(Replication.FindItem(sourceDeviceRplId));
	    if (rpl)
	    {
	        IEntity remoteEntity = rpl.GetEntity();
	        if (remoteEntity)
	        {
	            AG0_TDLDeviceComponent remoteDevice = AG0_TDLDeviceComponent.Cast(
	                remoteEntity.FindComponent(AG0_TDLDeviceComponent));
	            
	            if (remoteDevice && remoteDevice.m_CameraAttachment)
	            {
	                vector cameraTransform[4];
	                GetCameraTransformFromDevice(remoteEntity, remoteDevice, cameraTransform);
	                m_SpawnedFeedCamera.SetWorldTransform(cameraTransform);
	                AttachCameraToDevice(remoteEntity, remoteDevice);
	                m_AttachedFeedSourceId = sourceDeviceRplId;
	            }
	            else
	            {
	                m_PendingFeedSourceId = sourceDeviceRplId;
	                m_PendingFeedPosition = m_MenuController.GetSelectedMember().GetPosition();
	                m_fFeedAttachTimer = 0;
	            }
	        }
	        else
	        {
	            m_PendingFeedSourceId = sourceDeviceRplId;
	            m_PendingFeedPosition = m_MenuController.GetSelectedMember().GetPosition();
	            m_fFeedAttachTimer = 0;
	        }
	    }
	    else
	    {
	        m_PendingFeedSourceId = sourceDeviceRplId;
	        m_PendingFeedPosition = m_MenuController.GetSelectedMember().GetPosition();
	        m_fFeedAttachTimer = 0;
	    }
	    
	    camMgr.SetCamera(feedCamera);
	    m_bViewingRemoteFeed = true;

	    // FIX: Hide main menu UI when viewing feed
	    HideMainMenuUI();

	    if (m_wFeedOverlay)
	        m_wFeedOverlay.SetVisible(true);
	    
	    if (m_wFeedMemberName && m_MenuController.GetSelectedMember())
	        m_wFeedMemberName.SetText(m_MenuController.GetSelectedMember().GetPlayerName());
	}
	
	protected void HideMainMenuUI()
	{
	    Widget mainFrame = m_wRoot.FindAnyWidget("MainFrame");
	    if (mainFrame)
	        mainFrame.SetVisible(false);
	    
	    Widget image0 = m_wRoot.FindAnyWidget("Image0");
	    if (image0)
	        image0.SetVisible(false);
	}
	
	protected void ShowMainMenuUI()
	{
	    Widget mainFrame = m_wRoot.FindAnyWidget("MainFrame");
	    if (mainFrame)
	        mainFrame.SetVisible(true);
	    
	    Widget image0 = m_wRoot.FindAnyWidget("Image0");
	    if (image0)
	        image0.SetVisible(true);
	}
    
    //------------------------------------------------------------------------------------------------
    protected void ExitRemoteFeedView()
	{
	    if (!m_bViewingRemoteFeed)
	        return;
	    
	    CameraManager camMgr = GetGame().GetCameraManager();
	    if (camMgr && m_OriginalCamera)
	        camMgr.SetCamera(m_OriginalCamera);

	    if (m_SpawnedFeedCamera)
	    {
	        SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFeedCamera);
	        m_SpawnedFeedCamera = null;
	    }
	    
	    m_bViewingRemoteFeed = false;
	    m_PendingFeedSourceId = RplId.Invalid();
	    m_AttachedFeedSourceId = RplId.Invalid();
	    m_OriginalCamera = null;

	    if (m_wFeedOverlay)
	        m_wFeedOverlay.SetVisible(false);

	    // FIX: Restore main menu UI after exiting feed view
	    ShowMainMenuUI();
	}
    
    //------------------------------------------------------------------------------------------------
    protected void GetCameraTransformFromDevice(IEntity deviceEntity, AG0_TDLDeviceComponent deviceComp, out vector outTransform[4])
    {
        if (deviceComp.m_CameraAttachment)
        {
            deviceComp.m_CameraAttachment.GetWorldTransform(outTransform);
        }
        else
        {
            deviceEntity.GetWorldTransform(outTransform);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void AttachCameraToDevice(IEntity deviceEntity, AG0_TDLDeviceComponent deviceComp)
	{
	    if (!m_SpawnedFeedCamera || !deviceEntity || !deviceComp || !deviceComp.m_CameraAttachment)
	        return;
	    
	    int boneIndex = deviceComp.m_CameraAttachment.GetNodeId();
	    
	    deviceEntity.AddChild(m_SpawnedFeedCamera, boneIndex, EAddChildFlags.AUTO_TRANSFORM);
	    
	    vector localTransform[4];
	    deviceComp.m_CameraAttachment.GetLocalTransform(localTransform);
	    
	    // Pitch the camera down?
	    vector correctionAngles = Vector(0, 0, 0);
	    vector correctionMat[3];
	    Math3D.AnglesToMatrix(correctionAngles, correctionMat);
	    
	    // Apply correction to rotation (keep position)
	    vector correctedTransform[4];
	    Math3D.MatrixMultiply3(localTransform, correctionMat, correctedTransform);
	    correctedTransform[3] = localTransform[3];  // Preserve position
	    
	    m_SpawnedFeedCamera.SetLocalTransform(correctedTransform);
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool IsVideoSourceStillValid(RplId sourceId)
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return false;

	    if (!controller.IsVideoSourceAvailable(sourceId))
	        return false;

	    // Verify device is still broadcasting
	    RplComponent rpl = RplComponent.Cast(Replication.FindItem(sourceId));
	    if (rpl)
	    {
	        IEntity entity = rpl.GetEntity();
	        if (entity)
	        {
	            AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
	                entity.FindComponent(AG0_TDLDeviceComponent));
	            
	            if (device && !device.IsCameraBroadcasting())
	                return false;
	        }
	    }
	    
	    return true;
	}
	
	// Chat methods (subscribe, send, populate, image-card rendering,
	// badges, etc.) moved to AG0_TDLMenuController in the world-space parity
	// refactor. Thin wrapper kept below for OpenDirectChat which external
	// handlers (AG0_TDLDetailButtonHandler) call into the menu by name.
	void OpenDirectChat(RplId contactRplId, string contactName)
	{
	    if (m_MenuController)
	        m_MenuController.OpenDirectChat(contactRplId, contactName);
	}
	
	// PopulateChatView, ClearChatMessages, CreateMessageWidget,
	// DriveImageCardRendering, SizePhotoCanvas, InitAndDrawPhotoRenderer,
	// FormatTimestamp, SendChatMessage, SubscribeToMessageUpdates,
	// UnsubscribeFromMessageUpdates, OnMessagesUpdated, OnNewMessageReceived,
	// UpdateMemberCardBadges — moved to AG0_TDLMenuController.
	

}