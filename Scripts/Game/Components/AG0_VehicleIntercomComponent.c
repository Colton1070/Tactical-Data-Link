/**
 * Vehicle Intercommunication System (ICS)
 *
 * Provides crew-only voice comms by spawning a virtual radio into each
 * occupant's inventory on vehicle entry. All radios are tuned to a
 * deterministic frequency derived from the vehicle's replication ID,
 * ensuring per-vehicle isolation. On exit the radio is removed.
 *
 * PREFAB REQUIREMENTS
 *
 * Vehicle entity:
 *   - EventHandlerManagerComponent (standard on vehicles)
 *   - RplComponent (standard on vehicles)
 *   - SCR_VehicleICSComponent (this component - all ICS config lives here)
 *
 * Virtual radio prefab (VehicleICSItem.et):
 *   - BaseRadioComponent  - one RadioTransceiver, wide freq range
 *   - SCR_RadioComponent  - radio category MANPACK (bypasses held-in-hand check)
 *   - InventoryItemComponent
 *   - RplComponent
 */

// ============================================================================
// VON Entry
// ============================================================================

//! Subclass that suppresses the inventory name toast and the frequency label in the VON wheel.
//! - GetUIInfo returns null so no item name toast fires on inventory insertion.
//! - AdjustEntryModif blanks m_sText so the frequency string never appears in Update().
//! - SetChannelText is overridden to be a no-op: SCR_VONMenu.OnAdjustEntry calls
//!   radioEntry.SetChannelText(GetKnownChannel(...)) after every frequency nudge, which
//!   would replace our "ICS" label with a squad/platoon name or empty string. Blocking
//!   that call keeps the label stable; InitEntry sets it correctly via m_sChannelTextOverwrite.
class SCR_VONEntryICS : SCR_VONEntryRadio
{
	override UIInfo GetUIInfo()
	{
		return null;
	}

	override void AdjustEntryModif(int modifier)
	{
		super.AdjustEntryModif(modifier);
		m_sText = string.Empty;
	}

	//! Always enforce the overwrite value when one is set.
	//! InitEntry calls SetChannelText(m_sChannelTextOverwrite) to populate m_sChannelText,
	//! and SCR_VONMenu.OnAdjustEntry calls SetChannelText(GetKnownChannel(...)) after every
	//! frequency nudge. By redirecting both calls through the overwrite we satisfy the init
	//! path while silently discarding the external clobber.
	override void SetChannelText(string channel)
	{
		if (!m_sChannelTextOverwrite.IsEmpty())
			super.SetChannelText(m_sChannelTextOverwrite);
		else
			super.SetChannelText(channel);
	}
}

// ============================================================================
// Transceiver Tracker
// ============================================================================

//! Static registry of ICS transceivers - used by the modded VonDisplay to
//! identify ICS transmissions and hide the frequency overlay
class SCR_ICSTransceiverTracker
{
	protected static ref set<BaseTransceiver> s_ICSTransceivers = new set<BaseTransceiver>();

	static void Register(BaseTransceiver tsv)
	{
		if (tsv)
			s_ICSTransceivers.Insert(tsv);
	}

	static void Unregister(BaseTransceiver tsv)
	{
		if (tsv)
			s_ICSTransceivers.RemoveItem(tsv);
	}

	static bool IsICS(BaseTransceiver tsv)
	{
		return tsv && s_ICSTransceivers.Contains(tsv);
	}
}

// ============================================================================
// VonDisplay Override
// ============================================================================

//! Hides the frequency text for ICS transmissions in the HUD
modded class SCR_VonDisplay : SCR_InfoDisplayExtended
{
	override event void OnCapture(BaseTransceiver transmitter)
	{
		super.OnCapture(transmitter);

		if (!m_OutTransmission || !m_OutTransmission.m_Widgets)
			return;

		if (SCR_ICSTransceiverTracker.IsICS(transmitter))
			m_OutTransmission.m_Widgets.m_wFrequency.SetVisible(false);
	}

	override event void OnReceive(int playerId, bool isSenderEditor, BaseTransceiver receiver, int frequency, float quality)
	{
		super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);

		if (!SCR_ICSTransceiverTracker.IsICS(receiver))
			return;

		TransmissionData data = m_aTransmissionMap.Get(playerId);
		if (data && data.m_Widgets)
			data.m_Widgets.m_wFrequency.SetVisible(false);
	}
}

// ============================================================================
// ICS Component
// ============================================================================

[ComponentEditorProps(category: "GameScripted/Vehicle", description: "Vehicle ICS - intercom VON for vehicle occupants. All radio configuration is self-contained; no BaseRadioComponent or SCR_RadioComponent required on the vehicle.")]
class SCR_VehicleICSComponentClass : ScriptComponentClass
{
}

class SCR_VehicleICSComponent : ScriptComponent
{
	[Attribute("ICS", UIWidgets.EditBox, "Display name shown in VON wheel", category: "ICS")]
	protected string m_sEntryName;

	[Attribute("{97C1C800BC7834D7}Prefabs/Items/Core/VehicleICSItem.et", UIWidgets.ResourceNamePicker, "Virtual ICS radio prefab", "et", category: "ICS")]
	protected ResourceName m_VirtualRadioPrefab;

	[Attribute("", UIWidgets.EditBox, "Encryption key override. If empty, falls back to faction radio encryption key.", category: "ICS")]
	protected string m_sEncryptionKeyOverride;

	[Attribute("500000", UIWidgets.EditBox, "Minimum frequency (Hz) for deterministic vehicle frequency pool", category: "ICS")]
	protected int m_iMinFrequency;

	[Attribute("1000000", UIWidgets.EditBox, "Maximum frequency (Hz) for deterministic vehicle frequency pool", category: "ICS")]
	protected int m_iMaxFrequency;

	[Attribute("200", UIWidgets.EditBox, "Transmit range (metres) applied to the spawned virtual radio. Server-authoritative.", category: "ICS")]
	protected float m_fTransmitRange;

	// Resolved once at init - explicit override first, faction key as fallback
	protected string m_sEncryptionKey;

	// Deterministic per-vehicle frequency computed from RplId
	protected int m_iVehicleFrequency;

	// Local player state (only set on the client that owns the occupant)
	protected IEntity m_VirtualRadioEntity;
	protected BaseRadioComponent m_VirtualRadioComp;
	protected ref SCR_VONEntryICS m_ICSEntry;

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

		// Resolve encryption key - prefer explicit override, fall back to faction
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

		// Validate frequency range
		if (m_iMinFrequency >= m_iMaxFrequency)
		{
			m_iMinFrequency = 500000;
			m_iMaxFrequency = 1000000;
		}

		// Register compartment events
		EventHandlerManagerComponent eventHandler = EventHandlerManagerComponent.Cast(owner.FindComponent(EventHandlerManagerComponent));
		if (!eventHandler)
			return;

		eventHandler.RegisterScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered, false);
		eventHandler.RegisterScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft, false);
	}

	protected int GetVehicleFrequency()
	{
		if (m_iVehicleFrequency == 0)
		{
			int rplId = Replication.FindItemId(GetOwner());
			m_iVehicleFrequency = m_iMinFrequency + (Math.AbsInt(rplId) % (m_iMaxFrequency - m_iMinFrequency));
		}

		return m_iVehicleFrequency;
	}

	void ~SCR_VehicleICSComponent()
	{
		GetGame().GetCallqueue().Remove(PollForICSRadio);
		CleanupLocalICSEntry();
	}

	// ----------------------------------------------------------------
	// Compartment Events
	// ----------------------------------------------------------------

	protected void OnCompartmentEntered(IEntity vehicle, BaseCompartmentManagerComponent mgr, IEntity occupant, int managerID, int slotID)
	{
		if (Replication.IsServer())
		{
			int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(occupant);
			if (playerId > 0)
				SpawnICSRadio(playerId);
		}

		if (IsLocalPlayer(occupant))
			GetGame().GetCallqueue().CallLater(PollForICSRadio, 100, true);
	}

	protected void OnCompartmentLeft(IEntity vehicle, BaseCompartmentManagerComponent mgr, IEntity occupant, int managerID, int slotID)
	{
		if (Replication.IsServer())
		{
			int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(occupant);
			if (playerId > 0)
				RemoveICSRadioServer(playerId);
		}

		if (IsLocalPlayer(occupant))
		{
			GetGame().GetCallqueue().Remove(PollForICSRadio);
			CleanupLocalICSEntry();
		}
	}

	// ----------------------------------------------------------------
	// Server - Spawn / Remove
	// ----------------------------------------------------------------

	//! Spawn a virtual radio, configure it to match this vehicle, and insert into the player's inventory
	protected void SpawnICSRadio(int playerId)
	{
		IEntity player = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!player)
			return;

		EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		player.GetWorldTransform(spawnParams.Transform);

		IEntity radioEntity = GetGame().SpawnEntityPrefab(Resource.Load(m_VirtualRadioPrefab), GetGame().GetWorld(), spawnParams);
		if (!radioEntity)
			return;

		BaseRadioComponent radioComp = BaseRadioComponent.Cast(radioEntity.FindComponent(BaseRadioComponent));
		if (radioComp)
		{
			radioComp.SetPower(true);

			if (m_sEncryptionKey != string.Empty)
				radioComp.SetEncryptionKey(m_sEncryptionKey);

			BaseTransceiver tsv = radioComp.GetTransceiver(0);
			if (tsv)
			{
				tsv.SetFrequency(GetVehicleFrequency());
				tsv.SetRange(m_fTransmitRange);
			}
		}

		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(player.FindComponent(InventoryStorageManagerComponent));
		if (inventory)
			inventory.TryInsertItem(radioEntity);
	}

	//! Find and delete all virtual ICS radios in the player's inventory
	protected void RemoveICSRadioServer(int playerId)
	{
		IEntity player = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!player)
			return;

		InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(player.FindComponent(InventoryStorageManagerComponent));
		if (!inventory)
			return;

		array<IEntity> items = {};
		SCR_PrefabNamePredicate predicate = new SCR_PrefabNamePredicate();
		predicate.prefabName = m_VirtualRadioPrefab;
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
	// Client - Poll / Setup / Cleanup
	// ----------------------------------------------------------------

	//! Poll the local player's inventory until the replicated virtual radio arrives
	protected void PollForICSRadio()
	{
		if (m_ICSEntry)
		{
			GetGame().GetCallqueue().Remove(PollForICSRadio);
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
		predicate.prefabName = m_VirtualRadioPrefab;

		if (inventory.FindItems(items, predicate) <= 0 || items.IsEmpty())
			return;

		// Found - stop polling
		GetGame().GetCallqueue().Remove(PollForICSRadio);

		m_VirtualRadioEntity = items[0];
		m_VirtualRadioComp = BaseRadioComponent.Cast(m_VirtualRadioEntity.FindComponent(BaseRadioComponent));
		if (!m_VirtualRadioComp)
			return;

		// Configure client-side (these properties don't replicate)
		m_VirtualRadioComp.SetPower(true);

		if (m_sEncryptionKey != string.Empty)
			m_VirtualRadioComp.SetEncryptionKey(m_sEncryptionKey);

		BaseTransceiver tsv = m_VirtualRadioComp.GetTransceiver(0);
		if (!tsv)
			return;

		tsv.SetFrequency(GetVehicleFrequency());

		// Lock frequency - revert any external changes
		m_VirtualRadioComp.m_OnTansceiverFrequencyChangedInvoker.Insert(OnICSFrequencyChanged);

		// Register for VonDisplay suppression
		SCR_ICSTransceiverTracker.Register(tsv);

		// Remove the auto-created entry from SCR_GadgetManagerComponent.OnItemAdded
		SCR_VONController vonController = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
		if (!vonController)
			return;

		array<ref SCR_VONEntry> entries = {};
		vonController.GetVONEntries(entries);
		foreach (SCR_VONEntry entry : entries)
		{
			SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
			if (radioEntry && radioEntry.GetTransceiver() && radioEntry.GetTransceiver().GetRadio() == m_VirtualRadioComp)
			{
				vonController.RemoveEntry(entry);
				break;
			}
		}

		// Create custom ICS entry
		SCR_RadioComponent radioGadget = SCR_RadioComponent.Cast(m_VirtualRadioEntity.FindComponent(SCR_RadioComponent));
		if (!radioGadget)
			return;

		m_ICSEntry = new SCR_VONEntryICS();
		m_ICSEntry.SetRadioEntry(tsv, 1, radioGadget);
		m_ICSEntry.SetChannelTextOverwrite(m_sEntryName);
		m_ICSEntry.SetFrequencyTextOverwrite(string.Empty);
		vonController.AddEntry(m_ICSEntry);
	}

	//! Revert any frequency changes made by the player or other mods
	protected void OnICSFrequencyChanged(BaseTransceiver transceiver, int oldFreq, int newFreq)
	{
		if (newFreq != m_iVehicleFrequency)
			transceiver.SetFrequency(m_iVehicleFrequency);
	}

	//! Remove the VON entry, unregister the tracker, and release references
	protected void CleanupLocalICSEntry()
	{
		if (m_ICSEntry)
		{
			PlayerController pc = GetGame().GetPlayerController();
			if (pc)
			{
				SCR_VONController vonController = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
				if (vonController)
					vonController.RemoveEntry(m_ICSEntry);
			}

			m_ICSEntry = null;
		}

		if (m_VirtualRadioComp)
		{
			m_VirtualRadioComp.m_OnTansceiverFrequencyChangedInvoker.Remove(OnICSFrequencyChanged);

			BaseTransceiver tsv = m_VirtualRadioComp.GetTransceiver(0);
			if (tsv)
				SCR_ICSTransceiverTracker.Unregister(tsv);
		}

		m_VirtualRadioEntity = null;
		m_VirtualRadioComp = null;
	}

	// ----------------------------------------------------------------
	// Utility
	// ----------------------------------------------------------------

	protected bool IsLocalPlayer(IEntity entity)
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		return (entity == pc.GetControlledEntity());
	}
}