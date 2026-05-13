[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sPluginID")]
class AG0_ATAKPluginBase
{
    [Attribute("", UIWidgets.EditBox, "Unique plugin identifier", category: "Plugin Identity")]
    protected string m_sPluginID;

    [Attribute("", UIWidgets.EditBox, "Display name shown in toolbar", category: "Plugin Identity")]
    protected string m_sDisplayName;

    [Attribute("", UIWidgets.ResourceNamePicker, "Toolbar icon", "edds imageset", category: "Plugin Identity")]
    protected ResourceName m_sToolIcon;

    // Runtime state
    protected AG0_TDLDeviceComponent m_ATAKDevice;
    protected IEntity m_SourceDevice;
    protected AG0_TDLMenuUI m_MenuUI;
    protected bool m_bEnabled;

    // Identity
    string GetPluginID() { return m_sPluginID; }
    string GetDisplayName() { return m_sDisplayName; }
    ResourceName GetToolIcon() { return m_sToolIcon; }
    bool IsEnabled() { return m_bEnabled; }
    AG0_TDLDeviceComponent GetATAKDevice() { return m_ATAKDevice; }
    IEntity GetSourceDevice() { return m_SourceDevice; }
    AG0_TDLMenuUI GetMenuUI() { return m_MenuUI; }

    // Lifecycle
    //
    // Enable() is called by AG0_TDLMenuUI.RefreshPlugins() when the menu opens
    // with this plugin's source device in hand. The menuUi reference is kept
    // so the plugin can call back into the menu — most importantly to request
    // the side-panel slot via m_MenuUI.RequestPluginPanel(this).
    void Enable(AG0_TDLDeviceComponent atakDevice, IEntity sourceDevice, AG0_TDLMenuUI menuUi)
    {
        m_ATAKDevice = atakDevice;
        m_SourceDevice = sourceDevice;
        m_MenuUI = menuUi;
        m_bEnabled = true;
        OnEnabled();
    }

    void Disable()
    {
        OnDisabled();
        m_ATAKDevice = null;
        m_SourceDevice = null;
        m_MenuUI = null;
        m_bEnabled = false;
    }

    protected void OnEnabled() {}
    protected void OnDisabled() {}

    // Toolbar
    //
    // ProvidesToolbarTool: declaration only — true means the menu should render
    // a toolbar button for this plugin.
    //
    // OnToolActivated: fired by the toolbar button click. Typical override
    // calls m_MenuUI.RequestPluginPanel(this) to claim the side panel slot;
    // the menu handles the toggle (first click opens, second click closes back
    // to map). Plugins that want non-panel behaviour (e.g. an instant action)
    // can override this to do anything else.
    bool ProvidesToolbarTool() { return !m_sToolIcon.IsEmpty(); }
    void OnToolActivated(Widget menuRoot) {}

    // Menu lifecycle — every plugin gets these regardless of toolbar usage.
    // OnMenuOpened is the place to spawn always-on overlays (e.g. PTT status).
    void OnMenuOpened(Widget menuRoot) {}
    void OnMenuClosed() {}
    void OnMenuUpdate(float tDelta) {}

    // Side-panel lifecycle. Fired by AG0_TDLMenuUI.SetPanelContent when this
    // plugin transitions into / out of PLUGIN_TOOL panel content. panelRoot
    // is the empty PluginToolPanel Frame from TDLMenuUI.layout — fill it on
    // OnPanelShown, tear it down on OnPanelHidden. The menu owns the slot
    // visibility; the plugin owns the panel contents.
    void OnPanelShown(Widget panelRoot) {}
    void OnPanelHidden() {}
}
