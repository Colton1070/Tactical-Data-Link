class AG0_CDU_Power : ScriptedUserAction
{
    protected AG0_ControlDisplayUnitComponent m_CDUComponent = null;
    
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        Print("AG0_CDU_Power: PerformAction called", LogLevel.DEBUG);
        
        if (!m_CDUComponent) {
            m_CDUComponent = AG0_ControlDisplayUnitComponent.Cast(pOwnerEntity.FindComponent(AG0_ControlDisplayUnitComponent));
            if (!m_CDUComponent) {
                Print("AG0_CDU_Power: Failed to find AG0_ControlDisplayUnitComponent", LogLevel.ERROR);
                return;
            }
        }
        
        // Toggle display
        if (m_CDUComponent.IsDisplayOn())
            m_CDUComponent.TurnOffDisplay();
        else
            m_CDUComponent.TurnOnDisplay();
    }

    override bool GetActionNameScript(out string outName)
    {
        outName = "PWR";
        return true;
    }
}