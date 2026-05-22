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
    //! Fires (int absMouseX, int absMouseY) every poll tick the cursor moved.
    //! Marker tool ignores it; shape draw session uses it to rebuild the
    //! rubber-band ghost between clicks. Independent of drag state so the
    //! ghost tracks even when the user is just hovering after a click.
    ref ScriptInvoker m_OnCursorMove = new ScriptInvoker();
    // Tracks the previous polled cursor position so we only fire m_OnCursorMove
    // when it actually changed — keeps idle hand off the invoker stream.
    protected int m_iLastPolledMouseX = -1;
    protected int m_iLastPolledMouseY = -1;

    // Pan-suppression toggle. Pushed by the menu UI each frame and reflects
    // "user is actively drawing right now" — currently only when TDLDraw is
    // held AND FREEHAND is the active tool. While true, GetDragDelta returns
    // no pan delta and OnMouseButtonUp swallows the click-vs-drag short
    // circuit so a stray click during a draw doesn't drop a marker.
    // Sampling itself happens in AG0_TDLMenuController.Tick (cursor + key
    // poll), not here — the handler stays focused on pan input only.
    protected bool m_bFreehandActive;

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
            bool suppressClick = m_bFreehandActive;
            m_bDragging = false;

            // Click-vs-drag short-circuit. Suppressed while a draw is in
            // progress (TDLDraw held) so a click on the map during freehand
            // doesn't bleed through and place a marker / drop a shape point.
            if (!suppressClick && m_fTotalMoved < CLICK_THRESHOLD_PX)
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

        // Pan suppression while drawing — caller (menu UI) pushes the flag
        // each frame based on TDLDraw + active tool. Returning false-with-
        // zero-delta keeps the caller from applying any pan even if it
        // ignores the bool result.
        if (m_bFreehandActive)
        {
            deltaX = 0;
            deltaY = 0;
            return false;
        }

        return (deltaX != 0 || deltaY != 0);
    }

    //------------------------------------------------------------------------------------------------
    //! Toggle pan-suppression for draw mode. True while the user is actively
    //! drawing (e.g. TDLDraw held + FREEHAND tool armed); false otherwise so
    //! the user can pan freely between draws. The drag handler is purely a
    //! pan input now — sample emission moved to the controller's per-frame
    //! cursor poll so it can serve both KBM and world-space cursor models.
    void SetFreehandActive(bool active)
    {
        m_bFreehandActive = active;
    }
    
    //------------------------------------------------------------------------------------------------
    void CancelDrag()
    {
        m_bDragging = false;
    }

    //------------------------------------------------------------------------------------------------
    //! Called by the menu controller's per-frame update while shape-draw mode
    //! is armed. Fires m_OnCursorMove whenever the absolute mouse position
    //! changed since the last tick. Separate entry point from GetDragDelta
    //! because the shape rubber-band needs to track between clicks, not just
    //! during a held drag.
    void TickCursorPoll()
    {
        int mouseX, mouseY;
        WidgetManager.GetMousePos(mouseX, mouseY);

        if (mouseX == m_iLastPolledMouseX && mouseY == m_iLastPolledMouseY)
            return;

        m_iLastPolledMouseX = mouseX;
        m_iLastPolledMouseY = mouseY;
        m_OnCursorMove.Invoke(mouseX, mouseY);
    }
}