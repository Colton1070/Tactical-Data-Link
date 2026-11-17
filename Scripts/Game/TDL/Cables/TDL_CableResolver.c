//! One cable between two carried items.
class TDL_CableLink
{
	TDL_CablePortsComponent m_A;
	int m_iPortA;
	TDL_CablePortsComponent m_B;
	int m_iPortB;
	TDL_ECableCarry m_eCarries;
	float m_fLengthM;
	ResourceName m_sCablePrefab;
	vector m_vLoopDir;
}

//! Everything wired together on one character at one moment: the cables, and for each thing
//! a cable can carry, which items can reach each other through cables and hubs.
class TDL_CableState
{
	protected static const int CARRY_COUNT = 3;

	protected IEntity m_Carrier;
	protected float m_fResolvedAt;
	protected ref array<TDL_CablePortsComponent> m_aNodes = new array<TDL_CablePortsComponent>();
	protected ref array<ref TDL_CableLink> m_aLinks = new array<ref TDL_CableLink>();
	protected ref array<string> m_aStowed = new array<string>();

	// Each item takes one element for itself and one per port. Reachability is a union-find
	// over those elements, kept once per carry bit because a hub can pass audio between two
	// ports without passing data between them.
	protected ref array<int> m_aBase = new array<int>();
	protected ref array<ref array<int>> m_aSets = new array<ref array<int>>();
	protected ref array<int> m_aPortUsed = new array<int>();

	IEntity GetCarrier() { return m_Carrier; }
	float GetResolvedAt() { return m_fResolvedAt; }
	array<ref TDL_CableLink> GetLinks() { return m_aLinks; }

	//------------------------------------------------------------------------------------------------
	void TDL_CableState(IEntity carrier, float resolvedAt, notnull array<TDL_CablePortsComponent> nodes, notnull array<string> stowed)
	{
		m_Carrier = carrier;
		m_fResolvedAt = resolvedAt;
		m_aNodes.Copy(nodes);
		m_aStowed.Copy(stowed);

		int total = 0;
		foreach (TDL_CablePortsComponent node : m_aNodes)
		{
			m_aBase.Insert(total);
			total = total + 1 + node.GetPortCount();
		}

		for (int e = 0; e < total; e++)
			m_aPortUsed.Insert(0);

		TDL_CableRulesConfig config = TDL_CableRulesConfig.Get();
		if (config)
			ResolveLinks(config.GetRules());

		for (int c = 0; c < CARRY_COUNT; c++)
			m_aSets.Insert(BuildSet(1 << c, total));
	}

	//------------------------------------------------------------------------------------------------
	// Rules claim ports in priority order and a port takes one cable, so the result does not
	// depend on which machine works it out as long as the items arrive in the same order.
	protected void ResolveLinks(notnull array<TDL_CableRule> rules)
	{
		foreach (TDL_CableRule rule : rules)
		{
			for (int a = 0; a < m_aNodes.Count(); a++)
			{
				if (m_aNodes[a].GetKind() != rule.GetKindA())
					continue;

				int portA = m_aNodes[a].FindPort(rule.GetPortA());
				if (portA < 0 || m_aPortUsed[Element(a, portA)] != 0)
					continue;

				for (int b = 0; b < m_aNodes.Count(); b++)
				{
					if (b == a || m_aNodes[b].GetKind() != rule.GetKindB())
						continue;

					int portB = m_aNodes[b].FindPort(rule.GetPortB());
					if (portB < 0 || m_aPortUsed[Element(b, portB)] != 0)
						continue;

					TDL_CableLink link = new TDL_CableLink();
					link.m_A = m_aNodes[a];
					link.m_iPortA = portA;
					link.m_B = m_aNodes[b];
					link.m_iPortB = portB;
					link.m_eCarries = rule.GetCarries();
					link.m_fLengthM = rule.GetLengthM();
					link.m_sCablePrefab = rule.GetCablePrefab();
					link.m_vLoopDir = rule.GetLoopDir();
					m_aLinks.Insert(link);

					m_aPortUsed[Element(a, portA)] = 1;
					m_aPortUsed[Element(b, portB)] = 1;
					break;
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected array<int> BuildSet(int carryBit, int total)
	{
		array<int> parents = new array<int>();
		for (int e = 0; e < total; e++)
			parents.Insert(e);

		for (int n = 0; n < m_aNodes.Count(); n++)
		{
			TDL_CablePortsComponent node = m_aNodes[n];
			if (!node.IsHub())
			{
				for (int p = 0; p < node.GetPortCount(); p++)
					Union(parents, m_aBase[n], Element(n, p));
				continue;
			}

			foreach (TDL_CableHubJoin join : node.GetJoins())
			{
				if (!join || (join.GetCarries() & carryBit) == 0)
					continue;

				int joinA = node.FindPort(join.GetPortA());
				int joinB = node.FindPort(join.GetPortB());
				if (joinA >= 0 && joinB >= 0)
					Union(parents, Element(n, joinA), Element(n, joinB));
			}
		}

		foreach (TDL_CableLink link : m_aLinks)
		{
			if ((link.m_eCarries & carryBit) == 0)
				continue;
			Union(parents, Element(m_aNodes.Find(link.m_A), link.m_iPortA), Element(m_aNodes.Find(link.m_B), link.m_iPortB));
		}
		return parents;
	}

	//------------------------------------------------------------------------------------------------
	protected int Element(int node, int port)
	{
		return m_aBase[node] + 1 + port;
	}

	//------------------------------------------------------------------------------------------------
	protected int Root(notnull array<int> parents, int element)
	{
		while (parents[element] != element)
			element = parents[element];
		return element;
	}

	//------------------------------------------------------------------------------------------------
	protected void Union(notnull array<int> parents, int a, int b)
	{
		int rootA = Root(parents, a);
		int rootB = Root(parents, b);
		if (rootA != rootB)
			parents[rootB] = rootA;
	}

	//------------------------------------------------------------------------------------------------
	protected int FindNode(IEntity item)
	{
		for (int n = 0; n < m_aNodes.Count(); n++)
		{
			if (m_aNodes[n] && m_aNodes[n].GetOwner() == item)
				return n;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the two items can pass the given thing to each other through cables and hubs.
	//! \param carry a single TDL_ECableCarry bit
	bool AreWired(IEntity itemA, IEntity itemB, TDL_ECableCarry carry)
	{
		int nodeA = FindNode(itemA);
		int nodeB = FindNode(itemB);
		if (nodeA < 0 || nodeB < 0)
			return false;

		for (int c = 0; c < CARRY_COUNT; c++)
		{
			if ((1 << c) != carry)
				continue;
			return Root(m_aSets[c], m_aBase[nodeA]) == Root(m_aSets[c], m_aBase[nodeB]);
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! One line per cable, plus what was left out for being stowed. Doubles as the change
	//! detector for debug logging.
	string Describe()
	{
		string text = string.Empty;
		foreach (TDL_CableLink link : m_aLinks)
		{
			if (!link.m_A || !link.m_B)
				continue;
			text = text + string.Format("%1.%2 -> %3.%4 [%5]; ", link.m_A.GetKind(), link.m_A.GetPortId(link.m_iPortA), link.m_B.GetKind(), link.m_B.GetPortId(link.m_iPortB), CarryText(link.m_eCarries));
		}

		if (text.IsEmpty())
			text = "no cables; ";

		foreach (string kind : m_aStowed)
			text = text + string.Format("%1 stowed, not connected; ", kind);

		return text;
	}

	//------------------------------------------------------------------------------------------------
	static string CarryText(TDL_ECableCarry carries)
	{
		string text = string.Empty;
		if (carries & TDL_ECableCarry.DATA)
			text = text + "D";
		if (carries & TDL_ECableCarry.POWER)
			text = text + "P";
		if (carries & TDL_ECableCarry.AUDIO)
			text = text + "A";
		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! Draws every port and cable for one frame. A cable turns red when its ports are further
	//! apart than it is long.
	void DebugDraw()
	{
		if (!m_Carrier)
			return;

		BaseWorld world = m_Carrier.GetWorld();
		vector mat[4];

		for (int n = 0; n < m_aNodes.Count(); n++)
		{
			TDL_CablePortsComponent node = m_aNodes[n];
			if (!node)
				continue;

			for (int p = 0; p < node.GetPortCount(); p++)
			{
				if (!node.GetPortTransform(p, mat))
					continue;

				int colour = 0xFF9E9E9E;
				if (m_aPortUsed[Element(n, p)] != 0)
					colour = 0xFF4CD964;

				Shape.CreateSphere(colour, ShapeFlags.ONCE | ShapeFlags.NOZBUFFER, mat[3], 0.006);
				DebugTextWorldSpace.Create(world, node.GetKind() + "." + node.GetPortId(p), DebugTextFlags.ONCE | DebugTextFlags.CENTER | DebugTextFlags.FACE_CAMERA, mat[3][0], mat[3][1] + 0.02, mat[3][2], 8, colour, 0x80000000);
			}
		}

		vector ends[2];
		foreach (TDL_CableLink link : m_aLinks)
		{
			if (!link.m_A || !link.m_B)
				continue;
			if (!link.m_A.GetPortTransform(link.m_iPortA, mat))
				continue;
			ends[0] = mat[3];
			if (!link.m_B.GetPortTransform(link.m_iPortB, mat))
				continue;
			ends[1] = mat[3];

			float span = vector.Distance(ends[0], ends[1]);
			int lineColour = 0xFF4CD964;
			if (span > link.m_fLengthM)
				lineColour = 0xFFFF3B30;

			Shape.CreateLines(lineColour, ShapeFlags.ONCE | ShapeFlags.NOZBUFFER, ends, 2);

			vector mid = (ends[0] + ends[1]) * 0.5;
			string label = string.Format("%1  %2 / %3 m", CarryText(link.m_eCarries), span.ToString(-1, 2), link.m_fLengthM.ToString(-1, 2));
			DebugTextWorldSpace.Create(world, label, DebugTextFlags.ONCE | DebugTextFlags.CENTER | DebugTextFlags.FACE_CAMERA, mid[0], mid[1], mid[2], 8, lineColour, 0x80000000);
		}
	}
}

//! Works out a character's cables from what they carry. Inventory is already replicated, so
//! every machine derives the same answer and nothing about cables goes over the network.
class TDL_CableManager
{
	// Gear changes a few times a minute at most; half a second keeps the answer fresh
	// without walking the inventory every time a device asks.
	protected static const float REFRESH_MS = 500;

	protected static ref map<IEntity, ref TDL_CableState> s_mStates;
	protected static float s_fLastDebugTime = -1;
	protected static string s_sLastDebugText;

	//------------------------------------------------------------------------------------------------
	//! Current cables on a character, at most REFRESH_MS old.
	static TDL_CableState GetState(IEntity carrier)
	{
		if (!carrier)
			return null;

		if (!s_mStates)
			s_mStates = new map<IEntity, ref TDL_CableState>();

		float now = carrier.GetWorld().GetWorldTime();
		TDL_CableState state = s_mStates.Get(carrier);
		if (state && now >= state.GetResolvedAt() && now - state.GetResolvedAt() < REFRESH_MS)
			return state;

		for (int i = s_mStates.Count() - 1; i >= 0; i--)
		{
			if (!s_mStates.GetKey(i))
				s_mStates.RemoveElement(i);
		}

		array<TDL_CablePortsComponent> nodes = new array<TDL_CablePortsComponent>();
		array<string> stowed = new array<string>();
		Collect(carrier, nodes, stowed);

		state = new TDL_CableState(carrier, now, nodes, stowed);
		s_mStates.Set(carrier, state);
		return state;
	}

	//------------------------------------------------------------------------------------------------
	//! True when two items on the same character can pass the given thing to each other.
	//! \param carry a single TDL_ECableCarry bit
	static bool AreWired(IEntity carrier, IEntity itemA, IEntity itemB, TDL_ECableCarry carry)
	{
		TDL_CableState state = GetState(carrier);
		if (!state)
			return false;
		return state.AreWired(itemA, itemB, carry);
	}

	//------------------------------------------------------------------------------------------------
	//! Draws the locally controlled character's cables. Safe to call from several items in
	//! one frame; only the first call draws.
	static void DebugDrawLocal()
	{
		IEntity carrier = SCR_PlayerController.GetLocalControlledEntity();
		if (!carrier)
			return;

		float now = carrier.GetWorld().GetWorldTime();
		if (now == s_fLastDebugTime)
			return;
		s_fLastDebugTime = now;

		TDL_CableState state = GetState(carrier);
		if (!state)
			return;

		state.DebugDraw();

		string text = state.Describe();
		if (text == s_sLastDebugText)
			return;
		s_sLastDebugText = text;
		Print("[TDL_CABLE] " + text, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! True when an item is somewhere a cable to it can be seen: in the hand or riding on gear,
	//! not put away in a pocket or pack.
	static bool IsOnShow(IEntity item, IEntity carrier)
	{
		SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.Cast(carrier.FindComponent(SCR_GadgetManagerComponent));
		if (gadgets && gadgets.GetHeldGadget() == item)
			return true;
		return !IsStowed(item, carrier);
	}

	//------------------------------------------------------------------------------------------------
	// Same reach as the device walk in AG0_TDLSystem: the held gadget, everything in
	// inventory, and items riding on worn gear.
	protected static void Collect(IEntity carrier, notnull array<TDL_CablePortsComponent> nodes, notnull array<string> stowed)
	{
		array<IEntity> candidates = new array<IEntity>();
		IEntity held;

		SCR_GadgetManagerComponent gadgets = SCR_GadgetManagerComponent.Cast(carrier.FindComponent(SCR_GadgetManagerComponent));
		if (gadgets)
		{
			held = gadgets.GetHeldGadget();
			if (held)
				candidates.Insert(held);
		}

		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(carrier.FindComponent(InventoryStorageManagerComponent));
		if (inventory)
		{
			array<IEntity> items = new array<IEntity>();
			inventory.GetItems(items);
			foreach (IEntity item : items)
				candidates.Insert(item);
		}

		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(carrier.FindComponent(EquipedLoadoutStorageComponent));
		if (loadout)
		{
			array<typename> areas = new array<typename>();
			areas.Insert(LoadoutHeadCoverArea);
			areas.Insert(LoadoutArmoredVestSlotArea);
			areas.Insert(LoadoutVestArea);
			areas.Insert(LoadoutJacketArea);
			areas.Insert(LoadoutBackpackArea);

			foreach (typename area : areas)
			{
				IEntity cloth = loadout.GetClothFromArea(area);
				if (!cloth)
					continue;

				ClothNodeStorageComponent clothNodes = ClothNodeStorageComponent.Cast(cloth.FindComponent(ClothNodeStorageComponent));
				if (!clothNodes)
					continue;

				array<IEntity> clothItems = new array<IEntity>();
				clothNodes.GetAll(clothItems);
				foreach (IEntity clothItem : clothItems)
					candidates.Insert(clothItem);
			}
		}

		bool stowedConnect = false;
		TDL_CableRulesConfig config = TDL_CableRulesConfig.Get();
		if (config)
			stowedConnect = config.DoStowedItemsConnect();

		array<int> ids = new array<int>();
		foreach (IEntity candidate : candidates)
		{
			TDL_CablePortsComponent ports = TDL_CablePortsComponent.Of(candidate);
			if (!ports || nodes.Contains(ports))
				continue;

			if (candidate != held && !stowedConnect && IsStowed(candidate, carrier))
			{
				stowed.Insert(ports.GetKind());
				continue;
			}

			// Replication ids are the same on every machine, so sorting by them makes two
			// identical radios pair up with the same partners everywhere.
			int id = -1;
			RplComponent rpl = RplComponent.Cast(candidate.FindComponent(RplComponent));
			if (rpl)
				id = rpl.Id();

			int at = ids.Count();
			while (at > 0 && ids[at - 1] > id)
				at = at - 1;
			ids.InsertAt(id, at);
			nodes.InsertAt(ports, at);
		}
	}

	//------------------------------------------------------------------------------------------------
	// An item is stowed when it, or anything holding it on the way up to the character, is
	// put away where the game does not draw it. Stored like that it is hidden and parked at
	// the character's feet, so a cable to it would run down through the body.
	//
	// Storage purpose does not tell the two apart: a vest keeps the pouches drawn on it and
	// the loose items in its pockets in the same storage. What does is the slot an item sits
	// in and whether the game has hidden it.
	protected static bool IsStowed(IEntity item, IEntity carrier)
	{
		IEntity current = item;
		for (int depth = 0; depth < 8; depth++)
		{
			InventoryItemComponent inventoryItem = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (!inventoryItem)
				return false;

			InventoryStorageSlot slot = inventoryItem.GetParentSlot();
			if (!slot)
				return false;

			BaseInventoryStorageComponent storage = slot.GetStorage();
			if (!storage)
				return false;

			IEntity holder = storage.GetOwner();
			if (!holder || holder == carrier)
				return false;

			// A port or a gear slot draws what is in it.
			bool drawnSlot = TDL_AccessoryPortSlot.Cast(slot) != null || LoadoutSlotInfo.Cast(slot) != null;
			if (!drawnSlot)
			{
				if ((current.GetFlags() & EntityFlags.VISIBLE) == 0)
					return true;

				// A pocket hands out plain slots as things are dropped in. A slot of any
				// more particular kind was placed on the gear by hand, like the one that
				// holds a radio in its pouch, and shows what it holds.
				if (UniversalInventoryStorageComponent.Cast(storage) != null && slot.Type() == InventoryStorageSlot)
					return true;
			}

			current = holder;
		}
		return false;
	}
}
