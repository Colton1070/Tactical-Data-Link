//------------------------------------------------------------------------------------------------
// Edit box component with two-stage gamepad navigation:
// 1. D-pad navigates between wrappers (highlight)
// 2. A button activates edit mode
//------------------------------------------------------------------------------------------------
class AG0_EditBoxComponent : ScriptedWidgetComponent
{
    [Attribute("1 1 1 0.3", UIWidgets.ColorPicker)]
    protected ref Color m_cOutlineDefault;
    
    [Attribute("0.2 0.8 0.8 0.8", UIWidgets.ColorPicker)]
    protected ref Color m_cOutlineSelected;  // Wrapper has focus, not yet editing
    
    [Attribute("1 1 1 1", UIWidgets.ColorPicker)]
    protected ref Color m_cOutlineFocused;   // Actively editing
    
    [Attribute("1")]
    protected float m_fAnimationRate;
    
    protected Widget m_wRoot;
    protected Widget m_wOutline;
    protected EditBoxWidget m_wEditBox;
    
    protected bool m_bWrapperFocused;  // Wrapper button has gamepad focus
    protected bool m_bEditMode;        // Actually typing in the EditBox
    
    // Events
    ref ScriptInvoker m_OnTextChanged = new ScriptInvoker();  // (AG0_EditBoxComponent, string)
    ref ScriptInvoker m_OnConfirm = new ScriptInvoker();      // (AG0_EditBoxComponent, string)
    ref ScriptInvoker m_OnFocusChanged = new ScriptInvoker(); // (AG0_EditBoxComponent, bool)
    
    override void HandlerAttached(Widget w)
	{
	    m_wRoot = w;
	    m_wOutline = w.FindAnyWidget("Outline");
	    m_wEditBox = EditBoxWidget.Cast(w.FindAnyWidget("EditBox"));
	    
	    if (m_wOutline)
	        m_wOutline.SetColor(m_cOutlineDefault);
	    
	    // Hook into modular button for gamepad click
	    SCR_ModularButtonComponent modBtn = SCR_ModularButtonComponent.Cast(
	        w.FindHandler(SCR_ModularButtonComponent)
	    );
	    if (modBtn)
	    {
	        modBtn.m_OnClicked.Insert(OnButtonClicked);
	    }
	    
	    // Hook into EditBox events
	    if (m_wEditBox)
	    {
	        SCR_EventHandlerComponent evh = SCR_EventHandlerComponent.Cast(
	            m_wEditBox.FindHandler(SCR_EventHandlerComponent)
	        );
	        if (evh)
	        {
	            evh.GetOnFocus().Insert(OnEditBoxFocus);
	            evh.GetOnFocusLost().Insert(OnEditBoxFocusLost);
	            evh.GetOnChange().Insert(OnEditBoxChange);
	            evh.GetOnChangeFinal().Insert(OnEditBoxConfirm);
	        }
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	protected void OnButtonClicked(SCR_ModularButtonComponent btn)
	{
	    if (m_wEditBox)
	    {
	        GetGame().GetWorkspace().SetFocusedWidget(m_wEditBox);
	        m_wEditBox.ActivateWriteMode();
	    }
}
    
    //------------------------------------------------------------------------------------------------
    override void HandlerDeattached(Widget w)
    {
        if (m_wEditBox)
        {
            SCR_EventHandlerComponent evh = SCR_EventHandlerComponent.Cast(
                m_wEditBox.FindHandler(SCR_EventHandlerComponent)
            );
            if (evh)
            {
                evh.GetOnFocus().Remove(OnEditBoxFocus);
                evh.GetOnFocusLost().Remove(OnEditBoxFocusLost);
                evh.GetOnChange().Remove(OnEditBoxChange);
                evh.GetOnChangeFinal().Remove(OnEditBoxConfirm);
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // Gamepad nav lands on the button wrapper - just highlight, don't enter edit mode yet
    override bool OnFocus(Widget w, int x, int y)
    {
        m_bWrapperFocused = true;
        UpdateOutline();
        return false;  // Allow focus to stay on wrapper
    }
    
    //------------------------------------------------------------------------------------------------
    override bool OnFocusLost(Widget w, int x, int y)
    {
        m_bWrapperFocused = false;
        
        // If we're in edit mode and focus left, exit edit mode
        if (m_bEditMode)
            ExitEditMode();
        
        UpdateOutline();
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    // A button or click on wrapper - NOW enter edit mode
    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (button == 0)  // Left click / A button
        {
            EnterEditMode();
            return true;
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void EnterEditMode()
    {
        if (!m_wEditBox)
            return;
        
        m_bEditMode = true;
        
        // Prevent D-pad from leaving while typing
        if (m_wRoot)
            m_wRoot.SetFlags(WidgetFlags.NOFOCUS);
        
        // Now redirect to EditBox
        GetGame().GetWorkspace().SetFocusedWidget(m_wEditBox);
        m_wEditBox.ActivateWriteMode();
        
        // Tell menu system we're in an interaction
        SCR_MenuHelper.SetActiveWidgetInteractionState(true);
        
        UpdateOutline();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ExitEditMode()
    {
        m_bEditMode = false;
        
        // Make wrapper focusable again
        if (m_wRoot)
            m_wRoot.ClearFlags(WidgetFlags.NOFOCUS);
        
        // Release interaction state
        SCR_MenuHelper.SetActiveWidgetInteractionState(false);
        
        UpdateOutline();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnEditBoxFocus()
    {
        m_bEditMode = true;
        UpdateOutline();
        m_OnFocusChanged.Invoke(this, true);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnEditBoxFocusLost()
    {
        ExitEditMode();
        
        // Return focus to wrapper so D-pad can continue navigating
        if (m_wRoot && m_bWrapperFocused)
            GetGame().GetWorkspace().SetFocusedWidget(m_wRoot);
        
        m_OnFocusChanged.Invoke(this, false);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnEditBoxChange(Widget w)
    {
        m_OnTextChanged.Invoke(this, GetText());
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnEditBoxConfirm(Widget w)
    {
        m_OnConfirm.Invoke(this, GetText());
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateOutline()
    {
        if (!m_wOutline)
            return;
        
        Color targetColor = m_cOutlineDefault;
        
        if (m_bEditMode)
            targetColor = m_cOutlineFocused;      // Actively editing - brightest
        else if (m_bWrapperFocused)
            targetColor = m_cOutlineSelected;     // Selected but not editing - medium
        
        if (m_fAnimationRate > 0)
            AnimateWidget.Color(m_wOutline, targetColor, m_fAnimationRate);
        else
            m_wOutline.SetColor(targetColor);
    }
    
    //------------------------------------------------------------------------------------------------
    // Public API
    //------------------------------------------------------------------------------------------------
    string GetText()
    {
        if (!m_wEditBox)
            return string.Empty;
        return m_wEditBox.GetText();
    }
    
    //------------------------------------------------------------------------------------------------
    void SetText(string text)
    {
        if (m_wEditBox)
            m_wEditBox.SetText(text);
    }
    
    //------------------------------------------------------------------------------------------------
    void SetPlaceholder(string text)
    {
        if (m_wEditBox)
            m_wEditBox.SetPlaceholderText(text);
    }
    
    //------------------------------------------------------------------------------------------------
    void Focus()
    {
        if (m_wRoot)
            GetGame().GetWorkspace().SetFocusedWidget(m_wRoot);
    }
    
    //------------------------------------------------------------------------------------------------
    void ActivateWriteMode()
    {
        EnterEditMode();
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsFocused()
    {
        return m_bEditMode;
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsSelected()
    {
        return m_bWrapperFocused;
    }
    
    //------------------------------------------------------------------------------------------------
    EditBoxWidget GetEditBoxWidget()
    {
        return m_wEditBox;
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_EditBoxComponent FindComponent(Widget w)
    {
        if (!w)
            return null;
        return AG0_EditBoxComponent.Cast(w.FindHandler(AG0_EditBoxComponent));
    }
}