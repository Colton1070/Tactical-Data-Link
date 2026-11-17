//! What a cable can carry, as bits so one cable can carry several.
enum TDL_ECableCarry
{
	DATA	= 1,
	POWER	= 2,
	AUDIO	= 4
}

//! One connector on a device or hub.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sPortId")]
class TDL_CablePortDef
{
	[Attribute("", UIWidgets.EditBox, "Port name the connection rules refer to, e.g. usb")]
	protected string m_sPortId;

	[Attribute("", UIWidgets.Auto, "Where the connector sits on the model. +Z points the way a cable leaves it")]
	protected ref PointInfo m_Point;

	[Attribute("", UIWidgets.EditBox, "Worn items only: bone on the wearer the point is measured from, e.g. Spine5. A worn item is drawn on the wearer's skeleton, so its own origin says nothing about where it sits")]
	protected string m_sWearerBone;

	[Attribute("0 0 1", UIWidgets.EditBox, "Direction a cable leaves the connector, in the same space as the point. Kept apart from the point's angles so a port can be aimed without knowing how its bone is rolled")]
	protected vector m_vExitDir;

	[Attribute("1", UIWidgets.Slider, "How far a cable runs straight out of this connector before it starts to bend, as a share of the cable's usual. Lower for a soft lead that turns away at once", "0.1 2 0.05")]
	protected float m_fStiffness;

	[Attribute("", UIWidgets.ResourceNamePicker, "Cable drawn whenever this port is connected, for a device that brings its own connector. Wins over the rule's cable, and the cable's plug end sits on this port", "et")]
	protected ResourceName m_sCablePrefab;

	string GetId() { return m_sPortId; }
	PointInfo GetPoint() { return m_Point; }
	string GetWearerBone() { return m_sWearerBone; }
	vector GetExitDir() { return m_vExitDir; }
	float GetStiffness() { return m_fStiffness; }
	ResourceName GetCablePrefab() { return m_sCablePrefab; }
}

//! Two ports on a hub that pass something to each other. A hub with no join between two
//! ports keeps them apart, which is how an audio jack stays off the data path.
[BaseContainerProps()]
class TDL_CableHubJoin
{
	[Attribute("", UIWidgets.EditBox, "First port")]
	protected string m_sPortA;

	[Attribute("", UIWidgets.EditBox, "Second port")]
	protected string m_sPortB;

	[Attribute("3", UIWidgets.Flags, "What passes between the two ports", "", ParamEnumArray.FromEnum(TDL_ECableCarry))]
	protected TDL_ECableCarry m_eCarries;

	string GetPortA() { return m_sPortA; }
	string GetPortB() { return m_sPortB; }
	TDL_ECableCarry GetCarries() { return m_eCarries; }
}

//! One cable that exists whenever both ends are carried: this kind's port to that kind's port.
[BaseContainerProps()]
class TDL_CableRule
{
	[Attribute("", UIWidgets.EditBox, "Cable kind of the first device, e.g. PS_RDC")]
	protected string m_sKindA;

	[Attribute("", UIWidgets.EditBox, "Port on the first device")]
	protected string m_sPortA;

	[Attribute("", UIWidgets.EditBox, "Cable kind of the second device, e.g. PS_DUAL_PTT")]
	protected string m_sKindB;

	[Attribute("", UIWidgets.EditBox, "Port on the second device")]
	protected string m_sPortB;

	[Attribute("3", UIWidgets.Flags, "What this cable carries", "", ParamEnumArray.FromEnum(TDL_ECableCarry))]
	protected TDL_ECableCarry m_eCarries;

	[Attribute("0.6", UIWidgets.EditBox, "Cable length in metres")]
	protected float m_fLengthM;

	[Attribute("0", UIWidgets.EditBox, "Higher rules claim their ports first when two rules want the same port")]
	protected int m_iPriority;

	[Attribute("", UIWidgets.ResourceNamePicker, "Cable drawn for this rule, for one with its own plug moulded on. Empty draws the config's plain cable. A moulded plug sits on the first device's end", "et")]
	protected ResourceName m_sCablePrefab;

	[Attribute("0 0 0", UIWidgets.EditBox, "Which way the spare cable loops, in the wearer's space: X right, Y up, Z forward, so 0 0 -1 loops it behind. Zero lets it bow away from the chest")]
	protected vector m_vLoopDir;

	string GetKindA() { return m_sKindA; }
	string GetPortA() { return m_sPortA; }
	string GetKindB() { return m_sKindB; }
	string GetPortB() { return m_sPortB; }
	TDL_ECableCarry GetCarries() { return m_eCarries; }
	float GetLengthM() { return m_fLengthM; }
	int GetPriority() { return m_iPriority; }
	ResourceName GetCablePrefab() { return m_sCablePrefab; }
	vector GetLoopDir() { return m_vLoopDir; }
}

//! Every cable rule in one config, so what plugs into what is data rather than code.
[BaseContainerProps(configRoot: true)]
class TDL_CableRulesConfig
{
	static const ResourceName DEFAULT_PATH = "{7C4E1A92D03B5F16}Configs/TDL/TDL_CableRules.conf";

	[Attribute(desc: "Cables that connect themselves when both ends are carried")]
	protected ref array<ref TDL_CableRule> m_aRules;

	[Attribute("0", UIWidgets.CheckBox, "Also connect items stowed out of sight in pockets and packs. For testing before gear has places to mount everything")]
	protected bool m_bStowedItemsConnect;

	[Attribute("{7C4E1A95CAB1E010}Prefabs/Cables/TDL_Cable.et", UIWidgets.ResourceNamePicker, "Cable drawn for every link", "et")]
	protected ResourceName m_sCablePrefab;

	protected ref array<TDL_CableRule> m_aSorted;

	protected static ref TDL_CableRulesConfig s_Config;
	protected static bool s_bLoaded;

	bool DoStowedItemsConnect() { return m_bStowedItemsConnect; }
	ResourceName GetCablePrefab() { return m_sCablePrefab; }

	//------------------------------------------------------------------------------------------------
	//! Rules in the order they claim ports: priority first, config order within a priority.
	array<TDL_CableRule> GetRules()
	{
		if (m_aSorted)
			return m_aSorted;

		m_aSorted = new array<TDL_CableRule>();
		if (!m_aRules)
			return m_aSorted;

		foreach (TDL_CableRule rule : m_aRules)
		{
			if (!rule)
				continue;

			int at = m_aSorted.Count();
			while (at > 0 && m_aSorted[at - 1].GetPriority() < rule.GetPriority())
				at = at - 1;
			m_aSorted.InsertAt(rule, at);
		}
		return m_aSorted;
	}

	//------------------------------------------------------------------------------------------------
	static TDL_CableRulesConfig Get()
	{
		if (s_bLoaded)
			return s_Config;
		s_bLoaded = true;

		Resource resource = BaseContainerTools.LoadContainer(DEFAULT_PATH);
		if (!resource || !resource.IsValid())
		{
			Print(string.Format("[TDL_CABLE] Failed to load rules: %1", DEFAULT_PATH), LogLevel.ERROR);
			return null;
		}

		BaseContainer container = resource.GetResource().ToBaseContainer();
		if (!container)
			return null;

		s_Config = TDL_CableRulesConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(container));
		return s_Config;
	}
}
