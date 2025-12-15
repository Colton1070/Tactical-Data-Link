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
        Print("TDL_HINT_CONDITION: IsAvailable() called", LogLevel.DEBUG);
        
        if (!data)
        {
            Print("TDL_HINT_CONDITION: No data", LogLevel.DEBUG);
            return GetReturnResult(false);
        }
        
        SCR_PlayerController controller = SCR_PlayerController.Cast(
            GetGame().GetPlayerController()
        );
        if (!controller)
        {
            Print("TDL_HINT_CONDITION: No controller", LogLevel.DEBUG);
            return GetReturnResult(false);
        }
        
        array<AG0_TDLDeviceComponent> devices = controller.GetPlayerTDLDevices();
        Print(string.Format("TDL_HINT_CONDITION: Found %1 devices, required caps: %2", 
            devices.Count(), m_eRequiredCapabilities), LogLevel.DEBUG);
        
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            int activeCaps = device.GetActiveCapabilities();
            bool match = (activeCaps & m_eRequiredCapabilities) == m_eRequiredCapabilities;
            
            Print(string.Format("TDL_HINT_CONDITION: Device caps=%1, powered=%2, match=%3", 
                activeCaps, device.IsPowered(), match), LogLevel.DEBUG);
            
            if (match)
            {
                Print("TDL_HINT_CONDITION: Returning TRUE", LogLevel.DEBUG);
                return GetReturnResult(true);
            }
        }
        
        Print("TDL_HINT_CONDITION: Returning FALSE", LogLevel.DEBUG);
        return GetReturnResult(false);
    }
}