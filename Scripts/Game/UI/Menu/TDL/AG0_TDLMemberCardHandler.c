//------------------------------------------------------------------------------------------------
//! Handler for TDL Member Card buttons — manages click and focus events.
//! Bound to the AG0_TDLMenuController so it works identically on both the
//! fullscreen menu and the world-space device display (each instantiates its
//! own controller against the same TDLMenuUI.layout).
class AG0_TDLMemberCardHandler : ScriptedWidgetComponent
{
    protected AG0_TDLMenuController m_Controller;
    protected RplId m_MemberRplId;
    protected AG0_TDLNetworkMember m_MemberData;

    //------------------------------------------------------------------------------------------------
    void Init(AG0_TDLMenuController controller, RplId memberId, AG0_TDLNetworkMember memberData)
    {
        m_Controller = controller;
        m_MemberRplId = memberId;
        m_MemberData = memberData;
    }

    //------------------------------------------------------------------------------------------------
    // Called when card gains focus (D-pad navigation or mouse hover).
    // Controller fires m_OnMemberCardFocused so menu-side gamepad focus
    // tracking can update its remembered card index. World-space ignores it.
    override bool OnFocus(Widget w, int x, int y)
    {
        if (m_Controller)
            m_Controller.OnMemberCardFocused(m_MemberRplId);
        return false;
    }

    //------------------------------------------------------------------------------------------------
    // Called when card is clicked/pressed (A button or mouse click).
    // Controller drives the ShowDetailView transition so both frontends
    // navigate to the contact-detail panel identically.
    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (m_Controller)
            m_Controller.OnMemberCardClicked(m_MemberRplId, button);
        return true; // Consume the event
    }

    //------------------------------------------------------------------------------------------------
    RplId GetMemberRplId()
    {
        return m_MemberRplId;
    }
}
