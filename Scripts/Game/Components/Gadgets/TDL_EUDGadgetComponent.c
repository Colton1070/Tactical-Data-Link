[ComponentEditorProps(category: "TDL/Gadgets", description: "Makes a handheld end user device a gadget the character can hold in hand")]
class TDL_EUDGadgetComponentClass : SCR_GadgetComponentClass {}

//! Handheld EUD as a gadget. The character pose comes from m_eAnimVariable, which the
//! prefab sets to MAP: the two-handed, chest-height, eyes-down hold is how a tablet or
//! controller-style EUD is actually read.
class TDL_EUDGadgetComponent : SCR_GadgetComponent
{
	[Attribute("1", UIWidgets.CheckBox, "Put away, the device clips into a free mount the character is wearing instead of going to a pocket")]
	protected bool m_bAutoDock;

	//------------------------------------------------------------------------------------------------
	//! SPECIALIST_ITEM rather than MAP, so the gadget manager does not route the map key or
	//! map menu to the device.
	override EGadgetType GetType()
	{
		return EGadgetType.SPECIALIST_ITEM;
	}

	//------------------------------------------------------------------------------------------------
	//! Docked in a mount, the device is in storage as far as the gadget manager knows, and
	//! vanilla hides stored gadgets. The mount is how the device gets worn, so it has to stay
	//! drawn there.
	override void UpdateVisibility(EGadgetMode mode)
	{
		if (mode != EGadgetMode.IN_HAND && IsDocked())
		{
			InventoryItemComponent itemComp = InventoryItemComponent.Cast(GetOwner().FindComponent(InventoryItemComponent));
			if (itemComp)
				itemComp.ShowOwner();
			return;
		}

		super.UpdateVisibility(mode);
	}

	//------------------------------------------------------------------------------------------------
	override void ModeClear(EGadgetMode mode)
	{
		IEntity character = m_CharacterOwner;
		super.ModeClear(mode);

		// Deferred a frame: the device is still on its way out of the hand here, and the
		// inventory will not move an item that is mid-transition.
		if (mode == EGadgetMode.IN_HAND && m_bAutoDock && character)
			GetGame().GetCallqueue().CallLater(DockIfPutAway, 0, false, character);
	}

	//------------------------------------------------------------------------------------------------
	protected void DockIfPutAway(IEntity character)
	{
		IEntity owner = GetOwner();
		if (!owner || !character || IsDocked())
			return;

		// Dropped, or already back in the hand: not put away.
		if (m_iMode == EGadgetMode.IN_HAND || m_iMode == EGadgetMode.ON_GROUND || owner.GetRootParent() != character)
			return;

		// Inventory moves are asked for by the machine that controls the character.
		if (SCR_PlayerController.GetLocalControlledEntity() != character)
			return;

		TryDock(character);
	}

	//------------------------------------------------------------------------------------------------
	//! True when the character wears a mount with room for this device.
	bool CanDock(IEntity character)
	{
		int slotID;
		return !IsDocked() && TDL_AccessoryPortsComponent.FindFreeWornPort(character, GetOwner(), slotID) != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Moves the device into a free mount the character is wearing. False when there is none
	//! or the inventory refuses the move.
	bool TryDock(IEntity character)
	{
		int slotID;
		TDL_AccessoryPortsComponent ports = TDL_AccessoryPortsComponent.FindFreeWornPort(character, GetOwner(), slotID);
		if (!ports)
			return false;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
			return false;

		return inventory.TryMoveItemToStorage(GetOwner(), ports, slotID);
	}

	//------------------------------------------------------------------------------------------------
	bool IsDocked()
	{
		InventoryItemComponent itemComp = InventoryItemComponent.Cast(GetOwner().FindComponent(InventoryItemComponent));
		if (!itemComp)
			return false;
		return TDL_AccessoryPortSlot.Cast(itemComp.GetParentSlot()) != null;
	}
}
