[ComponentEditorProps(category: "TDL/Cables", description: "Cable connectors on a device or hub")]
class TDL_CablePortsComponentClass : ScriptComponentClass {}

//! The connectors an item has. Nothing here decides what is connected: that comes from the
//! cable rules and what else the character carries, resolved by TDL_CableManager.
class TDL_CablePortsComponent : ScriptComponent
{
	[Attribute("", UIWidgets.EditBox, "Kind the cable rules refer to, e.g. PS_MPU5")]
	protected string m_sCableKind;

	[Attribute(desc: "Connectors on this item")]
	protected ref array<ref TDL_CablePortDef> m_aPorts;

	[Attribute(desc: "Hubs only: which ports pass what to each other. Leave empty on a device, whose ports all reach the device itself")]
	protected ref array<ref TDL_CableHubJoin> m_aJoins;

	[Attribute("0", UIWidgets.CheckBox, "Draw the local character's cable links and log when they change", category: "TDL Debug")]
	protected bool m_bDebugDraw;

	// Checked twice a second: cables only appear or vanish when gear changes hands.
	protected static const int VISUALS_INTERVAL_MS = 500;

	protected IEntity m_LastWearer;

	string GetKind() { return m_sCableKind; }
	array<ref TDL_CableHubJoin> GetJoins() { return m_aJoins; }

	static TDL_CablePortsComponent Of(IEntity item)
	{
		if (!item)
			return null;
		return TDL_CablePortsComponent.Cast(item.FindComponent(TDL_CablePortsComponent));
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (m_aPorts)
		{
			foreach (TDL_CablePortDef port : m_aPorts)
			{
				if (port && port.GetPoint())
					port.GetPoint().Init(owner);
			}
		}

		if (!GetGame().InPlayMode())
			return;

		if (m_bDebugDraw)
			GetGame().GetCallqueue().CallLater(DebugTick, 0, true);

		if (RplSession.Mode() != RplMode.Dedicated)
			GetGame().GetCallqueue().CallLater(SyncVisuals, VISUALS_INTERVAL_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(DebugTick);
			GetGame().GetCallqueue().Remove(SyncVisuals);
		}

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void DebugTick()
	{
		TDL_CableManager.DebugDrawLocal();
	}

	//------------------------------------------------------------------------------------------------
	// Every carried item asks for its wearer's cables to be brought up to date. The wearer it
	// just left is asked too, because this may have been the last item that would ask for them.
	protected void SyncVisuals()
	{
		IEntity wearer = GetOwner().GetRootParent();
		if (!ChimeraCharacter.Cast(wearer))
			wearer = null;

		if (m_LastWearer && m_LastWearer != wearer)
			TDL_CableVisuals.Sync(m_LastWearer);

		m_LastWearer = wearer;
		if (wearer)
			TDL_CableVisuals.Sync(wearer);
	}

	//------------------------------------------------------------------------------------------------
	//! A hub joins ports to each other and is not an endpoint itself.
	bool IsHub()
	{
		return m_aJoins && m_aJoins.Count() > 0;
	}

	//------------------------------------------------------------------------------------------------
	int GetPortCount()
	{
		if (!m_aPorts)
			return 0;
		return m_aPorts.Count();
	}

	//------------------------------------------------------------------------------------------------
	string GetPortId(int index)
	{
		if (!m_aPorts || !m_aPorts.IsIndexValid(index) || !m_aPorts[index])
			return string.Empty;
		return m_aPorts[index].GetId();
	}

	//------------------------------------------------------------------------------------------------
	//! Index of the named port, -1 when this item has none by that name.
	int FindPort(string portId)
	{
		if (!m_aPorts)
			return -1;

		for (int i = 0; i < m_aPorts.Count(); i++)
		{
			if (m_aPorts[i] && m_aPorts[i].GetId() == portId)
				return i;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! World transform of a connector, read fresh so it follows the bone it sits on.
	bool GetPortTransform(int index, out vector mat[4])
	{
		IEntity owner = GetOwner();
		if (!owner || !m_aPorts || !m_aPorts.IsIndexValid(index) || !m_aPorts[index])
			return false;

		TDL_CablePortDef port = m_aPorts[index];
		PointInfo point = port.GetPoint();

		vector local[4];
		Math3D.MatrixIdentity4(local);

		if (!port.GetWearerBone().IsEmpty() && GetWearerBoneTransform(port.GetWearerBone(), mat))
		{
			if (!point)
				return true;

			vector boneMat[4];
			Math3D.MatrixCopy(mat, boneMat);
			point.GetLocalTransform(local);
			Math3D.MatrixMultiply4(boneMat, local, mat);
			return true;
		}

		vector ownerMat[4];
		owner.GetWorldTransform(ownerMat);
		if (point)
			point.GetModelTransform(local);
		Math3D.MatrixMultiply4(ownerMat, local, mat);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// False when the item is not on a character or the character has no such bone, so the
	// caller falls back to the item's own transform, which is right for an item on the ground.
	protected bool GetWearerBoneTransform(string boneName, out vector mat[4])
	{
		IEntity wearer = GetOwner().GetRootParent();
		if (!ChimeraCharacter.Cast(wearer))
			return false;

		Animation animation = wearer.GetAnimation();
		if (!animation)
			return false;

		TNodeId bone = animation.GetBoneIndex(boneName);
		if (bone == -1)
			return false;

		vector boneMat[4];
		if (!animation.GetBoneMatrix(bone, boneMat))
			return false;

		vector wearerMat[4];
		wearer.GetWorldTransform(wearerMat);
		Math3D.MatrixMultiply4(wearerMat, boneMat, mat);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Where a cable meets this connector and which way it leaves, in world space.
	//! How stiffly a cable leaves the port, as a share of the cable's usual straight run.
	float GetPortStiffness(int index)
	{
		if (!m_aPorts || !m_aPorts.IsIndexValid(index) || !m_aPorts[index])
			return 1;
		return m_aPorts[index].GetStiffness();
	}

	//------------------------------------------------------------------------------------------------
	//! The cable a port insists on, empty when it takes whatever the rule draws.
	ResourceName GetPortCablePrefab(int index)
	{
		if (!m_aPorts || !m_aPorts.IsIndexValid(index) || !m_aPorts[index])
			return ResourceName.Empty;
		return m_aPorts[index].GetCablePrefab();
	}

	//------------------------------------------------------------------------------------------------
	bool GetPortExit(int index, out vector pos, out vector dir)
	{
		vector mat[4];
		if (!GetPortTransform(index, mat))
			return false;

		vector rot[3];
		rot[0] = mat[0];
		rot[1] = mat[1];
		rot[2] = mat[2];

		pos = mat[3];
		dir = m_aPorts[index].GetExitDir().Multiply3(rot);
		if (dir.LengthSq() < 0.000001)
			dir = mat[2];
		dir = dir.Normalized();
		return true;
	}
}
