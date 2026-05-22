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
    //! Controller is the shared host of plugin lifecycle — both the fullscreen
    //! menu (AG0_TDLMenuUI) and the world-space device display
    //! (TDL_WorldSpaceDisplayComponent) drive an AG0_TDLMenuController against
    //! their own TDLMenuUI.layout instance and call RefreshPlugins on it.
    //! Plugins call m_Controller.RequestPluginPanel(this) to claim the side
    //! panel slot regardless of which frontend hosts them.
    protected AG0_TDLMenuController m_Controller;
    protected bool m_bEnabled;

    // Identity
    string GetPluginID() { return m_sPluginID; }
    string GetDisplayName() { return m_sDisplayName; }
    ResourceName GetToolIcon() { return m_sToolIcon; }
    bool IsEnabled() { return m_bEnabled; }
    AG0_TDLDeviceComponent GetATAKDevice() { return m_ATAKDevice; }
    IEntity GetSourceDevice() { return m_SourceDevice; }
    AG0_TDLMenuController GetController() { return m_Controller; }

    // Lifecycle
    //
    // Enable() is called by AG0_TDLMenuController.RefreshPlugins() when the
    // hosting frontend (menu open / world-space mounted) initialises plugin
    // state. The controller reference is kept so the plugin can call back —
    // most importantly to request the side-panel slot via
    // m_Controller.RequestPluginPanel(this).
    void Enable(AG0_TDLDeviceComponent atakDevice, IEntity sourceDevice, AG0_TDLMenuController controller)
    {
        m_ATAKDevice = atakDevice;
        m_SourceDevice = sourceDevice;
        m_Controller = controller;
        m_bEnabled = true;
        OnEnabled();
    }

    void Disable()
    {
        OnDisabled();
        m_ATAKDevice = null;
        m_SourceDevice = null;
        m_Controller = null;
        m_bEnabled = false;
    }

    //! Rebind the active controller without going through the full Enable
    //! path. Plugin button relays call this just before dispatching
    //! OnToolActivated so the plugin's RequestPluginPanel routes to the
    //! controller that spawned the clicked button — not whichever frontend
    //! called Enable last. Necessary because the plugin instance is shared
    //! per-device across both frontends (menu + world-space), so the menu
    //! closing (Disable → m_Controller = null) would otherwise strand the
    //! world-space button with a null controller.
    void SetController(AG0_TDLMenuController controller)
    {
        m_Controller = controller;
    }

    protected void OnEnabled() {}
    protected void OnDisabled() {}

    // Toolbar
    //
    // ProvidesToolbarTool: declaration only — true means the host should render
    // a toolbar button for this plugin.
    //
    // OnToolActivated: fired by the toolbar button click. Typical override
    // calls m_Controller.RequestPluginPanel(this) to claim the side panel slot;
    // the controller handles the toggle (first click opens, second click closes
    // back to map). Plugins that want non-panel behaviour (e.g. an instant
    // action) can override this to do anything else.
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
