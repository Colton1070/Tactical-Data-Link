//------------------------------------------------------------------------------------------------
// AG0_TDLMapCanvasDragHandler.c
// Handles mouse drag-to-pan on the TDL map canvas
//------------------------------------------------------------------------------------------------

class AG0_TDLMapCanvasDragHandler : ScriptedWidgetComponent
{
    protected bool m_bDragging;
    protected int m_iLastMouseX;
    protected int m_iLastMouseY;
    // Tracked for click-vs-drag detection — accumulated absolute movement
    // since mouse-down. If under CLICK_THRESHOLD on mouse-up we treat as a
    // click and fire m_OnClick rather than just ending the drag.
    protected int m_iDownMouseX;
    protected int m_iDownMouseY;
    protected float m_fTotalMoved;
    protected static const float CLICK_THRESHOLD_PX = 5.0;

    ref ScriptInvoker m_OnDragStart = new ScriptInvoker();
    //! Fires (int absMouseX, int absMouseY) when a left-button press+release
    //! happened with minimal movement (under CLICK_THRESHOLD_PX). Coords are
    //! WidgetManager.GetMousePos absolute workspace pixels — caller subtracts
    //! the canvas widget's screen-pos and feeds to AG0_TDLMapView.ScreenToWorld
    //! to resolve the world position.
    ref ScriptInvoker m_OnClick = new ScriptInvoker();

    //------------------------------------------------------------------------------------------------
    override bool OnMouseButtonDown(Widget w, int x, int y, int button)
    {
        if (button != 0)
            return false;

        m_bDragging = true;
        m_fTotalMoved = 0;
        WidgetManager.GetMousePos(m_iDownMouseX, m_iDownMouseY);
        m_iLastMouseX = m_iDownMouseX;
        m_iLastMouseY = m_iDownMouseY;

        m_OnDragStart.Invoke();

        return false;
    }

    //------------------------------------------------------------------------------------------------
    override bool OnMouseButtonUp(Widget w, int x, int y, int button)
    {
        if (button == 0)
        {
            m_bDragging = false;

            // Click vs drag — minimal cumulative movement counts as a click.
            // Used by the marker tool to drop a marker at cursor position.
            if (m_fTotalMoved < CLICK_THRESHOLD_PX)
            {
                int curX, curY;
                WidgetManager.GetMousePos(curX, curY);
                m_OnClick.Invoke(curX, curY);
            }
        }

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

        // Accumulate movement for click-vs-drag classification on mouse-up.
        // Manhattan distance is good enough for the threshold check.
        if (deltaX != 0 || deltaY != 0)
            m_fTotalMoved = m_fTotalMoved + Math.AbsFloat(deltaX) + Math.AbsFloat(deltaY);

        return (deltaX != 0 || deltaY != 0);
    }
    
    //------------------------------------------------------------------------------------------------
    void CancelDrag()
    {
        m_bDragging = false;
    }
}