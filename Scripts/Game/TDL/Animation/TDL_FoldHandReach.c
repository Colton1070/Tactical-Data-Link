//! Puts the wearer's left hand on a device docked in a folding mount while the fold is being
//! adjusted, and takes it away again shortly after. The hand goes where it sits on the device
//! when the device is held, so one grip serves wherever the mount is worn.
//!
//! The arm is moved by an IK node this addon cannot add to the character's graph on its own;
//! without the graph side present the variable does not bind and nothing here has any effect.
class TDL_FoldHandReach
{
	protected static const string WEIGHT_VARIABLE = "TDL_FoldHandWeight";
	protected static const string IK_TARGET = "TDL_FoldHand";

	// How long the hand stays after the last adjustment, and how long it takes to arrive or leave.
	protected static const float HOLD_S = 1.0;
	protected static const float BLEND_S = 0.3;

	// Long enough to ride out a slow frame between two Keep calls.
	protected static const float KEEP_S = 0.1;

	protected float m_fHoldS;
	protected float m_fWeight;
	protected TAnimGraphVariable m_iWeightVar = -1;
	protected IEntity m_BoundCharacter;
	protected bool m_bReported;

	//------------------------------------------------------------------------------------------------
	//! Call on every fold input; keeps the hand on the device while inputs keep coming.
	void Trigger()
	{
		m_fHoldS = HOLD_S;
	}

	//------------------------------------------------------------------------------------------------
	//! Call every frame something wants the hand kept on the device. The hand leaves as
	//! soon as the calls stop, without the wait a fold input gets.
	void Keep()
	{
		m_fHoldS = Math.Max(m_fHoldS, KEEP_S);
	}

	//------------------------------------------------------------------------------------------------
	//! Call every frame for the character whose hand it is; null when the mount is not worn.
	void Update(IEntity character, TDL_EUDBoneComponent hinge, float timeSlice)
	{
		// A mount taken off mid-reach must not leave its last wearer's arm pinned out.
		if (m_BoundCharacter != null && character != m_BoundCharacter)
			Release();

		if (m_fHoldS <= 0 && m_fWeight <= 0)
			return;
		if (!character)
		{
			m_fHoldS = 0;
			return;
		}

		m_fHoldS = m_fHoldS - timeSlice;

		IEntity device;
		if (hinge)
			device = FindDocked(hinge.GetOwner());

		float wanted = 0;
		if (m_fHoldS > 0 && device)
			wanted = 1;

		if (wanted > 0 && m_fWeight <= 0)
			m_bReported = false;

		float step = timeSlice / BLEND_S;
		if (m_fWeight < wanted)
			m_fWeight = Math.Min(m_fWeight + step, wanted);
		else
			m_fWeight = Math.Max(m_fWeight - step, wanted);

		CharacterAnimationComponent animation = CharacterAnimationComponent.Cast(character.FindComponent(CharacterAnimationComponent));
		if (!animation)
			return;

		if (character != m_BoundCharacter)
		{
			m_iWeightVar = animation.BindVariableFloat(WEIGHT_VARIABLE);
			m_BoundCharacter = character;
			m_bReported = false;
		}
		if (m_iWeightVar == -1)
		{
			if (!m_bReported)
				Print("[TDL_FOLD] Character graph has no " + WEIGHT_VARIABLE + " variable; hand reach is off", LogLevel.WARNING);
			m_bReported = true;
			return;
		}

		if (device && m_fWeight > 0)
		{
			vector position;
			vector anglesRad;
			GripTarget(character, device, position, anglesRad);
			// The call reports failure even when the target is applied, so its result is not read.
			animation.SetIKTarget(IK_TARGET, string.Empty, position, anglesRad);
		}

		animation.SetVariableFloat(m_iWeightVar, m_fWeight);
	}

	//------------------------------------------------------------------------------------------------
	protected void Release()
	{
		if (m_fWeight > 0 && m_iWeightVar != -1)
		{
			CharacterAnimationComponent animation = CharacterAnimationComponent.Cast(m_BoundCharacter.FindComponent(CharacterAnimationComponent));
			if (animation)
				animation.SetVariableFloat(m_iWeightVar, 0);
		}

		m_fWeight = 0;
		m_iWeightVar = -1;
		m_BoundCharacter = null;
	}

	//------------------------------------------------------------------------------------------------
	// Where the left hand goes, in the character's own space. The grip is the hold animation's
	// hand-to-device offset turned round: the hand as seen from the device.
	protected void GripTarget(IEntity character, IEntity device, out vector position, out vector anglesRad)
	{
		vector grip[4];
		grip[0] = "-0.18458 -0.95428 -0.23510";
		grip[1] = "-0.96770 0.21826 -0.12618";
		grip[2] = "0.17172 0.20422 -0.96375";
		grip[3] = "0.17030 0.00420 -0.04730";

		// The slot's transform is the docked device's own frame, including for a device docked
		// by one of its bones: the engine takes the bone off before it reports the slot.
		vector deviceMat[4];
		device.GetWorldTransform(deviceMat);
		InventoryItemComponent item = InventoryItemComponent.Cast(device.FindComponent(InventoryItemComponent));
		if (item && item.GetParentSlot())
			item.GetParentSlot().GetWorldTransform(deviceMat);

		vector hand[4];
		Math3D.MatrixMultiply4(deviceMat, grip, hand);

		vector turn[3];
		turn[0] = character.VectorToLocal(hand[0]);
		turn[1] = character.VectorToLocal(hand[1]);
		turn[2] = character.VectorToLocal(hand[2]);

		// The IK node takes plain turns about X, then Y, then Z, in radians. That is not what
		// MatrixToAngles gives (yaw, pitch, roll with its own signs), and feeding it those is
		// only right at some fold angles, so the three turns are read off the axes directly.
		position = character.CoordToLocal(hand[3]);
		float aboutX = Math.Atan2(turn[1][2], turn[2][2]);
		float aboutY = -Math.Asin(Math.Clamp(turn[0][2], -1, 1));
		float aboutZ = Math.Atan2(turn[0][1], turn[0][0]);
		anglesRad = Vector(aboutX, aboutY, aboutZ);
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity FindDocked(IEntity mount)
	{
		if (!mount)
			return null;

		IEntity child = mount.GetChildren();
		while (child)
		{
			InventoryItemComponent item = InventoryItemComponent.Cast(child.FindComponent(InventoryItemComponent));
			// One grip serves every end user device until each brings its own, so the hand
			// lands a little off on a device narrower or wider than the one it was taken from.
			if (item && TDL_AccessoryPortSlot.Cast(item.GetParentSlot()) && child.FindComponent(TDL_EUDGadgetComponent))
				return child;
			child = child.GetSibling();
		}
		return null;
	}
}
