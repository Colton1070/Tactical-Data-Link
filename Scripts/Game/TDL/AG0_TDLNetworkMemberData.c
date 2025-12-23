// AG0_TDLNetworkMemberData.c - Mario-style codec implementation

class AG0_TDLNetworkMember
{
    // Define our data sizes
    static const int MAX_PLAYER_NAME_LENGTH = 32;  // Fixed size for player name
    static const int DATA_SIZE = 4 + MAX_PLAYER_NAME_LENGTH + 12 + 4 + 4 + 4 + 1 + 1 + 4 + 4;
	// RplId(4) + PlayerName(32) + Position(12) + Signal(4) + NetworkIP(4) + Capabilities(4) + IsPowered(1) + GPSActive(1) + OwnerPlayerId(4) + VideoSourceRplId(4) = 74 bytes
    
    protected RplId m_RplId;
    protected string m_sPlayerName;
    protected vector m_vPosition;
    protected float m_fSignalStrength;
    protected int m_iNetworkIP;
    protected int m_iDeviceCapabilities;
    protected bool m_bIsPowered;
    protected bool m_bGPSActive;
	protected int m_iOwnerPlayerId = -1;  // -1 = no player owner
	protected RplId m_VideoSourceRplId;
    
    // Getters
    RplId GetRplId() { return m_RplId; }
    string GetPlayerName() { return m_sPlayerName; }
    vector GetPosition() { return m_vPosition; }
    float GetSignalStrength() { return m_fSignalStrength; }
    int GetNetworkIP() { return m_iNetworkIP; }
    int GetCapabilities() { return m_iDeviceCapabilities; }
	int GetOwnerPlayerId() { return m_iOwnerPlayerId; }
	RplId GetVideoSourceRplId() { return m_VideoSourceRplId; }
    
    // Setters
    void SetRplId(RplId rplId) { m_RplId = rplId; }
    void SetPlayerName(string playerName) { m_sPlayerName = playerName; }
    void SetPosition(vector position) { m_vPosition = position; }
    void SetNetworkIP(int networkIP) { m_iNetworkIP = networkIP; }
    void SetSignalStrength(float strength) { m_fSignalStrength = strength; }
    void SetCapabilities(int capabilities) { m_iDeviceCapabilities = capabilities; }
	void SetOwnerPlayerId(int playerId) { m_iOwnerPlayerId = playerId; }
	void SetVideoSourceRplId(RplId rplId) { m_VideoSourceRplId = rplId; }
    
    // Extract - following Mario's pattern exactly
    static bool Extract(AG0_TDLNetworkMember instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        // RplId - 4 bytes
        snapshot.SerializeBytes(instance.m_RplId, 4);
        
        // PlayerName - fixed size 32 bytes
        // We'll serialize the string directly with padding
        string paddedName = instance.m_sPlayerName;
        int nameLen = paddedName.Length();
        
        // Ensure it's exactly 32 characters
        if (nameLen > MAX_PLAYER_NAME_LENGTH)
        {
            paddedName = paddedName.Substring(0, MAX_PLAYER_NAME_LENGTH);
        }
        else if (nameLen < MAX_PLAYER_NAME_LENGTH)
        {
            // Pad with null terminators
            for (int i = nameLen; i < MAX_PLAYER_NAME_LENGTH; i++)
                paddedName += "\0";
        }
        
        // Serialize as raw bytes
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

        
        return true;
    }
    
    // Inject - mirror of Extract
    static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, AG0_TDLNetworkMember instance)
    {
        // RplId - 4 bytes
        snapshot.SerializeBytes(instance.m_RplId, 4);
        
        // PlayerName - fixed size 32 bytes
        // Read one byte at a time and build the string
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
                
            // Convert single character
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
		
        return true;
    }
    
    // Encode - Mario's way: one simple serialize call
    static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        snapshot.Serialize(packet, AG0_TDLNetworkMember.DATA_SIZE);
    }
    
    // Decode - Mario's way: one simple serialize call
    static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        return snapshot.Serialize(packet, AG0_TDLNetworkMember.DATA_SIZE);
    }
    
    // SnapCompare - compare all bytes at once
    static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return lhs.CompareSnapshots(rhs, AG0_TDLNetworkMember.DATA_SIZE);
    }
    
    // PropCompare - compare each field
    static bool PropCompare(AG0_TDLNetworkMember instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        // Follow Mario's pattern but we need to handle the string specially
        // For now, let's just force updates
        return false;  // Always update
    }
}

// AG0_TDLNetworkMembers - container for array of members
class AG0_TDLNetworkMembers
{
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
    
    static bool Extract(AG0_TDLNetworkMembers instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
	    // Handle null instance (this is what's causing the VME)
	    if (!instance)
	    {
	        // Write -1 as sentinel for "this RplProp was null"
	        int nullCount = -1;
	        snapshot.SerializeBytes(nullCount, 4);
	        return true;
	    }
	    
	    // Normal path - instance exists (could be empty, that's fine)
	    int count = instance.Count();
	    snapshot.SerializeBytes(count, 4);
	    
	    // Sanity check
	    if (count > 1000)
	    {
	        Print(string.Format("TDL_CODEC: WARNING - Suspiciously high member count %1", count), LogLevel.WARNING);
	    }
	    
	    for (int i = 0; i < count; i++)
	    {
	        if (!instance.m_aMembers[i])
	        {
	            Print(string.Format("TDL_CODEC: ERROR - Null member at index %1", i), LogLevel.ERROR);
	            // We're committed to this count, so write empty member
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
	    // Instance is already created by replication system
	    if (!instance)
	    {
	        Print("TDL_CODEC: ERROR - Inject called with null instance", LogLevel.ERROR);
	        return false;
	    }
	    
	    // Read count
	    int count;
	    snapshot.SerializeBytes(count, 4);
	    
	    // Clear existing data
	    instance.Clear();
	    
	    // Handle null sentinel - leave as empty container
	    if (count == -1)
	    {
	        Print("TDL_CODEC: Decoded null state, keeping empty container", LogLevel.DEBUG);
	        return true;
	    }
	    
	    // Validate reasonable count
	    if (count < 0 || count > 1000) // Sanity check
	    {
	        Print(string.Format("TDL_CODEC: Invalid member count %1", count), LogLevel.ERROR);
	        return false;
	    }
	    
	    // Read each member
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
        // First read and encode the count
        int count;
        snapshot.SerializeBytes(count, 4);
        packet.Serialize(count, 32);  // 4 bytes = 32 bits
        
        // Then encode all member data
        int totalMemberBytes = count * AG0_TDLNetworkMember.DATA_SIZE;
        if (totalMemberBytes > 0)
            snapshot.Serialize(packet, totalMemberBytes);
    }
    
    static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        // First decode the count
        int count;
        packet.Serialize(count, 32);  // 4 bytes = 32 bits
        snapshot.SerializeBytes(count, 4);
        
        // Then decode all member data
        int totalMemberBytes = count * AG0_TDLNetworkMember.DATA_SIZE;
        if (totalMemberBytes > 0)
            return snapshot.Serialize(packet, totalMemberBytes);
        
        return true;
    }
    
    static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        // For now, just force updates - we can optimize later
        // A proper implementation would need to know the count to compare the right amount of bytes
        // but we can't peek at the count without modifying the snapshot position
        return false;  // Always different = always update
    }
    
    static bool PropCompare(AG0_TDLNetworkMembers instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        // Handle null case
        if (!instance)
            return false;  // Need update
            
        // For now, always update
        return false;
    }
}