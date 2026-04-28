// AG0_TDLNetworkMemberData.c
//
// Codec rewritten to match the AG0_TDLMessageClient pattern (which works in real
// MP). The original used raw `SerializeBytes` plus a manual byte loop for the
// player name — that path silently dropped the string when sent as `array<ref T>`
// over a real dedicated server. Listen-server worked because in-process Rpc skips
// serialization.
//
// All snapshot writes use typed serializers (SerializeInt / SerializeString /
// SerializeFloat) so the snapshot's internal type tracking lines up with what
// Encode/Decode expect. Bools are stored as int 0/1 to keep the wire byte math
// trivial and avoid SerializeBool size ambiguity (1 bit vs 1 byte).

class AG0_TDLNetworkMember
{
    int m_RplId;
    string m_sPlayerName;
    vector m_vPosition;
    float m_fSignalStrength;
    int m_iNetworkIP;
    int m_iDeviceCapabilities;
    int m_iIsPowered;       // bool stored as int (0/1)
    int m_iGPSActive;       // bool stored as int (0/1)
    int m_iOwnerPlayerId = -1;
    int m_VideoSourceRplId;
    int m_iIsBridged = 0;   // bool stored as int (0/1)
    int m_iSourceNetworkId = -1;

    // Getters
    RplId GetRplId() { return m_RplId; }
    string GetPlayerName() { return m_sPlayerName; }
    vector GetPosition() { return m_vPosition; }
    float GetSignalStrength() { return m_fSignalStrength; }
    int GetNetworkIP() { return m_iNetworkIP; }
    int GetCapabilities() { return m_iDeviceCapabilities; }
    int GetOwnerPlayerId() { return m_iOwnerPlayerId; }
    RplId GetVideoSourceRplId() { return m_VideoSourceRplId; }
    bool IsBridged() { return m_iIsBridged != 0; }
    int GetSourceNetworkId() { return m_iSourceNetworkId; }

    // Setters
    void SetRplId(RplId rplId) { m_RplId = rplId; }
    void SetPlayerName(string playerName) { m_sPlayerName = playerName; }
    void SetPosition(vector position) { m_vPosition = position; }
    void SetNetworkIP(int networkIP) { m_iNetworkIP = networkIP; }
    void SetSignalStrength(float strength) { m_fSignalStrength = strength; }
    void SetCapabilities(int capabilities) { m_iDeviceCapabilities = capabilities; }
    void SetOwnerPlayerId(int playerId) { m_iOwnerPlayerId = playerId; }
    void SetVideoSourceRplId(RplId rplId) { m_VideoSourceRplId = rplId; }
    void SetIsBridged(bool bridged)
    {
        if (bridged)
            m_iIsBridged = 1;
        else
            m_iIsBridged = 0;
    }
    void SetSourceNetworkId(int networkId) { m_iSourceNetworkId = networkId; }

    //------------------------------------------------------------------------------------------------
    // SerializeFloat takes an inout parameter — vector index access is an rvalue.
    // Use temp floats and round-trip through Vector().
    static bool Extract(AG0_TDLNetworkMember instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        snapshot.SerializeInt(instance.m_RplId);
        snapshot.SerializeString(instance.m_sPlayerName);
        float px = instance.m_vPosition[0];
        float py = instance.m_vPosition[1];
        float pz = instance.m_vPosition[2];
        snapshot.SerializeFloat(px);
        snapshot.SerializeFloat(py);
        snapshot.SerializeFloat(pz);
        instance.m_vPosition = Vector(px, py, pz);
        snapshot.SerializeFloat(instance.m_fSignalStrength);
        snapshot.SerializeInt(instance.m_iNetworkIP);
        snapshot.SerializeInt(instance.m_iDeviceCapabilities);
        snapshot.SerializeInt(instance.m_iIsPowered);
        snapshot.SerializeInt(instance.m_iGPSActive);
        snapshot.SerializeInt(instance.m_iOwnerPlayerId);
        snapshot.SerializeInt(instance.m_VideoSourceRplId);
        snapshot.SerializeInt(instance.m_iIsBridged);
        snapshot.SerializeInt(instance.m_iSourceNetworkId);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, AG0_TDLNetworkMember instance)
    {
        snapshot.SerializeInt(instance.m_RplId);
        snapshot.SerializeString(instance.m_sPlayerName);
        float px = instance.m_vPosition[0];
        float py = instance.m_vPosition[1];
        float pz = instance.m_vPosition[2];
        snapshot.SerializeFloat(px);
        snapshot.SerializeFloat(py);
        snapshot.SerializeFloat(pz);
        instance.m_vPosition = Vector(px, py, pz);
        snapshot.SerializeFloat(instance.m_fSignalStrength);
        snapshot.SerializeInt(instance.m_iNetworkIP);
        snapshot.SerializeInt(instance.m_iDeviceCapabilities);
        snapshot.SerializeInt(instance.m_iIsPowered);
        snapshot.SerializeInt(instance.m_iGPSActive);
        snapshot.SerializeInt(instance.m_iOwnerPlayerId);
        snapshot.SerializeInt(instance.m_VideoSourceRplId);
        snapshot.SerializeInt(instance.m_iIsBridged);
        snapshot.SerializeInt(instance.m_iSourceNetworkId);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    // Layout (in order):
    //   int RplId          4
    //   string PlayerName  variable (length-prefixed)
    //   3x float Position  12
    //   float Signal       4
    //   int NetworkIP      4
    //   int Capabilities   4
    //   int IsPowered      4
    //   int GPSActive      4
    //   int OwnerPlayerId  4
    //   int VideoSourceId  4
    //   int IsBridged      4
    //   int SourceNetId    4
    // Fixed bytes: 52 + variable string.
    static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        snapshot.Serialize(packet, 4);     // RplId
        snapshot.EncodeString(packet);     // PlayerName
        snapshot.Serialize(packet, 16);    // 3 floats Position + Signal
        snapshot.Serialize(packet, 32);    // NetworkIP + Capabilities + IsPowered + GPSActive + OwnerPlayerId + VideoSourceRplId + IsBridged + SourceNetworkId
    }

    //------------------------------------------------------------------------------------------------
    static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        snapshot.Serialize(packet, 4);
        snapshot.DecodeString(packet);
        snapshot.Serialize(packet, 16);
        snapshot.Serialize(packet, 32);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return lhs.CompareSnapshots(rhs, 4)
            && lhs.CompareStringSnapshots(rhs)
            && lhs.CompareSnapshots(rhs, 16)
            && lhs.CompareSnapshots(rhs, 32);
    }

    //------------------------------------------------------------------------------------------------
    static bool PropCompare(AG0_TDLNetworkMember instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        return false;  // Always update.
    }
}

//------------------------------------------------------------------------------------------------
// Container for arrays of members.
class AG0_TDLNetworkMembers
{
    [Attribute("")]
    ref array<ref AG0_TDLNetworkMember> m_aMembers;

    void AG0_TDLNetworkMembers()
    {
        m_aMembers = new array<ref AG0_TDLNetworkMember>();
    }

    int Count() { return m_aMembers.Count(); }
    void Clear() { m_aMembers.Clear(); }
    void Add(AG0_TDLNetworkMember member) { m_aMembers.Insert(member); }

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
}
