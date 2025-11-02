//------------------------------------------------------------------------------------------------
//! Returns true when current controlled entity has a TDL device with specified capabilities
//! Returns opposite if m_bNegateCondition is enabled
[BaseContainerProps()]
class AG0_PlayerHasTDLDeviceWithCapabilitiesCondition : SCR_AvailableActionCondition
{
    [Attribute(
        defvalue: "36", // INFORMATION (32) + DISPLAY_OUTPUT (4) = 36
        UIWidgets.Flags,
        desc: "Required TDL device capabilities",
        enums: ParamEnumArray.FromEnum(AG0_ETDLDeviceCapability)
    )]
    protected AG0_ETDLDeviceCapability m_eRequiredCapabilities;
    
    override bool IsAvailable(SCR_AvailableActionsConditionData data)
    {
        if (!data)
            return GetReturnResult(false);
        
        AG0_TDLController controller = AG0_TDLController.Cast(
    		GetGame().GetWorld().GetSystems().FindMyController(AG0_TDLController)
		);
        if (!controller)
            return GetReturnResult(false);
        
        // Use existing method from modded player controller
        array<AG0_TDLDeviceComponent> devices = controller.GetPlayerTDLDevices();
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            // Check if device has all required capabilities (bitwise AND check)
            if ((device.GetActiveCapabilities() & m_eRequiredCapabilities) == m_eRequiredCapabilities)
                return GetReturnResult(true);
        }
        
        return GetReturnResult(false);
    }
}