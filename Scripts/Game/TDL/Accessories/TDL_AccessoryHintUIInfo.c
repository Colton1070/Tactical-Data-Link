//! Tooltip line on an accessory item, written by its spec, so a mod's new antenna or battery
//! describes itself without a hint of its own.
[BaseContainerProps()]
class TDL_AccessoryItemHintUIInfo : SCR_InventoryItemHintUIInfo
{
	override bool CanBeShown(InventoryItemComponent item, SCR_InventorySlotUI focusedSlot)
	{
		if (!item || !super.CanBeShown(item, focusedSlot))
			return false;
		return !GetItemHintName(item).IsEmpty();
	}

	override string GetItemHintName(InventoryItemComponent item)
	{
		if (!item)
			return string.Empty;

		TDL_AccessoryComponent accessory = TDL_AccessoryComponent.Of(item.GetOwner());
		if (!accessory)
			return string.Empty;

		return accessory.GetHintText();
	}
}

enum TDL_EPortHintState
{
	WORKING,	//!< Fitted and doing its job
	FAULT,		//!< Fitted but not working: an antenna set on the wrong band, a flat battery
	MISSING		//!< Port empty
}

//! Tooltip line on a device for one port in one state. A device lists one entry per state it
//! wants called out, each with its own text and colour, so a missing antenna can read red and
//! a fitted one plain. %1 in the name is the fitted accessory's display name, %2 its live
//! state (a battery's charge).
[BaseContainerProps(), SCR_BaseContainerLocalizedTitleField("Name")]
class TDL_PortStatusHintUIInfo : SCR_InventoryItemHintUIInfo
{
	[Attribute("1", UIWidgets.ComboBox, "Port this line reports on", "", ParamEnumArray.FromEnum(TDL_EAccessoryKind))]
	protected TDL_EAccessoryKind m_eKind;

	[Attribute("0", UIWidgets.ComboBox, "State this line is shown in", "", ParamEnumArray.FromEnum(TDL_EPortHintState))]
	protected TDL_EPortHintState m_eState;

	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(InventoryItemComponent item, SCR_InventorySlotUI focusedSlot)
	{
		if (!item || !super.CanBeShown(item, focusedSlot))
			return false;

		AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(item.GetOwner().FindComponent(AG0_TDLDeviceComponent));
		if (!device || !device.GetAccessoryPorts() || !device.GetAccessoryPorts().HasPort(m_eKind))
			return false;

		return GetState(device) == m_eState;
	}

	//------------------------------------------------------------------------------------------------
	override string GetItemHintName(InventoryItemComponent item)
	{
		string fittedName;
		string fittedStatus;
		if (item)
		{
			AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(item.GetOwner().FindComponent(AG0_TDLDeviceComponent));
			if (device && device.GetAccessoryPorts())
			{
				TDL_AccessoryComponent fitted = device.GetAccessoryPorts().GetFitted(m_eKind);
				if (fitted)
				{
					fittedStatus = fitted.GetStatusText();
					InventoryItemComponent fittedItem = InventoryItemComponent.Cast(fitted.GetOwner().FindComponent(InventoryItemComponent));
					if (fittedItem && fittedItem.GetUIInfo())
						fittedName = fittedItem.GetUIInfo().GetName();
				}
			}
		}
		return WidgetManager.Translate(GetName(), fittedName, fittedStatus);
	}

	//------------------------------------------------------------------------------------------------
	protected TDL_EPortHintState GetState(AG0_TDLDeviceComponent device)
	{
		TDL_AccessoryComponent fitted = device.GetAccessoryPorts().GetFitted(m_eKind);
		if (!fitted)
			return TDL_EPortHintState.MISSING;

		if (m_eKind == TDL_EAccessoryKind.RF_ANTENNA && device.GetAntennaRangeFactor() <= 0)
			return TDL_EPortHintState.FAULT;

		if (fitted.IsDepleted())
			return TDL_EPortHintState.FAULT;

		return TDL_EPortHintState.WORKING;
	}
}
