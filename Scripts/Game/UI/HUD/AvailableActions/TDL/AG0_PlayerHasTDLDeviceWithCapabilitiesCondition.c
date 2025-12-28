//------------------------------------------------------------------------------------------------
//! Returns true when current controlled entity has a TDL device with specified capabilities
//! Returns opposite if m_bNegateCondition is enabled
[BaseContainerProps()]
class AG0_PlayerHasTDLDeviceWithCapabilitiesCondition : SCR_AvailableActionCondition
{
    [Attribute(
        defvalue: "0", // None
        UIWidgets.Flags,
        desc: "Required TDL device capabilities",
        enums: ParamEnumArray.FromEnum(AG0_ETDLDeviceCapability)
    )]
    protected AG0_ETDLDeviceCapability m_eRequiredCapabilities;
    
	override bool IsAvailable(SCR_AvailableActionsConditionData data)
	{
	    SCR_PlayerController controller = SCR_PlayerController.Cast(
	        GetGame().GetPlayerController()
	    );
	    if (!controller)
	        return GetReturnResult(false);
	    
	    array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
	    
	    // Aggregate capabilities across ALL devices
	    int aggregatedCaps = 0;
	    foreach (AG0_TDLDeviceComponent device : devices)
	    {
	        aggregatedCaps |= device.GetActiveCapabilities();
	    }
	    
	    // Check if aggregate meets requirements
	    bool match = (aggregatedCaps & m_eRequiredCapabilities) == m_eRequiredCapabilities;
	    return GetReturnResult(match);
	}
}