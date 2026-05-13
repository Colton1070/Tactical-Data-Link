//------------------------------------------------------------------------------------------------
//! Available-action condition for the TDLFocusToggle action hint.
//!
//! Shows the prompt iff at least one held TDL device's world-space display is
//! either focus-eligible (player is currently raycasting onto the screen and the
//! device has an authored focus pivot) OR already focus-active (so the user can
//! see how to exit). Mirrors the eligibility logic in
//! TDL_WorldSpaceDisplayComponent so the hint only appears when pressing the key
//! will actually do something.
//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class AG0_PlayerCanFocusTDLDeviceCondition : SCR_AvailableActionCondition
{
    override bool IsAvailable(SCR_AvailableActionsConditionData data)
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            GetGame().GetPlayerController()
        );
        if (!controller)
            return GetReturnResult(false);

        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();

        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (!device)
                continue;

            IEntity owner = device.GetOwner();
            if (!owner)
                continue;

            TDL_WorldSpaceDisplayComponent wsd = TDL_WorldSpaceDisplayComponent.Cast(
                owner.FindComponent(TDL_WorldSpaceDisplayComponent)
            );
            if (!wsd)
                continue;

            // Active wins — keeps the prompt visible while focused so the user
            // can see how to exit. Eligible covers the enter case.
            if (wsd.IsFocusActive() || wsd.IsFocusEligible())
                return GetReturnResult(true);
        }

        return GetReturnResult(false);
    }
}
