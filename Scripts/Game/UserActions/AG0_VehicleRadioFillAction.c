class AG0_VehicleRadioFillAction : ScriptedUserAction
{
	protected SCR_VehicleRadioComponent m_RadioComp;

	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_RadioComp = SCR_VehicleRadioComponent.Cast(pOwnerEntity.FindComponent(SCR_VehicleRadioComponent));
	}

	override bool CanBeShownScript(IEntity user)
	{
		return m_RadioComp != null;
	}

	override bool CanBePerformedScript(IEntity user)
	{
		return m_RadioComp != null;
	}

	override bool GetActionNameScript(out string outName)
	{
		return m_RadioComp && m_RadioComp.GetFillActionName(outName);
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (m_RadioComp)
			m_RadioComp.PerformFillAction(pUserEntity);
	}

	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}
}
