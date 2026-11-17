class TDL_CableEntityClass : GenericEntityClass {}

//! One drawn cable: a tube skinned to a chain of bones, laid along a curve between two ports
//! every frame. It exists only on the machine drawing it; the link it shows comes from
//! inventory, which every machine already has.
class TDL_CableEntity : GenericEntity
{
	// Share of the pull that must run across the barrel before the plug follows it at all,
	// and before it follows at full speed.
	protected static const float PLUG_HOLD_BELOW = 0.15;
	protected static const float PLUG_FREE_ABOVE = 0.5;

	protected static const int CURVE_STEPS = 30;
	protected static const float LEAD_GAP_SHARE = 0.35;
	protected static const float HANDLE_GAP_SHARE = 0.5;

	[Attribute("24", UIWidgets.Slider, "Bones in the cable mesh", "2 32 1")]
	protected int m_iBoneCount;

	[Attribute("cable_", UIWidgets.EditBox, "Bone name prefix, followed by a two-digit index")]
	protected string m_sBonePrefix;

	[Attribute("0.05", UIWidgets.Slider, "Dead-straight run out of each connector before the cable may bend, in metres", "0 0.2 0.005")]
	protected float m_fLeadM;

	[Attribute("0.35", UIWidgets.Slider, "How far the cable keeps heading the way it left a connector, as a share of the gap between the two lead-outs. Lower bends sooner", "0.05 1 0.05")]
	protected float m_fBendShare;

	[Attribute("1.2", UIWidgets.Slider, "Drawn length as a multiple of the straight distance between the ports. The rest of the cable is taken to be tucked away", "1 2 0.05")]
	protected float m_fSlack;

	[Attribute("0.05", UIWidgets.Slider, "Shortest the cable is ever drawn, in metres", "0.01 0.5 0.01")]
	protected float m_fMinLengthM;

	[Attribute("0.035", UIWidgets.Slider, "Tightest turn the cable makes, as a radius in metres", "0.005 0.15 0.005")]
	protected float m_fMinBendRadiusM;

	[Attribute("0.15", UIWidgets.Slider, "Seconds the cable's sag takes to settle after the ports move. The ends never lag; only the slack between them is eased, which is what stops it shivering with every animation frame", "0 1 0.01")]
	protected float m_fSettleS;

	[Attribute("plug_a", UIWidgets.EditBox, "Bone carrying a right-angle plug moulded on the first end, set at the plug's corner. Leave the mesh without it for a plain cable")]
	protected string m_sPlugBone;

	[Attribute("plug_a_jack", UIWidgets.EditBox, "Bone marking where that plug meets the device. Only its position is read: it tells which way the plug's barrel points")]
	protected string m_sPlugJackBone;

	[Attribute("0.25", UIWidgets.Slider, "Seconds a right-angle plug takes to swing round to follow the cable", "0 1 0.01")]
	protected float m_fPlugSwingS;

	[Attribute("0", UIWidgets.CheckBox, "The plug is a device's own connector, modelled in the device's axes with its port at the mesh origin. It is set on the port exactly as modelled and never swings; the jack bone is not needed")]
	protected bool m_bPlugFixed;

	protected IEntity m_Carrier;
	protected TDL_CablePortsComponent m_A;
	protected int m_iPortA;
	protected TDL_CablePortsComponent m_B;
	protected int m_iPortB;
	protected float m_fLengthM;
	protected vector m_vLoopDir;
	protected float m_fStiffA = 1;
	protected float m_fStiffB = 1;

	protected ref array<int> m_aBoneIdx = new array<int>();
	protected ref array<vector> m_aBindPos = new array<vector>();
	protected ref array<vector> m_aLocalPos = new array<vector>();
	protected ref array<vector> m_aPath = new array<vector>();
	protected vector m_vBowAnchor;
	protected bool m_bBowSet;
	protected vector m_vBindX;
	protected vector m_vBindY;
	protected vector m_vBindZ;
	protected vector m_vBindAxis;
	protected vector m_vBindSide;
	protected vector m_vRollRef;
	protected bool m_bBonesInit;

	protected int m_iPlugBone = -1;
	protected vector m_vPlugBind;
	protected vector m_vPlugBarrelBind;
	protected float m_fPlugDropM;
	protected float m_fPlugReachM;
	protected vector m_vPlugBootAnchor;
	protected bool m_bPlugBootSet;
	protected vector m_vPlugCorner;
	protected vector m_vPlugBoot;
	protected vector m_vPlugBarrel;
	protected vector m_vPortX;
	protected vector m_vPortY;
	protected vector m_vPortZ;
	protected bool m_bWarnedNoBones;

	//------------------------------------------------------------------------------------------------
	void TDL_CableEntity(IEntitySource src, IEntity parent)
	{
		// The mask picks the event; the flag is what puts the entity in the update list at all.
		// Post-frame, so the ports are read after the character's animation has moved them.
		SetEventMask(EntityEvent.POSTFRAME);
		SetFlags(EntityFlags.ACTIVE, true);
	}

	//------------------------------------------------------------------------------------------------
	//! plugOnB turns the link round, for a cable whose plug belongs on the link's second port.
	void SetLink(notnull TDL_CableLink link, IEntity carrier, bool plugOnB = false)
	{
		m_Carrier = carrier;
		m_A = link.m_A;
		m_iPortA = link.m_iPortA;
		m_B = link.m_B;
		m_iPortB = link.m_iPortB;
		if (plugOnB)
		{
			m_A = link.m_B;
			m_iPortA = link.m_iPortB;
			m_B = link.m_A;
			m_iPortB = link.m_iPortA;
		}
		m_fLengthM = link.m_fLengthM;
		m_vLoopDir = link.m_vLoopDir;
		m_fStiffA = m_A.GetPortStiffness(m_iPortA);
		m_fStiffB = m_B.GetPortStiffness(m_iPortB);
	}

	//------------------------------------------------------------------------------------------------
	bool IsLinkAlive()
	{
		return m_A && m_B;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnPostFrame(IEntity owner, float timeSlice)
	{
		if (!m_A || !m_B)
			return;

		if (!m_bBonesInit && !InitBones())
			return;

		vector posA, dirA, posB, dirB;
		if (!m_Carrier || !m_A.GetPortExit(m_iPortA, posA, dirA) || !m_B.GetPortExit(m_iPortB, posB, dirB))
			return;

		// Everything from here is worked in the wearer's space, which is also this entity's
		// own. Out in the world the numbers are thousands of metres across and round off by
		// a millimetre or so, differently every frame the wearer moves, and the cable shivers.
		//
		// Converted through this entity, not the wearer: this entity and the worn items are
		// all children of the wearer and are moved in the same pass, so they agree with each
		// other even on a frame where the wearer itself has already turned further.
		posA = CoordToLocal(posA);
		posB = CoordToLocal(posB);
		dirA = VectorToLocal(dirA);
		dirB = VectorToLocal(dirB);

		if (m_iPlugBone != -1 && m_bPlugFixed)
		{
			// The connector is part of the device's shape, so it takes the port's own frame
			// and the tube starts wherever the mesh has it start.
			vector portMat[4];
			if (!m_A.GetPortTransform(m_iPortA, portMat))
				return;

			m_vPortX = VectorToLocal(portMat[0]);
			m_vPortY = VectorToLocal(portMat[1]);
			m_vPortZ = VectorToLocal(portMat[2]);
			m_vPlugCorner = posA + FromPort(m_vPlugBind);
			posA = posA + FromPort(m_aBindPos[0]);
		}
		else if (m_iPlugBone != -1)
		{
			// The barrel goes straight into the jack; the boot swings round it to point at
			// wherever the cable is being pulled from, and the cable starts at the boot's tip.
			m_vPlugCorner = posA + dirA * m_fPlugDropM;
			m_vPlugBarrel = dirA * -1;
			m_vPlugBoot = SwingBoot(posB - m_vPlugCorner, dirA, timeSlice);
			posA = m_vPlugCorner + m_vPlugBoot * m_fPlugReachM;
			dirA = m_vPlugBoot;
		}

		LayCurve(posA, dirA, posB, dirB, timeSlice);
		ApplyBones();
	}

	//------------------------------------------------------------------------------------------------
	protected bool InitBones()
	{
		Animation anim = GetAnimation();
		if (!anim)
		{
			if (!m_bWarnedNoBones)
				Print("[TDL_CABLE] Cable entity has no skeleton, mesh missing?", LogLevel.WARNING);
			m_bWarnedNoBones = true;
			return false;
		}

		m_aBoneIdx.Resize(m_iBoneCount);
		m_aBindPos.Resize(m_iBoneCount);
		m_aLocalPos.Resize(m_iBoneCount);

		bool haveBasis = false;
		for (int i = 0; i < m_iBoneCount; i++)
		{
			int idx = anim.GetBoneIndex(BoneName(i));
			m_aBoneIdx[i] = idx;
			if (idx == -1)
				continue;

			vector mat[4];
			anim.GetBoneMatrix(idx, mat);
			m_aBindPos[i] = mat[3];

			if (!haveBasis)
			{
				m_vBindX = mat[0];
				m_vBindY = mat[1];
				m_vBindZ = mat[2];
				haveBasis = true;
			}
		}

		if (!haveBasis || m_aBoneIdx[0] == -1 || m_aBoneIdx[1] == -1)
		{
			if (!m_bWarnedNoBones)
				Print(string.Format("[TDL_CABLE] Cable mesh has no bones named %1NN", m_sBonePrefix), LogLevel.WARNING);
			m_bWarnedNoBones = true;
			return false;
		}

		// Read off the rig, so nothing here depends on which way the bones came through export.
		m_vBindAxis = (m_aBindPos[1] - m_aBindPos[0]).Normalized();
		m_vBindSide = Square(m_vBindX, m_vBindAxis);
		if (m_vBindSide == vector.Zero)
			m_vBindSide = Square(m_vBindY, m_vBindAxis);
		m_vRollRef = m_vBindSide;
		InitPlug(anim);
		m_bBonesInit = true;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// The plug's shape is read off the rig: how far the barrel drops from the device to the
	// corner, which way it points, and how far the boot reaches from the corner to the
	// start of the tube.
	protected void InitPlug(Animation anim)
	{
		m_iPlugBone = -1;
		if (m_sPlugBone.IsEmpty())
			return;

		int plug = anim.GetBoneIndex(m_sPlugBone);
		if (plug == -1)
			return;

		vector mat[4];
		anim.GetBoneMatrix(plug, mat);
		m_vPlugBind = mat[3];

		if (m_bPlugFixed)
		{
			m_iPlugBone = plug;
			return;
		}

		if (m_sPlugJackBone.IsEmpty())
			return;

		int jack = anim.GetBoneIndex(m_sPlugJackBone);
		if (jack == -1)
			return;

		anim.GetBoneMatrix(jack, mat);

		vector barrel = mat[3] - m_vPlugBind;
		barrel = barrel - m_vBindAxis * vector.Dot(barrel, m_vBindAxis);
		m_fPlugDropM = barrel.Length();
		m_fPlugReachM = vector.Dot(m_aBindPos[0] - m_vPlugBind, m_vBindAxis);
		if (m_fPlugDropM < 0.0001)
			return;

		m_vPlugBarrelBind = barrel * (1 / m_fPlugDropM);
		m_iPlugBone = plug;
	}

	//------------------------------------------------------------------------------------------------
	// Which way the boot points: toward the pull, kept square to the barrel. It is turned
	// round the barrel a share of the way each frame, never blended in a straight line: a
	// straight blend passes through nothing when the pull swaps sides and the plug jumps.
	protected vector SwingBoot(vector pull, vector barrelOut, float timeSlice)
	{
		vector wanted = Square(pull, barrelOut);
		float pullLength = pull.Length();

		// How far off the barrel's own line the pull is. A cable leaving nearly straight out
		// of the jack says almost nothing about which way the boot should face, and the
		// little it says swings wildly, so the plug is left where it is.
		float offLine = 0;
		if (pullLength > 0.0001 && wanted != vector.Zero)
			offLine = (pull - barrelOut * vector.Dot(pull, barrelOut)).Length() / pullLength;

		vector current = vector.Zero;
		if (m_bPlugBootSet)
			current = Square(FromAnchor(m_vPlugBootAnchor), barrelOut);

		if (current == vector.Zero)
		{
			if (wanted == vector.Zero)
				wanted = Square("0 -1 0", barrelOut);
			if (wanted == vector.Zero)
				wanted = Square("1 0 0", barrelOut);
			m_vPlugBootAnchor = ToAnchor(wanted);
			m_bPlugBootSet = true;
			return wanted;
		}

		if (wanted == vector.Zero)
		{
			return current;
		}

		float share = 1;
		if (m_fPlugSwingS > 0)
			share = Math.Clamp(timeSlice / m_fPlugSwingS, 0, 1);
		share = share * Math.Clamp((offLine - PLUG_HOLD_BELOW) / (PLUG_FREE_ABOVE - PLUG_HOLD_BELOW), 0, 1);

		float angle = Math.Atan2(vector.Dot(Cross(current, wanted), barrelOut), vector.Dot(current, wanted));
		vector boot = Square(Rotate(current, barrelOut, angle * share), barrelOut);
		if (boot == vector.Zero)
			boot = current;

		m_vPlugBootAnchor = ToAnchor(boot);
		return boot;
	}

	//------------------------------------------------------------------------------------------------
	// The plug is rigid, so its bone takes the one turn that carries the bind boot and barrel
	// directions onto where they are now.
	protected void ApplyPlug(Animation anim)
	{
		if (m_bPlugFixed)
		{
			WriteBone(anim, m_iPlugBone, m_vPlugBind, FromPort(m_vBindX), FromPort(m_vBindY), FromPort(m_vBindZ), m_vPlugCorner);
			return;
		}

		vector bindBoot = m_vBindAxis;
		vector bindBarrel = m_vPlugBarrelBind;
		vector bindSide = Cross(bindBoot, bindBarrel);

		vector boot = m_vPlugBoot;
		vector barrel = m_vPlugBarrel;
		barrel = barrel - boot * vector.Dot(barrel, boot);
		if (barrel.LengthSq() < 0.000001)
			return;
		barrel = barrel.Normalized();
		vector side = Cross(boot, barrel);

		vector fx = boot * vector.Dot(m_vBindX, bindBoot) + barrel * vector.Dot(m_vBindX, bindBarrel) + side * vector.Dot(m_vBindX, bindSide);
		vector fy = boot * vector.Dot(m_vBindY, bindBoot) + barrel * vector.Dot(m_vBindY, bindBarrel) + side * vector.Dot(m_vBindY, bindSide);
		vector fz = boot * vector.Dot(m_vBindZ, bindBoot) + barrel * vector.Dot(m_vBindZ, bindBarrel) + side * vector.Dot(m_vBindZ, bindSide);

		WriteBone(anim, m_iPlugBone, m_vPlugBind, fx, fy, fz, m_vPlugCorner);
	}

	//------------------------------------------------------------------------------------------------
	// A direction or offset in the mesh's own axes, as it lies once the mesh sits on the port.
	protected vector FromPort(vector v)
	{
		return m_vPortX * v[0] + m_vPortY * v[1] + m_vPortZ * v[2];
	}

	//------------------------------------------------------------------------------------------------
	protected string BoneName(int i)
	{
		if (i < 10)
			return m_sBonePrefix + "0" + i.ToString();
		return m_sBonePrefix + i.ToString();
	}

	//------------------------------------------------------------------------------------------------
	// Straight out of each connector, then a cubic between the two lead-outs. The drawn length
	// follows the gap between the ports, up to the cable's real length; when that leaves spare,
	// the middle bows away from the body to use it.
	protected void LayCurve(vector posA, vector dirA, vector posB, vector dirB, float timeSlice)
	{
		float span = vector.Distance(posA, posB);
		float lead = Math.Min(m_fLeadM, m_fLengthM * 0.25);

		// Between two ports close together the usual straight runs are longer than the gap:
		// each end shoots past the other and the cable doubles back on itself. The runs are
		// held to a share of the gap so the ends still leave straight but turn in time.
		float leadLengthA = Math.Min(lead * m_fStiffA, span * LEAD_GAP_SHARE);
		float leadLengthB = Math.Min(lead * m_fStiffB, span * LEAD_GAP_SHARE);

		// Long enough to leave both connectors straight and still turn no tighter than the
		// cable allows, even when the two ports sit side by side.
		float shortest = Math.Max(m_fMinLengthM, leadLengthA + leadLengthB + m_fMinBendRadiusM * 3);
		float target = Math.Clamp(Math.Max(span * m_fSlack, shortest), m_fMinLengthM, m_fLengthM);

		vector leadA = posA + dirA * leadLengthA;
		vector leadB = posB + dirB * leadLengthB;
		float handle = Math.Max(vector.Distance(leadA, leadB) * m_fBendShare, m_fMinBendRadiusM * 1.5);
		handle = Math.Min(handle, span * HANDLE_GAP_SHARE);

		// A soft port shortens its end's follow-through, but never below what keeps the
		// first turn as wide as the cable can bend; under that the tube folds at the port.
		float handleFloor = Math.Min(handle, m_fMinBendRadiusM);
		vector c1 = leadA + dirA * Math.Max(handle * m_fStiffA, handleFloor);
		vector c2 = leadB + dirB * Math.Max(handle * m_fStiffB, handleFloor);

		vector bow = BowDirection(leadA, leadB);

		// The sag comes straight from how much cable is spare, by the rule for a shallow arc.
		// Searching for the sag that gives the exact length has more than one answer when the
		// curve already bulges, and it hops between them as the ports move.
		float bowAmount = 0;
		float spare = target - BuildPath(posA, leadA, c1, c2, leadB, posB);
		if (spare > 0)
		{
			float gap = Math.Max(vector.Distance(leadA, leadB), m_fMinBendRadiusM);
			bowAmount = Math.Sqrt(0.375 * gap * spare) * 1.333;
		}

		vector settled = SettleBow(bow * bowAmount, timeSlice);
		float total = BuildPath(posA, leadA, c1 + settled, c2 + settled, leadB, posB);

		// Bones go at even steps of distance along the path, so the tube keeps an even width
		// through the tight part of a bend.
		int segment = 0;
		float walked = 0;
		int lastPoint = m_aPath.Count() - 1;
		float lastBone = m_iBoneCount - 1;
		for (int b = 0; b < m_iBoneCount; b++)
		{
			float wanted = total * (b / lastBone);
			while (segment < lastPoint - 1 && walked + vector.Distance(m_aPath[segment], m_aPath[segment + 1]) < wanted)
			{
				walked = walked + vector.Distance(m_aPath[segment], m_aPath[segment + 1]);
				segment = segment + 1;
			}

			float segmentLength = vector.Distance(m_aPath[segment], m_aPath[segment + 1]);
			float along = 0;
			if (segmentLength > 0.000001)
				along = Math.Clamp((wanted - walked) / segmentLength, 0, 1);

			m_aLocalPos[b] = m_aPath[segment] + (m_aPath[segment + 1] - m_aPath[segment]) * along;
		}

		// The mesh is weighted so the tube is a smooth curve drawn towards the bones, not a
		// line through them, which leaves each end a little short of its end bone. Setting
		// the end bones this much past the ports puts the tube ends exactly on them.
		int end = m_iBoneCount - 1;
		if (end >= 2)
		{
			m_aLocalPos[0] = (m_aLocalPos[0] * 6 - m_aLocalPos[1]) * 0.2;
			m_aLocalPos[end] = (m_aLocalPos[end] * 6 - m_aLocalPos[end - 1]) * 0.2;
		}
	}

	//------------------------------------------------------------------------------------------------
	// Eases the sag towards where it should be.
	protected vector SettleBow(vector wanted, float timeSlice)
	{
		vector wantedAnchor = ToAnchor(wanted);
		if (!m_bBowSet || m_fSettleS <= 0)
		{
			m_vBowAnchor = wantedAnchor;
			m_bBowSet = true;
		}
		else
		{
			float blend = Math.Clamp(timeSlice / m_fSettleS, 0, 1);
			m_vBowAnchor = m_vBowAnchor + (wantedAnchor - m_vBowAnchor) * blend;
		}
		return FromAnchor(m_vBowAnchor);
	}

	//------------------------------------------------------------------------------------------------
	// Anything eased from frame to frame is remembered relative to the device at the first
	// port. The character's root turns in steps while the torso twists smoothly over it, so
	// a direction remembered in the root's space jumps on the body each time the root steps.
	protected vector ToAnchor(vector dir)
	{
		return m_A.GetOwner().VectorToLocal(VectorToParent(dir));
	}

	//------------------------------------------------------------------------------------------------
	protected vector FromAnchor(vector dir)
	{
		return VectorToLocal(m_A.GetOwner().VectorToParent(dir));
	}

	//------------------------------------------------------------------------------------------------
	// Fills m_aPath with the cable's centre line and returns its length.
	protected float BuildPath(vector posA, vector leadA, vector c1, vector c2, vector leadB, vector posB)
	{
		m_aPath.Clear();
		m_aPath.Insert(posA);
		float steps = CURVE_STEPS;
		for (int i = 0; i <= CURVE_STEPS; i++)
			m_aPath.Insert(CurvePoint(leadA, c1, c2, leadB, i / steps));
		m_aPath.Insert(posB);

		float length = 0;
		for (int p = 1; p < m_aPath.Count(); p++)
			length = length + vector.Distance(m_aPath[p - 1], m_aPath[p]);
		return length;
	}

	//------------------------------------------------------------------------------------------------
	// Away from the wearer's chest and square to the straight line between the ports. Falls
	// back to straight down when the ports sit in line with the chest.
	protected vector BowDirection(vector posA, vector posB)
	{
		vector chord = posB - posA;
		if (chord.LengthSq() < 0.000001)
			return "0 -1 0";
		chord = chord.Normalized();

		vector away = m_vLoopDir;
		if (away.LengthSq() < 0.0001)
			away = (posA + posB) * 0.5 - "0 1.25 0";

		away = away - chord * vector.Dot(away, chord);
		if (away.LengthSq() < 0.0001)
		{
			away = "0 -1 0";
			away = away - chord * vector.Dot(away, chord);
		}
		if (away.LengthSq() < 0.0001)
			return "1 0 0";
		return away.Normalized();
	}

	//------------------------------------------------------------------------------------------------
	protected vector CurvePoint(vector p0, vector p1, vector p2, vector p3, float t)
	{
		float u = 1 - t;
		return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
	}

	//------------------------------------------------------------------------------------------------
	// Each bone gets a full frame, not just a position: position alone leaves every ring of the
	// tube facing its bind direction and a sideways run flattens into a ribbon. The frame is
	// carried from bone to bone by the smallest turn that follows the curve, so it never twists.
	protected void ApplyBones()
	{
		Animation anim = GetAnimation();
		if (!anim)
			return;

		vector fx = m_vBindX;
		vector fy = m_vBindY;
		vector fz = m_vBindZ;
		vector axis = m_vBindAxis;
		int last = m_iBoneCount - 1;

		// The first frame is built from the cable's own heading and the roll it had last
		// frame. Turning the bind frame onto the heading instead has no steady answer when
		// the cable leaves pointing back along its bind axis, and the tube spins on the spot.
		vector heading = m_aLocalPos[1] - m_aLocalPos[0];
		if (heading.LengthSq() > 0.00000001)
		{
			heading = heading.Normalized();
			vector side = Square(m_vRollRef, heading);
			if (side == vector.Zero)
				side = Square(m_vBindSide, heading);
			if (side == vector.Zero)
				side = Square(m_vBindAxis, heading);
			if (side != vector.Zero)
			{
				m_vRollRef = side;
				vector third = Cross(heading, side);
				vector bindThird = Cross(m_vBindAxis, m_vBindSide);
				fx = heading * vector.Dot(m_vBindX, m_vBindAxis) + side * vector.Dot(m_vBindX, m_vBindSide) + third * vector.Dot(m_vBindX, bindThird);
				fy = heading * vector.Dot(m_vBindY, m_vBindAxis) + side * vector.Dot(m_vBindY, m_vBindSide) + third * vector.Dot(m_vBindY, bindThird);
				fz = heading * vector.Dot(m_vBindZ, m_vBindAxis) + side * vector.Dot(m_vBindZ, m_vBindSide) + third * vector.Dot(m_vBindZ, bindThird);
				axis = heading;
			}
		}

		for (int b = 0; b < m_iBoneCount; b++)
		{
			int boneIdx = m_aBoneIdx[b];
			if (boneIdx == -1)
				continue;

			int before = Math.Max(b - 1, 0);
			int after = Math.Min(b + 1, last);
			vector tangent = m_aLocalPos[after] - m_aLocalPos[before];
			if (tangent.LengthSq() > 0.00000001)
			{
				tangent = tangent.Normalized();

				vector turn = Cross(axis, tangent);
				float sine = turn.Length();
				float cosine = vector.Dot(axis, tangent);
				if (sine > 0.0001)
				{
					turn = turn * (1 / sine);
					float angle = Math.Atan2(sine, cosine);
					fx = Rotate(fx, turn, angle);
					fy = Rotate(fy, turn, angle);
					fz = Rotate(fz, turn, angle);
				}
				axis = tangent;
			}

			WriteBone(anim, boneIdx, m_aBindPos[b], fx, fy, fz, m_aLocalPos[b]);
		}

		if (m_iPlugBone != -1)
			ApplyPlug(anim);

		// Bone writes are not drawn until the entity is marked changed.
		Update();
	}

	//------------------------------------------------------------------------------------------------
	// SetBoneMatrix wants the bone's change from its bind pose, written in bind axes, with the
	// basis vectors as rows.
	protected void WriteBone(Animation anim, int boneIdx, vector bindPos, vector fx, vector fy, vector fz, vector localPos)
	{
		vector offset = localPos - bindPos;

		vector mat[4];
		mat[0] = Vector(vector.Dot(fx, m_vBindX), vector.Dot(fx, m_vBindY), vector.Dot(fx, m_vBindZ));
		mat[1] = Vector(vector.Dot(fy, m_vBindX), vector.Dot(fy, m_vBindY), vector.Dot(fy, m_vBindZ));
		mat[2] = Vector(vector.Dot(fz, m_vBindX), vector.Dot(fz, m_vBindY), vector.Dot(fz, m_vBindZ));
		mat[3] = Vector(vector.Dot(offset, m_vBindX), vector.Dot(offset, m_vBindY), vector.Dot(offset, m_vBindZ));

		anim.SetBoneMatrix(this, boneIdx, mat);
	}

	//------------------------------------------------------------------------------------------------
	// The part of v square to a unit axis, at unit length; zero when v runs along the axis.
	protected vector Square(vector v, vector unitAxis)
	{
		vector square = v - unitAxis * vector.Dot(v, unitAxis);
		if (square.LengthSq() < 0.0001)
			return vector.Zero;
		return square.Normalized();
	}

	//------------------------------------------------------------------------------------------------
	protected vector Cross(vector a, vector b)
	{
		return Vector(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
	}

	//------------------------------------------------------------------------------------------------
	protected vector Rotate(vector v, vector unitAxis, float angle)
	{
		float cosine = Math.Cos(angle);
		float sine = Math.Sin(angle);
		return v * cosine + Cross(unitAxis, v) * sine + unitAxis * (vector.Dot(unitAxis, v) * (1 - cosine));
	}
}

//! The cables currently drawn on one character.
class TDL_CableVisualSet
{
	string m_sSignature;
	ref array<TDL_CableEntity> m_aCables = new array<TDL_CableEntity>();
}

//! Keeps the drawn cables in step with what the resolver says is connected.
class TDL_CableVisuals
{
	protected static ref map<IEntity, ref TDL_CableVisualSet> s_mSets;

	//------------------------------------------------------------------------------------------------
	//! Spawns or removes cable entities so the character shows exactly its current links.
	//! Cheap to call often: nothing happens while the links are unchanged.
	static void Sync(IEntity carrier)
	{
		if (!carrier || RplSession.Mode() == RplMode.Dedicated)
			return;

		if (!s_mSets)
			s_mSets = new map<IEntity, ref TDL_CableVisualSet>();

		TDL_CableState state = TDL_CableManager.GetState(carrier);
		if (!state)
			return;

		array<TDL_CableLink> drawn = new array<TDL_CableLink>();
		string signature = string.Empty;
		foreach (TDL_CableLink link : state.GetLinks())
		{
			if (!link.m_A || !link.m_B)
				continue;
			if (!TDL_CableManager.IsOnShow(link.m_A.GetOwner(), carrier) || !TDL_CableManager.IsOnShow(link.m_B.GetOwner(), carrier))
				continue;

			drawn.Insert(link);
			signature = signature + string.Format("%1:%2>%3:%4;", link.m_A, link.m_iPortA, link.m_B, link.m_iPortB);
		}

		TDL_CableVisualSet visuals = s_mSets.Get(carrier);
		if (visuals && visuals.m_sSignature == signature && AllAlive(visuals))
			return;

		if (visuals)
			Clear(visuals);
		else
			visuals = new TDL_CableVisualSet();

		visuals.m_sSignature = signature;
		s_mSets.Set(carrier, visuals);

		if (drawn.IsEmpty())
			return;

		TDL_CableRulesConfig config = TDL_CableRulesConfig.Get();
		if (!config)
			return;

		foreach (TDL_CableLink drawnLink : drawn)
		{
			// A port that brings its own connector decides the cable, and gets the plug end.
			bool plugOnB = false;
			ResourceName prefabName = drawnLink.m_A.GetPortCablePrefab(drawnLink.m_iPortA);
			if (prefabName.IsEmpty())
			{
				prefabName = drawnLink.m_B.GetPortCablePrefab(drawnLink.m_iPortB);
				plugOnB = !prefabName.IsEmpty();
			}
			if (prefabName.IsEmpty())
				prefabName = drawnLink.m_sCablePrefab;
			if (prefabName.IsEmpty())
				prefabName = config.GetCablePrefab();

			Resource prefab = Resource.Load(prefabName);
			if (!prefab || !prefab.IsValid())
				continue;

			EntitySpawnParams params = new EntitySpawnParams();
			params.TransformMode = ETransformMode.WORLD;
			carrier.GetWorldTransform(params.Transform);

			TDL_CableEntity cable = TDL_CableEntity.Cast(GetGame().SpawnEntityPrefabLocal(prefab, carrier.GetWorld(), params));
			if (!cable)
				continue;

			// A child moves in the same transform pass as the character; a free entity placed
			// by script trails it by a frame.
			carrier.AddChild(cable, -1, EAddChildFlags.AUTO_TRANSFORM);

			// The cable lays itself out in the character's space, so the two must coincide.
			vector identity[4];
			Math3D.MatrixIdentity4(identity);
			cable.SetLocalTransform(identity);

			// Culling goes by the mesh's bind-pose box, which the laid cable leaves at once.
			// As a proxy it is culled with the character instead.
			cable.SetFlags(EntityFlags.PROXY);

			cable.SetLink(drawnLink, carrier, plugOnB);
			visuals.m_aCables.Insert(cable);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static bool AllAlive(notnull TDL_CableVisualSet visuals)
	{
		foreach (TDL_CableEntity cable : visuals.m_aCables)
		{
			if (!cable || !cable.IsLinkAlive())
				return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static void Clear(notnull TDL_CableVisualSet visuals)
	{
		foreach (TDL_CableEntity cable : visuals.m_aCables)
		{
			if (cable)
				SCR_EntityHelper.DeleteEntityAndChildren(cable);
		}
		visuals.m_aCables.Clear();
	}
}
