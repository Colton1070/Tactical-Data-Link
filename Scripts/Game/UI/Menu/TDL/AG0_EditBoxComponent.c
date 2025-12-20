//------------------------------------------------------------------------------------------------
// Minimal edit box component - focus outline and text access, nothing else
//------------------------------------------------------------------------------------------------
class AG0_EditBoxComponent : ScriptedWidgetComponent
{
    [Attribute("1 1 1 0.3", UIWidgets.ColorPicker)]
    protected ref Color m_cOutlineDefault;
    
    [Attribute("1 1 1 1", UIWidgets.ColorPicker)]
    protected ref Color m_cOutlineFocused;
    
    [Attribute("0.15")]
    protected float m_fAnimationRate;
    
    protected Widget m_wRoot;
    protected Widget m_wOutline;
    protected EditBoxWidget m_wEditBox;
    
    protected bool m_bIsFocused;
    
    // Events
    ref ScriptInvoker m_OnTextChanged = new ScriptInvoker();  // (AG0_EditBoxComponent, string)
    ref ScriptInvoker m_OnConfirm = new ScriptInvoker();      // (AG0_EditBoxComponent, string)
    ref ScriptInvoker m_OnFocusChanged = new ScriptInvoker(); // (AG0_EditBoxComponent, bool)
    
    //------------------------------------------------------------------------------------------------
    override void HandlerAttached(Widget w)
    {
        m_wRoot = w;
        m_wOutline = w.FindAnyWidget("Outline");
        m_wEditBox = EditBoxWidget.Cast(w.FindAnyWidget("EditBox"));
        
        if (m_wOutline)
            m_wOutline.SetColor(m_cOutlineDefault);
        
        // Hook into the edit box events via SCR_EventHandlerComponent if present
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
    // Gamepad nav lands on the button wrapper - redirect to the actual edit box
    override bool OnFocus(Widget w, int x, int y)
    {
        if (m_wEditBox)
        {
            GetGame().GetWorkspace().SetFocusedWidget(m_wEditBox);
            m_wEditBox.ActivateWriteMode();
        }
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnEditBoxFocus()
    {
        m_bIsFocused = true;
        
        // Prevent gamepad from bouncing back to wrapper while typing
        if (m_wRoot)
            m_wRoot.SetFlags(WidgetFlags.NOFOCUS);
        
        // Tell menu system we're in an interaction
        SCR_MenuHelper.SetActiveWidgetInteractionState(true);
        
        UpdateOutline();
        m_OnFocusChanged.Invoke(this, true);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnEditBoxFocusLost()
    {
        m_bIsFocused = false;
        
        // Make wrapper focusable again for future navigation
        if (m_wRoot)
            m_wRoot.ClearFlags(WidgetFlags.NOFOCUS);
        
        // Release interaction state
        SCR_MenuHelper.SetActiveWidgetInteractionState(false);
        
        UpdateOutline();
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
		if(m_bIsFocused)
			targetColor = m_cOutlineFocused;
        
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
        if (m_wEditBox)
        {
            GetGame().GetWorkspace().SetFocusedWidget(m_wEditBox);
            m_wEditBox.ActivateWriteMode();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void ActivateWriteMode()
    {
        if (m_wEditBox)
            m_wEditBox.ActivateWriteMode();
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsFocused()
    {
        return m_bIsFocused;
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