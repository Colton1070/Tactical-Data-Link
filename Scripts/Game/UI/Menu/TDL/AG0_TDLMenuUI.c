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
    protected ETDLPanelContent m_eActivePanel = ETDLPanelContent.NETWORK_LIST;
	protected ref AG0_TDLMapCanvasDragHandler m_DragHandler;
	protected bool m_bPlayerTracking = true;
    
    // Core references
    protected AG0_TDLDeviceComponent m_ActiveDevice;
    protected Widget m_wRoot;
    protected InputManager m_InputManager;
    
    // Map view - always active
    protected ref AG0_TDLMapView m_MapView;
    protected CanvasWidget m_wMapCanvas;
    protected Widget m_wMapPanel;
    
    // Side panel structure
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wCloseButton;
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
    protected TextWidget m_wDetailDistance;
    protected TextWidget m_wDetailCapabilities;
    protected Widget m_wViewFeedButton;
    protected Widget m_wBackButton;
    
    // Toolbar widgets
    protected Widget m_wToolbar;
    protected Widget m_wMenuButton;
    protected Widget m_wNetworkButton;
    
    // Zoom/compass controls
    protected Widget m_wZoomInButton;
    protected Widget m_wZoomOutButton;
    protected Widget m_wCompassButton;
    protected Widget m_wTrackButton;
    protected ImageWidget m_wHeadingIndicator;
    protected bool m_bTrackUp = true;  // Default to track-up mode
    
    // Self marker widget
	protected Widget m_wSelfMarkerWidget;
	protected TextWidget m_wGPSStatus;
	protected TextWidget m_wCallsign;
	protected TextWidget m_wGrid;
	protected TextWidget m_wAltitude;
	protected TextWidget m_wHeading;
	protected TextWidget m_wSpeed;
	protected TextWidget m_wError;
    
    // Selected member for detail view
    protected ref AG0_TDLNetworkMember m_SelectedMember;
    protected RplId m_SelectedDeviceId;
    
    // State tracking
    protected ref array<RplId> m_aCachedMemberIds = {};
    protected ref array<Widget> m_aMemberCards = {};
    protected float m_fUpdateTimer = 0;
    protected const float UPDATE_INTERVAL = 0.5;
    protected int m_iFocusedCardIndex = -1;
    
    // Layout paths
    protected const ResourceName MEMBER_CARD_LAYOUT = "{7C025C99261C96C5}UI/layouts/Menus/TDL/TDLMemberCardUI.layout";
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen()
	{
	    m_wRoot = GetRootWidget();
	    m_InputManager = GetGame().GetInputManager();
	    
	    // Map panel (always visible)
	    m_wMapPanel = m_wRoot.FindAnyWidget("MapPanel");
	    m_wMapCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("MapCanvas"));
	    
	    Widget dragSurface = m_wRoot.FindAnyWidget("MapDragSurface");
		if (dragSurface)
		{
		    m_DragHandler = new AG0_TDLMapCanvasDragHandler();
		    dragSurface.AddHandler(m_DragHandler);
		    m_DragHandler.m_OnDragStart.Insert(OnMapDragStart);
		}
	    
	    // Side panel structure
	    m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
	    m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
	    m_wCloseButton = m_wRoot.FindAnyWidget("CloseButton");
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
	    m_wDetailDistance = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailDistance"));
	    m_wDetailCapabilities = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailCapabilities"));
	    m_wViewFeedButton = m_wRoot.FindAnyWidget("ViewFeedButton");
	    m_wBackButton = m_wRoot.FindAnyWidget("BackButton");
	    
	    // Toolbar widgets
	    m_wToolbar = m_wRoot.FindAnyWidget("Toolbar");
	    m_wMenuButton = m_wRoot.FindAnyWidget("MenuButton");
	    m_wNetworkButton = m_wRoot.FindAnyWidget("NetworkButton");
	    
	    // Zoom/compass controls
	    m_wZoomInButton = m_wRoot.FindAnyWidget("ZoomInButton");
	    m_wZoomOutButton = m_wRoot.FindAnyWidget("ZoomOutButton");
	    m_wCompassButton = m_wRoot.FindAnyWidget("CompassButton");
	    m_wTrackButton = m_wRoot.FindAnyWidget("TrackButton");
	    m_wHeadingIndicator = ImageWidget.Cast(m_wRoot.FindAnyWidget("HeadingIndicator"));
	    
	    // Self marker widget
	    m_wSelfMarkerWidget = m_wRoot.FindAnyWidget("SelfMarkerWidget");
	    m_wGPSStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("GPSStatus"));
	    m_wCallsign = TextWidget.Cast(m_wRoot.FindAnyWidget("Callsign"));
	    m_wGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("Grid"));
	    m_wAltitude = TextWidget.Cast(m_wRoot.FindAnyWidget("Altitude"));
	    m_wHeading = TextWidget.Cast(m_wRoot.FindAnyWidget("Heading"));
	    m_wSpeed = TextWidget.Cast(m_wRoot.FindAnyWidget("Speed"));
	    m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("Error"));
	    
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
	    
	    // Get active TDL device
	    FindActiveDevice();
	    
	    // Restore or set initial panel state
	    if (s_bHasSavedState)
	        SetPanelContent(s_eLastPanel);
	    else
	        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
		
		HookButtonHandlers();
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
        // Close button
        if (m_wCloseButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCloseButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnClosePanelClicked);
        }
        
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
        
        // View feed button
        if (m_wViewFeedButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wViewFeedButton.FindHandler(SCR_ModularButtonComponent)
            );
            if (comp)
                comp.m_OnClicked.Insert(OnViewFeedClickedInternal);
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
    }
    
    //------------------------------------------------------------------------------------------------
    // PANEL MANAGEMENT
    //------------------------------------------------------------------------------------------------
    protected void SetPanelContent(ETDLPanelContent content)
    {
        m_eActivePanel = content;
        
        // Panel visibility
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
        if (!m_SelectedMember)
            return;
        
        if (m_wDetailPlayerName)
            m_wDetailPlayerName.SetText(m_SelectedMember.GetPlayerName());
        
        if (m_wDetailSignalStrength)
            m_wDetailSignalStrength.SetTextFormat("%1 dBm", m_SelectedMember.GetSignalStrength().ToString());
        
        if (m_wDetailNetworkIP)
            m_wDetailNetworkIP.SetText("192.168.0." + m_SelectedMember.GetNetworkIP().ToString());
        
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
        
        // Show/hide video button based on capability
        if (m_wViewFeedButton)
        {
            bool hasVideo = (m_SelectedMember.GetCapabilities() & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0;
            m_wViewFeedButton.SetVisible(hasVideo);
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
    protected void OnClosePanelClicked()
    {
        SetPanelContent(ETDLPanelContent.NONE);
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
    protected void OnViewFeedClickedInternal()
    {
        if (m_SelectedMember)
            Print("[TDLMenu] View feed clicked for " + m_SelectedMember.GetPlayerName(), LogLevel.NORMAL);
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
	    
	    // Process drag input
	    if (m_DragHandler && m_MapView)
	    {
	        int deltaX, deltaY;
	        if (m_DragHandler.GetDragDelta(deltaX, deltaY))
	        {
	            m_MapView.Pan(deltaX, -deltaY);
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
	    }
	    
	    // Handle input
	    HandleInput();
	}
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMapView(float tDelta)
	{
	    if (!m_MapView || !m_ActiveDevice)
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
	    
	    m_MapView.ClearMarkers();
	    
	    vector playerPos = player.GetOrigin();
	    float playerHeading = player.GetYawPitchRoll()[0];
	    m_MapView.AddSelfMarker(playerPos, playerHeading);
	    
	    array<ref AG0_TDLNetworkMember> members = {};
	    AG0_TDLNetworkMembers membersData = m_ActiveDevice.GetNetworkMembersData();
	    if (membersData)
	    {
	        map<RplId, ref AG0_TDLNetworkMember> membersMap = membersData.ToMap();
	        foreach (RplId rplId, AG0_TDLNetworkMember member : membersMap)
	        {
	            members.Insert(member);
	        }
	    }
	    foreach (AG0_TDLNetworkMember member : members)
	    {
	        m_MapView.AddMemberMarker(member.GetPosition(), member.GetPlayerName(), member.GetSignalStrength());
	    }
	    
	    m_MapView.Draw();
	}
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateSelfMarker()
	{
	    IEntity player = GetGame().GetPlayerController().GetControlledEntity();
	    if (!player)
	        return;
	    
	    vector pos = player.GetOrigin();
	    vector angles = player.GetYawPitchRoll();
	    
	    // GPS Status
	    if (m_wGPSStatus)
	        m_wGPSStatus.SetText("GPS: 3D FIX");  // TODO: actual GPS state from device
	    
	    // Callsign
	    if (m_wCallsign)
	    {
	        string callsign = "UNKNOWN";
	        if (m_ActiveDevice)
	            callsign = m_ActiveDevice.GetDisplayName();
	        m_wCallsign.SetText(callsign);
	    }
	    
	    // Grid coordinates
	    if (m_wGrid)
	        m_wGrid.SetTextFormat("%1, %2", Math.Round(pos[0]).ToString(), Math.Round(pos[2]).ToString());
	    
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
    protected void HandleInput()
    {
        // ESC or B button behavior depends on panel state
        if (m_InputManager.GetActionTriggered("MenuBack"))
        {
            switch (m_eActivePanel)
            {
                case ETDLPanelContent.MEMBER_DETAIL:
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
                if (device && device.CanAccessNetwork() && 
                    device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION) &&
                    device.HasCapability(AG0_ETDLDeviceCapability.DISPLAY_OUTPUT))
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
                if (device && device.CanAccessNetwork() &&
                    device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION) &&
                    device.HasCapability(AG0_ETDLDeviceCapability.DISPLAY_OUTPUT))
                {
                    m_ActiveDevice = device;
                    if (m_wDeviceName)
                        m_wDeviceName.SetText(device.GetDisplayName());
                    return;
                }
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // NETWORK LIST
    //------------------------------------------------------------------------------------------------
    protected void RefreshNetworkList()
    {
        if (!m_ActiveDevice || !m_ActiveDevice.HasNetworkMemberData())
            return;
        
        array<ref AG0_TDLNetworkMember> members = {};
		AG0_TDLNetworkMembers membersData = m_ActiveDevice.GetNetworkMembersData();
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
	    if (!m_ActiveDevice || !m_ActiveDevice.HasNetworkMemberData())
	        return;
	    
	    AG0_TDLNetworkMember member = m_ActiveDevice.GetNetworkMember(memberId);
	    if (!member)
	        return;
	    
	    PrintFormat("TDL Menu: Selected member %1", member.GetPlayerName());
	    ShowDetailView(member, memberId);
	}

    
    //------------------------------------------------------------------------------------------------
    override void OnMenuClose()
    {
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
		
        m_MapView = null;
        m_aMemberCards.Clear();
        m_aCachedMemberIds.Clear();
        
        super.OnMenuClose();
    }
}