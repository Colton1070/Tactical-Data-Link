//------------------------------------------------------------------------------------------------
//! Handler for clickable map markers
class AG0_TDLMapMarkerHandler : ScriptedWidgetEventHandler
{
    protected AG0_TDLMenuUI m_Menu;
    protected RplId m_MemberId;
    
    //------------------------------------------------------------------------------------------------
    void Init(AG0_TDLMenuUI menu, RplId memberId)
    {
        m_Menu = menu;
        m_MemberId = memberId;
    }
    
    //------------------------------------------------------------------------------------------------
    RplId GetMemberRplId()
    {
        return m_MemberId;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (m_Menu && button == 0)  // Left click
        {
            m_Menu.OnMapMarkerClicked(m_MemberId);
            return true;
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool OnMouseEnter(Widget w, int x, int y)
    {
        // Could add hover effect here
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        // Could remove hover effect here
        return false;
    }
	
	override bool OnFocus(Widget w, int x, int y)
	{
	    if (m_Menu)
	        m_Menu.OnMapMarkerFocused(m_MemberId);
	    return false;
	}
}