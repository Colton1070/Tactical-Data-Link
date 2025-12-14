modded enum ChimeraMenuPreset
{
	AG0_TDLMenu
}

//------------------------------------------------------------------------------------------------
//! TDL ATAK-style interface menu
class AG0_TDLMenuUI : ChimeraMenuBase
{
    // Core references
    protected AG0_TDLDeviceComponent m_ActiveDevice;
    protected Widget m_wRoot;
    protected InputManager m_InputManager;
    
    // Widget references
	protected Widget m_wScrollContainer;
	protected ScrollLayoutWidget m_wScrollLayout;
    protected Widget m_wNetworkGrid;
    protected TextWidget m_wDeviceName;
    protected TextWidget m_wNetworkStatus;
    
    // State tracking
    protected ref array<RplId> m_aCachedMemberIds = {};
    protected ref array<Widget> m_aMemberCards = {};
    protected float m_fUpdateTimer = 0;
    protected const float UPDATE_INTERVAL = 0.5; // 500ms
    protected int m_iFocusedCardIndex = -1;
    
    // Layout paths
    protected const ResourceName MEMBER_CARD_LAYOUT = "{7C025C99261C96C5}UI/layouts/Menus/TDL/TDLMemberCardUI.layout";
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen()
    {
        m_wRoot = GetRootWidget();
        m_InputManager = GetGame().GetInputManager();
        
        // Find our UI elements
		m_wScrollContainer = m_wRoot.FindAnyWidget("ScrollLayout");
		m_wScrollLayout = ScrollLayoutWidget.Cast(m_wScrollContainer);
        m_wNetworkGrid = m_wRoot.FindAnyWidget("NetworkGrid");
        m_wDeviceName = TextWidget.Cast(m_wRoot.FindAnyWidget("DeviceName"));
        m_wNetworkStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("NetworkStatus"));
        
        // Find a TDL device
        if (!FindTDLDevice())
        {
            // Show empty state
            UpdateEmptyState();
            return;
        }
        
        // Initial populate
        UpdateDeviceInfo();
        UpdateMemberGrid();
        
        // Setup input handlers - both for closing and navigation
        m_InputManager.AddActionListener("MenuBack", EActionTrigger.DOWN, OnBack);
        m_InputManager.AddActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnBack);
        
        // Set initial focus to first card if we have any
        if (!m_aMemberCards.IsEmpty())
            SetFocusToCard(0);
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuClose()
    {
        if (m_InputManager)
        {
            m_InputManager.RemoveActionListener("MenuBack", EActionTrigger.DOWN, OnBack);
            m_InputManager.RemoveActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnBack);
        }
        
        // Clear references
        m_ActiveDevice = null;
        m_aCachedMemberIds.Clear();
        m_aMemberCards.Clear();
        m_iFocusedCardIndex = -1;
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuUpdate(float tDelta)
    {
        if (!m_ActiveDevice || !m_ActiveDevice.IsPowered())
        {
            Close();
            return;
        }
        
        m_fUpdateTimer += tDelta;
        if (m_fUpdateTimer >= UPDATE_INTERVAL)
        {
            m_fUpdateTimer = 0;
            
            // Check if members changed
            array<RplId> currentMembers = m_ActiveDevice.GetConnectedMembers();
            if (HasMembershipChanged(currentMembers))
            {
                m_aCachedMemberIds = currentMembers;
                UpdateMemberGrid();
            }
            else
            {
                // Just update existing card data without recreating
                UpdateMemberCardData();
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool FindTDLDevice()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerController()
	    );
        if (!controller) return false;
        
        // Use the player controller's method to get devices
        array<AG0_TDLDeviceComponent> devices = controller.GetPlayerTDLDevices();
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device && device.IsPowered() && 
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
        if (!m_ActiveDevice) return;
        
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
            
        // Clear grid
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
    protected void UpdateMemberGrid()
    {
        if (!m_wNetworkGrid || !m_ActiveDevice) return;
        
        // Clear existing cards
        Widget child = m_wNetworkGrid.GetChildren();
        while (child)
        {
            Widget next = child.GetSibling();
            child.RemoveFromHierarchy();
            child = next;
        }
        m_aMemberCards.Clear();
        
        // Get member data if available
        if (!m_ActiveDevice.HasNetworkMemberData()) return;
        
        AG0_TDLNetworkMembers membersData = m_ActiveDevice.GetNetworkMembersData();
        if (!membersData) return;
        
        // Create cards for each member
        int count = membersData.Count();
        for (int i = 0; i < count; i++)
        {
            AG0_TDLNetworkMember member = membersData.Get(i);
            if (!member) continue;
            
            CreateMemberCard(member);
        }
        
        // Set focus to first card if we lost focus during refresh
        if (!m_aMemberCards.IsEmpty() && m_iFocusedCardIndex >= 0)
        {
            int newIndex = Math.Min(m_iFocusedCardIndex, m_aMemberCards.Count() - 1);
            SetFocusToCard(newIndex);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CreateMemberCard(AG0_TDLNetworkMember member)
    {
        Widget card = GetGame().GetWorkspace().CreateWidgets(MEMBER_CARD_LAYOUT, m_wNetworkGrid);
        if (!card) return;
        
        // Attach button handler
        ButtonWidget button = ButtonWidget.Cast(card);
        if (button)
        {
            AG0_TDLMemberCardHandler handler = new AG0_TDLMemberCardHandler();
            handler.Init(this, member.GetRplId(), member);
            button.AddHandler(handler);
        }
        
        // Update card content
        UpdateCardWidgets(card, member);
        
        m_aMemberCards.Insert(card);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateCardWidgets(Widget card, AG0_TDLNetworkMember member)
    {
        if (!card || !member) return;
        
        // Update name
        TextWidget nameText = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());
        
        // Update signal strength
        TextWidget signalText = TextWidget.Cast(card.FindAnyWidget("SignalStrength"));
        if (signalText)
            signalText.SetTextFormat("%1%%", Math.Round(member.GetSignalStrength()));
        
        // Update network IP
        TextWidget ipText = TextWidget.Cast(card.FindAnyWidget("NetworkIP"));
        if (ipText)
            ipText.SetTextFormat("IP: %1", member.GetNetworkIP());
        
        // Update distance (calculate from player position)
        TextWidget distText = TextWidget.Cast(card.FindAnyWidget("Distance"));
        if (distText && m_ActiveDevice)
        {
            float distance = vector.Distance(
                m_ActiveDevice.GetOwner().GetOrigin(), 
                member.GetPosition()
            );
            distText.SetTextFormat("%1m", Math.Round(distance));
        }
        
        // Update capabilities
        TextWidget capText = TextWidget.Cast(card.FindAnyWidget("Capabilities"));
        if (capText)
        {
            string caps = "";
            int capabilities = member.GetCapabilities();
            
            if (capabilities & AG0_ETDLDeviceCapability.GPS_PROVIDER)
                caps += "[GPS] ";
            if (capabilities & AG0_ETDLDeviceCapability.VIDEO_SOURCE)
                caps += "[CAM] ";
            if (capabilities & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT)
                caps += "[DISP] ";
                
            capText.SetText(caps);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Just update the data in existing cards without recreating them
    protected void UpdateMemberCardData()
    {
        if (!m_ActiveDevice || !m_ActiveDevice.HasNetworkMemberData())
            return;
        
        AG0_TDLNetworkMembers membersData = m_ActiveDevice.GetNetworkMembersData();
        if (!membersData) return;
        
        int count = Math.Min(m_aMemberCards.Count(), membersData.Count());
        for (int i = 0; i < count; i++)
        {
            AG0_TDLNetworkMember member = membersData.Get(i);
            if (!member) continue;
            
            UpdateCardWidgets(m_aMemberCards[i], member);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool HasMembershipChanged(array<RplId> newMembers)
    {
        if (newMembers.Count() != m_aCachedMemberIds.Count())
            return true;
            
        foreach (RplId id : newMembers)
        {
            if (!m_aCachedMemberIds.Contains(id))
                return true;
        }
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    // Set focus to a specific card index
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
    // Called by card handler when a card gains focus
    void OnMemberCardFocused(RplId memberId, AG0_TDLNetworkMember memberData)
    {
        // Update focused index
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
        
        // TODO: Update detail panel or preview when card is focused
        PrintFormat("TDL Menu: Focused on member %1", memberData.GetPlayerName());
    }
    
    //------------------------------------------------------------------------------------------------
    // Called by card handler when a card is clicked
    void OnMemberCardClicked(RplId memberId, AG0_TDLNetworkMember memberData, int button)
    {
        PrintFormat("TDL Menu: Clicked member %1 (button %2)", memberData.GetPlayerName(), button);
        
        // TODO: Open member detail page with options:
        // - View video feed (if VIDEO_SOURCE capability)
        // - Send waypoint
        // - Send message
        // - Request position update
        
        if (memberData.GetCapabilities() & AG0_ETDLDeviceCapability.VIDEO_SOURCE)
        {
            PrintFormat("  Member has camera - could switch to their feed");
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnBack()
    {
        Close();
    }
}