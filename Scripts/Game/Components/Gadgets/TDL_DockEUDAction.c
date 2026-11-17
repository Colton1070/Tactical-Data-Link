//! Clips a handheld device into a mount the character is wearing, from the inventory or
//! the world, for when it was not put away by hand.
class TDL_DockEUDAction : ScriptedUserAction
{
	protected TDL_EUDGadgetComponent m_Gadget;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Gadget = TDL_EUDGadgetComponent.Cast(pOwnerEntity.FindComponent(TDL_EUDGadgetComponent));
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_Gadget && user && m_Gadget.CanDock(user);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		return CanBeShownScript(user);
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (m_Gadget)
			m_Gadget.TryDock(pUserEntity);
	}

	//------------------------------------------------------------------------------------------------
	// The move is an inventory request from the player doing it; replication carries the result.
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBroadcastScript()
	{
		return false;
	}
}
