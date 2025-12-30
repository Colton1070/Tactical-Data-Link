[BaseContainerProps()]
class AG0_PlayerHasEUDCondition : SCR_AvailableActionCondition
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
            TDL_EUDBoneComponent boneComp = TDL_EUDBoneComponent.Cast(
                device.GetOwner().FindComponent(TDL_EUDBoneComponent)
            );
            if (boneComp)
                return GetReturnResult(true);
        }
        
        return GetReturnResult(false);
    }
}