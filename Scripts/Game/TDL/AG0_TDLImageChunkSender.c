// AG0_TDLImageChunkSender.c — Server-side chunk dispatch for image delivery.
//
// Responsibilities:
//   * Take an already-fetched image payload (string, sitting in the photo manager's cache)
//     plus a recipient list (player IDs already resolved by the caller from network membership)
//     and fan out chunked RPCs to each recipient's player controller.
//   * Frame-spread chunk dispatch (one chunk per frame per transfer) so the server doesn't
//     stutter when distributing a large image to many recipients.
//   * Track per-transfer state, fire failure/cleanup on stall timeout, idempotent settlement.
//
// Lives inside AG0_TDLPhotoManager (server-side instance). Update() called from the manager's
// Update() per tick. Server-only — calling on a client is a no-op.
//
// LEAK-PREVENTION:
//   * CallLater payloads carry int transferId — a disposed transfer no-ops the next step.
//   * Per-transfer `m_bSettled` flag makes settlement idempotent.
//   * Recipient list is held by-value (array<int> owned by the transfer record), no back-refs.

//------------------------------------------------------------------------------------------------
//! Caller-supplied callback invoked exactly once per transfer (success OR failure).
//------------------------------------------------------------------------------------------------
class AG0_TDLImageTransferCallback
{
    void OnTransferComplete(int transferId, string deliveryId) {}
    void OnTransferFailed(int transferId, string deliveryId, string reason) {}
}

//------------------------------------------------------------------------------------------------
//! Per-transfer state record. Owned exclusively by AG0_TDLImageChunkSender.m_ActiveTransfers.
//------------------------------------------------------------------------------------------------
class AG0_TDLImageTransfer
{
    int    m_iTransferId;
    string m_sDeliveryId;
    string m_sFingerprint;
    int    m_iSizeBytes;
    int    m_iNetworkId;        // For client-side store lookup when chunks complete

    // Aliased reference to the cache entry's payload string. Enfusion ref-counts strings,
    // so we don't pin the cache entry here — if the cache evicts mid-transfer, the string
    // bytes survive as long as we hold this ref.
    string m_sPayload;

    int    m_iTotalChunks;
    int    m_iNextChunkIdx;       // index of next chunk to dispatch
    int    m_iLastProgressMs;     // for stall timeout
    int    m_iStartedAtMs;
    bool   m_bPendingFired;       // pending RPC sent to recipients yet?
    bool   m_bSettled;             // idempotent guard

    ref array<int> m_aRecipients; // player IDs (held by value)

    ref AG0_TDLImageTransferCallback m_CompletionCb;

    void AG0_TDLImageTransfer(int transferId, string deliveryId, string fingerprint,
                              int sizeBytes, int networkId, string payload, int totalChunks,
                              array<int> recipients)
    {
        m_iTransferId    = transferId;
        m_sDeliveryId    = deliveryId;
        m_sFingerprint   = fingerprint;
        m_iSizeBytes     = sizeBytes;
        m_iNetworkId     = networkId;
        m_sPayload       = payload;
        m_iTotalChunks   = totalChunks;
        m_iNextChunkIdx  = 0;
        m_bPendingFired  = false;
        m_bSettled       = false;
        int now = System.GetTickCount();
        m_iStartedAtMs   = now;
        m_iLastProgressMs = now;

        // Defensive copy of the recipient list — caller shouldn't be able to mutate ours.
        m_aRecipients = new array<int>();
        if (recipients)
        {
            foreach (int pid : recipients)
                m_aRecipients.Insert(pid);
        }
    }

    //! Releases all held refs. Idempotent.
    void Dispose()
    {
        m_sPayload = "";
        m_aRecipients = null;
        m_CompletionCb = null;
        m_bSettled = true;
    }
}

//------------------------------------------------------------------------------------------------
//! AG0_TDLImageChunkSender — server-side chunk dispatcher.
//------------------------------------------------------------------------------------------------
class AG0_TDLImageChunkSender
{
    // Chunk size — must stay under the per-string-param RPC cap (8191 bytes, silently truncated).
    // 6 KB leaves ~2 KB headroom for the other RPC params and packet overhead.
    protected const int CHUNK_SIZE_BYTES = 6 * 1024;

    // Caps and timeouts.
    protected const int MAX_CONCURRENT_TRANSFERS = 16;
    protected const int STALL_TIMEOUT_MS         = 30000;

    // Active transfers, keyed by transferId. Owns the AG0_TDLImageTransfer records.
    protected ref map<int, ref AG0_TDLImageTransfer> m_ActiveTransfers = new map<int, ref AG0_TDLImageTransfer>();
    protected int m_iNextTransferId = 1;

    //----------------------------------------------------------------
    // LIFECYCLE
    //----------------------------------------------------------------

    bool Initialize()
    {
        return true;
    }

    void Update(float timeSlice)
    {
        // Stall timeout sweep. Snapshot keys, then process — don't mutate during iteration.
        int now = System.GetTickCount();
        array<int> stale = {};
        foreach (int tid, AG0_TDLImageTransfer t : m_ActiveTransfers)
        {
            if (!t.m_bSettled && (now - t.m_iLastProgressMs > STALL_TIMEOUT_MS))
                stale.Insert(tid);
        }
        foreach (int sid : stale)
        {
            Print(string.Format("[TDL_PHOTO_SEND] tid=%1 stalled (no progress in %2ms)",
                sid, STALL_TIMEOUT_MS), LogLevel.WARNING);
            FailTransfer(sid, "stall_timeout");
        }
    }

    void ~AG0_TDLImageChunkSender()
    {
        // Settle any in-flight transfers as failures so callbacks don't fire after we vanish.
        array<int> ids = {};
        foreach (int tid, AG0_TDLImageTransfer t : m_ActiveTransfers)
            ids.Insert(tid);
        foreach (int sid : ids)
            FailTransfer(sid, "sender_shutdown");
        m_ActiveTransfers.Clear();
    }

    //----------------------------------------------------------------
    // PUBLIC API — called by AG0_TDLPhotoManager
    //----------------------------------------------------------------

    //! Begin a new transfer. The payload string MUST already be in the photo manager cache
    //! (or otherwise have a stable lifetime for the duration of distribution). Returns the
    //! new transferId, or -1 if the call was rejected (over capacity, no recipients, etc.).
    int BeginTransfer(string deliveryId, string payload, string fingerprint, int sizeBytes,
                      int networkId, array<int> recipientPlayerIds,
                      AG0_TDLImageTransferCallback completionCb)
    {
        if (!Replication.IsServer())
        {
            Print("[TDL_PHOTO_SEND] BeginTransfer called on client — ignored", LogLevel.WARNING);
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "client_no_send");
            return -1;
        }

        if (!recipientPlayerIds || recipientPlayerIds.Count() == 0)
        {
            Print("[TDL_PHOTO_SEND] BeginTransfer with empty recipient list — ignored", LogLevel.WARNING);
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "no_recipients");
            return -1;
        }

        if (m_ActiveTransfers.Count() >= MAX_CONCURRENT_TRANSFERS)
        {
            Print(string.Format("[TDL_PHOTO_SEND] At transfer capacity (%1) — refusing new",
                MAX_CONCURRENT_TRANSFERS), LogLevel.WARNING);
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "transfer_capacity");
            return -1;
        }

        int payloadLen = payload.Length();
        if (payloadLen <= 0)
        {
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "empty_payload");
            return -1;
        }

        // Compute total chunk count. Round up.
        int totalChunks = payloadLen / CHUNK_SIZE_BYTES;
        if ((payloadLen % CHUNK_SIZE_BYTES) != 0)
            totalChunks = totalChunks + 1;

        int transferId = m_iNextTransferId;
        m_iNextTransferId = m_iNextTransferId + 1;

        AG0_TDLImageTransfer t = new AG0_TDLImageTransfer(transferId, deliveryId, fingerprint,
            sizeBytes, networkId, payload, totalChunks, recipientPlayerIds);
        t.m_CompletionCb = completionCb;
        m_ActiveTransfers.Set(transferId, t);

        Print(string.Format("[TDL_PHOTO_SEND] tid=%1 BeginTransfer deliveryId=%2 payloadLen=%3 chunks=%4 recipients=%5",
            transferId, deliveryId, payloadLen, totalChunks, t.m_aRecipients.Count()), LogLevel.NORMAL);

        // Dispatch the entire transfer synchronously, matching the proven-working pattern
        // used by RpcDo_ReceiveTDLTerrainRoadsChunk / RpcDo_ReceiveTDLTerrainStructuresChunk:
        // tight for-loop fires all RPCs back-to-back in one tick. Frame-spread CallLater
        // chains were observed to drop chunks somewhere in the dispatch path.
        DispatchAll(transferId);

        return transferId;
    }

    //! Cancel an in-flight transfer. Notifies recipients with a delivery-failed RPC and disposes.
    //! Idempotent — calling on an already-settled transfer is a no-op.
    void CancelTransfer(int transferId, string reason)
    {
        if (reason.IsEmpty())
            reason = "cancelled";
        FailTransfer(transferId, reason);
    }

    int GetActiveTransferCount() { return m_ActiveTransfers.Count(); }

    //----------------------------------------------------------------
    // INTERNAL: lookup helper
    //----------------------------------------------------------------

    protected AG0_TDLImageTransfer GetActiveTransfer(int transferId)
    {
        if (!m_ActiveTransfers.Contains(transferId))
            return null;
        AG0_TDLImageTransfer t = m_ActiveTransfers.Get(transferId);
        if (!t || t.m_bSettled)
            return null;
        return t;
    }

    //----------------------------------------------------------------
    // INTERNAL: synchronous dispatch — fire all chunks back-to-back, then settle
    //
    // Mirrors AG0_TDLSystem.PushPlayerTerrainRoads / PushPlayerTerrainStructures
    // exactly: tight for-loop, single 4-param chunk RPC, no Pending / Finalize /
    // Failed bookkeeping RPCs. Reassembler keys on deliveryId, allocates on first
    // chunk, auto-finalizes when iReceived == totalChunks.
    //----------------------------------------------------------------

    protected void DispatchAll(int transferId)
    {
        AG0_TDLImageTransfer t = GetActiveTransfer(transferId);
        if (!t)
            return;

        PlayerManager pm = GetGame().GetPlayerManager();
        if (!pm)
        {
            FailTransfer(transferId, "no_player_manager");
            return;
        }

        int payloadLen = t.m_sPayload.Length();
        for (int idx = 0; idx < t.m_iTotalChunks; idx = idx + 1)
        {
            int start = idx * CHUNK_SIZE_BYTES;
            int len = CHUNK_SIZE_BYTES;
            if (start + len > payloadLen)
                len = payloadLen - start;

            string chunkData = t.m_sPayload.Substring(start, len);

            int sentTo = 0;
            foreach (int playerId : t.m_aRecipients)
            {
                SCR_PlayerController pc = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
                if (!pc)
                    continue;
                // 4-param shape, identical to ReceiveTDLTerrainStructuresChunk:
                // (string deliveryId, int totalChunks, int chunkIndex, string chunkData)
                pc.ReceiveImageChunk(t.m_sDeliveryId, t.m_iTotalChunks, idx, chunkData);
                sentTo = sentTo + 1;
            }

            if ((idx % 16) == 0 || idx == t.m_iTotalChunks - 1)
            {
                Print(string.Format("[TDL_PHOTO_SEND] tid=%1 chunk %2/%3 (%4 bytes) → %5 recipients",
                    transferId, idx + 1, t.m_iTotalChunks, len, sentTo), LogLevel.NORMAL);
            }

            t.m_iNextChunkIdx   = idx + 1;
        }
        t.m_iLastProgressMs = System.GetTickCount();

        Print(string.Format("[TDL_PHOTO_SEND] tid=%1 all chunks fired (%2 chunks, %3 bytes, %4 recipients)",
            transferId, t.m_iTotalChunks, payloadLen, t.m_aRecipients.Count()), LogLevel.NORMAL);

        SettleSuccess(transferId);
    }

    //----------------------------------------------------------------
    // INTERNAL: settlement
    //----------------------------------------------------------------

    protected void SettleSuccess(int transferId)
    {
        AG0_TDLImageTransfer t = GetActiveTransfer(transferId);
        if (!t)
            return;

        AG0_TDLImageTransferCallback cb = t.m_CompletionCb;
        string deliveryId = t.m_sDeliveryId;
        t.Dispose();
        m_ActiveTransfers.Remove(transferId);

        if (cb)
            cb.OnTransferComplete(transferId, deliveryId);
    }

    //! Fail a transfer. Disposes; the client will surface stall-timeout in its reassembler
    //! if it had buffered some chunks. No Failed RPC is sent (we no longer have one — the
    //! 4-param chunk shape is the only RPC, mirroring the proven terrain pattern).
    protected void FailTransfer(int transferId, string reason)
    {
        AG0_TDLImageTransfer t = GetActiveTransfer(transferId);
        if (!t)
            return;

        Print(string.Format("[TDL_PHOTO_SEND] tid=%1 failing: %2", transferId, reason), LogLevel.WARNING);

        AG0_TDLImageTransferCallback cb = t.m_CompletionCb;
        string deliveryId = t.m_sDeliveryId;
        t.Dispose();
        m_ActiveTransfers.Remove(transferId);

        if (cb)
            cb.OnTransferFailed(transferId, deliveryId, reason);
    }
}
