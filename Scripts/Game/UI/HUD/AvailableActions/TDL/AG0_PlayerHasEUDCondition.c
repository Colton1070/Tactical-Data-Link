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
            if (TDL_EUDEntity.Cast(device.GetOwner()))
                return GetReturnResult(true);
        }
        
        return GetReturnResult(false);
    }
}