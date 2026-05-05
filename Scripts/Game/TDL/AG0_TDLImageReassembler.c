// AG0_TDLImageReassembler.c — Client-side chunk reassembly for image delivery.
//
// Mirrors the proven-working terrain chunk reassembly contract (see
// RpcDo_ReceiveTDLTerrainStructuresChunk in AG0_PlayerController_TDL.c):
//   * Single 4-param chunk RPC (string deliveryId, int totalChunks, int chunkIndex, string chunkData)
//   * Allocate per-deliveryId state on first chunk seen.
//   * Tolerate out-of-order arrivals + dedupe duplicates by index.
//   * Auto-finalize once iReceived == totalChunks — no separate Finalize / Pending / Failed RPCs.
//
// Lives inside AG0_TDLPhotoManager (client-side instance). Update() called from the manager's
// Update() per tick — drives stall-timeout sweep.
//
// LEAK-PREVENTION:
//   * No back-refs from reassembly records to the reassembler.
//   * Idempotent settlement (m_bSettled flag).
//   * Per-deliveryId Dispose() nulls all slots; map removal releases the record.

//------------------------------------------------------------------------------------------------
//! Per-delivery reassembly state.
//------------------------------------------------------------------------------------------------
class AG0_TDLImageReassembly
{
    string m_sDeliveryId;
    int    m_iExpectedTotal;
    int    m_iReceived;
    int    m_iLastChunkMs;
    int    m_iStartedAtMs;
    bool   m_bSettled;

    // Slot array of chunk strings, sized to m_iExpectedTotal. Null entries mean "not yet received".
    ref array<string> m_aChunks;
    // Mirror bool array for duplicate detection without string-equality cost.
    ref array<bool>   m_aHaveChunk;

    void AG0_TDLImageReassembly(string deliveryId, int expectedTotal)
    {
        m_sDeliveryId    = deliveryId;
        m_iExpectedTotal = expectedTotal;
        m_iReceived      = 0;
        m_bSettled       = false;
        int now = System.GetTickCount();
        m_iStartedAtMs   = now;
        m_iLastChunkMs   = now;

        m_aChunks = new array<string>();
        m_aChunks.Resize(expectedTotal);
        m_aHaveChunk = new array<bool>();
        m_aHaveChunk.Resize(expectedTotal);
    }

    //! Releases all chunk strings + slot arrays. Idempotent.
    void Dispose()
    {
        m_aChunks    = null;
        m_aHaveChunk = null;
        m_bSettled   = true;
    }

    //! True iff all expected chunks have been received.
    bool IsComplete()
    {
        return m_iReceived >= m_iExpectedTotal;
    }
}

//------------------------------------------------------------------------------------------------
//! AG0_TDLImageReassembler — client-side chunk accumulator.
//!
//! Single entry point (OnChunk) called from RpcDo_ReceiveImageChunk on AG0_PlayerController_TDL
//! via AG0_TDLPhotoManager.HandleChunkData. Auto-allocates on first chunk, auto-finalizes when
//! complete.
//------------------------------------------------------------------------------------------------
class AG0_TDLImageReassembler
{
    protected const int MAX_CONCURRENT_INBOUND = 16;
    protected const int STALL_TIMEOUT_MS       = 30000;

    // Keyed by deliveryId — image bytes are uniquely identified by the API's deliveryId
    // string, which is also what the replicated AG0_TDLMessageClient carries.
    protected ref map<string, ref AG0_TDLImageReassembly> m_Inbound = new map<string, ref AG0_TDLImageReassembly>();

    //----------------------------------------------------------------
    // LIFECYCLE
    //----------------------------------------------------------------

    bool Initialize()
    {
        return true;
    }

    void Update(float timeSlice)
    {
        // Stall sweep: drop any reassembly that hasn't received a chunk in a while.
        int now = System.GetTickCount();
        array<string> stale = {};
        foreach (string did, AG0_TDLImageReassembly r : m_Inbound)
        {
            if (!r.m_bSettled && (now - r.m_iLastChunkMs > STALL_TIMEOUT_MS))
                stale.Insert(did);
        }
        foreach (string sid : stale)
        {
            Print(string.Format("[TDL_PHOTO_RECV] deliveryId=%1 stalled (no chunk in %2ms)",
                sid, STALL_TIMEOUT_MS), LogLevel.WARNING);
            FailReassembly(sid, "stall_timeout");
        }
    }

    void ~AG0_TDLImageReassembler()
    {
        // Dispose any in-flight reassemblies; no callbacks fire from the dtor.
        foreach (string did, AG0_TDLImageReassembly r : m_Inbound)
        {
            if (r) r.Dispose();
        }
        m_Inbound.Clear();
    }

    //----------------------------------------------------------------
    // PUBLIC API — invoked from RpcDo_ReceiveImageChunk on AG0_PlayerController_TDL
    //----------------------------------------------------------------

    //! Receive one chunk. Allocates reassembly state on the first chunk for a deliveryId.
    //! Auto-finalizes when iReceived == expectedTotal.
    //! Tolerates out-of-order arrivals and dedupes duplicates by chunkIndex.
    void OnChunk(string deliveryId, int totalChunks, int chunkIndex, string chunkData,
                 AG0_TDLPhotoManager mgr)
    {
        if (deliveryId.IsEmpty())
            return;
        if (totalChunks <= 0)
            return;

        AG0_TDLImageReassembly r;
        if (m_Inbound.Contains(deliveryId))
        {
            r = m_Inbound.Get(deliveryId);
            if (r && r.m_bSettled)
                return;

            // Server resent with a different total → start fresh.
            if (r.m_iExpectedTotal != totalChunks)
            {
                r.Dispose();
                m_Inbound.Remove(deliveryId);
                r = null;
            }
        }

        if (!r)
        {
            if (m_Inbound.Count() >= MAX_CONCURRENT_INBOUND)
            {
                Print(string.Format("[TDL_PHOTO_RECV] At inbound capacity (%1) — dropping deliveryId=%2",
                    MAX_CONCURRENT_INBOUND, deliveryId), LogLevel.WARNING);
                return;
            }
            r = new AG0_TDLImageReassembly(deliveryId, totalChunks);
            m_Inbound.Set(deliveryId, r);
            Print(string.Format("[TDL_PHOTO_RECV] deliveryId=%1 first chunk → allocating buffer (chunks=%2)",
                deliveryId, totalChunks), LogLevel.NORMAL);
        }

        if (chunkIndex < 0 || chunkIndex >= r.m_iExpectedTotal)
        {
            Print(string.Format("[TDL_PHOTO_RECV] deliveryId=%1 chunk %2 out of range (total=%3)",
                deliveryId, chunkIndex, r.m_iExpectedTotal), LogLevel.WARNING);
            return;
        }

        if (r.m_aHaveChunk[chunkIndex])
            return;  // duplicate — ignore silently

        r.m_aChunks[chunkIndex]    = chunkData;
        r.m_aHaveChunk[chunkIndex] = true;
        r.m_iReceived              = r.m_iReceived + 1;
        r.m_iLastChunkMs           = System.GetTickCount();

        if ((chunkIndex % 16) == 0 || chunkIndex == r.m_iExpectedTotal - 1)
        {
            Print(string.Format("[TDL_PHOTO_RECV] deliveryId=%1 chunk %2/%3 received",
                deliveryId, r.m_iReceived, r.m_iExpectedTotal), LogLevel.NORMAL);
        }

        // Auto-finalize when complete. Same pattern as the terrain chunk path.
        if (r.IsComplete())
        {
            // Concatenate. Enfusion strings are immutable so we build via repeated append —
            // the engine handles this efficiently up to a point.
            string body = "";
            for (int i = 0; i < r.m_iExpectedTotal; i = i + 1)
            {
                body = body + r.m_aChunks[i];
            }

            Print(string.Format("[TDL_PHOTO_RECV] deliveryId=%1 concat done: %2 bytes — handing to decode",
                deliveryId, body.Length()), LogLevel.NORMAL);

            // Settle the reassembly side first — once we've handed the JSON to the decoder,
            // we don't need the slot array anymore.
            r.Dispose();
            m_Inbound.Remove(deliveryId);

            // Hand off to the manager's decode pipeline. The manager owns the post-decode
            // hookup (decoded-photo cache + message-store update). NetworkId is looked up
            // inside the manager from the local message store.
            if (mgr)
                mgr.OnReassemblyComplete(deliveryId, body);
        }
    }

    int GetActiveInboundCount() { return m_Inbound.Count(); }

    //----------------------------------------------------------------
    // INTERNAL: failure path
    //----------------------------------------------------------------

    protected void FailReassembly(string deliveryId, string reason)
    {
        if (!m_Inbound.Contains(deliveryId))
            return;
        AG0_TDLImageReassembly r = m_Inbound.Get(deliveryId);
        if (!r || r.m_bSettled)
            return;

        Print(string.Format("[TDL_PHOTO_RECV] deliveryId=%1 failing: %2", deliveryId, reason), LogLevel.WARNING);

        r.Dispose();
        m_Inbound.Remove(deliveryId);
    }
}
