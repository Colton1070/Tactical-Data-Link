// AG0_TDLMessage.c - TDL Network Messaging Data Classes

//------------------------------------------------------------------------------------------------
// Message destination types
//------------------------------------------------------------------------------------------------
enum ETDLMessageType
{
    NETWORK_BROADCAST,  // Message to all network members
    DIRECT              // Direct message to specific device
}

//------------------------------------------------------------------------------------------------
// Message delivery status (for sender's view)
//------------------------------------------------------------------------------------------------
enum ETDLMessageStatus
{
    PENDING,    // Not yet delivered to recipient
    DELIVERED,  // Delivered but not read
    READ        // Recipient has viewed the message
}

//------------------------------------------------------------------------------------------------
// TDL Message - Core message data structure
// Serializable for RPC transmission
//------------------------------------------------------------------------------------------------
class AG0_TDLMessage
{
    // Message identification
    protected int m_iMessageId;
    protected int m_iNetworkId;
    
    // Sender info
    protected RplId m_SenderRplId;
    protected string m_sSenderCallsign;
    
    // Timing
    protected int m_iTimestamp;  // Epoch seconds when sent
    
    // Content
    protected string m_sContent;
    
    // Routing
    protected ETDLMessageType m_eMessageType;
    protected RplId m_DirectRecipientRplId;      // Only used for DIRECT messages
    protected string m_sDirectRecipientCallsign; // For display purposes
    
    // Delivery tracking (server-side only, not serialized to clients)
    protected ref set<RplId> m_DeliveredTo;
    protected ref set<RplId> m_ReadBy;
    
    //------------------------------------------------------------------------------------------------
    void AG0_TDLMessage()
    {
        m_DeliveredTo = new set<RplId>();
        m_ReadBy = new set<RplId>();
    }
    
    //------------------------------------------------------------------------------------------------
    // Factory method for network broadcast
    //------------------------------------------------------------------------------------------------
    static AG0_TDLMessage CreateBroadcast(int messageId, int networkId, RplId senderRplId, 
                                          string senderCallsign, string content)
    {
        AG0_TDLMessage msg = new AG0_TDLMessage();
        msg.m_iMessageId = messageId;
        msg.m_iNetworkId = networkId;
        msg.m_SenderRplId = senderRplId;
        msg.m_sSenderCallsign = senderCallsign;
        msg.m_iTimestamp = System.GetUnixTime();
        msg.m_sContent = content;
        msg.m_eMessageType = ETDLMessageType.NETWORK_BROADCAST;
        msg.m_DirectRecipientRplId = RplId.Invalid();
        msg.m_sDirectRecipientCallsign = "";
        
        // Sender has "delivered" to themselves
        msg.m_DeliveredTo.Insert(senderRplId);
        
        return msg;
    }
    
    //------------------------------------------------------------------------------------------------
    // Factory method for direct message
    //------------------------------------------------------------------------------------------------
    static AG0_TDLMessage CreateDirect(int messageId, int networkId, RplId senderRplId, 
                                       string senderCallsign, string content,
                                       RplId recipientRplId, string recipientCallsign)
    {
        AG0_TDLMessage msg = new AG0_TDLMessage();
        msg.m_iMessageId = messageId;
        msg.m_iNetworkId = networkId;
        msg.m_SenderRplId = senderRplId;
        msg.m_sSenderCallsign = senderCallsign;
        msg.m_iTimestamp = System.GetUnixTime();
        msg.m_sContent = content;
        msg.m_eMessageType = ETDLMessageType.DIRECT;
        msg.m_DirectRecipientRplId = recipientRplId;
        msg.m_sDirectRecipientCallsign = recipientCallsign;
        
        // Sender has "delivered" to themselves
        msg.m_DeliveredTo.Insert(senderRplId);
        
        return msg;
    }
    
    //------------------------------------------------------------------------------------------------
    // Getters
    //------------------------------------------------------------------------------------------------
    int GetMessageId() { return m_iMessageId; }
    int GetNetworkId() { return m_iNetworkId; }
    RplId GetSenderRplId() { return m_SenderRplId; }
    string GetSenderCallsign() { return m_sSenderCallsign; }
    int GetTimestamp() { return m_iTimestamp; }
    string GetContent() { return m_sContent; }
    ETDLMessageType GetMessageType() { return m_eMessageType; }
    RplId GetDirectRecipientRplId() { return m_DirectRecipientRplId; }
    string GetDirectRecipientCallsign() { return m_sDirectRecipientCallsign; }
    
    //------------------------------------------------------------------------------------------------
    // Delivery tracking (server-side)
    //------------------------------------------------------------------------------------------------
    bool IsDeliveredTo(RplId deviceRplId)
    {
        return m_DeliveredTo.Contains(deviceRplId);
    }
    
    void MarkDeliveredTo(RplId deviceRplId)
    {
        m_DeliveredTo.Insert(deviceRplId);
    }
    
    bool IsReadBy(RplId deviceRplId)
    {
        return m_ReadBy.Contains(deviceRplId);
    }
    
    void MarkReadBy(RplId deviceRplId)
    {
        m_ReadBy.Insert(deviceRplId);
    }
    
    set<RplId> GetDeliveredTo() { return m_DeliveredTo; }
    set<RplId> GetReadBy() { return m_ReadBy; }
    
    //------------------------------------------------------------------------------------------------
    // Get delivery status for a specific device (used by sender to show receipt status)
    //------------------------------------------------------------------------------------------------
    ETDLMessageStatus GetStatusForRecipient(RplId recipientRplId)
    {
        if (m_ReadBy.Contains(recipientRplId))
            return ETDLMessageStatus.READ;
        if (m_DeliveredTo.Contains(recipientRplId))
            return ETDLMessageStatus.DELIVERED;
        return ETDLMessageStatus.PENDING;
    }
    
    //------------------------------------------------------------------------------------------------
    // Check if message is relevant to a device (should they see it?)
    //------------------------------------------------------------------------------------------------
    bool IsRelevantTo(RplId deviceRplId)
    {
        // Sender always sees their own messages
        if (deviceRplId == m_SenderRplId)
            return true;
        
        // Broadcast messages are relevant to everyone
        if (m_eMessageType == ETDLMessageType.NETWORK_BROADCAST)
            return true;
        
        // Direct messages only relevant to recipient
        if (m_eMessageType == ETDLMessageType.DIRECT)
            return deviceRplId == m_DirectRecipientRplId;
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    // Check if this message should be delivered to a device based on current connectivity
    // Returns true if the device can receive this message now
    //------------------------------------------------------------------------------------------------
    bool CanDeliverTo(RplId targetRplId, set<RplId> targetConnectedDevices)
    {
        // Already delivered
        if (m_DeliveredTo.Contains(targetRplId))
            return false;
        
        // Not relevant to this device
        if (!IsRelevantTo(targetRplId))
            return false;
        
        // Check if any device that HAS the message is connected to target
        foreach (RplId deliveredRplId : m_DeliveredTo)
        {
            if (targetConnectedDevices.Contains(deliveredRplId))
                return true;
        }
        
        return false;
    }
}

//------------------------------------------------------------------------------------------------
// Client-side message view - simplified version sent to clients
// Does not include delivery tracking sets (those are server-only)
//------------------------------------------------------------------------------------------------
class AG0_TDLMessageClient
{   
	int messageId;
    int networkId;
    RplId senderRplId;
    string senderCallsign;
    int timestamp;
    string content;
    ETDLMessageType messageType;
    RplId directRecipientRplId;
    string directRecipientCallsign;
    ETDLMessageStatus status; // Delivery status (for sender's messages)
    
    //------------------------------------------------------------------------------------------------
    static bool Extract(AG0_TDLMessageClient instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        snapshot.SerializeInt(instance.messageId);
        snapshot.SerializeInt(instance.networkId);
        snapshot.SerializeInt(instance.senderRplId);
        snapshot.SerializeString(instance.senderCallsign);
        snapshot.SerializeInt(instance.timestamp);
        snapshot.SerializeString(instance.content);
        snapshot.SerializeInt(instance.messageType);
        snapshot.SerializeInt(instance.directRecipientRplId);
        snapshot.SerializeString(instance.directRecipientCallsign);
        snapshot.SerializeInt(instance.status);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, AG0_TDLMessageClient instance)
    {
        snapshot.SerializeInt(instance.messageId);
        snapshot.SerializeInt(instance.networkId);
        snapshot.SerializeInt(instance.senderRplId);
        snapshot.SerializeString(instance.senderCallsign);
        snapshot.SerializeInt(instance.timestamp);
        snapshot.SerializeString(instance.content);
        snapshot.SerializeInt(instance.messageType);
        snapshot.SerializeInt(instance.directRecipientRplId);
        snapshot.SerializeString(instance.directRecipientCallsign);
        snapshot.SerializeInt(instance.status);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
    {
        snapshot.Serialize(packet, 12);    // messageId + networkId + senderRplId (3 ints)
        snapshot.EncodeString(packet);     // senderCallsign
        snapshot.Serialize(packet, 4);     // timestamp
        snapshot.EncodeString(packet);     // content
        snapshot.Serialize(packet, 8);     // messageType + directRecipientRplId (2 ints)
        snapshot.EncodeString(packet);     // directRecipientCallsign
        snapshot.Serialize(packet, 4);     // status
    }
    
    //------------------------------------------------------------------------------------------------
    static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
    {
        snapshot.Serialize(packet, 12);    // messageId + networkId + senderRplId
        snapshot.DecodeString(packet);     // senderCallsign
        snapshot.Serialize(packet, 4);     // timestamp
        snapshot.DecodeString(packet);     // content
        snapshot.Serialize(packet, 8);     // messageType + directRecipientRplId
        snapshot.DecodeString(packet);     // directRecipientCallsign
        snapshot.Serialize(packet, 4);     // status
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
    {
        return lhs.CompareSnapshots(rhs, 12)        // messageId + networkId + senderRplId
            && lhs.CompareStringSnapshots(rhs)      // senderCallsign
            && lhs.CompareSnapshots(rhs, 4)         // timestamp
            && lhs.CompareStringSnapshots(rhs)      // content
            && lhs.CompareSnapshots(rhs, 8)         // messageType + directRecipientRplId
            && lhs.CompareStringSnapshots(rhs)      // directRecipientCallsign
            && lhs.CompareSnapshots(rhs, 4);        // status
    }
    
    //------------------------------------------------------------------------------------------------
    static bool PropCompare(AG0_TDLMessageClient instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
    {
        return snapshot.CompareInt(instance.messageId)
            && snapshot.CompareInt(instance.networkId)
            && snapshot.CompareInt(instance.senderRplId)
            && snapshot.CompareString(instance.senderCallsign)
            && snapshot.CompareInt(instance.timestamp)
            && snapshot.CompareString(instance.content)
            && snapshot.CompareInt(instance.messageType)
            && snapshot.CompareInt(instance.directRecipientRplId)
            && snapshot.CompareString(instance.directRecipientCallsign)
            && snapshot.CompareInt(instance.status);
    }
	
    //------------------------------------------------------------------------------------------------
    // Create from server message for a specific viewer
    //------------------------------------------------------------------------------------------------
    static AG0_TDLMessageClient FromServerMessage(AG0_TDLMessage serverMsg, RplId viewerRplId)
    {
        AG0_TDLMessageClient clientMsg = new AG0_TDLMessageClient();
        clientMsg.messageId = serverMsg.GetMessageId();
        clientMsg.networkId = serverMsg.GetNetworkId();
        clientMsg.senderRplId = serverMsg.GetSenderRplId();
        clientMsg.senderCallsign = serverMsg.GetSenderCallsign();
        clientMsg.timestamp = serverMsg.GetTimestamp();
        clientMsg.content = serverMsg.GetContent();
        clientMsg.messageType = serverMsg.GetMessageType();
        clientMsg.directRecipientRplId = serverMsg.GetDirectRecipientRplId();
        clientMsg.directRecipientCallsign = serverMsg.GetDirectRecipientCallsign();
        
        // For sender, show delivery status to recipient
        // For recipient, status is always "delivered" (they have it)
        if (viewerRplId == serverMsg.GetSenderRplId())
        {
            // Sender viewing - show status for the recipient
            if (serverMsg.GetMessageType() == ETDLMessageType.DIRECT)
            {
                clientMsg.status = serverMsg.GetStatusForRecipient(serverMsg.GetDirectRecipientRplId());
            }
            else
            {
                // For broadcasts, show READ if anyone has read it, DELIVERED if anyone received
                // This is simplified - could be enhanced to show per-recipient status
                clientMsg.status = ETDLMessageStatus.DELIVERED;
                foreach (RplId readerId : serverMsg.GetReadBy())
                {
                    if (readerId != viewerRplId)
                    {
                        clientMsg.status = ETDLMessageStatus.READ;
                        break;
                    }
                }
            }
        }
        else
        {
            // Recipient viewing - they have it, so it's delivered
            clientMsg.status = ETDLMessageStatus.DELIVERED;
        }
        
        return clientMsg;
    }
    
    //------------------------------------------------------------------------------------------------
    // Helper to check if this is an outgoing message for a viewer
    //------------------------------------------------------------------------------------------------
    bool IsOutgoing(RplId viewerRplId)
    {
        return senderRplId == viewerRplId;
    }
    
    //------------------------------------------------------------------------------------------------
    // Get conversation ID for grouping messages
    // For broadcasts: "NETWORK"
    // For direct: the OTHER party's RplId (not self)
    //------------------------------------------------------------------------------------------------
    string GetConversationId(RplId viewerRplId)
    {
        if (messageType == ETDLMessageType.NETWORK_BROADCAST)
            return "NETWORK";
        
        // For direct messages, conversation is with the "other" party
        if (senderRplId == viewerRplId)
            return directRecipientRplId.ToString();
        else
            return senderRplId.ToString();
    }
}

//------------------------------------------------------------------------------------------------
// Container for client-side messages with helper methods
//------------------------------------------------------------------------------------------------
class AG0_TDLMessageStore
{
    protected ref array<ref AG0_TDLMessageClient> m_aMessages = {};
    protected ref map<int, int> m_mMessageIndex = new map<int, int>();  // messageId -> array index
    
    //------------------------------------------------------------------------------------------------
    void Clear()
    {
        m_aMessages.Clear();
        m_mMessageIndex.Clear();
    }
    
    //------------------------------------------------------------------------------------------------
    void AddOrUpdateMessage(AG0_TDLMessageClient msg)
    {
        if (m_mMessageIndex.Contains(msg.messageId))
        {
            // Update existing
            int idx = m_mMessageIndex.Get(msg.messageId);
            m_aMessages[idx] = msg;
        }
        else
        {
            // Add new
            m_mMessageIndex.Set(msg.messageId, m_aMessages.Count());
            m_aMessages.Insert(msg);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    AG0_TDLMessageClient GetByMessageId(int messageId)
    {
        if (!m_mMessageIndex.Contains(messageId))
            return null;
        return m_aMessages[m_mMessageIndex.Get(messageId)];
    }
    
    //------------------------------------------------------------------------------------------------
    // Get all messages for a conversation (sorted by timestamp)
    //------------------------------------------------------------------------------------------------
    array<ref AG0_TDLMessageClient> GetConversation(RplId viewerRplId, string conversationId)
    {
        array<ref AG0_TDLMessageClient> result = {};
        
        foreach (AG0_TDLMessageClient msg : m_aMessages)
        {
            if (msg.GetConversationId(viewerRplId) == conversationId)
                result.Insert(msg);
        }
        
        // Sort by timestamp (simple bubble sort - messages are usually small sets)
        for (int i = 0; i < result.Count() - 1; i++)
        {
            for (int j = 0; j < result.Count() - i - 1; j++)
            {
                if (result[j].timestamp > result[j + 1].timestamp)
                {
                    AG0_TDLMessageClient temp = result[j];
                    result[j] = result[j + 1];
                    result[j + 1] = temp;
                }
            }
        }
        
        return result;
    }
    
    //------------------------------------------------------------------------------------------------
    // Get network broadcast messages
    //------------------------------------------------------------------------------------------------
    array<ref AG0_TDLMessageClient> GetNetworkMessages()
    {
        array<ref AG0_TDLMessageClient> result = {};
        
        foreach (AG0_TDLMessageClient msg : m_aMessages)
        {
            if (msg.messageType == ETDLMessageType.NETWORK_BROADCAST)
                result.Insert(msg);
        }
        
        // Sort by timestamp
        for (int i = 0; i < result.Count() - 1; i++)
        {
            for (int j = 0; j < result.Count() - i - 1; j++)
            {
                if (result[j].timestamp > result[j + 1].timestamp)
                {
                    AG0_TDLMessageClient temp = result[j];
                    result[j] = result[j + 1];
                    result[j + 1] = temp;
                }
            }
        }
        
        return result;
    }
    
    //------------------------------------------------------------------------------------------------
    // Get direct messages with a specific contact
    //------------------------------------------------------------------------------------------------
    array<ref AG0_TDLMessageClient> GetDirectMessages(RplId viewerRplId, RplId contactRplId)
    {
        return GetConversation(viewerRplId, contactRplId.ToString());
    }
    
    //------------------------------------------------------------------------------------------------
    // Count unread messages (messages where viewer is recipient and hasn't marked read)
    // Note: "unread" tracking is client-side; server tracks "read" separately
    //------------------------------------------------------------------------------------------------
    int CountUnreadInConversation(RplId viewerRplId, string conversationId, set<int> readMessageIds)
    {
        int count = 0;
        foreach (AG0_TDLMessageClient msg : m_aMessages)
        {
            if (msg.GetConversationId(viewerRplId) != conversationId)
                continue;
            if (msg.IsOutgoing(viewerRplId))
                continue;  // Own messages don't count as unread
            if (readMessageIds.Contains(msg.messageId))
                continue;
            count++;
        }
        return count;
    }
    
    //------------------------------------------------------------------------------------------------
    // Get total unread count across all conversations
    //------------------------------------------------------------------------------------------------
    int CountTotalUnread(RplId viewerRplId, set<int> readMessageIds)
    {
        int count = 0;
        foreach (AG0_TDLMessageClient msg : m_aMessages)
        {
            if (msg.IsOutgoing(viewerRplId))
                continue;
            if (readMessageIds.Contains(msg.messageId))
                continue;
            count++;
        }
        return count;
    }
    
    //------------------------------------------------------------------------------------------------
    array<ref AG0_TDLMessageClient> GetAllMessages() { return m_aMessages; }
    int Count() { return m_aMessages.Count(); }
}