//! Device sub-mesh drawn while one accessory is fitted to a port.
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sAccessoryKey")]
class TDL_PortMeshRule
{
	[Attribute("", UIWidgets.EditBox, "Accessory key this rule draws, e.g. PS_ANT_2005")]
	protected string m_sAccessoryKey;

	[Attribute("", UIWidgets.EditBox, "Sub-mesh on the device model shown while that accessory is fitted, e.g. MPU5_Ant2005_LOD0")]
	protected string m_sMeshName;

	string GetKey() { return m_sAccessoryKey; }
	string GetMeshName() { return m_sMeshName; }
}

//! One accessory port: what it takes and how a fitted accessory is drawn. The port is the
//! slot itself, so its pivot, kind and rules are authored together in InitialStorageSlots.
class TDL_AccessoryPortSlot : SCR_EquipmentStorageSlot
{
	[Attribute("0", UIWidgets.ComboBox, "Kind of accessory this port takes", "", ParamEnumArray.FromEnum(TDL_EAccessoryKind), category: "TDL Port")]
	protected TDL_EAccessoryKind m_eKind;

	[Attribute(desc: "Accessory keys this port takes. Empty takes any accessory of the right kind, which is what an RF port wants: a wrong-band set still screws on, it just never links", category: "TDL Port")]
	protected ref array<string> m_aAcceptedKeys;

	[Attribute(desc: "Device sub-meshes that stand in for fitted accessories. An accessory with no rule is drawn as its own item at the port pivot", category: "TDL Port")]
	protected ref array<ref TDL_PortMeshRule> m_aMeshRules;

	TDL_EAccessoryKind GetKind() { return m_eKind; }
	array<ref TDL_PortMeshRule> GetMeshRules() { return m_aMeshRules; }

	bool Accepts(IEntity item)
	{
		TDL_AccessoryComponent accessory = TDL_AccessoryComponent.Of(item);
		if (!accessory || accessory.GetKind() != m_eKind)
			return false;
		if (!m_aAcceptedKeys || m_aAcceptedKeys.Count() == 0)
			return true;
		return m_aAcceptedKeys.Contains(accessory.GetKey());
	}

	string GetMeshNameFor(string key)
	{
		if (!m_aMeshRules)
			return string.Empty;
		foreach (TDL_PortMeshRule rule : m_aMeshRules)
		{
			if (rule && rule.GetKey() == key)
				return rule.GetMeshName();
		}
		return string.Empty;
	}

	bool HasMeshFor(string key)
	{
		if (!m_aMeshRules)
			return false;
		foreach (TDL_PortMeshRule rule : m_aMeshRules)
		{
			if (rule && rule.GetKey() == key)
				return true;
		}
		return false;
	}
}

[ComponentEditorProps(category: "TDL/Accessories", description: "Accessory ports on a TDL device: antenna set, GPS antenna, battery")]
class TDL_AccessoryPortsComponentClass : SCR_EquipmentStorageComponentClass {}

//! Every port on a device in one storage, gated by data on the port slots rather than by a
//! storage class per item. The device component reads what is fitted through GetFitted;
//! nothing here is replicated because inventory contents already are, so every machine
//! derives the same meshes and the same capabilities from the same slots.
class TDL_AccessoryPortsComponent : SCR_EquipmentStorageComponent
{
	protected AG0_TDLDeviceComponent m_Device;
	protected ref map<string, int> m_mMeshIndices = new map<string, int>();

	// Every live port storage. A mount's ports sit under its universal storage, where a
	// component search from the character does not reliably reach, so they are listed here.
	protected static ref array<TDL_AccessoryPortsComponent> s_aAll;

	// Kept outside WORKBENCH so prefabs that set it still load cleanly in game builds,
	// where it does nothing.
	[Attribute("", UIWidgets.EditBox, "Workbench edit mode only: accessory keys to draw as fitted, comma-separated, e.g. PS_ANT_2005,PS_GPS_STUB. Each port shows the first key it has a mesh rule for", category: "TDL Preview")]
	protected string m_sPreviewKeys;

	//------------------------------------------------------------------------------------------------
	override protected void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// The device registers with this storage rather than searching for it, because the
		// storage usually sits as a child of the item's universal storage and FindComponent
		// from the device side is not guaranteed to reach it.
		m_Device = AG0_TDLDeviceComponent.Cast(owner.FindComponent(AG0_TDLDeviceComponent));
		if (m_Device)
			m_Device.SetAccessoryPorts(this);

		if (!s_aAll)
			s_aAll = new array<TDL_AccessoryPortsComponent>();
		s_aAll.Insert(this);

		// Sub-mesh indices are not resolvable until the mesh object has initialised.
		if (GetGame())
			GetGame().GetCallqueue().CallLater(RefreshVisuals, 0, false, null);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnDelete(IEntity owner)
	{
		if (GetGame())
			GetGame().GetCallqueue().Remove(RefreshVisuals);
		if (s_aAll)
			s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	override bool OnOverrideCanStoreItem() { return true; }
	override bool OnOverrideCanReplaceItem() { return true; }

	//------------------------------------------------------------------------------------------------
	override bool CanStoreItem(IEntity item, int slotID)
	{
		if (slotID < 0)
			return FindFreePort(item) != null;

		TDL_AccessoryPortSlot port = GetPort(slotID);
		return port && !Get(slotID) && port.Accepts(item);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanReplaceItem(IEntity nextItem, int slotID)
	{
		TDL_AccessoryPortSlot port = GetPort(slotID);
		return port && port.Accepts(nextItem);
	}

	//------------------------------------------------------------------------------------------------
	override protected InventoryStorageSlot GetEmptySlotForItem(IEntity item)
	{
		return FindFreePort(item);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnAddedToSlot(IEntity item, int slotID)
	{
		super.OnAddedToSlot(item, slotID);
		RefreshVisuals(null);
		if (m_Device)
			m_Device.OnAccessoriesChanged();
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnRemovedFromSlot(IEntity item, int slotID)
	{
		super.OnRemovedFromSlot(item, slotID);

		// Undo the hide a mesh rule may have applied, or the item stays invisible wherever
		// it goes next.
		InventoryItemComponent itemComp = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (itemComp)
			itemComp.ShowOwner();

		RefreshVisuals(item);
		if (m_Device)
			m_Device.OnAccessoriesChanged();
	}

	//------------------------------------------------------------------------------------------------
	TDL_AccessoryPortSlot GetPort(int slotID)
	{
		if (slotID < 0 || slotID >= GetSlotsCount())
			return null;
		return TDL_AccessoryPortSlot.Cast(GetSlot(slotID));
	}

	//------------------------------------------------------------------------------------------------
	bool HasPort(TDL_EAccessoryKind kind)
	{
		for (int i = 0, count = GetSlotsCount(); i < count; i++)
		{
			TDL_AccessoryPortSlot port = GetPort(i);
			if (port && port.GetKind() == kind)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! The accessory in the first port of this kind that has one fitted, or null.
	TDL_AccessoryComponent GetFitted(TDL_EAccessoryKind kind)
	{
		for (int i = 0, count = GetSlotsCount(); i < count; i++)
		{
			TDL_AccessoryPortSlot port = GetPort(i);
			if (!port || port.GetKind() != kind)
				continue;
			TDL_AccessoryComponent accessory = TDL_AccessoryComponent.Of(Get(i));
			if (accessory)
				return accessory;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! A free port that takes the item, on something the character wears or holds in view.
	//! This is how a device finds a mount to dock in without the player opening the mount.
	//! \param[out] slotID Slot of the free port in the returned storage.
	static TDL_AccessoryPortsComponent FindFreeWornPort(IEntity carrier, IEntity item, out int slotID)
	{
		slotID = -1;
		if (!s_aAll || !carrier || !item)
			return null;

		foreach (TDL_AccessoryPortsComponent ports : s_aAll)
		{
			if (!ports)
				continue;

			IEntity holder = ports.GetOwner();
			if (!holder || holder == item || holder.GetRootParent() != carrier)
				continue;

			// A mount put away in a pack is not somewhere a device can be clipped into.
			if (!TDL_CableManager.IsOnShow(holder, carrier))
				continue;

			TDL_AccessoryPortSlot port = ports.FindFreePort(item);
			if (!port)
				continue;

			slotID = port.GetID();
			return ports;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected TDL_AccessoryPortSlot FindFreePort(IEntity item)
	{
		for (int i = 0, count = GetSlotsCount(); i < count; i++)
		{
			TDL_AccessoryPortSlot port = GetPort(i);
			if (port && !Get(i) && port.Accepts(item))
				return port;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the rule mesh for each fitted accessory and hides every other rule mesh. Each
	//! machine runs this from its own inventory callbacks; sub-mesh visibility is local.
	//! \param leaving Item mid-removal, treated as already gone.
	protected void RefreshVisuals(IEntity leaving)
	{
		array<string> fittedKeys = new array<string>();
		for (int i = 0, count = GetSlotsCount(); i < count; i++)
		{
			IEntity fitted = Get(i);
			TDL_AccessoryComponent accessory;
			if (fitted != leaving)
				accessory = TDL_AccessoryComponent.Of(fitted);

			string key;
			if (accessory)
				key = accessory.GetKey();
			fittedKeys.Insert(key);

			// The device mesh draws this accessory, so the item entity at the pivot would be
			// a second copy.
			TDL_AccessoryPortSlot port = GetPort(i);
			if (accessory && port && port.HasMeshFor(key))
			{
				InventoryItemComponent itemComp = InventoryItemComponent.Cast(fitted.FindComponent(InventoryItemComponent));
				if (itemComp)
					itemComp.HideOwner();
			}
		}

		ApplyMeshes(fittedKeys);
	}

	//------------------------------------------------------------------------------------------------
	//! \param keysBySlot Accessory key fitted in each slot, "" for empty, indexed like the slots.
	protected void ApplyMeshes(array<string> keysBySlot)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		set<string> shown = new set<string>();
		array<string> ruled = new array<string>();

		for (int i = 0, count = GetSlotsCount(); i < count; i++)
		{
			TDL_AccessoryPortSlot port = GetPort(i);
			if (!port || !port.GetMeshRules() || i >= keysBySlot.Count())
				continue;

			string key = keysBySlot[i];
			foreach (TDL_PortMeshRule rule : port.GetMeshRules())
			{
				if (!rule)
					continue;
				ruled.Insert(rule.GetMeshName());
				if (!key.IsEmpty() && rule.GetKey() == key)
					shown.Insert(rule.GetMeshName());
			}
		}

		foreach (string meshName : ruled)
		{
			SetMeshVisible(owner, meshName, shown.Contains(meshName));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void SetMeshVisible(IEntity owner, string meshName, bool visible)
	{
		int index;
		if (!m_mMeshIndices.Find(meshName, index))
		{
			index = ResolveMeshIndex(owner, meshName);
			m_mMeshIndices.Set(meshName, index);
			if (index < 0)
				Print(string.Format("[TDL_ACCESSORY] %1 has no sub-mesh '%2' or '%3'. Tick NoMerge on that mesh in the xob import settings; the global merge toggle alone still folds parts that share a material", owner, meshName, AlternateLodName(meshName)), LogLevel.WARNING);
		}

		if (index >= 0)
			GameAnimationUtils.ShowMesh(owner, index, visible);
	}

	//------------------------------------------------------------------------------------------------
	// Sub-meshes are named without the _LOD0 suffix in game, but rules are easiest to author
	// from the Blender object name, so both spellings resolve.
	protected int ResolveMeshIndex(IEntity owner, string meshName)
	{
		int index = GameAnimationUtils.FindMeshIndex(owner, meshName);
		if (index >= 0)
			return index;
		return GameAnimationUtils.FindMeshIndex(owner, AlternateLodName(meshName));
	}

	//------------------------------------------------------------------------------------------------
	//! The name with _LOD0 removed if it has it, added if it does not.
	protected static string AlternateLodName(string meshName)
	{
		const string lodSuffix = "_LOD0";
		int nameLength = meshName.Length();
		int suffixLength = lodSuffix.Length();
		if (nameLength > suffixLength && meshName.Substring(nameLength - suffixLength, suffixLength) == lodSuffix)
			return meshName.Substring(0, nameLength - suffixLength);
		return meshName + lodSuffix;
	}

#ifdef WORKBENCH
	//------------------------------------------------------------------------------------------------
	override int _WB_GetAfterWorldUpdateSpecs(IEntity owner, IEntitySource src)
	{
		return EEntityFrameUpdateSpecs.CALL_WHEN_ENTITY_VISIBLE;
	}

	//------------------------------------------------------------------------------------------------
	// Edit mode never spawns slot contents, so the preview keys stand in for fitted
	// accessories. Re-applied every frame because the editor rebuilds the entity whenever a
	// property changes, which resets sub-mesh visibility.
	protected override void _WB_AfterWorldUpdate(IEntity owner, float timeSlice)
	{
		if (!owner || !SCR_Global.IsEditMode(owner))
			return;

		array<string> preview = new array<string>();
		m_sPreviewKeys.Split(",", preview, true);
		for (int p = 0, previewCount = preview.Count(); p < previewCount; p++)
		{
			preview[p] = preview[p].Trim();
		}

		array<string> keysBySlot = new array<string>();
		for (int i = 0, count = GetSlotsCount(); i < count; i++)
		{
			TDL_AccessoryPortSlot port = GetPort(i);
			string key;
			if (port)
			{
				foreach (string candidate : preview)
				{
					if (port.HasMeshFor(candidate))
					{
						key = candidate;
						break;
					}
				}
				DrawPortLabel(owner, port, key);
			}
			keysBySlot.Insert(key);
		}

		ApplyMeshes(keysBySlot);
	}

	//------------------------------------------------------------------------------------------------
	protected void DrawPortLabel(IEntity owner, TDL_AccessoryPortSlot port, string key)
	{
		vector transform[4];
		port.GetWorldTransform(transform);

		string text = string.Format("%1 [%2]", port.GetSourceName(), typename.EnumToString(TDL_EAccessoryKind, port.GetKind()));
		int colour = Color.WHITE;
		if (!key.IsEmpty())
		{
			int index = ResolveMeshIndex(owner, port.GetMeshNameFor(key));
			text += string.Format("\n%1 -> %2", key, port.GetMeshNameFor(key));
			if (index < 0)
			{
				text += " (MESH NOT FOUND)";
				colour = Color.RED;
			}
			else
			{
				colour = Color.GREEN;
			}
		}

		DebugTextWorldSpace.Create(owner.GetWorld(), text, DebugTextFlags.ONCE | DebugTextFlags.CENTER | DebugTextFlags.FACE_CAMERA, transform[3][0], transform[3][1] + 0.03, transform[3][2], 10, colour, 0x80000000);
	}
#endif
}
