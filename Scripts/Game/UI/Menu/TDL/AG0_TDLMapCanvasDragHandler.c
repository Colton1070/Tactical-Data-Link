//------------------------------------------------------------------------------------------------
// AG0_TDLMapCanvasDragHandler.c
// Handles mouse drag-to-pan on the TDL map canvas
//------------------------------------------------------------------------------------------------

class AG0_TDLMapCanvasDragHandler : ScriptedWidgetComponent
{
    protected bool m_bDragging;
    protected int m_iLastMouseX;
    protected int m_iLastMouseY;
    
    ref ScriptInvoker m_OnDragStart = new ScriptInvoker();
    
    //------------------------------------------------------------------------------------------------
    override bool OnMouseButtonDown(Widget w, int x, int y, int button)
    {
        if (button != 0)
            return false;
        
        m_bDragging = true;
        WidgetManager.GetMousePos(m_iLastMouseX, m_iLastMouseY);
        
        m_OnDragStart.Invoke();
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool OnMouseButtonUp(Widget w, int x, int y, int button)
    {
        if (button == 0)
            m_bDragging = false;
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        // Stop dragging if mouse leaves canvas
        m_bDragging = false;
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsDragging()
    {
        return m_bDragging;
    }
    
    //------------------------------------------------------------------------------------------------
    bool GetDragDelta(out int deltaX, out int deltaY)
    {
        if (!m_bDragging)
            return false;
        
        int mouseX, mouseY;
        WidgetManager.GetMousePos(mouseX, mouseY);
        
        deltaX = mouseX - m_iLastMouseX;
        deltaY = mouseY - m_iLastMouseY;
        
        m_iLastMouseX = mouseX;
        m_iLastMouseY = mouseY;
        
        return (deltaX != 0 || deltaY != 0);
    }
    
    //------------------------------------------------------------------------------------------------
    void CancelDrag()
    {
        m_bDragging = false;
    }
}