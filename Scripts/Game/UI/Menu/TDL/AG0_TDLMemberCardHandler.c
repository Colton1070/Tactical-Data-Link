//------------------------------------------------------------------------------------------------
//! Handler for TDL Member Card buttons - manages click and focus events
class AG0_TDLMemberCardHandler : ScriptedWidgetComponent
{
    protected AG0_TDLMenuUI m_Menu;
    protected RplId m_MemberRplId;
    protected AG0_TDLNetworkMember m_MemberData;
    
    //------------------------------------------------------------------------------------------------
    void Init(AG0_TDLMenuUI menu, RplId memberId, AG0_TDLNetworkMember memberData)
    {
        m_Menu = menu;
        m_MemberRplId = memberId;
        m_MemberData = memberData;
    }
    
    //------------------------------------------------------------------------------------------------
    // Called when card gains focus (D-pad navigation or mouse hover)
	override bool OnFocus(Widget w, int x, int y)
	{
	    if (m_Menu)
	        m_Menu.OnMemberCardFocused(m_MemberRplId);
	    return false;
	}
    
    //------------------------------------------------------------------------------------------------
    // Called when card is clicked/pressed (A button or mouse click)
    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (m_Menu)
            m_Menu.OnMemberCardClicked(m_MemberRplId, button);
        return true; // Consume the event
    }
    
    //------------------------------------------------------------------------------------------------
    RplId GetMemberRplId()
    {
        return m_MemberRplId;
    }
}