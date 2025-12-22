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
    protected bool m_bEnabled;
    
    // Identity
    string GetPluginID() { return m_sPluginID; }
    string GetDisplayName() { return m_sDisplayName; }
    ResourceName GetToolIcon() { return m_sToolIcon; }
    bool IsEnabled() { return m_bEnabled; }
    AG0_TDLDeviceComponent GetATAKDevice() { return m_ATAKDevice; }
    IEntity GetSourceDevice() { return m_SourceDevice; }
    
    // Lifecycle
    void Enable(AG0_TDLDeviceComponent atakDevice, IEntity sourceDevice)
    {
        m_ATAKDevice = atakDevice;
        m_SourceDevice = sourceDevice;
        m_bEnabled = true;
        OnEnabled();
    }
    
    void Disable()
    {
        OnDisabled();
        m_ATAKDevice = null;
        m_SourceDevice = null;
        m_bEnabled = false;
    }
    
    protected void OnEnabled() {}
    protected void OnDisabled() {}
    
    // Toolbar
    bool ProvidesToolbarTool() { return !m_sToolIcon.IsEmpty(); }
    void OnToolActivated(Widget menuRoot) {}
    
    // Panel access - called when menu opens, plugin can grab whatever it needs
    void OnMenuOpened(Widget menuRoot) {}
    void OnMenuClosed() {}
	void OnMenuUpdate(float tDelta) {}

    
    // Panel slot - if plugin wants its own dedicated panel area
    Widget CreatePluginPanel(Widget parent) { return null; }
}