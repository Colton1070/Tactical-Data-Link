//------------------------------------------------------------------------------------------------
//! Shared TDL menu controller.
//!
//! Owns the side-panel state machine (which content is active, who the
//! selected member is, which plugin owns the plugin slot) and the navigation
//! button handlers (Network / Back / Settings / MarkerTool / Settings Back).
//! Both the full-screen menu (AG0_TDLMenuUI) and the world-space device
//! display (TDL_WorldSpaceDisplayComponent) instantiate one per frontend so
//! the same layout — TDLMenuUI.layout — drives the same panel logic on both.
//!
//! State persistence happens via class-level statics so the same panel /
//! selection / plugin restoration survives menu open/close.
//!
//! Frontend-specific side effects (gamepad focus, chat view repopulation,
//! marker tool sub-panel lifecycle, crosshair visibility) are NOT in the
//! controller — frontends subscribe to m_OnPanelChanged and handle their
//! own widgets in response to the state transition.
//------------------------------------------------------------------------------------------------
class AG0_TDLMenuController
{
    // ============================================
    // PERSISTENT STATE — survives menu open/close
    // ============================================
    static protected ETDLPanelContent s_eLastPanel = ETDLPanelContent.NETWORK_LIST;
    static protected RplId s_LastSelectedDeviceId = RplId.Invalid();
    static protected RplId s_LastChatContactRplId = RplId.Invalid();
    static protected string s_sLastChatContactName;
    static protected string s_sLastPanelPluginID;

    static ETDLPanelContent GetLastPanel() { return s_eLastPanel; }
    static void SetLastPanel(ETDLPanelContent p) { s_eLastPanel = p; }
    static RplId GetLastSelectedDeviceId() { return s_LastSelectedDeviceId; }
    static void SetLastSelectedDeviceId(RplId id) { s_LastSelectedDeviceId = id; }
    static RplId GetLastChatContactRplId() { return s_LastChatContactRplId; }
    static void SetLastChatContactRplId(RplId id) { s_LastChatContactRplId = id; }
    static string GetLastChatContactName() { return s_sLastChatContactName; }
    static void SetLastChatContactName(string n) { s_sLastChatContactName = n; }
    static string GetLastPanelPluginID() { return s_sLastPanelPluginID; }
    static void SetLastPanelPluginID(string id) { s_sLastPanelPluginID = id; }

    // ============================================
    // INSTANCE STATE
    // ============================================
    protected Widget m_wRoot;
    protected ETDLPanelContent m_eActivePanel = ETDLPanelContent.NETWORK_LIST;
    protected AG0_ATAKPluginBase m_ActivePanelPlugin;
    protected AG0_TDLNetworkMember m_SelectedMember;
    protected RplId m_SelectedDeviceId;
    protected RplId m_ChatContactRplId;
    protected string m_sChatContactName;

    // ============================================
    // CACHED WIDGET REFS
    // ============================================
    // Panel structure
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wNetworkContent;
    protected Widget m_wDetailContent;
    protected Widget m_wSettingsContent;
    protected Widget m_wMarkerToolContent;
    protected Widget m_wChatContent;
    protected Widget m_wPluginToolPanel;

    // Detail content widgets
    protected TextWidget m_wDetailPlayerName;
    protected TextWidget m_wDetailSignalStrength;
    protected TextWidget m_wDetailNetworkIP;
    protected TextWidget m_wDetailGrid;
    protected TextWidget m_wDetailDistance;
    protected TextWidget m_wDetailCapabilities;
    protected Widget m_wViewFeedButton;

    // Navigation buttons (we own these handlers)
    protected Widget m_wNetworkButton;
    protected Widget m_wBackButton;
    protected Widget m_wSettingsButton;
    protected Widget m_wSettingsBackButton;
    protected Widget m_wMarkerToolButton;

    // Optional ref the menu may inject so the controller can update its
    // last-message-preview text after chat send. Not required.
    protected Widget m_wChatContactName;

    // ============================================
    // EVENTS
    // ============================================
    //! Fired after SetPanelContent settles. Frontend handlers query
    //! GetActivePanel() (or other accessors) and do whatever frontend-specific
    //! work they need — chat view repopulation, marker tool sub-panel
    //! lifecycle, crosshair visibility, gamepad focus, etc.
    ref ScriptInvoker m_OnPanelChanged = new ScriptInvoker();

    //! Fired when ShowDetailView completes. Menu listens to refresh detail
    //! widgets the controller didn't populate (e.g. view-feed button visibility).
    ref ScriptInvoker m_OnDetailShown = new ScriptInvoker();

    // ============================================
    // ACCESSORS
    // ============================================
    Widget GetRoot() { return m_wRoot; }
    ETDLPanelContent GetActivePanel() { return m_eActivePanel; }
    AG0_ATAKPluginBase GetActivePanelPlugin() { return m_ActivePanelPlugin; }
    AG0_TDLNetworkMember GetSelectedMember() { return m_SelectedMember; }
    RplId GetSelectedDeviceId() { return m_SelectedDeviceId; }
    RplId GetChatContactRplId() { return m_ChatContactRplId; }
    string GetChatContactName() { return m_sChatContactName; }

    void SetSelectedMember(AG0_TDLNetworkMember m) { m_SelectedMember = m; }
    void SetSelectedDeviceId(RplId id) { m_SelectedDeviceId = id; }
    void SetChatContact(RplId id, string name)
    {
        m_ChatContactRplId = id;
        m_sChatContactName = name;
        // Eager persist — chat state is restored on next menu open even if
        // the menu didn't run a normal save path.
        s_LastChatContactRplId = id;
        s_sLastChatContactName = name;
    }

    // ============================================
    // INIT / CLEANUP
    // ============================================
    bool Init(Widget root)
    {
        if (!root)
            return false;
        m_wRoot = root;

        // Panel structure
        m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
        m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
        m_wNetworkContent = m_wRoot.FindAnyWidget("NetworkContent");
        m_wDetailContent = m_wRoot.FindAnyWidget("DetailContent");
        m_wSettingsContent = m_wRoot.FindAnyWidget("SettingsContent");
        m_wMarkerToolContent = m_wRoot.FindAnyWidget("MarkerToolContent");
        m_wChatContent = m_wRoot.FindAnyWidget("ChatContent");
        m_wPluginToolPanel = m_wRoot.FindAnyWidget("PluginToolPanel");

        // Detail content widgets
        m_wDetailPlayerName = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailPlayerName"));
        m_wDetailSignalStrength = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailSignalStrength"));
        m_wDetailNetworkIP = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailNetworkIP"));
        m_wDetailGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailGrid"));
        m_wDetailDistance = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailDistance"));
        m_wDetailCapabilities = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailCapabilities"));
        m_wViewFeedButton = m_wRoot.FindAnyWidget("ViewFeedButton");

        // Navigation buttons we own
        m_wNetworkButton = m_wRoot.FindAnyWidget("NetworkButton");
        m_wBackButton = m_wRoot.FindAnyWidget("BackButton");
        m_wSettingsButton = m_wRoot.FindAnyWidget("SettingsButton");
        m_wSettingsBackButton = m_wRoot.FindAnyWidget("SettingsBackButton");
        m_wMarkerToolButton = m_wRoot.FindAnyWidget("MarkerToolButton");

        m_wChatContactName = m_wRoot.FindAnyWidget("ContactName");

        HookButtonHandlers();

        return true;
    }

    protected void HookButtonHandlers()
    {
        // Inlined per button — Enfusion can't take a method-pointer type as a
        // function parameter, so the helper had to go. The repetition is fine
        // for five buttons.
        if (m_wNetworkButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wNetworkButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnNetworkButtonClicked);
        }
        if (m_wBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnBackClicked);
        }
        if (m_wSettingsButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsClicked);
        }
        if (m_wSettingsBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsBackClicked);
        }
        if (m_wMarkerToolButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolButtonClicked);
        }
    }

    void Cleanup()
    {
        // Don't touch statics — those are intentionally persistent. Just drop
        // instance refs so the controller can be safely re-created on next
        // open without lingering widget pointers.
        m_wRoot = null;
        m_ActivePanelPlugin = null;
        m_SelectedMember = null;
    }

    // ============================================
    // PANEL SWITCHING
    // ============================================
    void SetPanelContent(ETDLPanelContent content)
    {
        // Guard: PLUGIN_TOOL requires an active plugin. Fall back to
        // NETWORK_LIST to keep the menu usable if a stale state slips through
        // (e.g. s_eLastPanel restored across menu open with no plugin owner).
        if (content == ETDLPanelContent.PLUGIN_TOOL && !m_ActivePanelPlugin)
            content = ETDLPanelContent.NETWORK_LIST;

        bool wasPluginTool = (m_eActivePanel == ETDLPanelContent.PLUGIN_TOOL);
        bool willBePluginTool = (content == ETDLPanelContent.PLUGIN_TOOL);

        m_eActivePanel = content;

        bool showPanel = (content != ETDLPanelContent.NONE);
        bool showNetwork = (content == ETDLPanelContent.NETWORK_LIST);
        bool showDetail = (content == ETDLPanelContent.MEMBER_DETAIL);
        bool showChat = (content == ETDLPanelContent.DIRECT_CHAT);
        bool showSettings = (content == ETDLPanelContent.SETTINGS);
        bool showMarkerTool = (content == ETDLPanelContent.MARKER_TOOL);
        bool showPluginTool = willBePluginTool;

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
            case ETDLPanelContent.PLUGIN_TOOL:
                if (m_ActivePanelPlugin)
                    title = m_ActivePanelPlugin.GetDisplayName();
                break;
        }

        // Sync the world-space visibility statics so the device display tracks
        // the menu's current panel content.
        AG0_TDLDisplayController.SetPanelState(showPanel, showNetwork, showDetail, showSettings, showMarkerTool, title);

        if (m_wSidePanel)
            m_wSidePanel.SetVisible(showPanel);

        if (showPanel)
        {
            if (m_wNetworkContent)
                m_wNetworkContent.SetVisible(showNetwork);
            if (m_wDetailContent)
                m_wDetailContent.SetVisible(showDetail);
            if (m_wSettingsContent)
                m_wSettingsContent.SetVisible(showSettings);
            if (m_wMarkerToolContent)
                m_wMarkerToolContent.SetVisible(showMarkerTool);
            if (m_wPluginToolPanel)
                m_wPluginToolPanel.SetVisible(showPluginTool);
            if (m_wChatContent)
                m_wChatContent.SetVisible(showChat);

            if (m_wPanelTitle)
                m_wPanelTitle.SetText(title);

            if (showChat && m_wChatContactName)
            {
                TextWidget chatName = TextWidget.Cast(m_wChatContactName);
                if (chatName)
                    chatName.SetText(m_sChatContactName);
            }
        }

        // Plugin panel lifecycle. OnPanelHidden fires before clearing the
        // active-plugin reference, so the plugin can still call back into the
        // menu during teardown. OnPanelShown fires after the slot is made
        // visible so the plugin can measure / focus into a live widget.
        if (wasPluginTool && !willBePluginTool && m_ActivePanelPlugin)
        {
            AG0_ATAKPluginBase exiting = m_ActivePanelPlugin;
            m_ActivePanelPlugin = null;
            exiting.OnPanelHidden();
        }
        if (willBePluginTool && m_ActivePanelPlugin)
            m_ActivePanelPlugin.OnPanelShown(m_wPluginToolPanel);

        // Notify the frontend so it can do its menu-specific reactions
        // (marker tool sub-panel, chat view repopulate, crosshair, gamepad
        // focus). Frontends that don't have those concerns (the world-space
        // device display) just ignore the event.
        m_OnPanelChanged.Invoke();
    }

    //------------------------------------------------------------------------------------------------
    void ToggleSidePanel()
    {
        if (m_eActivePanel == ETDLPanelContent.NONE)
            SetPanelContent(ETDLPanelContent.NETWORK_LIST);
        else
            SetPanelContent(ETDLPanelContent.NONE);
    }

    //------------------------------------------------------------------------------------------------
    void ShowDetailView(AG0_TDLNetworkMember member, RplId deviceId)
    {
        m_SelectedMember = member;
        m_SelectedDeviceId = deviceId;
        s_LastSelectedDeviceId = deviceId;

        PopulateDetailView();
        SetPanelContent(ETDLPanelContent.MEMBER_DETAIL);

        m_OnDetailShown.Invoke();
    }

    //------------------------------------------------------------------------------------------------
    //! Plugin entry point for claiming the side panel. Toggles like the
    //! navigation buttons: if the calling plugin is already the active panel
    //! owner, close back to the map (NONE); otherwise install this plugin as
    //! the owner and switch to PLUGIN_TOOL.
    void RequestPluginPanel(AG0_ATAKPluginBase plugin)
    {
        if (!plugin)
            return;

        bool alreadyActive = (m_eActivePanel == ETDLPanelContent.PLUGIN_TOOL && m_ActivePanelPlugin == plugin);
        if (alreadyActive)
        {
            SetPanelContent(ETDLPanelContent.NONE);
            return;
        }

        m_ActivePanelPlugin = plugin;
        SetPanelContent(ETDLPanelContent.PLUGIN_TOOL);
    }

    // ============================================
    // DETAIL POPULATION
    // ============================================
    void PopulateDetailView()
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
    //! Member lookup via the player controller's aggregated TDL membership.
    //! Same path AG0_TDLDisplayController uses internally — duplicated here so
    //! the controller can resolve members independently of either frontend.
    AG0_TDLNetworkMember GetNetworkMemberById(RplId rplId)
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return null;
        AG0_TDLNetworkMembers data = pc.GetAggregatedTDLMembers();
        if (!data)
            return null;
        return data.GetByRplId(rplId);
    }

    // ============================================
    // NAVIGATION BUTTON HANDLERS
    // ============================================
    protected void OnNetworkButtonClicked()
    {
        ToggleSidePanel();
    }

    protected void OnBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    protected void OnSettingsClicked()
    {
        SetPanelContent(ETDLPanelContent.SETTINGS);
    }

    protected void OnSettingsBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    protected void OnMarkerToolButtonClicked()
    {
        SetPanelContent(ETDLPanelContent.MARKER_TOOL);
    }

    // ============================================
    // STATE SAVE / RESTORE
    // ============================================
    //! Called by the menu just before close so the next open restores state.
    //! Captures the current plugin ID before transitioning out so it can be
    //! re-installed on next open if the plugin is still enabled.
    void SaveState()
    {
        s_LastSelectedDeviceId = m_SelectedDeviceId;
        s_LastChatContactRplId = m_ChatContactRplId;
        s_sLastChatContactName = m_sChatContactName;

        if (m_eActivePanel == ETDLPanelContent.PLUGIN_TOOL)
        {
            string pluginID = "";
            if (m_ActivePanelPlugin)
                pluginID = m_ActivePanelPlugin.GetPluginID();

            SetPanelContent(ETDLPanelContent.NONE);   // fires OnPanelHidden cleanly

            s_eLastPanel = ETDLPanelContent.PLUGIN_TOOL;
            s_sLastPanelPluginID = pluginID;
        }
        else
        {
            s_eLastPanel = m_eActivePanel;
            s_sLastPanelPluginID = "";
        }
    }

    //! Called by the menu after RefreshPlugins() so plugins are enabled and
    //! available for the PLUGIN_TOOL restore lookup. activePlugins is the
    //! menu's m_aActivePlugins.
    void RestoreState(array<ref AG0_ATAKPluginBase> activePlugins)
    {
        m_SelectedDeviceId = s_LastSelectedDeviceId;
        m_ChatContactRplId = s_LastChatContactRplId;
        m_sChatContactName = s_sLastChatContactName;

        if (m_SelectedDeviceId != RplId.Invalid())
            m_SelectedMember = GetNetworkMemberById(m_SelectedDeviceId);

        // Plugin panel restore: if last session ended on a plugin panel, see
        // whether that plugin is still enabled now and re-install it as the
        // panel owner so SetPanelContent(PLUGIN_TOOL) doesn't fall back.
        if (s_eLastPanel == ETDLPanelContent.PLUGIN_TOOL && !s_sLastPanelPluginID.IsEmpty() && activePlugins)
        {
            foreach (AG0_ATAKPluginBase plugin : activePlugins)
            {
                if (plugin && plugin.GetPluginID() == s_sLastPanelPluginID)
                {
                    m_ActivePanelPlugin = plugin;
                    break;
                }
            }
        }

        SetPanelContent(s_eLastPanel);
    }
}
