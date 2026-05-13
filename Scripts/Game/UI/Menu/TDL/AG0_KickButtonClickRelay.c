//------------------------------------------------------------------------------------------------
//! Per-row click relay for MPU5 network-member kick buttons.
//!
//! Same rationale as AG0_PluginButtonClickRelay: SCR_ButtonBaseComponent's
//! m_OnClicked is an untyped ScriptInvoker (fires zero-arg), so a handler
//! parameter receives null. To dispatch the right RplId when each row's kick
//! button is clicked, the button gets its own relay that captures the target
//! RplId at construction time and exposes a zero-arg OnClick().
//!
//! Lifetime: MPU5 holds a `ref array<ref AG0_KickButtonClickRelay>` that's
//! cleared whenever the node list is rebuilt (BuildNodeList runs at 1 Hz).
//! Relays die with the buttons they serve.
//------------------------------------------------------------------------------------------------
class AG0_KickButtonClickRelay
{
    protected RplId m_TargetRplId;

    //------------------------------------------------------------------------------------------------
    void AG0_KickButtonClickRelay(RplId targetRplId)
    {
        m_TargetRplId = targetRplId;
    }

    //------------------------------------------------------------------------------------------------
    //! Bound zero-arg to SCR_ModularButtonComponent.m_OnClicked. Sends the
    //! kick request to the server via SCR_PlayerController.
    void OnClick()
    {
        if (!m_TargetRplId.IsValid())
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (controller)
            controller.RequestKickDevice(m_TargetRplId);
    }
}
