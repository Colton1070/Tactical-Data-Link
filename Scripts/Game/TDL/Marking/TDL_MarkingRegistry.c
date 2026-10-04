//! REST callback for the registry's GET / PUT. Handler-registration shape, same as the
//! image and queue callbacks: the override-OnSuccess variant logs "Function was not set".
class TDL_MarkingRestCallback : RestCallback
{
    protected string m_sIdentityId;
    protected bool m_bIsFetch;
    protected bool m_bIsImage;

    void TDL_MarkingRestCallback(string identityId, bool isFetch, bool isImage = false)
    {
        m_sIdentityId = identityId;
        m_bIsFetch = isFetch;
        m_bIsImage = isImage;
        SetOnSuccess(OnSuccessHandler);
        SetOnError(OnErrorHandler);
    }

    void OnSuccessHandler(RestCallback cb)
    {
        TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
        if (!reg)
            return;
        if (m_bIsImage)
            reg.OnImageFetchSuccess(m_sIdentityId, cb.GetData());
        else if (m_bIsFetch)
            reg.OnFetchSuccess(m_sIdentityId, cb.GetHttpCode(), cb.GetData());
    }

    void OnErrorHandler(RestCallback cb)
    {
        TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
        if (!reg)
            return;
        if (m_bIsImage)
            reg.OnImageFetchFailure(m_sIdentityId, cb.GetHttpCode());
        else if (m_bIsFetch)
            reg.OnFetchFailure(m_sIdentityId, cb.GetHttpCode());
        else
            Print(string.Format("[TDL_Marking] write-back for %1 failed http=%2", m_sIdentityId, cb.GetHttpCode()), LogLevel.WARNING);
    }
}

//! Completion for a marking image chunk transfer; logging only, the item's RplProp
//! already told clients which id to wait for.
class TDL_MarkingImageTransferCallback : AG0_TDLImageTransferCallback
{
    override void OnTransferFailed(int transferId, string deliveryId, string reason)
    {
        Print(string.Format("[TDL_Marking] image %1 transfer failed: %2", deliveryId, reason), LogLevel.WARNING);
    }
}

//! Server-only. The only place that knows identity -> (slot -> text). Items never do;
//! they carry whatever the registry last stamped on them through their RplProp.
//!
//! Sources, in the order they usually happen: a marking_set queue command, the API pull
//! on a registry miss at spawn, and the in-game dialog (which also writes back so the
//! site and other servers agree). Every source ends in Apply(), which stamps every
//! TDL_MarkingComponent the player's character holds or wears.
class TDL_MarkingRegistry
{
    protected static const string API_BASE_URL = "https://tdl.blufor.info/api/mod";

    protected static ref TDL_MarkingRegistry s_Instance;

    protected ref map<string, ref map<string, string>> m_Records = new map<string, ref map<string, string>>();
    //! identity -> (slot -> image id). Kept apart from the text map so a text-only merge
    //! never touches images and vice versa.
    protected ref map<string, ref map<string, string>> m_Images = new map<string, ref map<string, string>>();
    //! identity -> (slot -> "id0,id1,…") for animated images, and the hold time. "" = still.
    protected ref map<string, ref map<string, string>> m_ImageFrames = new map<string, ref map<string, string>>();
    protected ref map<string, ref map<string, int>> m_FrameMs = new map<string, ref map<string, int>>();
    //! identity -> (slot -> colours).
    protected ref map<string, ref map<string, ref TDL_MarkingStyle>> m_Styles = new map<string, ref map<string, ref TDL_MarkingStyle>>();
    //! Image ids whose payload sits in the photo manager cache and has gone out to
    //! everyone present; late joiners get the whole set on audit.
    protected ref set<string> m_ImagesReady = new set<string>();
    //! imageId -> players it has been chunked to, so the on-fetch broadcast and the
    //! late-joiner resend never deliver (and decode) the same image twice.
    protected ref map<string, ref set<int>> m_ImageSentTo = new map<string, ref set<int>>();
    protected ref set<string> m_ImageFetchInFlight = new set<string>();
    protected ref set<string> m_Fetched = new set<string>();
    protected ref set<string> m_FetchInFlight = new set<string>();
    protected ref map<string, ref TDL_MarkingRestCallback> m_Callbacks = new map<string, ref TDL_MarkingRestCallback>();
    protected bool m_bSpawnHookRegistered;

    //------------------------------------------------------------------------------------------------
    static TDL_MarkingRegistry GetInstance()
    {
        if (!Replication.IsServer())
            return null;
        if (!s_Instance)
            s_Instance = new TDL_MarkingRegistry();
        return s_Instance;
    }

    //------------------------------------------------------------------------------------------------
    //! Idempotent. The game mode may not exist yet when the registry is first touched, so
    //! callers retry this on their own cadence; TDL_MarkingComponent init is one of them.
    void EnsureSpawnHook()
    {
        if (m_bSpawnHookRegistered)
            return;
        SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
        if (!gameMode)
            return;
        ScriptInvokerBase<SCR_BaseGameMode_PlayerIdAndEntity> invoker = gameMode.GetOnPlayerSpawned();
        if (!invoker)
            return;
        invoker.Insert(OnPlayerSpawned);
        // Also on audit success: fires per connecting player once their controller is
        // addressable, independent of the spawn flow (Workbench play mode skips it).
        ScriptInvokerBase<SCR_BaseGameMode_PlayerId> audit = gameMode.GetOnPlayerAuditSuccess();
        if (audit)
            audit.Insert(OnPlayerAudited);
        m_bSpawnHookRegistered = true;

        // Anyone already in the session (the host, players who joined before the registry
        // came up) never hits either hook; resolve them now.
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        if (!playerMgr)
            return;
        array<int> playerIds = {};
        playerMgr.GetPlayers(playerIds);
        foreach (int playerId : playerIds)
        {
            string identityId = IdentityForPlayer(playerId);
            if (!identityId.IsEmpty())
                GetGame().GetCallqueue().CallLater(ResolveAndApply, 750, false, identityId);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void OnPlayerAudited(int playerId)
    {
        // Player ids are reused across sessions: a fresh audit means a fresh client that has
        // none of the images a previous holder of this id was sent.
        foreach (string imageId, set<int> sentTo : m_ImageSentTo)
            sentTo.RemoveItem(playerId);
        // Late joiner: every marking image already in circulation, before their own record.
        GetGame().GetCallqueue().CallLater(SendAllImagesTo, 2000, false, playerId);
        string identityId = IdentityForPlayer(playerId);
        if (identityId.IsEmpty())
            return;
        GetGame().GetCallqueue().CallLater(ResolveAndApply, 750, false, identityId);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnPlayerSpawned(int playerId, IEntity entity)
    {
        string identityId = IdentityForPlayer(playerId);
        if (identityId.IsEmpty())
            return;
        // Loadout attachments land over the next frames; stamping on the spawn frame
        // finds an empty inventory.
        GetGame().GetCallqueue().CallLater(ResolveAndApply, 750, false, identityId);
    }

    //------------------------------------------------------------------------------------------------
    //! Registry hit -> apply now. Miss -> one API pull, then apply from the callback.
    void ResolveAndApply(string identityId)
    {
        if (identityId.IsEmpty())
            return;
        if (m_Records.Contains(identityId) || m_Fetched.Contains(identityId))
        {
            Apply(identityId);
            return;
        }
        Fetch(identityId);
    }

    //------------------------------------------------------------------------------------------------
    //! Called by a marking item once it knows who is holding it (server side).
    void OnItemHeldBy(int playerId, TDL_MarkingComponent item)
    {
        string identityId = IdentityForPlayer(playerId);
        if (identityId.IsEmpty() || !item)
            return;
        map<string, string> rec = m_Records.Get(identityId);
        map<string, string> images = m_Images.Get(identityId);
        if (rec)
        {
            string text;
            if (rec.Find(item.GetSlot(), text))
                item.SetText(text);
        }
        if (images)
        {
            string imageId;
            string frameList;
            int frameMs;
            if (ImageReadyFor(identityId, item.GetSlot(), imageId, frameList, frameMs))
                item.SetImage(imageId, frameList, frameMs);
        }
        map<string, ref TDL_MarkingStyle> styles = m_Styles.Get(identityId);
        if (styles)
        {
            TDL_MarkingStyle st = styles.Get(item.GetSlot());
            if (st)
                item.SetStyle(st.m_sBg, st.m_sFg);
        }
        if (rec || images || styles)
            return;
        if (!m_Fetched.Contains(identityId))
            Fetch(identityId);
    }

    //------------------------------------------------------------------------------------------------
    //! marking_set from the queue, or a merge from the API pull. Empty text deletes the slot.
    void Merge(string identityId, array<string> slotKeys, array<string> slotTexts, bool applyNow)
    {
        if (identityId.IsEmpty() || !slotKeys || !slotTexts)
            return;
        map<string, string> rec = m_Records.Get(identityId);
        if (!rec)
        {
            rec = new map<string, string>();
            m_Records.Set(identityId, rec);
        }
        int n = slotKeys.Count();
        if (slotTexts.Count() < n)
            n = slotTexts.Count();
        // A cleared slot is kept as "" rather than removed: Apply has to stamp the empty
        // string onto the item to take the old text off it, and a missing key would skip it.
        for (int i = 0; i < n; i++)
            rec.Set(slotKeys[i], slotTexts[i]);
        m_Fetched.Insert(identityId);
        if (applyNow)
            Apply(identityId);
    }


    //------------------------------------------------------------------------------------------------
    //! Image ids per slot, "" clears. A new id is fetched from the API and chunked to every
    //! client before Apply stamps it, so items never point at pixels nobody has. Animated
    //! images pass their frame list ("id0,id1,…") and hold time in the optional arrays;
    //! every frame is fetched and the slot is held back until all of them have gone out.
    void MergeImages(string identityId, array<string> slotKeys, array<string> slotImageIds, bool applyNow,
                     array<string> slotFrames = null, array<string> slotFrameMs = null)
    {
        if (identityId.IsEmpty() || !slotKeys || !slotImageIds)
            return;
        map<string, string> images = m_Images.Get(identityId);
        if (!images)
        {
            images = new map<string, string>();
            m_Images.Set(identityId, images);
        }
        map<string, string> frames = m_ImageFrames.Get(identityId);
        if (!frames)
        {
            frames = new map<string, string>();
            m_ImageFrames.Set(identityId, frames);
        }
        map<string, int> holds = m_FrameMs.Get(identityId);
        if (!holds)
        {
            holds = new map<string, int>();
            m_FrameMs.Set(identityId, holds);
        }
        int n = slotKeys.Count();
        if (slotImageIds.Count() < n)
            n = slotImageIds.Count();
        for (int i = 0; i < n; i++)
        {
            string imageId = slotImageIds[i];
            images.Set(slotKeys[i], imageId);
            string frameList = "";
            int frameMs = 0;
            if (!imageId.IsEmpty() && slotFrames && slotFrames.Count() > i)
                frameList = slotFrames[i];
            if (!frameList.IsEmpty() && slotFrameMs && slotFrameMs.Count() > i)
                frameMs = slotFrameMs[i].ToInt();
            frames.Set(slotKeys[i], frameList);
            holds.Set(slotKeys[i], frameMs);

            array<string> ids = {};
            if (!frameList.IsEmpty())
                frameList.Split(",", ids, true);
            if (ids.IsEmpty() && !imageId.IsEmpty())
                ids.Insert(imageId);
            foreach (string id : ids)
            {
                if (id != TDL_MarkingComponent.TEST_IMAGE_ID && !m_ImagesReady.Contains(id))
                    FetchImage(id);
            }
        }
        m_Fetched.Insert(identityId);
        if (applyNow)
            Apply(identityId);
    }

    //------------------------------------------------------------------------------------------------
    //! Which of a slot's frame ids have not been fetched+sent yet, for the log.
    protected string MissingFrames(string identityId, string slot)
    {
        map<string, string> images = m_Images.Get(identityId);
        map<string, string> frames = m_ImageFrames.Get(identityId);
        string imageId;
        string frameList;
        if (images)
            images.Find(slot, imageId);
        if (frames)
            frames.Find(slot, frameList);
        array<string> ids = {};
        if (!frameList.IsEmpty())
            frameList.Split(",", ids, true);
        if (ids.IsEmpty() && !imageId.IsEmpty())
            ids.Insert(imageId);
        string missing = "";
        int n = 0;
        foreach (string id : ids)
        {
            if (id == TDL_MarkingComponent.TEST_IMAGE_ID || m_ImagesReady.Contains(id))
                continue;
            n++;
            if (n <= 3)
                missing += id + " ";
        }
        if (n == 0)
            return "nothing missing";
        return string.Format("%1 frame(s) not ready (%2in flight: %3)", n, missing, m_ImageFetchInFlight.Count());
    }

    //------------------------------------------------------------------------------------------------
    //! True once every frame of the slot's image (or the single image) has been sent out.
    protected bool ImageReadyFor(string identityId, string slot, out string imageId, out string frameList, out int frameMs)
    {
        imageId = "";
        frameList = "";
        frameMs = 0;
        map<string, string> images = m_Images.Get(identityId);
        if (!images || !images.Find(slot, imageId))
            return false;
        map<string, string> frames = m_ImageFrames.Get(identityId);
        if (frames)
            frames.Find(slot, frameList);
        map<string, int> holds = m_FrameMs.Get(identityId);
        if (holds)
            holds.Find(slot, frameMs);
        if (imageId.IsEmpty())
            return true;   // a clear is always ready
        array<string> ids = {};
        if (!frameList.IsEmpty())
            frameList.Split(",", ids, true);
        if (ids.IsEmpty())
            ids.Insert(imageId);
        foreach (string id : ids)
        {
            if (id != TDL_MarkingComponent.TEST_IMAGE_ID && !m_ImagesReady.Contains(id))
                return false;
        }
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Colours per slot ("" = item default), stored as the resulting state like the others.
    void MergeStyles(string identityId, array<string> slotKeys, array<string> bgs, array<string> fgs, bool applyNow)
    {
        if (identityId.IsEmpty() || !slotKeys || !bgs || !fgs)
            return;
        map<string, ref TDL_MarkingStyle> styles = m_Styles.Get(identityId);
        if (!styles)
        {
            styles = new map<string, ref TDL_MarkingStyle>();
            m_Styles.Set(identityId, styles);
        }
        int n = slotKeys.Count();
        if (bgs.Count() < n)
            n = bgs.Count();
        if (fgs.Count() < n)
            n = fgs.Count();
        for (int i = 0; i < n; i++)
        {
            TDL_MarkingStyle st = new TDL_MarkingStyle();
            st.m_sBg = bgs[i];
            st.m_sFg = fgs[i];
            styles.Set(slotKeys[i], st);
        }
        m_Fetched.Insert(identityId);
        if (applyNow)
            Apply(identityId);
    }

    //------------------------------------------------------------------------------------------------
    //! Chat command path: one colour key on one slot, kept for the session and written back
    //! so a linked player keeps it. The other key keeps whatever it was.
    void OnLocalSetStyle(string identityId, string slot, string key, string value)
    {
        if (identityId.IsEmpty() || slot.IsEmpty())
            return;
        map<string, ref TDL_MarkingStyle> styles = m_Styles.Get(identityId);
        TDL_MarkingStyle cur;
        if (styles)
            cur = styles.Get(slot);
        string bg = "";
        string fg = "";
        if (cur)
        {
            bg = cur.m_sBg;
            fg = cur.m_sFg;
        }
        if (key == "fg")
            fg = value;
        else
            bg = value;
        array<string> keys = {};
        array<string> bgs = {};
        array<string> fgs = {};
        keys.Insert(slot);
        bgs.Insert(bg);
        fgs.Insert(fg);
        MergeStyles(identityId, keys, bgs, fgs, true);
        WriteBackStyle(identityId, slot, key, value);
    }

    //------------------------------------------------------------------------------------------------
    //! Session-only image set (chat command / tests). Nothing is written back: image
    //! markings are uploaded on the site, the mod only ever selects by id.
    void OnLocalSetImage(string identityId, string slot, string imageId)
    {
        array<string> keys = {};
        array<string> ids = {};
        keys.Insert(slot);
        ids.Insert(imageId);
        MergeImages(identityId, keys, ids, true);
    }

    //------------------------------------------------------------------------------------------------
    //! In-game dialog path: keep the registry current and tell the API, so the site shows
    //! the value and other servers pull it on spawn.
    void OnLocalSet(string identityId, string slot, string text, bool applyNow = true)
    {
        if (identityId.IsEmpty() || slot.IsEmpty())
            return;
        array<string> keys = {};
        array<string> texts = {};
        keys.Insert(slot);
        texts.Insert(text);
        Merge(identityId, keys, texts, applyNow);
        WriteBack(identityId, slot, text);
    }

    //------------------------------------------------------------------------------------------------
    //! Session-scoped view of a player's record, for `#tdl markings`.
    map<string, string> GetRecord(string identityId)
    {
        return m_Records.Get(identityId);
    }

    //------------------------------------------------------------------------------------------------
    //! One line per slot with everything the registry holds for it, for `#tdl markings`.
    string DescribeSlot(string identityId, string slot)
    {
        string line = "";
        map<string, string> rec = m_Records.Get(identityId);
        string text;
        if (rec && rec.Find(slot, text) && !text.IsEmpty())
            line += text;
        map<string, string> images = m_Images.Get(identityId);
        string imageId;
        if (images && images.Find(slot, imageId) && !imageId.IsEmpty())
        {
            string state = "pending";
            string frameList;
            int frameMs;
            string unused;
            if (ImageReadyFor(identityId, slot, unused, frameList, frameMs))
                state = "sent";
            line += string.Format(" [image %1 %2", imageId, state);
            if (!frameList.IsEmpty())
            {
                array<string> ids = {};
                frameList.Split(",", ids, true);
                line += string.Format(", %1 frames @ %2 ms", ids.Count(), frameMs);
            }
            line += "]";
        }
        map<string, ref TDL_MarkingStyle> styles = m_Styles.Get(identityId);
        if (styles)
        {
            TDL_MarkingStyle st = styles.Get(slot);
            if (st)
            {
                if (!st.m_sBg.IsEmpty())
                    line += " bg=" + st.m_sBg;
                if (!st.m_sFg.IsEmpty())
                    line += " text=" + st.m_sFg;
            }
        }
        return line.Trim();
    }

    //------------------------------------------------------------------------------------------------
    //! Every slot that has anything set for this player, across text, image and style.
    void GetSlotsInUse(string identityId, out array<string> slots)
    {
        slots = {};
        map<string, string> rec = m_Records.Get(identityId);
        if (rec)
        {
            foreach (string slot, string text : rec)
            {
                if (!slots.Contains(slot))
                    slots.Insert(slot);
            }
        }
        map<string, string> images = m_Images.Get(identityId);
        if (images)
        {
            foreach (string slot, string id : images)
            {
                if (!slots.Contains(slot))
                    slots.Insert(slot);
            }
        }
        map<string, ref TDL_MarkingStyle> styles = m_Styles.Get(identityId);
        if (styles)
        {
            foreach (string slot, TDL_MarkingStyle st : styles)
            {
                if (!slots.Contains(slot))
                    slots.Insert(slot);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    void Apply(string identityId)
    {
        map<string, string> rec = m_Records.Get(identityId);
        map<string, string> images = m_Images.Get(identityId);
        map<string, ref TDL_MarkingStyle> styles = m_Styles.Get(identityId);
        if ((!rec || rec.IsEmpty()) && (!images || images.IsEmpty()) && (!styles || styles.IsEmpty()))
            return;

        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        int playerId = system.GetPlayerIdFromIdentityId(identityId);
        if (playerId <= 0)
            return;
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        if (!playerMgr)
            return;
        IEntity character = playerMgr.GetPlayerControlledEntity(playerId);
        if (!character)
            return;

        SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
        if (!inventory)
            return;
        array<IEntity> items = {};
        inventory.GetItems(items);

        int stamped = 0;
        foreach (IEntity item : items)
        {
            if (!item)
                continue;
            TDL_MarkingComponent marking = TDL_MarkingComponent.Cast(item.FindComponent(TDL_MarkingComponent));
            if (!marking)
                continue;
            string text;
            if (rec && rec.Find(marking.GetSlot(), text))
            {
                marking.SetText(text);
                stamped++;
            }
            if (styles)
            {
                TDL_MarkingStyle st = styles.Get(marking.GetSlot());
                if (st)
                    marking.SetStyle(st.m_sBg, st.m_sFg);
            }
            if (images)
            {
                // Hold the ids back until every frame has been sent; Apply runs again from
                // the image fetch path once they have.
                string imageId;
                string frameList;
                int frameMs;
                if (ImageReadyFor(identityId, marking.GetSlot(), imageId, frameList, frameMs))
                {
                    marking.SetImage(imageId, frameList, frameMs);
                    stamped++;
                }
                else if (!imageId.IsEmpty())
                {
                    Print(string.Format("[TDL_Marking] slot %1 image %2 held back for player %3: %4", marking.GetSlot(), imageId, playerId, MissingFrames(identityId, marking.GetSlot())), LogLevel.DEBUG);
                }
            }
        }
        Print(string.Format("[TDL_Marking] applied to %1 item(s) for player %2", stamped, playerId), LogLevel.DEBUG);
    }

    // ============================================
    // API
    // ============================================

    //------------------------------------------------------------------------------------------------
    protected void Fetch(string identityId)
    {
        if (m_FetchInFlight.Contains(identityId))
            return;
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        AG0_TDLApiManager api = system.GetApiManager();
        if (!api || !api.CanCommunicate())
            return;

        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
            return;
        ctx.SetHeaders(string.Format("Authorization,Bearer %1", api.GetApiKey()));

        TDL_MarkingRestCallback cb = new TDL_MarkingRestCallback(identityId, true);
        m_Callbacks.Set("get:" + identityId, cb);
        m_FetchInFlight.Insert(identityId);
        ctx.GET(cb, string.Format("/markings/player/%1", identityId));
    }

    //------------------------------------------------------------------------------------------------
    void OnFetchSuccess(string identityId, int httpCode, string data)
    {
        m_FetchInFlight.RemoveItem(identityId);
        m_Callbacks.Remove("get:" + identityId);
        m_Fetched.Insert(identityId);

        // 204: no record yet. Nothing to stamp; the prefab default stands.
        if (data.IsEmpty())
            return;

        JsonLoadContext json = new JsonLoadContext();
        if (!json.LoadFromString(data))
        {
            Print("[TDL_Marking] markings GET returned unparseable JSON", LogLevel.WARNING);
            return;
        }
        array<string> keys = {};
        array<string> texts = {};
        array<string> imageIds = {};
        json.ReadValue("slotKeys", keys);
        json.ReadValue("slotTexts", texts);
        Merge(identityId, keys, texts, false);
        if (json.ReadValue("slotImageIds", imageIds) && imageIds.Count() > 0)
        {
            array<string> frames = {};
            array<string> frameMs = {};
            json.ReadValue("slotImageFrames", frames);
            json.ReadValue("slotFrameMs", frameMs);
            MergeImages(identityId, keys, imageIds, false, frames, frameMs);
        }
        array<string> bgs = {};
        array<string> fgs = {};
        if (json.ReadValue("slotBg", bgs) && json.ReadValue("slotFg", fgs) && bgs.Count() > 0)
            MergeStyles(identityId, keys, bgs, fgs, false);
        Apply(identityId);
    }

    // ============================================
    // Images
    // ============================================

    //------------------------------------------------------------------------------------------------
    //! GET the pre-rendered payload for one image id. Same JSON shape the message image
    //! pipeline serves ({ w, h, p, rgz }), so the photo manager's cache, chunk sender and
    //! decoder take it unchanged.
    protected void FetchImage(string imageId)
    {
        if (imageId.IsEmpty() || m_ImageFetchInFlight.Contains(imageId))
            return;
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        AG0_TDLApiManager api = system.GetApiManager();
        if (!api || !api.CanCommunicate())
            return;
        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
            return;
        ctx.SetHeaders(string.Format("Authorization,Bearer %1", api.GetApiKey()));

        TDL_MarkingRestCallback cb = new TDL_MarkingRestCallback(imageId, true, true);
        m_Callbacks.Set("img:" + imageId, cb);
        m_ImageFetchInFlight.Insert(imageId);
        ctx.GET(cb, string.Format("/markings/image/%1", imageId));
    }

    //------------------------------------------------------------------------------------------------
    void OnImageFetchSuccess(string imageId, string data)
    {
        m_ImageFetchInFlight.RemoveItem(imageId);
        m_Callbacks.Remove("img:" + imageId);
        if (data.IsEmpty())
        {
            Print(string.Format("[TDL_Marking] image %1 returned no body", imageId), LogLevel.WARNING);
            return;
        }
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        AG0_TDLPhotoManager photos = system.GetPhotoManager();
        if (!photos)
            return;

        photos.PutCacheEntry(imageId, data, "rgz", "", data.Length());
        // Decode on the server too: a listen host paints its own patches from the same
        // decoded cache that remote clients fill from chunks.
        photos.DecodePhotoFromJson(data, new AG0_TDLDecodedPhotoSink(imageId, -1));
        m_ImagesReady.Insert(imageId);

        array<int> playerIds = {};
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        if (playerMgr)
            playerMgr.GetPlayers(playerIds);
        int sent = SendImageTo(photos, imageId, playerIds);
        Print(string.Format("[TDL_Marking] image %1 cached (%2 bytes), sent to %3 player(s)", imageId, data.Length(), sent), LogLevel.DEBUG);

        // Stamp every record that was waiting on this id (as its image or one of its frames).
        foreach (string identityId, map<string, string> images : m_Images)
        {
            bool waiting = false;
            foreach (string slot, string id : images)
            {
                if (id == imageId)
                {
                    waiting = true;
                    break;
                }
            }
            map<string, string> frames = m_ImageFrames.Get(identityId);
            if (!waiting && frames)
            {
                foreach (string slot, string list : frames)
                {
                    if (list.Contains(imageId))
                    {
                        waiting = true;
                        break;
                    }
                }
            }
            if (waiting)
                Apply(identityId);
        }
    }

    //------------------------------------------------------------------------------------------------
    void OnImageFetchFailure(string imageId, int httpCode)
    {
        m_ImageFetchInFlight.RemoveItem(imageId);
        m_Callbacks.Remove("img:" + imageId);
        Print(string.Format("[TDL_Marking] image %1 GET failed http=%2", imageId, httpCode), LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    //! One transfer per known image to a single (late-joining) player.
    protected void SendAllImagesTo(int playerId)
    {
        if (m_ImagesReady.IsEmpty())
            return;
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        AG0_TDLPhotoManager photos = system.GetPhotoManager();
        if (!photos)
            return;
        array<int> one = {};
        one.Insert(playerId);
        foreach (string imageId : m_ImagesReady)
            SendImageTo(photos, imageId, one);
    }

    //------------------------------------------------------------------------------------------------
    //! Chunk one image to the given players, skipping any it already went to. Returns how
    //! many were actually sent.
    protected int SendImageTo(AG0_TDLPhotoManager photos, string imageId, array<int> playerIds)
    {
        set<int> sentTo = m_ImageSentTo.Get(imageId);
        if (!sentTo)
        {
            sentTo = new set<int>();
            m_ImageSentTo.Set(imageId, sentTo);
        }
        // Listen host: its client reads the server's decoded cache directly
        // (AG0_TDLPhotoManager.GetActiveInstance prefers the system manager), so chunking
        // the image to the host player would only decode it a second time. A dedicated
        // server has no local player controller, so this stays -1 there.
        int hostPlayerId = -1;
        PlayerController localPc = GetGame().GetPlayerController();
        if (localPc)
            hostPlayerId = localPc.GetPlayerId();
        array<int> targets = {};
        foreach (int playerId : playerIds)
        {
            if (sentTo.Contains(playerId))
                continue;
            if (playerId == hostPlayerId)
            {
                sentTo.Insert(playerId);
                continue;
            }
            sentTo.Insert(playerId);
            targets.Insert(playerId);
        }
        if (targets.IsEmpty())
            return 0;
        photos.BeginDistribute(imageId, -1, targets, new TDL_MarkingImageTransferCallback());
        return targets.Count();
    }

    //------------------------------------------------------------------------------------------------
    void OnFetchFailure(string identityId, int httpCode)
    {
        m_FetchInFlight.RemoveItem(identityId);
        m_Callbacks.Remove("get:" + identityId);
        // 403 = feature off for this server; don't hammer the API for every spawn.
        if (httpCode == 403 || httpCode == 404)
            m_Fetched.Insert(identityId);
        Print(string.Format("[TDL_Marking] markings GET for %1 failed http=%2", identityId, httpCode), LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    protected void WriteBack(string identityId, string slot, string text)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        AG0_TDLApiManager api = system.GetApiManager();
        if (!api || !api.CanCommunicate())
            return;

        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
            return;
        ctx.SetHeaders(string.Format("Authorization,Bearer %1,Content-Type,application/json", api.GetApiKey()));

        JsonSaveContext json = new JsonSaveContext();
        json.StartObject("markings");
        if (text.IsEmpty())
        {
            json.WriteValue(slot, "");
        }
        else
        {
            json.StartObject(slot);
            json.WriteValue("kind", "text");
            json.WriteValue("text", text);
            json.EndObject();
        }
        json.EndObject();

        TDL_MarkingRestCallback cb = new TDL_MarkingRestCallback(identityId, false);
        m_Callbacks.Set("put:" + identityId + ":" + slot, cb);
        ctx.PUT(cb, string.Format("/markings/player/%1", identityId), json.SaveToString());
    }

    //------------------------------------------------------------------------------------------------
    //! PUT one style key; text and image on the slot are untouched by the API's merge.
    protected void WriteBackStyle(string identityId, string slot, string key, string value)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system)
            return;
        AG0_TDLApiManager api = system.GetApiManager();
        if (!api || !api.CanCommunicate())
            return;
        RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
        if (!ctx)
            return;
        ctx.SetHeaders(string.Format("Authorization,Bearer %1,Content-Type,application/json", api.GetApiKey()));

        JsonSaveContext json = new JsonSaveContext();
        json.StartObject("markings");
        json.StartObject(slot);
        json.StartObject("style");
        json.WriteValue(key, value);
        json.EndObject();
        json.EndObject();
        json.EndObject();

        TDL_MarkingRestCallback cb = new TDL_MarkingRestCallback(identityId, false);
        m_Callbacks.Set("putstyle:" + identityId + ":" + slot + ":" + key, cb);
        ctx.PUT(cb, string.Format("/markings/player/%1", identityId), json.SaveToString());
    }

    //------------------------------------------------------------------------------------------------
    protected string IdentityForPlayer(int playerId)
    {
        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
        if (!system || playerId <= 0)
            return "";
        return system.GetPlayerIdentityId(playerId);
    }
}
