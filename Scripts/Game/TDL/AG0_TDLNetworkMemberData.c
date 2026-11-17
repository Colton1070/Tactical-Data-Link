class AG0_TDLNetworkMember
{
    static const int MAX_PLAYER_NAME_LENGTH = 32;  // Fixed size for player name
    static const int DATA_SIZE = 4 + MAX_PLAYER_NAME_LENGTH + 12 + 4 + 4 + 4 + 1 + 1 + 4 + 4 + 1 + 4 + 1;
	// RplId(4) + PlayerName(32) + Position(12) + Signal(4) + NetworkIP(4) + Capabilities(4) +
	// IsPowered(1) + GPSActive(1) + OwnerPlayerId(4) + VideoSourceRplId(4) +
	// IsBridged(1) + SourceNetworkId(4) + FixLost(1) = 76 bytes

    protected RplId m_RplId;
    protected string m_sPlayerName;
    protected vector m_vPosition;
    protected float m_fSignalStrength;
    protected int m_iNetworkIP;
    protected int m_iDeviceCapabilities;
    protected bool m_bIsPowered;
    protected bool m_bGPSActive;
	protected int m_iOwnerPlayerId = -1;  // -1 = no player owner
	protected RplId m_VideoSourceRplId = RplId.Invalid();

	// Bridge fields
	// m_bIsBridged: true when this member belongs to a foreign network relayed via a bridge device
	// m_iSourceNetworkId: the network ID this member actually lives in (for UI labeling)
	protected bool m_bIsBridged = false;
	protected int m_iSourceNetworkId = -1;

	// The device is linked but has no GPS antenna, so the position it sends is its last fix.
	protected bool m_bFixLost;

	// Client-side only, never serialized. A member the latest update left out keeps its last
	// reported position and is drawn as stale, the way a TAK contact that stops reporting is.
	protected float m_fLastHeardS;
	protected bool m_bStale;
	protected float m_fLastFixS;

    // Getters
    RplId GetRplId() { return m_RplId; }
    string GetPlayerName() { return m_sPlayerName; }
    vector GetPosition() { return m_vPosition; }
    float GetSignalStrength() { return m_fSignalStrength; }
    int GetNetworkIP() { return m_iNetworkIP; }
    int GetCapabilities() { return m_iDeviceCapabilities; }
    // Surfaces the deserialized GPS-active flag — populated by the snapshot
    // Inject path, no setter elsewhere in code. Used by the web mirror's
    // per-viewer contacts array to render the GPS-fix badge per network member.
    bool IsGPSActive() { return m_bGPSActive; }
	int GetOwnerPlayerId() { return m_iOwnerPlayerId; }
	RplId GetVideoSourceRplId() { return m_VideoSourceRplId; }
	bool IsBridged() { return m_bIsBridged; }
	int GetSourceNetworkId() { return m_iSourceNetworkId; }
	bool IsPowered() { return m_bIsPowered; }
	bool IsStale() { return m_bStale; }
	float GetLastHeardS() { return m_fLastHeardS; }
	bool IsFixLost() { return m_bFixLost; }
	float GetLastFixS() { return m_fLastFixS; }

	//------------------------------------------------------------------------------------------------
	//! The drawn position is old: either the link is gone or the device lost its GPS fix.
	//! Link-state UI (signal, NO LINK) reads IsStale instead, because a member without a fix
	//! is still reachable.
	bool IsPositionStale() { return m_bStale || m_bFixLost; }

	//------------------------------------------------------------------------------------------------
	//! Seconds since the drawn position was current; 0 while it is.
	float GetPositionAgeS()
	{
		// Members that never went through MergeLive have no fix time; show them as just stale.
		if (!IsPositionStale() || m_fLastFixS <= 0)
			return 0;
		return Now() - m_fLastFixS;
	}

	//------------------------------------------------------------------------------------------------
	string GetPositionAgeText()
	{
		return FormatAge(GetPositionAgeS());
	}

	//------------------------------------------------------------------------------------------------
	//! Seconds since this member was last in a live update; 0 while it is live.
	float GetStaleAgeS()
	{
		if (!m_bStale)
			return 0;
		return Now() - m_fLastHeardS;
	}

	//------------------------------------------------------------------------------------------------
	//! "45s" under a minute, "12m" after, for list rows and map labels.
	string GetStaleAgeText()
	{
		return FormatAge(GetStaleAgeS());
	}

	//------------------------------------------------------------------------------------------------
	//! "45s" under a minute, "12m" after.
	static string FormatAge(float seconds)
	{
		int age = seconds;
		if (age < 60)
			return string.Format("%1s", age);
		return string.Format("%1m", age / 60);
	}

	//------------------------------------------------------------------------------------------------
	static float Now()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;
		return world.GetWorldTime() / 1000.0;
	}

	//------------------------------------------------------------------------------------------------
	void MarkHeard(float now)
	{
		m_bStale = false;
		m_fLastHeardS = now;
	}

	//------------------------------------------------------------------------------------------------
	//! Carries the fix time across updates: it only advances while the device has a fix, so
	//! a member without one ages from the moment it lost it, not from its latest update.
	void TrackFix(AG0_TDLNetworkMember previous, float now)
	{
		if (!m_bFixLost)
		{
			m_fLastFixS = now;
			return;
		}
		if (previous && previous.m_bFixLost)
			m_fLastFixS = previous.m_fLastFixS;
		else
			m_fLastFixS = now;
	}

	//------------------------------------------------------------------------------------------------
	//! A member without a fix for as long as an out-of-contact one is kept loses its map icon
	//! the same way. It stays in the member list because it is still reachable.
	bool IsPositionExpired()
	{
		return m_bFixLost && GetPositionAgeS() > AG0_TDLNetworkMembers.STALE_EXPIRE_S;
	}

	//------------------------------------------------------------------------------------------------
	//! What was live about the member goes with the link: no signal, no feed to open.
	void MarkStale()
	{
		m_bStale = true;
		m_fSignalStrength = 0;
		m_VideoSourceRplId = RplId.Invalid();
	}

    // Setters
    void SetRplId(RplId rplId) { m_RplId = rplId; }
    void SetPlayerName(string playerName) { m_sPlayerName = playerName; }
    void SetPosition(vector position) { m_vPosition = position; }
    void SetNetworkIP(int networkIP) { m_iNetworkIP = networkIP; }
    void SetSignalStrength(float strength) { m_fSignalStrength = strength; }
    void SetCapabilities(int capabilities) { m_iDeviceCapabilities = capabilities; }
	void SetOwnerPlayerId(int playerId) { m_iOwnerPlayerId = playerId; }
	void SetVideoSourceRplId(RplId rplId) { m_VideoSourceRplId = rplId; }
	void SetIsBridged(bool bridged) { m_bIsBridged = bridged; }
	void SetSourceNetworkId(int networkId) { m_iSourceNetworkId = networkId; }
	void SetIsPowered(bool powered) { m_bIsPowered = powered; }
	void SetGPSActive(bool active) { m_bGPSActive = active; }
	void SetFixLost(bool lost) { m_bFixLost = lost; }

    static bool Extract(AG0_TDLNetworkMember instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        // RplId - 4 bytes
        snapshot.SerializeBytes(instance.m_RplId, 4);

        // PlayerName - fixed size 32 bytes
        string paddedName = instance.m_sPlayerName;
        int nameLen = paddedName.Length();

        if (nameLen > MAX_PLAYER_NAME_LENGTH)
        {
            paddedName = paddedName.Substring(0, MAX_PLAYER_NAME_LENGTH);
        }
        else if (nameLen < MAX_PLAYER_NAME_LENGTH)
        {
            for (int i = nameLen; i < MAX_PLAYER_NAME_LENGTH; i++)
                paddedName += "\0";
        }

        for (int i = 0; i < MAX_PLAYER_NAME_LENGTH; i++)
        {
            int charByte = 0;
            if (i < nameLen)
                charByte = paddedName.ToAscii(i);
            snapshot.SerializeBytes(charByte, 1);
        }

        // Position - 12 bytes (3 floats)
        snapshot.SerializeBytes(instance.m_vPosition, 12);

        // SignalStrength - 4 bytes
        snapshot.SerializeBytes(instance.m_fSignalStrength, 4);

        // NetworkIP - 4 bytes
        snapshot.SerializeBytes(instance.m_iNetworkIP, 4);

        // Capabilities - 4 bytes
        snapshot.SerializeBytes(instance.m_iDeviceCapabilities, 4);

        // Booleans - 1 byte each
        snapshot.SerializeBytes(instance.m_bIsPowered, 1);
        snapshot.SerializeBytes(instance.m_bGPSActive, 1);

		// PlayerId - 4 bytes
		snapshot.SerializeBytes(instance.m_iOwnerPlayerId, 4);

		// VideoSourceRplId - 4 bytes
        snapshot.SerializeBytes(instance.m_VideoSourceRplId, 4);

		// IsBridged - 1 byte
		snapshot.SerializeBytes(instance.m_bIsBridged, 1);

		// SourceNetworkId - 4 bytes
		snapshot.SerializeBytes(instance.m_iSourceNetworkId, 4);

		// FixLost - 1 byte
		snapshot.SerializeBytes(instance.m_bFixLost, 1);

        return true;
    }

    // Inject - mirror of Extract
    static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, AG0_TDLNetworkMember instance)
    {
        // RplId - 4 bytes
        snapshot.SerializeBytes(instance.m_RplId, 4);

        // PlayerName - fixed size 32 bytes
        instance.m_sPlayerName = "";
        for (int i = 0; i < MAX_PLAYER_NAME_LENGTH; i++)
        {
            int charByte;
            snapshot.SerializeBytes(charByte, 1);

            if (charByte == 0)
            {
                // Skip remaining bytes to maintain position
                for (int j = i + 1; j < MAX_PLAYER_NAME_LENGTH; j++)
                {
                    int dummy;
                    snapshot.SerializeBytes(dummy, 1);
                }
                break;
            }

            string charStr = string.Empty;
            charStr += charByte.AsciiToString();
            instance.m_sPlayerName += charStr;
        }

        // Position - 12 bytes
        snapshot.SerializeBytes(instance.m_vPosition, 12);

        // SignalStrength - 4 bytes
        snapshot.SerializeBytes(instance.m_fSignalStrength, 4);

        // NetworkIP - 4 bytes
        snapshot.SerializeBytes(instance.m_iNetworkIP, 4);

        // Capabilities - 4 bytes
        snapshot.SerializeBytes(instance.m_iDeviceCapabilities, 4);

        // Booleans - 1 byte each
        snapshot.SerializeBytes(instance.m_bIsPowered, 1);
        snapshot.SerializeBytes(instance.m_bGPSActive, 1);

		// PlayerId - 4 bytes
		snapshot.SerializeBytes(instance.m_iOwnerPlayerId, 4);

		// VideoSourceRplId - 4 bytes
		snapshot.SerializeBytes(instance.m_VideoSourceRplId, 4);

		// IsBridged - 1 byte
		snapshot.SerializeBytes(instance.m_bIsBridged, 1);

		// SourceNetworkId - 4 bytes
		snapshot.SerializeBytes(instance.m_iSourceNetworkId, 4);

		// FixLost - 1 byte
		snapshot.SerializeBytes(instance.m_bFixLost, 1);

        return true;
    }

    static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        snapshot.Serialize(packet, AG0_TDLNetworkMember.DATA_SIZE);
    }

    static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        return snapshot.Serialize(packet, AG0_TDLNetworkMember.DATA_SIZE);
    }

    // SnapCompare - compare all bytes at once
    static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return lhs.CompareSnapshots(rhs, AG0_TDLNetworkMember.DATA_SIZE);
    }

    // PropCompare - always update
    static bool PropCompare(AG0_TDLNetworkMember instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        return false;
    }
}

// AG0_TDLNetworkMembers - container for array of members
class AG0_TDLNetworkMembers
{
    //! How long a member out of contact is kept at its last position before it is dropped.
    static const float STALE_EXPIRE_S = 300;

    [Attribute("")]
    ref array<ref AG0_TDLNetworkMember> m_aMembers;

    void AG0_TDLNetworkMembers()
    {
        m_aMembers = new array<ref AG0_TDLNetworkMember>();
    }

    int Count()
    {
        return m_aMembers.Count();
    }

    void Clear()
    {
        m_aMembers.Clear();
    }

    void Add(AG0_TDLNetworkMember member)
    {
        m_aMembers.Insert(member);
    }

    AG0_TDLNetworkMember Get(int index)
    {
        if (index >= 0 && index < m_aMembers.Count())
            return m_aMembers[index];
        return null;
    }

    AG0_TDLNetworkMember GetByRplId(RplId rplId)
    {
        foreach (AG0_TDLNetworkMember member : m_aMembers)
        {
            if (member.GetRplId() == rplId)
                return member;
        }
        return null;
    }

    map<RplId, ref AG0_TDLNetworkMember> ToMap()
    {
        map<RplId, ref AG0_TDLNetworkMember> result = new map<RplId, ref AG0_TDLNetworkMember>();

        foreach (AG0_TDLNetworkMember member : m_aMembers)
        {
            result.Set(member.GetRplId(), member);
        }

        return result;
    }

    static AG0_TDLNetworkMembers FromMap(map<RplId, ref AG0_TDLNetworkMember> memberMap)
    {
        AG0_TDLNetworkMembers result = new AG0_TDLNetworkMembers();

        foreach (RplId rplId, AG0_TDLNetworkMember member : memberMap)
        {
            result.Add(member);
        }

        return result;
    }

    //------------------------------------------------------------------------------------------------
    //! Client: the member set after a live update. A member the update leaves out but the
    //! network still lists is kept, stale, until STALE_EXPIRE_S; one the network no longer lists
    //! goes at once, so a device that left, died or was deleted never lingers as a stale icon.
    static AG0_TDLNetworkMembers MergeLive(AG0_TDLNetworkMembers previous, array<ref AG0_TDLNetworkMember> live, array<RplId> roster)
    {
        float now = AG0_TDLNetworkMember.Now();
        AG0_TDLNetworkMembers result = new AG0_TDLNetworkMembers();
        set<RplId> liveIds = new set<RplId>();
        foreach (AG0_TDLNetworkMember member : live)
        {
            if (!member)
                continue;
            member.MarkHeard(now);

            AG0_TDLNetworkMember before;
            if (previous)
                before = previous.GetByRplId(member.GetRplId());
            member.TrackFix(before, now);

            liveIds.Insert(member.GetRplId());
            result.Add(member);
        }
        if (!previous || !roster)
            return result;

        set<RplId> rosterIds = new set<RplId>();
        foreach (RplId id : roster)
            rosterIds.Insert(id);
        foreach (AG0_TDLNetworkMember old : previous.m_aMembers)
        {
            if (!old || liveIds.Contains(old.GetRplId()) || !rosterIds.Contains(old.GetRplId()))
                continue;
            if (!old.IsStale())
                old.MarkStale();
            if (now - old.GetLastHeardS() > STALE_EXPIRE_S)
                continue;
            result.Add(old);
        }
        return result;
    }

    static bool Extract(AG0_TDLNetworkMembers instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
	    if (!instance)
	    {
	        int nullCount = -1;
	        snapshot.SerializeBytes(nullCount, 4);
	        return true;
	    }

	    int count = instance.Count();
	    snapshot.SerializeBytes(count, 4);

	    if (count > 1000)
	    {
	        Print(string.Format("TDL_CODEC: WARNING - Suspiciously high member count %1", count), LogLevel.WARNING);
	    }

	    for (int i = 0; i < count; i++)
	    {
	        if (!instance.m_aMembers[i])
	        {
	            Print(string.Format("TDL_CODEC: ERROR - Null member at index %1", i), LogLevel.ERROR);
	            AG0_TDLNetworkMember emptyMember = new AG0_TDLNetworkMember();
	            AG0_TDLNetworkMember.Extract(emptyMember, ctx, snapshot);
	        }
	        else
	        {
	            AG0_TDLNetworkMember.Extract(instance.m_aMembers[i], ctx, snapshot);
	        }
	    }

	    return true;
	}

	static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, AG0_TDLNetworkMembers instance)
	{
	    if (!instance)
	    {
	        Print("TDL_CODEC: ERROR - Inject called with null instance", LogLevel.ERROR);
	        return false;
	    }

	    int count;
	    snapshot.SerializeBytes(count, 4);

	    instance.Clear();

	    if (count == -1)
	    {
	        Print("TDL_CODEC: Decoded null state, keeping empty container", LogLevel.DEBUG);
	        return true;
	    }

	    if (count < 0 || count > 1000)
	    {
	        Print(string.Format("TDL_CODEC: Invalid member count %1", count), LogLevel.ERROR);
	        return false;
	    }

	    for (int i = 0; i < count; i++)
	    {
	        AG0_TDLNetworkMember member = new AG0_TDLNetworkMember();
	        if (!AG0_TDLNetworkMember.Inject(snapshot, ctx, member))
	        {
	            Print(string.Format("TDL_CODEC: Failed to inject member %1/%2", i, count), LogLevel.ERROR);
	            return false;
	        }
	        instance.Add(member);
	    }

	    return true;
	}

    // Calculate total size: 4 bytes for count + (member count * member size)
    static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        int count;
        snapshot.SerializeBytes(count, 4);
        packet.Serialize(count, 32);

        int totalMemberBytes = count * AG0_TDLNetworkMember.DATA_SIZE;
        if (totalMemberBytes > 0)
            snapshot.Serialize(packet, totalMemberBytes);
    }

    static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        int count;
        packet.Serialize(count, 32);
        snapshot.SerializeBytes(count, 4);

        int totalMemberBytes = count * AG0_TDLNetworkMember.DATA_SIZE;
        if (totalMemberBytes > 0)
            return snapshot.Serialize(packet, totalMemberBytes);

        return true;
    }

    static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return false;  // Always update
    }

    static bool PropCompare(AG0_TDLNetworkMembers instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        if (!instance)
            return false;
        return false;
    }
}