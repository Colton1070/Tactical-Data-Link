/**
 * Vehicle Radio System
 *
 * Provides synchronized multi-transceiver radio access for eligible vehicle crew.
 * Each qualifying occupant receives a virtual radio spawned server-side and
 * replicated to their inventory, with one VON entry per configured channel.
 *
 * SYNC ARCHITECTURE
 *
 * Frequency synchronization uses RplProp on m_aFrequencies:
 *
 *   1. Any crew member tunes a transceiver via the VON wheel.
 *   2. OnTansceiverFrequencyChanged fires on the server.
 *   3. The server checks m_aFrequencies - if the frequency changed, updates the
 *      array and calls Replication.BumpMe() to broadcast it to all clients.
 *   4. OnFrequenciesReplicated fires on each client; if that client has this
 *      vehicle's radio, it sets the transceiver frequency locally.
 *   5. The server sees the echo from step 4 via OnTansceiverFrequencyChanged,
 *      but the early-return array check stops any further action.
 *
 * PREFAB REQUIREMENTS
 *
 * Vehicle entity:
 *   - EventHandlerManagerComponent  (standard on vehicles)
 *   - RplComponent                  (standard on vehicles)
 *   - SCR_VehicleRadioComponent     (this component)
 *
 * Virtual radio prefab (e.g. VehicleRadioItem.et):
 *   - BaseRadioComponent  - N transceivers matching m_aChannelNames count
 *   - SCR_RadioComponent  - category MANPACK, m_bCanBeHeld = 0
 *   - InventoryItemComponent
 *   - RplComponent
 *   - MeshObject / RigidBody can be empty shells (invisible virtual item)
 *
 * COMPARTMENT FILTERING
 *
 * m_aAllowedCompartmentNames accepts compartment name strings as defined in the
 * vehicle prefab (e.g. "Pilot", "CoDriver", "Gunner").
 * Leave the array empty to grant all compartments access.
 * Names are resolved via BaseCompartmentManagerComponent.FindCompartmentByName and
 * compared by pointer equality against the slot from the compartment entered event.
 */

// ============================================================================
// VON Entry
// ============================================================================

//! Suppresses the inventory name toast that fires when the virtual radio enters
//! the player's gadget slot - we never want "Vehicle Radio" flashing on screen
class SCR_VONEntryVehicleRadio : SCR_VONEntryRadio
{
	override UIInfo GetUIInfo()
	{
		return null;
	}

	ref ScriptInvoker m_OnDeselected = new ScriptInvoker();

	bool IsSelected()
	{
		return m_bIsSelected;
	}

	//! Fire the deselect invoker so the component can sync authoritative frequencies
	//! the moment this entry stops being the active one in the VON wheel.
	override void SetSelected(bool state)
	{
		bool wasSelected = m_bIsSelected;
		super.SetSelected(state);
		if (wasSelected && !state)
			m_OnDeselected.Invoke();
	}

	//! Always enforce the overwrite value when one is set.
	override void SetChannelText(string channel)
	{
		if (!m_sChannelTextOverwrite.IsEmpty())
			super.SetChannelText(m_sChannelTextOverwrite);
		else
			super.SetChannelText(channel);
	}
}

// ============================================================================
// Component Class
// ============================================================================

[ComponentEditorProps(category: "GameScripted/Vehicle", description: "Vehicle radio - synchronized multi-transceiver VON radio for crew. Configure channel names and compartment access here.")]
class SCR_VehicleRadioComponentClass : ScriptComponentClass
{
}

// ============================================================================
// Component
// ============================================================================

class SCR_VehicleRadioComponent : ScriptComponent
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Virtual radio prefab (transceiver count must match channel names)", "et", category: "Radio")]
	protected ResourceName m_sVirtualRadioPrefab;

	[Attribute("", UIWidgets.Auto, "Channel names shown in VON wheel - one per transceiver", category: "Radio")]
	protected ref array<string> m_aChannelNames;

	[Attribute("", UIWidgets.Auto, "Allowed compartment names (must match prefab exactly). Empty = all.", category: "Radio")]
	protected ref array<string> m_aAllowedCompartmentNames;

	[Attribute("", UIWidgets.EditBox, "Encryption key override. Empty = faction default.", category: "Radio")]
	protected string m_sEncryptionKeyOverride;

	// ----------------------------------------------------------------
	// Runtime State - Server
	// ----------------------------------------------------------------

	[RplProp(onRplName: "OnFrequenciesReplicated")]
	protected ref array<int> m_aFrequencies = new array<int>();

	protected ref array<int>                    m_aTrackedPlayerIds    = new array<int>();
	protected ref array<IEntity>                m_aTrackedEntities     = new array<IEntity>();
	protected ref array<BaseRadioComponent>     m_aTrackedRadioComps   = new array<BaseRadioComponent>();

	// ----------------------------------------------------------------
	// Runtime State - Local Client
	// ----------------------------------------------------------------

	protected IEntity m_LocalRadioEntity;
	protected BaseRadioComponent m_LocalRadioComp;

	protected ref array<ref SCR_VONEntryVehicleRadio> m_aLocalEntries = new array<ref SCR_VONEntryVehicleRadio>();

	protected string m_sEncryptionKey;

	// ----------------------------------------------------------------
	// Lifecycle
	// ----------------------------------------------------------------

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		if (!GetGame().InPlayMode())
			return;

		if (m_sVirtualRadioPrefab.IsEmpty())
			return;

		if (m_sEncryptionKeyOverride != string.Empty)
		{
			m_sEncryptionKey = m_sEncryptionKeyOverride;
		}
		else
		{
			FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(owner.FindComponent(FactionAffiliationComponent));
			if (factionComp && factionComp.GetAffiliatedFaction())
				m_sEncryptionKey = factionComp.GetAffiliatedFaction().GetFactionRadioEncryptionKey();
		}

		EventHandlerManagerComponent eventHandler = EventHandlerManagerComponent.Cast(owner.FindComponent(EventHandlerManagerComponent));
		if (!eventHandler)
			return;

		eventHandler.RegisterScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered, false);
		eventHandler.RegisterScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft, false);

		if (Replication.IsServer())
			AG0_RadioCryptoFillBridge.s_OnKeyReceived.Insert(OnBridgeKeyReceived);
	}

	void ~SCR_VehicleRadioComponent()
	{
		GetGame().GetCallqueue().Remove(PollForVehicleRadio);
		CleanupLocalEntries();

		AG0_RadioCryptoFillBridge.s_OnKeyReceived.Remove(OnBridgeKeyReceived);
	}

	// ----------------------------------------------------------------
	// Compartment Filtering
	// ----------------------------------------------------------------

	protected bool IsCompartmentAllowed(BaseCompartmentManagerComponent mgr, int slotID, int managerID)
	{
		if (!m_aAllowedCompartmentNames || m_aAllowedCompartmentNames.IsEmpty())
			return true;

		BaseCompartmentSlot incomingSlot = mgr.FindCompartment(slotID, managerID);
		if (!incomingSlot)
			return false;

		foreach (string name : m_aAllowedCompartmentNames)
		{
			BaseCompartmentSlot namedSlot = mgr.FindCompartmentByName(name);
			if (namedSlot && namedSlot == incomingSlot)
				return true;
		}

		return false;
	}

	// ----------------------------------------------------------------
	// Compartment Events
	// ----------------------------------------------------------------

	protected void OnCompartmentEntered(IEntity vehicle, BaseCompartmentManagerComponent mgr, IEntity occupant, int managerID, int slotID)
	{
		if (!IsCompartmentAllowed(mgr, slotID, managerID))
			return;

		if (Replication.IsServer())
		{
			int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(occupant);
			if (playerId > 0)
				SpawnVehicleRadio(playerId);
		}

		if (IsLocalPlayer(occupant))
			GetGame().GetCallqueue().CallLater(PollForVehicleRadio, 100, true);
	}

	protected void OnCompartmentLeft(IEntity vehicle, BaseCompartmentManagerComponent mgr, IEntity occupant, int managerID, int slotID)
	{
		if (Replication.IsServer())
		{
			int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(occupant);
			if (playerId > 0)
				RemovePlayerRadio(playerId);
		}

		if (IsLocalPlayer(occupant))
		{
			GetGame().GetCallqueue().Remove(PollForVehicleRadio);
			CleanupLocalEntries();
		}
	}

	// ----------------------------------------------------------------
	// Server - Tracked Player Helpers
	// ----------------------------------------------------------------

	protected int FindTrackedIndex(int playerId)
	{
		for (int i = 0, count = m_aTrackedPlayerIds.Count(); i < count; i++)
		{
			if (m_aTrackedPlayerIds[i] == playerId)
				return i;
		}
		return -1;
	}

	protected void TrackPlayer(int playerId, IEntity radioEntity, BaseRadioComponent radioComp)
	{
		m_aTrackedPlayerIds.Insert(playerId);
		m_aTrackedEntities.Insert(radioEntity);
		m_aTrackedRadioComps.Insert(radioComp);
	}

	protected void UntrackIndex(int index)
	{
		m_aTrackedPlayerIds.RemoveOrdered(index);
		m_aTrackedEntities.RemoveOrdered(index);
		m_aTrackedRadioComps.RemoveOrdered(index);
	}

	// ----------------------------------------------------------------
	// Server - Spawn / Remove
	// ----------------------------------------------------------------

	protected void SpawnVehicleRadio(int playerId)
	{
		if (FindTrackedIndex(playerId) != -1)
			return;

		IEntity player = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!player)
			return;

		EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		player.GetWorldTransform(spawnParams.Transform);

		IEntity radioEntity = GetGame().SpawnEntityPrefab(Resource.Load(m_sVirtualRadioPrefab), GetGame().GetWorld(), spawnParams);
		if (!radioEntity)
			return;

		BaseRadioComponent radioComp = BaseRadioComponent.Cast(radioEntity.FindComponent(BaseRadioComponent));
		if (!radioComp)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(radioEntity);
			return;
		}

		if (m_aFrequencies.IsEmpty())
			InitialiseFrequenciesFromRadio(radioComp);

		ConfigureRadioServer(radioComp);

		radioComp.m_OnTansceiverFrequencyChangedInvoker.Insert(OnServerTransceiverFrequencyChanged);

		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(player.FindComponent(InventoryStorageManagerComponent));
		if (!inventory)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(radioEntity);
			return;
		}

		inventory.TryInsertItem(radioEntity);

		TrackPlayer(playerId, radioEntity, radioComp);
	}

	protected void InitialiseFrequenciesFromRadio(BaseRadioComponent radioComp)
	{
		int count = radioComp.TransceiversCount();
		m_aFrequencies.Resize(count);

		for (int i = 0; i < count; i++)
		{
			BaseTransceiver tsv = radioComp.GetTransceiver(i);
			if (tsv)
				m_aFrequencies[i] = tsv.GetFrequency();
			else
				m_aFrequencies[i] = 0;
		}
	}

	protected void ConfigureRadioServer(BaseRadioComponent radioComp)
	{
		radioComp.SetPower(true);

		if (m_sEncryptionKey != string.Empty)
			radioComp.SetEncryptionKey(m_sEncryptionKey);

		int tsvCount = radioComp.TransceiversCount();
		for (int i = 0; i < tsvCount && i < m_aFrequencies.Count(); i++)
		{
			BaseTransceiver tsv = radioComp.GetTransceiver(i);
			if (!tsv)
				continue;

			radioComp.SetTransceiverFrequency(tsv, m_aFrequencies[i]);
		}

		if (!m_sCurrentCryptoKey.IsEmpty())
		{
			AG0_TDLRadioComponent tdlComp = AG0_TDLRadioComponent.Cast(radioComp.GetOwner().FindComponent(AG0_TDLRadioComponent));
			if (tdlComp)
				tdlComp.SetCryptoKeyDirectly(m_sCurrentCryptoKey);
		}
	}

	protected void RemovePlayerRadio(int playerId)
	{
		int index = FindTrackedIndex(playerId);
		if (index == -1)
			return;

		BaseRadioComponent radioComp = m_aTrackedRadioComps[index];

		if (radioComp)
			radioComp.m_OnTansceiverFrequencyChangedInvoker.Remove(OnServerTransceiverFrequencyChanged);

		UntrackIndex(index);

		IEntity player = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!player)
			return;

		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(player.FindComponent(InventoryStorageManagerComponent));
		if (!inventory)
			return;

		array<IEntity> items = {};
		SCR_PrefabNamePredicate predicate = new SCR_PrefabNamePredicate();
		predicate.prefabName = m_sVirtualRadioPrefab;
		inventory.FindItems(items, predicate);

		foreach (IEntity item : items)
		{
			if (!item)
				continue;

			inventory.TryRemoveItemFromStorage(item, null);
			inventory.TryDeleteItem(item);
		}
	}

	// ----------------------------------------------------------------
	// Server - Frequency Sync
	// ----------------------------------------------------------------

	protected void OnServerTransceiverFrequencyChanged(BaseTransceiver transceiver, int oldFreq, int newFreq)
	{
		int sourceIndex      = -1;
		int transceiverIndex = -1;

		for (int p = 0, playerCount = m_aTrackedRadioComps.Count(); p < playerCount; p++)
		{
			BaseRadioComponent comp = m_aTrackedRadioComps[p];
			if (!comp)
				continue;

			int tsvCount = comp.TransceiversCount();
			for (int i = 0; i < tsvCount; i++)
			{
				if (comp.GetTransceiver(i) == transceiver)
				{
					sourceIndex      = p;
					transceiverIndex = i;
					break;
				}
			}

			if (transceiverIndex != -1)
				break;
		}

		if (transceiverIndex == -1)
			return;

		if (transceiverIndex < m_aFrequencies.Count() && m_aFrequencies[transceiverIndex] == newFreq)
			return;

		if (transceiverIndex >= m_aFrequencies.Count())
			m_aFrequencies.Resize(transceiverIndex + 1);

		m_aFrequencies[transceiverIndex] = newFreq;

		Replication.BumpMe();
	}

	// ----------------------------------------------------------------
	// Client - Poll / Setup / Cleanup
	// ----------------------------------------------------------------

	protected void PollForVehicleRadio()
	{
		if (m_LocalRadioEntity)
		{
			GetGame().GetCallqueue().Remove(PollForVehicleRadio);
			return;
		}

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity player = pc.GetControlledEntity();
		if (!player)
			return;

		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(player.FindComponent(InventoryStorageManagerComponent));
		if (!inventory)
			return;

		array<IEntity> items = {};
		SCR_PrefabNamePredicate predicate = new SCR_PrefabNamePredicate();
		predicate.prefabName = m_sVirtualRadioPrefab;

		if (inventory.FindItems(items, predicate) <= 0 || items.IsEmpty())
			return;

		GetGame().GetCallqueue().Remove(PollForVehicleRadio);
		SetupLocalRadio(items[0], pc);
	}

	protected void SetupLocalRadio(IEntity radioEntity, PlayerController pc)
	{
		m_LocalRadioEntity = radioEntity;
		m_LocalRadioComp   = BaseRadioComponent.Cast(radioEntity.FindComponent(BaseRadioComponent));
		if (!m_LocalRadioComp)
			return;

		m_LocalRadioComp.SetPower(true);
		if (m_sEncryptionKey != string.Empty)
			m_LocalRadioComp.SetEncryptionKey(m_sEncryptionKey);

		SCR_VONController vonController = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
		if (!vonController)
			return;

		SCR_RadioComponent radioGadget = SCR_RadioComponent.Cast(radioEntity.FindComponent(SCR_RadioComponent));
		if (!radioGadget)
			return;

		array<ref SCR_VONEntry> existingEntries = {};
		vonController.GetVONEntries(existingEntries);
		foreach (SCR_VONEntry entry : existingEntries)
		{
			SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
			if (radioEntry && radioEntry.GetTransceiver() && radioEntry.GetTransceiver().GetRadio() == m_LocalRadioComp)
				vonController.RemoveEntry(entry);
		}

		int transceiverCount = m_LocalRadioComp.TransceiversCount();
		int nameCount        = 0;
		if (m_aChannelNames)
			nameCount = m_aChannelNames.Count();

		for (int i = 0; i < transceiverCount; i++)
		{
			BaseTransceiver tsv = m_LocalRadioComp.GetTransceiver(i);
			if (!tsv)
				continue;

			string channelName;
			if (i < nameCount)
				channelName = m_aChannelNames[i];
			else
				channelName = string.Format("CH%1", i + 1);

			SCR_VONEntryVehicleRadio entry = new SCR_VONEntryVehicleRadio();
			entry.SetRadioEntry(tsv, i + 1, radioGadget);
			entry.SetChannelTextOverwrite(channelName);
			entry.m_OnDeselected.Insert(SyncAuthoritativeFrequencies);
			vonController.AddEntry(entry);

			m_aLocalEntries.Insert(entry);
		}
	}

	protected void OnFrequenciesReplicated()
	{
		if (!m_LocalRadioComp)
			return;

		for (int i = 0, count = m_aFrequencies.Count(); i < count; i++)
		{
			BaseTransceiver tsv = m_LocalRadioComp.GetTransceiver(i);
			if (!tsv || tsv.GetFrequency() == m_aFrequencies[i])
				continue;

			if (i < m_aLocalEntries.Count() && m_aLocalEntries[i] && m_aLocalEntries[i].IsSelected())
				continue;

			tsv.SetFrequency(m_aFrequencies[i]);

			if (i < m_aLocalEntries.Count() && m_aLocalEntries[i])
			{
				m_aLocalEntries[i].AdjustEntryModif(0);
				m_aLocalEntries[i].Update();
			}
		}
	}

	protected void SyncAuthoritativeFrequencies()
	{
		if (!m_LocalRadioComp)
			return;

		for (int i = 0, count = m_aFrequencies.Count(); i < count; i++)
		{
			BaseTransceiver tsv = m_LocalRadioComp.GetTransceiver(i);
			if (tsv && tsv.GetFrequency() != m_aFrequencies[i])
				tsv.SetFrequency(m_aFrequencies[i]);
		}
	}

	protected void CleanupLocalEntries()
	{
		PlayerController pc = GetGame().GetPlayerController();
		SCR_VONController vonController;
		if (pc)
			vonController = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));

		foreach (SCR_VONEntryVehicleRadio entry : m_aLocalEntries)
		{
			if (vonController)
				vonController.RemoveEntry(entry);
		}
		m_aLocalEntries.Clear();

		m_LocalRadioEntity = null;
		m_LocalRadioComp   = null;
	}

	// ----------------------------------------------------------------
	// Utility
	// ----------------------------------------------------------------

	protected bool IsLocalPlayer(IEntity entity)
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		return entity == pc.GetControlledEntity();
	}

	// ----------------------------------------------------------------
	// TDL Integration
	// ----------------------------------------------------------------

	//! Authoritative crypto key for all crew radios on this vehicle.
	//! Empty string = unfilled (default). Propagated to all tracked crew on change,
	//! replicated to clients so each applies it to their local virtual radio.
	[RplProp(onRplName: "OnCryptoKeyReplicated")]
	protected string m_sCurrentCryptoKey;

	protected void OnCryptoKeyReplicated()
	{
		if (!m_LocalRadioEntity)
			return;

		AG0_TDLRadioComponent tdlComp = AG0_TDLRadioComponent.Cast(m_LocalRadioEntity.FindComponent(AG0_TDLRadioComponent));
		if (tdlComp)
			tdlComp.SetCryptoKeyDirectly(m_sCurrentCryptoKey);
	}

	protected void OnVehicleCryptoKeyChanged(string newKey)
	{
		if (newKey == m_sCurrentCryptoKey)
			return;

		m_sCurrentCryptoKey = newKey;

		foreach (IEntity radioEntity : m_aTrackedEntities)
		{
			AG0_TDLRadioComponent tdlComp = AG0_TDLRadioComponent.Cast(radioEntity.FindComponent(AG0_TDLRadioComponent));
			if (tdlComp && tdlComp.GetCurrentCryptoKey() != newKey)
				tdlComp.SetCryptoKeyDirectly(newKey);
		}

		Replication.BumpMe();
	}

	bool CanShowFillAction()
	{
		return true;
	}

	bool GetFillActionName(out string outName)
	{
		if (!m_sCurrentCryptoKey.IsEmpty())
			outName = "Drop Radio Key Fill";
		else
			outName = "Fill Radio Key";

		return true;
	}

	void PerformFillAction(IEntity user)
	{
		if (!Replication.IsServer())
			return;

		if (!m_sCurrentCryptoKey.IsEmpty())
		{
			SetCryptoKeyFromFill(string.Empty);
			return;
		}

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		SCR_PlayerController pc = SCR_PlayerController.Cast(
			GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!pc)
			return;

		pc.AskOpenRadioCryptoFill(Replication.FindItemId(GetOwner()));
	}

	protected void OnBridgeKeyReceived(RplId entityRplId, string key)
	{
		if (Replication.FindItemId(GetOwner()) != entityRplId)
			return;

		SetCryptoKeyFromFill(key);
	}

	void SetCryptoKeyFromFill(string key)
	{
		if (!Replication.IsServer())
			return;

		OnVehicleCryptoKeyChanged(key);
	}
}
