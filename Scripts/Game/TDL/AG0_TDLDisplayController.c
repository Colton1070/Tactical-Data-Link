// AG0_TDLDisplayController.c
// Shared controller for ATAK display - used by both fullscreen menu and world-space device
// Handles: map view, map markers, self info panel, network status, member cards
// State is STATIC so multiple instances (menu + device) stay synchronized

class AG0_TDLDisplayController
{
    // STATIC STATE - shared across all instances for synchronization
    static bool s_bPlayerTracking = true;
    static bool s_bTrackUp = true;
    static float s_fZoom = 0.15;
    static vector s_vCenter;
    static bool s_bHasState = false;
    
    // STATIC PANEL STATE - shared so menu and device show same panels
    static bool s_bSidePanelVisible = true;
    static bool s_bNetworkContentVisible = true;
    static bool s_bDetailContentVisible = false;
    static bool s_bSettingsContentVisible = false;
    static string s_sPanelTitle = "CONTACTS";
    
    // Instance widgets
    protected Widget m_wRoot;
    protected ref AG0_TDLMapView m_MapView;
    protected CanvasWidget m_wMapCanvas;
    
    // Map marker overlay
    protected Widget m_wMarkerOverlay;
    protected Widget m_wSelfMapMarker;
    protected ref map<RplId, Widget> m_mMemberMarkers = new map<RplId, Widget>();
    protected const float MARKER_SIZE = 64.0;
    
    // Self marker info panel
    protected TextWidget m_wGPSStatus;
    protected TextWidget m_wCallsign;
    protected TextWidget m_wGrid;
    protected TextWidget m_wAltitude;
    protected TextWidget m_wHeading;
    protected TextWidget m_wSpeed;
    protected TextWidget m_wError;
    protected Widget m_wHeadingIndicator;
    
    // Network panel
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wNetworkContent;
    protected Widget m_wDetailContent;
    protected Widget m_wSettingsContent;
    protected Widget m_wMemberList;
    protected TextWidget m_wDeviceName;
    protected TextWidget m_wNetworkStatus;
    
    // Member cards (display only - menu adds click handlers separately)
    protected ref array<Widget> m_aMemberCards = {};
    protected ref array<RplId> m_aCachedMemberIds = {};
    
    // Update timing
    protected float m_fUpdateTimer = 0;
    protected const float UPDATE_INTERVAL = 0.5;
    
    // Colors
    protected ref Color COLOR_CYAN = new Color(0.2, 0.8, 0.8, 1.0);
    protected ref Color COLOR_RED = new Color(1.0, 0.2, 0.2, 1.0);
    
    // Layout paths
    protected const ResourceName SELF_MARKER_LAYOUT = "{A242BD2B06D27E00}UI/layouts/Menus/TDL/TDLMenuSelfMarker.layout";
    protected const ResourceName MEMBER_MARKER_LAYOUT = "{23872C52B88FDB59}UI/layouts/Menus/TDL/TDLMenuBuddyMarker.layout";
    protected const ResourceName MEMBER_CARD_LAYOUT = "{7C025C99261C96C5}UI/layouts/Menus/TDL/TDLMemberCardUI.layout";
    
    //------------------------------------------------------------------------------------------------
    // PUBLIC API
    //------------------------------------------------------------------------------------------------
    
    bool Init(Widget root)
    {
        if (!root)
            return false;
        
        m_wRoot = root;
        
        // Map canvas
        m_wMapCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("MapCanvas"));
        
        // Self marker info panel
        m_wGPSStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("GPSStatus"));
        m_wCallsign = TextWidget.Cast(m_wRoot.FindAnyWidget("Callsign"));
        m_wGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("Grid"));
        m_wAltitude = TextWidget.Cast(m_wRoot.FindAnyWidget("Altitude"));
        m_wHeading = TextWidget.Cast(m_wRoot.FindAnyWidget("Heading"));
        m_wSpeed = TextWidget.Cast(m_wRoot.FindAnyWidget("Speed"));
        m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("Error"));
        m_wHeadingIndicator = m_wRoot.FindAnyWidget("HeadingIndicator");
        
        // Network panel
        m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
        m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
        m_wNetworkContent = m_wRoot.FindAnyWidget("NetworkContent");
        m_wDetailContent = m_wRoot.FindAnyWidget("DetailContent");
        m_wSettingsContent = m_wRoot.FindAnyWidget("SettingsContent");
        m_wMemberList = m_wRoot.FindAnyWidget("MemberList");
        m_wDeviceName = TextWidget.Cast(m_wRoot.FindAnyWidget("DeviceName"));
        m_wNetworkStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("NetworkStatus"));
        
        // Apply colors
        ApplySelfPanelColors();
        
        // Initialize map view
        if (m_wMapCanvas)
        {
            m_MapView = new AG0_TDLMapView();
            if (!m_MapView.Init(m_wMapCanvas))
            {
                Print("[TDLDisplayController] Failed to init map view", LogLevel.WARNING);
                return false;
            }
            
            // Restore from static state or initialize
            if (s_bHasState)
            {
                m_MapView.SetZoom(s_fZoom);
                m_MapView.SetCenter(s_vCenter);
            }
            else
            {
                m_MapView.CenterOnPlayer();
                m_MapView.SetZoom(0.15);
                s_bHasState = true;
            }
        }
        
        // Marker overlay
        m_wMarkerOverlay = m_wRoot.FindAnyWidget("MarkerOverlay");
        if (m_wMarkerOverlay)
        {
            m_wSelfMapMarker = GetGame().GetWorkspace().CreateWidgets(SELF_MARKER_LAYOUT, m_wMarkerOverlay);
        }
        
        // Show contacts panel by default - use static state
        ApplyPanelState();
        
        Print("[TDLDisplayController] Init complete", LogLevel.DEBUG);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    void Update(float tDelta)
    {
        UpdateMapView(tDelta);
        UpdateSelfMarker();
        
        // Apply panel state each frame so device syncs with menu
        ApplyPanelState();
        
        // Periodic updates
        m_fUpdateTimer += tDelta;
        if (m_fUpdateTimer >= UPDATE_INTERVAL)
        {
            m_fUpdateTimer = 0;
            UpdateNetworkStatus();
            RefreshMemberCards();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void Cleanup()
    {
        // Save state to static before cleanup
        if (m_MapView)
        {
            s_fZoom = m_MapView.GetZoom();
            s_vCenter = m_MapView.GetCenter();
        }
        
        // Cleanup self marker
        if (m_wSelfMapMarker)
        {
            m_wSelfMapMarker.RemoveFromHierarchy();
            m_wSelfMapMarker = null;
        }
        
        // Cleanup member map markers
        foreach (RplId id, Widget marker : m_mMemberMarkers)
        {
            if (marker)
                marker.RemoveFromHierarchy();
        }
        m_mMemberMarkers.Clear();
        
        // Cleanup member cards
        foreach (Widget card : m_aMemberCards)
        {
            if (card)
                card.RemoveFromHierarchy();
        }
        m_aMemberCards.Clear();
        m_aCachedMemberIds.Clear();
        
        m_MapView = null;
    }
    
    //------------------------------------------------------------------------------------------------
    // STATIC STATE ACCESSORS - changes affect all instances
    //------------------------------------------------------------------------------------------------
    
    AG0_TDLMapView GetMapView()
    {
        return m_MapView;
    }
    
    static void SetPlayerTracking(bool tracking)
    {
        s_bPlayerTracking = tracking;
    }
    
    static bool GetPlayerTracking()
    {
        return s_bPlayerTracking;
    }
    
    static void SetTrackUp(bool trackUp)
    {
        s_bTrackUp = trackUp;
    }
    
    static bool GetTrackUp()
    {
        return s_bTrackUp;
    }
    
    //------------------------------------------------------------------------------------------------
    // STATIC PANEL STATE ACCESSORS
    //------------------------------------------------------------------------------------------------
    
    static void SetPanelState(bool sideVisible, bool networkVisible, bool detailVisible, bool settingsVisible, string title)
    {
        s_bSidePanelVisible = sideVisible;
        s_bNetworkContentVisible = networkVisible;
        s_bDetailContentVisible = detailVisible;
        s_bSettingsContentVisible = settingsVisible;
        s_sPanelTitle = title;
    }
    
    static bool GetSidePanelVisible()
    {
        return s_bSidePanelVisible;
    }
    
    //------------------------------------------------------------------------------------------------
    // Apply static panel state to this instance's widgets
    void ApplyPanelState()
    {
        if (m_wSidePanel)
            m_wSidePanel.SetVisible(s_bSidePanelVisible);
        
        if (m_wNetworkContent)
            m_wNetworkContent.SetVisible(s_bNetworkContentVisible);
        
        if (m_wDetailContent)
            m_wDetailContent.SetVisible(s_bDetailContentVisible);
        
        if (m_wSettingsContent)
            m_wSettingsContent.SetVisible(s_bSettingsContentVisible);
        
        if (m_wPanelTitle)
            m_wPanelTitle.SetText(s_sPanelTitle);
    }
    
    //------------------------------------------------------------------------------------------------
    // Get member cards array so menu can add click handlers
    array<Widget> GetMemberCards()
    {
        return m_aMemberCards;
    }
    
    array<RplId> GetMemberCardIds()
    {
        return m_aCachedMemberIds;
    }
    
    //------------------------------------------------------------------------------------------------
    // PROTECTED IMPLEMENTATION
    //------------------------------------------------------------------------------------------------
    
    protected void ApplySelfPanelColors()
    {
        if (m_wCallsign) m_wCallsign.SetColor(COLOR_CYAN);
        if (m_wGrid) m_wGrid.SetColor(COLOR_CYAN);
        if (m_wAltitude) m_wAltitude.SetColor(COLOR_CYAN);
        if (m_wHeading) m_wHeading.SetColor(COLOR_CYAN);
        if (m_wSpeed) m_wSpeed.SetColor(COLOR_CYAN);
        if (m_wError) m_wError.SetColor(COLOR_CYAN);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMapView(float tDelta)
    {
        if (!m_MapView)
            return;
        
        IEntity player = GetGame().GetPlayerController().GetControlledEntity();
        if (!player)
            return;
        
        // Use STATIC state for tracking/rotation
        if (s_bPlayerTracking)
            m_MapView.CenterOnPlayer();
        
        if (s_bTrackUp)
        {
            vector angles = player.GetYawPitchRoll();
            m_MapView.SetTrackUp(angles[0]);
        }
        else
        {
            m_MapView.SetRotation(0);
        }
        
        // Update heading indicator
        ImageWidget headingImg = ImageWidget.Cast(m_wHeadingIndicator);
        if (headingImg)
            headingImg.SetRotation(m_MapView.GetRotation());
        
		// Feed shapes into map view from player controller
		SCR_PlayerController controller = SCR_PlayerController.Cast(
			GetGame().GetPlayerController()
		);
		if (controller)
		{
			AG0_TDLMapShapeManager shapeMgr = controller.GetTDLShapeManager();
			if (shapeMgr)
				m_MapView.SetShapes(shapeMgr.GetShapes());
			else
				m_MapView.SetShapes(null);
		}
		else
		{
			m_MapView.SetShapes(null);
		}
		
        // Draw map
        m_MapView.Draw();
        
        // Update markers
        UpdateSelfMapMarker(player);
        UpdateMemberMapMarkers();
        
        // Keep static state in sync
        s_fZoom = m_MapView.GetZoom();
        s_vCenter = m_MapView.GetCenter();
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
        
        // Callsign
        if (m_wCallsign)
            m_wCallsign.SetText(GetPlayerCallsign());
        
        // Grid
        if (m_wGrid)
            m_wGrid.SetText(AG0_MGRSGridUtils.GetFullMGRS(pos, 5));
        
        // Altitude
        if (m_wAltitude)
            m_wAltitude.SetTextFormat("%1 MSL", Math.Round(pos[1]).ToString());
        
        // Heading
        if (m_wHeading)
        {
            float hdg = angles[0];
            if (hdg < 0) hdg += 360;
            m_wHeading.SetTextFormat("%1°M", Math.Round(hdg).ToString());
        }
        
        // Speed
        if (m_wSpeed)
        {
            Physics phys = player.GetPhysics();
            if (phys)
            {
                float speed = phys.GetVelocity().Length() * 2.237;
                m_wSpeed.SetTextFormat("%1 MPH", Math.Round(speed).ToString());
            }
            else
            {
                m_wSpeed.SetText("-- MPH");
            }
        }
        
        // Error
        if (m_wError)
            m_wError.SetText("+/- 5m");
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateNetworkStatus()
    {
        AG0_TDLNetworkMembers memberData = GetNetworkMembersFromController();
        
        if (m_wDeviceName)
            m_wDeviceName.SetText(GetPlayerCallsign());
        
        if (m_wNetworkStatus)
        {
            if (memberData && memberData.Count() > 0)
                m_wNetworkStatus.SetTextFormat("CONNECTED (%1 nodes)", memberData.Count());
            else
                m_wNetworkStatus.SetText("NO NETWORK");
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateSelfMapMarker(IEntity player)
    {
        if (!m_wSelfMapMarker || !m_MapView || !m_wMarkerOverlay)
            return;
        
        vector playerPos = player.GetOrigin();
        float playerHeading = player.GetYawPitchRoll()[0];
        
        float layoutX, layoutY;
        m_MapView.WorldToLayout(playerPos, layoutX, layoutY);
        
        FrameSlot.SetPos(m_wSelfMapMarker, layoutX, layoutY);
        
        ImageWidget markerImage = ImageWidget.Cast(m_wSelfMapMarker.FindAnyWidget("MarkerImage"));
        if (markerImage)
        {
            float markerRotation = playerHeading + m_MapView.GetRotation();
            markerImage.SetRotation(markerRotation);
        }
        
        m_wSelfMapMarker.SetVisible(true);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMemberMapMarkers()
    {
        if (!m_MapView || !m_wMarkerOverlay)
            return;
        
        set<RplId> ownDeviceIds = GetPlayerOwnDeviceIds();
        array<ref AG0_TDLNetworkMember> members = GetMembersArray();
        
        ref set<RplId> processedIds = new set<RplId>();
        
        // Get canvas bounds
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        float canvasW, canvasH;
        m_wMapCanvas.GetScreenSize(canvasW, canvasH);
        float layoutCanvasW = workspace.DPIUnscale(canvasW);
        float layoutCanvasH = workspace.DPIUnscale(canvasH);
        float margin = MARKER_SIZE;
        
        foreach (AG0_TDLNetworkMember member : members)
        {
            RplId memberId = member.GetRplId();
            
            if (ownDeviceIds.Contains(memberId))
                continue;
            
            vector memberPos = member.GetPosition();
            float layoutX, layoutY;
            m_MapView.WorldToLayout(memberPos, layoutX, layoutY);
            
            bool isVisible = (layoutX >= -margin && layoutX <= layoutCanvasW + margin &&
                              layoutY >= -margin && layoutY <= layoutCanvasH + margin);
            
            if (!isVisible)
            {
                if (m_mMemberMarkers.Contains(memberId))
                {
                    Widget marker = m_mMemberMarkers.Get(memberId);
                    if (marker)
                        marker.SetVisible(false);
                }
                continue;
            }
            
            processedIds.Insert(memberId);
            
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
            
            FrameSlot.SetPos(marker, layoutX, layoutY);
            marker.SetVisible(true);
            
            TextWidget label = TextWidget.Cast(marker.FindAnyWidget("DeviceIdentifier"));
            if (label)
                label.SetText(member.GetPlayerName());
        }
        
        // Cleanup orphaned
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
    protected Widget CreateMemberMapMarker(AG0_TDLNetworkMember member)
    {
        Widget marker = GetGame().GetWorkspace().CreateWidgets(MEMBER_MARKER_LAYOUT, m_wMarkerOverlay);
        if (!marker)
            return null;
        
        TextWidget label = TextWidget.Cast(marker.FindAnyWidget("DeviceIdentifier"));
        if (label)
            label.SetText(member.GetPlayerName());
        
        return marker;
    }
    
    //------------------------------------------------------------------------------------------------
    // MEMBER CARDS (sidebar list)
    //------------------------------------------------------------------------------------------------
    
    protected void RefreshMemberCards()
    {
        if (!m_wMemberList)
            return;
        
        array<ref AG0_TDLNetworkMember> members = GetMembersArray();
        
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
        foreach (AG0_TDLNetworkMember member : members)
        {
            Widget card = GetGame().GetWorkspace().CreateWidgets(MEMBER_CARD_LAYOUT, m_wMemberList);
            if (!card)
                continue;
            
            UpdateCardWidgets(card, member);
            m_aMemberCards.Insert(card);
            m_aCachedMemberIds.Insert(member.GetRplId());
        }
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
        if (!card || !member)
            return;
        
        TextWidget nameText = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());
        
        TextWidget ipText = TextWidget.Cast(card.FindAnyWidget("NetworkIP"));
        if (ipText)
            ipText.SetText("192.168.0." + member.GetNetworkIP().ToString());
        
        ImageWidget statusDot = ImageWidget.Cast(card.FindAnyWidget("StatusDot"));
        if (statusDot)
        {
            float signal = member.GetSignalStrength();
            if (signal >= 60)
                statusDot.SetColor(Color.FromRGBA(0, 200, 0, 255));
            else if (signal >= 30)
                statusDot.SetColor(Color.FromRGBA(200, 200, 0, 255));
            else
                statusDot.SetColor(Color.FromRGBA(200, 0, 0, 255));
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPERS
    //------------------------------------------------------------------------------------------------
    
    protected array<ref AG0_TDLNetworkMember> GetMembersArray()
    {
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
        return members;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool HasGPSProvider()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return false;
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device && device.HasCapability(AG0_ETDLDeviceCapability.GPS_PROVIDER))
                return true;
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected string GetPlayerCallsign()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return "UNKNOWN";
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device && device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS))
                return device.GetDisplayName();
        }
        return "UNKNOWN";
    }
    
    //------------------------------------------------------------------------------------------------
    protected AG0_TDLNetworkMembers GetNetworkMembersFromController()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return null;
        return controller.GetAggregatedTDLMembers();
    }
    
    //------------------------------------------------------------------------------------------------
    protected set<RplId> GetPlayerOwnDeviceIds()
    {
        set<RplId> deviceIds = new set<RplId>();
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return deviceIds;
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            RplId deviceId = device.GetDeviceRplId();
            if (deviceId != RplId.Invalid())
                deviceIds.Insert(deviceId);
        }
        return deviceIds;
    }
}