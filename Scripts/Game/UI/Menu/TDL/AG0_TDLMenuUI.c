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
class AG0_TDLMenuUI : ChimeraMenuBase
{
    // Persistent state (survives menu open/close within session)
    static protected ETDLPanelContent s_eLastPanel = ETDLPanelContent.NETWORK_LIST;
    static protected float s_fLastZoom = 0.15;
    static protected vector s_vLastCenter;
    static protected bool s_bLastPlayerTracking = true;
    static protected bool s_bLastTrackUp = true;
    static protected bool s_bHasSavedState = false;
    
    // Panel state
	// Active plugins (menu owns lifecycle)
	protected ref array<ref AG0_ATAKPluginBase> m_aActivePlugins = {};
    protected ETDLPanelContent m_eActivePanel = ETDLPanelContent.NETWORK_LIST;
	protected ref AG0_TDLMapCanvasDragHandler m_DragHandler;
	protected bool m_bPlayerTracking = true;
    
    // Core references
    protected AG0_TDLDeviceComponent m_ActiveDevice;      // The ATAK/EUD display device
    protected AG0_TDLDeviceComponent m_NetworkDevice;     // The radio providing network data
    protected Widget m_wRoot;
    protected InputManager m_InputManager;
    
    // Map view - always active
    protected ref AG0_TDLMapView m_MapView;
    protected CanvasWidget m_wMapCanvas;
    
    // Side panel structure (sibling of MainPanel in HorizontalLayout)
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wNetworkContent;
    protected Widget m_wDetailContent;
    
    // Network content widgets
    protected Widget m_wScrollContainer;
    protected ScrollLayoutWidget m_wScrollLayout;
    protected Widget m_wMemberList;
    protected TextWidget m_wDeviceName;
    protected TextWidget m_wNetworkStatus;
    
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
    protected Widget m_wSettingsContent;
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
    
    // Zoom/compass controls
    protected Widget m_wZoomInButton;
    protected Widget m_wZoomOutButton;
    protected Widget m_wCompassButton;
    protected Widget m_wTrackButton;
    protected ImageWidget m_wHeadingIndicator;
    protected bool m_bTrackUp = true;  // Default to track-up mode
    
    // Self marker widget (info panel)
	protected Widget m_wSelfMarkerWidget;
	protected TextWidget m_wGPSStatus;
	protected TextWidget m_wCallsign;
	protected TextWidget m_wGrid;
	protected TextWidget m_wAltitude;
	protected TextWidget m_wHeading;
	protected TextWidget m_wSpeed;
	protected TextWidget m_wError;
    
    // Self panel colors
    protected ref Color COLOR_CYAN = new Color(0.2, 0.8, 0.8, 1.0);    // Cyan for normal text
    protected ref Color COLOR_RED = new Color(1.0, 0.2, 0.2, 1.0);      // Red for NO GPS
    
    // Map marker overlay system (widget-based, clickable)
    protected Widget m_wMarkerOverlay;                                      // Parent frame for all markers
    protected Widget m_wSelfMapMarker;                                      // Player's position marker on map
    protected ref map<RplId, Widget> m_mMemberMarkers = new map<RplId, Widget>();  // Network member markers
    protected const float MARKER_SIZE = 64.0;                               // Marker widget size in pixels
    protected bool m_bMarkerFocused = false;                                // True when gamepad focus is on a map marker
    
    // Selected member for detail view
    protected ref AG0_TDLNetworkMember m_SelectedMember;
    protected RplId m_SelectedDeviceId;
    
    // State tracking
    protected ref array<RplId> m_aCachedMemberIds = {};
    protected ref array<Widget> m_aMemberCards = {};
    protected float m_fUpdateTimer = 0;
    protected const float UPDATE_INTERVAL = 0.5;
    protected int m_iFocusedCardIndex = -1;
    
    // Gamepad map pan settings
    protected const float STICK_PAN_SPEED = 400.0;      // Pixels per second at full deflection
    protected const float STICK_DEADZONE = 0.15;        // Ignore small stick movements
    
    // Layout paths
    protected const ResourceName MEMBER_CARD_LAYOUT = "{7C025C99261C96C5}UI/layouts/Menus/TDL/TDLMemberCardUI.layout";
    protected const ResourceName SELF_MARKER_LAYOUT = "{A242BD2B06D27E00}UI/layouts/Menus/TDL/TDLMenuSelfMarker.layout";
    protected const ResourceName MEMBER_MARKER_LAYOUT = "{23872C52B88FDB59}UI/layouts/Menus/TDL/TDLMenuBuddyMarker.layout";
	
	// Remote feed viewing
	protected bool m_bViewingRemoteFeed = false;
	protected IEntity m_SpawnedFeedCamera;
	protected CameraBase m_OriginalCamera;
	protected Widget m_wFeedOverlay;
	protected Widget m_wFeedBackButton;
	protected TextWidget m_wFeedMemberName;
	
	// Pending feed attachment (for streaming delay)
	protected RplId m_PendingFeedSourceId;
	protected RplId m_AttachedFeedSourceId;  // Track which device we're actually attached to
	protected vector m_PendingFeedPosition;
	protected float m_fFeedAttachTimer;
	protected const float FEED_ATTACH_TIMEOUT = 5.0;
	
	protected const ResourceName FEED_CAMERA_PREFAB = "{F3CDC6E4F329E496}Prefabs/Characters/Core/TDLDevicePlayerCamera.et";
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen()
	{
	    m_wRoot = GetRootWidget();
	    m_InputManager = GetGame().GetInputManager();
	    
	    // Map canvas (inside MainPanel > MainView)
	    m_wMapCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("MapCanvas"));
	    
	    Widget dragSurface = m_wRoot.FindAnyWidget("MapDragSurface");
		if (dragSurface)
		{
		    // Prevent gamepad navigation from focusing this widget
		    dragSurface.SetFlags(WidgetFlags.NOFOCUS);
		    
		    m_DragHandler = new AG0_TDLMapCanvasDragHandler();
		    dragSurface.AddHandler(m_DragHandler);
		    m_DragHandler.m_OnDragStart.Insert(OnMapDragStart);
		}
	    
	    // Side panel structure (now sibling of MainPanel)
	    m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
	    m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
	    m_wNetworkContent = m_wRoot.FindAnyWidget("NetworkContent");
	    m_wDetailContent = m_wRoot.FindAnyWidget("DetailContent");
	    
	    // Network content widgets
	    m_wScrollContainer = m_wRoot.FindAnyWidget("ScrollLayout");
	    m_wScrollLayout = ScrollLayoutWidget.Cast(m_wScrollContainer);
	    m_wMemberList = m_wRoot.FindAnyWidget("MemberList");
	    m_wDeviceName = TextWidget.Cast(m_wRoot.FindAnyWidget("DeviceName"));
	    m_wNetworkStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("NetworkStatus"));
	    
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
	    
	    // Settings content widgets
	    m_wSettingsContent = m_wRoot.FindAnyWidget("SettingsContent");
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
	    m_wHeadingIndicator = ImageWidget.Cast(m_wRoot.FindAnyWidget("HeadingIndicator"));
	    
	    // Self marker widget (info panel in StatusPanels)
	    m_wSelfMarkerWidget = m_wRoot.FindAnyWidget("SelfMarkerWidget");
	    m_wGPSStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("GPSStatus"));
	    m_wCallsign = TextWidget.Cast(m_wRoot.FindAnyWidget("Callsign"));
	    m_wGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("Grid"));
	    m_wAltitude = TextWidget.Cast(m_wRoot.FindAnyWidget("Altitude"));
	    m_wHeading = TextWidget.Cast(m_wRoot.FindAnyWidget("Heading"));
	    m_wSpeed = TextWidget.Cast(m_wRoot.FindAnyWidget("Speed"));
	    m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("Error"));
		
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
	    
	    // Apply initial cyan colors to self panel text widgets
	    ApplySelfPanelColors();
	    
	    // Initialize map view
	    if (m_wMapCanvas)
	    {
	        m_MapView = new AG0_TDLMapView();
	        m_MapView.Init(m_wMapCanvas);
	        
	        // Restore state from previous session or use defaults
	        if (s_bHasSavedState)
	        {
	            m_MapView.SetCenter(s_vLastCenter);
	            m_MapView.SetZoom(s_fLastZoom);
	            m_bPlayerTracking = s_bLastPlayerTracking;
	            m_bTrackUp = s_bLastTrackUp;
	        }
	        else
	        {
	            m_MapView.CenterOnPlayer();
	            m_MapView.SetZoom(0.15);
	        }
	    }
	    
	    // Setup marker overlay (sibling to MapCanvas, sits on top)
	    m_wMarkerOverlay = m_wRoot.FindAnyWidget("MarkerOverlay");
	    if (m_wMarkerOverlay)
	    {
	        // Create self marker widget
	        m_wSelfMapMarker = GetGame().GetWorkspace().CreateWidgets(SELF_MARKER_LAYOUT, m_wMarkerOverlay);
	    }
	    
	    // Get active TDL device
	    FindActiveDevice();
	    
	    // Find the network device (radio) for network data
	    FindNetworkDevice();
		
		RefreshPlugins();
	    
	    // Restore or set initial panel state
	    if (s_bHasSavedState)
	        SetPanelContent(s_eLastPanel);
	    else
	        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
		
		HookButtonHandlers();
	}
	
	//------------------------------------------------------------------------------------------------
	//! Apply cyan color to all self panel text widgets
	protected void ApplySelfPanelColors()
	{
	    // Set cyan for all text except GPSStatus (handled separately in UpdateSelfMarker)
	    if (m_wCallsign) m_wCallsign.SetColor(COLOR_CYAN);
	    if (m_wGrid) m_wGrid.SetColor(COLOR_CYAN);
	    if (m_wAltitude) m_wAltitude.SetColor(COLOR_CYAN);
	    if (m_wHeading) m_wHeading.SetColor(COLOR_CYAN);
	    if (m_wSpeed) m_wSpeed.SetColor(COLOR_CYAN);
	    if (m_wError) m_wError.SetColor(COLOR_CYAN);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Format grid reference with proper zero-padding
	protected string FormatGridReference(vector pos, int numDigits = 5)
	{
	    return AG0_MGRSGridUtils.GetFullMGRS(pos, numDigits);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Check if player has any GPS-providing device
	protected bool HasGPSProvider()
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return false;
	    
	    array<AG0_TDLDeviceComponent> heldDevices = controller.GetPlayerTDLDevices();
	    foreach (AG0_TDLDeviceComponent device : heldDevices)
	    {
	        if (device && device.HasCapability(AG0_ETDLDeviceCapability.GPS_PROVIDER))
	            return true;
	    }
	    
	    return false;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get network member data directly from the player controller
	//! This allows the ATAK (display-only device) to show data from the player's radio
	protected AG0_TDLNetworkMembers GetNetworkMembersFromController()
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return null;
	    
	    return controller.GetAggregatedTDLMembers();
	}
	
	//------------------------------------------------------------------------------------------------
	//! Check if player has any network member data available (via any networked device)
	protected bool HasNetworkMemberData()
	{
	    AG0_TDLNetworkMembers data = GetNetworkMembersFromController();
	    return data && data.Count() > 0;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get a specific network member by RplId from the player controller
	protected AG0_TDLNetworkMember GetNetworkMemberById(RplId rplId)
	{
	    AG0_TDLNetworkMembers data = GetNetworkMembersFromController();
	    if (!data)
	        return null;
	    
	    return data.GetByRplId(rplId);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Find the player's network device (radio) that provides network data
	protected void FindNetworkDevice()
	{
	    m_NetworkDevice = null;
	    
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return;
	    
	    array<AG0_TDLDeviceComponent> devices = controller.GetPlayerTDLDevices();
	    foreach (AG0_TDLDeviceComponent device : devices)
	    {
	        // Look for a device with NETWORK_ACCESS that's in a network
	        if (device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS) && device.IsInNetwork())
	        {
	            m_NetworkDevice = device;
	            return;
	        }
	    }
	    
	    // If no device is in a network, just find one with NETWORK_ACCESS capability
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
	//! Get all RplIds of devices owned by the player (for filtering self from buddy markers)
	protected set<RplId> GetPlayerOwnDeviceIds()
	{
	    set<RplId> deviceIds = new set<RplId>();
	    
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return deviceIds;
	    
	    array<AG0_TDLDeviceComponent> devices = controller.GetPlayerTDLDevices();
	    foreach (AG0_TDLDeviceComponent device : devices)
	    {
	        RplId deviceId = device.GetDeviceRplId();
	        if (deviceId != RplId.Invalid())
	            deviceIds.Insert(deviceId);
	    }
	    
	    return deviceIds;
	}
	
	void RefreshPlugins()
	{
	    // Disable previous plugins
	    foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
	        plugin.Disable();
	    m_aActivePlugins.Clear();
	    
	    if (!m_ActiveDevice || !m_ActiveDevice.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
	        return;
	    
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller) return;
	    
	    array<AG0_TDLDeviceComponent> heldDevices = controller.GetPlayerTDLDevices();
	    
	    // Collect supported plugin IDs from all held devices
	    set<string> supportedPluginIDs = new set<string>();
	    foreach (AG0_TDLDeviceComponent device : heldDevices)
	    {
	        array<string> devicePlugins = device.GetSupportedATAKPlugins();
	        if (!devicePlugins) continue;
	        
	        foreach (string pluginID : devicePlugins)
	            supportedPluginIDs.Insert(pluginID);
	    }
	    
	    // Get available plugin configs from ATAK device, enable matching ones
	    array<ref AG0_ATAKPluginBase> availablePlugins = m_ActiveDevice.GetAvailablePlugins();
	    foreach (AG0_ATAKPluginBase plugin : availablePlugins)
	    {
	        if (!supportedPluginIDs.Contains(plugin.GetPluginID())) 
	            continue;
	        
	        // Find source device for this plugin
	        IEntity sourceDevice = FindSourceDeviceForPlugin(plugin.GetPluginID(), heldDevices);
	        plugin.Enable(m_ActiveDevice, sourceDevice);
	        m_aActivePlugins.Insert(plugin);
	    }
	    
	    // Notify plugins menu is open
	    foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
	        plugin.OnMenuOpened(m_wRoot);
	}
	
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
    protected void InitMapView()
    {
        if (!m_wMapCanvas)
        {
            Print("[TDLMenu] MapCanvas not found", LogLevel.ERROR);
            return;
        }
        
        m_MapView = new AG0_TDLMapView();
        if (!m_MapView.Init(m_wMapCanvas))
        {
            Print("[TDLMenu] Failed to initialize map view", LogLevel.ERROR);
            return;
        }
        
        // Center on player, default zoom
        m_MapView.CenterOnPlayer();
        m_MapView.SetZoom(0.15);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void HookButtonHandlers()
    {
        // Back button (detail -> network)
        if (m_wBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wBackButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnBackClicked);
        }
        
        // Network toggle button
        if (m_wNetworkButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wNetworkButton.FindHandler(SCR_ModularButtonComponent)
            );
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
        
        // View feed button
        if (m_wViewFeedButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wViewFeedButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnViewFeedClickedInternal);
        }
        
        // View location button (go to member position)
        if (m_wViewLocationButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wViewLocationButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnViewLocationClickedInternal);
        }
        
        // Zoom controls
        if (m_wZoomInButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wZoomInButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnZoomInClickedInternal);
        }
        
        if (m_wZoomOutButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wZoomOutButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnZoomOutClickedInternal);
        }
        
        // Compass toggle
        if (m_wCompassButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCompassButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnCompassClickedInternal);
        }
        
        // Track player toggle
        if (m_wTrackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wTrackButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnTrackClickedInternal);
        }
        
        // Settings button (in network panel header)
        if (m_wSettingsButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsButtonClicked);
        }
        
        // Callsign save button
        if (m_wCallsignSaveButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCallsignSaveButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnCallsignSaveClicked);
        }
        
        // Settings back button
        if (m_wSettingsBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsBackButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsBackClicked);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // PANEL MANAGEMENT
    //------------------------------------------------------------------------------------------------
    protected void SetPanelContent(ETDLPanelContent content)
    {
        m_eActivePanel = content;
        
        // Panel visibility - showing/hiding affects MainPanel size due to HorizontalLayout
        bool showPanel = (content != ETDLPanelContent.NONE);
        if (m_wSidePanel)
            m_wSidePanel.SetVisible(showPanel);
        
        if (!showPanel)
            return;
        
        // Content visibility within panel
        if (m_wNetworkContent)
            m_wNetworkContent.SetVisible(content == ETDLPanelContent.NETWORK_LIST);
        
        if (m_wDetailContent)
            m_wDetailContent.SetVisible(content == ETDLPanelContent.MEMBER_DETAIL);
        
        if (m_wSettingsContent)
            m_wSettingsContent.SetVisible(content == ETDLPanelContent.SETTINGS);
        
        // Update panel title
        if (m_wPanelTitle)
        {
            switch (content)
            {
                case ETDLPanelContent.NETWORK_LIST:
                    m_wPanelTitle.SetText("CONTACTS");
                    break;
                case ETDLPanelContent.MEMBER_DETAIL:
                    m_wPanelTitle.SetText("CONTACT DETAILS");
                    break;
                case ETDLPanelContent.SETTINGS:
                    m_wPanelTitle.SetText("SETTINGS");
                    break;
            }
        }
        
        // Set focus appropriately
        SetPanelFocus(content);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void SetPanelFocus(ETDLPanelContent content)
    {
        switch (content)
        {
            case ETDLPanelContent.NETWORK_LIST:
                if (!m_aMemberCards.IsEmpty())
                    SetFocusToCard(Math.Max(0, m_iFocusedCardIndex));
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
                // Focus could go to a map control or toolbar button
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
	    // Re-fetch member to get current position data
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
	    
	    // Grid coordinates using proper military grid format
	    if (m_wDetailGrid)
	    {
	        vector memberPos = m_SelectedMember.GetPosition();
	        string gridLabel = FormatGridReference(memberPos, 5);
	        m_wDetailGrid.SetText(gridLabel);
	    }
	    
	    // Calculate distance
	    if (m_wDetailDistance)
	    {
	        IEntity player = GetGame().GetPlayerController().GetControlledEntity();
	        if (player)
	        {
	            float dist = vector.Distance(player.GetOrigin(), m_SelectedMember.GetPosition());
	            m_wDetailDistance.SetTextFormat("%1 m", Math.Round(dist).ToString());
	        }
	    }
	    
	    // Capabilities
	    if (m_wDetailCapabilities)
	    {
	        string caps = BuildCapabilitiesString(m_SelectedMember.GetCapabilities());
	        m_wDetailCapabilities.SetText(caps);
	    }
	    
	    // Show/hide video button based on actual broadcasting status (not just capability)
	    if (m_wViewFeedButton)
	    {
	        // Check if member is actually broadcasting, not just if they have the capability
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
    // BUTTON HANDLERS (internal - protected)
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
	protected void OnCameraButtonClicked()
	{
	    AG0_TDLDeviceComponent cameraDevice = GetLocalCameraDevice();
	    if (!cameraDevice)
	    {
	        Print("[TDL Menu] No camera device found", LogLevel.DEBUG);
	        return;
	    }
	    
	    bool newState = !cameraDevice.IsCameraBroadcasting();
	    
	    // Use RPC through PlayerController for proper MP support
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (controller)
	    {
	        RplId deviceRplId = cameraDevice.GetDeviceRplId();
	        if (deviceRplId != RplId.Invalid())
	        {
	            controller.RequestSetCameraBroadcasting(deviceRplId, newState);
	            Print(string.Format("[TDL Menu] Requested camera broadcast: %1 for device %2", newState, deviceRplId), LogLevel.DEBUG);
	        }
	        else
	        {
	            Print("[TDL Menu] Camera device has invalid RplId", LogLevel.WARNING);
	        }
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    protected void OnViewLocationClickedInternal()
    {
        if (!m_SelectedMember || !m_MapView)
            return;
        
        m_MapView.SetCenter(m_SelectedMember.GetPosition());
        m_bPlayerTracking = false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnZoomInClickedInternal()
    {
        if (m_MapView)
            m_MapView.ZoomIn(0.05);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnZoomOutClickedInternal()
    {
        if (m_MapView)
            m_MapView.ZoomOut(0.05);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnCompassClickedInternal()
    {
        m_bTrackUp = !m_bTrackUp;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnTrackClickedInternal()
    {
        m_bPlayerTracking = !m_bPlayerTracking;
        
        // If re-enabling tracking, immediately center on player
        if (m_bPlayerTracking && m_MapView)
            m_MapView.CenterOnPlayer();
    }
	
	//------------------------------------------------------------------------------------------------
	// IMPROVED: Try instant attachment first, fallback to player position only if device not streamed
	//------------------------------------------------------------------------------------------------
	protected void OnViewFeedClickedInternal()
	{
	    if (!m_SelectedMember)
	    {
	        Print("[TDL Feed] No selected member", LogLevel.DEBUG);
	        return;
	    }
	    
	    RplId sourceId = m_SelectedMember.GetVideoSourceRplId();
	    if (sourceId == RplId.Invalid())
	    {
	        Print("[TDL Feed] Selected member has no video source", LogLevel.DEBUG);
	        return;
	    }
	    
	    // Try to find the device immediately - this is the preferred path
	    RplComponent rpl = RplComponent.Cast(Replication.FindItem(sourceId));
	    if (rpl)
	    {
	        IEntity remoteEntity = rpl.GetEntity();
	        if (remoteEntity)
	        {
	            AG0_TDLDeviceComponent remoteDevice = AG0_TDLDeviceComponent.Cast(
	                remoteEntity.FindComponent(AG0_TDLDeviceComponent));
	            
	            if (remoteDevice && remoteDevice.m_CameraAttachment)
	            {
	                // Device is already streamed in - attach instantly
	                Print("[TDL Feed] Device already streamed, attaching instantly", LogLevel.DEBUG);
	                EnterRemoteFeedViewInstant(sourceId, remoteEntity, remoteDevice);
	                return;
	            }
	        }
	    }
	    
	    // Device not streamed in yet - spawn at player position and wait
	    Print("[TDL Feed] Device not streamed, spawning at player position", LogLevel.DEBUG);
	    EnterRemoteFeedViewDeferred(sourceId, m_SelectedMember.GetPosition());
	}
	
	protected void UpdateCameraButtonState()
	{
	    if (!m_wCameraButton)
	        return;
	    
	    AG0_TDLDeviceComponent cameraDevice = GetLocalCameraDevice();
	    bool broadcasting = cameraDevice && cameraDevice.IsCameraBroadcasting();
	    
	    // Update button text or icon based on state
	    ImageWidget cameraIcon = ImageWidget.Cast(m_wCameraButton.FindAnyWidget("CameraImage"));
	    if (cameraIcon) {
			if(broadcasting) {
				cameraIcon.SetColor(Color.Red);
			}
			else {
				cameraIcon.SetColor(Color.White);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	protected AG0_TDLDeviceComponent GetLocalCameraDevice()
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!controller)
	        return null;
	    
	    foreach (AG0_TDLDeviceComponent device : controller.GetPlayerTDLDevices())
	    {
	        if (device.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE) && device.IsPowered())
	            return device;
	    }
	    return null;
	}
	
	//------------------------------------------------------------------------------------------------
	// INSTANT ATTACHMENT: Device is already available, position camera at exact attachment point
	//------------------------------------------------------------------------------------------------
	protected void EnterRemoteFeedViewInstant(RplId sourceId, IEntity remoteEntity, AG0_TDLDeviceComponent remoteDevice)
	{
	    if (System.IsConsoleApp())
	        return;
	    
	    // Store original camera
	    m_OriginalCamera = GetGame().GetCameraManager().CurrentCamera();
	    
	    // Calculate camera world transform from device's attachment point
	    vector cameraTransform[4];
	    GetCameraTransformFromDevice(remoteEntity, remoteDevice, cameraTransform);
	    
	    // Spawn camera at the exact attachment position
	    EntitySpawnParams params = new EntitySpawnParams();
	    params.TransformMode = ETransformMode.WORLD;
	    Math3D.MatrixCopy(cameraTransform, params.Transform);
	    
	    m_SpawnedFeedCamera = GetGame().SpawnEntityPrefab(
	        Resource.Load(FEED_CAMERA_PREFAB), null, params);
	    
	    if (!m_SpawnedFeedCamera)
	    {
	        Print("[TDL Feed] Failed to spawn camera entity", LogLevel.ERROR);
	        return;
	    }
	    
	    CameraBase feedCam = CameraBase.Cast(m_SpawnedFeedCamera);
	    if (!feedCam)
	    {
	        SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFeedCamera);
	        m_SpawnedFeedCamera = null;
	        Print("[TDL Feed] Spawned entity is not a camera", LogLevel.ERROR);
	        return;
	    }
	    
	    // Attach camera to follow the device
	    AttachCameraToDevice(remoteEntity, remoteDevice);
	    
	    // Switch to our feed camera
	    GetGame().GetCameraManager().SetCamera(feedCam);
	    
	    m_bViewingRemoteFeed = true;
	    m_PendingFeedSourceId = RplId.Invalid(); // No pending - already attached
	    m_AttachedFeedSourceId = sourceId; // Track what we're attached to
	    
	    SetFeedViewMode(true);
	    Print(string.Format("[TDL Feed] Instant attachment to device %1 successful", sourceId), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// DEFERRED ATTACHMENT: Device not streamed, position at player and poll for attachment
	//------------------------------------------------------------------------------------------------
	protected void EnterRemoteFeedViewDeferred(RplId sourceId, vector playerPosition)
	{
	    if (System.IsConsoleApp())
	        return;
	    
	    m_OriginalCamera = GetGame().GetCameraManager().CurrentCamera();
	    
	    // Spawn camera at the player's position (device location approximation)
	    // Add some height offset to avoid spawning in ground
	    vector spawnPos = playerPosition;
	    spawnPos[1] = spawnPos[1] + 1.5; // Eye level offset
	    
	    EntitySpawnParams params = new EntitySpawnParams();
	    params.Transform[3] = spawnPos;
	    
	    m_SpawnedFeedCamera = GetGame().SpawnEntityPrefab(
	        Resource.Load(FEED_CAMERA_PREFAB), null, params);
	    
	    if (!m_SpawnedFeedCamera)
	    {
	        Print("[TDL Feed] Failed to spawn camera entity", LogLevel.ERROR);
	        return;
	    }
	    
	    CameraBase feedCam = CameraBase.Cast(m_SpawnedFeedCamera);
	    if (!feedCam)
	    {
	        SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFeedCamera);
	        m_SpawnedFeedCamera = null;
	        Print("[TDL Feed] Spawned entity is not a camera", LogLevel.ERROR);
	        return;
	    }
	    
	    // Switch camera immediately - this triggers streaming for that area
	    GetGame().GetCameraManager().SetCamera(feedCam);
	    m_bViewingRemoteFeed = true;
	    
	    // Store pending attachment info for polling
	    m_PendingFeedSourceId = sourceId;
	    m_PendingFeedPosition = playerPosition;
	    m_fFeedAttachTimer = 0;
	    m_AttachedFeedSourceId = RplId.Invalid();
	    
	    SetFeedViewMode(true);
	    Print(string.Format("[TDL Feed] Deferred view started, waiting for device %1 to stream in", sourceId), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// HELPER: Calculate camera world transform from device attachment point
	//------------------------------------------------------------------------------------------------
	protected void GetCameraTransformFromDevice(IEntity deviceEntity, AG0_TDLDeviceComponent device, out vector outTransform[4])
	{
	    vector entityTransform[4], localOffset[4];
	    
	    // Get device's world transform
	    deviceEntity.GetWorldTransform(entityTransform);
	    
	    // Get camera attachment local offset
	    device.m_CameraAttachment.GetLocalTransform(localOffset);
	    
	    // Multiply to get final world transform
	    Math3D.MatrixMultiply4(entityTransform, localOffset, outTransform);
	}
	
	//------------------------------------------------------------------------------------------------
	// FIXED: Properly attach camera to device (camera follows device, not other way around)
	//------------------------------------------------------------------------------------------------
	protected void AttachCameraToDevice(IEntity deviceEntity, AG0_TDLDeviceComponent device)
	{
	    if (!m_SpawnedFeedCamera || !deviceEntity || !device)
	        return;
	    
	    // First try slot-based attachment if device has a camera slot
	    SlotManagerComponent slotMgr = SlotManagerComponent.Cast(
	        deviceEntity.FindComponent(SlotManagerComponent));
	    
	    if (slotMgr)
	    {
	        EntitySlotInfo slot = slotMgr.GetSlotByName("Camera");
	        if (slot)
	        {
	            slot.AttachEntity(m_SpawnedFeedCamera);
	            Print("[TDL Feed] Attached camera via slot", LogLevel.DEBUG);
	            return;
	        }
	    }
	    
	    // Fallback: Make camera a child of the device entity
	    // The camera follows the device, with transform offset from PointInfo
	    vector localMat[4];
	    device.m_CameraAttachment.GetLocalTransform(localMat);
	    
	    // FIXED: Device is parent, camera is child (not the other way around)
	    deviceEntity.AddChild(m_SpawnedFeedCamera, -1, EAddChildFlags.AUTO_TRANSFORM);
	    m_SpawnedFeedCamera.SetLocalTransform(localMat);
	    
	    Print("[TDL Feed] Attached camera as child entity", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// UPDATED: Exit feed view - cleanup attachment
	//------------------------------------------------------------------------------------------------
	protected void ExitRemoteFeedView()
	{
	    if (!m_bViewingRemoteFeed)
	        return;
	    
	    // Restore original camera first
	    if (m_OriginalCamera)
	        GetGame().GetCameraManager().SetCamera(m_OriginalCamera);
	    
	    // Detach and cleanup spawned camera
	    if (m_SpawnedFeedCamera)
	    {
	        // If attached as child, detach first
	        IEntity parent = m_SpawnedFeedCamera.GetParent();
	        if (parent)
	        {
	            parent.RemoveChild(m_SpawnedFeedCamera);
	        }
	        
	        SCR_EntityHelper.DeleteEntityAndChildren(m_SpawnedFeedCamera);
	        m_SpawnedFeedCamera = null;
	    }
	    
	    m_bViewingRemoteFeed = false;
	    m_OriginalCamera = null;
	    m_PendingFeedSourceId = RplId.Invalid();
	    m_AttachedFeedSourceId = RplId.Invalid();
	    
	    SetFeedViewMode(false);
	    Print("[TDL Feed] Exited feed view", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void SetFeedViewMode(bool viewing)
	{
	    Widget mainFrame = m_wRoot.FindAnyWidget("MainFrame");
	    
	    if (mainFrame)
	        mainFrame.SetVisible(!viewing);
	    
	    if (m_wFeedOverlay)
	        m_wFeedOverlay.SetVisible(viewing);
	    
	    if (m_wFeedMemberName && m_SelectedMember)
	        m_wFeedMemberName.SetText(m_SelectedMember.GetPlayerName());
	    
	    if (viewing && m_wFeedBackButton)
	        GetGame().GetWorkspace().SetFocusedWidget(m_wFeedBackButton);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnFeedBackClicked()
	{
	    ExitRemoteFeedView();
	}
    
    //------------------------------------------------------------------------------------------------
    protected void OnSettingsButtonClicked()
    {
        ShowSettingsPanel();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnCallsignSaveClicked()
    {
        if (!m_CallsignEditBox || !m_NetworkDevice)
            return;
        
        string newCallsign = m_CallsignEditBox.GetText();
        
        // Use player controller to RPC the change on the network device (radio)
        SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (playerController)
        {
            playerController.RequestSetDeviceCallsign(m_NetworkDevice.GetDeviceRplId(), newCallsign);
            Print(string.Format("[TDLMenu] Requested callsign change to: %1 for network device", newCallsign), LogLevel.DEBUG);
        }
        
        // Return to network list
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnSettingsBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ShowSettingsPanel()
    {
        // Populate edit box with current callsign from network device
        if (m_CallsignEditBox && m_NetworkDevice)
        {
            string currentCallsign = m_NetworkDevice.GetCustomCallsign();
            m_CallsignEditBox.SetText(currentCallsign);
        }
        
        SetPanelContent(ETDLPanelContent.SETTINGS);
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
    
	protected void OnMapDragStart()
	{
	    m_bPlayerTracking = false;
	}
	
	void EnablePlayerTracking()
	{
	    m_bPlayerTracking = true;
	}
	
    //------------------------------------------------------------------------------------------------
    // UPDATE LOOP
    //------------------------------------------------------------------------------------------------
    override void OnMenuUpdate(float tDelta)
	{
	    super.OnMenuUpdate(tDelta);
		
		if (m_bViewingRemoteFeed)
	    {
	        // MenuBack input as escape hatch
	        if (m_InputManager && m_InputManager.GetActionTriggered("MenuBack"))
	        {
	            ExitRemoteFeedView();
	            return;
	        }
	        
	        // Poll for pending attachment (only if not already attached)
	        if (m_PendingFeedSourceId != RplId.Invalid())
	        {
	            m_fFeedAttachTimer += tDelta;
	            
	            // Try to find and attach to the device
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
	                        // Device streamed in! Now attach properly
	                        
	                        // First reposition camera to correct location
	                        vector cameraTransform[4];
	                        GetCameraTransformFromDevice(remoteEntity, remoteDevice, cameraTransform);
	                        m_SpawnedFeedCamera.SetWorldTransform(cameraTransform);
	                        
	                        // Then attach
	                        AttachCameraToDevice(remoteEntity, remoteDevice);
	                        
	                        m_AttachedFeedSourceId = m_PendingFeedSourceId;
	                        m_PendingFeedSourceId = RplId.Invalid();
	                        Print("[TDL Feed] Deferred attachment successful", LogLevel.DEBUG);
	                    }
	                }
	            }
	            
	            // Timeout - device never streamed in, stay at player position
	            if (m_fFeedAttachTimer > FEED_ATTACH_TIMEOUT)
	            {
	                Print("[TDL Feed] Timeout waiting for device to stream - staying at player position", LogLevel.WARNING);
	                m_PendingFeedSourceId = RplId.Invalid();
	                // Don't set m_AttachedFeedSourceId - we're not properly attached
	            }
	        }
	        
	        return;
	    }
		
	    // Process mouse drag input
	    if (m_DragHandler && m_MapView)
	    {
	        int deltaX, deltaY;
	        if (m_DragHandler.GetDragDelta(deltaX, deltaY))
	        {
	            m_MapView.Pan(deltaX, -deltaY);
	            m_bPlayerTracking = false;
	        }
	    }
	    
	    // Process gamepad right stick pan input
	    if (m_MapView && m_InputManager)
	    {
	        float panX = m_InputManager.GetActionValue("TDLPanHorizontal");
	        float panY = m_InputManager.GetActionValue("TDLPanVertical");
	        
	        // Apply deadzone and convert to pan delta
	        if (Math.AbsFloat(panX) > STICK_DEADZONE || Math.AbsFloat(panY) > STICK_DEADZONE)
	        {
	            float deltaX = -panX * STICK_PAN_SPEED * tDelta;
	            float deltaY = panY * STICK_PAN_SPEED * tDelta;
	            m_MapView.Pan(deltaX, -deltaY);  // Negate Y so stick-up pans view up
	            m_bPlayerTracking = false;
	        }
	    }
	    
	    // Always update map (it's always visible)
	    UpdateMapView(tDelta);
	    
	    // Always update self marker
	    UpdateSelfMarker();
	    
	    // Periodic network refresh
	    m_fUpdateTimer += tDelta;
	    if (m_fUpdateTimer >= UPDATE_INTERVAL)
	    {
	        m_fUpdateTimer = 0;
	        
	        if (m_eActivePanel == ETDLPanelContent.NETWORK_LIST)
	            RefreshNetworkList();
	        else if (m_eActivePanel == ETDLPanelContent.MEMBER_DETAIL)
	            PopulateDetailView();
			UpdateCameraButtonState();
	    }
	    
	    // Handle input
	    HandleInput();
		
		// Update active plugins
		foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
		    plugin.OnMenuUpdate(tDelta);
	}
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMapView(float tDelta)
	{
	    if (!m_MapView)
	        return;
	    
	    IEntity player = GetGame().GetPlayerController().GetControlledEntity();
	    if (!player)
	        return;
	    
	    if (m_bPlayerTracking)
	        m_MapView.CenterOnPlayer();
	    
	    if (m_bTrackUp)
	    {
	        vector angles = player.GetYawPitchRoll();
	        m_MapView.SetTrackUp(angles[0]);
	    }
	    else
	    {
	        m_MapView.SetRotation(0);
	    }
	    
	    // Update heading indicator to point north
	    if (m_wHeadingIndicator)
	        m_wHeadingIndicator.SetRotation(m_MapView.GetRotation());
	    
	    // Draw map background, buildings, grid (no canvas markers)
	    m_MapView.Draw();
	    
	    // Update widget-based markers
	    UpdateSelfMapMarker(player);
	    UpdateMemberMapMarkers();
	}
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateSelfMarker()
	{
	    IEntity player = GetGame().GetPlayerController().GetControlledEntity();
	    if (!player)
	        return;
	    
	    vector pos = player.GetOrigin();
	    vector angles = player.GetYawPitchRoll();
	    
	    // GPS Status - check if any held device provides GPS
	    if (m_wGPSStatus)
	    {
	        bool hasGPS = HasGPSProvider();
	        if (hasGPS)
	        {
	            m_wGPSStatus.SetText("GPS: 3D FIX");
	            m_wGPSStatus.SetColor(COLOR_CYAN);
	        }
	        else
	        {
	            m_wGPSStatus.SetText("NO GPS");
	            m_wGPSStatus.SetColor(COLOR_RED);
	        }
	    }
	    
	    // Callsign - use network device (radio) callsign, not ATAK
	    if (m_wCallsign)
	    {
	        string callsign = "UNKNOWN";
	        if (m_NetworkDevice)
	            callsign = m_NetworkDevice.GetDisplayName();
	        m_wCallsign.SetText(callsign);
	    }
	    
	    // Grid coordinates
	    if (m_wGrid)
		{
		    string gridLabel = FormatGridReference(pos, 5);
		    m_wGrid.SetText(gridLabel);
		}
	    
	    // Altitude (Y axis in Arma)
	    if (m_wAltitude)
	        m_wAltitude.SetTextFormat("%1 MSL", Math.Round(pos[1]).ToString());
	    
	    // Heading
	    if (m_wHeading)
	    {
	        float heading = angles[0];
	        if (heading < 0) heading += 360;
	        m_wHeading.SetTextFormat("%1°M", Math.Round(heading).ToString());
	    }
	    
	    // Speed - need velocity from physics or character controller
	    if (m_wSpeed)
	    {
	        Physics phys = player.GetPhysics();
	        if (phys)
	        {
	            float speed = phys.GetVelocity().Length() * 2.237;  // m/s to MPH
	            m_wSpeed.SetTextFormat("%1 MPH", Math.Round(speed).ToString());
	        }
	        else
	        {
	            m_wSpeed.SetText("-- MPH");
	        }
	    }
	    
	    // Error/accuracy - placeholder since we don't have real GPS error
	    if (m_wError)
	        m_wError.SetText("+/- 5m");  // TODO: actual accuracy from device
	}
    
    //------------------------------------------------------------------------------------------------
    // MAP MARKER WIDGET MANAGEMENT
    //------------------------------------------------------------------------------------------------
    
    //------------------------------------------------------------------------------------------------
	//! Update self marker position and rotation on the map overlay
	protected void UpdateSelfMapMarker(IEntity player)
	{
	    if (!m_wSelfMapMarker || !m_MapView || !m_wMarkerOverlay)
	        return;
	    
	    vector playerPos = player.GetOrigin();
	    float playerHeading = player.GetYawPitchRoll()[0];
	    
	    // Convert world position to layout coordinates for widget positioning
	    float layoutX, layoutY;
	    m_MapView.WorldToLayout(playerPos, layoutX, layoutY);
	    
	    FrameSlot.SetPos(m_wSelfMapMarker, layoutX, layoutY);
	    
	    // Rotate marker to show heading (accounting for map rotation)
	    ImageWidget markerImage = ImageWidget.Cast(m_wSelfMapMarker.FindAnyWidget("MarkerImage"));
	    if (markerImage)
	    {
	        float markerRotation = playerHeading + m_MapView.GetRotation();
	        markerImage.SetRotation(markerRotation);
	    }
	    
	    m_wSelfMapMarker.SetVisible(true);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Update network member markers - creates/destroys/positions as needed
	protected void UpdateMemberMapMarkers()
	{
	    if (!m_MapView || !m_wMarkerOverlay)
	        return;
	    
	    // Get all of the player's device RplIds to filter them out
	    set<RplId> ownDeviceIds = GetPlayerOwnDeviceIds();
	    
	    // Get current network members from player controller
	    array<ref AG0_TDLNetworkMember> members = {};
	    AG0_TDLNetworkMembers membersData = GetNetworkMembersFromController();
	    if (membersData)
	    {
	        map<RplId, ref AG0_TDLNetworkMember> membersMap = membersData.ToMap();
	        foreach (RplId rplId, AG0_TDLNetworkMember member : membersMap)
	        {
	            members.Insert(member);
	        }
	    }
	    
	    // Track which members we process this frame
	    ref set<RplId> processedIds = new set<RplId>();
	    
	    // Get canvas bounds in layout coordinates for visibility check
	    WorkspaceWidget workspace = GetGame().GetWorkspace();
	    float canvasW, canvasH;
	    m_wMapCanvas.GetScreenSize(canvasW, canvasH);
	    float layoutCanvasW = workspace.DPIUnscale(canvasW);
	    float layoutCanvasH = workspace.DPIUnscale(canvasH);
	    
	    float margin = MARKER_SIZE;
	    
	    foreach (AG0_TDLNetworkMember member : members)
	    {
	        RplId memberId = member.GetRplId();
	        
	        // Skip any of our own devices - we show self with the self marker
	        if (ownDeviceIds.Contains(memberId))
	            continue;
	        
	        vector memberPos = member.GetPosition();
	        
	        // Convert to layout coordinates for widget positioning
	        float layoutX, layoutY;
	        m_MapView.WorldToLayout(memberPos, layoutX, layoutY);
	        
	        // Check if on screen (with some margin for partially visible markers)
	        bool isVisible = (layoutX >= -margin && layoutX <= layoutCanvasW + margin &&
	                          layoutY >= -margin && layoutY <= layoutCanvasH + margin);
	        
	        if (!isVisible)
	        {
	            // Off screen - destroy marker if it exists
	            if (m_mMemberMarkers.Contains(memberId))
	            {
	                Widget marker = m_mMemberMarkers.Get(memberId);
	                if (marker)
	                    marker.RemoveFromHierarchy();
	                m_mMemberMarkers.Remove(memberId);
	            }
	            continue;
	        }
	        
	        processedIds.Insert(memberId);
	        
	        // Get or create marker widget
	        Widget marker;
	        if (m_mMemberMarkers.Contains(memberId))
	        {
	            marker = m_mMemberMarkers.Get(memberId);
	        }
	        else
	        {
	            marker = CreateMemberMapMarker(member);
	            if (!marker)
	                continue;
	            m_mMemberMarkers.Set(memberId, marker);
	        }
	        
	        // Position marker using layout coordinates
	        FrameSlot.SetPos(marker, layoutX, layoutY);
	        marker.SetVisible(true);
	    }
	    
	    // Cleanup orphaned markers (members no longer in network)
	    array<RplId> toRemove = {};
	    foreach (RplId id, Widget w : m_mMemberMarkers)
	    {
	        if (!processedIds.Contains(id))
	            toRemove.Insert(id);
	    }
	    
	    foreach (RplId id : toRemove)
	    {
	        Widget marker = m_mMemberMarkers.Get(id);
	        if (marker)
	            marker.RemoveFromHierarchy();
	        m_mMemberMarkers.Remove(id);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    //! Create a clickable marker widget for a network member
    protected Widget CreateMemberMapMarker(AG0_TDLNetworkMember member)
    {
        Widget marker = GetGame().GetWorkspace().CreateWidgets(MEMBER_MARKER_LAYOUT, m_wMarkerOverlay);
        if (!marker)
            return null;
        
        // Setup click handler on MarkerButton (inside the layout hierarchy)
        ButtonWidget button = ButtonWidget.Cast(marker.FindAnyWidget("MarkerButton"));
        if (button)
        {
            AG0_TDLMapMarkerHandler handler = new AG0_TDLMapMarkerHandler();
            handler.Init(this, member.GetRplId());
            button.AddHandler(handler);
        }
        
        // Set device identifier label
        TextWidget label = TextWidget.Cast(marker.FindAnyWidget("DeviceIdentifier"));
        if (label)
            label.SetText(member.GetPlayerName());
        
        return marker;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Cleanup all map markers
    protected void CleanupAllMapMarkers()
    {
        // Destroy self marker
        if (m_wSelfMapMarker)
        {
            m_wSelfMapMarker.RemoveFromHierarchy();
            m_wSelfMapMarker = null;
        }
        
        // Destroy all member markers
        foreach (RplId id, Widget marker : m_mMemberMarkers)
        {
            if (marker)
                marker.RemoveFromHierarchy();
        }
        m_mMemberMarkers.Clear();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Callback when a map marker is clicked
    void OnMapMarkerClicked(RplId memberId)
    {
        if (!HasNetworkMemberData())
            return;
        
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
        if (!member)
            return;
        
        PrintFormat("[TDLMenu] Map marker clicked: %1", member.GetPlayerName());
        ShowDetailView(member, memberId);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Callback when a map marker gains focus (gamepad navigation)
    void OnMapMarkerFocused(RplId memberId)
    {
        if (!m_MapView)
            return;
        
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
        if (!member)
            return;
        
        // Pan map to center on the focused member
        m_MapView.SetCenter(member.GetPosition());
        m_bPlayerTracking = false;
        m_bMarkerFocused = true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Clear marker focus state (called when focus leaves markers)
    void ClearMarkerFocus()
    {
        m_bMarkerFocused = false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void HandleInput()
    {
        // ESC or B button behavior
        if (m_InputManager.GetActionTriggered("MenuBack"))
        {
            // If focused on a map marker, return to toolbar instead of normal back behavior
            if (m_bMarkerFocused)
            {
                m_bMarkerFocused = false;
                if (m_wNetworkButton)
                    GetGame().GetWorkspace().SetFocusedWidget(m_wNetworkButton);
                return;
            }
            
            // Normal panel-based back behavior
            switch (m_eActivePanel)
            {
                case ETDLPanelContent.MEMBER_DETAIL:
                    SetPanelContent(ETDLPanelContent.NETWORK_LIST);
                    break;
                    
                case ETDLPanelContent.SETTINGS:
                    SetPanelContent(ETDLPanelContent.NETWORK_LIST);
                    break;
                    
                case ETDLPanelContent.NETWORK_LIST:
                    SetPanelContent(ETDLPanelContent.NONE);
                    break;
                    
                case ETDLPanelContent.NONE:
                    Close();
                    break;
            }
        }
    }
	
    //------------------------------------------------------------------------------------------------
	// DEVICE DISCOVERY
	//------------------------------------------------------------------------------------------------
	protected void FindActiveDevice()
	{
	    IEntity player = GetGame().GetPlayerController().GetControlledEntity();
	    if (!player)
	        return;
	    
	    // Check held gadget first
	    SCR_GadgetManagerComponent gadgetMgr = SCR_GadgetManagerComponent.Cast(
	        player.FindComponent(SCR_GadgetManagerComponent)
	    );
	    if (gadgetMgr)
	    {
	        IEntity heldGadget = gadgetMgr.GetHeldGadget();
	        if (heldGadget)
	        {
	            AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
	                heldGadget.FindComponent(AG0_TDLDeviceComponent)
	            );
	            if (device && device.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
	            {
	                m_ActiveDevice = device;
	                if (m_wDeviceName)
	                    m_wDeviceName.SetText(device.GetDisplayName());
	                return;
	            }
	        }
	    }
	    
	    // Check inventory
	    InventoryStorageManagerComponent storage = InventoryStorageManagerComponent.Cast(
	        player.FindComponent(InventoryStorageManagerComponent)
	    );
	    if (storage)
	    {
	        array<IEntity> items = {};
	        storage.GetItems(items);
	        
	        foreach (IEntity item : items)
	        {
	            AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
	                item.FindComponent(AG0_TDLDeviceComponent)
	            );
	            if (device && device.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
	            {
	                m_ActiveDevice = device;
	                if (m_wDeviceName)
	                    m_wDeviceName.SetText(device.GetDisplayName());
	                return;
	            }
	        }
	    }
	    
	    // Check equipment slots (vest, backpack, etc.)
	    ChimeraCharacter character = ChimeraCharacter.Cast(player);
	    if (character)
	    {
	        EquipedLoadoutStorageComponent loadoutStorage = EquipedLoadoutStorageComponent.Cast(
	            character.FindComponent(EquipedLoadoutStorageComponent)
	        );
	        if (loadoutStorage)
	        {
	            array<typename> equipmentAreas = {
	                LoadoutHeadCoverArea, LoadoutArmoredVestSlotArea, 
	                LoadoutVestArea, LoadoutJacketArea, LoadoutBackpackArea
	            };
	            
	            foreach (typename area : equipmentAreas)
	            {
	                IEntity container = loadoutStorage.GetClothFromArea(area);
	                if (!container)
	                    continue;
	                
	                // Check the container itself
	                AG0_TDLDeviceComponent containerDevice = AG0_TDLDeviceComponent.Cast(
	                    container.FindComponent(AG0_TDLDeviceComponent)
	                );
	                if (containerDevice && containerDevice.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
	                {
	                    m_ActiveDevice = containerDevice;
	                    if (m_wDeviceName)
	                        m_wDeviceName.SetText(containerDevice.GetDisplayName());
	                    return;
	                }
	                
	                // Check items stored in the container
	                ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
	                    container.FindComponent(ClothNodeStorageComponent)
	                );
	                if (!clothStorage)
	                    continue;
	                
	                array<IEntity> clothItems = {};
	                clothStorage.GetAll(clothItems);
	                
	                foreach (IEntity clothItem : clothItems)
	                {
	                    AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
	                        clothItem.FindComponent(AG0_TDLDeviceComponent)
	                    );
	                    if (device && device.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
	                    {
	                        m_ActiveDevice = device;
	                        if (m_wDeviceName)
	                            m_wDeviceName.SetText(device.GetDisplayName());
	                        return;
	                    }
	                }
	            }
	        }
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    // NETWORK LIST
    //------------------------------------------------------------------------------------------------
    protected void RefreshNetworkList()
    {
        if (!HasNetworkMemberData())
            return;
        
        array<ref AG0_TDLNetworkMember> members = {};
		AG0_TDLNetworkMembers membersData = GetNetworkMembersFromController();
		if (membersData)
		{
		    map<RplId, ref AG0_TDLNetworkMember> membersMap = membersData.ToMap();
		    foreach (RplId rplId, AG0_TDLNetworkMember member : membersMap)
		    {
		        members.Insert(member);
		    }
		}
        
        // Check if rebuild needed
        bool needsRebuild = false;
        if (members.Count() != m_aCachedMemberIds.Count())
        {
            needsRebuild = true;
        }
        else
        {
            foreach (int i, AG0_TDLNetworkMember member : members)
            {
                if (i >= m_aCachedMemberIds.Count() || m_aCachedMemberIds[i] != member.GetRplId())
                {
                    needsRebuild = true;
                    break;
                }
            }
        }
        
        if (needsRebuild)
            RebuildMemberCards(members);
        else
            UpdateMemberCards(members);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void RebuildMemberCards(array<ref AG0_TDLNetworkMember> members)
    {
        // Clear existing
        foreach (Widget card : m_aMemberCards)
        {
            if (card)
                card.RemoveFromHierarchy();
        }
        m_aMemberCards.Clear();
        m_aCachedMemberIds.Clear();
        
        if (!m_wMemberList)
            return;
        
        // Create cards
        foreach (int index, AG0_TDLNetworkMember member : members)
        {
            CreateMemberCard(member, index);
            m_aCachedMemberIds.Insert(member.GetRplId());
        }
        
        // Reset focus
        if (!m_aMemberCards.IsEmpty())
            SetFocusToCard(0);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CreateMemberCard(AG0_TDLNetworkMember member, int index)
	{
	    Widget card = GetGame().GetWorkspace().CreateWidgets(MEMBER_CARD_LAYOUT, m_wMemberList);
	    if (!card) return;
	    
	    ButtonWidget button = ButtonWidget.Cast(card);
	    if (button)
	    {
	        AG0_TDLMemberCardHandler handler = new AG0_TDLMemberCardHandler();
	        handler.Init(this, member.GetRplId(), member);
	        button.AddHandler(handler);
	        
	        // First card: set UP navigation to settings button
	        if (index == 0)
	        {
	            button.SetNavigation(WidgetNavigationDirection.UP, WidgetNavigationRuleType.EXPLICIT, "SettingsButton");
	        }
	    }
	    
	    UpdateCardWidgets(card, member);
	    m_aMemberCards.Insert(card);
	}
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMemberCards(array<ref AG0_TDLNetworkMember> members)
    {
        foreach (int i, Widget card : m_aMemberCards)
        {
            if (i >= members.Count())
                break;
            
            UpdateCardWidgets(card, members[i]);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateCardWidgets(Widget card, AG0_TDLNetworkMember member)
	{
	    if (!card || !member) return;
	    
	    TextWidget nameText = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
	    if (nameText)
	        nameText.SetText(member.GetPlayerName());
	    
	    TextWidget ipText = TextWidget.Cast(card.FindAnyWidget("NetworkIP"));
	    if (ipText)
	        ipText.SetText("192.168.0." + member.GetNetworkIP().ToString());
	    
	    // Status dot color based on signal strength
	    ImageWidget statusDot = ImageWidget.Cast(card.FindAnyWidget("StatusDot"));
	    if (statusDot)
	    {
	        float signal = member.GetSignalStrength();
	        if (signal >= 60)
	            statusDot.SetColor(Color.FromRGBA(0, 200, 0, 255));      // Green - good
	        else if (signal >= 30)
	            statusDot.SetColor(Color.FromRGBA(200, 200, 0, 255));    // Yellow - weak
	        else
	            statusDot.SetColor(Color.FromRGBA(200, 0, 0, 255));      // Red - poor
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    protected void SetFocusToCard(int index)
    {
        if (index < 0 || index >= m_aMemberCards.Count())
            return;
        
        Widget card = m_aMemberCards[index];
        ButtonWidget button = ButtonWidget.Cast(card);
        
        if (button)
        {
            GetGame().GetWorkspace().SetFocusedWidget(button);
            m_iFocusedCardIndex = index;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // CALLBACKS FROM MEMBER CARDS
    //------------------------------------------------------------------------------------------------
    void OnMemberCardFocused(RplId memberId)
	{
	    for (int i = 0; i < m_aMemberCards.Count(); i++)
	    {
	        Widget card = m_aMemberCards[i];
	        AG0_TDLMemberCardHandler handler = AG0_TDLMemberCardHandler.Cast(
	            ButtonWidget.Cast(card).FindHandler(AG0_TDLMemberCardHandler)
	        );
	        
	        if (handler && handler.GetMemberRplId() == memberId)
	        {
	            m_iFocusedCardIndex = i;
	            break;
	        }
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    void OnMemberCardClicked(RplId memberId, int button)
	{
	    if (!HasNetworkMemberData())
	        return;
	    
	    AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
	    if (!member)
	        return;
	    
	    PrintFormat("TDL Menu: Selected member %1", member.GetPlayerName());
	    ShowDetailView(member, memberId);
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
		
	    // Save state to static for next open
	    if (m_MapView)
	    {
	        s_fLastZoom = m_MapView.GetZoom();
	        s_vLastCenter = m_MapView.GetCenter();
	    }
	    s_eLastPanel = m_eActivePanel;
	    s_bLastPlayerTracking = m_bPlayerTracking;
	    s_bLastTrackUp = m_bTrackUp;
	    s_bHasSavedState = true;
		
		// Notify and disable plugins
	    foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
	    {
	        plugin.OnMenuClosed();
	        plugin.Disable();
	    }
	    m_aActivePlugins.Clear();
		
		// Cleanup map markers
		CleanupAllMapMarkers();
		
        m_MapView = null;
        m_aMemberCards.Clear();
        m_aCachedMemberIds.Clear();
        
        super.OnMenuClose();
    }
}