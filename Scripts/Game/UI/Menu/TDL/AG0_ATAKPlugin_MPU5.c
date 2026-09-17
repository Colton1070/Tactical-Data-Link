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
    // Parallel to m_aNodeWidgets — captures each spawned entry's source
    // RplId so BuildNodeList can restore gamepad focus to the same
    // member's entry after the periodic refresh tears down and rebuilds.
    // Without this, focus on a node entry gets dropped (typically to a
    // widget outside the panel) every UPDATE_INTERVAL tick.
    protected ref array<RplId> m_aNodeRplIds = {};
    protected ref array<ref AG0_KickButtonClickRelay> m_aKickRelays = {};
    
    // Cached references
    protected AG0_TDLDeviceComponent m_MPU5Device;
    protected AG0_TDLRadioComponent m_TDLRadio;
    protected Widget m_MenuRoot;

    // Which of this MPU5's transceivers the PTT overlay reports. Tracked
    // from the player's active VON entry rather than pinned to 0 — the
    // radio carries two transceivers and VON auto-tune puts the squad
    // frequency on the first one, so a fixed index 0 reported the squad
    // channel no matter which channel the player had keyed.
    protected int m_iDisplayTransceiverIdx;

    // Last string pushed into the PTT text widget. UpdatePTTOverlay runs
    // every frame so a channel switch lands on the next one; the compare
    // keeps that from turning into a SetText per frame.
    protected string m_sLastPTTText;

    // Update throttling
    protected float m_fUpdateTimer;
    protected const float UPDATE_INTERVAL = 1.0;
    
    // Layouts
    protected const ResourceName PTT_OVERLAY_LAYOUT = "{5AC27C972C60DF5B}UI/layouts/Menus/TDL/Plugins/MPU5/PTTOverlay.layout";
    protected const ResourceName MANAGEMENT_PANEL_LAYOUT = "{DE93DDC86DC3A2D8}UI/layouts/Menus/TDL/Plugins/MPU5/MPU5_ManagementPanel.layout";
    protected const ResourceName NODE_ENTRY_LAYOUT = "{B2FCF0951F439103}UI/layouts/Menus/TDL/Plugins/MPU5/MPU5_NodeEntry.layout";
    
    //------------------------------------------------------------------------------------------------
    // Lifecycle
    //------------------------------------------------------------------------------------------------
    override void OnEnabled()
    {
        m_MPU5Device = AG0_TDLDeviceComponent.Cast(
            GetSourceDevice().FindComponent(AG0_TDLDeviceComponent));
        m_TDLRadio = AG0_TDLRadioComponent.Cast(
            GetSourceDevice().FindComponent(AG0_TDLRadioComponent));

        m_iDisplayTransceiverIdx = 0;
        m_sLastPTTText = string.Empty;
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
        // The controller owns the toggle now — RequestPluginPanel(this) opens
        // the side-panel slot the first time and closes it back to the map on
        // a second click. Matches the Menu button's behaviour for consistency
        // across all toolbar usage. Routed through the controller so the same
        // plugin works on both the fullscreen menu and the world-space display.
        if (m_Controller)
            m_Controller.RequestPluginPanel(this);
    }
    
    //------------------------------------------------------------------------------------------------
    // Menu lifecycle
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpened(Widget menuRoot)
    {
        m_MenuRoot = menuRoot;

        Widget overlayArea = menuRoot.FindAnyWidget("PluginPanelRightSide");
        if (overlayArea)
        {
            m_wPTTOverlay = GetGame().GetWorkspace().CreateWidgets(PTT_OVERLAY_LAYOUT, overlayArea);
            if (m_wPTTOverlay)
            {
                // PTTOverlay.layout's freq text widget is named "PTTText".
                // m_wPTTIcon is intentionally left null — the layout's icon
                // widget is unnamed ("Image0") and there are two, so a
                // safe lookup needs a layout rename. Until then the icon
                // stays static and the field is unused.
                m_wPTTFrequency = TextWidget.Cast(m_wPTTOverlay.FindAnyWidget("PTTText"));
            }
        }

        UpdatePTTOverlay();
    }
    
    override void OnMenuClosed()
    {
        // Defensive: if the panel is still up here, the menu didn't run its
        // SetPanelContent(NONE) cleanup path first. Tear down ourselves so
        // we don't leak widgets across menu opens.
        if (m_wManagementPanel)
            OnPanelHidden();

        if (m_wPTTOverlay)
        {
            m_wPTTOverlay.RemoveFromHierarchy();
            m_wPTTOverlay = null;
        }

        m_wPTTFrequency = null;
        m_sLastPTTText = string.Empty;
        m_MenuRoot = null;
    }

    override void OnMenuUpdate(float tDelta)
    {
        // Deliberately outside the UPDATE_INTERVAL gate — the overlay says
        // which channel PTT will key, so lagging a channel switch by up to a
        // second is misleading at exactly the moment it matters. The work is
        // a short pointer walk plus a string compare; the widget is only
        // touched when the reading actually changes.
        UpdatePTTOverlay();

        m_fUpdateTimer += tDelta;
        if (m_fUpdateTimer < UPDATE_INTERVAL)
            return;
        m_fUpdateTimer = 0;

        if (m_wManagementPanel)
            UpdateManagementPanel();
    }
    
    //------------------------------------------------------------------------------------------------
    // PTT Overlay
    //------------------------------------------------------------------------------------------------
    protected void UpdatePTTOverlay()
    {
        if (!m_wPTTOverlay || !m_MPU5Device || !m_wPTTFrequency)
            return;

        RefreshDisplayTransceiverIdx();

        string text = string.Format("CH%1 %2",
            m_iDisplayTransceiverIdx + 1,
            FormatFrequencyText(GetMPU5Frequency(m_iDisplayTransceiverIdx)));

        if (text == m_sLastPTTText)
            return;

        m_sLastPTTText = text;
        m_wPTTFrequency.SetText(text);
    }

    //------------------------------------------------------------------------------------------------
    //! Point m_iDisplayTransceiverIdx at whichever of this MPU5's transceivers
    //! the player currently has keyed.
    //!
    //! Channel selection lives on the local SCR_VONController, not on the
    //! radio — the radio only knows each transceiver's frequency, so there is
    //! no way to ask it which one the player picked. GetActiveEntry is the
    //! same handle AG0_ControlDisplayUnitComponent uses to resolve the keyed
    //! radio.
    //!
    //! The index is left alone when the active entry is direct speech or
    //! belongs to a different radio, so the overlay keeps showing the last
    //! channel this MPU5 was on instead of falling back to CH1 — which is the
    //! squad channel and would read as a live value.
    protected void RefreshDisplayTransceiverIdx()
    {
        IEntity sourceDevice = GetSourceDevice();
        if (!sourceDevice)
            return;

        PlayerController playerController = GetGame().GetPlayerController();
        if (!playerController)
            return;

        SCR_VONController vonController = SCR_VONController.Cast(
            playerController.FindComponent(SCR_VONController));
        if (!vonController)
            return;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(vonController.GetActiveEntry());
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio || radio.GetOwner() != sourceDevice)
            return;

        // VON numbers transceivers from 1.
        int idx = radioEntry.GetTransceiverNumber() - 1;
        if (idx < 0)
            return;

        m_iDisplayTransceiverIdx = idx;
    }

    //------------------------------------------------------------------------------------------------
    //! Format a kHz integer as "30.500 MHz" with three-digit kHz padding.
    //! Returns "---" when the freq is zero / negative (radio not bound or
    //! transceiver missing). Manual leading-zero padding because Enfusion's
    //! string.Format has no width specifier — without it, 30005 kHz would
    //! render as "30.5 MHz" (visually identical to 30500 kHz), which is
    //! exactly the kind of off-by-an-order-of-magnitude bug operators
    //! shouldn't be reading off the overlay.
    protected string FormatFrequencyText(int freqKHz)
    {
        if (freqKHz <= 0)
            return "---";

        int mhz = freqKHz / 1000;
        int khz = freqKHz % 1000;
        string khzStr = khz.ToString();
        if (khz < 10)
            khzStr = "00" + khzStr;
        else if (khz < 100)
            khzStr = "0" + khzStr;

        return string.Format("%1.%2 MHz", mhz, khzStr);
    }
    
    //------------------------------------------------------------------------------------------------
    // Management Panel
    //
    // OnPanelShown / OnPanelHidden are driven by AG0_TDLMenuController.SetPanelContent
    // whenever this plugin enters / exits the PLUGIN_TOOL panel slot. The
    // controller hands us the empty PluginToolPanel widget — we spawn our
    // management layout into it and bind. The controller owns toggle-off; we
    // just clean up.
    //------------------------------------------------------------------------------------------------
    override void OnPanelShown(Widget panelRoot)
    {
        if (!panelRoot)
            return;

        m_wManagementPanel = GetGame().GetWorkspace().CreateWidgets(MANAGEMENT_PANEL_LAYOUT, panelRoot);
        if (!m_wManagementPanel)
            return;

        m_wNodeList = m_wManagementPanel.FindAnyWidget("NodeList");
        m_wNetworkName = TextWidget.Cast(m_wManagementPanel.FindAnyWidget("NetworkName"));
        m_wNodeCount = TextWidget.Cast(m_wManagementPanel.FindAnyWidget("NodeCount"));

        // Hook close button. Routed through RequestPluginPanel so the close
        // button shares the same toggle-off path as a second toolbar click.
        Widget closeBtn = m_wManagementPanel.FindAnyWidget("CloseButton");
        if (closeBtn)
        {
            SCR_ModularButtonComponent btnComp = SCR_ModularButtonComponent.FindComponent(closeBtn);
            if (btnComp)
                btnComp.m_OnClicked.Insert(OnCloseClicked);
        }

        UpdateManagementPanel();
    }

    override void OnPanelHidden()
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
        if (m_Controller)
            m_Controller.RequestPluginPanel(this);
    }
    
    protected void UpdateManagementPanel()
    {
        if (!m_wManagementPanel || !m_MPU5Device) 
            return;
        
        if (m_wNetworkName)
        {
            if (m_MPU5Device.IsInNetwork())
                m_wNetworkName.SetText(string.Format("Network %1", m_MPU5Device.GetCurrentNetworkID()));
            else
                m_wNetworkName.SetText("Not Connected");
        }
        
        BuildNodeList();

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

        // Capture which member's entry the gamepad cursor was focused on
        // before tearing the list down. The rebuild removes the focused
        // widget from the hierarchy, which throws focus to whatever the
        // engine picks next — usually a widget outside the panel.
        // Restoring to the same RplId after rebuild keeps the user where
        // they were navigating.
        RplId focusedRplId = CaptureFocusedNodeRplId();

        ClearNodeWidgets();

        AG0_TDLNetworkMembers members = m_MPU5Device.GetNetworkMembersData();
        if (!members)
            return;

        map<RplId, ref AG0_TDLNetworkMember> memberMap = members.ToMap();

        foreach (RplId rplId, AG0_TDLNetworkMember member : memberMap)
        {
            CreateNodeEntry(rplId, member);
        }

        if (focusedRplId != RplId.Invalid())
            RestoreFocusToNode(focusedRplId);
    }

    //------------------------------------------------------------------------------------------------
    //! Walk up from the currently-focused widget and return the RplId of
    //! the node entry the focus is inside, or RplId.Invalid() if focus is
    //! elsewhere (close button, network name, outside the panel entirely).
    //! Used by BuildNodeList to remember which member's row had focus
    //! before the periodic refresh tears the list down.
    protected RplId CaptureFocusedNodeRplId()
    {
        Widget focused = GetGame().GetWorkspace().GetFocusedWidget();
        if (!focused)
            return RplId.Invalid();

        Widget cur = focused;
        while (cur)
        {
            int idx = m_aNodeWidgets.Find(cur);
            if (idx != -1 && idx < m_aNodeRplIds.Count())
                return m_aNodeRplIds[idx];
            cur = cur.GetParent();
        }
        return RplId.Invalid();
    }

    //------------------------------------------------------------------------------------------------
    //! Restore focus to the node entry for the given RplId, if present in
    //! the freshly-rebuilt list. No-op when the member has left the
    //! network and their entry is gone — focus then stays wherever the
    //! engine moved it during the rebuild (acceptable; the user can
    //! navigate back manually).
    protected void RestoreFocusToNode(RplId rplId)
    {
        int idx = m_aNodeRplIds.Find(rplId);
        if (idx == -1 || idx >= m_aNodeWidgets.Count())
            return;
        Widget w = m_aNodeWidgets[idx];
        if (w)
            GetGame().GetWorkspace().SetFocusedWidget(w);
    }
    
    protected void CreateNodeEntry(RplId rplId, AG0_TDLNetworkMember member)
    {
        Widget nodeEntry = GetGame().GetWorkspace().CreateWidgets(NODE_ENTRY_LAYOUT, m_wNodeList);
        if (!nodeEntry) 
            return;
        
        TextWidget nameText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());

        TextWidget ipText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeIP"));
        if (ipText)
            ipText.SetText(string.Format("IP: %1", member.GetNetworkIP()));

        TextWidget signalText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeSignal"));
        if (signalText)
            signalText.SetText(string.Format("%1%%", member.GetSignalStrength()));

        TextWidget capsText = TextWidget.Cast(nodeEntry.FindAnyWidget("NodeCaps"));
        if (capsText)
            capsText.SetText(GetCapabilityString(member.GetCapabilities()));
        
        // Kick button — bound via a per-row relay that captures the RplId.
        // m_OnClicked is zero-arg-fire, so we can't recover the target from
        // the sender; the relay is the cleanest dispatch.
        //
        // MPU5_NodeEntry.layout names its ROOT widget "KickButton" (i.e. the
        // whole entry is the button), so FindAnyWidget("KickButton") wouldn't
        // find anything — descendants only. Use nodeEntry directly.
        SCR_ModularButtonComponent btnComp = SCR_ModularButtonComponent.FindComponent(nodeEntry);
        if (btnComp)
        {
            AG0_KickButtonClickRelay relay = new AG0_KickButtonClickRelay(rplId);
            m_aKickRelays.Insert(relay);
            btnComp.m_OnClicked.Insert(relay.OnClick);
        }

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
        m_aKickRelays.Clear();
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
    // Helpers
    //------------------------------------------------------------------------------------------------
    protected int GetMPU5Frequency(int transceiverIdx)
    {
        if (!m_TDLRadio)
            return 0;
        // Pull from AG0_TDLRadioComponent.GetCurrentFrequency rather than the
        // raw BaseTransceiver — that wrapper returns the live hopped value
        // when frequency-hop is enabled, falling back to the fixed tsv
        // frequency otherwise. Going through the transceiver directly would
        // always show the base frequency, which is misleading when the
        // radio is mid-hop.
        return m_TDLRadio.GetCurrentFrequency(transceiverIdx);
    }
}