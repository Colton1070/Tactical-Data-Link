class AG0_CDU_CryptoAction : ScriptedUserAction
{
	[Attribute("1", UIWidgets.CheckBox, "Fill crypto key (true) or clear crypto key (false)")]
    protected bool m_bIsFill;
	
    protected AG0_ControlDisplayUnitComponent m_CDUComponent = null;
    
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (!m_CDUComponent) {
            m_CDUComponent = AG0_ControlDisplayUnitComponent.Cast(pOwnerEntity.FindComponent(AG0_ControlDisplayUnitComponent));
            if (!m_CDUComponent) {
                Print("AG0_CDU_CryptoAction: Failed to find AG0_ControlDisplayUnitComponent", LogLevel.ERROR);
                return;
            }
        }
        
        // Just delegate to the component
        if (m_bIsFill)
            m_CDUComponent.InitiateCryptoFill(pUserEntity);
        else
            m_CDUComponent.ClearCryptoFill(pUserEntity);
    }

    override bool GetActionNameScript(out string outName)
    {
        outName = "FILL CRYPTO KEY";
        return true;
    }
}