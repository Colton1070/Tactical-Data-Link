//------------------------------------------------------------------------------------------------
// AG0_TDLRadialMenu.c
//
// The TDL map's context wheel. Right-click on mouse, X on a pad, and the entries act on
// whatever the map is pointed at — the cursor in 2D, the crosshair in 3D, the device cursor
// on the EUD.
//
// It exists because every map action currently costs a trip to the side panel, which is a
// couple of clicks on a mouse and a genuine expedition on a controller. A wheel puts the
// common ones one press away on both, and ATAK has already proved the shape works under
// gloves and stress: a ring of large targets around the point you are already looking at,
// no precise aiming required.
//
// This is TDL's own wheel, not the vanilla one. SCR_RadialMenuController cannot be built from
// script — its m_Data and m_RMControls are config-authored attributes, so a constructed one
// dies on first open — and the menu those wrap belongs to SCR_RadialMenuGameModeComponent,
// which is simply absent from the game modes TDL runs on. Depending on a component the host
// mission may not have is no foundation for a core interaction, so the wheel is drawn here
// with the same CanvasWidget draw commands the map itself is painted with.
//
// Placement walks a tree rather than firing on the first press, matching how ATAK asks for a
// marker: what kind, then whose, then what dimension, then what it is. Every step reads its
// choices from the marker tool's own selectors, so the wheel and the panel can never disagree
// about what a marker means.
//
// Geometry is carried in physical pixels throughout, which is the space canvas draw commands
// and widget screen positions both speak. FrameSlot is the one boundary that wants layout
// units, so DPIUnscale happens there and nowhere else.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Which page of the tree is showing. Option pages map one-to-one onto a marker tool
//! selector; see OptionForPage.
//------------------------------------------------------------------------------------------------
enum ETDLRadialPage
{
    ROOT,
    PLACE_TYPE,
    MIL_FACTION,
    MIL_DIMENSION,
    MIL_TYPE,
    CUSTOM_ICON,
    CUSTOM_COLOR,
    SHAPE_TOOL,
    SHAPE_COLOR
}

//------------------------------------------------------------------------------------------------
//! What an entry does when performed. Carried as an id rather than a closure because
//! Enfusion cannot take a method pointer as a parameter, so the dispatch is a switch.
//------------------------------------------------------------------------------------------------
enum ETDLRadialAction
{
    OPEN_PAGE,
    PICK_OPTION,
    SHOW_MORE,
    GO_BACK,
    DELETE_AT_POINT,
    OPEN_MARKER_TOOL,
    OPEN_DRAW_TOOL,
    TOGGLE_MAP3D,
    TOGGLE_BLOODHOUND,
    CENTRE_ON_PLAYER,
    PIN_RANGE_BEARING,
    OPEN_MEMBER_DETAIL,
    OPEN_MEMBER_CHAT
}

//------------------------------------------------------------------------------------------------
//! One slice: what it says, what it does, the number that qualifies it, and the widget
//! showing its label.
//------------------------------------------------------------------------------------------------
class AG0_TDLRadialEntry
{
    ETDLRadialAction m_eAction;
    string m_sLabel;

    //! Page to open for OPEN_PAGE, selector index for PICK_OPTION. Unused otherwise.
    int m_iPayload;

    //! Set when the choice has a picture of its own. An icon that only ever appears as the
    //! word "tdl_checkpoint" is not a choice anyone can make at a glance.
    ResourceName m_sImageset;
    string m_sQuad;

    //! Set when the choice is a colour, so it can be shown as the colour. Packed ARGB rather
    //! than a Color: that is a ref-counted class, and a plain member holding one is a weak
    //! handle whose instance goes away between the ring being built and being drawn.
    bool m_bHasColor;
    int m_iColor;

    //! Shown but refused. ATAK greys entries rather than dropping them so a ring keeps the same
    //! shape whatever the target's state — an operator who has learned "Delete is the top slice"
    //! should not find Place there instead because this particular marker is not theirs.
    bool m_bDisabled;

    TextWidget m_wLabel;
    ImageWidget m_wIcon;
}

//------------------------------------------------------------------------------------------------
class AG0_TDLRadialMenu
{
    //------------------------------------------------------------------------------------------------
    // GEOMETRY — physical pixels
    //------------------------------------------------------------------------------------------------

    //! The ring is sized to the surface it opens on rather than fixed, because the same wheel
    //! is drawn on a 4K monitor and on the EUD's render target, and a radius that reads well
    //! on one is either a postage stamp or the whole screen on the other.
    protected static const float RING_SCREEN_FRACTION = 0.32;
    protected static const float RING_OUTER_MAX_PX = 260.0;
    protected static const float RING_OUTER_MIN_PX = 130.0;

    //! The hole in the middle is not decoration: it is the cancel zone, and it has to be big
    //! enough that a resting stick or an unmoved mouse lands in it rather than on a slice.
    protected static const float RING_INNER_RATIO = 0.35;

    //! Labels sit nearer the inner edge than the outer one. A label centred in the band runs
    //! out of room first on the slices pointing left and right, where the ring is at its
    //! narrowest measured horizontally — which is exactly where the text was overflowing.
    protected static const float RING_LABEL_RATIO = 0.66;

    //! Arc is drawn as a strip of quads because a ring segment is not convex and a single
    //! polygon command would fill across the hole.
    protected static const float SECTOR_STEP_DEG = 6.0;

    //! Gap between neighbouring slices, in degrees, so the ring reads as separate targets
    //! rather than one disc.
    protected static const float SLICE_GAP_DEG = 1.6;

    //! Breathing room between a label and the rim its box is measured against.
    protected static const float LABEL_PAD_PX = 12.0;
    protected static const float LABEL_MIN_W_PX = 56.0;

    //! Icon box, as a share of the ring band. Big enough to read a 128px marker glyph at a
    //! glance, small enough that its label still fits under it.
    //! Sized for a slice the icon has to itself — it shares its band with no text, so a smaller
    //! glyph would read as a mark floating in a large empty wedge.
    protected static const float ICON_ONLY_SIZE_RATIO = 0.30;
    protected static const float ICON_ONLY_SIZE_MIN_PX = 40.0;
    protected static const float ICON_OPACITY_IDLE = 0.75;
    protected static const float ICON_OPACITY_OFF = 0.30;

    //! Font tracks the ring so the two shrink together on a small surface.
    protected static const float LABEL_FONT_RATIO = 0.074;
    protected static const int LABEL_FONT_MIN_PX = 12;
    protected static const int LABEL_FONT_MAX_PX = 20;

    //! Past this the slices get too thin to hit, so the rest go behind a More entry.
    protected static const int MAX_SLICES = 12;

    //------------------------------------------------------------------------------------------------
    // COLOURS — ARGB, matching the map's own palette
    //------------------------------------------------------------------------------------------------
    protected static const int COLOR_SLICE_IDLE = 0xCC101C24;
    protected static const int COLOR_SLICE_HOT = 0xE61E5A66;
    protected static const int COLOR_DIVIDER = 0x55A8B4BE;
    protected static const int COLOR_RIM = 0x664DE6E6;
    protected static const int COLOR_RIM_HOT = 0xFF4DE6E6;
    protected static const int COLOR_HUB = 0xE60A1016;
    protected static const int COLOR_HUB_RING = 0x884DE6E6;
    protected static const int COLOR_LABEL_IDLE = 0xFFB4BEC8;
    protected static const int COLOR_LABEL_HOT = 0xFFFFFFFF;
    //! Dim enough to read as unavailable at a glance without becoming invisible — the operator
    //! still needs to see that the entry exists and count slices past it.
    protected static const int COLOR_LABEL_OFF = 0xFF5A646E;
    protected static const int COLOR_SLICE_OFF = 0x99101C24;
    protected static const int COLOR_HUB_TEXT = 0xFF4DE6E6;

    protected static const float RIM_WIDTH_PX = 1.5;
    protected static const float RIM_WIDTH_HOT_PX = 2.5;

    //------------------------------------------------------------------------------------------------
    // SELECTION
    //------------------------------------------------------------------------------------------------

    //! Below this the stick is not pointing anywhere in particular, so nothing is selected.
    //! A wheel that latched a slice the instant it opened would fire whatever happened to sit
    //! under the resting stick.
    protected static const float SELECT_DEADZONE = 0.35;

    //! Pointer travel before the mouse counts as aiming, as a share of the inner radius, so
    //! the cancel hole and the mouse dead zone stay the same gesture at any ring size.
    protected static const float MOUSE_SELECT_MIN_RATIO = 0.65;

    //! Ticks the wheel keeps hold of the confirm button after a press dismisses it.
    protected static const int DRAIN_TICKS = 2;

    //------------------------------------------------------------------------------------------------
    protected AG0_TDLMenuController m_Owner;

    protected CanvasWidget m_wCanvas;
    protected TextWidget m_wHubLabel;
    protected ref array<ref CanvasWidgetCommand> m_aDrawCommands = {};
    protected ref array<ref AG0_TDLRadialEntry> m_aEntries = {};

    //! Pages visited to get here, so Back retraces rather than guessing a parent.
    protected ref array<int> m_aPageStack = {};

    protected ETDLRadialPage m_ePage = ETDLRadialPage.ROOT;
    protected bool m_bOpen;
    protected int m_iSelected = -1;

    //! First option index shown on a paged option ring — see SHOW_MORE.
    protected int m_iOptionOffset;

    //! Confirm is edge-triggered off a held action. Without the latch, the press that opened
    //! the wheel would still be down on the first tick and immediately pick a slice.
    protected bool m_bConfirmLatched;

    //! Counts down once a press has dismissed the wheel. The map's click handlers act on
    //! release rather than press, so handing the button back on the release frame would let
    //! the press that chose a slice also land as a map click. Counting past the release makes
    //! that independent of which of the two runs first in a frame.
    protected int m_iDrainTicks;

    //! Captured when the wheel opens rather than read when an entry fires. The cursor keeps
    //! moving while it is up — on a pad the same stick that picks a slice would otherwise
    //! drag the target point out from under the action. It also survives every page of the
    //! tree, so a marker lands where the operator aimed on the first press.
    protected vector m_vOpenWorldPos;

    //! What the wheel was opened over, resolved once at open. Held rather than re-picked per
    //! frame because the ring must not change under the operator's thumb while they are aiming
    //! at a slice — and because the map can pan beneath an open wheel.
    protected ref AG0_TDLMapPickResult m_OpenPick;

    //! The aim point that named that world position, in MapCanvas-local physical pixels.
    protected float m_fAimCanvasX;
    protected float m_fAimCanvasY;

    //! Pointer position at open, in physical pixels — see ResolveMouseAim. Kept raw, with
    //! the edge-clamp shift applied on read, because the wheel re-centres on every page of
    //! the tree and folding the shift into the stored value would compound it per page.
    protected float m_fOpenMouseX;
    protected float m_fOpenMouseY;

    //! How far the edge clamp pushed the wheel off the aim point this page.
    protected float m_fClampShiftX;
    protected float m_fClampShiftY;

    //! Wheel centre and the frontend's extent, in the frontend root's local physical pixels.
    protected float m_fCentreX;
    protected float m_fCentreY;
    protected float m_fRootW;
    protected float m_fRootH;

    //! Ring measurements for this open, derived from the surface — see RING_SCREEN_FRACTION.
    protected float m_fRingOuter;
    protected float m_fRingInner;
    protected float m_fLabelRadius;
    protected int m_iLabelFont;

    //------------------------------------------------------------------------------------------------
    void AG0_TDLRadialMenu(AG0_TDLMenuController owner)
    {
        m_Owner = owner;
    }

    //------------------------------------------------------------------------------------------------
    bool IsOpen()
    {
        return m_bOpen;
    }

    //------------------------------------------------------------------------------------------------
    //! True while the wheel owns the confirm button, including the tail of the press that
    //! chose an entry. Every consumer of a click asks this rather than IsOpen, so a confirm
    //! cannot leak through once the wheel has already dismissed itself.
    bool IsConsumingClicks()
    {
        return m_bOpen || m_iDrainTicks > 0;
    }

    //------------------------------------------------------------------------------------------------
    //! Open over a world position, drawn around the canvas-local point that named it. A
    //! second press while open closes instead, so the same button both raises and dismisses
    //! it.
    //!
    //! The caller supplies both because only it knows which pointer named the target — the
    //! crosshair on a pad, the mouse on a desk, the device cursor on the EUD — and the wheel
    //! has to appear where the operator was already looking.
    void Toggle(vector worldPos, float canvasX, float canvasY)
    {
        if (!m_Owner)
            return;

        if (m_bOpen)
        {
            Close();
            return;
        }

        m_vOpenWorldPos = worldPos;
        m_OpenPick = m_Owner.ResolveTargetAt(worldPos);
        m_fAimCanvasX = canvasX;
        m_fAimCanvasY = canvasY;

        int mouseX;
        int mouseY;
        WidgetManager.GetMousePos(mouseX, mouseY);
        m_fOpenMouseX = mouseX;
        m_fOpenMouseY = mouseY;

        // Latched closed so the press that opened the wheel cannot also confirm on it.
        m_bConfirmLatched = true;
        m_aPageStack.Clear();

        OpenPage(ETDLRadialPage.ROOT);
    }

    //------------------------------------------------------------------------------------------------
    void Close()
    {
        DestroyWidgets();
        m_aEntries.Clear();
        m_aPageStack.Clear();
        m_ePage = ETDLRadialPage.ROOT;
        m_iSelected = -1;
        m_iOptionOffset = 0;
        m_bOpen = false;
    }

    //------------------------------------------------------------------------------------------------
    //! Dismiss on a Back press, reporting whether there was anything to dismiss. A press
    //! inside the tree steps back one page instead of throwing the whole wheel away.
    //!
    //! Back is resolved by the frontend rather than in Tick because the frontend's own Back
    //! chain runs in the same frame: if the wheel closed itself first, the menu would read
    //! the same press as still unhandled and walk out a level behind it.
    bool CloseOnBack()
    {
        if (!m_bOpen)
            return false;

        if (!m_aPageStack.IsEmpty())
        {
            GoBack();
            return true;
        }

        Close();
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void DestroyWidgets()
    {
        foreach (AG0_TDLRadialEntry entry : m_aEntries)
        {
            if (entry.m_wLabel)
                entry.m_wLabel.RemoveFromHierarchy();

            if (entry.m_wIcon)
                entry.m_wIcon.RemoveFromHierarchy();

            entry.m_wLabel = null;
            entry.m_wIcon = null;
        }

        if (m_wHubLabel)
            m_wHubLabel.RemoveFromHierarchy();

        m_wHubLabel = null;

        if (m_wCanvas)
            m_wCanvas.RemoveFromHierarchy();

        m_wCanvas = null;
        m_aDrawCommands.Clear();
    }

    //------------------------------------------------------------------------------------------------
    //! Build and draw the wheel for a page, centred on wherever the operator is aiming.
    protected void OpenPage(ETDLRadialPage page)
    {
        DestroyWidgets();
        m_aEntries.Clear();
        m_iSelected = -1;
        m_ePage = page;

        Widget frontendRoot = m_Owner.GetRoot();
        if (!frontendRoot)
            return;

        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
            return;

        BuildEntries();
        if (m_aEntries.IsEmpty())
        {
            Close();
            return;
        }

        ResolveCentre(frontendRoot);

        // IGNORE_CURSOR | NOFOCUS on everything the wheel creates: it sits over the map
        // surface, and anything above that surface which accepts the cursor swallows the drag
        // the map needs. The wheel reads input itself rather than being clicked. BLEND
        // because every fill in it is translucent — the map has to stay legible underneath.
        WidgetFlags flags = WidgetFlags.VISIBLE | WidgetFlags.BLEND
            | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS;

        m_wCanvas = CanvasWidget.Cast(workspace.CreateWidget(WidgetType.CanvasWidgetTypeID,
            flags, new Color(1, 1, 1, 1), 0, frontendRoot));
        if (!m_wCanvas)
            return;

        FrameSlot.SetPos(m_wCanvas, 0, 0);
        FrameSlot.SetSize(m_wCanvas, workspace.DPIUnscale(m_fRootW), workspace.DPIUnscale(m_fRootH));

        CreateHubLabel(workspace, frontendRoot, flags);

        int count = m_aEntries.Count();
        for (int i = 0; i < count; i = i + 1)
        {
            CreateSliceLabel(workspace, frontendRoot, flags, i, count);
        }

        m_bOpen = true;
        Repaint();
    }

    //------------------------------------------------------------------------------------------------
    //! Wheel centre, in the frontend root's local physical pixels. Opens on the aim point so
    //! the wheel appears where the operator was already looking rather than dragging their
    //! eye to the middle of the screen. Falls back to the root's centre if the canvas the aim
    //! point was measured against cannot be found.
    protected void ResolveCentre(Widget frontendRoot)
    {
        float rootX;
        float rootY;
        frontendRoot.GetScreenPos(rootX, rootY);
        frontendRoot.GetScreenSize(m_fRootW, m_fRootH);

        m_fCentreX = m_fRootW * 0.5;
        m_fCentreY = m_fRootH * 0.5;

        ResolveRingSize();

        Widget canvasWidget = frontendRoot.FindAnyWidget("MapCanvas");
        if (canvasWidget)
        {
            float canvasX;
            float canvasY;
            canvasWidget.GetScreenPos(canvasX, canvasY);

            m_fCentreX = canvasX - rootX + m_fAimCanvasX;
            m_fCentreY = canvasY - rootY + m_fAimCanvasY;
        }

        // Pulled back inside the frontend when the aim point sits near an edge. An unclamped
        // wheel would push half its slices past the screen, where they can still be selected
        // by angle but not read.
        float margin = m_fRingOuter + 8;

        float unclampedX = m_fCentreX;
        float unclampedY = m_fCentreY;

        if (m_fRootW > margin * 2)
            m_fCentreX = Math.Clamp(m_fCentreX, margin, m_fRootW - margin);

        if (m_fRootH > margin * 2)
            m_fCentreY = Math.Clamp(m_fCentreY, margin, m_fRootH - margin);

        // Mouse selection measures travel from where the pointer was when the wheel opened,
        // and that origin only coincides with the wheel centre while the clamp is idle.
        // Recording the shift keeps the slice under the pointer the slice that lights up when
        // the wheel has been pushed off the aim point.
        m_fClampShiftX = m_fCentreX - unclampedX;
        m_fClampShiftY = m_fCentreY - unclampedY;
    }

    //------------------------------------------------------------------------------------------------
    //! Fit the ring to the shorter side of the surface, then derive everything else from it so
    //! the proportions hold whatever size it lands at.
    protected void ResolveRingSize()
    {
        float shortSide = m_fRootW;
        if (m_fRootH < shortSide)
            shortSide = m_fRootH;

        m_fRingOuter = shortSide * RING_SCREEN_FRACTION;
        if (m_fRingOuter > RING_OUTER_MAX_PX)
            m_fRingOuter = RING_OUTER_MAX_PX;
        if (m_fRingOuter < RING_OUTER_MIN_PX)
            m_fRingOuter = RING_OUTER_MIN_PX;

        m_fRingInner = m_fRingOuter * RING_INNER_RATIO;
        m_fLabelRadius = m_fRingOuter * RING_LABEL_RATIO;

        m_iLabelFont = Math.Clamp(m_fRingOuter * LABEL_FONT_RATIO,
            LABEL_FONT_MIN_PX, LABEL_FONT_MAX_PX);
    }

    //------------------------------------------------------------------------------------------------
    // DRAWING
    //------------------------------------------------------------------------------------------------

    //! Rebuild the whole command list. Cheap enough to redo on every selection change — the
    //! ring is a few hundred triangles, against a map that draws thousands — and rebuilding
    //! wholesale means the highlight can never drift out of step with the geometry.
    protected void Repaint()
    {
        if (!m_wCanvas)
            return;

        m_aDrawCommands.Clear();

        int count = m_aEntries.Count();
        float span = 360.0 / count;

        for (int i = 0; i < count; i = i + 1)
        {
            float mid = i * span;
            float a0 = mid - span * 0.5 + SLICE_GAP_DEG * 0.5;
            float a1 = mid + span * 0.5 - SLICE_GAP_DEG * 0.5;

            int fill = COLOR_SLICE_IDLE;
            int rim = COLOR_RIM;
            float rimWidth = RIM_WIDTH_PX;

            // A disabled slice never lights, including under the cursor — the highlight is the
            // promise that confirming will do something.
            if (m_aEntries[i].m_bDisabled)
            {
                fill = COLOR_SLICE_OFF;
            }
            else if (i == m_iSelected)
            {
                fill = COLOR_SLICE_HOT;
                rim = COLOR_RIM_HOT;
                rimWidth = RIM_WIDTH_HOT_PX;
            }

            AddSector(m_fRingInner, m_fRingOuter, a0, a1, fill);

            AddArc(m_fRingOuter, a0, a1, rimWidth, rim);
            AddArc(m_fRingInner, a0, a1, rimWidth, rim);
            AddRadialLine(m_fRingInner, m_fRingOuter, a0, rimWidth, COLOR_DIVIDER);
            AddRadialLine(m_fRingInner, m_fRingOuter, a1, rimWidth, COLOR_DIVIDER);
        }

        // Hub last so it sits over the inner rim rather than under it.
        AddDisc(m_fRingInner - 6, COLOR_HUB);
        AddArc(m_fRingInner - 6, 0, 360, RIM_WIDTH_PX, COLOR_HUB_RING);

        m_wCanvas.SetDrawCommands(m_aDrawCommands);
    }

    //------------------------------------------------------------------------------------------------
    //! One annular sector, as a strip of quads. A ring segment is concave, and a single
    //! polygon command would fill straight across the hub.
    protected void AddSector(float rIn, float rOut, float a0, float a1, int color)
    {
        int steps = (a1 - a0) / SECTOR_STEP_DEG + 1;
        if (steps < 1)
            steps = 1;

        float step = (a1 - a0) / steps;

        for (int i = 0; i < steps; i = i + 1)
        {
            float b0 = a0 + step * i;
            float b1 = b0 + step;

            float s0 = Math.Sin(b0 * Math.DEG2RAD);
            float c0 = Math.Cos(b0 * Math.DEG2RAD);
            float s1 = Math.Sin(b1 * Math.DEG2RAD);
            float c1 = Math.Cos(b1 * Math.DEG2RAD);

            PolygonDrawCommand quad = new PolygonDrawCommand();
            quad.m_iColor = color;
            quad.m_Vertices = {
                m_fCentreX + s0 * rIn,  m_fCentreY - c0 * rIn,
                m_fCentreX + s0 * rOut, m_fCentreY - c0 * rOut,
                m_fCentreX + s1 * rOut, m_fCentreY - c1 * rOut,
                m_fCentreX + s1 * rIn,  m_fCentreY - c1 * rIn
            };
            m_aDrawCommands.Insert(quad);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Filled circle for the hub. Convex, so one polygon command is enough.
    protected void AddDisc(float radius, int color)
    {
        array<float> verts = {};
        int steps = 360 / SECTOR_STEP_DEG;

        for (int i = 0; i < steps; i = i + 1)
        {
            float a = i * SECTOR_STEP_DEG * Math.DEG2RAD;
            verts.Insert(m_fCentreX + Math.Sin(a) * radius);
            verts.Insert(m_fCentreY - Math.Cos(a) * radius);
        }

        PolygonDrawCommand disc = new PolygonDrawCommand();
        disc.m_iColor = color;
        disc.m_Vertices = verts;
        m_aDrawCommands.Insert(disc);
    }

    //------------------------------------------------------------------------------------------------
    //! Arc as a chain of straight segments. LineDrawCommand takes two points, so a curve is
    //! spelled out rather than asked for.
    protected void AddArc(float radius, float a0, float a1, float width, int color)
    {
        int steps = (a1 - a0) / SECTOR_STEP_DEG + 1;
        if (steps < 1)
            steps = 1;

        float step = (a1 - a0) / steps;

        for (int i = 0; i < steps; i = i + 1)
        {
            float b0 = (a0 + step * i) * Math.DEG2RAD;
            float b1 = (a0 + step * (i + 1)) * Math.DEG2RAD;

            LineDrawCommand seg = new LineDrawCommand();
            seg.m_iColor = color;
            seg.m_fWidth = width;
            seg.m_Vertices = {
                m_fCentreX + Math.Sin(b0) * radius, m_fCentreY - Math.Cos(b0) * radius,
                m_fCentreX + Math.Sin(b1) * radius, m_fCentreY - Math.Cos(b1) * radius
            };
            m_aDrawCommands.Insert(seg);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void AddRadialLine(float rIn, float rOut, float angleDeg, float width, int color)
    {
        float a = angleDeg * Math.DEG2RAD;

        LineDrawCommand seg = new LineDrawCommand();
        seg.m_iColor = color;
        seg.m_fWidth = width;
        seg.m_Vertices = {
            m_fCentreX + Math.Sin(a) * rIn,  m_fCentreY - Math.Cos(a) * rIn,
            m_fCentreX + Math.Sin(a) * rOut, m_fCentreY - Math.Cos(a) * rOut
        };
        m_aDrawCommands.Insert(seg);
    }

    //------------------------------------------------------------------------------------------------
    //! Labels are widgets rather than TextDrawCommands so they can be centred on their slice
    //! by pivot — a long entry name then grows both ways instead of sliding off its own wedge,
    //! which a draw command anchored at a fixed point cannot do.
    protected void CreateSliceLabel(WorkspaceWidget workspace, Widget frontendRoot,
        WidgetFlags flags, int index, int count)
    {
        float angle = index * (360.0 / count) * Math.DEG2RAD;

        float offsetX = Math.Sin(angle) * m_fLabelRadius;
        float offsetY = Math.Cos(angle) * m_fLabelRadius;

        float x = m_fCentreX + offsetX;
        float y = m_fCentreY - offsetY;

        AG0_TDLRadialEntry entry = m_aEntries[index];

        // An entry with its own picture shows only the picture. The names behind these are raw
        // imageset quad strings — SCR_MarkerIconEntry exposes no display name — so the text was
        // reading "tdl_observation_post" under a glyph that already says it, wrapping out of its
        // slice on the longer ones. The icon carries the whole slice instead, drawn larger now
        // that it has the room.
        if (!entry.m_sQuad.IsEmpty())
        {
            float iconSize = m_fRingOuter * ICON_ONLY_SIZE_RATIO;
            if (iconSize < ICON_ONLY_SIZE_MIN_PX)
                iconSize = ICON_ONLY_SIZE_MIN_PX;

            CreateSliceIcon(workspace, frontendRoot, flags, index, x, y, iconSize);
            ApplyIconOpacity(index);
            return;
        }

        TextWidget label = CreateCentredText(workspace, frontendRoot, flags, x, y,
            ResolveLabelWidth(offsetX, offsetY, count), m_iLabelFont * 1.5, m_iLabelFont);
        if (!label)
            return;

        label.SetText(entry.m_sLabel);
        entry.m_wLabel = label;
        ApplyLabelColour(index);
    }

    //------------------------------------------------------------------------------------------------
    //! Selection feedback for a slice with no text to brighten. The hot fill already marks the
    //! choice, but an icon that lifts to full opacity when selected is readable at the edge of
    //! vision, which is where a wheel is actually used — the operator is watching the map.
    protected void ApplyIconOpacity(int index)
    {
        if (index < 0 || index >= m_aEntries.Count())
            return;

        AG0_TDLRadialEntry entry = m_aEntries[index];
        if (!entry.m_wIcon)
            return;

        if (entry.m_bDisabled)
        {
            entry.m_wIcon.SetOpacity(ICON_OPACITY_OFF);
            return;
        }

        if (index == m_iSelected)
            entry.m_wIcon.SetOpacity(1.0);
        else
            entry.m_wIcon.SetOpacity(ICON_OPACITY_IDLE);
    }

    //------------------------------------------------------------------------------------------------
    //! The choice's own picture, tinted the way the panel would tint it.
    protected void CreateSliceIcon(WorkspaceWidget workspace, Widget frontendRoot,
        WidgetFlags flags, int index, float centreX, float centreY, float size)
    {
        AG0_TDLRadialEntry entry = m_aEntries[index];

        ImageWidget icon = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID,
            flags | WidgetFlags.STRETCH, new Color(1, 1, 1, 1), 0, frontendRoot));
        if (!icon)
            return;

        FrameSlot.SetAnchorMin(icon, 0, 0);
        FrameSlot.SetAnchorMax(icon, 0, 0);
        FrameSlot.SetAlignment(icon, 0.5, 0.5);
        FrameSlot.SetPos(icon, workspace.DPIUnscale(centreX), workspace.DPIUnscale(centreY));
        FrameSlot.SetSize(icon, workspace.DPIUnscale(size), workspace.DPIUnscale(size));

        icon.LoadImageFromSet(0, entry.m_sImageset, entry.m_sQuad);

        if (entry.m_bHasColor)
            icon.SetColorInt(entry.m_iColor);

        entry.m_wIcon = icon;
    }

    //------------------------------------------------------------------------------------------------
    //! How wide a label box may be at its own position without leaving the ring.
    //!
    //! Two limits bind, and which one bites depends on the angle. Sideways, the box runs into
    //! the outer rim: the ring's horizontal half-width at that height is the chord of the
    //! outer circle there, and the box is already offset along it. Around the ring, the box
    //! runs into its neighbours: that limit is the chord subtended by one slice at the label
    //! radius. A fixed width honours neither, which is what pushed the sideways labels
    //! straight through the rim.
    protected float ResolveLabelWidth(float offsetX, float offsetY, int count)
    {
        float chordHalf = m_fRingOuter * m_fRingOuter - offsetY * offsetY;
        if (chordHalf < 0)
            chordHalf = 0;

        float radialLimit = Math.Sqrt(chordHalf) - Math.AbsFloat(offsetX);

        float halfSpanRad = (180.0 / count) * Math.DEG2RAD;
        float neighbourLimit = m_fLabelRadius * Math.Sin(halfSpanRad);

        float limit = radialLimit;
        if (neighbourLimit < limit)
            limit = neighbourLimit;

        float width = (limit - LABEL_PAD_PX) * 2;
        if (width < LABEL_MIN_W_PX)
            width = LABEL_MIN_W_PX;

        return width;
    }

    //------------------------------------------------------------------------------------------------
    //! The hub names the page, which is what turns a tree of identical rings into somewhere
    //! the operator knows they are.
    protected void CreateHubLabel(WorkspaceWidget workspace, Widget frontendRoot, WidgetFlags flags)
    {
        m_wHubLabel = CreateCentredText(workspace, frontendRoot, flags, m_fCentreX, m_fCentreY,
            m_fRingInner * 1.6, m_iLabelFont * 1.6, m_iLabelFont + 2);
        if (!m_wHubLabel)
            return;

        m_wHubLabel.SetText(PageTitle(m_ePage));
        m_wHubLabel.SetColorInt(COLOR_HUB_TEXT);
    }

    //------------------------------------------------------------------------------------------------
    //! Position is a centre point, not a corner: alignment 0.5 puts the widget's own middle
    //! on it, which is what the ring's geometry hands out.
    protected TextWidget CreateCentredText(WorkspaceWidget workspace, Widget frontendRoot,
        WidgetFlags flags, float centreX, float centreY, float boxW, float boxH, int fontSize)
    {
        TextWidget text = TextWidget.Cast(workspace.CreateWidget(WidgetType.TextWidgetTypeID,
            flags, new Color(1, 1, 1, 1), 0, frontendRoot));
        if (!text)
            return null;

        FrameSlot.SetAnchorMin(text, 0, 0);
        FrameSlot.SetAnchorMax(text, 0, 0);
        FrameSlot.SetAlignment(text, 0.5, 0.5);
        FrameSlot.SetPos(text, workspace.DPIUnscale(centreX), workspace.DPIUnscale(centreY));
        FrameSlot.SetSize(text, workspace.DPIUnscale(boxW), workspace.DPIUnscale(boxH));

        text.SetExactFontSize(fontSize);
        text.SetTextWrapping(false);
        return text;
    }

    //------------------------------------------------------------------------------------------------
    // INPUT
    //------------------------------------------------------------------------------------------------

    //! Pumped by the frontend every frame: reads the aim, moves the highlight, and resolves
    //! confirm. Called even while shut so the confirm drain can finish — see
    //! IsConsumingClicks.
    void Tick(float timeSlice, InputManager im)
    {
        if (!im)
            return;

        bool confirmHeld = im.GetActionValue("TDLScreenClick") > 0.5;

        if (m_iDrainTicks > 0 && !confirmHeld)
            m_iDrainTicks = m_iDrainTicks - 1;

        if (!m_bOpen)
            return;

        UpdateSelection(im);

        if (!confirmHeld)
        {
            m_bConfirmLatched = false;
            return;
        }

        if (m_bConfirmLatched)
            return;

        m_bConfirmLatched = true;
        PerformSelected();
    }

    //------------------------------------------------------------------------------------------------
    //! Aim comes from the right stick when it is deflected and from the pointer otherwise, so
    //! mouse and pad drive the same wheel with no mode switch between them. Nothing is
    //! selected until the aim clears its threshold — see SELECT_DEADZONE.
    protected void UpdateSelection(InputManager im)
    {
        float aimX = im.GetActionValue("TDLPanHorizontal");
        float aimY = im.GetActionValue("TDLPanVertical");

        bool haveAim = Math.AbsFloat(aimX) > SELECT_DEADZONE || Math.AbsFloat(aimY) > SELECT_DEADZONE;
        if (!haveAim)
        {
            if (!ResolveMouseAim(aimX, aimY))
            {
                SetSelected(-1);
                return;
            }
        }

        float angleDeg = Math.Atan2(aimX, aimY) * Math.RAD2DEG;
        if (angleDeg < 0)
            angleDeg = angleDeg + 360;

        int count = m_aEntries.Count();
        float step = 360.0 / count;

        int index = Math.Round(angleDeg / step);
        if (index >= count)
            index = index - count;

        SetSelected(index);
    }

    //------------------------------------------------------------------------------------------------
    //! Pointer travel since the wheel opened, as an up-positive aim vector. False until the
    //! pointer clears the dead zone, which is how a mouse user cancels by simply not moving.
    //!
    //! Measured from where the pointer was when the wheel opened rather than from the wheel
    //! centre, because on a pad the wheel opens on the crosshair while the pointer sits
    //! wherever it was last left. Distance from centre would read that stale pointer as a
    //! held aim and latch a slice nobody chose; travel reads zero until a hand moves a mouse.
    protected bool ResolveMouseAim(out float aimX, out float aimY)
    {
        aimX = 0;
        aimY = 0;

        int mouseX;
        int mouseY;
        WidgetManager.GetMousePos(mouseX, mouseY);

        float dx = mouseX - m_fOpenMouseX - m_fClampShiftX;
        float dy = mouseY - m_fOpenMouseY - m_fClampShiftY;

        float minTravel = m_fRingInner * MOUSE_SELECT_MIN_RATIO;
        if (dx * dx + dy * dy < minTravel * minTravel)
            return false;

        // Screen Y grows downward while the wheel and the stick both treat up as positive.
        aimX = dx;
        aimY = -dy;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void SetSelected(int index)
    {
        if (m_iSelected == index)
            return;

        int previous = m_iSelected;
        m_iSelected = index;

        ApplyLabelColour(previous);
        ApplyLabelColour(m_iSelected);
        ApplyIconOpacity(previous);
        ApplyIconOpacity(m_iSelected);
        Repaint();
    }

    //------------------------------------------------------------------------------------------------
    protected void ApplyLabelColour(int index)
    {
        if (index < 0 || index >= m_aEntries.Count())
            return;

        TextWidget label = m_aEntries[index].m_wLabel;
        if (!label)
            return;

        AG0_TDLRadialEntry entry = m_aEntries[index];

        if (entry.m_bDisabled)
        {
            label.SetColorInt(COLOR_LABEL_OFF);
            label.SetOpacity(1.0);
            return;
        }

        // A colour entry is drawn in its own colour, brightened rather than recoloured when
        // selected, so the swatch never lies about what it will place.
        if (entry.m_bHasColor && entry.m_sQuad.IsEmpty())
        {
            label.SetColorInt(entry.m_iColor);
            if (index == m_iSelected)
                label.SetOpacity(1.0);
            else
                label.SetOpacity(0.7);

            return;
        }

        if (index == m_iSelected)
            label.SetColorInt(COLOR_LABEL_HOT);
        else
            label.SetColorInt(COLOR_LABEL_IDLE);
    }

    //------------------------------------------------------------------------------------------------
    // TREE
    //------------------------------------------------------------------------------------------------

    protected void PerformSelected()
    {
        if (m_iSelected < 0 || m_iSelected >= m_aEntries.Count())
        {
            m_iDrainTicks = DRAIN_TICKS;
            Close();
            return;
        }

        AG0_TDLRadialEntry entry = m_aEntries[m_iSelected];

        // Refused rather than treated as a cancel: closing the wheel on a mis-aimed confirm
        // costs the operator the whole re-open, and the greyed slice already said why nothing
        // happened.
        if (entry.m_bDisabled)
            return;

        ETDLRadialAction action = entry.m_eAction;
        int payload = entry.m_iPayload;
        vector worldPos = m_vOpenWorldPos;

        // Steps that lead somewhere keep the wheel up; anything that acts finishes.
        if (action == ETDLRadialAction.OPEN_PAGE)
        {
            PushPage(payload);
            return;
        }

        if (action == ETDLRadialAction.GO_BACK)
        {
            GoBack();
            return;
        }

        if (action == ETDLRadialAction.SHOW_MORE)
        {
            m_iOptionOffset = payload;
            OpenPage(m_ePage);
            return;
        }

        if (action == ETDLRadialAction.PICK_OPTION)
        {
            PerformPick(payload, worldPos);
            return;
        }

        m_iDrainTicks = DRAIN_TICKS;
        Close();
        Perform(action, worldPos);
    }

    //------------------------------------------------------------------------------------------------
    protected void PushPage(int page)
    {
        m_aPageStack.Insert(m_ePage);
        m_iOptionOffset = 0;
        ApplyMarkerTypeForPage(page);
        OpenPage(page);
    }

    //! Spin-box indices the marker tool uses for its categories. Placement from the wheel
    //! drives the same control the panel does rather than carrying its own idea of type, so a
    //! marker means the same thing whichever route built it.
    protected static const int TYPE_INDEX_CUSTOM = 0;
    protected static const int TYPE_INDEX_MILITARY = 1;
    protected static const int TYPE_INDEX_SHAPE = 2;

    //------------------------------------------------------------------------------------------------
    //! Entering a branch switches the marker tool to that branch's type, because every page
    //! below it reads that type's selectors and the placement at the end builds from it.
    protected void ApplyMarkerTypeForPage(int page)
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();
        if (!markerTool)
            return;

        if (page == ETDLRadialPage.MIL_FACTION)
            markerTool.SetMarkerTypeIndex(TYPE_INDEX_MILITARY);
        else if (page == ETDLRadialPage.CUSTOM_ICON)
            markerTool.SetMarkerTypeIndex(TYPE_INDEX_CUSTOM);
        else if (page == ETDLRadialPage.SHAPE_TOOL)
            markerTool.SetMarkerTypeIndex(TYPE_INDEX_SHAPE);
    }

    //------------------------------------------------------------------------------------------------
    protected void GoBack()
    {
        if (m_aPageStack.IsEmpty())
        {
            Close();
            return;
        }

        int last = m_aPageStack.Count() - 1;
        int page = m_aPageStack[last];
        m_aPageStack.Remove(last);

        m_iOptionOffset = 0;
        OpenPage(page);
    }

    //------------------------------------------------------------------------------------------------
    //! Apply one selector choice, then move to whatever the page owes next. The marker tool
    //! holds the state between steps, which is what lets a wheel-built marker be identical to
    //! a panel-built one.
    protected void PerformPick(int optionIndex, vector worldPos)
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();
        if (!markerTool)
        {
            Close();
            return;
        }

        markerTool.SetOptionIndex(OptionForPage(m_ePage), optionIndex);

        switch (m_ePage)
        {
            case ETDLRadialPage.MIL_FACTION:
                PushPage(ETDLRadialPage.MIL_DIMENSION);
                return;

            case ETDLRadialPage.MIL_DIMENSION:
                PushPage(ETDLRadialPage.MIL_TYPE);
                return;

            case ETDLRadialPage.CUSTOM_ICON:
                PushPage(ETDLRadialPage.CUSTOM_COLOR);
                return;

            case ETDLRadialPage.SHAPE_TOOL:
                PushPage(ETDLRadialPage.SHAPE_COLOR);
                return;
        }

        // Terminal option pages. MIL_TYPE and CUSTOM_COLOR drop a marker on the captured
        // point; SHAPE_COLOR cannot, because a shape needs clicks the wheel is in the way of
        // — it arms the tool and gets out of the way instead, which is how ATAK does it too.
        bool wasShape = m_ePage == ETDLRadialPage.SHAPE_COLOR;

        m_iDrainTicks = DRAIN_TICKS;
        Close();

        if (!wasShape)
        {
            markerTool.PlaceCurrentMarker(worldPos, false);
            return;
        }

        // Shape clicks only reach the draw session while a tool panel owns the map click, so
        // arming a tool has to bring the panel up with it or the next click would fall through
        // to nothing. Drawing is its own panel now, and it is the one that offers Shape.
        m_Owner.SetPanelContent(ETDLPanelContent.DRAW_TOOL);
    }

    //------------------------------------------------------------------------------------------------
    protected void Perform(ETDLRadialAction action, vector worldPos)
    {
        if (!m_Owner)
            return;

        switch (action)
        {
            case ETDLRadialAction.OPEN_MARKER_TOOL:
                m_Owner.SetPanelContent(ETDLPanelContent.MARKER_TOOL);
                break;

            case ETDLRadialAction.OPEN_DRAW_TOOL:
                m_Owner.SetPanelContent(ETDLPanelContent.DRAW_TOOL);
                break;

            case ETDLRadialAction.DELETE_AT_POINT:
                PerformDeleteAt(worldPos);
                break;

            case ETDLRadialAction.TOGGLE_MAP3D:
                m_Owner.ToggleMap3DFromRadial();
                break;

            case ETDLRadialAction.TOGGLE_BLOODHOUND:
                AG0_TDLMenuController.SetBloodhoundEnabled(
                    !AG0_TDLMenuController.GetBloodhoundEnabled());
                break;

            case ETDLRadialAction.CENTRE_ON_PLAYER:
                m_Owner.CentreMapOnPlayer();
                break;

            case ETDLRadialAction.PIN_RANGE_BEARING:
                m_Owner.PinRangeBearingAt(worldPos);
                break;

            case ETDLRadialAction.OPEN_MEMBER_DETAIL:
                if (m_OpenPick)
                    m_Owner.ShowDetailForMember(m_OpenPick.m_MemberRplId);
                break;

            case ETDLRadialAction.OPEN_MEMBER_CHAT:
                if (m_OpenPick)
                    m_Owner.OpenChatForMember(m_OpenPick.m_MemberRplId);
                break;
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void PerformDeleteAt(vector worldPos)
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();
        if (!markerTool)
            return;

        markerTool.SweepDeleteAt(worldPos);
    }

    //------------------------------------------------------------------------------------------------
    // PAGES
    //------------------------------------------------------------------------------------------------

    protected string PageTitle(ETDLRadialPage page)
    {
        switch (page)
        {
            case ETDLRadialPage.PLACE_TYPE:     return "PLACE";
            case ETDLRadialPage.MIL_FACTION:    return "AFFILIATION";
            case ETDLRadialPage.MIL_DIMENSION:  return "DIMENSION";
            case ETDLRadialPage.MIL_TYPE:       return "TYPE";
            case ETDLRadialPage.CUSTOM_ICON:    return "ICON";
            case ETDLRadialPage.CUSTOM_COLOR:   return "COLOUR";
            case ETDLRadialPage.SHAPE_TOOL:     return "SHAPE";
            case ETDLRadialPage.SHAPE_COLOR:    return "COLOUR";
        }

        return "TDL";
    }

    //------------------------------------------------------------------------------------------------
    //! Which of the marker tool's selectors an option page is choosing from. Pages that are
    //! not option pages never reach this.
    protected ETDLMarkerOption OptionForPage(ETDLRadialPage page)
    {
        switch (page)
        {
            case ETDLRadialPage.MIL_FACTION:    return ETDLMarkerOption.MIL_FACTION;
            case ETDLRadialPage.MIL_DIMENSION:  return ETDLMarkerOption.MIL_DIMENSION;
            case ETDLRadialPage.MIL_TYPE:       return ETDLMarkerOption.MIL_TYPE;
            case ETDLRadialPage.CUSTOM_ICON:    return ETDLMarkerOption.PLACED_ICON;
            case ETDLRadialPage.CUSTOM_COLOR:   return ETDLMarkerOption.PLACED_COLOR;
            case ETDLRadialPage.SHAPE_TOOL:     return ETDLMarkerOption.SHAPE_TOOL;
        }

        return ETDLMarkerOption.SHAPE_COLOR;
    }

    //------------------------------------------------------------------------------------------------
    //! Rebuilt on every open because what belongs in a ring depends on state that changes
    //! while the wheel is shut — which page, whether 3D is up, whether the bloodhound is
    //! running, and whether there is anything under the pointer worth offering an action for.
    protected void BuildEntries()
    {
        if (m_ePage == ETDLRadialPage.ROOT)
        {
            BuildRootEntries();
            return;
        }

        if (m_ePage == ETDLRadialPage.PLACE_TYPE)
        {
            BuildPlaceTypeEntries();
            return;
        }

        BuildOptionEntries();
    }

    //------------------------------------------------------------------------------------------------
    //! The root ring is chosen by what the wheel was opened over, the way ATAK picks its menu XML
    //! from the target's CoT type. A wheel that offers the same six things over a contact, a
    //! drawn shape and bare ground makes the operator read every slice every time; one that
    //! changes with the target lets them learn "second slice on a contact is chat".
    protected void BuildRootEntries()
    {
        AG0_ETDLPickKind kind = AG0_ETDLPickKind.NONE;
        if (m_OpenPick)
            kind = m_OpenPick.m_eKind;

        switch (kind)
        {
            case AG0_ETDLPickKind.MEMBER:
                BuildMemberEntries();
                return;

            case AG0_ETDLPickKind.SELF:
                BuildSelfEntries();
                return;

            case AG0_ETDLPickKind.OWN_MARKER:
            case AG0_ETDLPickKind.OTHER_MARKER:
                BuildMarkerEntries();
                return;

            case AG0_ETDLPickKind.SHAPE:
                BuildShapeEntries();
                return;
        }

        BuildEmptyMapEntries();
    }

    //------------------------------------------------------------------------------------------------
    //! Bare ground — the full toolbox, because there is no object to act on.
    protected void BuildEmptyMapEntries()
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();

        // The panel builds its selector lists when it is first shown, and the wheel can be
        // raised in a session where that never happened. Asking for them up front is what
        // stops Place opening onto a ring with nothing but Back in it.
        if (markerTool)
            markerTool.EnsureSelectorsBuilt();

        // Placement first so it lands in the same slice every time — it is the entry an
        // operator reaches for without looking, and a wheel is only fast when it is stable.
        if (markerTool && HasAnyPlacementBranch(markerTool))
            AddEntry(ETDLRadialAction.OPEN_PAGE, "Place", ETDLRadialPage.PLACE_TYPE);

        AddEntry(ETDLRadialAction.OPEN_DRAW_TOOL, "Draw", 0);
        AddEntry(ETDLRadialAction.PIN_RANGE_BEARING, "Measure To", 0);
        AddEntry(ETDLRadialAction.OPEN_MARKER_TOOL, "Markers", 0);
        AddEntry(ETDLRadialAction.TOGGLE_MAP3D, ResolveMap3DLabel(), 0);
        AddEntry(ETDLRadialAction.TOGGLE_BLOODHOUND, ResolveBloodhoundLabel(), 0);
        AddEntry(ETDLRadialAction.CENTRE_ON_PLAYER, "Centre", 0);
    }

    //------------------------------------------------------------------------------------------------
    //! A contact. Mirrors ATAK's `a-f.xml` reduced to what TDL can actually do today: details,
    //! the direct thread, and a measurement to them.
    protected void BuildMemberEntries()
    {
        AddEntry(ETDLRadialAction.OPEN_MEMBER_DETAIL, "Details", 0);
        AddEntry(ETDLRadialAction.OPEN_MEMBER_CHAT, "Chat", 0);
        AddEntry(ETDLRadialAction.PIN_RANGE_BEARING, "Measure To", 0);
        AddEntry(ETDLRadialAction.CENTRE_ON_PLAYER, "Centre", 0);
        AddEntry(ETDLRadialAction.TOGGLE_MAP3D, ResolveMap3DLabel(), 0);
    }

    //------------------------------------------------------------------------------------------------
    //! Own position — ATAK's self ring. Placement stays because dropping a marker on yourself is
    //! how a contact report starts.
    protected void BuildSelfEntries()
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();
        if (markerTool)
            markerTool.EnsureSelectorsBuilt();

        if (markerTool && HasAnyPlacementBranch(markerTool))
            AddEntry(ETDLRadialAction.OPEN_PAGE, "Place", ETDLRadialPage.PLACE_TYPE);

        AddEntry(ETDLRadialAction.CENTRE_ON_PLAYER, "Centre", 0);
        AddEntry(ETDLRadialAction.OPEN_MARKER_TOOL, "Markers", 0);
        AddEntry(ETDLRadialAction.TOGGLE_MAP3D, ResolveMap3DLabel(), 0);
    }

    //------------------------------------------------------------------------------------------------
    //! A placed marker. Delete is offered only on the operator's own, matching the sweep's owner
    //! rule — the server refuses the rest, and an entry that silently fails is worse than absent.
    protected void BuildMarkerEntries()
    {
        if (m_OpenPick && m_OpenPick.IsDeletable())
            AddEntry(ETDLRadialAction.DELETE_AT_POINT, "Delete", 0);
        else
            AddDisabledEntry(ETDLRadialAction.DELETE_AT_POINT, "Delete", 0);

        AddEntry(ETDLRadialAction.PIN_RANGE_BEARING, "Measure To", 0);
        AddEntry(ETDLRadialAction.OPEN_MARKER_TOOL, "Markers", 0);
        AddEntry(ETDLRadialAction.CENTRE_ON_PLAYER, "Centre", 0);
    }

    //------------------------------------------------------------------------------------------------
    //! A drawn shape. Deletion goes through the same sweep the delete sub-mode uses, so ownership
    //! is settled server-side exactly as it is everywhere else.
    protected void BuildShapeEntries()
    {
        AddEntry(ETDLRadialAction.DELETE_AT_POINT, "Delete", 0);
        AddEntry(ETDLRadialAction.PIN_RANGE_BEARING, "Measure To", 0);
        AddEntry(ETDLRadialAction.OPEN_DRAW_TOOL, "Draw", 0);
    }

    //------------------------------------------------------------------------------------------------
    protected string ResolveMap3DLabel()
    {
        if (AG0_TDLMap3DView.IsViewOpen())
            return "2D View";

        return "3D View";
    }

    //------------------------------------------------------------------------------------------------
    protected string ResolveBloodhoundLabel()
    {
        if (AG0_TDLMenuController.GetBloodhoundEnabled())
            return "Bloodhound Off";

        return "Bloodhound";
    }

    //------------------------------------------------------------------------------------------------
    //! True when at least one kind of thing can actually be placed. A config that supplies no
    //! military entry, for instance, leaves that whole branch empty, and an entry that opens
    //! onto nothing is worse than one that is not offered.
    protected bool HasAnyPlacementBranch(AG0_TDLMarkerToolPanel markerTool)
    {
        return markerTool.GetOptionCount(ETDLMarkerOption.MIL_FACTION) > 0
            || markerTool.GetOptionCount(ETDLMarkerOption.PLACED_ICON) > 0
            || markerTool.GetOptionCount(ETDLMarkerOption.SHAPE_TOOL) > 0;
    }

    //------------------------------------------------------------------------------------------------
    //! Choosing what to place sets the marker tool's own type first, so every page after this
    //! one reads the selectors belonging to the thing being built.
    protected void BuildPlaceTypeEntries()
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();
        if (!markerTool)
            return;

        if (markerTool.GetOptionCount(ETDLMarkerOption.MIL_FACTION) > 0)
            AddEntry(ETDLRadialAction.OPEN_PAGE, "Military", ETDLRadialPage.MIL_FACTION);

        if (markerTool.GetOptionCount(ETDLMarkerOption.PLACED_ICON) > 0)
            AddEntry(ETDLRadialAction.OPEN_PAGE, "Custom", ETDLRadialPage.CUSTOM_ICON);

        if (markerTool.GetOptionCount(ETDLMarkerOption.SHAPE_TOOL) > 0)
            AddEntry(ETDLRadialAction.OPEN_PAGE, "Shape", ETDLRadialPage.SHAPE_TOOL);

        AddEntry(ETDLRadialAction.GO_BACK, "Back", 0);
    }

    //------------------------------------------------------------------------------------------------
    //! An option page is the selector's own list, in the selector's own order, so a slice and
    //! a spin box position mean the same thing. Long lists page rather than shrink, because a
    //! wheel stops being reachable long before it stops being drawable.
    protected void BuildOptionEntries()
    {
        AG0_TDLMarkerToolPanel markerTool = m_Owner.GetMarkerToolPanel();
        if (!markerTool)
            return;

        ETDLMarkerOption option = OptionForPage(m_ePage);
        int total = markerTool.GetOptionCount(option);
        if (total <= 0)
        {
            AddEntry(ETDLRadialAction.GO_BACK, "Back", 0);
            return;
        }

        if (m_iOptionOffset >= total)
            m_iOptionOffset = 0;

        // The Back slice always costs one, and a More slice costs another when the remainder
        // does not fit.
        int room = MAX_SLICES - 1;
        int remaining = total - m_iOptionOffset;
        bool needsMore = remaining > room;
        if (needsMore)
            room = room - 1;

        int shown = remaining;
        if (shown > room)
            shown = room;

        for (int i = 0; i < shown; i = i + 1)
        {
            int optionIndex = m_iOptionOffset + i;
            AddEntry(ETDLRadialAction.PICK_OPTION,
                markerTool.GetOptionLabel(option, optionIndex), optionIndex);
            DecorateLastEntry(markerTool, option, optionIndex);
        }

        if (needsMore)
            AddEntry(ETDLRadialAction.SHOW_MORE, "More", m_iOptionOffset + shown);

        AddEntry(ETDLRadialAction.GO_BACK, "Back", 0);
    }

    //------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------
    //! Attach whatever the selector can show a choice as — its marker glyph, or its colour.
    //! Icons carry the colour the panel would place them in, so what the ring shows is what
    //! lands on the map.
    protected void DecorateLastEntry(AG0_TDLMarkerToolPanel markerTool,
        ETDLMarkerOption option, int optionIndex)
    {
        AG0_TDLRadialEntry entry = m_aEntries[m_aEntries.Count() - 1];

        Color colour;
        if (markerTool.GetOptionColor(option, optionIndex, colour))
        {
            entry.m_bHasColor = true;
            entry.m_iColor = PackColor(colour);
            return;
        }

        ResourceName imageset;
        string quad;
        if (!markerTool.GetOptionImage(option, optionIndex, imageset, quad))
            return;

        entry.m_sImageset = imageset;
        entry.m_sQuad = quad;

        Color placedColour;
        if (markerTool.GetCurrentPlacedColor(placedColour))
        {
            entry.m_bHasColor = true;
            entry.m_iColor = PackColor(placedColour);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Engine Color (0..1 floats) to the packed ARGB int the widget setters take.
    protected int PackColor(Color c)
    {
        int a = Math.Clamp(c.A() * 255, 0, 255);
        int r = Math.Clamp(c.R() * 255, 0, 255);
        int g = Math.Clamp(c.G() * 255, 0, 255);
        int b = Math.Clamp(c.B() * 255, 0, 255);
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    //------------------------------------------------------------------------------------------------
    protected void AddEntry(ETDLRadialAction action, string label, int payload)
    {
        AG0_TDLRadialEntry entry = new AG0_TDLRadialEntry();
        entry.m_eAction = action;
        entry.m_sLabel = label;
        entry.m_iPayload = payload;
        m_aEntries.Insert(entry);
    }

    //------------------------------------------------------------------------------------------------
    //! Add an entry that is present but refused, so the ring keeps its shape when a target
    //! cannot support one of its usual actions.
    protected void AddDisabledEntry(ETDLRadialAction action, string label, int payload)
    {
        AddEntry(action, label, payload);

        AG0_TDLRadialEntry entry = m_aEntries[m_aEntries.Count() - 1];
        entry.m_bDisabled = true;
    }
}
