// AG0_TDLPhotoManager.c — TDL photo (image) lifecycle manager
//
// Owns:
//   * rgz / r / d payload cache (byte-bounded LRU + age sweep)
//   * In-flight HTTP fetch requests (server-only — REST is server-only per project constraint)
//   * Per-request decode pipeline state (base64 → gunzip → rect parse), CallLater-driven
//   * Idempotent settlement (success/failure exactly once per request)
//
// Lives on both server and client as a sibling of AG0_TDLApiManager on AG0_TDLSystem.
// Server-only operations (HTTP fetch) are gated internally; the cache and decode pipeline
// run wherever a payload is handed in.
//
// PHASE 1 SCOPE: cache + sweep + server-side fetch + decode chain. Chunk distribution
// to clients over RPC will be added in phase 2 (see AG0_TDLImageChunkSender / Reassembler).
//
// LEAK-PREVENTION DESIGN NOTES:
//   * CallLater payloads carry int requestIds, NEVER refs. A disposed request is
//     looked up and the step no-ops — Disposed transfers naturally drop from the
//     scheduling chain within one tick.
//   * Per-request state lives in AG0_TDLPhotoFetchRequest, owned exclusively by
//     m_PendingFetches. Callbacks hold int ids only, no back-refs to manager.
//   * Idempotent settlement (SettleSuccess / SettleFailure both early-return on
//     m_bSettled), so a late REST callback after a timeout is a no-op.
//   * Periodic sweep (1Hz) times out stuck fetches and ages out cache entries.

//------------------------------------------------------------------------------------------------
//! Per-deliveryId cache entry — server-side rgz cache, byte-bounded LRU + age sweep.
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoCacheEntry
{
    string m_sDeliveryId;
    string m_sPayload;          // The rgz / r / d body (whatever the API returned)
    string m_sFieldKind;        // "rgz" | "r" | "d" — wire format of m_sPayload
    string m_sFingerprint;
    int    m_iSizeBytes;
    int    m_iLastAccessMs;
    int    m_iInsertedAtMs;

    void AG0_TDLPhotoCacheEntry(string deliveryId, string payload, string fieldKind,
                                string fingerprint, int sizeBytes)
    {
        m_sDeliveryId   = deliveryId;
        m_sPayload      = payload;
        m_sFieldKind    = fieldKind;
        m_sFingerprint  = fingerprint;
        m_iSizeBytes    = sizeBytes;
        int now = System.GetTickCount();
        m_iLastAccessMs = now;
        m_iInsertedAtMs = now;
    }
}

//------------------------------------------------------------------------------------------------
//! Caller-supplied completion callback. Subclass and pass to FetchImage / DecodePhotoFromJson.
//! Either OnPhotoReady or OnPhotoFailed will fire exactly once per request.
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoFetchCallback
{
    void OnPhotoReady(int requestId, AG0_TDLPhotoData photo) {}
    void OnPhotoFailed(int requestId, string reason) {}
}

//------------------------------------------------------------------------------------------------
//! Reassembly→decode bridge. Forwarded to the decode pipeline by AG0_TDLPhotoManager
//! after a chunked transfer is fully reassembled. Holds deliveryId/networkId only —
//! looks up the manager via AG0_TDLPhotoManager.GetActiveInstance() so there's no
//! back-ref cycle. The static lookup tries the server-side AG0_TDLSystem first, then
//! falls back to the local player controller's per-PC manager (client side).
//------------------------------------------------------------------------------------------------
class AG0_TDLDecodedPhotoSink : AG0_TDLPhotoFetchCallback
{
    protected string m_sDeliveryId;
    protected int    m_iNetworkId;

    void AG0_TDLDecodedPhotoSink(string deliveryId, int networkId)
    {
        m_sDeliveryId = deliveryId;
        m_iNetworkId  = networkId;
    }

    override void OnPhotoReady(int requestId, AG0_TDLPhotoData photo)
    {
        AG0_TDLPhotoManager mgr = AG0_TDLPhotoManager.GetActiveInstance();
        if (!mgr) return;
        mgr.OnDecodedPhotoReady(m_sDeliveryId, m_iNetworkId, photo);
    }

    override void OnPhotoFailed(int requestId, string reason)
    {
        AG0_TDLPhotoManager mgr = AG0_TDLPhotoManager.GetActiveInstance();
        if (!mgr) return;
        mgr.OnDecodedPhotoFailed(m_sDeliveryId, m_iNetworkId, reason);
    }
}

//------------------------------------------------------------------------------------------------
//! Per-request state. Owned by AG0_TDLPhotoManager.m_PendingFetches; never referenced
//! by ref from anywhere else (callbacks and CallLater payloads carry int requestIds).
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoFetchRequest
{
    int    m_iRequestId;
    string m_sUrl;                  // For logging only
    string m_sDeliveryId;           // Empty for ad-hoc URLs
    string m_sFingerprint;          // Set when fetch is deliveryId-keyed (used at cache-population time)
    int    m_iSizeBytes;            // From queue item; used for cache entry sizing
    int    m_iStartedAtMs;
    bool   m_bSettled;              // Idempotent settlement guard

    ref AG0_TDLImageCallback      m_RestCallback;     // The HTTP callback (lifetime tied here)
    ref AG0_TDLPhotoFetchCallback m_CompletionCb;     // Caller-supplied; invoked at most once

    // --- Decode pipeline state (per-request, advanced by CallLater(StepBase64, ...)) ---
    ref AG0_TDLPhotoData     m_PendingPhoto;
    string                   m_sPendingPayload;
    string                   m_sPendingFieldKind;     // "rgz" | "r" | "d"
    ref AG0_TDLBase64Decoder m_PendingB64;
    ref array<int>           m_aPendingBytes;         // base64 output → gunzip input
    int                      m_iPendingT0;
    int                      m_iPendingTBatch;

    void AG0_TDLPhotoFetchRequest(int requestId, string url, string deliveryId)
    {
        m_iRequestId    = requestId;
        m_sUrl          = url;
        m_sDeliveryId   = deliveryId;
        m_iStartedAtMs  = System.GetTickCount();
        m_bSettled      = false;
    }

    //! Releases all refs. Idempotent. Called from settlement paths and the manager dtor.
    void Dispose()
    {
        m_RestCallback      = null;
        m_CompletionCb      = null;
        m_PendingPhoto      = null;
        m_sPendingPayload   = "";
        m_sPendingFieldKind = "";
        m_PendingB64        = null;
        m_aPendingBytes     = null;
        m_bSettled          = true;
    }
}

//------------------------------------------------------------------------------------------------
//! REST callback for image fetches. Holds an int requestId only (no manager back-ref) so
//! a callback firing after manager shutdown / request timeout finds nothing and no-ops.
//!
//! Uses the SetOnSuccess/SetOnError handler-registration pattern (matching
//! AG0_TDLApiQueueCallback in AG0_TDLAPIConfig.c). The handler-style signatures take a
//! `RestCallback cb` and pull data / http codes off it via cb.GetData() / cb.GetHttpCode().
//! The override-OnSuccess(string,int) shape is a fallback path in this Enfusion version
//! that fires "Function was not set" warnings and surfaces engine-internal error codes
//! instead of real HTTP codes.
//------------------------------------------------------------------------------------------------
class AG0_TDLImageCallback : RestCallback
{
    protected int m_iRequestId;

    void AG0_TDLImageCallback(int requestId)
    {
        m_iRequestId = requestId;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }

    void OnSuccessHandler(RestCallback cb)
    {
        string data = cb.GetData();
        // REST callbacks always fire server-side (only the server makes API calls).
        AG0_TDLPhotoManager mgr = AG0_TDLPhotoManager.GetActiveInstance();
        if (!mgr) return;
        Print(string.Format("[TDL_PHOTO] rid=%1 REST OnSuccess (%2 bytes)",
            m_iRequestId, data.Length()), LogLevel.NORMAL);
        mgr.OnFetchSuccess(m_iRequestId, data);
    }

    void OnErrorHandler(RestCallback cb)
    {
        AG0_TDLPhotoManager mgr = AG0_TDLPhotoManager.GetActiveInstance();
        if (!mgr) return;

        // Distinguish timeout from HTTP-status errors so failure event reasons are accurate.
        if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
        {
            Print(string.Format("[TDL_PHOTO] rid=%1 REST timeout", m_iRequestId), LogLevel.ERROR);
            mgr.OnFetchFailure(m_iRequestId, "http_timeout");
            return;
        }

        int httpCode = cb.GetHttpCode();
        Print(string.Format("[TDL_PHOTO] rid=%1 REST error httpCode=%2", m_iRequestId, httpCode), LogLevel.ERROR);
        mgr.OnFetchFailure(m_iRequestId, string.Format("http_error_%1", httpCode));
    }
}

//------------------------------------------------------------------------------------------------
//! AG0_TDLPhotoManager — sibling to AG0_TDLApiManager on AG0_TDLSystem.
//! Runs on both server and client. Server-only methods are internally gated.
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoManager
{
    // --- API ---
    protected static const string API_BASE = "https://tdl.blufor.info/api/image";

    // --- Cache (server-side authoritative; clients receive decoded photos via RPC in phase 2) ---
    protected ref map<string, ref AG0_TDLPhotoCacheEntry> m_Cache = new map<string, ref AG0_TDLPhotoCacheEntry>();
    protected int m_iCacheTotalBytes      = 0;
    protected int m_iCacheCapBytes        = 50 * 1024 * 1024;   // 50 MB hard cap
    protected int m_iCacheEntryMaxAgeMs   = 30 * 60 * 1000;     // 30 min age-out
    protected int m_iCacheMaxEntries      = 500;                // entry-count cap

    // --- Pending fetches ---
    protected ref map<int, ref AG0_TDLPhotoFetchRequest> m_PendingFetches = new map<int, ref AG0_TDLPhotoFetchRequest>();
    protected int m_iNextRequestId        = 1;
    protected int m_iFetchTimeoutMs       = 60 * 1000;          // 60s per-request timeout
    protected int m_iMaxConcurrentFetches = 32;                 // back-pressure cap

    // --- Periodic sweep ---
    protected float m_fTimeSinceSweep     = 0;
    protected const float SWEEP_INTERVAL_SEC = 1.0;

    // --- Decode tunables ---
    //! ms of wall-clock time the resumable base64 decoder may consume per frame.
    //! 8ms ≈ half a 60fps frame — safe default. 4 = buttery. 12 = fast-but-hitchy.
    protected int m_iB64MsPerFrame        = 8;

    //! Diagnostic flag — when true, periodic sweep prints inflight/cache counters.
    protected bool m_bDebugMemory         = false;

    //----------------------------------------------------------------
    // PHASE 2: chunk distribution (server) + reassembly (client)
    //----------------------------------------------------------------

    //! Server-side chunk dispatcher. Owned here, lazily instantiated.
    protected ref AG0_TDLImageChunkSender m_ChunkSender;

    //! Client-side chunk reassembler. Owned here.
    protected ref AG0_TDLImageReassembler m_Reassembler;

    //! Client-side decoded-photo cache, keyed by deliveryId. Populated when a reassembly
    //! decode completes; consumed by message-list UI when rendering image-messages.
    protected ref map<string, ref AG0_TDLPhotoData> m_DecodedPhotos = new map<string, ref AG0_TDLPhotoData>();
    protected int m_iMaxDecodedPhotos = 200;

    //! Fired when a decoded photo lands in m_DecodedPhotos. UI subscribes to refresh
    //! image-message widgets that were showing a "transferring" placeholder.
    //! Signature: (string deliveryId, int networkId)
    protected ref ScriptInvoker m_OnDecodedPhotoArrived = new ScriptInvoker();

    //! Fired when an image transfer fails (decode error, missing chunks, etc.). UI
    //! subscribes to flip the message to a failure state.
    //! Signature: (string deliveryId, int networkId)
    protected ref ScriptInvoker m_OnDecodedPhotoFailed = new ScriptInvoker();

    ScriptInvoker GetOnDecodedPhotoArrived() { return m_OnDecodedPhotoArrived; }
    ScriptInvoker GetOnDecodedPhotoFailed()  { return m_OnDecodedPhotoFailed; }

    //----------------------------------------------------------------
    // LIFECYCLE
    //----------------------------------------------------------------

    bool Initialize()
    {
        // Both sides get a reassembler (cheap; clients use it to receive, server to no-op).
        m_Reassembler = new AG0_TDLImageReassembler();
        m_Reassembler.Initialize();

        // Server-only: instantiate the chunk sender. Clients never originate transfers.
        if (Replication.IsServer())
        {
            m_ChunkSender = new AG0_TDLImageChunkSender();
            m_ChunkSender.Initialize();
        }

        Print("[TDL_PHOTO] AG0_TDLPhotoManager initialized", LogLevel.NORMAL);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Static lookup for callbacks / sinks that don't have a back-reference. Tries the
    //! server-side AG0_TDLSystem first (it owns the server's outbound fetch + chunk sender);
    //! if that's not present (remote client — AG0_TDLSystem is server-only), falls back to
    //! the local player controller's per-PC photo manager.
    //!
    //! This is the only correct way for AG0_TDLDecodedPhotoSink / AG0_TDLImageCallback to
    //! reach a manager from inside their callback methods. AG0_TDLSystem.GetInstance() alone
    //! returns null on remote clients and was the cause of every "[image — incoming…]"
    //! failure to render in MP.
    static AG0_TDLPhotoManager GetActiveInstance()
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (system)
        {
            AG0_TDLPhotoManager m = system.GetPhotoManager();
            if (m) return m;
        }

        // Client-side fallback: per-PC photo manager owned by the local player controller.
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc) return null;
        SCR_PlayerController tdlPc = SCR_PlayerController.Cast(pc);
        if (!tdlPc) return null;
        return tdlPc.GetOrCreatePhotoManager();
    }

    void Update(float timeSlice)
    {
        m_fTimeSinceSweep += timeSlice;
        if (m_fTimeSinceSweep >= SWEEP_INTERVAL_SEC)
        {
            Sweep();
            m_fTimeSinceSweep = 0;
        }

        if (m_ChunkSender)
            m_ChunkSender.Update(timeSlice);
        if (m_Reassembler)
            m_Reassembler.Update(timeSlice);
    }

    void ~AG0_TDLPhotoManager()
    {
        // Settle any in-flight requests as failures so callbacks don't fire after we vanish.
        // Snapshot ids first so we don't mutate the map during iteration.
        array<int> ids = {};
        foreach (int rid, AG0_TDLPhotoFetchRequest req : m_PendingFetches)
            ids.Insert(rid);
        foreach (int rid : ids)
            SettleFailure(rid, "manager_shutdown");

        m_Cache.Clear();
        m_PendingFetches.Clear();
        m_iCacheTotalBytes = 0;

        // Phase 2: tear down chunk sender + reassembler (their dtors handle in-flight settlement).
        m_ChunkSender = null;
        m_Reassembler = null;
        if (m_DecodedPhotos)
            m_DecodedPhotos.Clear();
    }

    //----------------------------------------------------------------
    // PUBLIC FETCH API
    //----------------------------------------------------------------

    //! Server-only. Issues an HTTP fetch through the existing /api/image proxy endpoint.
    //! Returns the new requestId, or -1 if the call was rejected (client-side, over capacity).
    //! The completionCb fires exactly once on the originating thread (Enfusion main thread).
    int FetchImage(string imageUrl, int size, int colors, AG0_TDLPhotoFetchCallback completionCb)
    {
        if (!Replication.IsServer())
        {
            Print("[TDL_PHOTO] FetchImage called on client — ignored (REST is server-only)", LogLevel.WARNING);
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "client_no_rest");
            return -1;
        }

        if (m_PendingFetches.Count() >= m_iMaxConcurrentFetches)
        {
            Print(string.Format("[TDL_PHOTO] At fetch capacity (%1) — refusing new fetch", m_iMaxConcurrentFetches), LogLevel.WARNING);
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "fetch_capacity");
            return -1;
        }

        int requestId = m_iNextRequestId;
        m_iNextRequestId = m_iNextRequestId + 1;

        string fullUrl = string.Format("%1?url=%2&s=%3&c=%4", API_BASE, imageUrl, size, colors);

        AG0_TDLPhotoFetchRequest req = new AG0_TDLPhotoFetchRequest(requestId, fullUrl, "");
        req.m_CompletionCb = completionCb;
        req.m_RestCallback = new AG0_TDLImageCallback(requestId);

        m_PendingFetches.Set(requestId, req);

        Print(string.Format("[TDL_PHOTO] rid=%1 FetchImage url=%2", requestId, fullUrl), LogLevel.NORMAL);

        RestContext ctx = GetGame().GetRestApi().GetContext(fullUrl);
        if (ctx)
            ApplyAuthHeader(ctx);
        ctx.GET(req.m_RestCallback, "");

        return requestId;
    }

    //! Server-only. Hits the /api/image/test endpoint (palette-quantized synthetic test image).
    int FetchTestImage(int size, int colors, AG0_TDLPhotoFetchCallback completionCb)
    {
        if (!Replication.IsServer())
        {
            Print("[TDL_PHOTO] FetchTestImage called on client — ignored", LogLevel.WARNING);
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "client_no_rest");
            return -1;
        }

        if (m_PendingFetches.Count() >= m_iMaxConcurrentFetches)
        {
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "fetch_capacity");
            return -1;
        }

        int requestId = m_iNextRequestId;
        m_iNextRequestId = m_iNextRequestId + 1;

        string fullUrl = string.Format("%1/test?s=%2&c=%3", API_BASE, size, colors);

        AG0_TDLPhotoFetchRequest req = new AG0_TDLPhotoFetchRequest(requestId, fullUrl, "");
        req.m_CompletionCb = completionCb;
        req.m_RestCallback = new AG0_TDLImageCallback(requestId);

        m_PendingFetches.Set(requestId, req);

        Print(string.Format("[TDL_PHOTO] rid=%1 FetchTestImage url=%2", requestId, fullUrl), LogLevel.NORMAL);

        RestContext ctx = GetGame().GetRestApi().GetContext(fullUrl);
        if (ctx)
            ApplyAuthHeader(ctx);
        ctx.GET(req.m_RestCallback, "");

        return requestId;
    }

    //! Decode a server JSON response into an AG0_TDLPhotoData without going to the network.
    //! Useful for offline repro (paste a curl-captured response into a test action) and for
    //! phase-2 chunk reassembly (the reassembler will hand the rebuilt JSON to this method).
    int DecodePhotoFromJson(string jsonData, AG0_TDLPhotoFetchCallback completionCb)
    {
        if (m_PendingFetches.Count() >= m_iMaxConcurrentFetches)
        {
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "fetch_capacity");
            return -1;
        }

        int requestId = m_iNextRequestId;
        m_iNextRequestId = m_iNextRequestId + 1;

        AG0_TDLPhotoFetchRequest req = new AG0_TDLPhotoFetchRequest(requestId, "<inline>", "");
        req.m_CompletionCb = completionCb;
        m_PendingFetches.Set(requestId, req);

        Print(string.Format("[TDL_PHOTO] rid=%1 DecodePhotoFromJson len=%2", requestId, jsonData.Length()), LogLevel.NORMAL);
        BeginDecode(requestId, jsonData);
        return requestId;
    }

    //----------------------------------------------------------------
    // PHASE 2: deliveryId-keyed fetch + chunk distribution + reassembly hookup
    //----------------------------------------------------------------

    //! Server-only. Fetches the API's pre-rendered image at a deliveryId-scoped URL (the
    //! `fetchUrl` field from an `image_deliver` queue item). On success, the raw JSON
    //! response body is cached under `deliveryId` so the chunk sender can read from it,
    //! and `completionCb.OnPhotoReady` fires with a decoded AG0_TDLPhotoData (so the server
    //! has a local view if it ever needs one — primarily the call sequence for callers is
    //! "fetch → on success kick BeginDistribute").
    int FetchByDeliveryId(string deliveryId, string fetchUrl, string fingerprint, int sizeBytes,
                          AG0_TDLPhotoFetchCallback completionCb)
    {
        if (!Replication.IsServer())
        {
            Print("[TDL_PHOTO] FetchByDeliveryId called on client — ignored (REST is server-only)", LogLevel.WARNING);
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "client_no_rest");
            return -1;
        }

        if (deliveryId.IsEmpty() || fetchUrl.IsEmpty())
        {
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "missing_delivery_id_or_url");
            return -1;
        }

        if (m_PendingFetches.Count() >= m_iMaxConcurrentFetches)
        {
            if (completionCb)
                completionCb.OnPhotoFailed(-1, "fetch_capacity");
            return -1;
        }

        int requestId = m_iNextRequestId;
        m_iNextRequestId = m_iNextRequestId + 1;

        AG0_TDLPhotoFetchRequest req = new AG0_TDLPhotoFetchRequest(requestId, fetchUrl, deliveryId);
        req.m_sFingerprint = fingerprint;
        req.m_iSizeBytes   = sizeBytes;
        req.m_CompletionCb = completionCb;
        req.m_RestCallback = new AG0_TDLImageCallback(requestId);

        m_PendingFetches.Set(requestId, req);

        Print(string.Format("[TDL_PHOTO] rid=%1 FetchByDeliveryId deliveryId=%2 url=%3",
            requestId, deliveryId, fetchUrl), LogLevel.NORMAL);

        // Use API_BASE as the context base + /<deliveryId> as the relative path (matches
        // the existing AG0_TDLApiManager pattern). Passing the full fetchUrl as the base
        // with empty path causes Enfusion's RestContext.SetHeaders to silently no-op,
        // and the request goes out without auth → API returns 401 → callback fires error
        // code 15. The queue's `fetchUrl` field is preserved for logging only; the dev /
        // prod environment selection happens via the API_BASE constant.
        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE);
        if (!ctx)
        {
            SettleFailure(requestId, "rest_context_null");
            return requestId;
        }
        ApplyAuthHeader(ctx);

        string relPath = string.Format("/%1", deliveryId);
        ctx.GET(req.m_RestCallback, relPath);

        return requestId;
    }

    //----------------------------------------------------------------
    // INTERNAL: auth helper
    //----------------------------------------------------------------

    //! Stamp the Bearer token onto a RestContext so the API recognizes the request.
    //! Pulled from AG0_TDLApiManager's stored config — same key the existing
    //! /api/mod/submit and /api/mod/queue calls use. If the api manager isn't
    //! initialized (e.g. running on client, or pre-init), we leave the headers
    //! empty and let the request 401 — failure path handles it gracefully.
    protected void ApplyAuthHeader(RestContext ctx)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system) return;
        AG0_TDLApiManager api = system.GetApiManager();
        if (!api) return;
        string key = api.GetApiKey();
        if (key.IsEmpty()) return;
        string headers = string.Format("Authorization,Bearer %1,Content-Type,application/json", key);
        ctx.SetHeaders(headers);
    }

    //! Server-only. Begin chunked distribution of a previously-cached image to recipients.
    //! `deliveryId` must already be in `m_Cache` (typically populated via FetchByDeliveryId).
    //! `recipientPlayerIds` is the resolved list of online TDL network members. The caller
    //! is responsible for resolving network membership — this method just dispatches.
    //! Returns the new transferId, or -1 if rejected.
    int BeginDistribute(string deliveryId, int networkId, array<int> recipientPlayerIds,
                        AG0_TDLImageTransferCallback completionCb)
    {
        if (!Replication.IsServer())
        {
            Print("[TDL_PHOTO] BeginDistribute called on client — ignored", LogLevel.WARNING);
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "client_no_send");
            return -1;
        }

        if (!m_ChunkSender)
        {
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "no_chunk_sender");
            return -1;
        }

        AG0_TDLPhotoCacheEntry entry = GetCacheEntry(deliveryId);
        if (!entry)
        {
            Print(string.Format("[TDL_PHOTO] BeginDistribute: deliveryId=%1 not in cache", deliveryId), LogLevel.WARNING);
            if (completionCb)
                completionCb.OnTransferFailed(-1, deliveryId, "cache_miss");
            return -1;
        }

        return m_ChunkSender.BeginTransfer(deliveryId, entry.m_sPayload, entry.m_sFingerprint,
            entry.m_iSizeBytes, networkId, recipientPlayerIds, completionCb);
    }

    //! Client-side decoded-photo accessor. Returns null if the photo isn't decoded yet
    //! (still transferring) or if the deliveryId is unknown. Used by message-list UI to
    //! decide between rendering the image and showing a "transferring" placeholder.
    AG0_TDLPhotoData GetDecodedPhoto(string deliveryId)
    {
        if (deliveryId.IsEmpty())
            return null;

        // Fast path: hash-based lookup.
        if (m_DecodedPhotos.Contains(deliveryId))
            return m_DecodedPhotos.Get(deliveryId);

        // Fallback: linear scan with direct string compare. Works around Enfusion
        // map<string, ...> hash mismatches that can happen when the lookup string
        // and the cache key took different serialization paths (snapshot serializer
        // for AG0_TDLMessageClient.imageDeliveryId vs. raw RPC string param for the
        // chunk sender's deliveryId), even though both originated from the same
        // queue command's deliveryId field.
        foreach (string key, AG0_TDLPhotoData photo : m_DecodedPhotos)
        {
            if (key == deliveryId)
                return photo;
        }

        return null;
    }

    //! Forward chunk reception from AG0_PlayerController_TDL.RpcDo_ReceiveImageChunk.
    //! Single entry point — the reassembler keys on deliveryId, allocates on first chunk,
    //! auto-finalizes once iReceived == totalChunks. Mirrors the proven terrain pattern.
    void HandleChunkData(string deliveryId, int totalChunks, int chunkIndex, string chunkData)
    {
        if (m_Reassembler)
            m_Reassembler.OnChunk(deliveryId, totalChunks, chunkIndex, chunkData, this);
    }

    //! Called by the reassembler when a complete JSON body has been rebuilt and is ready
    //! for decode. Hands the JSON to the existing decode pipeline; on photo ready, stores
    //! it in m_DecodedPhotos so the message-list UI can find it by deliveryId.
    //! NetworkId is resolved from the local player's message stores by scanning for the
    //! image-message with this deliveryId.
    void OnReassemblyComplete(string deliveryId, string jsonBody)
    {
        int networkId = FindLocalNetworkIdForDeliveryId(deliveryId);

        Print(string.Format("[TDL_PHOTO] reassembly complete (deliveryId=%1 networkId=%2) — decoding",
            deliveryId, networkId), LogLevel.NORMAL);

        AG0_TDLDecodedPhotoSink sink = new AG0_TDLDecodedPhotoSink(deliveryId, networkId);
        DecodePhotoFromJson(jsonBody, sink);
    }

    //! Scan the local player's per-network message stores for an image-message with the
    //! given deliveryId. Returns the networkId or -1 if not found. Used post-reassembly to
    //! avoid threading networkId through the chunk RPCs (which mirror terrain's 4-param shape).
    protected int FindLocalNetworkIdForDeliveryId(string deliveryId)
    {
        if (deliveryId.IsEmpty()) return -1;
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc) return -1;
        SCR_PlayerController tdlPc = SCR_PlayerController.Cast(pc);
        if (!tdlPc) return -1;
        array<int> ids = tdlPc.GetAllTDLNetworkIds();
        if (!ids) return -1;
        foreach (int nid : ids)
        {
            AG0_TDLMessageStore store = tdlPc.GetTDLMessageStore(nid);
            if (!store) continue;
            array<ref AG0_TDLMessageClient> all = store.GetAllMessages();
            if (!all) continue;
            foreach (AG0_TDLMessageClient msg : all)
            {
                if (msg && msg.contentType == ETDLMessageContentType.IMAGE
                       && msg.imageDeliveryId == deliveryId)
                    return nid;
            }
        }
        return -1;
    }

    //! Called by AG0_TDLDecodedPhotoSink on decode failure. Marks the message failed
    //! locally and fires the failure invoker.
    void OnDecodedPhotoFailed(string deliveryId, int networkId, string reason)
    {
        Print(string.Format("[TDL_PHOTO] decode failed (deliveryId=%1 networkId=%2): %3",
            deliveryId, networkId, reason), LogLevel.WARNING);

        MarkMessageFailedLocally(networkId, deliveryId);

        if (m_OnDecodedPhotoFailed)
            m_OnDecodedPhotoFailed.Invoke(deliveryId, networkId);
    }

    //! Internal: stash a successfully-decoded photo and update the local message store entry.
    //! Called by AG0_TDLDecodedPhotoSink on its callback.
    void OnDecodedPhotoReady(string deliveryId, int networkId, AG0_TDLPhotoData photo)
    {
        if (deliveryId.IsEmpty() || !photo)
            return;

        // Bound the decoded-photo map. Drop the oldest entry (insertion-ordered approximation)
        // if we're at capacity. For phase 2 minimal we don't track LRU here — the cap is high
        // enough that natural churn handles it.
        if (m_DecodedPhotos.Count() >= m_iMaxDecodedPhotos)
        {
            // Simple eviction: drop the first key we find. Not strictly LRU but acceptable
            // for phase 2 scope — can refine later if memory pressure shows up.
            string victimKey = "";
            foreach (string k, AG0_TDLPhotoData p : m_DecodedPhotos)
            {
                victimKey = k;
                break;
            }
            if (victimKey != "")
                m_DecodedPhotos.Remove(victimKey);
        }

        m_DecodedPhotos.Set(deliveryId, photo);

        Print(string.Format("[TDL_PHOTO] decoded photo cached for deliveryId=%1 (decoded_count=%2)",
            deliveryId, m_DecodedPhotos.Count()), LogLevel.NORMAL);

        // Flip the corresponding message's local transferState to READY (server-side replicated
        // state may still say TRANSFERRING — local view wins for rendering decisions).
        MarkMessageReadyLocally(networkId, deliveryId);

        // The OnDecodedPhotoArrived invoker is kept for any future subscribers but the
        // chat menu does NOT subscribe — it polls the decoded-photo cache directly via
        // DriveImageCardRendering on every OnMenuUpdate tick. ScriptInvokers proved
        // unreliable across the MP replication boundary and we got tired of debugging it.
        if (m_OnDecodedPhotoArrived)
            m_OnDecodedPhotoArrived.Invoke(deliveryId, networkId);
    }

    //----------------------------------------------------------------
    // INTERNAL: message-store reach-through (client-side)
    //----------------------------------------------------------------

    //! Find the local player's message store for the given network, locate the image-message
    //! by deliveryId, mark its transferState READY locally. No-op if anything is missing —
    //! this is a best-effort UI hint, not a correctness-critical operation.
    protected void MarkMessageReadyLocally(int networkId, string deliveryId)
    {
        if (networkId < 0 || deliveryId.IsEmpty())
            return;
        AG0_TDLMessageStore store = GetLocalMessageStoreFor(networkId);
        if (!store)
            return;
        array<ref AG0_TDLMessageClient> all = store.GetAllMessages();
        if (!all)
            return;
        foreach (AG0_TDLMessageClient msg : all)
        {
            if (msg && msg.contentType == ETDLMessageContentType.IMAGE && msg.imageDeliveryId == deliveryId)
            {
                msg.imageTransferState = ETDLImageTransferState.READY;
                return;
            }
        }
    }

    protected void MarkMessageFailedLocally(int networkId, string deliveryId)
    {
        if (networkId < 0 || deliveryId.IsEmpty())
            return;
        AG0_TDLMessageStore store = GetLocalMessageStoreFor(networkId);
        if (!store)
            return;
        array<ref AG0_TDLMessageClient> all = store.GetAllMessages();
        if (!all)
            return;
        foreach (AG0_TDLMessageClient msg : all)
        {
            if (msg && msg.contentType == ETDLMessageContentType.IMAGE && msg.imageDeliveryId == deliveryId)
            {
                msg.imageTransferState = ETDLImageTransferState.FAILED;
                return;
            }
        }
    }

    //! Resolve the local player's per-network message store. Returns null on dedicated server
    //! (no local player) or if the store hasn't been allocated yet.
    protected AG0_TDLMessageStore GetLocalMessageStoreFor(int networkId)
    {
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return null;
        SCR_PlayerController tdlPc = SCR_PlayerController.Cast(pc);
        if (!tdlPc)
            return null;
        return tdlPc.GetTDLMessageStore(networkId);
    }

    //----------------------------------------------------------------
    // INTERNAL: lookup helper — returns the active (non-settled) request for
    // requestId, or null. Use everywhere we need to look up a request from a
    // CallLater payload or a REST callback. Avoids the map.Find(out) shape
    // and matches the Contains+Get pattern used elsewhere in the project.
    //----------------------------------------------------------------

    protected AG0_TDLPhotoFetchRequest GetActiveFetch(int requestId)
    {
        if (!m_PendingFetches.Contains(requestId))
            return null;
        AG0_TDLPhotoFetchRequest req = m_PendingFetches.Get(requestId);
        if (!req || req.m_bSettled)
            return null;
        return req;
    }

    //----------------------------------------------------------------
    // INTERNAL: SETTLEMENT FROM REST CALLBACK
    //----------------------------------------------------------------

    //! Called by AG0_TDLImageCallback when the HTTP fetch returns 2xx.
    //! Hands the body off to the decode chain. If the request is unknown / already
    //! settled (e.g. timed out and swept), this is a no-op.
    //!
    //! For deliveryId-keyed fetches (image_deliver flow), also caches the raw JSON body
    //! under deliveryId BEFORE decode kicks off — the chunk sender reads from this cache
    //! to fan out to recipients, and we want it populated whether or not the local decode
    //! succeeds.
    void OnFetchSuccess(int requestId, string jsonData)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req)
            return;

        // Cache the raw response when this fetch carries a deliveryId. The chunk sender
        // reads the JSON from cache by deliveryId.
        if (!req.m_sDeliveryId.IsEmpty())
        {
            int sz = req.m_iSizeBytes;
            if (sz <= 0)
                sz = jsonData.Length();
            PutCacheEntry(req.m_sDeliveryId, jsonData, "json", req.m_sFingerprint, sz);
        }

        BeginDecode(requestId, jsonData);
    }

    //! Called by AG0_TDLImageCallback on HTTP error / timeout.
    void OnFetchFailure(int requestId, string reason)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req)
            return;

        SettleFailure(requestId, reason);
    }

    //----------------------------------------------------------------
    // INTERNAL: DECODE CHAIN (per-request state, looked up by requestId)
    //
    //   BeginDecode → StepBase64 (loops) → StepGunzipAndRects → SettleSuccess
    //
    // CallLater payloads carry the requestId (int). A disposed/settled request
    // causes the step to look up nothing and bail — no leak, no crash.
    //----------------------------------------------------------------

    protected void BeginDecode(int requestId, string jsonData)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req)
            return;

        JsonLoadContext json = new JsonLoadContext();
        if (!json.LoadFromString(jsonData))
        {
            SettleFailure(requestId, "json_parse_failed");
            return;
        }

        AG0_TDLPhotoData photo = new AG0_TDLPhotoData();
        if (!json.ReadValue("w", photo.m_iWidth) ||
            !json.ReadValue("h", photo.m_iHeight) ||
            !json.ReadValue("p", photo.m_aPalette))
        {
            SettleFailure(requestId, "missing_w_h_p");
            return;
        }

        string rgzField = "";
        string rField   = "";
        string dField   = "";
        bool hasRgz = json.ReadValue("rgz", rgzField) && rgzField.Length() > 0;
        bool hasR   = json.ReadValue("r",   rField)   && rField.Length()   > 0;
        bool hasD   = json.ReadValue("d",   dField)   && dField.Length()   > 0;

        if (hasRgz)
        {
            req.m_sPendingFieldKind = "rgz";
            req.m_sPendingPayload   = rgzField;
        }
        else if (hasR)
        {
            req.m_sPendingFieldKind = "r";
            req.m_sPendingPayload   = rField;
        }
        else if (hasD)
        {
            req.m_sPendingFieldKind = "d";
            req.m_sPendingPayload   = dField;
        }
        else
        {
            SettleFailure(requestId, "no_payload_field");
            return;
        }

        req.m_PendingPhoto = photo;
        req.m_iPendingT0   = System.GetTickCount();

        req.m_PendingB64 = new AG0_TDLBase64Decoder();
        req.m_PendingB64.Init(req.m_sPendingPayload);
        req.m_sPendingPayload = "";   // free the JSON-string copy now that the decoder owns it

        req.m_iPendingTBatch = System.GetTickCount();
        Print(string.Format("[TDL_PHOTO] rid=%1 decode start (%2 chars %3 field, %4ms/frame)",
            requestId, req.m_PendingB64.GetTotal(), req.m_sPendingFieldKind, m_iB64MsPerFrame), LogLevel.NORMAL);

        GetGame().GetCallqueue().CallLater(StepBase64, 0, false, requestId);
    }

    //! One frame of base64 decoding. Re-schedules itself until done, then defers gunzip.
    protected void StepBase64(int requestId)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req || !req.m_PendingB64)
            return;

        bool more = req.m_PendingB64.Step(m_iB64MsPerFrame);

        if (more)
        {
            int batchEnd = System.GetTickCount();
            if (batchEnd - req.m_iPendingTBatch > 250)
            {
                int progress = req.m_PendingB64.GetProgress();
                int total    = req.m_PendingB64.GetTotal();
                int pct      = 0;
                if (total > 0)
                    pct = (progress * 100) / total;
                Print(string.Format("[TDL_PHOTO] rid=%1 base64 %2/%3 (%4%%)",
                    requestId, progress, total, pct), LogLevel.NORMAL);
                req.m_iPendingTBatch = batchEnd;
            }
            GetGame().GetCallqueue().CallLater(StepBase64, 0, false, requestId);
            return;
        }

        // Base64 done — capture output, free decoder, schedule gunzip on a clean frame.
        req.m_aPendingBytes = req.m_PendingB64.GetOutput();
        req.m_PendingB64    = null;

        int tNow = System.GetTickCount();
        Print(string.Format("[TDL_PHOTO] rid=%1 T+%2ms base64 done (%3 bytes)",
            requestId, tNow - req.m_iPendingT0, req.m_aPendingBytes.Count()), LogLevel.NORMAL);

        GetGame().GetCallqueue().CallLater(StepGunzipAndRects, 0, false, requestId);
    }

    //! Synchronous gunzip + rect parse, scheduled on its own frame so the last base64 batch
    //! and the gunzip step never combine into a single big hitch. If gunzip becomes the new
    //! dominant hitch, AG0_TDLGzip would get the same resumable treatment as AG0_TDLBase64Decoder.
    protected void StepGunzipAndRects(int requestId)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req || !req.m_PendingPhoto || !req.m_aPendingBytes)
            return;

        array<int> bytes = req.m_aPendingBytes;
        req.m_aPendingBytes = null;

        if (req.m_sPendingFieldKind == "rgz")
        {
            int tA = System.GetTickCount();
            array<int> raw = AG0_TDLGzip.Gunzip(bytes);
            int tB = System.GetTickCount();
            Print(string.Format("[TDL_PHOTO] rid=%1 T+%2ms (+%3ms): gunzip produced %4 bytes",
                requestId, tB - req.m_iPendingT0, tB - tA, raw.Count()), LogLevel.NORMAL);

            if (raw.Count() == 0)
            {
                SettleFailure(requestId, "gunzip_failed");
                return;
            }

            req.m_PendingPhoto.m_aRects = AG0_TDLPhotoData.DecodeRectsFromBytes(raw);
            int tC = System.GetTickCount();
            Print(string.Format("[TDL_PHOTO] rid=%1 T+%2ms (+%3ms): %4 rects parsed",
                requestId, tC - req.m_iPendingT0, tC - tB, req.m_PendingPhoto.GetRectCount()), LogLevel.NORMAL);
        }
        else if (req.m_sPendingFieldKind == "r")
        {
            int tA = System.GetTickCount();
            req.m_PendingPhoto.m_aRects = AG0_TDLPhotoData.DecodeRectsFromBytes(bytes);
            int tB = System.GetTickCount();
            Print(string.Format("[TDL_PHOTO] rid=%1 T+%2ms (+%3ms): %4 rects parsed (uncompressed)",
                requestId, tB - req.m_iPendingT0, tB - tA, req.m_PendingPhoto.GetRectCount()), LogLevel.NORMAL);
        }
        else if (req.m_sPendingFieldKind == "d")
        {
            req.m_PendingPhoto.m_aPixels = bytes;
            int tA = System.GetTickCount();
            Print(string.Format("[TDL_PHOTO] rid=%1 T+%2ms: %3 pixels (legacy)",
                requestId, tA - req.m_iPendingT0, req.m_PendingPhoto.m_aPixels.Count()), LogLevel.NORMAL);
        }

        AG0_TDLPhotoData ready = req.m_PendingPhoto;
        SettleSuccess(requestId, ready);
    }

    //----------------------------------------------------------------
    // INTERNAL: SETTLEMENT (idempotent — exactly one fires per request)
    //----------------------------------------------------------------

    protected void SettleSuccess(int requestId, AG0_TDLPhotoData photo)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req)
            return;

        AG0_TDLPhotoFetchCallback cb = req.m_CompletionCb;
        req.Dispose();
        m_PendingFetches.Remove(requestId);

        if (cb)
            cb.OnPhotoReady(requestId, photo);
    }

    protected void SettleFailure(int requestId, string reason)
    {
        AG0_TDLPhotoFetchRequest req = GetActiveFetch(requestId);
        if (!req)
            return;

        Print(string.Format("[TDL_PHOTO] rid=%1 settle failure: %2", requestId, reason), LogLevel.WARNING);

        AG0_TDLPhotoFetchCallback cb = req.m_CompletionCb;
        req.Dispose();
        m_PendingFetches.Remove(requestId);

        if (cb)
            cb.OnPhotoFailed(requestId, reason);
    }

    //----------------------------------------------------------------
    // CACHE
    //----------------------------------------------------------------

    //! Returns the cache entry (and bumps last-access for LRU), or null on miss.
    AG0_TDLPhotoCacheEntry GetCacheEntry(string deliveryId)
    {
        if (!m_Cache.Contains(deliveryId))
            return null;
        AG0_TDLPhotoCacheEntry entry = m_Cache.Get(deliveryId);
        if (!entry)
            return null;
        entry.m_iLastAccessMs = System.GetTickCount();
        return entry;
    }

    //! Insert or replace a cache entry. Evicts LRU as needed to fit byte / count budget.
    void PutCacheEntry(string deliveryId, string payload, string fieldKind,
                       string fingerprint, int sizeBytes)
    {
        if (m_Cache.Contains(deliveryId))
        {
            AG0_TDLPhotoCacheEntry existing = m_Cache.Get(deliveryId);
            if (existing)
                m_iCacheTotalBytes = m_iCacheTotalBytes - existing.m_iSizeBytes;
            m_Cache.Remove(deliveryId);
        }

        // Evict LRU until we fit byte budget.
        while (m_iCacheTotalBytes + sizeBytes > m_iCacheCapBytes && m_Cache.Count() > 0)
            EvictLRU();

        // Evict LRU until we fit entry-count budget.
        while (m_Cache.Count() >= m_iCacheMaxEntries)
            EvictLRU();

        AG0_TDLPhotoCacheEntry entry = new AG0_TDLPhotoCacheEntry(deliveryId, payload, fieldKind, fingerprint, sizeBytes);
        m_Cache.Set(deliveryId, entry);
        m_iCacheTotalBytes = m_iCacheTotalBytes + sizeBytes;
    }

    void EvictCache(string deliveryId)
    {
        if (!m_Cache.Contains(deliveryId))
            return;
        AG0_TDLPhotoCacheEntry entry = m_Cache.Get(deliveryId);
        if (entry)
            m_iCacheTotalBytes = m_iCacheTotalBytes - entry.m_iSizeBytes;
        m_Cache.Remove(deliveryId);
    }

    int GetCacheTotalBytes() { return m_iCacheTotalBytes; }
    int GetCacheEntryCount() { return m_Cache.Count(); }
    int GetPendingFetchCount() { return m_PendingFetches.Count(); }

    //! Drop the oldest entry by m_iLastAccessMs. Caller must guarantee Count() > 0.
    protected void EvictLRU()
    {
        string oldestKey = "";
        int    oldestMs  = -1;
        foreach (string key, AG0_TDLPhotoCacheEntry entry : m_Cache)
        {
            if (oldestMs < 0 || entry.m_iLastAccessMs < oldestMs)
            {
                oldestMs  = entry.m_iLastAccessMs;
                oldestKey = key;
            }
        }
        if (oldestKey != "" && m_Cache.Contains(oldestKey))
        {
            AG0_TDLPhotoCacheEntry victim = m_Cache.Get(oldestKey);
            if (victim)
                m_iCacheTotalBytes = m_iCacheTotalBytes - victim.m_iSizeBytes;
            m_Cache.Remove(oldestKey);
        }
    }

    //----------------------------------------------------------------
    // PERIODIC SWEEP — 1Hz from Update()
    //----------------------------------------------------------------

    protected void Sweep()
    {
        int now = System.GetTickCount();

        // Cache age sweep. Snapshot keys to evict, then evict — don't mutate during iteration.
        array<string> staleCacheKeys = {};
        foreach (string key, AG0_TDLPhotoCacheEntry entry : m_Cache)
        {
            if (now - entry.m_iInsertedAtMs > m_iCacheEntryMaxAgeMs)
                staleCacheKeys.Insert(key);
        }
        foreach (string key : staleCacheKeys)
        {
            if (!m_Cache.Contains(key))
                continue;
            AG0_TDLPhotoCacheEntry victim = m_Cache.Get(key);
            if (victim)
                m_iCacheTotalBytes = m_iCacheTotalBytes - victim.m_iSizeBytes;
            m_Cache.Remove(key);
        }

        // Fetch timeout sweep.
        array<int> staleFetchIds = {};
        foreach (int rid, AG0_TDLPhotoFetchRequest req : m_PendingFetches)
        {
            if (!req.m_bSettled && (now - req.m_iStartedAtMs > m_iFetchTimeoutMs))
                staleFetchIds.Insert(rid);
        }
        foreach (int sid : staleFetchIds)
        {
            Print(string.Format("[TDL_PHOTO] rid=%1 timed out after %2ms — settling failure",
                sid, m_iFetchTimeoutMs), LogLevel.WARNING);
            SettleFailure(sid, "timeout");
        }

        if (m_bDebugMemory)
        {
            Print(string.Format("[TDL_PHOTO] inflight=%1 cache_entries=%2 cache_bytes=%3 (cap %4)",
                m_PendingFetches.Count(), m_Cache.Count(), m_iCacheTotalBytes, m_iCacheCapBytes), LogLevel.NORMAL);
        }
    }

    //----------------------------------------------------------------
    // DIAGNOSTICS
    //----------------------------------------------------------------

    void SetDebugMemory(bool enabled)
    {
        m_bDebugMemory = enabled;
    }

    void DumpDiagnostics()
    {
        Print(string.Format("[TDL_PHOTO] DIAG inflight=%1 cache_entries=%2 cache_bytes=%3/%4",
            m_PendingFetches.Count(), m_Cache.Count(), m_iCacheTotalBytes, m_iCacheCapBytes), LogLevel.NORMAL);
    }
}
