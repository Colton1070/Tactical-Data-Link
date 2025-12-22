[BaseContainerProps()]
class AG0_ATAKPlugin_MPU5 : AG0_ATAKPluginBase
{
    // PTT Overlay (always visible when plugin active)
    protected Widget m_wPTTOverlay;
    protected TextWidget m_wPTTFrequency;
    protected ImageWidget m_wPTTIcon;
    
    // Management Panel (toolbar opens this)
    protected Widget m_wManagementPanel;
    protected Widget m_wNodeList;
    protected TextWidget m_wNetworkName;
    protected TextWidget m_wNodeCount;
    protected ref array<Widget> m_aNodeWidgets = {};
    protected ref array<RplId> m_aNodeRplIds = {};
    
    // Cached references
    protected AG0_TDLDeviceComponent m_MPU5Device;
    protected AG0_TDLRadioComponent m_TDLRadio;
    protected Widget m_MenuRoot;
    
    // Update throttling
    protected float m_fUpdateTimer;
    protected const float UPDATE_INTERVAL = 1.0;
    
    // Layouts
    protected const ResourceName PTT_OVERLAY_LAYOUT = "{RESOURCE}UI/layouts/Menus/TDL/MPU5_PTTOverlay.layout";
    protected const ResourceName MANAGEMENT_PANEL_LAYOUT = "{RESOURCE}UI/layouts/Menus/TDL/MPU5_ManagementPanel.layout";
    protected const ResourceName NODE_ENTRY_LAYOUT = "{RESOURCE}UI/layouts/Menus/TDL/MPU5_NodeEntry.layout";
    
    //------------------------------------------------------------------------------------------------
    // Lifecycle
    //------------------------------------------------------------------------------------------------
    override void OnEnabled()
    {
        m_MPU5Device = AG0_TDLDeviceComponent.Cast(
            GetSourceDevice().FindComponent(AG0_TDLDeviceComponent));
        m_TDLRadio = AG0_TDLRadioComponent.Cast(
            GetSourceDevice().FindComponent(AG0_TDLRadioComponent));
    }
    
    override void OnDisabled()
    {
        m_MPU5Device = null;
        m_TDLRadio = null;
    }
    
    //------------------------------------------------------------------------------------------------
    // Toolbar integration
    //------------------------------------------------------------------------------------------------
    override bool ProvidesToolbarTool() 
    { 
        return true; 
    }
    
    override void OnToolActivated(Widget menuRoot)
    {
        if (m_wManagementPanel)
            CloseManagementPanel();
        else
            OpenManagementPanel(menuRoot);
    }
    
    //------------------------------------------------------------------------------------------------
    // Menu lifecycle
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpened(Widget menuRoot)
    {
        m_MenuRoot = menuRoot;
        
        // Create PTT overlay
        Widget overlayArea = menuRoot.FindAnyWidget("PluginPanelRightSide");
        if (overlayArea)
        {
            m_wPTTOverlay = GetGame().GetWorkspace().CreateWidgets(PTT_OVERLAY_LAYOUT, overlayArea);
            if (m_wPTTOverlay)
            {
                m_wPTTFrequency = TextWidget.Cast(m_wPTTOverlay.FindAnyWidget("Frequency"));
                m_wPTTIcon = ImageWidget.Cast(m_wPTTOverlay.FindAnyWidget("PTTIcon"));
            }
        }
        
        UpdatePTTOverlay();
    }
    
    override void OnMenuClosed()
    {
        CloseManagementPanel();
        
        if (m_wPTTOverlay)
        {
            m_wPTTOverlay.RemoveFromHierarchy();
            m_wPTTOverlay = null;
        }
        
        m_MenuRoot = null;
    }
    
    override void OnMenuUpdate(float tDelta)
    {
        m_fUpdateTimer += tDelta;
        if (m_fUpdateTimer < UPDATE_INTERVAL) 
            return;
        m_fUpdateTimer = 0;
        
        UpdatePTTOverlay();
        
        if (m_wManagementPanel)
            UpdateManagementPanel();
    }
    
    //------------------------------------------------------------------------------------------------
    // PTT Overlay
    //------------------------------------------------------------------------------------------------
    protected void UpdatePTTOverlay()
    {
        if (!m_wPTTOverlay || !m_MPU5Device) 
            return;
        
        if (m_wPTTFrequency)
        {
            int freq = GetMPU5Frequency();
            if (freq > 0)
                m_wPTTFrequency.SetText(string.Format("%1", freq));
            else
                m_wPTTFrequency.SetText("---");
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Management Panel
    //------------------------------------------------------------------------------------------------
    protected void OpenManagementPanel(Widget menuRoot)
    {
        Widget panelArea = menuRoot.FindAnyWidget("PluginToolPanel");
        if (!panelArea) 
            return;
        
        m_wManagementPanel = GetGame().GetWorkspace().CreateWidgets(MANAGEMENT_PANEL_LAYOUT, panelArea);
        if (!m_wManagementPanel) 
            return;
        
        // Cache widget refs
        m_wNodeList = m_wManagementPanel.FindAnyWidget("NodeList");
        m_wNetworkName = TextWidget.Cast(m_wManagementPanel.FindAnyWidget("NetworkName"));
        m_wNodeCount = TextWidget.Cast(m_wManagementPanel.FindAnyWidget("NodeCount"));
        
        // Hook close button
        Widget closeBtn = m_wManagementPanel.FindAnyWidget("CloseButton");
        if (closeBtn)
        {
            SCR_ModularButtonComponent btnComp = SCR_ModularButtonComponent.FindComponent(closeBtn);
            if (btnComp)
                btnComp.m_OnClicked.Insert(OnCloseClicked);
        }
        
        UpdateManagementPanel();
    }
    
    protected void CloseManagementPanel()
    {
        ClearNodeWidgets();
        
        if (m_wManagementPanel)
        {
            m_wManagementPanel.RemoveFromHierarchy();
            m_wManagementPanel = null;
            m_wNodeList = null;
            m_wNetworkName = null;
            m_wNodeCount = null;
        }
    }
    
    protected void OnCloseClicked()
    {
        CloseManagementPanel();
    }
    
    protected void UpdateManagementPanel()
    {
        if (!m_wManagementPanel || !m_MPU5Device) 
            return;
        
        // Update header info
        if (m_wNetworkName)
        {
            if (m_MPU5Device.IsInNetwork())
                m_wNetworkName.SetText(string.Format("Network %1", m_MPU5Device.GetCurrentNetworkID()));
            else
                m_wNetworkName.SetText("Not Connected");
        }
        
        // Rebuild node list
        BuildNodeList();
        
        // Update node count
        if (m_wNodeCount)
            m_wNodeCount.SetText(string.Format("%1 Nodes", m_aNodeWidgets.Count()));
    }
    
    //------------------------------------------------------------------------------------------------
    // Node List
    //------------------------------------------------------------------------------------------------
    protected void BuildNodeList()
    {
        if (!m_wNodeList || !m_MPU5Device) 
            return;
        
        ClearNodeWidgets();
        
        AG0_TDLNetworkMembers members = m_MPU5Device.GetNetworkMembersData();
        if (!members) 
            return;
        
        map<RplId, ref AG0_TDLNetworkMember> memberMap = members.ToMap();
        
        foreach (RplId rplId, AG0_TDLNetworkMember member : memberMap)
        {
            CreateNodeEntry(rplId, member);
        }
    }
    
    protected void CreateNodeEntry(RplId rplId, AG0_TDLNetworkMember member)
    {
        Widget nodeEntry = GetGame().GetWorkspace().CreateWidgets(NODE_ENTRY_LAYOUT, m_wNodeList);
        if (!nodeEntry) 
            return;
        
        // Name
        TextWidget nameText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());
        
        // IP
        TextWidget ipText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeIP"));
        if (ipText)
            ipText.SetText(string.Format("IP: %1", member.GetNetworkIP()));
        
        // Signal
        TextWidget signalText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeSignal"));
        if (signalText)
            signalText.SetText(string.Format("%1%%", member.GetSignalStrength()));
        
        // Capabilities
        TextWidget capsText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeCaps"));
        if (capsText)
            capsText.SetText(GetCapabilityString(member.GetCapabilities()));
        
        // Kick button
        Widget kickBtn = nodeEntry.FindAnyWidget("KickButton");
        if (kickBtn)
        {
            SCR_ModularButtonComponent btnComp = SCR_ModularButtonComponent.FindComponent(kickBtn);
            if (btnComp)
                btnComp.m_OnClicked.Insert(OnKickClicked);
        }
        
        // Track for cleanup and kick lookup
        m_aNodeWidgets.Insert(nodeEntry);
        m_aNodeRplIds.Insert(rplId);
    }
    
    protected void ClearNodeWidgets()
    {
        foreach (Widget w : m_aNodeWidgets)
        {
            if (w)
                w.RemoveFromHierarchy();
        }
        m_aNodeWidgets.Clear();
        m_aNodeRplIds.Clear();
    }
    
    protected string GetCapabilityString(int caps)
    {
        string result = "";
        
        if ((caps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0)
            result += "[GPS]";
        if ((caps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0)
            result += "[CAM]";
        if ((caps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0)
            result += "[DISP]";
        
        if (result.IsEmpty())
            result = "[BASIC]";
        
        return result;
    }
    
    //------------------------------------------------------------------------------------------------
    // Kick Action
    //------------------------------------------------------------------------------------------------
    protected void OnKickClicked(SCR_ModularButtonComponent btn)
    {
        Widget kickButton = btn.GetRootWidget();
        if (!kickButton) 
            return;
        
        Widget nodeEntry = kickButton.GetParent();
        if (!nodeEntry) 
            return;
        
        int idx = m_aNodeWidgets.Find(nodeEntry);
        if (idx < 0 || idx >= m_aNodeRplIds.Count()) 
            return;
        
        RplId targetRplId = m_aNodeRplIds[idx];
        if (!targetRplId.IsValid()) 
            return;
        
        // Request kick through player controller
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (controller)
            controller.RequestKickDevice(targetRplId);
    }
    
    //------------------------------------------------------------------------------------------------
    // Helpers
    //------------------------------------------------------------------------------------------------
    protected int GetMPU5Frequency()
    {
        if (!m_TDLRadio) 
            return 0;
        
        BaseRadioComponent baseRadio = m_TDLRadio.GetRadioComponent();
        if (!baseRadio) 
            return 0;
        
        BaseTransceiver transceiver = baseRadio.GetTransceiver(0);
        if (!transceiver) 
            return 0;
        
        return transceiver.GetFrequency();
    }
}