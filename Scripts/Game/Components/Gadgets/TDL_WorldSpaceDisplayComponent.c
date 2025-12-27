class TDL_WorldSpaceDisplayComponentClass : ScriptComponentClass {}

class TDL_WorldSpaceDisplayComponent : ScriptComponent
{
    // Screen entity from slot
    protected IEntity m_ScreenEntity;
    
    // Root container widget (for cleanup)
    protected Widget m_wRTContainer;
    
    // RTTextureWidget that renders to the screen
    protected RTTextureWidget m_RTWidget;
    
    // Container frame inside RT widget
    protected Widget m_wContentFrame;
    
    // Root widget containing the ATAK layout
    protected Widget m_wRoot;
    
    // Display controller - handles all the ATAK logic
    protected ref AG0_TDLDisplayController m_DisplayController;
    
    // Layout paths
    [Attribute("{A13D983933B16A90}UI/layouts/Menus/TDL/TDLMenuRenderTarget.layout", UIWidgets.ResourceNamePicker, "RT Container layout", "layout")]
    protected ResourceName m_RTContainerLayout;
    
    [Attribute("{DF6A0F6906E0F330}UI/layouts/Menus/TDL/TDLMenuUI.layout", UIWidgets.ResourceNamePicker, "ATAK UI layout", "layout")]
    protected ResourceName m_ATAKLayout;
    
    // Slot name to find screen entity
    [Attribute("Screen", UIWidgets.EditBox, "Name of slot containing screen mesh")]
    protected string m_sScreenSlotName;
    
    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        
        // Only setup on local machine where we need visuals
        if (!System.IsConsoleApp())
        {
            SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);
        
        // Defer setup to ensure slots are populated
        GetGame().GetCallqueue().CallLater(SetupRenderTarget, 100, false, owner);
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnFrame(IEntity owner, float timeSlice)
    {
        // Update the display controller each frame
        if (m_DisplayController)
            m_DisplayController.Update(timeSlice);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void SetupRenderTarget(IEntity owner)
    {
        // Find the screen entity from slot
        SlotManagerComponent slotMgr = SlotManagerComponent.Cast(owner.FindComponent(SlotManagerComponent));
        if (!slotMgr)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No SlotManagerComponent found", LogLevel.ERROR);
            return;
        }
        
        // Get screen entity from named slot
        EntitySlotInfo screenSlot = slotMgr.GetSlotByName(m_sScreenSlotName);
        if (!screenSlot)
        {
            Print(string.Format("[TDL_WorldSpaceDisplay] FAIL: No '%1' slot found", m_sScreenSlotName), LogLevel.ERROR);
            return;
        }
        
        m_ScreenEntity = screenSlot.GetAttachedEntity();
        if (!m_ScreenEntity)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No entity in Screen slot", LogLevel.ERROR);
            return;
        }
        
        // Create the RTTextureWidget and bind to screen
        CreateRenderTarget();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void CreateRenderTarget()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No workspace", LogLevel.ERROR);
            return;
        }
        
        // Create RT container from layout
        m_wRTContainer = workspace.CreateWidgets(m_RTContainerLayout);
        if (!m_wRTContainer)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Could not create RT container layout", LogLevel.ERROR);
            return;
        }
        
        // Find the RTTextureWidget inside the layout (it's a child, not the root)
        Widget rtWidget = m_wRTContainer.FindAnyWidget("RTTexture0");
        if (!rtWidget)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: No RTTexture0 found in layout", LogLevel.ERROR);
            m_wRTContainer.RemoveFromHierarchy();
            m_wRTContainer = null;
            return;
        }
        
        m_RTWidget = RTTextureWidget.Cast(rtWidget);
        if (!m_RTWidget)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: RTTexture0 is not RTTextureWidget", LogLevel.ERROR);
            m_wRTContainer.RemoveFromHierarchy();
            m_wRTContainer = null;
            return;
        }
        
        // Find the content frame inside the RT widget
        m_wContentFrame = m_RTWidget.FindAnyWidget("ContentFrame");
        if (!m_wContentFrame)
        {
            // If no content frame, use the RT widget directly as parent
            Print("[TDL_WorldSpaceDisplay] No ContentFrame, using RTWidget directly", LogLevel.WARNING);
            m_wContentFrame = m_RTWidget;
        }
        
        // Load ATAK layout as child of content frame
        m_wRoot = workspace.CreateWidgets(m_ATAKLayout, m_wContentFrame);
        if (!m_wRoot)
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Could not create ATAK layout", LogLevel.ERROR);
            return;
        }
        
        // Bind to the screen mesh
        m_RTWidget.SetRenderTarget(m_ScreenEntity);
        
        Print("[TDL_WorldSpaceDisplay] Render target bound, initializing display controller...", LogLevel.NORMAL);
        
        // Initialize the display controller with the ATAK layout
        m_DisplayController = new AG0_TDLDisplayController();
        if (!m_DisplayController.Init(m_wRoot))
        {
            Print("[TDL_WorldSpaceDisplay] FAIL: Display controller init failed", LogLevel.ERROR);
            m_DisplayController = null;
            return;
        }
        
        Print("[TDL_WorldSpaceDisplay] World-space ATAK display initialized successfully", LogLevel.NORMAL);
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnDelete(IEntity owner)
    {
        Cleanup();
        super.OnDelete(owner);
    }
    
    //------------------------------------------------------------------------------------------------
    void Cleanup()
    {
        // Cleanup display controller first
        if (m_DisplayController)
        {
            m_DisplayController.Cleanup();
            m_DisplayController = null;
        }
        
        // Remove render target binding
        if (m_RTWidget && m_ScreenEntity)
            m_RTWidget.RemoveRenderTarget(m_ScreenEntity);
        
        // Remove the root container (this removes all children too)
        if (m_wRTContainer)
        {
            m_wRTContainer.RemoveFromHierarchy();
            m_wRTContainer = null;
        }
        
        m_RTWidget = null;
        m_wContentFrame = null;
        m_wRoot = null;
    }
}