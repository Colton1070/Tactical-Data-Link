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
    SETTINGS        // Future: settings/config
}

//------------------------------------------------------------------------------------------------
//! TDL ATAK-style interface menu - Map-centric with overlay panels
//! Uses AG0_TDLDisplayController for display, handles interactions
class AG0_TDLMenuUI : ChimeraMenuBase
{
    // Persistent state for menu-specific things (panel state survives open/close)
    static protected ETDLPanelContent s_eLastPanel = ETDLPanelContent.NETWORK_LIST;
    
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
    protected const float STICK_PAN_SPEED = 400.0;
    protected const float STICK_DEADZONE = 0.15;
    
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
        
        // Get active devices
        FindActiveDevice();
        FindNetworkDevice();
        RefreshPlugins();
        
        // Restore or set initial panel state
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
        
        // Process gamepad right stick pan input
        if (m_DisplayController && m_InputManager)
        {
            float panX = m_InputManager.GetActionValue("TDLPanHorizontal");
            float panY = m_InputManager.GetActionValue("TDLPanVertical");
            
            if (Math.AbsFloat(panX) > STICK_DEADZONE || Math.AbsFloat(panY) > STICK_DEADZONE)
            {
                float deltaX = -panX * STICK_PAN_SPEED * tDelta;
                float deltaY = panY * STICK_PAN_SPEED * tDelta;
                
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
    
    //------------------------------------------------------------------------------------------------
    // PANEL MANAGEMENT
    //------------------------------------------------------------------------------------------------
    protected void SetPanelContent(ETDLPanelContent content)
    {
        m_eActivePanel = content;
        
        bool showPanel = (content != ETDLPanelContent.NONE);
        bool showNetwork = (content == ETDLPanelContent.NETWORK_LIST);
        bool showDetail = (content == ETDLPanelContent.MEMBER_DETAIL);
        bool showSettings = (content == ETDLPanelContent.SETTINGS);
        
        string title = "CONTACTS";
        switch (content)
        {
            case ETDLPanelContent.MEMBER_DETAIL:
                title = "CONTACT DETAILS";
                break;
            case ETDLPanelContent.SETTINGS:
                title = "SETTINGS";
                break;
        }
        
        // Update static state so device stays in sync
        AG0_TDLDisplayController.SetPanelState(showPanel, showNetwork, showDetail, showSettings, title);
        
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
        
        if (m_wPanelTitle)
            m_wPanelTitle.SetText(title);
        
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
}