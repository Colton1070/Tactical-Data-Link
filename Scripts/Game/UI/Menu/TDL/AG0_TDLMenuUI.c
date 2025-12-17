//------------------------------------------------------------------------------------------------
// AG0_TDLMenuUI.c
//------------------------------------------------------------------------------------------------
modded enum ChimeraMenuPreset
{
    AG0_TDLMenu
}

enum ETDLMenuView
{
    NETWORK,
    MEMBER_DETAIL
}

class AG0_TDLMenuUI : ChimeraMenuBase
{
    protected ETDLMenuView m_eCurrentView = ETDLMenuView.NETWORK;
    
    // Core references
    protected AG0_TDLDeviceComponent m_ActiveDevice;
    protected Widget m_wRoot;
    protected InputManager m_InputManager;
    
    // Selected member for detail view
    protected ref AG0_TDLNetworkMember m_SelectedMember;
    protected RplId m_SelectedDeviceId;
    
    // Panel widgets
    protected Widget m_wNetworkPanel;
    protected Widget m_wDetailPanel;
    
    // Network panel widgets
    protected Widget m_wScrollContainer;
    protected ScrollLayoutWidget m_wScrollLayout;
    protected Widget m_wNetworkGrid;
    protected TextWidget m_wDeviceName;
    protected TextWidget m_wNetworkStatus;
    
    // Detail panel widgets
    protected TextWidget m_wDetailPlayerName;
    protected TextWidget m_wDetailSignalStrength;
    protected TextWidget m_wDetailNetworkIP;
    protected TextWidget m_wDetailDistance;
    protected TextWidget m_wDetailCapabilities;
    protected Widget m_wViewFeedButton;
    protected Widget m_wBackButton;
    
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
        
        // Find panels
        m_wNetworkPanel = m_wRoot.FindAnyWidget("NetworkPanel");
        m_wDetailPanel = m_wRoot.FindAnyWidget("DetailPanel");
        
        // Network panel widgets
        m_wScrollContainer = m_wRoot.FindAnyWidget("ScrollLayout");
        m_wScrollLayout = ScrollLayoutWidget.Cast(m_wScrollContainer);
        m_wNetworkGrid = m_wRoot.FindAnyWidget("NetworkGrid");
        m_wDeviceName = TextWidget.Cast(m_wRoot.FindAnyWidget("DeviceName"));
        m_wNetworkStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("NetworkStatus"));
        
        // Detail panel widgets
        m_wDetailPlayerName = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailPlayerName"));
        m_wDetailSignalStrength = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailSignalStrength"));
        m_wDetailNetworkIP = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailNetworkIP"));
        m_wDetailDistance = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailDistance"));
        m_wDetailCapabilities = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailCapabilities"));
        m_wViewFeedButton = m_wRoot.FindAnyWidget("ViewFeedButton");
        m_wBackButton = m_wRoot.FindAnyWidget("BackButton");
        
        // Setup detail button handlers
        SetupDetailButtons();
        
        // Find a TDL device
        if (!FindTDLDevice())
        {
            UpdateEmptyState();
            return;
        }
        
        // Start in network view
        ShowNetworkView();
        
        // Initial populate
        UpdateDeviceInfo();
        UpdateMemberGrid();
        
        // Setup input handlers
        m_InputManager.AddActionListener("MenuBack", EActionTrigger.DOWN, OnBack);
        m_InputManager.AddActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnClose);
        
        if (!m_aMemberCards.IsEmpty())
            SetFocusToCard(0);
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuClose()
    {
        if (m_InputManager)
        {
            m_InputManager.RemoveActionListener("MenuBack", EActionTrigger.DOWN, OnBack);
            m_InputManager.RemoveActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnClose);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void SetupDetailButtons()
    {
        if (m_wBackButton)
        {
            ButtonWidget btn = ButtonWidget.Cast(m_wBackButton);
            if (btn)
            {
                AG0_TDLDetailButtonHandler handler = new AG0_TDLDetailButtonHandler();
                handler.Init(this, "back");
                btn.AddHandler(handler);
            }
        }
        
        if (m_wViewFeedButton)
        {
            ButtonWidget btn = ButtonWidget.Cast(m_wViewFeedButton);
            if (btn)
            {
                AG0_TDLDetailButtonHandler handler = new AG0_TDLDetailButtonHandler();
                handler.Init(this, "viewfeed");
                btn.AddHandler(handler);
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // VIEW SWITCHING
    //------------------------------------------------------------------------------------------------
    protected void ShowNetworkView()
    {
        m_eCurrentView = ETDLMenuView.NETWORK;
        
        if (m_wNetworkPanel)
            m_wNetworkPanel.SetVisible(true);
        if (m_wDetailPanel)
            m_wDetailPanel.SetVisible(false);
        
        if (!m_aMemberCards.IsEmpty())
        {
            int idx = Math.Max(0, m_iFocusedCardIndex);
            SetFocusToCard(idx);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ShowDetailView(AG0_TDLNetworkMember member, RplId deviceId)
    {
        m_SelectedMember = member;
        m_SelectedDeviceId = deviceId;
        m_eCurrentView = ETDLMenuView.MEMBER_DETAIL;
        
        if (m_wNetworkPanel)
            m_wNetworkPanel.SetVisible(false);
        if (m_wDetailPanel)
            m_wDetailPanel.SetVisible(true);
        
        PopulateDetailView();
        
        if (m_wBackButton)
            GetGame().GetWorkspace().SetFocusedWidget(m_wBackButton);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void PopulateDetailView()
    {
        if (!m_SelectedMember)
            return;
        
        if (m_wDetailPlayerName)
            m_wDetailPlayerName.SetText(m_SelectedMember.GetPlayerName());
        
        if (m_wDetailSignalStrength)
            m_wDetailSignalStrength.SetTextFormat("Signal: %1%%", Math.Round(m_SelectedMember.GetSignalStrength()));
        
        if (m_wDetailNetworkIP)
            m_wDetailNetworkIP.SetTextFormat("IP: %1", m_SelectedMember.GetNetworkIP());
        
        if (m_wDetailDistance && m_ActiveDevice)
        {
            float dist = m_ActiveDevice.GetDistanceToMember(m_SelectedDeviceId);
            m_wDetailDistance.SetTextFormat("Distance: %1m", Math.Round(dist));
        }
        
        if (m_wDetailCapabilities)
            m_wDetailCapabilities.SetText(BuildCapabilitiesString(m_SelectedMember.GetCapabilities()));
        
        if (m_wViewFeedButton)
        {
            bool canView = CanViewFeed();
            m_wViewFeedButton.SetVisible(canView);
            m_wViewFeedButton.SetEnabled(canView);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected string BuildCapabilitiesString(int capFlags)
    {
        string caps = "";
        
        if (capFlags & AG0_ETDLDeviceCapability.GPS_PROVIDER)
            caps += "[GPS] ";
        if (capFlags & AG0_ETDLDeviceCapability.VIDEO_SOURCE)
            caps += "[CAM] ";
        if (capFlags & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT)
            caps += "[DISP] ";
        if (capFlags & AG0_ETDLDeviceCapability.INFORMATION)
            caps += "[INFO] ";
        
        return caps;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool CanViewFeed()
    {
        if (!m_SelectedMember)
            return false;
        
        if (!(m_SelectedMember.GetCapabilities() & AG0_ETDLDeviceCapability.VIDEO_SOURCE))
            return false;
        
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return false;
        
        return pc.IsSourceBroadcasting(m_SelectedDeviceId);
    }
    
    //------------------------------------------------------------------------------------------------
    // PUBLIC BUTTON HANDLERS (called by AG0_TDLDetailButtonHandler)
    //------------------------------------------------------------------------------------------------
    void OnViewFeedClicked()
    {
        Print("TDL_MENU: View Feed clicked", LogLevel.DEBUG);
        
        RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_SelectedDeviceId));
        if (!rpl)
        {
            Print("TDL_MENU: Could not resolve RplId", LogLevel.WARNING);
            return;
        }
        
        AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
            rpl.GetEntity().FindComponent(AG0_TDLDeviceComponent));
        
        if (!device)
        {
            Print("TDL_MENU: Could not find TDL device", LogLevel.WARNING);
            return;
        }
        
        AG0_PlayerCameraOverrideComponent camOverride = GetPlayerCameraOverride();
        if (!camOverride)
        {
            Print("TDL_MENU: No camera override component", LogLevel.WARNING);
            return;
        }
        
        camOverride.SetViewedDevice(device);
        Close();
    }
    
    //------------------------------------------------------------------------------------------------
    void OnDetailBackClicked()
    {
        ShowNetworkView();
    }
    
    //------------------------------------------------------------------------------------------------
    protected AG0_PlayerCameraOverrideComponent GetPlayerCameraOverride()
    {
        CameraManager camMgr = GetGame().GetCameraManager();
        if (!camMgr)
            return null;
        
        CameraBase camera = camMgr.CurrentCamera();
        if (!camera)
            return null;
        
        return AG0_PlayerCameraOverrideComponent.Cast(
            camera.FindComponent(AG0_PlayerCameraOverrideComponent));
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnBack()
    {
        if (m_eCurrentView == ETDLMenuView.MEMBER_DETAIL)
        {
            ShowNetworkView();
        }
        else
        {
            Close();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnClose()
    {
        Close();
    }
    
    //------------------------------------------------------------------------------------------------
    // DEVICE AND DATA MANAGEMENT
    //------------------------------------------------------------------------------------------------
    protected bool FindTDLDevice()
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return false;
        
        array<AG0_TDLDeviceComponent> devices = pc.GetPlayerTDLDevices();
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.IsPowered() && 
                device.HasCapability(AG0_ETDLDeviceCapability.INFORMATION) &&
                device.HasCapability(AG0_ETDLDeviceCapability.DISPLAY_OUTPUT))
            {
                m_ActiveDevice = device;
                return true;
            }
        }
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateDeviceInfo()
    {
        if (!m_ActiveDevice)
            return;
        
        if (m_wDeviceName)
        {
            string deviceName = "TDL Device";
            if (m_ActiveDevice.GetOwner())
                deviceName = m_ActiveDevice.GetOwner().GetName();
            m_wDeviceName.SetText(deviceName);
        }
        
        if (m_wNetworkStatus)
        {
            if (m_ActiveDevice.IsInNetwork())
            {
                int networkId = m_ActiveDevice.GetCurrentNetworkID();
                int memberCount = m_ActiveDevice.GetConnectedMembers().Count();
                m_wNetworkStatus.SetTextFormat("Network %1 - %2 members", networkId, memberCount);
            }
            else
            {
                m_wNetworkStatus.SetText("Not connected");
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateEmptyState()
    {
        if (m_wDeviceName)
            m_wDeviceName.SetText("No TDL Device");
            
        if (m_wNetworkStatus)
            m_wNetworkStatus.SetText("No compatible device found");
            
        if (m_wNetworkGrid)
        {
            Widget child = m_wNetworkGrid.GetChildren();
            while (child)
            {
                Widget next = child.GetSibling();
                child.RemoveFromHierarchy();
                child = next;
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // MEMBER GRID
    //------------------------------------------------------------------------------------------------
    protected void UpdateMemberGrid()
    {
        if (!m_wNetworkGrid || !m_ActiveDevice) return;
        
        Widget child = m_wNetworkGrid.GetChildren();
        while (child)
        {
            Widget next = child.GetSibling();
            child.RemoveFromHierarchy();
            child = next;
        }
        m_aMemberCards.Clear();
        
        if (!m_ActiveDevice.HasNetworkMemberData()) return;
        
        AG0_TDLNetworkMembers membersData = m_ActiveDevice.GetNetworkMembersData();
        if (!membersData) return;
        
        int count = membersData.Count();
        for (int i = 0; i < count; i++)
        {
            AG0_TDLNetworkMember member = membersData.Get(i);
            if (!member) continue;
            
            CreateMemberCard(member, i);
        }
        
        if (!m_aMemberCards.IsEmpty() && m_iFocusedCardIndex >= 0)
        {
            int newIndex = Math.Min(m_iFocusedCardIndex, m_aMemberCards.Count() - 1);
            SetFocusToCard(newIndex);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CreateMemberCard(AG0_TDLNetworkMember member, int index)
    {
        Widget card = GetGame().GetWorkspace().CreateWidgets(MEMBER_CARD_LAYOUT, m_wNetworkGrid);
        if (!card) return;
        
        int col = index % 2;
        int row = index / 2;
        GridSlot.SetColumn(card, col);
        GridSlot.SetRow(card, row);
        
        GridLayoutWidget grid = GridLayoutWidget.Cast(m_wNetworkGrid);
        if (grid)
        {
            grid.SetRowFillWeight(row, 1);
            grid.SetColumnFillWeight(col, 1);
        }
        
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
    protected void UpdateCardWidgets(Widget card, AG0_TDLNetworkMember member)
    {
        if (!card || !member) return;
        
        TextWidget nameText = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());
        
        TextWidget signalText = TextWidget.Cast(card.FindAnyWidget("SignalStrength"));
        if (signalText)
            signalText.SetTextFormat("%1%%", Math.Round(member.GetSignalStrength()));
        
        TextWidget ipText = TextWidget.Cast(card.FindAnyWidget("NetworkIP"));
        if (ipText)
            ipText.SetTextFormat("IP: %1", member.GetNetworkIP());
        
        TextWidget distText = TextWidget.Cast(card.FindAnyWidget("Distance"));
        if (distText && m_ActiveDevice)
        {
            float dist = m_ActiveDevice.GetDistanceToMember(member.GetRplId());
            distText.SetTextFormat("%1m", Math.Round(dist));
        }
        
        TextWidget capsText = TextWidget.Cast(card.FindAnyWidget("Capabilities"));
        if (capsText)
            capsText.SetText(BuildCapabilitiesString(member.GetCapabilities()));
    }
    
    //------------------------------------------------------------------------------------------------
    // FOCUS MANAGEMENT
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
        
        if (!m_ActiveDevice || !m_ActiveDevice.HasNetworkMemberData())
            return;
        
        AG0_TDLNetworkMember member = m_ActiveDevice.GetNetworkMembersData().GetByRplId(memberId);
        if (member)
            PrintFormat("TDL Menu: Focused on member %1", member.GetPlayerName());
    }
    
    //------------------------------------------------------------------------------------------------
    void OnMemberCardClicked(RplId memberId, int button)
    {
        if (!m_ActiveDevice || !m_ActiveDevice.HasNetworkMemberData())
            return;
        
        AG0_TDLNetworkMember member = m_ActiveDevice.GetNetworkMembersData().GetByRplId(memberId);
        if (!member)
            return;
        
        PrintFormat("TDL Menu: Selected member %1", member.GetPlayerName());
        
        ShowDetailView(member, memberId);
    }
}