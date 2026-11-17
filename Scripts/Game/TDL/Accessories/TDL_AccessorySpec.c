//! Which kind of port an accessory fits. Ports and accessories meet on this value, so a
//! battery can never be offered to an antenna port however its prefab is set up. Another
//! mod adds a kind with a modded enum and a TDL_AccessorySpec subclass that returns it.
enum TDL_EAccessoryKind
{
	NONE,
	RF_ANTENNA,
	GPS_ANTENNA,
	BATTERY,
	EUD
}

//! RF bands as bits so one antenna can cover several. A radio's module is a single band.
enum TDL_ERFBand
{
	L		= 1,	// 1350-1390 MHz
	BAS		= 2,	// 2025-2110 MHz broadcast auxiliary
	S		= 4,	// 2200-2500 MHz
	C_LOW	= 8,	// 4400-5000 MHz
	C_HIGH	= 16	// upper C
}

enum TDL_EBatteryChemistry
{
	LI_ION,
	LI_SO2,
	LITHIUM_AA,
	ALKALINE_AA,
	NIMH
}

class TDL_RFBandNames
{
	static string Name(TDL_ERFBand band)
	{
		switch (band)
		{
			case TDL_ERFBand.L: return "L";
			case TDL_ERFBand.BAS: return "BAS";
			case TDL_ERFBand.S: return "S";
			case TDL_ERFBand.C_LOW: return "Lower C";
			case TDL_ERFBand.C_HIGH: return "Upper C";
		}
		return "?";
	}

	//! "S", or "L / S" for a multi-band mask.
	static string Describe(int bands)
	{
		string text;
		for (int bit = 1; bit <= TDL_ERFBand.C_HIGH; bit = bit << 1)
		{
			if ((bands & bit) == 0)
				continue;
			if (!text.IsEmpty())
				text += " / ";
			text += Name(bit);
		}
		return text;
	}
}

//! What an accessory is, as opposed to which item it is. Ports check the kind; the device
//! reads the subclass for the numbers that change its behaviour.
[BaseContainerProps()]
class TDL_AccessorySpec
{
	TDL_EAccessoryKind GetKind()
	{
		return TDL_EAccessoryKind.NONE;
	}

	//! One line for the inventory tooltip: what a player needs to know to pick this over another.
	//! Empty for no line.
	string GetHintText()
	{
		return string.Empty;
	}
}

[BaseContainerProps()]
class TDL_RFAntennaSpec : TDL_AccessorySpec
{
	[Attribute("4", UIWidgets.Flags, "Bands the antenna radiates on. A radio links only if its module band is one of them", "", ParamEnumArray.FromEnum(TDL_ERFBand))]
	protected TDL_ERFBand m_eBands;

	[Attribute("2.15", UIWidgets.EditBox, "Gain in dBi from the datasheet. 2.15 is a plain sleeve dipole")]
	protected float m_fGainDbi;

	[Attribute("3", UIWidgets.EditBox, "Antenna elements in the set, one per RF port it fills")]
	protected int m_iElements;

	override TDL_EAccessoryKind GetKind()
	{
		return TDL_EAccessoryKind.RF_ANTENNA;
	}

	TDL_ERFBand GetBands() { return m_eBands; }
	float GetGainDbi() { return m_fGainDbi; }
	int GetElements() { return m_iElements; }

	bool Covers(TDL_ERFBand band)
	{
		return (m_eBands & band) != 0;
	}

	override string GetHintText()
	{
		return string.Format("%1 band, %2 dBi", TDL_RFBandNames.Describe(m_eBands), m_fGainDbi.ToString(-1, 2));
	}
}

[BaseContainerProps()]
class TDL_GPSAntennaSpec : TDL_AccessorySpec
{
	override TDL_EAccessoryKind GetKind()
	{
		return TDL_EAccessoryKind.GPS_ANTENNA;
	}
}

//! A handheld end user device as the accessory, for mounts and docks it rides in. The key
//! decides which mounts it fits, since mounts are cut for one case shape.
[BaseContainerProps()]
class TDL_EUDSpec : TDL_AccessorySpec
{
	override TDL_EAccessoryKind GetKind()
	{
		return TDL_EAccessoryKind.EUD;
	}
}

//! Datasheet figures for a battery type. Charge state lives on the battery item, not here,
//! so a half-used pack stays half-used when it moves between radios.
[BaseContainerProps()]
class TDL_BatterySpec : TDL_AccessorySpec
{
	[Attribute("0", UIWidgets.ComboBox, "Decides whether the pack can be recharged", "", ParamEnumArray.FromEnum(TDL_EBatteryChemistry))]
	protected TDL_EBatteryChemistry m_eChemistry;

	[Attribute("74", UIWidgets.EditBox, "Energy when full, in watt-hours")]
	protected float m_fCapacityWh;

	[Attribute("12.6", UIWidgets.EditBox, "Nominal voltage")]
	protected float m_fNominalVoltage;

	[Attribute("50", UIWidgets.EditBox, "Highest continuous draw the pack is rated for, in watts")]
	protected float m_fMaxOutputW;

	override TDL_EAccessoryKind GetKind()
	{
		return TDL_EAccessoryKind.BATTERY;
	}

	TDL_EBatteryChemistry GetChemistry() { return m_eChemistry; }
	float GetCapacityWh() { return m_fCapacityWh; }
	float GetNominalVoltage() { return m_fNominalVoltage; }
	float GetMaxOutputW() { return m_fMaxOutputW; }

	override string GetHintText()
	{
		return string.Format("%1 Wh %2, %3 V", m_fCapacityWh.ToString(-1, 0), GetChemistryName(), m_fNominalVoltage.ToString(-1, 1));
	}

	string GetChemistryName()
	{
		switch (m_eChemistry)
		{
			case TDL_EBatteryChemistry.LI_ION: return "Li-ion";
			case TDL_EBatteryChemistry.LI_SO2: return "Li-SO2";
			case TDL_EBatteryChemistry.LITHIUM_AA: return "lithium AA";
			case TDL_EBatteryChemistry.ALKALINE_AA: return "alkaline AA";
			case TDL_EBatteryChemistry.NIMH: return "NiMH";
		}
		return typename.EnumToString(TDL_EBatteryChemistry, m_eChemistry);
	}

	bool IsRechargeable()
	{
		return m_eChemistry == TDL_EBatteryChemistry.LI_ION || m_eChemistry == TDL_EBatteryChemistry.NIMH;
	}
}

//! The radio's RF front end. Range authored on the device is the range with the stock
//! antenna set fitted; this scales it for whatever set is actually on the radio.
[BaseContainerProps()]
class TDL_RFProfile
{
	[Attribute("4", UIWidgets.ComboBox, "Band of the RF module inside the radio. Fixed per prefab; a different band is a different radio", "", ParamEnumArray.FromEnum(TDL_ERFBand))]
	protected TDL_ERFBand m_eModuleBand;

	[Attribute("2.15", UIWidgets.EditBox, "Gain in dBi of the stock antenna the device's network range was authored for")]
	protected float m_fReferenceGainDbi;

	[Attribute("3", UIWidgets.Slider, "Path loss exponent. 2 is free space; ground-to-ground manpack is typically 3-4", "2 5 0.1")]
	protected float m_fPathLossExponent;

	TDL_ERFBand GetModuleBand() { return m_eModuleBand; }

	//! Multiplier on the authored range for the antenna set fitted. 0 when nothing is fitted
	//! or the set is on the wrong band: the radio cannot reach anyone without antennas.
	//! Log-distance path loss is why the gain difference enters as a power of ten: each
	//! 10*n dB of extra margin buys ten times the distance.
	float GetRangeFactor(TDL_RFAntennaSpec antenna)
	{
		if (!antenna || !antenna.Covers(m_eModuleBand))
			return 0;

		float exponent = Math.Max(m_fPathLossExponent, 1);
		float deltaDb = antenna.GetGainDbi() - m_fReferenceGainDbi;
		return Math.Pow(10, deltaDb / (10 * exponent));
	}
}

//! What a battery-powered device draws. Watt figures rather than a runtime so the same pack
//! lasts longer in a frugal radio and shorter in a hungry one, as it does in the field.
[BaseContainerProps()]
class TDL_PowerProfile
{
	[Attribute("5", UIWidgets.EditBox, "Draw while switched on and on the net, in watts")]
	protected float m_fIdleW;

	[Attribute("4", UIWidgets.EditBox, "Extra draw while streaming camera video, in watts")]
	protected float m_fVideoW;

	float GetDrawW(bool streamingVideo)
	{
		if (streamingVideo)
			return m_fIdleW + m_fVideoW;
		return m_fIdleW;
	}
}
