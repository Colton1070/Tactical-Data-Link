[ComponentEditorProps(category: "TDL/Accessories", description: "Marks an item as a device accessory: antenna set, GPS antenna or battery")]
class TDL_AccessoryComponentClass : ScriptComponentClass {}

//! One class for every accessory item. What the item is lives in its key and spec, so a new
//! antenna, GPS puck or battery from any mod is a prefab, never a new script class.
class TDL_AccessoryComponent : ScriptComponent
{
	[Attribute("", UIWidgets.EditBox, "Identifies this accessory model to port accept lists and mesh rules, e.g. PS_ANT_2005")]
	protected string m_sAccessoryKey;

	[Attribute(desc: "What the accessory is. Pick the subclass that matches the port it should fit")]
	protected ref TDL_AccessorySpec m_Spec;

	// Energy is a server float; clients only ever need the whole percent, which changes a few
	// times an hour, so that is all that replicates.
	protected float m_fEnergyWh = -1;

	[RplProp()]
	protected int m_iChargePercent = 100;

	string GetKey() { return m_sAccessoryKey; }
	TDL_AccessorySpec GetSpec() { return m_Spec; }

	TDL_EAccessoryKind GetKind()
	{
		if (!m_Spec)
			return TDL_EAccessoryKind.NONE;
		return m_Spec.GetKind();
	}

	static TDL_AccessoryComponent Of(IEntity item)
	{
		if (!item)
			return null;
		return TDL_AccessoryComponent.Cast(item.FindComponent(TDL_AccessoryComponent));
	}

	//------------------------------------------------------------------------------------------------
	TDL_BatterySpec GetBatterySpec()
	{
		return TDL_BatterySpec.Cast(m_Spec);
	}

	//------------------------------------------------------------------------------------------------
	//! Remaining charge, 0-100. Always 100 for anything that is not a battery.
	int GetChargePercent()
	{
		return m_iChargePercent;
	}

	//------------------------------------------------------------------------------------------------
	bool IsDepleted()
	{
		return GetBatterySpec() && m_iChargePercent <= 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only. Takes energy out of a battery and replicates the percent when it changes.
	void DrainWh(float wh)
	{
		TDL_BatterySpec battery = GetBatterySpec();
		if (!battery || wh <= 0 || !Replication.IsServer())
			return;

		float capacity = battery.GetCapacityWh();
		if (capacity <= 0)
			return;

		if (m_fEnergyWh < 0)
			m_fEnergyWh = capacity * m_iChargePercent / 100;

		m_fEnergyWh = Math.Max(0, m_fEnergyWh - wh);

		// Rounded up so the pack reads 1% until it is genuinely empty, the way a fuel gauge
		// should never show zero on a radio that is still talking.
		int percent = Math.Ceil(m_fEnergyWh / capacity * 100);
		if (percent == m_iChargePercent)
			return;

		m_iChargePercent = percent;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Short live state for device tooltips: charge for a battery, empty for everything else.
	string GetStatusText()
	{
		if (!GetBatterySpec())
			return string.Empty;
		return m_iChargePercent.ToString() + "%";
	}

	//------------------------------------------------------------------------------------------------
	//! Tooltip line for the item itself: the spec, plus charge for a battery.
	string GetHintText()
	{
		if (!m_Spec)
			return string.Empty;

		string spec = m_Spec.GetHintText();
		if (!GetBatterySpec())
			return spec;

		return string.Format("%1 charged, %2", GetStatusText(), spec);
	}
}
