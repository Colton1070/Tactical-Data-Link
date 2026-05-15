class AG0_RadioFillKeyUserAction : SCR_InventoryAction
{
	//------------------------------------------------------------------------------------------------
	protected AG0_TDLRadioComponent m_TdlRadioComp;


	override bool CanBeShownScript(IEntity user)
	{
		if (!m_TdlRadioComp)
			return false;

		CharacterControllerComponent charComp = CharacterControllerComponent.Cast(user.FindComponent(CharacterControllerComponent));
		if (charComp && !charComp.GetInspect())
			return false; // Can only perform while inspecting

		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_TdlRadioComp)
		{
			Print("AG0_RadioFillKeyUserAction: Missing AG0_TDLRadioComponent on PerformAction.", LogLevel.WARNING);
			return;
		}

		string currentKey = m_TdlRadioComp.GetCurrentCryptoKey();
		if (currentKey == m_TdlRadioComp.GetDefaultCryptoKey()) {
		    m_TdlRadioComp.FillKey(pUserEntity);
		}
		else {
			m_TdlRadioComp.DropFillKey();
		}
	}

	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}

	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_TdlRadioComp = AG0_TDLRadioComponent.Cast(pOwnerEntity.FindComponent(AG0_TDLRadioComponent));
		if (!m_TdlRadioComp)
			Print(string.Format("AG0_RadioFillKeyUserAction: Owner entity '%1' does not have AG0_TDLRadioComponent.", pOwnerEntity), LogLevel.WARNING);
	}

	override bool GetActionNameScript(out string outName)
	{
		if (!m_TdlRadioComp)
			return false;

		string currentKey = m_TdlRadioComp.GetCurrentCryptoKey();
		if (currentKey != m_TdlRadioComp.GetDefaultCryptoKey()) {
		    outName = "Drop Key Fill";
		}
		else {
			outName = "Fill Key";
		}

		return true;
	}
};