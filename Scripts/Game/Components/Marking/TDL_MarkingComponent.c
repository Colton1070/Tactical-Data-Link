[ComponentEditorProps(category: "TDL/Marking", description: "Paints replicated marking text onto the owner's $rendertarget face material")]
class TDL_MarkingComponentClass : ScriptComponentClass {}

//! Dynamic text marking (patch, name tape, band). The text is server-authoritative and
//! rides an RplProp, so JIP and late loaders get it for free; every client paints its own
//! RTTextureWidget bound to the owner mesh. The bind/shared toggles are left in for the
//! render-target measurements and cost nothing when left at their defaults.
//!
//! Preview-world copies (inventory / arsenal render) are skipped on purpose: a render
//! target does not resolve inside another render target, so binding one there only costs a
//! widget and never shows.
class TDL_MarkingComponent : ScriptComponent
{
    static const string LINE_SEPARATOR = "/";

    [Attribute("{17564B8C11410C4F}UI/layouts/TDL/TDL_PatchRT.layout", UIWidgets.ResourceNamePicker, "RT layout", "layout")]
    protected ResourceName m_sLayout;

    [Attribute("0", UIWidgets.ComboBox, "Marking slot this item renders. The player's marking record is keyed by slot, so a name tape and a sleeve patch carry different text", "", ParamEnumArray.FromEnum(TDL_EMarkingSlot))]
    protected TDL_EMarkingSlot m_eSlot;

    [Attribute("AG0", UIWidgets.EditBox, "Initial text. Use / to split into two rows, e.g. AG0/TEST")]
    protected string m_sDefaultText;

    [Attribute("0", UIWidgets.ComboBox, "AUTHORED: face has its own BCR/NMO, the render target is text only. PROCEDURAL: the face material is the render target and carries swatch + text", "", ParamEnumArray.FromEnum(TDL_EMarkingMode))]
    protected TDL_EMarkingMode m_eMode;

    [Attribute("", UIWidgets.ResourceNamePicker, "PROCEDURAL only: fabric swatch drawn under the text. Empty = flat Background colour", "edds imageset")]
    protected ResourceName m_sSwatch;

    [Attribute("0.16 0.17 0.12 1", UIWidgets.ColorPicker, "PROCEDURAL only: fabric colour when no swatch is set; tints the swatch when one is (alpha 0 = no tint)")]
    protected ref Color m_Background;

    [Attribute("0.86 0.82 0.66 1", UIWidgets.ColorPicker, "Thread")]
    protected ref Color m_Thread;

    [Attribute("1", UIWidgets.CheckBox, "Convert image and background colours from sRGB to linear before drawing. The render target is read as sRGB, so without this they come out washed out and bright")]
    protected bool m_bLinearCanvasColours;

    [Attribute("1", UIWidgets.CheckBox, "Bind the render target on init. Off = shows what an unbound $rendertarget face looks like")]
    protected bool m_bBindOnInit;

    [Attribute("0", UIWidgets.CheckBox, "Bind every instance to ONE shared widget instead of one widget each")]
    protected bool m_bUseSharedWidget;

    [RplProp(onRplName: "OnTextChanged")]
    protected string m_sText = "";

    //! Id of the image marking drawn under the text (a delivery id in the photo manager's
    //! decoded cache), "" for none. TEST_IMAGE_ID draws a local checkerboard so the canvas
    //! path can be checked with no API round trip.
    [RplProp(onRplName: "OnImageChanged")]
    protected string m_sImageId = "";

    //! Animated image: every frame id, comma-separated (m_sImageId is frame 0), and the hold
    //! time. Empty = still image.
    [RplProp(onRplName: "OnImageChanged")]
    protected string m_sImageFrames = "";
    [RplProp(onRplName: "OnImageChanged")]
    protected int m_iFrameMs = 0;

    //! Style colours from the player's record: "" = item default, "#rrggbb", or "rainbow".
    [RplProp(onRplName: "OnStyleChanged")]
    protected string m_sBg = "";
    [RplProp(onRplName: "OnStyleChanged")]
    protected string m_sFg = "";

    static const string TEST_IMAGE_ID = "test";
    protected static const int RAINBOW_TICK_MS = 50;
    protected static const float RAINBOW_DEG_PER_TICK = 4;
    protected static const int IMAGE_RETRY_MS = 1000;
    protected static const int IMAGE_RETRY_MAX = 90;

    protected Widget m_wRoot;
    protected RTTextureWidget m_wRT;
    protected TextWidget m_wLine1;
    protected TextWidget m_wLine2;
    protected CanvasWidget m_wImage;
    protected CanvasWidget m_wFill;
    //! One renderer per frame; each owns the command list the canvas is pointed at in turn.
    protected ref array<ref AG0_TDLPhotoRenderer> m_aFrameRenderers = {};
    protected ref array<ref AG0_TDLPhotoData> m_aPendingFramePhotos;
    protected int m_iFrameIndex;
    protected int m_iAnimGeneration;
    protected float m_fRainbowHue;
    protected bool m_bRainbowRunning;
    //! SetDrawCommands keeps a reference, not a copy: the array has to outlive the call.
    protected ref array<ref CanvasWidgetCommand> m_aFillCommands = {};
    protected ref array<ref CanvasWidgetCommand> m_aNoCommands = {};
    protected ref AG0_TDLPhotoRenderer m_ImageRenderer;
    protected string m_sPaintedImageId;
    protected int m_iImageRetries;
    protected bool m_bBound;

    protected static Widget s_wSharedRoot;
    protected static RTTextureWidget s_wSharedRT;
    protected static int s_iSharedUsers;
    protected static int s_iLiveWidgets;

    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        if (SCR_Global.IsEditMode())
            return;

        // The prefab default is only ever applied by the authority; clients receive it through
        // replication so a prefab edit can't desync a running server.
        if (Replication.IsServer())
        {
            m_sText = Sanitize(m_sDefaultText, m_eSlot);
            // Loadout items are spawned already attached; give the hierarchy a moment, then
            // ask the registry who holds us so a spawn-time patch says the right thing.
            GetGame().GetCallqueue().CallLater(ResolveHolder, 1000, false);
        }

        if (System.IsConsoleApp())
            return;
        if (owner.GetWorld() != GetGame().GetWorld())
            return;

        // Same deferral the world-space display uses: the mesh object isn't guaranteed to
        // exist for SetRenderTarget on the init frame.
        GetGame().GetCallqueue().CallLater(DeferredBind, 100, false, owner);
    }

    //------------------------------------------------------------------------------------------------
    //! Server: walk up to the character holding this item and let the registry stamp it.
    //! Also the hook for pickup — a patch taken off the ground re-resolves through here.
    void ResolveHolder()
    {
        if (!Replication.IsServer())
            return;
        IEntity owner = GetOwner();
        if (!owner)
            return;
        IEntity root = owner.GetRootParent();
        if (!root || root == owner)
            return;
        PlayerManager playerMgr = GetGame().GetPlayerManager();
        if (!playerMgr)
            return;
        int playerId = playerMgr.GetPlayerIdFromControlledEntity(root);
        if (playerId <= 0)
            return;
        TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
        if (reg)
            reg.OnItemHeldBy(playerId, this);
    }

    //------------------------------------------------------------------------------------------------
    protected void DeferredBind(IEntity owner)
    {
        if (!m_bBindOnInit)
            return;
        if (!GetOwner())
            return;
        Bind();
    }

    //------------------------------------------------------------------------------------------------
    //! Items have no entity name; the prefab is what identifies one in a log line.
    protected string OwnerName()
    {
        IEntity owner = GetOwner();
        if (!owner)
            return "?";
        EntityPrefabData prefabData = owner.GetPrefabData();
        if (prefabData)
            return prefabData.GetPrefabName();
        return owner.GetName();
    }

    //------------------------------------------------------------------------------------------------
    //! Wire/record key for this item's slot. The enum is the editor-facing pick list; the
    //! string is what the API record, the queue command and the registry are keyed by.
    string GetSlot()
    {
        return TDL_MarkingSlots.ToKey(m_eSlot);
    }

    //------------------------------------------------------------------------------------------------
    string GetText()
    {
        return m_sText;
    }

    //------------------------------------------------------------------------------------------------
    //! Server entry point. Clients reach it through SCR_PlayerController.RequestSetMarking,
    //! never directly: item components don't own RPCs.
    void SetText(string text)
    {
        if (!Replication.IsServer())
            return;

        string clean = Sanitize(text, m_eSlot);
        if (clean == m_sText)
            return;

        m_sText = clean;
        Replication.BumpMe();
        Paint();
    }

    //------------------------------------------------------------------------------------------------
    protected void OnTextChanged()
    {
        Paint();
    }

    //------------------------------------------------------------------------------------------------
    string GetImageId()
    {
        return m_sImageId;
    }

    //------------------------------------------------------------------------------------------------
    //! Server entry point, same rules as SetText. The ids are only keys: the pixels reach
    //! clients through the photo manager's chunk transfer, never through replication.
    //! `frames` is "" for a still, else every frame id comma-separated with `frameMs` hold.
    void SetImage(string imageId, string frames = "", int frameMs = 0)
    {
        if (!Replication.IsServer())
            return;
        if (imageId == m_sImageId && frames == m_sImageFrames && frameMs == m_iFrameMs)
            return;
        m_sImageId = imageId;
        m_sImageFrames = frames;
        m_iFrameMs = frameMs;
        Replication.BumpMe();
        Print(string.Format("[TDL_Marking] %1 image set to '%2' (%3 frame(s))", OwnerName(), imageId, Math.Max(1, frames.Length() / 21)), LogLevel.DEBUG);
        PaintImage();
    }

    //------------------------------------------------------------------------------------------------
    void SetImageId(string imageId)
    {
        SetImage(imageId, "", 0);
    }

    //------------------------------------------------------------------------------------------------
    //! Frame ids to draw, in order: the frame list when animated, else the single image.
    protected void FrameIds(out array<string> ids)
    {
        ids = {};
        if (!m_sImageFrames.IsEmpty())
            m_sImageFrames.Split(",", ids, true);
        if (ids.IsEmpty() && !m_sImageId.IsEmpty())
            ids.Insert(m_sImageId);
    }

    //------------------------------------------------------------------------------------------------
    //! What PaintImage has to see before it repaints: image + frames + timing as one key.
    protected string ImageKey()
    {
        return m_sImageId + "|" + m_sImageFrames + "|" + m_iFrameMs.ToString();
    }

    //------------------------------------------------------------------------------------------------
    protected void OnImageChanged()
    {
        PaintImage();
    }

    //------------------------------------------------------------------------------------------------
    string GetBg()
    {
        return m_sBg;
    }

    //------------------------------------------------------------------------------------------------
    string GetFg()
    {
        return m_sFg;
    }

    //------------------------------------------------------------------------------------------------
    //! Server entry point, same rules as SetText.
    void SetStyle(string bg, string fg)
    {
        if (!Replication.IsServer())
            return;
        if (bg == m_sBg && fg == m_sFg)
            return;
        m_sBg = bg;
        m_sFg = fg;
        Replication.BumpMe();
        Paint();
    }

    //------------------------------------------------------------------------------------------------
    protected void OnStyleChanged()
    {
        Paint();
    }

    //------------------------------------------------------------------------------------------------
    //! Cleared (or never set) shows the prefab's placeholder rather than a bare face.
    protected string DisplayText()
    {
        if (m_sText.IsEmpty())
            return Sanitize(m_sDefaultText, m_eSlot);
        return m_sText;
    }

    //------------------------------------------------------------------------------------------------
    //! Uppercase, row/column limits from the slot profile, charset limited to what embroidery
    //! would plausibly carry. Anything else is dropped rather than rejected so a stray
    //! character never leaves a player with an empty marking.
    static string Sanitize(string text, TDL_EMarkingSlot slot)
    {
        int maxRows;
        int maxCols;
        TDL_MarkingSlots.GetProfile(slot, maxRows, maxCols);

        array<string> rows = {};
        text.ToUpper();
        text.Split(LINE_SEPARATOR, rows, false);

        string built = "";
        int rowCount = 0;
        foreach (string row : rows)
        {
            string kept = "";
            for (int i = 0; i < row.Length(); i++)
            {
                string c = row.Get(i);
                int code = c.ToAscii();
                bool ok = (code >= 65 && code <= 90) || (code >= 48 && code <= 57) || code == 32 || code == 45 || code == 46;
                if (ok)
                    kept += c;
                if (kept.Length() >= maxCols)
                    break;
            }
            kept = kept.Trim();
            if (kept.IsEmpty())
                continue;
            if (rowCount > 0)
                built += LINE_SEPARATOR;
            built += kept;
            rowCount++;
            if (rowCount == maxRows)
                break;
        }
        return built;
    }

    //------------------------------------------------------------------------------------------------
    //! Create (or borrow) the RT widget, paint the current text, bind it to the owner mesh.
    void Bind()
    {
        if (m_bBound)
            return;

        IEntity owner = GetOwner();
        if (!owner)
            return;

        if (m_bUseSharedWidget)
        {
            if (!s_wSharedRT)
            {
                s_wSharedRoot = GetGame().GetWorkspace().CreateWidgets(m_sLayout);
                if (!s_wSharedRoot)
                {
                    Print("[TDL_Marking] shared layout failed to load: " + m_sLayout, LogLevel.ERROR);
                    return;
                }
                s_wSharedRT = RTTextureWidget.Cast(s_wSharedRoot.FindAnyWidget("RTTexture0"));
                s_iLiveWidgets++;
            }
            m_wRoot = s_wSharedRoot;
            m_wRT = s_wSharedRT;
            s_iSharedUsers++;
        }
        else
        {
            m_wRoot = GetGame().GetWorkspace().CreateWidgets(m_sLayout);
            if (!m_wRoot)
            {
                Print("[TDL_Marking] layout failed to load: " + m_sLayout, LogLevel.ERROR);
                return;
            }
            m_wRT = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture0"));
            s_iLiveWidgets++;
        }

        if (!m_wRT)
        {
            Print("[TDL_Marking] RTTexture0 missing from layout", LogLevel.ERROR);
            return;
        }

        m_wLine1 = TextWidget.Cast(m_wRoot.FindAnyWidget("Line1"));
        m_wLine2 = TextWidget.Cast(m_wRoot.FindAnyWidget("Line2"));
        m_wImage = CanvasWidget.Cast(m_wRoot.FindAnyWidget("Image"));
        m_wFill = CanvasWidget.Cast(m_wRoot.FindAnyWidget("Fill"));
        m_sPaintedImageId = "";
        Paint();

        m_wRT.SetRenderTarget(owner);
        m_bBound = true;
        // The canvas reports its size from the workspace, which is not laid out on the
        // frame the widgets are created; draw the image once the RT is live instead.
        GetGame().GetCallqueue().CallLater(PaintImage, 100, false);
        Print(string.Format("[TDL_Marking] bound %1 slot=%2 showing '%3' (live widgets: %4)", OwnerName(), GetSlot(), DisplayText(), s_iLiveWidgets), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    //! Release the binding and the widget. Safe to call twice.
    //! releaseFromMesh = false when the owner is already being torn down: RemoveRenderTarget
    //! on a deleted entity throws, and the mesh object it would clean goes away with the
    //! entity anyway. The widget itself is still dropped so nothing keeps rendering.
    void Unbind(bool releaseFromMesh = true)
    {
        IEntity owner = GetOwner();
        if (releaseFromMesh && m_wRT && owner && m_bBound)
            m_wRT.RemoveRenderTarget(owner);
        m_bBound = false;

        if (!m_wRoot)
            return;

        if (m_bUseSharedWidget)
        {
            s_iSharedUsers--;
            if (s_iSharedUsers <= 0 && s_wSharedRoot)
            {
                s_wSharedRoot.RemoveFromHierarchy();
                s_wSharedRoot = null;
                s_wSharedRT = null;
                s_iLiveWidgets--;
            }
        }
        else
        {
            m_wRoot.RemoveFromHierarchy();
            s_iLiveWidgets--;
        }
        m_wRoot = null;
        m_wRT = null;
        m_wLine1 = null;
        m_wLine2 = null;
        m_wImage = null;
        m_wFill = null;
        m_iAnimGeneration++;
        m_aFrameRenderers.Clear();
        m_ImageRenderer = null;
        m_sPaintedImageId = "";
        m_bRainbowRunning = false;
    }

    //------------------------------------------------------------------------------------------------
    protected void Paint()
    {
        if (!m_wRoot)
            return;

        array<string> rows = {};
        string shown = DisplayText();
        shown.Split(LINE_SEPARATOR, rows, false);
        string line1 = "";
        string line2 = "";
        if (rows.Count() > 0)
            line1 = rows[0];
        if (rows.Count() > 1)
            line2 = rows[1];
        bool twoRows = !line2.IsEmpty();

        PaintSwatch(ImageWidget.Cast(m_wRoot.FindAnyWidget("Swatch")));
        PaintFill();

        Color threadColor = TextColor();
        if (m_wLine1)
        {
            m_wLine1.SetText(line1);
            m_wLine1.SetColor(threadColor);
            // Most cell tags are a single 3-5 character callsign, so one row gets the whole
            // face; two rows split it.
            if (twoRows)
            {
                FrameSlot.SetSize(m_wLine1, 400, 104);
                m_wLine1.SetExactFontSize(FitFontSize(line1, 90));
            }
            else
            {
                FrameSlot.SetSize(m_wLine1, 400, 208);
                m_wLine1.SetExactFontSize(FitFontSize(line1, 160));
            }
        }
        if (m_wLine2)
        {
            m_wLine2.SetText(line2);
            m_wLine2.SetColor(threadColor);
            m_wLine2.SetExactFontSize(FitFontSize(line2, 90));
            m_wLine2.SetVisible(twoRows);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Text colour: the style's fg when it is a hex, the current rainbow hue when animating,
    //! else the prefab thread colour.
    protected Color TextColor()
    {
        if (m_sFg == TDL_MarkingColors.RAINBOW)
            return TDL_MarkingColors.FromArgb(TDL_MarkingColors.FromHue(m_fRainbowHue + 180));
        int argb;
        if (TDL_MarkingColors.FromHex(m_sFg, argb))
            return TDL_MarkingColors.FromArgb(argb);
        return m_Thread;
    }

    //------------------------------------------------------------------------------------------------
    //! Flat background from the style's bg, drawn as one quad on the Fill canvas over the
    //! swatch and under the image and text. A canvas is used rather than tinting the swatch
    //! so a chosen colour replaces the fabric instead of multiplying with it.
    protected void PaintFill()
    {
        if (!m_wFill)
            return;
        bool rainbow = (m_sBg == TDL_MarkingColors.RAINBOW) || (m_sFg == TDL_MarkingColors.RAINBOW);
        int argb;
        bool flat = TDL_MarkingColors.FromHex(m_sBg, argb);
        if (m_sBg == TDL_MarkingColors.RAINBOW)
            argb = TDL_MarkingColors.FromHue(m_fRainbowHue);
        if (m_bLinearCanvasColours)
            argb = TDL_MarkingColors.ToLinear(argb);

        // Never hidden: a hidden widget has no layout size, so it could never be measured
        // for the quad. No background = an empty command list.
        m_wFill.SetVisible(true);
        if (!flat && m_sBg != TDL_MarkingColors.RAINBOW)
        {
            m_aFillCommands.Clear();
            m_wFill.SetDrawCommands(m_aNoCommands);
        }
        else
        {
            float w;
            float h;
            m_wFill.GetScreenSize(w, h);
            if (w <= 0 || h <= 0)
            {
                // Not laid out yet; the next Paint (or rainbow tick) gets it.
                if (!rainbow)
                    GetGame().GetCallqueue().CallLater(PaintFill, 100, false);
            }
            else
            {
                array<float> verts = new array<float>();
                verts.Insert(0);     verts.Insert(0);
                verts.Insert(w);     verts.Insert(0);
                verts.Insert(w);     verts.Insert(h);
                verts.Insert(0);     verts.Insert(h);
                array<int> idxs = new array<int>();
                idxs.Insert(0); idxs.Insert(1); idxs.Insert(2);
                idxs.Insert(0); idxs.Insert(2); idxs.Insert(3);
                TriMeshDrawCommand cmd = new TriMeshDrawCommand();
                cmd.m_iColor = argb;
                cmd.m_Vertices = verts;
                cmd.m_Indices = idxs;
                m_aFillCommands.Clear();
                m_aFillCommands.Insert(cmd);
                m_wFill.SetDrawCommands(m_aFillCommands);
                if (!rainbow)
                    Print(string.Format("[TDL_Marking] fill %1 on %2", m_sBg, OwnerName()), LogLevel.DEBUG);
            }
        }

        if (rainbow && !m_bRainbowRunning)
        {
            m_bRainbowRunning = true;
            GetGame().GetCallqueue().CallLater(RainbowTick, RAINBOW_TICK_MS, false);
        }
        if (!rainbow)
            m_bRainbowRunning = false;
    }

    //------------------------------------------------------------------------------------------------
    //! One step round the hue wheel; re-arms itself while either colour is "rainbow".
    protected void RainbowTick()
    {
        if (!m_bRainbowRunning || !m_wRoot)
        {
            m_bRainbowRunning = false;
            return;
        }
        m_fRainbowHue += RAINBOW_DEG_PER_TICK;
        if (m_fRainbowHue >= 360)
            m_fRainbowHue -= 360;
        m_bRainbowRunning = false;
        // Only the colour-bearing widgets; a full Paint would reload the swatch every tick.
        PaintFill();
        Color threadColor = TextColor();
        if (m_wLine1)
            m_wLine1.SetColor(threadColor);
        if (m_wLine2)
            m_wLine2.SetColor(threadColor);
    }

    //------------------------------------------------------------------------------------------------
    //! AUTHORED leaves the background fully transparent so the overlay quad adds glyphs only;
    //! PROCEDURAL paints the fabric (swatch image tinted by Background, or Background alone).
    protected void PaintSwatch(ImageWidget image)
    {
        if (!image)
            return;
        if (m_eMode == TDL_EMarkingMode.AUTHORED)
        {
            image.SetVisible(false);
            return;
        }
        image.SetVisible(true);
        if (m_sSwatch.IsEmpty())
        {
            image.SetColor(m_Background);
            return;
        }
        if (!image.LoadImageTexture(0, m_sSwatch))
            Print("[TDL_Marking] swatch failed to load: " + m_sSwatch, LogLevel.WARNING);
        // Background is a tint here; a fully transparent one (the natural way to say "no
        // tint" on the prefab) would erase the swatch, so treat it as untinted instead.
        if (m_Background.A() <= 0)
            image.SetColor(Color.White);
        else
            image.SetColor(m_Background);
    }

    //------------------------------------------------------------------------------------------------
    //! Draw the image marking into the canvas between swatch and text. The decoded pixels
    //! live in the photo manager cache under each frame id; they can land after the ids do
    //! (chunk transfer still running), so a miss re-polls once a second for a while. An
    //! animation needs every frame before it starts, so the loop never shows a gap.
    protected void PaintImage()
    {
        if (!m_wImage)
            return;

        if (m_sImageId.IsEmpty())
        {
            if (!m_sPaintedImageId.IsEmpty() || m_ImageRenderer || !m_aFrameRenderers.IsEmpty())
                ClearImage();
            return;
        }
        string key = ImageKey();
        if (key == m_sPaintedImageId)
            return;

        array<string> ids;
        FrameIds(ids);
        array<ref AG0_TDLPhotoData> photos = {};
        AG0_TDLPhotoManager manager = AG0_TDLPhotoManager.GetActiveInstance();
        foreach (string id : ids)
        {
            AG0_TDLPhotoData photo;
            if (id == TEST_IMAGE_ID)
                photo = AG0_TDLPhotoData.CreateTestCheckerboardRects(64, 8);
            else if (manager)
                photo = manager.GetDecodedPhoto(id);
            if (!photo)
                break;
            photos.Insert(photo);
        }

        if (photos.Count() < ids.Count() || photos.IsEmpty())
        {
            if (m_iImageRetries >= IMAGE_RETRY_MAX)
            {
                Print(string.Format("[TDL_Marking] gave up waiting for image %1 on %2: frame %3 of %4 never decoded here", m_sImageId, OwnerName(), photos.Count() + 1, ids.Count()), LogLevel.WARNING);
                return;
            }
            if (m_iImageRetries == 0 || m_iImageRetries == 10 || m_iImageRetries == 30)
                Print(string.Format("[TDL_Marking] waiting for image %1 on %2: %3 of %4 frame(s) decoded (try %5)", m_sImageId, OwnerName(), photos.Count(), ids.Count(), m_iImageRetries), LogLevel.DEBUG);
            m_iImageRetries++;
            GetGame().GetCallqueue().CallLater(PaintImage, IMAGE_RETRY_MS, false);
            return;
        }
        m_iImageRetries = 0;

        // Canvas draw commands are in physical pixels while the slot is in logical units, so
        // the renderer has to size itself from GetScreenSize (it does, on Init and Draw).
        // That is 0 until the RT subtree has had a layout pass; wait for it like a missing
        // photo rather than drawing at the wrong scale.
        float screenW;
        float screenH;
        m_wImage.SetVisible(true);   // hidden by ClearImage; must be visible to have a size
        m_wImage.GetScreenSize(screenW, screenH);
        if (screenW <= 0 || screenH <= 0)
        {
            if (m_iImageRetries >= IMAGE_RETRY_MAX)
                return;
            m_iImageRetries++;
            GetGame().GetCallqueue().CallLater(PaintImage, IMAGE_RETRY_MS, false);
            return;
        }

        // Build every frame's command list once, one frame per game frame so a 16-frame
        // loop never lands in a single hitch; the ticker then only swaps which list the
        // canvas points at. The first frame shows as soon as it is built.
        m_iAnimGeneration++;
        m_aFrameRenderers.Clear();
        m_ImageRenderer = null;
        m_aPendingFramePhotos = photos;
        m_sPaintedImageId = key;
        BuildNextFrame(m_iAnimGeneration);
    }

    //------------------------------------------------------------------------------------------------
    protected void BuildNextFrame(int generation)
    {
        if (generation != m_iAnimGeneration || !m_wImage || !m_aPendingFramePhotos)
            return;
        int index = m_aFrameRenderers.Count();
        if (index >= m_aPendingFramePhotos.Count())
        {
            m_aPendingFramePhotos = null;
            int commands = 0;
            foreach (AG0_TDLPhotoRenderer r : m_aFrameRenderers)
                commands += r.GetCommandCount();
            Print(string.Format("[TDL_Marking] image %1 drawn on %2 (%3 frame(s), %4 commands)", m_sImageId, OwnerName(), m_aFrameRenderers.Count(), commands), LogLevel.DEBUG);
            if (m_aFrameRenderers.Count() > 1)
            {
                int hold = m_iFrameMs;
                if (hold < 50)
                    hold = 100;
                GetGame().GetCallqueue().CallLater(FrameTick, hold, false, generation);
            }
            return;
        }

        AG0_TDLPhotoData photo = m_aPendingFramePhotos[index];
        if (m_bLinearCanvasColours)
            photo = LinearizedCopy(photo);
        AG0_TDLPhotoRenderer renderer = new AG0_TDLPhotoRenderer();
        if (!renderer.Init(m_wImage))
        {
            m_aPendingFramePhotos = null;
            return;
        }
        renderer.SetFitMode(AG0_TDLPhotoFitMode.CONTAIN);
        // Always the sync build: the incremental path hands its result to the canvas
        // itself when it finishes, which would fight the frame swap.
        renderer.SetIncrementalParams(2000, 1000000);
        renderer.SetPhotoData(photo);
        renderer.Draw();
        m_aFrameRenderers.Insert(renderer);
        if (index == 0)
        {
            m_ImageRenderer = renderer;
            m_iFrameIndex = 0;
            m_wImage.SetDrawCommands(renderer.GetDrawCommands());
        }
        GetGame().GetCallqueue().CallLater(BuildNextFrame, 0, false, generation);
    }

    //------------------------------------------------------------------------------------------------
    //! Same geometry, palette converted to linear; the decoded photo in the cache stays
    //! sRGB for anything else that draws it (the message UI draws to the screen, not an RT).
    protected AG0_TDLPhotoData LinearizedCopy(AG0_TDLPhotoData src)
    {
        AG0_TDLPhotoData copy = new AG0_TDLPhotoData();
        copy.m_iWidth = src.m_iWidth;
        copy.m_iHeight = src.m_iHeight;
        copy.m_aPixels = src.m_aPixels;
        copy.m_aRects = src.m_aRects;
        copy.m_aTris = src.m_aTris;
        copy.m_iTriCount = src.m_iTriCount;
        copy.m_aPalette = new array<int>();
        foreach (int c : src.m_aPalette)
            copy.m_aPalette.Insert(TDL_MarkingColors.ToLinear(c));
        return copy;
    }

    //------------------------------------------------------------------------------------------------
    //! Next frame; re-arms itself until the image changes (generation) or the widget goes.
    protected void FrameTick(int generation)
    {
        if (generation != m_iAnimGeneration || !m_wImage || m_aFrameRenderers.Count() < 2)
            return;
        m_iFrameIndex = (m_iFrameIndex + 1) % m_aFrameRenderers.Count();
        m_wImage.SetDrawCommands(m_aFrameRenderers[m_iFrameIndex].GetDrawCommands());
        int hold = m_iFrameMs;
        if (hold < 50)
            hold = 100;
        GetGame().GetCallqueue().CallLater(FrameTick, hold, false, generation);
    }

    //------------------------------------------------------------------------------------------------
    protected void ClearImage()
    {
        if (!m_wImage)
            return;
        m_wImage.SetDrawCommands(m_aNoCommands);
        m_wImage.SetVisible(false);
        m_iAnimGeneration++;
        m_aPendingFramePhotos = null;
        m_aFrameRenderers.Clear();
        m_ImageRenderer = null;
        m_sPaintedImageId = "";
        m_iImageRetries = 0;
    }

    //------------------------------------------------------------------------------------------------
    //! SetExactFontSize switches off the layout's shrink-to-fit, so fit by hand: Roboto
    //! Condensed Bold caps average ~0.58 em advance, and the row is 400 px wide.
    protected static int FitFontSize(string text, int maxSize)
    {
        int len = text.Length();
        if (len <= 0)
            return maxSize;
        int fit = (400 * 100) / (58 * len);
        if (fit < maxSize)
            return fit;
        return maxSize;
    }

    //------------------------------------------------------------------------------------------------
    override void OnDelete(IEntity owner)
    {
        Unbind(false);
        super.OnDelete(owner);
    }

#ifdef WORKBENCH
    protected int m_iWbFramesSeen;

    //------------------------------------------------------------------------------------------------
    //! Edit mode has no replication and no init events, so bind from the editor tick once
    //! the workspace is up; the face then shows the prefab's Initial text so the mesh, UVs
    //! and material can be judged in the prefab editor without a play session.
    override void _WB_AfterWorldUpdate(IEntity owner, float timeSlice)
    {
        if (m_bBound || !m_bBindOnInit)
            return;
        m_iWbFramesSeen++;
        if (m_iWbFramesSeen < 5)
            return;
        if (!GetGame() || !GetGame().GetWorkspace())
            return;
        Bind();
    }

    //------------------------------------------------------------------------------------------------
    //! Repaint on any attribute edit so text, slot and colours update live in the editor.
    override bool _WB_OnKeyChanged(IEntity owner, BaseContainer src, string key, BaseContainerList ownerContainers, IEntity parent)
    {
        m_sText = "";
        Paint();
        PaintImage();
        return false;
    }
#endif
}
