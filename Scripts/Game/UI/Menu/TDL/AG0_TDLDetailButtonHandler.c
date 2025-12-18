//------------------------------------------------------------------------------------------------
// AG0_TDLDetailButtonHandler.c
//------------------------------------------------------------------------------------------------
class AG0_TDLDetailButtonHandler : ScriptedWidgetComponent
{
    protected AG0_TDLMenuUI m_Menu;
    protected string m_sAction;
    
    void Init(AG0_TDLMenuUI menu, string action)
    {
        m_Menu = menu;
        m_sAction = action;
    }
    
    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (!m_Menu)
            return false;
        
        if (m_sAction == "back")
            m_Menu.OnDetailBackClicked();
        else if (m_sAction == "viewfeed")
            m_Menu.OnViewFeedClicked();
        else if (m_sAction == "map")
            m_Menu.OnMapViewClicked();
        else if (m_sAction == "mapback")
            m_Menu.OnMapBackClicked();
        else if (m_sAction == "zoomin")
            m_Menu.OnZoomInClicked();
        else if (m_sAction == "zoomout")
            m_Menu.OnZoomOutClicked();
        
        return true;
    }
}