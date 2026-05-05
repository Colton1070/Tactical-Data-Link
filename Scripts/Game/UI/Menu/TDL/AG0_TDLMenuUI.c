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
    MARKER_TOOL     // Marker placement tool — type/subtype/colour/text picker; placement via PC click or console pan+confirm
}

//------------------------------------------------------------------------------------------------
//! TDL ATAK-style interface menu - Map-centric with overlay panels
//! Uses AG0_TDLDisplayController for display, handles interactions
class AG0_TDLMenuUI : ChimeraMenuBase
{
    // Persistent state for menu-specific things (panel state survives open/close)
    static protected ETDLPanelContent s_eLastPanel = ETDLPanelContent.NETWORK_LIST;
	static protected RplId s_LastSelectedDeviceId = RplId.Invalid();
	static protected RplId s_LastChatContactRplId = RplId.Invalid();
	static protected string s_sLastChatContactName;
    
    // ============================================
    // DISPLAY CONTROLLER - handles map, markers, self info, member cards
    // ============================================
    protected ref AG0_TDLDisplayController m_DisplayController;
    
    // ============================================
    // MENU-ONLY STATE (interaction, not display)
    // ============================================
    protected ETDLPanelContent m_eActivePanel = ETDLPanelContent.NETWORK_LIST;
    protected ref AG0_TDLMapCanvasDragHandler m_DragHandler;
    protected ref array<ref AG0_ATAKPluginBase> m_aActivePlugins = {};
    
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
    
    // Settings content widgets
    protected Widget m_wSettingsButton;
    protected Widget m_wCallsignEditBoxRoot;
    protected ref AG0_EditBoxComponent m_CallsignEditBox;
    protected Widget m_wCallsignSaveButton;
    protected Widget m_wSettingsBackButton;

    // Marker tool — side-tray panel + activation button. Panel content widgets
    // (type picker, subtype area, colour row, text field, place button) are
    // owned by AG0_TDLMarkerToolPanel and looked up off m_wMarkerToolContent
    // when the controller is constructed. Lookups tolerate a missing layout
    // section so the script compiles + runs even before the layout is authored.
    protected Widget m_wMarkerToolContent;
    protected Widget m_wMarkerToolButton;
    protected Widget m_wMarkerToolBackButton;
    protected Widget m_wMarkerToolPlaceButton;
    protected ref AG0_TDLMarkerToolPanel m_MarkerToolPanel;

    // Crosshair shown at the centre of the map view while the marker tool
    // panel is active. Pairs with mapView.GetCenter() being the place-position
    // for both gamepad (TDLPlaceMarker action) and KB+M (the panel-level Place
    // button) — both place at canvas centre, so a visible centre marker helps
    // both modes line up the placement. Direct map-clicks place at the cursor
    // (not centre), so the crosshair is just informational, not authoritative.
    //
    // Note on gamepad-only gating: we tried branching on
    // ArmaReforgerScripted.OnInputDeviceIsGamepadInvoker, but it only fires on
    // *changes* between KB+M and pad — meaning the cached value stays at its
    // (potentially wrong) default until the player flips devices. The public
    // API has no synchronous "is currently using gamepad" getter, so rather
    // than ship a feature that's invisible to most players on first open, we
    // show the crosshair unconditionally while MARKER_TOOL is the active panel.
    protected Widget m_wMarkerCrosshair;
    
    // Toolbar widgets
    protected Widget m_wToolbar;
    protected Widget m_wMenuButton;
    protected Widget m_wNetworkButton;
    protected Widget m_wCameraButton;
    
    // Zoom/compass controls - menu handles button clicks
    protected Widget m_wZoomInButton;
    protected Widget m_wZoomOutButton;
    protected Widget m_wCompassButton;
    protected Widget m_wTrackButton;
    
    // Selected member for detail view
    protected ref AG0_TDLNetworkMember m_SelectedMember;
    protected RplId m_SelectedDeviceId;
    
    // State tracking for card handlers
    protected int m_iFocusedCardIndex = -1;
    protected int m_iLastCardCount = 0;
    
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
	
	// Chat panel widgets
	protected Widget m_wViewChatButton;
	protected Widget m_wChatContent;
	protected TextWidget m_wChatContactName;
	protected ScrollLayoutWidget m_wChatScrollLayout;
	protected Widget m_wChatMessageList;
	protected Widget m_wChatEditBoxRoot;
	protected ref AG0_EditBoxComponent m_ChatEditBox;
	protected Widget m_wChatSendButton;
	
	// Chat state
	protected RplId m_ChatContactRplId = RplId.Invalid();
	protected string m_sChatContactName = "";
	protected ref array<Widget> m_aChatMessageWidgets = {};
	// Parallel arrays per chat message card. Indexed in lockstep with m_aChatMessageWidgets.
	// All four are cleared together in ClearChatMessages.
	//   * m_aChatMessageRenderers[i] — non-null iff the image at index i has been drawn.
	//                                  Text messages and not-yet-drawn images: null.
	//   * m_aChatMessageDeliveryIds[i] — image-message's deliveryId for cache lookup;
	//                                    "" for text messages.
	//   * m_aChatMessageImageCanvases[i] — the ImageCanvas widget on the card; null for
	//                                      text messages.
	// On every OnMenuUpdate tick we walk these arrays and, for each entry where the
	// renderer is null but the deliveryId resolves to a cached photo, render the image
	// in-place. This bypasses the OnDecodedPhotoArrived invoker chain (which doesn't
	// reach the menu in MP) and avoids full PopulateChatView repaints.
	protected ref array<ref AG0_TDLPhotoRenderer> m_aChatMessageRenderers = {};
	protected ref array<string>                   m_aChatMessageDeliveryIds = {};
	protected ref array<CanvasWidget>             m_aChatMessageImageCanvases = {};
	protected bool m_bScrollToBottom = false;

	// Periodic image-canvas refresh accumulator. Image canvases lose their draw commands
	// after several minutes of idle time (engine-side cleanup of CanvasWidgetCommand
	// arrays — symptom is blank canvases on otherwise-still-alive image-messages).
	// We re-Draw() each renderer every IMAGE_REDRAW_INTERVAL seconds; the commands
	// rebuild from the live m_PhotoData and get re-set on the canvas.
	protected float m_fImageRedrawAccum = 0;
	protected const float IMAGE_REDRAW_INTERVAL = 30.0;
	
	protected const ResourceName MESSAGE_CARD_LAYOUT = "{2507AC45B21BBC57}UI/layouts/Menus/TDL/TDLMessageUI.layout";
    
    protected const ResourceName FEED_CAMERA_PREFAB = "{F3CDC6E4F329E496}Prefabs/Characters/Core/TDLDevicePlayerCamera.et";
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen()
    {
        m_wRoot = GetRootWidget();
        m_InputManager = GetGame().GetInputManager();
        
        // Setup drag handler for map pan
        Widget dragSurface = m_wRoot.FindAnyWidget("MapDragSurface");
        if (dragSurface)
        {
            dragSurface.SetFlags(WidgetFlags.NOFOCUS);
            m_DragHandler = new AG0_TDLMapCanvasDragHandler();
            dragSurface.AddHandler(m_DragHandler);
            m_DragHandler.m_OnDragStart.Insert(OnMapDragStart);
            m_DragHandler.m_OnClick.Insert(OnMapClicked);
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
        
        // Settings widgets
        m_wSettingsButton = m_wRoot.FindAnyWidget("SettingsButton");
        m_wCallsignEditBoxRoot = m_wRoot.FindAnyWidget("CallsignEditBox");
        if (m_wCallsignEditBoxRoot)
            m_CallsignEditBox = AG0_EditBoxComponent.FindComponent(m_wCallsignEditBoxRoot);
        m_wCallsignSaveButton = m_wRoot.FindAnyWidget("CallsignSaveButton");
        m_wSettingsBackButton = m_wRoot.FindAnyWidget("SettingsBackButton");

        // Marker tool widgets — null-tolerant lookup so the script runs cleanly
        // even before the layout section exists. Activation button lives in the
        // toolbar/side-panel button row alongside MenuButton/NetworkButton/etc.
        m_wMarkerToolContent = m_wRoot.FindAnyWidget("MarkerToolContent");
        m_wMarkerToolButton = m_wRoot.FindAnyWidget("MarkerToolButton");
        m_wMarkerToolBackButton = m_wRoot.FindAnyWidget("MarkerToolBackButton");
        m_wMarkerToolPlaceButton = m_wRoot.FindAnyWidget("MarkerToolPlaceButton");
        m_wMarkerCrosshair = m_wRoot.FindAnyWidget("MarkerCrosshair");
        if (!m_wMarkerCrosshair)
            Print("[TDLMenu] MarkerCrosshair widget not found in layout — crosshair will not appear", LogLevel.WARNING);
        
        // Toolbar widgets
        m_wToolbar = m_wRoot.FindAnyWidget("Toolbar");
        m_wMenuButton = m_wRoot.FindAnyWidget("MenuButton");
        m_wNetworkButton = m_wRoot.FindAnyWidget("NetworkButton");
        m_wCameraButton = m_wRoot.FindAnyWidget("CameraButton");
        
        // Zoom/compass controls
        m_wZoomInButton = m_wRoot.FindAnyWidget("ZoomInButton");
        m_wZoomOutButton = m_wRoot.FindAnyWidget("ZoomOutButton");
        m_wCompassButton = m_wRoot.FindAnyWidget("CompassButton");
        m_wTrackButton = m_wRoot.FindAnyWidget("TrackButton");
        
        // Feed overlay
        m_wFeedOverlay = m_wRoot.FindAnyWidget("FeedOverlay");
        m_wFeedMemberName = TextWidget.Cast(m_wRoot.FindAnyWidget("FeedMemberName"));
        m_wFeedBackButton = m_wRoot.FindAnyWidget("FeedBackButton");
		
		// Chat content widgets
		m_wViewChatButton = m_wRoot.FindAnyWidget("ViewChatButton");
		m_wChatContent = m_wRoot.FindAnyWidget("ChatContent");
		m_wChatContactName = TextWidget.Cast(m_wRoot.FindAnyWidget("ContactName"));
		m_wChatMessageList = m_wRoot.FindAnyWidget("MessageList");
		m_wChatEditBoxRoot = m_wRoot.FindAnyWidget("MessageEditBox");
		if (m_wChatEditBoxRoot)
		    m_ChatEditBox = AG0_EditBoxComponent.FindComponent(m_wChatEditBoxRoot);
		m_wChatSendButton = m_wRoot.FindAnyWidget("ChatSendButton");
		
		// Find scroll layout inside chat content
		if (m_wChatContent)
		    m_wChatScrollLayout = ScrollLayoutWidget.Cast(m_wChatContent.FindAnyWidget("ScrollLayout"));
        
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

        // ============================================
        // INITIALIZE MARKER TOOL PANEL
        // ============================================
        // Built lazily — sub-form layouts spawn on first OnPanelShown so we
        // don't pay creation cost when the player never opens the tool.
        // Cancel/place events route back through the menu so the menu can
        // own panel-state and (later) cursor-vs-centre place-position math.
        if (m_wMarkerToolContent)
        {
            m_MarkerToolPanel = new AG0_TDLMarkerToolPanel();
            if (m_MarkerToolPanel.Init(m_wMarkerToolContent))
            {
                m_MarkerToolPanel.m_OnCancelRequested.Insert(OnMarkerToolCancelRequested);
                m_MarkerToolPanel.m_OnPlaceRequested.Insert(OnMarkerToolPlaceRequested);
                m_MarkerToolPanel.m_OnMarkerDeleted.Insert(OnMarkerToolMarkerDeleted);
            }
            else
            {
                m_MarkerToolPanel = null;
            }
        }
        
        // Get active devices
        FindActiveDevice();
        FindNetworkDevice();
        RefreshPlugins();
		
		SubscribeToMessageUpdates();
        
        m_SelectedDeviceId = s_LastSelectedDeviceId;
		m_ChatContactRplId = s_LastChatContactRplId;
		m_sChatContactName = s_sLastChatContactName;
		
		// Re-fetch member data if we have a valid device ID
		if (m_SelectedDeviceId != RplId.Invalid())
		    m_SelectedMember = GetNetworkMemberById(m_SelectedDeviceId);
		
		SetPanelContent(s_eLastPanel);
        
        // Hook button handlers
        HookButtonHandlers();
        
        // Attach click handlers to cards
        AttachCardHandlers();
		
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

        // Per-frame image card walk: for each image-message card that hasn't been
        // rendered yet, look up its photo and render in-place if it's now available.
        // This is the deterministic path for image-message UI refresh. No invokers,
        // no PopulateChatView repaints, no panel/network gates — just inspects each
        // card's state and acts on it.
        DriveImageCardRendering();

        // Periodic re-Draw of already-rendered canvases — see m_fImageRedrawAccum comment.
        if (m_aChatMessageRenderers && m_aChatMessageRenderers.Count() > 0)
        {
            m_fImageRedrawAccum += tDelta;
            if (m_fImageRedrawAccum >= IMAGE_REDRAW_INTERVAL)
            {
                m_fImageRedrawAccum = 0;
                foreach (AG0_TDLPhotoRenderer r : m_aChatMessageRenderers)
                {
                    if (r)
                        r.Draw();
                }
            }
        }
        else
        {
            m_fImageRedrawAccum = 0;
        }

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
        
        // Marker tool placement actions (gamepad A / keyboard Enter / X / R).
        // Polled here rather than registered as InputManager listeners
        // because the menu's focus chain consumes those actions before
        // listeners fire — but GetActionTriggered still works in this
        // context, same as the other actions polled in this loop.
        if (m_eActivePanel == ETDLPanelContent.MARKER_TOOL && m_MarkerToolPanel && m_InputManager)
            m_MarkerToolPanel.TickPlaceActionPoll(m_InputManager);

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
        
        // Check if cards were rebuilt and need handlers
        if (m_DisplayController)
        {
            array<Widget> cards = m_DisplayController.GetMemberCards();
            if (cards && cards.Count() != m_iLastCardCount)
            {
                AttachCardHandlers();
                m_iLastCardCount = cards.Count();
            }
        }
        
        // Update detail view if showing
        if (m_eActivePanel == ETDLPanelContent.MEMBER_DETAIL)
            PopulateDetailView();
        
		if (m_bScrollToBottom && m_wChatScrollLayout)
		{
		    m_wChatScrollLayout.SetSliderPos(0, 1.0);
		    m_bScrollToBottom = false;
		}
		
        // Update camera button state
        UpdateCameraButtonState();
        
        // Handle input
        HandleInput();
        
        // Update plugins
        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
            plugin.OnMenuUpdate(tDelta);
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
        
        // Save panel state
        s_eLastPanel = m_eActivePanel;
        
        // Notify and disable plugins
        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
        {
            plugin.OnMenuClosed();
            plugin.Disable();
        }
        m_aActivePlugins.Clear();
        
		UnsubscribeFromMessageUpdates();

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
    
    //------------------------------------------------------------------------------------------------
    // CARD HANDLER ATTACHMENT
    //------------------------------------------------------------------------------------------------
    protected void AttachCardHandlers()
    {
        if (!m_DisplayController)
            return;
        
        array<Widget> cards = m_DisplayController.GetMemberCards();
        array<RplId> cardIds = m_DisplayController.GetMemberCardIds();
        
        if (!cards || !cardIds)
            return;
        
        for (int i = 0; i < cards.Count(); i++)
        {
            Widget card = cards[i];
            if (!card || i >= cardIds.Count())
                continue;
            
            RplId memberId = cardIds[i];
            
            ButtonWidget button = ButtonWidget.Cast(card);
            if (!button)
                continue;
            
            // Check if handler already exists
            AG0_TDLMemberCardHandler existingHandler = AG0_TDLMemberCardHandler.Cast(
                button.FindHandler(AG0_TDLMemberCardHandler));
            if (existingHandler)
                continue;
            
            // Get member data for handler
            AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
            
            AG0_TDLMemberCardHandler handler = new AG0_TDLMemberCardHandler();
            handler.Init(this, memberId, member);
            button.AddHandler(handler);

            // First card: set UP navigation to settings button
            if (i == 0)
                button.SetNavigation(WidgetNavigationDirection.UP, WidgetNavigationRuleType.EXPLICIT, "SettingsButton");
        }

        // Set initial notification badges. Cards may have been created with stale state
        // (NotificationDot visible from layout default, NotificationNumberText showing "0");
        // this call hides badges with no unread and populates counts where there are.
        UpdateMemberCardBadges();
    }
    
    //------------------------------------------------------------------------------------------------
    // BUTTON HANDLERS
    //------------------------------------------------------------------------------------------------
    protected void HookButtonHandlers()
    {
        if (m_wBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnBackClicked);
        }
        
        if (m_wNetworkButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wNetworkButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnNetworkButtonClicked);
        }
        
        if (m_wCameraButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCameraButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnCameraButtonClicked);
        }
        
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
        
        if (m_wSettingsButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsClicked);
        }
        
        if (m_wCallsignSaveButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCallsignSaveButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnCallsignSaveClicked);
        }
        
        if (m_wSettingsBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsBackClicked);
        }
		
		if (m_wViewChatButton)
		{
		    SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
		        m_wViewChatButton.FindHandler(SCR_ModularButtonComponent));
		    if (comp)
		        comp.m_OnClicked.Insert(OnViewDirectChatClicked);
		}
		
		if (m_wChatSendButton)
		{
		    SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
		        m_wChatSendButton.FindHandler(SCR_ModularButtonComponent));
		    if (comp)
		        comp.m_OnClicked.Insert(OnChatSendClicked);
		}

        // Marker tool — toolbar activation + side-panel back. Note the
        // sub-form Cancel/Public/Private buttons inside the spawned
        // edit-box layouts are wired by AG0_TDLMarkerToolPanel itself.
        if (m_wMarkerToolButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolButtonClicked);
        }

        if (m_wMarkerToolBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolCancelRequested);
        }

        // Dedicated panel-level Place button — separate from the in-edit-box
        // ButtonPublic widgets so pressing A while navigating spinboxes /
        // combos doesn't accidentally place. Mouse click on this button
        // routes through the place-requested path.
        if (m_wMarkerToolPlaceButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolPlaceButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolPlaceButtonClicked);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnMapDragStart()
    {
        AG0_TDLDisplayController.SetPlayerTracking(false);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnNetworkButtonClicked()
    {
        ToggleSidePanel();
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
        if (!m_SelectedMember || !m_DisplayController)
            return;
        
        vector pos = m_SelectedMember.GetPosition();
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
        {
            mapView.SetCenter(pos);
            AG0_TDLDisplayController.SetPlayerTracking(false);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnSettingsClicked()
    {
        SetPanelContent(ETDLPanelContent.SETTINGS);
        
        if (m_CallsignEditBox && m_NetworkDevice)
            m_CallsignEditBox.SetText(m_NetworkDevice.GetDisplayName());
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnCallsignSaveClicked()
    {
        if (!m_CallsignEditBox || !m_NetworkDevice)
            return;

        string newCallsign = m_CallsignEditBox.GetText();
        if (newCallsign.IsEmpty())
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        RplId deviceId = m_NetworkDevice.GetDeviceRplId();
        if (deviceId != RplId.Invalid())
            controller.RequestSetDeviceCallsign(deviceId, newCallsign);

        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnSettingsBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    //! Crosshair is visible iff the marker tool panel is the active side
    //! panel. See the m_wMarkerCrosshair declaration comment for why the
    //! gamepad-only gating was dropped.
    protected void UpdateMarkerCrosshairVisibility()
    {
        if (!m_wMarkerCrosshair)
            return;

        m_wMarkerCrosshair.SetVisible(m_eActivePanel == ETDLPanelContent.MARKER_TOOL);
    }

    //------------------------------------------------------------------------------------------------
    // MARKER TOOL HANDLERS
    //------------------------------------------------------------------------------------------------

    //! Toolbar button — open the marker tool side panel.
    protected void OnMarkerToolButtonClicked()
    {
        SetPanelContent(ETDLPanelContent.MARKER_TOOL);
    }

    //! Fired by the marker-tool panel's Cancel/Back buttons (sub-form +
    //! side-panel back). Returns to the contacts list.
    protected void OnMarkerToolCancelRequested()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    //! Fired by the marker-tool panel's Place Public/Private buttons + the
    //! action-poll path (gamepad A / keyboard Enter / X / R). Always places
    //! at the map view's centre — the player has already framed the map
    //! to where they want the marker via pan, and the centre is the
    //! "crosshair" the ghost widget aligns to on console.
    protected void OnMarkerToolPlaceRequested(bool isLocal)
    {
        if (!m_MarkerToolPanel || !m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        m_MarkerToolPanel.PlaceCurrentMarker(mapView.GetCenter(), isLocal);
    }

    //! Mouse click on the dedicated panel-level Place button. Always a
    //! public placement at canvas centre — mirrors the gamepad
    //! TDLPlaceMarker action's behaviour. The user has already framed
    //! the desired location with map pan, so centre = intended drop.
    protected void OnMarkerToolPlaceButtonClicked()
    {
        OnMarkerToolPlaceRequested(false);
    }

    //! Forwarded from AG0_TDLMarkerToolPanel.m_OnMarkerDeleted right after
    //! the AskRemoveStaticMarker RPC fires. The display controller's
    //! 3D widget map is keyed by SCR_MapMarkerBase reference and only
    //! prunes during UpdateVanillaMarkers — which happens next frame and
    //! sees the manager's *current* state, which still has the marker
    //! until the server's broadcast back. Kicking the prune now removes
    //! the on-map icon the same frame as the scroll card.
    protected void OnMarkerToolMarkerDeleted(int markerId)
    {
        if (m_DisplayController)
            m_DisplayController.DropVanillaMarkerWidgetById(markerId);
    }

    //! Fired by AG0_TDLMapCanvasDragHandler when the user left-clicks
    //! MapDragSurface without significant drag (KBM placement path).
    //! Converts the absolute workspace mouse coords into canvas-local
    //! pixels, runs them through AG0_TDLMapView.ScreenToWorld, and places
    //! the marker there. Only active while the marker tool panel is
    //! the visible side-panel — clicks elsewhere just pan the map.
    protected void OnMapClicked(int absMouseX, int absMouseY)
    {
        if (m_eActivePanel != ETDLPanelContent.MARKER_TOOL)
            return;
        if (!m_MarkerToolPanel || !m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        // Resolve canvas widget's absolute screen position so we can
        // translate the absolute mouse pos into canvas-local pixels —
        // ScreenToWorld expects canvas-local, not workspace-absolute.
        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasScreenX, canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);

        float localX = absMouseX - canvasScreenX;
        float localY = absMouseY - canvasScreenY;

        vector worldPos;
        mapView.ScreenToWorld(localX, localY, worldPos);

        m_MarkerToolPanel.PlaceCurrentMarker(worldPos, false);
    }
    
    //------------------------------------------------------------------------------------------------
    // PANEL MANAGEMENT
    //------------------------------------------------------------------------------------------------
    protected void SetPanelContent(ETDLPanelContent content)
    {
        m_eActivePanel = content;

        bool showPanel = (content != ETDLPanelContent.NONE);
        bool showNetwork = (content == ETDLPanelContent.NETWORK_LIST);
        bool showDetail = (content == ETDLPanelContent.MEMBER_DETAIL);
		bool showChat = (content == ETDLPanelContent.DIRECT_CHAT);
        bool showSettings = (content == ETDLPanelContent.SETTINGS);
        bool showMarkerTool = (content == ETDLPanelContent.MARKER_TOOL);

        string title = "CONTACTS";
        switch (content)
        {
            case ETDLPanelContent.MEMBER_DETAIL:
                title = "CONTACT DETAILS";
                break;
            case ETDLPanelContent.SETTINGS:
                title = "SETTINGS";
                break;
			case ETDLPanelContent.DIRECT_CHAT:
			    title = m_sChatContactName;
			    break;
            case ETDLPanelContent.MARKER_TOOL:
                title = "MARKER TOOL";
                break;
        }

        // Update static state so device stays in sync
        AG0_TDLDisplayController.SetPanelState(showPanel, showNetwork, showDetail, showSettings, showMarkerTool, title);

        // Apply to local widgets immediately
        if (m_wSidePanel)
            m_wSidePanel.SetVisible(showPanel);

        if (!showPanel)
            return;

        if (m_wNetworkContent)
            m_wNetworkContent.SetVisible(showNetwork);

        if (m_wDetailContent)
            m_wDetailContent.SetVisible(showDetail);

        if (m_wSettingsContent)
            m_wSettingsContent.SetVisible(showSettings);

        if (m_wMarkerToolContent)
            m_wMarkerToolContent.SetVisible(showMarkerTool);

        // Lazy spawn of the sub-form layouts on first MARKER_TOOL show
        if (showMarkerTool && m_MarkerToolPanel)
            m_MarkerToolPanel.OnPanelShown();
        else if (!showMarkerTool && m_MarkerToolPanel)
            m_MarkerToolPanel.OnPanelHidden();

        // Centre-screen crosshair lives outside MarkerToolContent (it's in
        // MainView, not the side panel) so it has its own visibility hook —
        // gated on both the panel state we just set and the cached input-device
        // bool. Re-evaluate every panel transition.
        UpdateMarkerCrosshairVisibility();

        if (m_wPanelTitle)
            m_wPanelTitle.SetText(title);

		if (m_wChatContent)
		{
		    m_wChatContent.SetVisible(showChat);
		    if (showChat)
		    {
		        if (m_wChatContactName)
		            m_wChatContactName.SetText(m_sChatContactName);
		        PopulateChatView();
		    }
		}

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
                if (m_CallsignEditBox)
                    m_CallsignEditBox.Focus();
                else if (m_wSettingsBackButton)
                    GetGame().GetWorkspace().SetFocusedWidget(m_wSettingsBackButton);
                break;
			
			case ETDLPanelContent.DIRECT_CHAT:
			    if (m_ChatEditBox)
			        m_ChatEditBox.Focus();
			    break;

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
    protected void ToggleSidePanel()
    {
        if (m_eActivePanel == ETDLPanelContent.NONE)
            SetPanelContent(ETDLPanelContent.NETWORK_LIST);
        else
            SetPanelContent(ETDLPanelContent.NONE);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ShowDetailView(AG0_TDLNetworkMember member, RplId deviceId)
    {
        m_SelectedMember = member;
	    m_SelectedDeviceId = deviceId;
	    s_LastSelectedDeviceId = deviceId;
        
        PopulateDetailView();
        SetPanelContent(ETDLPanelContent.MEMBER_DETAIL);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void PopulateDetailView()
    {
        if (m_SelectedDeviceId != RplId.Invalid())
            m_SelectedMember = GetNetworkMemberById(m_SelectedDeviceId);
        
        if (!m_SelectedMember)
            return;
        
        if (m_wDetailPlayerName)
            m_wDetailPlayerName.SetText(m_SelectedMember.GetPlayerName());
        
        if (m_wDetailSignalStrength)
            m_wDetailSignalStrength.SetTextFormat("%1 dBm", m_SelectedMember.GetSignalStrength().ToString());
        
        if (m_wDetailNetworkIP)
            m_wDetailNetworkIP.SetText("192.168.0." + m_SelectedMember.GetNetworkIP().ToString());
        
        if (m_wDetailGrid)
        {
            vector memberPos = m_SelectedMember.GetPosition();
            m_wDetailGrid.SetText(AG0_MGRSGridUtils.GetFullMGRS(memberPos, 5));
        }
        
        if (m_wDetailDistance)
        {
            IEntity player = GetGame().GetPlayerController().GetControlledEntity();
            if (player)
            {
                float dist = vector.Distance(player.GetOrigin(), m_SelectedMember.GetPosition());
                m_wDetailDistance.SetTextFormat("%1 m", Math.Round(dist).ToString());
            }
        }
        
        if (m_wDetailCapabilities)
        {
            string caps = BuildCapabilitiesString(m_SelectedMember.GetCapabilities());
            m_wDetailCapabilities.SetText(caps);
        }
        
        if (m_wViewFeedButton)
        {
            RplId videoSourceId = m_SelectedMember.GetVideoSourceRplId();
            bool isBroadcasting = videoSourceId != RplId.Invalid();
            m_wViewFeedButton.SetVisible(isBroadcasting);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected string BuildCapabilitiesString(int caps)
    {
        string result = "";
        if ((caps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0) result += "[GPS] ";
        if ((caps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0) result += "[CAM] ";
        if ((caps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0) result += "[DISP] ";
        if ((caps & AG0_ETDLDeviceCapability.INFORMATION) != 0) result += "[INFO] ";
        return result;
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
            if (m_eActivePanel == ETDLPanelContent.MEMBER_DETAIL)
            {
                SetPanelContent(ETDLPanelContent.NETWORK_LIST);
            }
			if (m_eActivePanel == ETDLPanelContent.DIRECT_CHAT)
			{
				SetPanelContent(ETDLPanelContent.MEMBER_DETAIL);
			}
            else if (m_eActivePanel == ETDLPanelContent.SETTINGS)
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
        OnBackClicked();
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
    void OnMemberCardFocused(RplId memberId)
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
    void OnMemberCardClicked(RplId memberId, int button)
    {
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
        if (!member)
            return;
        
        ShowDetailView(member, memberId);
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
    
    //------------------------------------------------------------------------------------------------
    void RefreshPlugins()
    {
        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
            plugin.Disable();
        m_aActivePlugins.Clear();
        
        if (!m_ActiveDevice || !m_ActiveDevice.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
            return;
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller) return;
        
        array<AG0_TDLDeviceComponent> heldDevices = controller.GetHeldDevicesCached();
        
        set<string> supportedPluginIDs = new set<string>();
        foreach (AG0_TDLDeviceComponent device : heldDevices)
        {
            array<string> devicePlugins = device.GetSupportedATAKPlugins();
            if (!devicePlugins) continue;
            
            foreach (string pluginID : devicePlugins)
                supportedPluginIDs.Insert(pluginID);
        }
        
        array<ref AG0_ATAKPluginBase> availablePlugins = m_ActiveDevice.GetAvailablePlugins();
        foreach (AG0_ATAKPluginBase plugin : availablePlugins)
        {
            if (!supportedPluginIDs.Contains(plugin.GetPluginID())) 
                continue;
            
            IEntity sourceDevice = FindSourceDeviceForPlugin(plugin.GetPluginID(), heldDevices);
            plugin.Enable(m_ActiveDevice, sourceDevice);
            m_aActivePlugins.Insert(plugin);
        }
        
        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
            plugin.OnMenuOpened(m_wRoot);
    }
    
    //------------------------------------------------------------------------------------------------
    protected IEntity FindSourceDeviceForPlugin(string pluginID, array<AG0_TDLDeviceComponent> devices)
    {
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            array<string> supported = device.GetSupportedATAKPlugins();
            if (supported && supported.Contains(pluginID))
                return device.GetOwner();
        }
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    // CAMERA BUTTON
    //------------------------------------------------------------------------------------------------
    protected void OnCameraButtonClicked()
    {
        AG0_TDLDeviceComponent cameraDevice = GetLocalCameraDevice();
        if (!cameraDevice)
            return;
        
        bool newState = !cameraDevice.IsCameraBroadcasting();
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (controller)
        {
            RplId deviceRplId = cameraDevice.GetDeviceRplId();
            if (deviceRplId != RplId.Invalid())
                controller.RequestSetCameraBroadcasting(deviceRplId, newState);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected AG0_TDLDeviceComponent GetLocalCameraDevice()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return null;
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
                return device;
        }
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateCameraButtonState()
    {
        if (!m_wCameraButton)
            return;
        
        AG0_TDLDeviceComponent cameraDevice = GetLocalCameraDevice();
        bool hasCamera = cameraDevice != null;
        
        m_wCameraButton.SetVisible(hasCamera);
        
        if (hasCamera)
        {
            bool isBroadcasting = cameraDevice.IsCameraBroadcasting();
            ImageWidget icon = ImageWidget.Cast(m_wCameraButton.FindAnyWidget("CameraImage"));
            if (icon)
            {
                if (isBroadcasting)
                    icon.SetColor(Color.FromRGBA(255, 100, 100, 255));
                else
                    icon.SetColor(Color.FromRGBA(255, 255, 255, 255));
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // REMOTE FEED VIEWING
    //------------------------------------------------------------------------------------------------
    protected void OnViewFeedClickedInternal()
    {
        if (!m_SelectedMember)
            return;
        
        RplId videoSourceId = m_SelectedMember.GetVideoSourceRplId();
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
	    
	    // Spawn camera
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
	    
	    // Try to find the remote device and position camera
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
	                m_PendingFeedPosition = m_SelectedMember.GetPosition();
	                m_fFeedAttachTimer = 0;
	            }
	        }
	        else
	        {
	            m_PendingFeedSourceId = sourceDeviceRplId;
	            m_PendingFeedPosition = m_SelectedMember.GetPosition();
	            m_fFeedAttachTimer = 0;
	        }
	    }
	    else
	    {
	        m_PendingFeedSourceId = sourceDeviceRplId;
	        m_PendingFeedPosition = m_SelectedMember.GetPosition();
	        m_fFeedAttachTimer = 0;
	    }
	    
	    // Activate camera
	    camMgr.SetCamera(feedCamera);
	    m_bViewingRemoteFeed = true;
	    
	    // ========================================
	    // FIX: Hide main menu UI when viewing feed
	    // ========================================
	    HideMainMenuUI();
	    
	    // Show overlay
	    if (m_wFeedOverlay)
	        m_wFeedOverlay.SetVisible(true);
	    
	    if (m_wFeedMemberName && m_SelectedMember)
	        m_wFeedMemberName.SetText(m_SelectedMember.GetPlayerName());
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
	    
	    // Restore original camera
	    CameraManager camMgr = GetGame().GetCameraManager();
	    if (camMgr && m_OriginalCamera)
	        camMgr.SetCamera(m_OriginalCamera);
	    
	    // Delete spawned camera
	    if (m_SpawnedFeedCamera)
	    {
	        SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFeedCamera);
	        m_SpawnedFeedCamera = null;
	    }
	    
	    m_bViewingRemoteFeed = false;
	    m_PendingFeedSourceId = RplId.Invalid();
	    m_AttachedFeedSourceId = RplId.Invalid();
	    m_OriginalCamera = null;
	    
	    // Hide overlay
	    if (m_wFeedOverlay)
	        m_wFeedOverlay.SetVisible(false);
	    
	    // ========================================
	    // FIX: Restore main menu UI after exiting feed view
	    // ========================================
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
	    // Check if source is still in our available sources
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return false;
	    
	    // Check local streaming devices
	    if (!controller.IsVideoSourceAvailable(sourceId))
	        return false;
	    
	    // Optionally verify device is still broadcasting
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
	
	//------------------------------------------------------------------------------------------------
	// CHAT METHODS
	//------------------------------------------------------------------------------------------------
	
	protected void OnViewDirectChatClicked()
	{
	    if (!m_SelectedMember)
	        return;
	    
	    OpenDirectChat(m_SelectedDeviceId, m_SelectedMember.GetPlayerName());
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnChatSendClicked()
	{
	    SendChatMessage();
	}
	
	//------------------------------------------------------------------------------------------------
	void OpenDirectChat(RplId contactRplId, string contactName)
	{
	    m_ChatContactRplId = contactRplId;
	    m_sChatContactName = contactName;
	    s_LastChatContactRplId = contactRplId;
	    s_sLastChatContactName = contactName;
	    SetPanelContent(ETDLPanelContent.DIRECT_CHAT);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void PopulateChatView()
	{
	    if (!m_wChatMessageList)
	        return;
	    
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller || !m_NetworkDevice)
	        return;
	    
	    int networkId = m_NetworkDevice.GetCurrentNetworkID();
	    RplId myDeviceRplId = m_NetworkDevice.GetDeviceRplId();
	    
	    // Get direct messages with this contact
	    array<ref AG0_TDLMessageClient> messages = controller.GetDirectMessages(networkId, myDeviceRplId, m_ChatContactRplId);
	    
	    // Clear existing message widgets
	    ClearChatMessages();
	    
	    // Create widgets for each message
	    foreach (AG0_TDLMessageClient msg : messages)
	    {
	        CreateMessageWidget(msg, myDeviceRplId);
	    }
	    
	    // Mark messages as read
	    foreach (AG0_TDLMessageClient msg : messages)
	    {
	        if (!msg.IsOutgoing(myDeviceRplId) && !controller.IsMessageLocallyRead(msg.messageId))
	        {
	            controller.MarkMessageLocallyRead(msg.messageId);
	            SCR_PlayerController.RequestMarkMessageRead(controller, myDeviceRplId, msg.messageId);
	        }
	    }

	    // Marking-as-read above changed the unread count for the active contact;
	    // refresh badges so the dot clears (or count decrements) without waiting
	    // for the next OnMessagesUpdated tick.
	    UpdateMemberCardBadges();
	    
	    m_bScrollToBottom = true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected void ClearChatMessages()
	{
	    foreach (Widget w : m_aChatMessageWidgets)
	    {
	        if (w)
	            w.RemoveFromHierarchy();
	    }
	    m_aChatMessageWidgets.Clear();
	    // Renderers are ref-counted; clearing the array drops the last ref so they
	    // release their CanvasWidget references. Their canvases are destroyed by the
	    // RemoveFromHierarchy() pass above, so we rely on this ordering — never the
	    // other way around.
	    m_aChatMessageRenderers.Clear();
	    m_aChatMessageDeliveryIds.Clear();
	    m_aChatMessageImageCanvases.Clear();
	}
	
	//------------------------------------------------------------------------------------------------
	protected void CreateMessageWidget(AG0_TDLMessageClient msg, RplId viewerRplId)
	{
	    if (!m_wChatMessageList)
	        return;

	    Widget card = GetGame().GetWorkspace().CreateWidgets(MESSAGE_CARD_LAYOUT, m_wChatMessageList);
	    if (!card)
	        return;

	    m_aChatMessageWidgets.Insert(card);

	    TextWidget playerName = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
	    TextWidget messageContent = TextWidget.Cast(card.FindAnyWidget("MessageContent"));
	    TextWidget timeWidget = TextWidget.Cast(card.FindAnyWidget("Time"));
	    ImageWidget statusDot = ImageWidget.Cast(card.FindAnyWidget("StatusDot"));

	    // Debug logging
	    Print(string.Format("TDL_CHAT: Creating message widget - timestamp: %1", msg.timestamp), LogLevel.DEBUG);
	    Print(string.Format("TDL_CHAT: timeWidget found: %1", timeWidget != null), LogLevel.DEBUG);

	    if (playerName)
	        playerName.SetText(msg.senderCallsign);

	    // Default the message body to the message content (caption for image-messages).
	    // Image-message rendering below may overwrite this with a transferring/failed status string.
	    if (messageContent)
	        messageContent.SetText(msg.content);

	    if (timeWidget)
	    {
	        string formatted = FormatTimestamp(msg.timestamp);
	        Print(string.Format("TDL_CHAT: Setting time to: %1", formatted), LogLevel.DEBUG);
	        timeWidget.SetText(formatted);
	    }

	    bool isOutgoing = msg.IsOutgoing(viewerRplId);
	    if (statusDot)
	    {
	        if (isOutgoing)
	        {
	            switch (msg.status)
	            {
	                case ETDLMessageStatus.PENDING:
	                    statusDot.SetColor(Color.Gray);
	                    break;
	                case ETDLMessageStatus.DELIVERED:
	                    statusDot.SetColor(Color.FromInt(0xFF33CCCC));
	                    break;
	                case ETDLMessageStatus.READ:
	                    statusDot.SetColor(Color.FromInt(0xFF00FF00));
	                    break;
	            }
	        }
	        else
	        {
	            statusDot.SetVisible(false);
	        }
	    }

	    // ----- Image-message setup -----
	    // For TEXT messages: remove ImageOverlay so the card collapses to header+content.
	    // For IMAGE messages: stash the canvas + deliveryId in the parallel arrays. The
	    // actual rendering happens in DriveImageCardRendering on the next OnMenuUpdate
	    // tick (or any subsequent tick once the photo lands in the manager's cache).
	    // Show a placeholder caption in the meantime.
	    string trackedDeliveryId = "";
	    CanvasWidget trackedCanvas = null;

	    if (msg.contentType != ETDLMessageContentType.IMAGE)
	    {
	        Widget imageOverlayKill = card.FindAnyWidget("ImageOverlay");
	        if (imageOverlayKill)
	            imageOverlayKill.RemoveFromHierarchy();
	    }
	    else
	    {
	        Widget imageOverlay = card.FindAnyWidget("ImageOverlay");
	        CanvasWidget imageCanvas = CanvasWidget.Cast(card.FindAnyWidget("ImageCanvas"));

	        if (imageOverlay && imageCanvas)
	        {
	            imageOverlay.SetVisible(true);
	            // Keep the canvas hidden until DriveImageCardRendering successfully renders;
	            // it'll flip back to visible right before scheduling Init+Draw.
	            imageCanvas.SetVisible(false);

	            trackedDeliveryId = msg.imageDeliveryId;
	            trackedCanvas = imageCanvas;

	            // Set placeholder text. If the photo's already cached (sender-side, or a
	            // late chat-open), DriveImageCardRendering will swap the placeholder out
	            // when it draws on the next tick — so the placeholder is only ever visible
	            // for at most one frame in the cached case.
	            string statusText;
	            if (msg.imageTransferState == ETDLImageTransferState.FAILED)
	                statusText = "[image — transfer failed]";
	            else
	                statusText = "[image — incoming…]";

	            if (messageContent)
	            {
	                string combined = msg.content;
	                if (!combined.IsEmpty())
	                    combined = combined + " ";
	                combined = combined + statusText;
	                messageContent.SetText(combined);
	            }
	        }
	    }

	    // Append to all four parallel arrays in lockstep. Renderer starts null;
	    // DriveImageCardRendering will populate it once the photo is available.
	    m_aChatMessageRenderers.Insert(null);
	    m_aChatMessageDeliveryIds.Insert(trackedDeliveryId);
	    m_aChatMessageImageCanvases.Insert(trackedCanvas);
	}

	//------------------------------------------------------------------------------------------------
	//! Polling check: are there image-messages in the visible network's store whose photos
	//! have been cached by the photo manager but whose chat cards aren't currently rendering
	//! them? Counts decoded image-messages and compares to the number of non-null renderers
	//! in m_aChatMessageRenderers — a mismatch means at least one image is ready to draw
	//! but is still showing the "[image — incoming…]" placeholder.
	//!
	//! Used as a fallback path when the OnDecodedPhotoArrived invoker chain doesn't reach
	//! us (observed intermittently in MP). The check is cheap — small store iteration plus
	//! a renderer-array count.
	//------------------------------------------------------------------------------------------------
	//! Walk the per-card parallel arrays. For each image-message card that still has a
	//! null renderer, look up its photo by deliveryId. If the photo is in the manager's
	//! decoded cache, render it into the card's canvas in-place — size the SizeLayout to
	//! the photo's aspect, instantiate the renderer, defer Init+Draw to next frame so the
	//! layout pass settles. Mark the card rendered by storing the renderer in the parallel
	//! array.
	//!
	//! This is the only code path that turns a "[image — incoming…]" placeholder into a
	//! rendered image. It runs every OnMenuUpdate tick and is cheap when nothing's pending
	//! (a few null/empty checks per visible message).
	protected void DriveImageCardRendering()
	{
	    int n = m_aChatMessageWidgets.Count();
	    if (n == 0)
	        return;
	    // Guard the parallel arrays against length skew (shouldn't happen, but defensive).
	    if (m_aChatMessageRenderers.Count() != n) return;
	    if (m_aChatMessageDeliveryIds.Count() != n) return;
	    if (m_aChatMessageImageCanvases.Count() != n) return;

	    // Photo manager lives on the local player controller (NOT on AG0_TDLSystem,
	    // which is server-only and doesn't exist on remote clients). GetActiveInstance
	    // tries the server system first, then falls back to the local PC's per-PC manager.
	    AG0_TDLPhotoManager photoMgr = AG0_TDLPhotoManager.GetActiveInstance();
	    if (!photoMgr) return;

	    for (int i = 0; i < n; i++)
	    {
	        // Skip if already rendered, or not an image-message, or canvas missing.
	        if (m_aChatMessageRenderers[i] != null) continue;

	        string deliveryId = m_aChatMessageDeliveryIds[i];
	        if (deliveryId.IsEmpty()) continue;

	        CanvasWidget canvas = m_aChatMessageImageCanvases[i];
	        if (!canvas) continue;

	        AG0_TDLPhotoData photo = photoMgr.GetDecodedPhoto(deliveryId);
	        if (!photo) continue;  // bytes haven't arrived yet — try again next tick.

	        // Photo is ready. Size the wrapping SizeLayout to the photo's aspect, then
	        // defer Init+Draw to next frame so the layout pass applies before the
	        // renderer queries the canvas's screen size.
	        Widget card = m_aChatMessageWidgets[i];
	        if (card)
	        {
	            SizeLayoutWidget sizeLayout = SizeLayoutWidget.Cast(card.FindAnyWidget("ImageContainer"));
	            if (sizeLayout && photo.m_iWidth > 0 && photo.m_iHeight > 0)
	                SizePhotoCanvas(sizeLayout, photo.m_iWidth, photo.m_iHeight);
	        }

	        canvas.SetVisible(true);

	        AG0_TDLPhotoRenderer renderer = new AG0_TDLPhotoRenderer();
	        m_aChatMessageRenderers[i] = renderer;
	        GetGame().GetCallqueue().CallLater(InitAndDrawPhotoRenderer, 0, false,
	            renderer, canvas, photo);
	    }
	}

	//------------------------------------------------------------------------------------------------
	//! Pin the ImageContainer SizeLayout to a width/height matching the photo's natural
	//! aspect ratio, clamped to layout-friendly bounds. Setting Min == Max on both axes
	//! removes the slack the SizeLayout normally has — the canvas inside (HorizontalAlign
	//! 3 / VerticalAlign 3) fills exactly to those dims, and the renderer's CONTAIN fit
	//! mode is then drawing into a canvas with the right shape so there's no letterboxing.
	//!
	//! Bounds:
	//!   MAX_W: fits inside parent "SizeLayout" (`MaxDesiredWidth 400`) minus content padding
	//!   MAX_H: caps card height so portraits don't blow up the chat list scroll
	//!   MIN_DIM: floor so tiny / extremely wide / extremely tall photos still register
	//!
	//! Extreme aspect ratios (very wide or very tall) fall through the bounds and end up
	//! in a clipped box; the renderer's CONTAIN fit mode letterboxes inside that.
	protected void SizePhotoCanvas(SizeLayoutWidget sizeLayout, int photoW, int photoH)
	{
	    const float MAX_W = 384.0;
	    const float MAX_H = 384.0;
	    const float MIN_DIM = 96.0;

	    float pw = photoW;
	    float ph = photoH;
	    float aspect = pw / ph;

	    // Start by maximizing width; if that overshoots the height cap, switch to height-driven.
	    float targetW = MAX_W;
	    float targetH = targetW / aspect;
	    if (targetH > MAX_H)
	    {
	        targetH = MAX_H;
	        targetW = targetH * aspect;
	    }
	    // Independent floor on both axes — extreme aspects degrade into letterboxing rather
	    // than producing a 16x600 sliver of canvas.
	    if (targetW < MIN_DIM) targetW = MIN_DIM;
	    if (targetH < MIN_DIM) targetH = MIN_DIM;

	    sizeLayout.SetMinDesiredWidth(targetW);
	    sizeLayout.SetMaxDesiredWidth(targetW);
	    sizeLayout.SetMinDesiredHeight(targetH);
	    sizeLayout.SetMaxDesiredHeight(targetH);
	}

	//------------------------------------------------------------------------------------------------
	//! Run after the one-frame defer from CreateMessageWidget so the canvas has its final
	//! dimensions from the layout pass following SizePhotoCanvas's SetMin/MaxDesired* calls.
	//! AG0_TDLPhotoRenderer.Init reads GetSizeInUnits() inside the call — without the defer
	//! it would read pre-layout (often zero) dims and the rendered image would only
	//! occupy the corner of the canvas.
	//!
	//! If the message-card widget has been removed from the hierarchy in the interval
	//! (ClearChatMessages ran while we were waiting), the canvas may be invalid;
	//! Init returns false on null/destroyed widgets and we bail without drawing.
	protected void InitAndDrawPhotoRenderer(AG0_TDLPhotoRenderer renderer, CanvasWidget canvas, AG0_TDLPhotoData photo)
	{
	    if (!renderer || !canvas || !photo)
	        return;
	    if (!renderer.Init(canvas))
	        return;
	    renderer.SetPhotoData(photo);
	    renderer.Draw();
	}

	//------------------------------------------------------------------------------------------------
	protected string FormatTimestamp(int timestamp)
	{
	    int now = System.GetUnixTime();
	    int diff = now - timestamp;
	    
	    if (diff < 60)
	        return "Just now";
	    else if (diff < 3600)
	        return string.Format("%1m ago", diff / 60);
	    else if (diff < 86400)
	        return string.Format("%1h ago", diff / 3600);
	    else
	        return string.Format("%1d ago", diff / 86400);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void SendChatMessage()
	{
	    if (!m_ChatEditBox || !m_NetworkDevice)
	        return;

	    string content = m_ChatEditBox.GetText();
	    if (content.IsEmpty())
	        return;

	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return;

	    RplId senderDeviceRplId = m_NetworkDevice.GetDeviceRplId();

	    if (m_ChatContactRplId != RplId.Invalid())
	        SCR_PlayerController.RequestSendDirectMessage(controller, senderDeviceRplId, content, m_ChatContactRplId);

	    m_ChatEditBox.SetText("");
	    m_bScrollToBottom = true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected void SubscribeToMessageUpdates()
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return;

	    controller.GetOnMessagesUpdated().Insert(OnMessagesUpdated);
	    controller.GetOnNewMessageReceived().Insert(OnNewMessageReceived);

	    // Image-message refresh: when a chunked image transfer completes (or fails) for
	    // any deliveryId, re-render the chat view if it's the visible panel. The photo
	    // manager fires these once per delivery; cheap to handle redundantly.
	    // Image-message rendering doesn't subscribe to the photo manager invokers.
	    // DriveImageCardRendering (called every OnMenuUpdate tick) is the deterministic
	    // path. Invokers were unreliable across the MP replication boundary.
	}

	//------------------------------------------------------------------------------------------------
	protected void UnsubscribeFromMessageUpdates()
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return;

	    controller.GetOnMessagesUpdated().Remove(OnMessagesUpdated);
	    controller.GetOnNewMessageReceived().Remove(OnNewMessageReceived);

	    // Image-message rendering is driven by DriveImageCardRendering, not by photo-manager
	    // invokers. Nothing to unsubscribe on the photo-manager side.
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMessagesUpdated(int networkId)
	{
	    if (m_eActivePanel == ETDLPanelContent.DIRECT_CHAT)
	    {
	        if (m_NetworkDevice && m_NetworkDevice.GetCurrentNetworkID() == networkId)
	            PopulateChatView();
	    }
	    // Badges refresh regardless of active panel — the contact list is visible on
	    // the network/member panels, and we want unread counters to reflect new
	    // arrivals even when the user isn't in chat view.
	    UpdateMemberCardBadges();
	}

	//------------------------------------------------------------------------------------------------
	//! Update each member-card's notification dot + count based on per-contact unread
	//! direct messages. Called on initial card attach, when new messages arrive, and
	//! after PopulateChatView marks the active conversation read.
	//!
	//! Pulls cards from m_DisplayController (which owns the lifecycle — cards are
	//! created/destroyed as the network membership changes) and reads unread counts
	//! from SCR_PlayerController.GetDirectChatUnreadCount. Per-contact tracking lives
	//! in m_LocallyReadMessages on the controller; this just renders that state.
	protected void UpdateMemberCardBadges()
	{
	    if (!m_DisplayController || !m_NetworkDevice)
	        return;

	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return;

	    int networkId = m_NetworkDevice.GetCurrentNetworkID();
	    RplId myDeviceRplId = m_NetworkDevice.GetDeviceRplId();

	    array<Widget> cards = m_DisplayController.GetMemberCards();
	    array<RplId> cardIds = m_DisplayController.GetMemberCardIds();
	    if (!cards || !cardIds)
	        return;

	    for (int i = 0; i < cards.Count(); i++)
	    {
	        Widget card = cards[i];
	        if (!card || i >= cardIds.Count())
	            continue;

	        RplId contactId = cardIds[i];
	        int unread = controller.GetDirectChatUnreadCount(networkId, myDeviceRplId, contactId);

	        Widget notifDot = card.FindAnyWidget("NotificationDot");
	        if (!notifDot)
	            continue;

	        if (unread <= 0)
	        {
	            notifDot.SetVisible(false);
	            continue;
	        }

	        notifDot.SetVisible(true);
	        // RichTextWidget extends TextWidget — TextWidget.Cast still resolves the layout's
	        // RichTextWidgetClass entry. SetText accepts the count formatted as a string.
	        TextWidget numText = TextWidget.Cast(card.FindAnyWidget("NotificationNumberText"));
	        if (numText)
	            numText.SetText(unread.ToString());
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnNewMessageReceived(int networkId, int messageId)
	{
	    if (m_eActivePanel == ETDLPanelContent.DIRECT_CHAT)
	        m_bScrollToBottom = true;
	}
}