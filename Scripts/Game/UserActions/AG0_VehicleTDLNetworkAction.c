// In-vehicle TDL network join/leave/create action.
//
// Sibling of AG0_DeviceTDLNetworkAction (inventory-inspect variant). Inherits
// the seat-policy attributes from SCR_VehicleActionBase (m_bInteriorOnly,
// m_bPilotOnly, m_aDefinedCompartmentSectionsOnly, etc.) so the prefab author
// picks which compartments expose the action without writing event handlers.
//
// State of truth lives on AG0_TDLDeviceComponent on the vehicle entity; this
// action is a pure dispatcher into the existing dialog flow that the inventory
// variant uses, so the server-side join/create/leave path is shared and there
// is no duplicate dialog-routing logic to keep in sync.
//
// m_eForceWaveform pins the operation to a single waveform bit on multi-
// waveform devices — typically a vehicle authors one action per waveform
// (e.g. pilot seat exposes "Join LINK16" and "Join BFT2" as separate actions
// pointing at the same TDL device). NONE means use the device's full mask
// (existing behavior preserved for prefabs that don't author the override).
class AG0_VehicleTDLNetworkAction : SCR_VehicleActionBase
{
	[Attribute("0", UIWidgets.CheckBox, "Create network. If unchecked, this action toggles join/leave.")]
	protected bool m_bCreateNetwork;

	[Attribute("0", UIWidgets.ComboBox,
		"Designated waveform for create/join/leave. NONE = use device's full waveform mask.",
		"", ParamEnumArray.FromEnum(AG0_ETDLWaveform))]
	protected AG0_ETDLWaveform m_eForceWaveform;

	protected AG0_TDLDeviceComponent m_TdlDeviceComp;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		m_TdlDeviceComp = AG0_TDLDeviceComponent.Cast(pOwnerEntity.FindComponent(AG0_TDLDeviceComponent));
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!super.CanBeShownScript(user))
			return false;

		if (!m_TdlDeviceComp)
			return false;

		if (!m_TdlDeviceComp.IsPowered() || !m_TdlDeviceComp.CanAccessNetwork())
			return false;

		// Strict-subset validation. A misauthored prefab (force-waveform set
		// to a bit the device doesn't support) is hidden entirely rather than
		// showing an action that would silently no-op.
		if (m_eForceWaveform != 0 && (m_TdlDeviceComp.GetWaveform() & m_eForceWaveform) == 0)
			return false;

		// Hide Create when the device is already in a network of the relevant
		// waveform — there's nothing to create in that slot. With no override
		// "relevant" is "any network" (existing coarse behavior); with an
		// override it's per-waveform via the device's replicated mask.
		if (m_bCreateNetwork)
		{
			if (IsInRelevantNetwork())
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_TdlDeviceComp)
			return false;

		// Generic "TDL" label when no waveform is forced; specialize to the
		// designated waveform's display name otherwise so a vehicle prefab
		// authoring one action per waveform shows "Join BFT-2 Network" /
		// "Join LINK-16 Network" instead of duplicate generic "Join TDL Network"
		// entries that the crew can't tell apart.
		string netLabel = "TDL";
		if (m_eForceWaveform != 0)
			netLabel = AG0_TDLWaveformInfo.GetDisplayName(m_eForceWaveform);

		if (m_bCreateNetwork)
		{
			outName = string.Format("Create %1 Network", netLabel);
			return true;
		}

		if (IsInRelevantNetwork())
			outName = string.Format("Leave %1 Network", netLabel);
		else
			outName = string.Format("Join %1 Network", netLabel);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Override PerformAction directly rather than the base's SetState toggle so
	// pUserEntity reaches the device's dialog router. The base's SetState(bool)
	// has no user parameter, but CreateNetworkDialog / JoinNetworkDialog need
	// it to address the right player controller for the dialog RPC.
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_TdlDeviceComp)
			return;

		if (m_bCreateNetwork)
		{
			m_TdlDeviceComp.CreateNetworkDialog(pUserEntity, m_eForceWaveform);
			return;
		}

		if (IsInRelevantNetwork())
		{
			// Targeted leave when an override is authored, full-leave otherwise.
			// The targeted path routes server-side resolution of which networkID
			// to detach from — clients don't know per-network waveforms.
			if (m_eForceWaveform == 0)
				m_TdlDeviceComp.LeaveNetworkTDL();
			else
				m_TdlDeviceComp.LeaveNetworkTDLByWaveform(m_eForceWaveform);
			return;
		}

		m_TdlDeviceComp.JoinNetworkDialog(pUserEntity, m_eForceWaveform);
	}

	//------------------------------------------------------------------------------------------------
	// Centralized "is the device already on the network this action targets"
	// check. With no override it's a coarse "any network" boolean; with an
	// override it's a per-waveform check via the replicated mask. Used by
	// CanBeShownScript, GetActionNameScript, and PerformAction so all three
	// agree on the same definition of "relevant."
	protected bool IsInRelevantNetwork()
	{
		if (!m_TdlDeviceComp)
			return false;
		if (m_eForceWaveform == 0)
			return m_TdlDeviceComp.IsInNetwork();
		return m_TdlDeviceComp.IsInNetworkOfWaveform(m_eForceWaveform);
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}
}
