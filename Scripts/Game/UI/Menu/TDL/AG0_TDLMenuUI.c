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
    protected float m_fUpdateTimer = 0;
    protected const float UPDATE_INTERVAL = 0.5; // 500ms
    
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
        m_InputManager.AddActionListener("OpenTDLMenu", EActionTrigger.DOWN, OnBack); // Same key to close
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
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CreateMemberCard(AG0_TDLNetworkMember member)
    {
        Widget card = GetGame().GetWorkspace().CreateWidgets(MEMBER_CARD_LAYOUT, m_wNetworkGrid);
        if (!card) return;
        
        // Update card content
        TextWidget nameText = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());
            
        TextWidget signalText = TextWidget.Cast(card.FindAnyWidget("SignalStrength"));
        if (signalText)
            signalText.SetTextFormat("%1%%", Math.Round(member.GetSignalStrength()));
            
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
    protected void OnBack()
    {
        Close();
    }
}