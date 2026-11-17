[BaseContainerProps()]
class AG0_PlayerHasEUDCondition : SCR_AvailableActionCondition
{
    override bool IsAvailable(notnull SCR_AvailableActionsConditionData data)
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            GetGame().GetPlayerController()
        );
        if (!controller)
            return GetReturnResult(false);
        
        return GetReturnResult(controller.HasEUDHinge());
    }
}