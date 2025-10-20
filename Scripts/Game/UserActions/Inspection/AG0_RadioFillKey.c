class AG0_RadioFillKeyUserAction : SCR_InventoryAction
{
	//protected SCR_RadioComponent m_RadioComp;

	//------------------------------------------------------------------------------------------------
	protected AG0_TDLRadioComponent m_TdlRadioComp;


	override bool CanBeShownScript(IEntity user)
	{
		// Check if we have the correct component type
		if (!m_TdlRadioComp)
			return false;

		// Keep the inspect check if desired
		CharacterControllerComponent charComp = CharacterControllerComponent.Cast(user.FindComponent(CharacterControllerComponent));
		if (charComp && !charComp.GetInspect())
			return false; // Can only perform while inspecting

		// Add other conditions? E.g., Radio must be powered?
		// if (!m_TdlRadioComp.GetRadioComponent() || !m_TdlRadioComp.GetRadioComponent().IsPowered())
		//    return false;

		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		// Double check component existence and type safety
		if (!m_TdlRadioComp)
		{
			Print("AG0_RadioFillKeyUserAction: Missing AG0_TDLRadioComponent on PerformAction.", LogLevel.WARNING);
			return;
		}

		// Call the method on the component to open the dialog
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
		// Attempt to get the specific TDL radio component
		m_TdlRadioComp = AG0_TDLRadioComponent.Cast(pOwnerEntity.FindComponent(AG0_TDLRadioComponent));
		if (!m_TdlRadioComp)
			Print(string.Format("AG0_RadioFillKeyUserAction: Owner entity '%1' does not have AG0_TDLRadioComponent.", pOwnerEntity), LogLevel.WARNING);

		// We don't need the generic SCR_RadioComponent reference here anymore
		// m_RadioComp = SCR_RadioComponent.Cast(pOwnerEntity.FindComponent(SCR_RadioComponent));
	}

	override bool GetActionNameScript(out string outName)
	{
		// Use the specific component reference
		if (!m_TdlRadioComp)
			return false; // Cannot perform action if component is missing

		// Optional: Change action name based on state (e.g., "Update Key" if already set)
		string currentKey = m_TdlRadioComp.GetCurrentCryptoKey();
		if (currentKey != m_TdlRadioComp.GetDefaultCryptoKey()) {
		    outName = "Drop Key Fill";
		}
		else {
			outName = "Fill Key"; // Changed name slightly for clarity
		}
		
		return true;
	}
};