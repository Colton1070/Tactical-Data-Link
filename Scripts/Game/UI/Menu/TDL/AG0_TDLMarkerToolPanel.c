//------------------------------------------------------------------------------------------------
// AG0_TDLMarkerToolPanel
//------------------------------------------------------------------------------------------------
//! Owns the marker-tool side-tray content. Spawns the duplicated vanilla
//! edit-box layouts (ATAKMapMarkerEditBox + ATAKMapMilitaryMarkerEditBox)
//! into their respective sections, populates pickers from
//! SCR_MapMarkerManagerComponent's marker config, listens for changes,
//! drives a live military symbol preview, and fires placement events
//! when the user commits a marker.
//!
//! Construction model: AG0_TDLMenuUI creates one instance after the menu
//! widgets resolve, calls Init(root) once. The two edit-box layouts are
//! lazily spawned on the first OnPanelShown() so they don't pay creation
//! cost when the marker tool is never opened. Section widgets toggle
//! visibility per the top spinbox; spawned widgets stay alive across
//! show/hide cycles since they hold spinbox / edit-box state we don't
//! want to rebuild on every flip.
//!
//! v1 scope notes:
//!  - PLACED_CUSTOM (TDL) markers only — single category in our config.
//!    The MarkerEditTab inside ATAKMapMarkerEditBox is left un-populated
//!    until we have multiple categories worth tabbing between.
//!  - Rotation slider in ATAKMapMarkerEditBox is left un-hooked; markers
//!    place with rotation 0. Hookup is straight-forward (m_PlacedSlider
//!    field + GetOnChangedFinal) when polish-passing.
//!  - Timestamp spinbox (vanilla MapMilitaryMarkerEditBoxTimestamp)
//!    similarly left un-hooked — vanilla layout decoration only.
//!  - Military icon-flag combos (ComboBox1, ComboBox2) populate with a
//!    single "None" entry for now since EMilitarySymbolIcon enum values
//!    aren't exposed in the public API. Markers place with iconFlags=0
//!    (basic symbol). When we figure out the enum we expose more flags.
//------------------------------------------------------------------------------------------------
class AG0_TDLMarkerToolPanel
{
    // Layout resources — duplicated under our mod, GUIDs from the .layout.meta files
    protected static const ResourceName PLACED_LAYOUT   = "{5A61BDF73D7743D6}UI/layouts/Map/ATAKMapMarkerEditBox.layout";
    protected static const ResourceName MILITARY_LAYOUT = "{247748758083F100}UI/layouts/Map/ATAKMapMilitaryMarkerEditBox.layout";
    // Per-marker entry card spawned into the MarkerList scroll between
    // Place and Back. Vanilla M-map style (icon + text + delete-X) — clicking
    // the card deletes the underlying marker.
    protected static const ResourceName MARKER_ENTRY_LAYOUT = "{D0D62BD37945894C}UI/layouts/Map/ATAKMapMarkerEntry.layout";

    // Shape sub-form layout. Named widgets the spawn looks up:
    // ShapeToolSpinBox, StrokeColorSpinBox, StrokeWidthSpinBox, FillToggle,
    // and a SCR_EditBoxComponent root named EditBoxRoot for the label.
    protected static const ResourceName SHAPE_LAYOUT = "{54F9660B26F602EA}UI/layouts/Map/ATAKMapShapeEditBox.layout";

    // Type spinbox indices — keep in sync with the m_aElementNames in TDLMenuUI.layout
    protected static const int TYPE_INDEX_PLACED   = 0;
    protected static const int TYPE_INDEX_MILITARY = 1;
    protected static const int TYPE_INDEX_SHAPE    = 2;
    protected static const int TYPE_INDEX_DELETE   = 3;

    // World-space radius around the cursor that picks up a marker for the
    // sweep-delete gesture. At default zoom (~0.15) this is roughly 5–10
    // pixels on screen — wide enough to forgive imprecise sweeps without
    // grabbing markers the user didn't intend.
    protected static const float DELETE_SWEEP_RADIUS_M = 30.0;

    // ============================================
    // CROSS-FRONTEND CONFIG STATICS
    //
    // The marker tool panel lives once per AG0_TDLMenuController instance —
    // one for the fullscreen menu, one for the world-space display. Their
    // spinbox widgets are independent. To make "configure in menu, then
    // place from world-space" work, we mirror every user-driven selection
    // into class-level statics; OnPanelShown applies them back onto the
    // local widgets so each panel re-enters with the most-recent picks.
    //
    // Sentinel value -1 = "no saved selection yet, use the spinbox default".
    // Edit-box text statics start as empty strings.
    // ============================================
    static protected int    s_iLastType            = -1;
    static protected int    s_iLastPlacedIcon      = -1;
    static protected int    s_iLastPlacedColor     = -1;
    static protected string s_sLastPlacedText      = "";
    static protected int    s_iLastMilFaction      = -1;
    static protected int    s_iLastMilDimension    = -1;
    static protected int    s_iLastMilCombo1       = -1;
    static protected int    s_iLastMilCombo2       = -1;
    static protected string s_sLastMilText         = "";
    static protected int    s_iLastShapeTool       = -1;
    static protected int    s_iLastShapeStrokeColor = -1;
    static protected int    s_iLastShapeStrokeWidth = -1;
    static protected int    s_iLastShapeFill       = -1;
    static protected string s_sLastShapeLabel      = "";

    //------------------------------------------------------------------------------------------------
    // ROOT WIDGETS
    //------------------------------------------------------------------------------------------------

    // The MarkerToolContent panel root and its children we look up in Init
    protected Widget m_wRoot;
    protected Widget m_wPlacedSection;
    protected Widget m_wMilitarySection;
    protected Widget m_wShapeSection;
    // Layout-side: add a FrameWidget named "MarkerToolDeleteSection" to
    // MarkerToolContent (typically a short help-text TextWidget inside).
    // Tolerated as null until the layout is updated — delete mode then
    // silently no-ops the section toggle, but the sweep behaviour still
    // works because it doesn't depend on a sub-form.
    protected Widget m_wDeleteSection;
    protected SCR_SpinBoxComponent m_TypeSpinBox;
    protected SCR_ModularButtonComponent m_BackButton;

    // Player-owned marker scroll list (between Place and Back). m_wMarkerList
    // is the VerticalLayout we spawn ATAKMapMarkerEntry cards into. Parallel
    // arrays keep cards alive (m_aMarkerEntryWidgets) and pin per-card click
    // handlers (m_aMarkerEntryHandlers) so they don't get GC'd while the
    // SCR_ModularButtonComponent invoker still references them.
    protected Widget m_wMarkerList;
    protected ref array<Widget> m_aMarkerEntryWidgets = {};
    protected ref array<ref AG0_TDLMarkerEntryHandler> m_aMarkerEntryHandlers = {};
    // Marker IDs whose AskRemoveStaticMarker RPC is in flight. Skipped during
    // RefreshMarkerList so the entry doesn't reappear between the click and
    // the server's broadcast back. Self-cleared once the manager's lists no
    // longer contain the ID.
    protected ref array<int> m_aPendingDeletes = {};
    // Shape IDs whose AskDeleteShape RPC is in flight. Sweep checks this
    // before re-firing so the same shape doesn't get a delete request
    // every frame while the server round-trip is pending. Cleared lazily
    // — entries linger after the shape is actually gone, but the per-
    // shape-id miss is harmless because GetShape returns null for
    // already-deleted shapes anyway.
    protected ref array<string> m_aPendingShapeDeletes = {};

    // Roots of the spawned edit-box layouts (null until OnPanelShown)
    protected Widget m_wPlacedEditBox;
    protected Widget m_wMilitaryEditBox;
    protected Widget m_wShapeEditBox;

    //------------------------------------------------------------------------------------------------
    // SHAPE EDIT-BOX WIDGETS (looked up after spawn)
    //------------------------------------------------------------------------------------------------

    // Spinbox driving the AG0_ETDLShapeTool selection. Order matches the
    // enum (index 0 = UNKNOWN sentinel reserved for "no tool"; the layout's
    // first visible item maps to CIRCLE).
    protected SCR_SpinBoxComponent m_ShapeToolSpinBox;
    protected SCR_SpinBoxComponent m_ShapeStrokeColorSpinBox;
    protected SCR_SpinBoxComponent m_ShapeStrokeWidthSpinBox;
    protected SCR_SpinBoxComponent m_ShapeFillSpinBox;
    protected SCR_EditBoxComponent m_ShapeLabelEditBox;

    // Stroke colour entries shared with the placed-marker config, so the
    // palette stays consistent with markers the player has already learned.
    // ARGB ints pulled once at spawn time; index aligns with the spinbox.
    protected ref array<int> m_aShapeStrokeColorValues = {};
    // Stroke widths in pixels, parallel to the StrokeWidthSpinBox items.
    protected ref array<float> m_aShapeStrokeWidthValues = {};

    // Active draw session. One per panel instance; created in Init.
    protected ref AG0_TDLShapeDrawSession m_ShapeDrawSession;

    //------------------------------------------------------------------------------------------------
    // PLACED EDIT-BOX WIDGETS (looked up after spawn)
    //------------------------------------------------------------------------------------------------

    protected SCR_SpinBoxComponent m_PlacedIconSpinBox;
    protected SCR_SpinBoxComponent m_PlacedColorSpinBox;
    protected SCR_EditBoxComponent m_PlacedEditBox;
    // Live icon preview at top of the placed edit box. Updates as the user
    // changes Icon / Color spinboxes — pulls imageset+quad from the active
    // SCR_MarkerIconEntry and applies the SCR_MarkerColorEntry tint.
    protected ImageWidget m_wPlacedPreviewImage;

    // Cached mapping spinbox-index → global icon-entry-index. We filter
    // m_aPlacedMarkerIcons by category for the visible list, so the
    // spinbox index is per-category but the marker needs the global one.
    protected ref array<int> m_aPlacedIconGlobalIndex = {};

    //------------------------------------------------------------------------------------------------
    // MILITARY EDIT-BOX WIDGETS
    //------------------------------------------------------------------------------------------------

    protected SCR_SpinBoxComponent m_MilFactionSpinBox;
    protected SCR_SpinBoxComponent m_MilDimensionSpinBox;
    protected SCR_ComboBoxComponent m_MilCombo1;
    protected SCR_ComboBoxComponent m_MilCombo2;
    protected SCR_EditBoxComponent m_MilEditBox;
    protected SCR_MilitarySymbolUIComponent m_MilSymbolUI;
    // Root of the live military preview (the MarkerPreview SizeLayoutWidget).
    // Cached so UpdateMilitaryPreview can walk it and apply the faction tint
    // to every ImageWidget — SCR_MilitarySymbolUIComponent's Update() doesn't
    // honour any "color" property the way SCR_MapMarkerWidgetComponent.SetColor
    // does on the live M-map. Without this, the preview renders white
    // regardless of the selected faction (BLUFOR/OPFOR/etc.).
    protected Widget m_wMilitaryPreviewRoot;

    // Live military symbol — fed into m_MilSymbolUI.Update each time a
    // selector changes. Carries faction/dimension/icon-flags state.
    protected ref SCR_MilitarySymbol m_MilitarySymbol = new SCR_MilitarySymbol();

    //------------------------------------------------------------------------------------------------
    // CACHED CONFIG REFS
    //------------------------------------------------------------------------------------------------

    protected SCR_MapMarkerEntryPlaced m_PlacedConfig;
    protected SCR_MapMarkerEntryMilitary m_MilitaryConfig;

    //------------------------------------------------------------------------------------------------
    // PUBLIC EVENTS
    //------------------------------------------------------------------------------------------------

    //! Fires when the user presses cancel/back on either sub-form. Caller
    //! (the menu) routes this to SetPanelContent(NETWORK_LIST).
    ref ScriptInvoker m_OnCancelRequested = new ScriptInvoker();

    //! Fires when the user clicks a Place button on either sub-form,
    //! signature (bool isLocal) — true for Place Private (custom only),
    //! false for Place Public. The menu handler resolves the world
    //! position from the map cursor (PC) or canvas centre (console) and
    //! calls PlaceCurrentMarker on us. Sub-form buttons don't know about
    //! cursor state — the menu owns that routing.
    ref ScriptInvoker m_OnPlaceRequested = new ScriptInvoker();

    //! Fires after a successful marker delete request, signature (int markerId).
    //! The menu hooks this and forwards to the display controller so the
    //! 3D map widget for the deleted marker is pruned the same frame as
    //! the scroll card, instead of waiting for the next UpdateVanillaMarkers
    //! tick (which doesn't see the deletion until after the RPC roundtrip).
    ref ScriptInvoker m_OnMarkerDeleted = new ScriptInvoker();

    //! Fires when the user finishes drawing a shape, signature
    //! (AG0_TDLMapShape draft, AG0_ETDLShapeTool tool). Menu controller
    //! subscribes and routes the draft to the player-controller's
    //! AskCreateShape RPC. The draft has geometry + style filled in; the
    //! menu controller adds creator identity / network context before send.
    ref ScriptInvoker m_OnShapeDrawCommitted = new ScriptInvoker();


    //------------------------------------------------------------------------------------------------
    // LIFECYCLE
    //------------------------------------------------------------------------------------------------

    //! Bind to MarkerToolContent and look up the immediate children. Sub-form
    //! widget lookups happen lazily in OnPanelShown so we don't pay the
    //! spawn + populate cost until the user actually opens the tool.
    bool Init(Widget root)
    {
        if (!root)
            return false;

        m_wRoot = root;

        m_wPlacedSection   = m_wRoot.FindAnyWidget("MarkerToolPlacedSection");
        m_wMilitarySection = m_wRoot.FindAnyWidget("MarkerToolMilitarySection");
        // Tolerated as null when the layout hasn't been extended yet — shape
        // mode silently no-ops in that case so the existing marker tool keeps
        // working through the layout transition.
        m_wShapeSection    = m_wRoot.FindAnyWidget("MarkerToolShapeSection");
        m_wDeleteSection   = m_wRoot.FindAnyWidget("MarkerToolDeleteSection");

        Widget typeSpinBoxWidget = m_wRoot.FindAnyWidget("MarkerToolTypeSpinBox");
        if (typeSpinBoxWidget)
        {
            m_TypeSpinBox = SCR_SpinBoxComponent.Cast(typeSpinBoxWidget.FindHandler(SCR_SpinBoxComponent));
            if (m_TypeSpinBox)
                m_TypeSpinBox.m_OnChanged.Insert(OnTypeSpinBoxChanged);
        }

        Widget backButtonWidget = m_wRoot.FindAnyWidget("MarkerToolBackButton");
        if (backButtonWidget)
        {
            m_BackButton = SCR_ModularButtonComponent.FindComponent(backButtonWidget);
            if (m_BackButton)
                m_BackButton.m_OnClicked.Insert(OnBackButtonClicked);
        }

        // Player-marker scroll list — host VerticalLayout authored in
        // TDLMenuUI.layout between MarkerToolPlaceButton and
        // MarkerToolBackButton. Tolerated as null for layouts that haven't
        // added it yet (we early-out of Refresh in that case).
        m_wMarkerList = m_wRoot.FindAnyWidget("MarkerList");

        // Cache config refs once — config is loaded with the marker manager
        // at game start so this is safe in Init. Both can be null on a
        // pathologically-unconfigured map; downstream null-checks handle it.
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (markerMgr)
        {
            SCR_MapMarkerConfig cfg = markerMgr.GetMarkerConfig();
            if (cfg)
            {
                m_PlacedConfig   = SCR_MapMarkerEntryPlaced.Cast(cfg.GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_CUSTOM));
                m_MilitaryConfig = SCR_MapMarkerEntryMilitary.Cast(cfg.GetMarkerEntryConfigByType(SCR_EMapMarkerType.PLACED_MILITARY));
            }
        }

        // Default visibility — Placed is the leaf option most often used,
        // matches type spinbox default index 0.
        if (m_wPlacedSection) m_wPlacedSection.SetVisible(true);
        if (m_wMilitarySection) m_wMilitarySection.SetVisible(false);
        if (m_wShapeSection) m_wShapeSection.SetVisible(false);
        if (m_wDeleteSection) m_wDeleteSection.SetVisible(false);

        // One draw session per panel instance. Persistent across show/hide so
        // an in-progress draw survives panel re-entry; the session is
        // cancelled explicitly via cancel action or tool-spinbox change.
        m_ShapeDrawSession = new AG0_TDLShapeDrawSession();
        m_ShapeDrawSession.m_OnCommitted.Insert(OnShapeSessionCommitted);

        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Called by AG0_TDLMenuUI when the panel becomes visible
    //! (SetPanelContent(MARKER_TOOL)). Spawns + populates sub-forms on
    //! first call; cheap on subsequent calls.
    void OnPanelShown()
    {
        if (!m_wPlacedEditBox)
            SpawnPlacedEditBox();

        if (!m_wMilitaryEditBox)
            SpawnMilitaryEditBox();

        if (!m_wShapeEditBox && m_wShapeSection)
            SpawnShapeEditBox();

        // Repopulate the player-owned marker scroll every time we re-enter
        // the panel — markers may have been added/removed by other paths
        // (other players' deletions, API edits, etc.) since we last showed.
        RefreshMarkerList();

        // Restore cross-frontend selection from statics. Must run after the
        // sub-form spawns above so the spinboxes have their item lists in
        // place — SetCurrentItem on an empty spinbox is a no-op.
        ApplyPersistedState();
    }

    //------------------------------------------------------------------------------------------------
    //! Called when panel is hidden. Capture edit-box text into the cross-
    //! frontend statics so the other frontend's panel re-enters with the
    //! same custom-text values. Spinbox indices are already captured in
    //! their own m_OnChanged handlers; only the edit boxes need this
    //! catch-on-hide path since SCR_EditBoxComponent doesn't have a
    //! per-keystroke invoker we subscribe to.
    void OnPanelHidden()
    {
        CapturePersistedText();
    }

    //------------------------------------------------------------------------------------------------
    //! Apply remembered spinbox / edit-box state from the cross-frontend
    //! statics. Sentinel -1 = "no saved value yet, leave the spinbox on its
    //! default". Order matters: type spinbox first so its section-visibility
    //! handler runs before we touch sub-section selectors that depend on
    //! the active section being shown.
    protected void ApplyPersistedState()
    {
        if (m_TypeSpinBox && s_iLastType != -1)
            m_TypeSpinBox.SetCurrentItem(s_iLastType);

        if (m_PlacedIconSpinBox && s_iLastPlacedIcon != -1)
            m_PlacedIconSpinBox.SetCurrentItem(s_iLastPlacedIcon);
        if (m_PlacedColorSpinBox && s_iLastPlacedColor != -1)
            m_PlacedColorSpinBox.SetCurrentItem(s_iLastPlacedColor);
        if (m_PlacedEditBox && !s_sLastPlacedText.IsEmpty())
            m_PlacedEditBox.SetValue(s_sLastPlacedText);

        if (m_MilFactionSpinBox && s_iLastMilFaction != -1)
            m_MilFactionSpinBox.SetCurrentItem(s_iLastMilFaction);
        if (m_MilDimensionSpinBox && s_iLastMilDimension != -1)
            m_MilDimensionSpinBox.SetCurrentItem(s_iLastMilDimension);
        if (m_MilCombo1 && s_iLastMilCombo1 != -1)
            m_MilCombo1.SetCurrentItem(s_iLastMilCombo1);
        if (m_MilCombo2 && s_iLastMilCombo2 != -1)
            m_MilCombo2.SetCurrentItem(s_iLastMilCombo2);
        if (m_MilEditBox && !s_sLastMilText.IsEmpty())
            m_MilEditBox.SetValue(s_sLastMilText);

        if (m_ShapeToolSpinBox && s_iLastShapeTool != -1)
            m_ShapeToolSpinBox.SetCurrentItem(s_iLastShapeTool);
        if (m_ShapeStrokeColorSpinBox && s_iLastShapeStrokeColor != -1)
            m_ShapeStrokeColorSpinBox.SetCurrentItem(s_iLastShapeStrokeColor);
        if (m_ShapeStrokeWidthSpinBox && s_iLastShapeStrokeWidth != -1)
            m_ShapeStrokeWidthSpinBox.SetCurrentItem(s_iLastShapeStrokeWidth);
        if (m_ShapeFillSpinBox && s_iLastShapeFill != -1)
            m_ShapeFillSpinBox.SetCurrentItem(s_iLastShapeFill);
        if (m_ShapeLabelEditBox && !s_sLastShapeLabel.IsEmpty())
            m_ShapeLabelEditBox.SetValue(s_sLastShapeLabel);
    }

    //------------------------------------------------------------------------------------------------
    //! Snapshot the live edit-box values into the cross-frontend statics.
    //! Called on panel hide and just before placing a marker (so place-then-
    //! switch carries the text the user actually placed with).
    protected void CapturePersistedText()
    {
        if (m_PlacedEditBox)
            s_sLastPlacedText = m_PlacedEditBox.GetValue();
        if (m_MilEditBox)
            s_sLastMilText = m_MilEditBox.GetValue();
        if (m_ShapeLabelEditBox)
            s_sLastShapeLabel = m_ShapeLabelEditBox.GetValue();
    }

    //------------------------------------------------------------------------------------------------
    //! Polled from AG0_TDLMenuUI.OnMenuUpdate when MARKER_TOOL panel is
    //! active. Listens for a dedicated TDLPlaceMarker input action — NOT
    //! the global MenuSelect — so navigating selectors / opening combos /
    //! pressing A on a focused widget never accidentally drops a marker.
    //! User binds TDLPlaceMarker to whatever they want (e.g. RB on
    //! gamepad, Enter on keyboard) in the input config.
    //!
    //! Bind a parallel TDLPlaceMarkerPrivate action if/when you want a
    //! separate "place private" path; for now any TDLPlaceMarker fires
    //! a public placement.
    void TickPlaceActionPoll(InputManager im)
    {
        if (!im)
            return;

        // Don't intercept if the user is editing a text field — the action
        // there should commit the field, not place a marker.
        if (IsTextEditFocused())
            return;

        if (im.GetActionTriggered("TDLPlaceMarker"))
            m_OnPlaceRequested.Invoke(false);

        // Variadic shape commit uses geometric close-the-loop instead of a
        // dedicated input action — the draw session itself spots a click
        // near the target closing vertex and auto-commits from AddPoint.
        // Cancel is handled by the panel's Back button via OnBackButtonClicked.
    }

    //------------------------------------------------------------------------------------------------
    //! Returns true when the focused widget is one of our edit boxes —
    //! used to suppress placement actions while typing custom text.
    protected bool IsTextEditFocused()
    {
        Widget focused = GetGame().GetWorkspace().GetFocusedWidget();
        if (!focused)
            return false;

        if (m_PlacedEditBox && focused == m_PlacedEditBox.GetRootWidget())
            return true;
        if (m_MilEditBox && focused == m_MilEditBox.GetRootWidget())
            return true;

        // EditBox widgets often nest the actual text widget several levels
        // deep — walk up the parent chain checking for a match against
        // either edit-box root.
        Widget cur = focused.GetParent();
        while (cur)
        {
            if (m_PlacedEditBox && cur == m_PlacedEditBox.GetRootWidget())
                return true;
            if (m_MilEditBox && cur == m_MilEditBox.GetRootWidget())
                return true;
            cur = cur.GetParent();
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    // PUBLIC ACCESSORS — used by ghost renderer + place handlers
    //------------------------------------------------------------------------------------------------

    //! Build a fresh SCR_MapMarkerBase carrying the panel's current
    //! selection. Caller sets world position and calls
    //! markerMgr.InsertStaticMarker(marker, isLocal). Returns null if
    //! the panel hasn't been shown yet (sub-forms not spawned).
    SCR_MapMarkerBase BuildCurrentMarkerPrototype()
    {
        if (!m_TypeSpinBox)
            return null;

        int type = m_TypeSpinBox.GetCurrentIndex();
        if (type == TYPE_INDEX_MILITARY)
            return BuildMilitaryMarkerPrototype();
        else
            return BuildPlacedMarkerPrototype();
    }

    //------------------------------------------------------------------------------------------------
    // INTERNAL — SPAWN SUB-FORMS
    //------------------------------------------------------------------------------------------------

    protected void SpawnPlacedEditBox()
    {
        if (!m_wPlacedSection)
            return;

        m_wPlacedEditBox = GetGame().GetWorkspace().CreateWidgets(PLACED_LAYOUT, m_wPlacedSection);
        if (!m_wPlacedEditBox)
        {
            Print("[AG0_TDLMarkerToolPanel] Failed to spawn ATAKMapMarkerEditBox layout", LogLevel.WARNING);
            return;
        }

        // Look up named widgets SCOPED to the spawned root — both edit-box
        // layouts use overlapping names (EditBoxRoot, ButtonCancel, etc.)
        // so we must not search from m_wRoot or we'd hit collisions.
        // Spinbox lookups via the static helper (cleaner than
        // FindAnyWidget + cast). Names match the layout contract — see
        // the layout-edit guidance after this refactor.
        m_PlacedIconSpinBox  = SCR_SpinBoxComponent.GetSpinBoxComponent("IconSpinBox",  m_wPlacedEditBox);
        m_PlacedColorSpinBox = SCR_SpinBoxComponent.GetSpinBoxComponent("ColorSpinBox", m_wPlacedEditBox);

        if (m_PlacedIconSpinBox)
            m_PlacedIconSpinBox.m_OnChanged.Insert(OnPlacedIconChanged);
        if (m_PlacedColorSpinBox)
            m_PlacedColorSpinBox.m_OnChanged.Insert(OnPlacedIconChanged);

        // Preview image — lives inside the MarkerPreview SizeLayoutWidget
        // added to ATAKMapMarkerEditBox.layout. UpdatePlacedPreview reads
        // from the live spinbox state and pushes imageset+quad+tint here.
        Widget previewRoot = m_wPlacedEditBox.FindAnyWidget("MarkerPreview");
        if (previewRoot)
        {
            Widget previewImg = previewRoot.FindAnyWidget("MarkerPreviewImage");
            if (previewImg)
                m_wPlacedPreviewImage = ImageWidget.Cast(previewImg);
        }

        Widget editRoot = m_wPlacedEditBox.FindAnyWidget("EditBoxRoot");
        if (editRoot)
            m_PlacedEditBox = SCR_EditBoxComponent.Cast(editRoot.FindHandler(SCR_EditBoxComponent));

        // The vanilla layout's HorizontalLayoutButtons row (Cancel / Public /
        // Private) has been removed from ATAKMapMarkerEditBox.layout —
        // placement and cancel both go through the panel-level
        // MarkerToolPlaceButton / MarkerToolBackButton in MarkerToolContent
        // instead. Single source of truth for those actions, and pressing
        // A on a focused spinbox no longer accidentally triggers the
        // in-form ButtonPublic via SCR_InputButtonComponent's auto-action-fire.

        PopulatePlacedSelectors();
    }

    //------------------------------------------------------------------------------------------------
    protected void SpawnMilitaryEditBox()
    {
        if (!m_wMilitarySection)
            return;

        m_wMilitaryEditBox = GetGame().GetWorkspace().CreateWidgets(MILITARY_LAYOUT, m_wMilitarySection);
        if (!m_wMilitaryEditBox)
        {
            Print("[AG0_TDLMarkerToolPanel] Failed to spawn ATAKMapMilitaryMarkerEditBox layout", LogLevel.WARNING);
            return;
        }

        m_MilFactionSpinBox   = SCR_SpinBoxComponent.GetSpinBoxComponent("FactionSpinBox",   m_wMilitaryEditBox);
        m_MilDimensionSpinBox = SCR_SpinBoxComponent.GetSpinBoxComponent("DimensionSpinBox", m_wMilitaryEditBox);

        if (m_MilFactionSpinBox)
            m_MilFactionSpinBox.m_OnChanged.Insert(OnMilitarySelectorChanged);
        if (m_MilDimensionSpinBox)
            m_MilDimensionSpinBox.m_OnChanged.Insert(OnMilitarySelectorChanged);

        Widget combo1 = m_wMilitaryEditBox.FindAnyWidget("ComboBox1");
        if (combo1)
        {
            m_MilCombo1 = SCR_ComboBoxComponent.Cast(combo1.FindHandler(SCR_ComboBoxComponent));
            if (m_MilCombo1)
                m_MilCombo1.m_OnChanged.Insert(OnMilitarySelectorChanged);
        }

        Widget combo2 = m_wMilitaryEditBox.FindAnyWidget("ComboBox2");
        if (combo2)
        {
            m_MilCombo2 = SCR_ComboBoxComponent.Cast(combo2.FindHandler(SCR_ComboBoxComponent));
            if (m_MilCombo2)
                m_MilCombo2.m_OnChanged.Insert(OnMilitarySelectorChanged);
        }

        Widget editRoot = m_wMilitaryEditBox.FindAnyWidget("EditBoxRoot");
        if (editRoot)
            m_MilEditBox = SCR_EditBoxComponent.Cast(editRoot.FindHandler(SCR_EditBoxComponent));

        // Same as the placed sub-form — HorizontalLayoutButtons row removed
        // from ATAKMapMilitaryMarkerEditBox.layout. Place / Cancel go
        // through the panel-level buttons.

        // Live military symbol preview — dedicated MarkerPreview widget at
        // the top of the edit-box layout (SizeLayoutWidget host with an
        // OverlayWidget child carrying SCR_MilitarySymbolUIComponent).
        // FindHandler walks the widget tree so we don't care whether the
        // component sits on the host or the inner overlay. Replaced the
        // earlier rootFrame0 + FrameSlot positioning hack — this is the
        // clean route now that the layout has a purpose-built preview slot.
        Widget militaryPreview = m_wMilitaryEditBox.FindAnyWidget("MarkerPreview");
        if (militaryPreview)
        {
            m_wMilitaryPreviewRoot = militaryPreview;
            m_MilSymbolUI = SCR_MilitarySymbolUIComponent.Cast(militaryPreview.FindHandler(SCR_MilitarySymbolUIComponent));
            if (!m_MilSymbolUI)
                m_MilSymbolUI = FindMilitarySymbolComponent(militaryPreview);
        }

        PopulateMilitarySelectors();
        UpdateMilitaryPreview();
    }

    //------------------------------------------------------------------------------------------------
    //! Recursive descend looking for SCR_MilitarySymbolUIComponent —
    //! handler is on an OverlayWidget child of the inherited prefab tree
    //! and FindHandler on the parent doesn't always reach descendants.
    protected SCR_MilitarySymbolUIComponent FindMilitarySymbolComponent(Widget w)
    {
        if (!w)
            return null;

        SCR_MilitarySymbolUIComponent comp = SCR_MilitarySymbolUIComponent.Cast(w.FindHandler(SCR_MilitarySymbolUIComponent));
        if (comp)
            return comp;

        Widget child = w.GetChildren();
        while (child)
        {
            comp = FindMilitarySymbolComponent(child);
            if (comp)
                return comp;
            child = child.GetSibling();
        }

        return null;
    }

    //------------------------------------------------------------------------------------------------
    // INTERNAL — POPULATION FROM CONFIG
    //------------------------------------------------------------------------------------------------

    //! Build the icon + color selector items from the placed-marker config.
    //! Icon list is filtered to the first category (we have only "TDL"
    //! in this mod's config — multi-category support would extend this).
    protected void PopulatePlacedSelectors()
    {
        if (!m_PlacedConfig)
            return;

        // Icon entries — filter to first category and remember global indices.
        // SpinBox: ClearAll then AddItem per visible icon, finally SetCurrentItem(0)
        // to land on the first entry. SetCycleMode(true) lets the user wrap
        // around at either end via the d-pad / arrows.
        if (m_PlacedIconSpinBox)
        {
            m_PlacedIconSpinBox.ClearAll();
            m_aPlacedIconGlobalIndex.Clear();

            array<ref SCR_MarkerIconEntry> iconEntries = m_PlacedConfig.GetIconEntries();
            if (iconEntries)
            {
                for (int i = 0; i < iconEntries.Count(); i++)
                {
                    // For v1, accept all entries — single-category config.
                    // Multi-category support: filter on
                    // m_PlacedConfig.GetIconCategoryID(i) == m_iCurrentCategory.
                    SCR_MarkerIconEntry entry = iconEntries[i];
                    if (!entry)
                        continue;

                    // Use the imageset quad as the readable label —
                    // SCR_MarkerIconEntry doesn't expose a display-name
                    // accessor in the public API.
                    ResourceName imageset, imagesetGlow;
                    string quad;
                    if (m_PlacedConfig.GetIconEntry(i, imageset, imagesetGlow, quad))
                    {
                        m_PlacedIconSpinBox.AddItem(quad);
                        m_aPlacedIconGlobalIndex.Insert(i);
                    }
                }
            }

            m_PlacedIconSpinBox.SetCycleMode(true);
            if (m_aPlacedIconGlobalIndex.Count() > 0)
                m_PlacedIconSpinBox.SetCurrentItem(0);
        }

        // Color entries — SCR_MarkerColorEntry exposes GetName() for the
        // human-readable label ("Red" / "Blue" / etc.).
        if (m_PlacedColorSpinBox)
        {
            m_PlacedColorSpinBox.ClearAll();
            array<ref SCR_MarkerColorEntry> colorEntries = m_PlacedConfig.GetColorEntries();
            int colorCount = 0;
            if (colorEntries)
            {
                for (int i = 0; i < colorEntries.Count(); i++)
                {
                    SCR_MarkerColorEntry colorEntry = colorEntries[i];
                    if (!colorEntry)
                        continue;
                    m_PlacedColorSpinBox.AddItem(colorEntry.GetName());
                    colorCount++;
                }
            }
            m_PlacedColorSpinBox.SetCycleMode(true);
            if (colorCount > 0)
                m_PlacedColorSpinBox.SetCurrentItem(0);
        }

        // Stamp the initial preview so the user sees the default icon+colour
        // combo before any spinbox interaction. Without this the
        // MarkerPreviewImage stays at whatever it was authored as in the
        // layout (a blank/white box) until the user nudges a spinbox.
        UpdatePlacedPreview();
    }

    //------------------------------------------------------------------------------------------------
    //! Build the faction + dimension selector items + icon-flag combos
    //! from the military-marker config.
    protected void PopulateMilitarySelectors()
    {
        if (!m_MilitaryConfig)
            return;

        // Faction list — use the human-readable translation (BLUFOR, OPFOR,
        // INDFOR, CIVILIAN) instead of the raw enum index. GetTranslation
        // resolves the localised faction name from the entry's m_sTranslation.
        if (m_MilFactionSpinBox)
        {
            m_MilFactionSpinBox.ClearAll();
            int factionCount = 0;
            array<ref SCR_MarkerMilitaryFactionEntry> factionEntries = m_MilitaryConfig.GetMilitaryFactionEntries();
            if (factionEntries)
            {
                for (int i = 0; i < factionEntries.Count(); i++)
                {
                    SCR_MarkerMilitaryFactionEntry entry = factionEntries[i];
                    if (!entry)
                        continue;
                    m_MilFactionSpinBox.AddItem(entry.GetTranslation());
                    factionCount++;
                }
            }
            m_MilFactionSpinBox.SetCycleMode(true);
            if (factionCount > 0)
                m_MilFactionSpinBox.SetCurrentItem(0);
        }

        // Dimension list
        if (m_MilDimensionSpinBox)
        {
            m_MilDimensionSpinBox.ClearAll();
            int dimCount = 0;
            array<ref SCR_MarkerMilitaryDimension> dimensions = m_MilitaryConfig.GetMilitaryDimensions();
            if (dimensions)
            {
                for (int i = 0; i < dimensions.Count(); i++)
                {
                    SCR_MarkerMilitaryDimension dim = dimensions[i];
                    if (!dim)
                        continue;
                    m_MilDimensionSpinBox.AddItem(dim.GetTranslation());
                    dimCount++;
                }
            }
            m_MilDimensionSpinBox.SetCycleMode(true);
            if (dimCount > 0)
                m_MilDimensionSpinBox.SetCurrentItem(0);
        }

        // Icon-flag combos — EMilitarySymbolIcon enum values aren't in the
        // public API but the engine accepts the underlying ints, so we
        // populate with labeled bit values matching MIL-STD-2525 unit-type
        // conventions. Vanilla M-map renders these via the SCR_MilitarySymbol's
        // SetIcons; the live preview at top of the form will reflect the
        // chosen combination so you can verify what each flag visually means.
        // If a name doesn't match what the preview shows, edit the labels in
        // PopulateIconFlagCombo — the int values are the contract with the
        // engine, the strings are just hints.
        //
        // Type 1 + Type 2 — both populate from SCR_MapMarkerEntryMilitary's
        // GetMilitaryTypes() array. Each SCR_MarkerMilitaryType entry bundles
        // one EMilitarySymbolIcon flag with its localised translation, and
        // vanilla M-map exposes the same single array to both combos. Order
        // and content come from the config (matches whatever the user sees
        // on the M-map for parity), and the OR'd result of both selections
        // feeds SCR_MilitarySymbol.SetIcons. No manual enum walking needed.
        PopulateMilitaryTypesCombo(m_MilCombo1);
        PopulateMilitaryTypesCombo(m_MilCombo2);
    }

    //------------------------------------------------------------------------------------------------
    //! Populate one icon-flag combo from SCR_MapMarkerEntryMilitary's
    //! GetMilitaryTypes() array. This is the canonical config-driven path
    //! that vanilla M-map uses — each SCR_MarkerMilitaryType entry exposes
    //! a single EMilitarySymbolIcon flag (GetType) + localised label
    //! (GetTranslation), and vanilla iterates the same array for both
    //! combos. Following that pattern gives full M-map parity for whatever
    //! types the config defines, in the config's own order.
    //!
    //! Items store their bit value as Managed (boxed) item data so
    //! ResolveCurrentIconFlags can read back via GetCurrentItemData
    //! without maintaining an index-to-value parallel array.
    protected void PopulateMilitaryTypesCombo(SCR_ComboBoxComponent combo)
    {
        if (!combo || !m_MilitaryConfig)
            return;

        combo.ClearAll();

        // First entry is always "None" so the user can opt out of this
        // combo's contribution to the OR'd icon flags.
        AddIconFlagItem(combo, 0, "None");

        array<ref SCR_MarkerMilitaryType> types = m_MilitaryConfig.GetMilitaryTypes();
        if (types)
        {
            for (int i = 0; i < types.Count(); i++)
            {
                SCR_MarkerMilitaryType type = types[i];
                if (!type)
                    continue;
                AddIconFlagItem(combo, type.GetType(), type.GetTranslation());
            }
        }

        combo.SetCurrentItem(0);  // Default: no flag from this combo
    }

    //------------------------------------------------------------------------------------------------
    //! Add a (label, int-flag-value) pair to a SCR_ComboBoxComponent. Stores
    //! the bit value as boxed-int data so ResolveCurrentIconFlags can read
    //! it back without a parallel array.
    protected void AddIconFlagItem(SCR_ComboBoxComponent combo, int flagValue, string label)
    {
        if (!combo)
            return;
        AG0_TDLBoxedInt boxed = new AG0_TDLBoxedInt();
        boxed.m_iValue = flagValue;
        combo.AddItem(label, false, boxed);
    }

    //------------------------------------------------------------------------------------------------
    // INTERNAL — LIVE PREVIEW
    //------------------------------------------------------------------------------------------------

    //! Push the panel's current military selection into the inline
    //! SCR_MilitarySymbolUIComponent. Called whenever any of the four
    //! military pickers (faction / dimension / Type 1 / Type 2) changes.
    //!
    //! Builds a fresh SCR_MilitarySymbol each call rather than mutating a
    //! cached one — vanilla SCR_TutorialMapUIBase.UpdateMilitarySymbol does
    //! the same, and Update() apparently does a ref-identity compare that
    //! skips redraws when the same object is passed back. Allocating a
    //! new symbol per change ensures the visual actually refreshes.
    protected void UpdateMilitaryPreview()
    {
        if (!m_MilitaryConfig || !m_MilSymbolUI)
            return;

        EMilitarySymbolIdentity identity  = ResolveCurrentFactionIdentity();
        EMilitarySymbolDimension dimension = ResolveCurrentDimension();
        EMilitarySymbolIcon iconFlags     = ResolveCurrentIconFlags();

        SCR_MilitarySymbol symbol = new SCR_MilitarySymbol();
        symbol.SetIdentity(identity);
        symbol.SetDimension(dimension);
        symbol.SetIcons(iconFlags);

        m_MilSymbolUI.Update(symbol);

        // Apply the faction's tint to the preview's underlying ImageWidgets.
        // SCR_MilitarySymbolUIComponent doesn't expose a SetColor — the live
        // M-map markers tint via SCR_MapMarkerWidgetComponent.SetColor, a
        // different component on the marker layout. For the preview we walk
        // the widget tree from m_wMilitaryPreviewRoot and tint every
        // ImageWidget we find, which is what the M-map's SetColor does
        // internally.
        Color factionColor = ResolveCurrentFactionColor();
        if (m_wMilitaryPreviewRoot)
            TintAllImageWidgets(m_wMilitaryPreviewRoot, factionColor);
    }

    //------------------------------------------------------------------------------------------------
    //! Resolve the colour of the currently-selected faction entry. Falls back
    //! to white when no faction is selected, matching the live marker's
    //! "no faction colour configured" render.
    protected Color ResolveCurrentFactionColor()
    {
        if (m_MilFactionSpinBox && m_MilitaryConfig)
        {
            int idx = m_MilFactionSpinBox.GetCurrentIndex();
            array<ref SCR_MarkerMilitaryFactionEntry> entries = m_MilitaryConfig.GetMilitaryFactionEntries();
            if (entries && idx >= 0 && idx < entries.Count())
            {
                SCR_MarkerMilitaryFactionEntry entry = entries[idx];
                if (entry)
                    return entry.GetColor();
            }
        }
        return Color.White;
    }

    //------------------------------------------------------------------------------------------------
    //! Recursively walk a widget tree and apply the colour to every
    //! ImageWidget found. Used for the military preview tint (and the
    //! per-entry-card military symbol tint in the marker scroll list).
    //! ImageWidget.SetColor is the only colour hook the engine exposes for
    //! image fills — so we have to fan it out manually rather than rely on
    //! a parent-level "Inherit Color" that the symbol's runtime widgets
    //! aren't authored to honour.
    protected void TintAllImageWidgets(Widget root, Color color)
    {
        if (!root)
            return;

        ImageWidget img = ImageWidget.Cast(root);
        if (img)
            img.SetColor(color);

        Widget child = root.GetChildren();
        while (child)
        {
            TintAllImageWidgets(child, color);
            child = child.GetSibling();
        }
    }

    //------------------------------------------------------------------------------------------------
    protected EMilitarySymbolIdentity ResolveCurrentFactionIdentity()
    {
        if (!m_MilFactionSpinBox || m_MilFactionSpinBox.GetCurrentIndex() < 0)
            return 0;  // Default — first faction in enum

        array<ref SCR_MarkerMilitaryFactionEntry> factionEntries = m_MilitaryConfig.GetMilitaryFactionEntries();
        if (!factionEntries || m_MilFactionSpinBox.GetCurrentIndex() >= factionEntries.Count())
            return 0;

        return factionEntries[m_MilFactionSpinBox.GetCurrentIndex()].GetFactionIdentity();
    }

    //------------------------------------------------------------------------------------------------
    protected EMilitarySymbolDimension ResolveCurrentDimension()
    {
        if (!m_MilDimensionSpinBox || m_MilDimensionSpinBox.GetCurrentIndex() < 0)
            return 0;

        array<ref SCR_MarkerMilitaryDimension> dimensions = m_MilitaryConfig.GetMilitaryDimensions();
        if (!dimensions || m_MilDimensionSpinBox.GetCurrentIndex() >= dimensions.Count())
            return 0;

        return dimensions[m_MilDimensionSpinBox.GetCurrentIndex()].GetDimension();
    }

    //------------------------------------------------------------------------------------------------
    //! Read current Type 1 + Type 2 flag selections and OR them into a
    //! single EMilitarySymbolIcon value. Each combo stores its bit value
    //! as boxed-int item data — we pull via GetCurrentItemData rather than
    //! an index lookup so labels and bit values can be edited
    //! independently without sync bugs.
    protected EMilitarySymbolIcon ResolveCurrentIconFlags()
    {
        int flags = 0;
        flags = flags | ResolveComboFlagValue(m_MilCombo1);
        flags = flags | ResolveComboFlagValue(m_MilCombo2);
        return flags;
    }

    //------------------------------------------------------------------------------------------------
    //! Helper — pull the int flag value from the combo's currently-selected
    //! item data. Returns 0 if anything's missing (combo null, no selection,
    //! data isn't a boxed int).
    protected int ResolveComboFlagValue(SCR_ComboBoxComponent combo)
    {
        if (!combo)
            return 0;
        Managed data = combo.GetCurrentItemData();
        if (!data)
            return 0;
        AG0_TDLBoxedInt boxed = AG0_TDLBoxedInt.Cast(data);
        if (!boxed)
            return 0;
        return boxed.m_iValue;
    }

    //------------------------------------------------------------------------------------------------
    // INTERNAL — MARKER PROTOTYPE BUILDERS
    //------------------------------------------------------------------------------------------------

    //! Build a placed (TDL custom) marker carrying the current selection.
    //! Caller sets world position before InsertStaticMarker.
    protected SCR_MapMarkerBase BuildPlacedMarkerPrototype()
    {
        SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
        marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);

        // Icon entry — spinbox index → global index mapping table built
        // during PopulatePlacedSelectors. -1 falls back to entry 0.
        int iconEntryIndex = 0;
        if (m_PlacedIconSpinBox && m_PlacedIconSpinBox.GetCurrentIndex() >= 0
            && m_PlacedIconSpinBox.GetCurrentIndex() < m_aPlacedIconGlobalIndex.Count())
        {
            iconEntryIndex = m_aPlacedIconGlobalIndex[m_PlacedIconSpinBox.GetCurrentIndex()];
        }
        marker.SetIconEntry(iconEntryIndex);

        int colorEntryIndex = 0;
        if (m_PlacedColorSpinBox && m_PlacedColorSpinBox.GetCurrentIndex() >= 0)
            colorEntryIndex = m_PlacedColorSpinBox.GetCurrentIndex();
        marker.SetColorEntry(colorEntryIndex);

        if (m_PlacedEditBox)
            marker.SetCustomText(m_PlacedEditBox.GetValue());

        // Rotation slider not hooked in v1 — defaults to 0
        return marker;
    }

    //------------------------------------------------------------------------------------------------
    //! Build a military marker via SCR_MapMarkerManagerComponent's helper —
    //! handles configID encoding (faction + dimension determinator math)
    //! that we'd otherwise have to replicate manually.
    protected SCR_MapMarkerBase BuildMilitaryMarkerPrototype()
    {
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
            return null;

        EMilitarySymbolIdentity identity  = ResolveCurrentFactionIdentity();
        EMilitarySymbolDimension dimension = ResolveCurrentDimension();
        EMilitarySymbolIcon iconFlags     = ResolveCurrentIconFlags();

        // Dev print — investigates the user-reported "dimension doesn't
        // render on the placed marker" bug. PrepareMilitaryMarker encodes
        // dimensionID*100 + factionID into m_iConfigID; if the values
        // here look right but the placed marker doesn't visually reflect
        // dimension, the regression is in the render path
        // (CreateVanillaMarkerWidget) not in the panel state. Remove this
        // print once the dimension issue is understood.
        int factionSpinIdx = -99;
        if (m_MilFactionSpinBox)
            factionSpinIdx = m_MilFactionSpinBox.GetCurrentIndex();
        int dimSpinIdx = -99;
        if (m_MilDimensionSpinBox)
            dimSpinIdx = m_MilDimensionSpinBox.GetCurrentIndex();
        Print(string.Format("[AG0_TDLMarkerToolPanel] Build military: identity=%1 dimension=%2 iconFlags=%3 spinIdx{f=%4 d=%5}",
            identity, dimension, iconFlags, factionSpinIdx, dimSpinIdx), LogLevel.NORMAL);

        SCR_MapMarkerBase marker = markerMgr.PrepareMilitaryMarker(identity, dimension, iconFlags);
        if (!marker)
        {
            Print("[AG0_TDLMarkerToolPanel] PrepareMilitaryMarker returned null — likely faction or dimension not registered in SCR_MapMarkerEntryMilitary config", LogLevel.WARNING);
            return null;
        }

        Print(string.Format("[AG0_TDLMarkerToolPanel] PrepareMilitaryMarker → configID=%1", marker.GetMarkerConfigID()), LogLevel.NORMAL);

        if (m_MilEditBox)
            marker.SetCustomText(m_MilEditBox.GetValue());

        return marker;
    }

    //------------------------------------------------------------------------------------------------
    // INTERNAL — EVENT HANDLERS
    //------------------------------------------------------------------------------------------------

    //! Parameterless to match the safe button-handler convention — read
    //! current state via GetCurrentIndex on the cached component ref so
    //! we don't depend on the invoker's exact signature.
    protected void OnTypeSpinBoxChanged()
    {
        if (!m_TypeSpinBox)
            return;

        int index = m_TypeSpinBox.GetCurrentIndex();
        if (m_wPlacedSection)
            m_wPlacedSection.SetVisible(index == TYPE_INDEX_PLACED);
        if (m_wMilitarySection)
            m_wMilitarySection.SetVisible(index == TYPE_INDEX_MILITARY);
        if (m_wShapeSection)
            m_wShapeSection.SetVisible(index == TYPE_INDEX_SHAPE);
        if (m_wDeleteSection)
            m_wDeleteSection.SetVisible(index == TYPE_INDEX_DELETE);

        // Leaving shape mode mid-draw discards the in-progress geometry.
        // Re-entering re-arms with the spinbox's tool selection, matching
        // the user's expectation that switching modes is a clean reset.
        if (index != TYPE_INDEX_SHAPE && m_ShapeDrawSession && m_ShapeDrawSession.IsArmed())
            m_ShapeDrawSession.Cancel();
        if (index == TYPE_INDEX_SHAPE)
            ReArmShapeSession();

        // Mirror to static for cross-frontend persistence.
        s_iLastType = index;
    }

    //------------------------------------------------------------------------------------------------
    //! Fired by both the icon spinbox and the colour spinbox — they share
    //! this handler because the placed preview reflects icon + colour
    //! together. Parameterless to tolerate the different invoker
    //! signatures: SCR_SpinBoxComponent.m_OnChanged passes the component
    //! itself (Class), SCR_ComboBoxComponent.m_OnChanged passes the same.
    //! Reading state via the cached spinbox refs in UpdatePlacedPreview
    //! keeps us decoupled from invoker payload shape.
    protected void OnPlacedIconChanged()
    {
        UpdatePlacedPreview();

        // Mirror to statics — both icon and color update through this same
        // handler (the placed preview reflects both together), so capture
        // both indices regardless of which spinbox changed.
        if (m_PlacedIconSpinBox)
            s_iLastPlacedIcon = m_PlacedIconSpinBox.GetCurrentIndex();
        if (m_PlacedColorSpinBox)
            s_iLastPlacedColor = m_PlacedColorSpinBox.GetCurrentIndex();
    }

    //------------------------------------------------------------------------------------------------
    //! Refresh the placed-marker preview ImageWidget — pull the active
    //! SCR_MarkerIconEntry's imageset+quad and the active SCR_MarkerColorEntry's
    //! Color, push both onto m_wPlacedPreviewImage. Called on icon-spinbox
    //! and colour-spinbox change events.
    protected void UpdatePlacedPreview()
    {
        if (!m_PlacedConfig || !m_wPlacedPreviewImage)
            return;

        // Resolve the active icon entry — same selector→global-index
        // mapping that BuildPlacedMarkerPrototype uses, so the preview
        // stays in lock-step with what'll actually get placed.
        int iconEntryIndex = 0;
        if (m_PlacedIconSpinBox && m_PlacedIconSpinBox.GetCurrentIndex() >= 0
            && m_PlacedIconSpinBox.GetCurrentIndex() < m_aPlacedIconGlobalIndex.Count())
        {
            iconEntryIndex = m_aPlacedIconGlobalIndex[m_PlacedIconSpinBox.GetCurrentIndex()];
        }

        // Pull imageset + quad from the entry. SCR_MarkerIconEntry.GetIconResource
        // returns void with three out params (imageset, glow imageset, quad).
        ResourceName imageset, imagesetGlow;
        string quad;
        array<ref SCR_MarkerIconEntry> iconEntries = m_PlacedConfig.GetIconEntries();
        if (iconEntries && iconEntryIndex >= 0 && iconEntryIndex < iconEntries.Count())
        {
            SCR_MarkerIconEntry entry = iconEntries[iconEntryIndex];
            if (entry)
                entry.GetIconResource(imageset, imagesetGlow, quad);
        }

        if (!imageset.IsEmpty() && !quad.IsEmpty())
            m_wPlacedPreviewImage.LoadImageFromSet(0, imageset, quad);

        // Apply the colour-entry tint. SCR_MarkerColorEntry.GetColor returns
        // a Color object; ImageWidget.SetColor takes Color directly.
        int colorIdx = 0;
        if (m_PlacedColorSpinBox && m_PlacedColorSpinBox.GetCurrentIndex() >= 0)
            colorIdx = m_PlacedColorSpinBox.GetCurrentIndex();

        array<ref SCR_MarkerColorEntry> colorEntries = m_PlacedConfig.GetColorEntries();
        if (colorEntries && colorIdx >= 0 && colorIdx < colorEntries.Count())
        {
            SCR_MarkerColorEntry colorEntry = colorEntries[colorIdx];
            if (colorEntry)
                m_wPlacedPreviewImage.SetColor(colorEntry.GetColor());
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Parameterless to match the spinbox/combo invoker payload — same
    //! reasoning as OnPlacedIconChanged. Reads state via the cached refs
    //! inside UpdateMilitaryPreview rather than depending on a passed arg.
    protected void OnMilitarySelectorChanged()
    {
        UpdateMilitaryPreview();

        // Mirror to statics. All four military selectors share this handler
        // so we capture the lot at once — cheap, and keeps the statics in
        // a consistent snapshot regardless of which one changed.
        if (m_MilFactionSpinBox)
            s_iLastMilFaction = m_MilFactionSpinBox.GetCurrentIndex();
        if (m_MilDimensionSpinBox)
            s_iLastMilDimension = m_MilDimensionSpinBox.GetCurrentIndex();
        if (m_MilCombo1)
            s_iLastMilCombo1 = m_MilCombo1.GetCurrentIndex();
        if (m_MilCombo2)
            s_iLastMilCombo2 = m_MilCombo2.GetCurrentIndex();
    }

    //------------------------------------------------------------------------------------------------
    //! Place a marker via the marker manager. Caller (the menu) supplies
    //! the world position resolved from either the map cursor (PC) or the
    //! canvas centre (console).
    void PlaceCurrentMarker(vector worldPos, bool isLocal)
    {
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
            return;

        SCR_MapMarkerBase marker = BuildCurrentMarkerPrototype();
        if (!marker)
            return;

        marker.SetWorldPos((int)worldPos[0], (int)worldPos[2]);
        markerMgr.InsertStaticMarker(marker, isLocal);

        // Snapshot edit-box text into cross-frontend statics so a follow-up
        // place from the other frontend defaults to the same custom label.
        CapturePersistedText();

        // The newly-placed marker is owned by us, so refresh the scroll list
        // so the player can see (and delete) the entry without re-opening the
        // panel. RefreshMarkerList early-outs cleanly if the list widget isn't
        // present, so this is safe before the layout has been authored.
        RefreshMarkerList();
    }

    //------------------------------------------------------------------------------------------------
    // PLAYER MARKER SCROLL LIST
    //------------------------------------------------------------------------------------------------

    //! Rebuild the player-owned marker scroll list from
    //! SCR_MapMarkerManagerComponent's static + disabled marker arrays.
    //! Filter: PLACED_CUSTOM or PLACED_MILITARY whose GetMarkerOwnerID
    //! matches the local player. Reads both lists because vanilla SCR's
    //! Update() shuffles markers between them based on M-map visibility,
    //! so GetStaticMarkers() alone misses markers placed off-frame
    //! (see project memory: vanilla_marker_enum). Cleared and rebuilt
    //! wholesale — entry counts are tiny (single-digit per player) so an
    //! incremental diff isn't worth the bookkeeping.
    void RefreshMarkerList()
    {
        if (!m_wMarkerList)
        {
            Print("[AG0_TDLMarkerToolPanel] RefreshMarkerList: m_wMarkerList is null — layout missing the MarkerList VerticalLayout?", LogLevel.WARNING);
            return;
        }

        ClearMarkerList();

        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
        {
            Print("[AG0_TDLMarkerToolPanel] RefreshMarkerList: marker manager not available", LogLevel.WARNING);
            return;
        }

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        int selfPlayerId = -1;
        if (controller)
            selfPlayerId = controller.GetPlayerId();

        // Union both lists — same trick as AG0_TDLDisplayController.UpdateVanillaMarkers.
        array<SCR_MapMarkerBase> allMarkers = markerMgr.GetStaticMarkers();
        array<SCR_MapMarkerBase> disabled = markerMgr.GetDisabledMarkers();
        if (disabled)
        {
            foreach (SCR_MapMarkerBase d : disabled)
            {
                if (d && allMarkers.Find(d) == -1)
                    allMarkers.Insert(d);
            }
        }

        // Drop pending-delete IDs that the manager has already cleared —
        // tracks the post-RPC state so a transient pending entry doesn't
        // linger forever if the server confirms the removal.
        for (int p = m_aPendingDeletes.Count() - 1; p >= 0; p--)
        {
            int pendingId = m_aPendingDeletes[p];
            bool stillThere = false;
            foreach (SCR_MapMarkerBase m : allMarkers)
            {
                if (m && m.GetMarkerID() == pendingId)
                {
                    stillThere = true;
                    break;
                }
            }
            if (!stillThere)
                m_aPendingDeletes.Remove(p);
        }

        int totalSeen   = 0;
        int totalKept   = 0;
        int totalSkipped = 0;
        foreach (SCR_MapMarkerBase marker : allMarkers)
        {
            if (!marker)
                continue;
            totalSeen++;

            int t = marker.GetType();
            if (t != SCR_EMapMarkerType.PLACED_CUSTOM && t != SCR_EMapMarkerType.PLACED_MILITARY)
            {
                totalSkipped++;
                continue;
            }

            // Owner-ID filter — keep only markers we own. We accept selfPlayerId
            // <= 0 as a "show everything" fallback for cases where the player
            // controller hasn't reported a valid playerID yet (early menu open
            // on dedicated server). Better to over-show than blank the list.
            int ownerId = marker.GetMarkerOwnerID();
            if (selfPlayerId > 0 && ownerId > 0 && ownerId != selfPlayerId)
            {
                totalSkipped++;
                continue;
            }

            // Skip markers awaiting their AskRemoveStaticMarker round-trip.
            // The RPC has been sent but the manager still tracks them; without
            // this gate the entry would visibly come back between the click
            // and the server broadcast.
            if (m_aPendingDeletes.Find(marker.GetMarkerID()) != -1)
            {
                totalSkipped++;
                continue;
            }

            CreateMarkerEntry(marker);
            totalKept++;
        }

        Print(string.Format("[AG0_TDLMarkerToolPanel] RefreshMarkerList: selfPlayerId=%1 staticTotal=%2 disabledTotal=%3 seen=%4 kept=%5 skipped=%6",
            selfPlayerId,
            markerMgr.GetStaticMarkers().Count(),
            markerMgr.GetDisabledMarkers().Count(),
            totalSeen, totalKept, totalSkipped),
            LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    //! Drop all spawned entry cards + their pinned click handlers. Called
    //! before every Refresh and before delete-driven re-renders. Order
    //! matters: handlers are dropped first since they hold a back-pointer
    //! to this panel; widgets follow.
    protected void ClearMarkerList()
    {
        foreach (Widget w : m_aMarkerEntryWidgets)
        {
            if (w)
                w.RemoveFromHierarchy();
        }
        m_aMarkerEntryWidgets.Clear();
        m_aMarkerEntryHandlers.Clear();
    }

    //------------------------------------------------------------------------------------------------
    //! Spawn one ATAKMapMarkerEntry card for the given marker, populate it
    //! (icon + text + military symbol as appropriate), and bind its OnClicked
    //! to a fresh AG0_TDLMarkerEntryHandler that captures the marker ID.
    //! Per-card handler lets us reuse the handy SCR_ModularButtonComponent
    //! invoker without needing a parameterised closure (which Enfusion lacks).
    protected void CreateMarkerEntry(SCR_MapMarkerBase marker)
    {
        if (!m_wMarkerList || !marker)
            return;

        Widget card = GetGame().GetWorkspace().CreateWidgets(MARKER_ENTRY_LAYOUT, m_wMarkerList);
        if (!card)
        {
            Print(string.Format("[AG0_TDLMarkerToolPanel] CreateMarkerEntry: CreateWidgets failed for layout '%1' — check the GUID matches ATAKMapMarkerEntry.layout.meta", MARKER_ENTRY_LAYOUT), LogLevel.WARNING);
            return;
        }

        m_aMarkerEntryWidgets.Insert(card);

        // Body text — use the marker's custom text when set, otherwise fall
        // back to a stable identifier so the card isn't visually empty.
        TextWidget markerText = TextWidget.Cast(card.FindAnyWidget("MarkerText"));
        if (markerText)
        {
            string label = marker.GetCustomText();
            if (!label || label.IsEmpty())
                label = string.Format("Marker #%1", marker.GetMarkerID());
            markerText.SetText(label);
        }

        // Visual — placed/custom uses the icon+colour entries from the marker
        // config; military uses the SCR_MilitarySymbolUIComponent baked into
        // the entry's SymbolWidget. We toggle visibility between the two.
        Widget iconWidget   = card.FindAnyWidget("MarkerIcon");
        Widget glowWidget   = card.FindAnyWidget("MarkerIconGlow");
        Widget symbolWidget = card.FindAnyWidget("SymbolWidget");

        int t = marker.GetType();
        if (t == SCR_EMapMarkerType.PLACED_MILITARY)
        {
            if (iconWidget) iconWidget.SetVisible(false);
            if (glowWidget) glowWidget.SetVisible(false);
            if (symbolWidget)
            {
                symbolWidget.SetVisible(true);
                ApplyMilitarySymbolToEntry(symbolWidget, marker);
            }
        }
        else
        {
            // PLACED_CUSTOM
            if (symbolWidget) symbolWidget.SetVisible(false);
            if (iconWidget)   iconWidget.SetVisible(true);
            ApplyPlacedIconToEntry(iconWidget, marker);
        }

        // Wire the click → delete path. The handler is a ScriptedWidgetComponent
        // attached via AddHandler — engine then routes OnClick to it directly,
        // sidestepping the cross-instance ScriptInvoker binding issue.
        // We pin the handler in m_aMarkerEntryHandlers so it stays alive across
        // Refresh cycles (ClearMarkerList drops the array before rebuilding).
        ButtonWidget btnWidget = ButtonWidget.Cast(card);
        if (btnWidget)
        {
            AG0_TDLMarkerEntryHandler handler = new AG0_TDLMarkerEntryHandler();
            handler.m_iMarkerId = marker.GetMarkerID();
            handler.m_Marker = marker;
            handler.m_Panel = this;
            m_aMarkerEntryHandlers.Insert(handler);

            btnWidget.AddHandler(handler);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Stamp the placed-marker icon entry's imageset+quad onto the card's
    //! MarkerIcon ImageWidget and apply the colour-entry tint. Mirrors the
    //! preview-image code path in UpdatePlacedPreview.
    protected void ApplyPlacedIconToEntry(Widget iconWidget, SCR_MapMarkerBase marker)
    {
        if (!iconWidget || !marker || !m_PlacedConfig)
            return;

        ImageWidget img = ImageWidget.Cast(iconWidget);
        if (!img)
            return;

        int iconEntryIndex = marker.GetIconEntry();
        ResourceName imageset, imagesetGlow;
        string quad;
        array<ref SCR_MarkerIconEntry> iconEntries = m_PlacedConfig.GetIconEntries();
        if (iconEntries && iconEntryIndex >= 0 && iconEntryIndex < iconEntries.Count())
        {
            SCR_MarkerIconEntry entry = iconEntries[iconEntryIndex];
            if (entry)
                entry.GetIconResource(imageset, imagesetGlow, quad);
        }

        if (!imageset.IsEmpty() && !quad.IsEmpty())
            img.LoadImageFromSet(0, imageset, quad);

        // Colour tint — same lookup as the preview path.
        int colorIdx = marker.GetColorEntry();
        array<ref SCR_MarkerColorEntry> colorEntries = m_PlacedConfig.GetColorEntries();
        if (colorEntries && colorIdx >= 0 && colorIdx < colorEntries.Count())
        {
            SCR_MarkerColorEntry colorEntry = colorEntries[colorIdx];
            if (colorEntry)
                img.SetColor(colorEntry.GetColor());
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Drive the SCR_MilitarySymbolUIComponent inside the entry card's
    //! SymbolWidget with a freshly-built SCR_MilitarySymbol decoded from
    //! the marker's stored configID + faction flags. Mirrors how vanilla
    //! M-map renders military markers — same decode pattern as
    //! AG0_TDLDisplayController.CreateVanillaMarkerWidget: pull configID
    //! off the marker, divmod by FACTION/DIMENSION_DETERMINATOR, look up
    //! the faction entry + dimension, OR in marker.GetFlags() as the icon
    //! bits.
    protected void ApplyMilitarySymbolToEntry(Widget symbolWidget, SCR_MapMarkerBase marker)
    {
        if (!symbolWidget || !marker || !m_MilitaryConfig)
            return;

        SCR_MilitarySymbolUIComponent symUI = SCR_MilitarySymbolUIComponent.Cast(symbolWidget.FindHandler(SCR_MilitarySymbolUIComponent));
        if (!symUI)
            symUI = FindMilitarySymbolComponent(symbolWidget);
        if (!symUI)
            return;

        int configID = marker.GetMarkerConfigID();
        int factionID   = configID % m_MilitaryConfig.FACTION_DETERMINATOR;
        int dimensionID = configID * m_MilitaryConfig.DIMENSION_DETERMINATOR;

        array<ref SCR_MarkerMilitaryFactionEntry> factionEntries = m_MilitaryConfig.GetMilitaryFactionEntries();
        array<ref SCR_MarkerMilitaryDimension>    dimensions     = m_MilitaryConfig.GetMilitaryDimensions();

        SCR_MilitarySymbol milSymbol = new SCR_MilitarySymbol();
        Color tint = Color.White;
        if (factionEntries && dimensions
            && factionEntries.IsIndexValid(factionID) && dimensions.IsIndexValid(dimensionID))
        {
            SCR_MarkerMilitaryFactionEntry factionEntry = factionEntries[factionID];
            milSymbol.SetIdentity(factionEntry.GetFactionIdentity());
            milSymbol.SetDimension(dimensions[dimensionID].GetDimension());
            milSymbol.SetIcons(marker.GetFlags());
            tint = factionEntry.GetColor();
        }
        // If decode failed, fall back to a default symbol so the entry still
        // renders visibly rather than blanking out.

        symUI.Update(milSymbol);
        // Apply faction tint — same reasoning as UpdateMilitaryPreview.
        TintAllImageWidgets(symbolWidget, tint);
    }

    //------------------------------------------------------------------------------------------------
    //! Called by AG0_TDLMarkerEntryHandler.OnEntryClicked when the user
    //! activates an entry card. Resolves the marker by ID against the live
    //! manager state (handles the race where another path already deleted
    //! it), removes via the public client→server path, then refreshes the
    //! scroll so the deleted card disappears.
    //! Vanilla M-map deletes player-owned markers via SCR_MapMarkerSyncComponent
    //! on the local PlayerController — the sync component RPCs the markerID
    //! up to the server, which calls SCR_MapMarkerManagerComponent.OnAskRemoveStaticMarker
    //! and broadcasts the removal back to all clients. The marker manager's
    //! public RemoveStaticMarker(marker) is NOT the right entry point: it
    //! only operates on markers in m_aStaticMarkers (skipping anything in
    //! m_aDisabledMarkers, which is where vanilla M-map's Update() shuffles
    //! off-frame markers — see project memory: vanilla_marker_enum), and
    //! its semantics are "I have authority to remove this object" rather
    //! than "Ask the server to remove on my behalf". Going through the sync
    //! component handles both lists transparently and keeps proper MP
    //! ownership/limit checks (m_bIsDeleteRestricted, m_iPlacedMarkerLimit)
    //! that vanilla enforces.
    void OnMarkerEntryClicked(SCR_MapMarkerBase marker, int cachedMarkerId)
    {
        Print(string.Format("[AG0_TDLMarkerToolPanel] OnMarkerEntryClicked: cachedMarkerId=%1 marker=%2", cachedMarkerId, marker), LogLevel.NORMAL);

        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
        {
            Print("[AG0_TDLMarkerToolPanel] OnMarkerEntryClicked: no marker manager", LogLevel.WARNING);
            return;
        }

        // If the cached marker pointer went stale (server cleanup, etc.),
        // walk both lists by cached ID as a fallback. We need *some* live
        // marker reference because vanilla AskRemoveStaticMarker does a
        // GetStaticMarkerByID sanity check internally and silently bails
        // when the lookup fails — so we also need the marker to be in the
        // static list, not the disabled list, before we send the RPC.
        if (!marker)
            marker = FindMarkerInBothLists(markerMgr, cachedMarkerId);
        if (!marker)
        {
            Print(string.Format("[AG0_TDLMarkerToolPanel] OnMarkerEntryClicked: marker %1 not in static or disabled — already removed?", cachedMarkerId), LogLevel.NORMAL);
            // Drop the entry from the UI either way; manager state is already past us.
            RemoveCardForMarker(cachedMarkerId);
            return;
        }

        // Re-enable if currently in m_aDisabledMarkers. Vanilla's
        // SCR_MapMarkerSyncComponent.AskRemoveStaticMarker calls
        // markerMgr.GetStaticMarkerByID(id), which only searches
        // m_aStaticMarkers. Off-frame markers shuffled into
        // m_aDisabledMarkers by the M-map's Update() would silently fail
        // the sanity check and the RPC would never fire. Toggling the
        // disabled flag back to false moves the marker into the static
        // list so the lookup hits.
        markerMgr.SetStaticMarkerDisabled(marker, false);

        // Use the canonical ID from the live marker — guards against any
        // drift between what we cached at refresh time and what the
        // manager currently has on the marker (e.g. server reassigning
        // the ID after broadcasting a placement).
        int liveId = marker.GetMarkerID();

        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
        {
            Print("[AG0_TDLMarkerToolPanel] OnMarkerEntryClicked: no SCR_PlayerController", LogLevel.WARNING);
            return;
        }

        SCR_MapMarkerSyncComponent syncComp = SCR_MapMarkerSyncComponent.Cast(pc.FindComponent(SCR_MapMarkerSyncComponent));
        if (!syncComp)
        {
            Print("[AG0_TDLMarkerToolPanel] OnMarkerEntryClicked: SCR_MapMarkerSyncComponent not found on PlayerController", LogLevel.WARNING);
            return;
        }

        syncComp.AskRemoveStaticMarker(liveId);
        Print(string.Format("[AG0_TDLMarkerToolPanel] OnMarkerEntryClicked: AskRemoveStaticMarker(%1) RPC sent (cached=%2)", liveId, cachedMarkerId), LogLevel.NORMAL);

        // Optimistic UI: pin both IDs in pending-deletes (in case they
        // differ) and yank the card right away. Also fire m_OnMarkerDeleted
        // so the menu can prune the 3D map widget via the display controller
        // — without that, the widget stays on the map until the manager's
        // RPC roundtrip lands and UpdateVanillaMarkers next ticks.
        if (m_aPendingDeletes.Find(liveId) == -1)
            m_aPendingDeletes.Insert(liveId);
        if (cachedMarkerId != liveId && m_aPendingDeletes.Find(cachedMarkerId) == -1)
            m_aPendingDeletes.Insert(cachedMarkerId);
        RemoveCardForMarker(cachedMarkerId);
        m_OnMarkerDeleted.Invoke(liveId);
    }

    //------------------------------------------------------------------------------------------------
    //! Walk both the static and the disabled marker lists, returning the
    //! first marker whose GetMarkerID matches. Used by OnMarkerEntryClicked
    //! when the cached marker reference has gone stale and we need to
    //! re-resolve from the manager's authoritative state.
    protected SCR_MapMarkerBase FindMarkerInBothLists(SCR_MapMarkerManagerComponent markerMgr, int markerId)
    {
        if (!markerMgr)
            return null;

        SCR_MapMarkerBase m = markerMgr.GetStaticMarkerByID(markerId);
        if (m)
            return m;

        array<SCR_MapMarkerBase> disabled = markerMgr.GetDisabledMarkers();
        if (disabled)
        {
            foreach (SCR_MapMarkerBase d : disabled)
            {
                if (d && d.GetMarkerID() == markerId)
                    return d;
            }
        }
        return null;
    }

    //------------------------------------------------------------------------------------------------
    //! Find the spawned entry card for a given marker ID (via the parallel
    //! handler array — each handler captured the ID at create time) and
    //! drop it from the widget hierarchy. Used for the optimistic UI path
    //! in OnMarkerEntryClicked: we yank the card immediately so the player
    //! gets feedback on click, then the next RefreshMarkerList rebuilds
    //! authoritatively from the manager.
    protected void RemoveCardForMarker(int markerId)
    {
        for (int i = 0; i < m_aMarkerEntryHandlers.Count(); i++)
        {
            AG0_TDLMarkerEntryHandler h = m_aMarkerEntryHandlers[i];
            if (!h || h.m_iMarkerId != markerId)
                continue;
            if (i < m_aMarkerEntryWidgets.Count() && m_aMarkerEntryWidgets[i])
                m_aMarkerEntryWidgets[i].RemoveFromHierarchy();
            return;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! MarkerToolBackButton in MarkerToolContent fires this. Two-state:
    //! during a shape draw with collected points the press cancels the
    //! draw and stays on the panel; otherwise it routes through
    //! m_OnCancelRequested so the menu can flip the active panel back to
    //! NETWORK_LIST. Lets the same button serve both "abort this draw"
    //! and "leave the marker tool" without a dedicated cancel binding.
    protected void OnBackButtonClicked()
    {
        if (m_ShapeDrawSession && m_ShapeDrawSession.IsArmed() && m_ShapeDrawSession.GetPointCount() > 0)
        {
            OnShapeCancelAction();
            return;
        }

        m_OnCancelRequested.Invoke();
    }

    //------------------------------------------------------------------------------------------------
    // SHAPE MODE
    //------------------------------------------------------------------------------------------------

    //! True when the shape sub-form is the active sub-mode. Used by the menu
    //! controller to decide whether a map click feeds the marker placement
    //! path or the shape draw session.
    bool IsShapeModeActive()
    {
        if (!m_TypeSpinBox)
            return false;
        return m_TypeSpinBox.GetCurrentIndex() == TYPE_INDEX_SHAPE;
    }

    //! True when the delete sub-form is the active sub-mode. Map clicks
    //! and TDLDraw-held cursor sweeps route through SweepDeleteAt to
    //! remove the player's own markers near the cursor.
    bool IsDeleteModeActive()
    {
        if (!m_TypeSpinBox)
            return false;
        return m_TypeSpinBox.GetCurrentIndex() == TYPE_INDEX_DELETE;
    }

    //! Sweep-delete the local player's own PLACED_CUSTOM / PLACED_MILITARY
    //! markers within DELETE_SWEEP_RADIUS_M of the supplied world position.
    //! Called per-frame (TDLDraw held) for sweep, or once per click-place
    //! event for single-click delete. Reuses the AskRemoveStaticMarker RPC
    //! path that the marker scroll list uses, and the same m_aPendingDeletes
    //! gate to suppress per-frame duplicate dispatch while a removal is
    //! in flight. Only the player's own markers are affected — teammates'
    //! markers stay put regardless of how wide the sweep goes.
    void SweepDeleteAt(vector worldPos)
    {
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
            return;

        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return;
        int selfPlayerId = pc.GetPlayerId();
        if (selfPlayerId <= 0)
            return;

        SCR_MapMarkerSyncComponent syncComp = SCR_MapMarkerSyncComponent.Cast(
            pc.FindComponent(SCR_MapMarkerSyncComponent));
        if (!syncComp)
            return;

        // Union both lists — same trick as RefreshMarkerList. Vanilla
        // SCR shuffles off-frame markers from m_aStaticMarkers into
        // m_aDisabledMarkers; without checking both the sweep would miss
        // markers placed outside the current M-map viewport.
        array<SCR_MapMarkerBase> allMarkers = markerMgr.GetStaticMarkers();
        array<SCR_MapMarkerBase> disabled = markerMgr.GetDisabledMarkers();
        if (disabled)
        {
            foreach (SCR_MapMarkerBase d : disabled)
            {
                if (d && allMarkers.Find(d) == -1)
                    allMarkers.Insert(d);
            }
        }

        float radiusSq = DELETE_SWEEP_RADIUS_M * DELETE_SWEEP_RADIUS_M;
        vector targetPos = Vector(worldPos[0], 0, worldPos[2]);

        foreach (SCR_MapMarkerBase marker : allMarkers)
        {
            if (!marker)
                continue;
            int t = marker.GetType();
            if (t != SCR_EMapMarkerType.PLACED_CUSTOM && t != SCR_EMapMarkerType.PLACED_MILITARY)
                continue;
            if (marker.GetMarkerOwnerID() != selfPlayerId)
                continue;

            int markerId = marker.GetMarkerID();
            if (m_aPendingDeletes.Find(markerId) != -1)
                continue;

            // World pos uses int coords on the marker (SetWorldPos took
            // ints during placement). GetWorldPos writes into an int[2]
            // out-array where index 0 = world X and index 1 = world Z
            // (no Y component — markers live on the map plane). Distance
            // squared keeps the per-marker cost to a couple multiplies
            // plus a compare.
            int mPos[2];
            marker.GetWorldPos(mPos);
            float dx = mPos[0] - targetPos[0];
            float dz = mPos[1] - targetPos[2];
            if (dx * dx + dz * dz > radiusSq)
                continue;

            // Same pre-RPC dance as OnMarkerEntryClicked: unhide from the
            // disabled list (so the server's GetStaticMarkerByID lookup
            // hits) before asking for removal.
            markerMgr.SetStaticMarkerDisabled(marker, false);

            syncComp.AskRemoveStaticMarker(markerId);
            m_aPendingDeletes.Insert(markerId);
            m_OnMarkerDeleted.Invoke(markerId);
        }

        SweepDeleteShapesAt(targetPos, radiusSq, selfPlayerId);
    }

    //------------------------------------------------------------------------------------------------
    //! Shape half of the sweep — hit-tests against the shape's centroid,
    //! every vertex, and (for radial shapes) the bounding radius so any
    //! visible part of the shape under the cursor triggers the delete.
    //! Dispatches AG0_PlayerController_TDL.AskDeleteShape; the server is
    //! the sole authority on ownership and silently no-ops requests for
    //! shapes the caller didn't create. No client-side identity gate —
    //! SCR_PlayerIdentityUtils.GetPlayerIdentityId is server-authoritative
    //! and returns empty on dedicated-MP clients, which would block every
    //! delete the user can legitimately make. m_aPendingShapeDeletes
    //! prevents the same id being re-asked while one round-trip is in
    //! flight, so the per-sweep grief-RPC ceiling is "one per shape under
    //! the cursor" — cheap even with full coverage.
    protected void SweepDeleteShapesAt(vector targetPos, float radiusSq, int selfPlayerId)
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return;

        AG0_TDLMapShapeManager shapeMgr = pc.GetTDLShapeManager();
        if (!shapeMgr)
            return;

        float sweepRadius = DELETE_SWEEP_RADIUS_M;

        array<ref AG0_TDLMapShape> shapes = shapeMgr.GetShapes();
        if (!shapes)
            return;

        foreach (AG0_TDLMapShape shape : shapes)
        {
            if (!shape || shape.m_sId.IsEmpty())
                continue;
            if (m_aPendingShapeDeletes.Find(shape.m_sId) != -1)
                continue;
            if (!IsCursorHittingShape(shape, targetPos, sweepRadius))
                continue;

            pc.AskDeleteShape(shape.m_sId);
            m_aPendingShapeDeletes.Insert(shape.m_sId);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Hit-test a shape against a cursor position with a forgiving radius.
    //! Order of checks is cheap-to-expensive: centroid first (always a
    //! single distance check), then bounding-radius for radial shapes
    //! (circle / sector / range_rings), then per-vertex (vertex-based
    //! shapes). Returns true on the first criterion that hits — sweep
    //! delete doesn't need a precise point-in-polygon, just "near enough
    //! to be the obvious target".
    protected bool IsCursorHittingShape(AG0_TDLMapShape shape, vector targetPos, float sweepRadius)
    {
        float dxC = shape.m_vCenter[0] - targetPos[0];
        float dzC = shape.m_vCenter[2] - targetPos[2];
        float centerDistSq = dxC * dxC + dzC * dzC;
        float sweepRadiusSq = sweepRadius * sweepRadius;
        if (centerDistSq <= sweepRadiusSq)
            return true;

        // Radial shapes: cursor inside the shape's enclosing disc (with
        // sweep tolerance) counts as a hit. Catches the case where the
        // user sweeps across the interior of a large circle whose
        // centroid is far from the cursor.
        if (shape.m_fRadius > 0)
        {
            float reach = shape.m_fRadius + sweepRadius;
            if (centerDistSq <= reach * reach)
                return true;
        }

        // Vertex-based shapes: any vertex within sweep radius hits.
        // Stored as flat [x0,z0, x1,z1, ...].
        if (shape.m_aVertices && !shape.m_aVertices.IsEmpty())
        {
            int n = shape.m_aVertices.Count();
            for (int i = 0; i + 1 < n; i += 2)
            {
                float dx = shape.m_aVertices[i] - targetPos[0];
                float dz = shape.m_aVertices[i + 1] - targetPos[2];
                if (dx * dx + dz * dz <= sweepRadiusSq)
                    return true;
            }
        }

        return false;
    }

    AG0_TDLShapeDrawSession GetShapeDrawSession()
    {
        return m_ShapeDrawSession;
    }

    //! Forward a world-space click into the active shape session. The menu
    //! controller calls this on map-click or TDLPlaceMarker action when shape
    //! mode is active; it has already resolved the world position from the
    //! map cursor (PC) or map view centre (gamepad / world-space frontend).
    //!
    //! Freehand short-circuit: that tool uses TDLDraw-held cursor sampling,
    //! not discrete clicks. When TDLDraw and the click action are co-bound
    //! to the same key, releasing the key fires both a TDLDraw release
    //! (which commits via TickFreehandDraw) AND a click-release (which
    //! would otherwise leak through to AddFreehandSample here, adding a
    //! stray final sample). Returning early in freehand mode keeps the
    //! click path inert and the commit path the single source of truth.
    void OnShapeClick(vector worldPos)
    {
        if (!m_ShapeDrawSession)
            return;
        if (m_ShapeDrawSession.GetActiveTool() == AG0_ETDLShapeTool.FREEHAND)
            return;
        if (!m_ShapeDrawSession.IsArmed())
            ReArmShapeSession();
        m_ShapeDrawSession.AddPoint(worldPos);
    }

    //! Variadic-shape commit action — closes polygon / range_rings / route.
    //! Fixed-point tools auto-commit through AddPoint and ignore this.
    void OnShapeCommitAction()
    {
        if (m_ShapeDrawSession)
            m_ShapeDrawSession.CommitVariadic();
    }

    //! Cancel the in-progress draw without committing. Caller (menu / world-
    //! space frontend) wires this to a cancel-shaped input action and to
    //! mode-leaving transitions where dropping geometry is appropriate.
    void OnShapeCancelAction()
    {
        if (m_ShapeDrawSession && m_ShapeDrawSession.IsArmed())
            m_ShapeDrawSession.Cancel();
        ReArmShapeSession();
    }

    //! Push the live cursor world position into the session for ghost rubber-
    //! banding. Menu controller resolves the cursor world from the canvas
    //! drag handler's poll, world-space frontend pushes from its own cursor
    //! plumbing; both feed the same entry point.
    void SetShapeCursorWorld(vector worldPos)
    {
        if (m_ShapeDrawSession)
            m_ShapeDrawSession.SetCursorWorld(worldPos);
    }

    //! Commit the freehand session on draw release. Controller calls this
    //! when TDLDraw transitions from held to released. Equivalent to the
    //! variadic commit action — produces a closed shape when enough samples
    //! have been collected, no-op otherwise (session stays armed, user can
    //! start another draw without re-selecting the tool).
    void OnShapeFreehandEnd()
    {
        if (m_ShapeDrawSession)
            m_ShapeDrawSession.CommitVariadic();
    }

    //------------------------------------------------------------------------------------------------
    //! Spawn the shape sub-form layout into MarkerToolShapeSection and look
    //! up the named widgets. {RESOURCE} marker on SHAPE_LAYOUT means the
    //! layout file isn't authored yet — the spawn will fail cleanly and the
    //! sub-form stays inert until the layout lands.
    protected void SpawnShapeEditBox()
    {
        if (!m_wShapeSection)
            return;

        m_wShapeEditBox = GetGame().GetWorkspace().CreateWidgets(SHAPE_LAYOUT, m_wShapeSection);
        if (!m_wShapeEditBox)
        {
            Print("[AG0_TDLMarkerToolPanel] ATAKMapShapeEditBox.layout not yet authored — shape sub-form inert", LogLevel.NORMAL);
            return;
        }

        m_ShapeToolSpinBox        = SCR_SpinBoxComponent.GetSpinBoxComponent("ShapeToolSpinBox",       m_wShapeEditBox);
        m_ShapeStrokeColorSpinBox = SCR_SpinBoxComponent.GetSpinBoxComponent("StrokeColorSpinBox",     m_wShapeEditBox);
        m_ShapeStrokeWidthSpinBox = SCR_SpinBoxComponent.GetSpinBoxComponent("StrokeWidthSpinBox",     m_wShapeEditBox);
        m_ShapeFillSpinBox        = SCR_SpinBoxComponent.GetSpinBoxComponent("FillToggle",             m_wShapeEditBox);

        Widget editRoot = m_wShapeEditBox.FindAnyWidget("EditBoxRoot");
        if (editRoot)
            m_ShapeLabelEditBox = SCR_EditBoxComponent.Cast(editRoot.FindHandler(SCR_EditBoxComponent));

        if (m_ShapeToolSpinBox)
            m_ShapeToolSpinBox.m_OnChanged.Insert(OnShapeToolChanged);
        if (m_ShapeStrokeColorSpinBox)
            m_ShapeStrokeColorSpinBox.m_OnChanged.Insert(OnShapeStyleChanged);
        if (m_ShapeStrokeWidthSpinBox)
            m_ShapeStrokeWidthSpinBox.m_OnChanged.Insert(OnShapeStyleChanged);
        if (m_ShapeFillSpinBox)
            m_ShapeFillSpinBox.m_OnChanged.Insert(OnShapeStyleChanged);

        PopulateShapeSelectors();
    }

    //------------------------------------------------------------------------------------------------
    //! Build the spinbox item lists. Tool list mirrors AG0_ETDLShapeTool
    //! values (FREEHAND deliberately absent per v1 scope). Colour list
    //! reuses the placed-marker config so the in-mod palette stays
    //! consistent and the user sees colours they're already familiar with.
    protected void PopulateShapeSelectors()
    {
        if (m_ShapeToolSpinBox)
        {
            m_ShapeToolSpinBox.ClearAll();
            m_ShapeToolSpinBox.AddItem("Circle");
            m_ShapeToolSpinBox.AddItem("Rectangle");
            m_ShapeToolSpinBox.AddItem("Polygon");
            m_ShapeToolSpinBox.AddItem("Sector");
            m_ShapeToolSpinBox.AddItem("Range Rings");
            m_ShapeToolSpinBox.AddItem("Route");
            m_ShapeToolSpinBox.AddItem("Freehand");
            m_ShapeToolSpinBox.SetCycleMode(true);
            m_ShapeToolSpinBox.SetCurrentItem(0);
        }

        m_aShapeStrokeColorValues.Clear();
        if (m_ShapeStrokeColorSpinBox)
        {
            m_ShapeStrokeColorSpinBox.ClearAll();
            if (m_PlacedConfig)
            {
                array<ref SCR_MarkerColorEntry> colorEntries = m_PlacedConfig.GetColorEntries();
                if (colorEntries)
                {
                    foreach (SCR_MarkerColorEntry entry : colorEntries)
                    {
                        if (!entry)
                            continue;
                        m_ShapeStrokeColorSpinBox.AddItem(entry.GetName());
                        m_aShapeStrokeColorValues.Insert(ColorToArgbInt(entry.GetColor()));
                    }
                }
            }
            // Fallback so the spinbox isn't empty when the config is missing.
            if (m_aShapeStrokeColorValues.IsEmpty())
            {
                m_ShapeStrokeColorSpinBox.AddItem("Red");
                m_aShapeStrokeColorValues.Insert(0xFFFF0000);
            }
            m_ShapeStrokeColorSpinBox.SetCycleMode(true);
            m_ShapeStrokeColorSpinBox.SetCurrentItem(0);
        }

        m_aShapeStrokeWidthValues.Clear();
        if (m_ShapeStrokeWidthSpinBox)
        {
            m_ShapeStrokeWidthSpinBox.ClearAll();
            m_ShapeStrokeWidthSpinBox.AddItem("1 px"); m_aShapeStrokeWidthValues.Insert(1);
            m_ShapeStrokeWidthSpinBox.AddItem("2 px"); m_aShapeStrokeWidthValues.Insert(2);
            m_ShapeStrokeWidthSpinBox.AddItem("3 px"); m_aShapeStrokeWidthValues.Insert(3);
            m_ShapeStrokeWidthSpinBox.AddItem("4 px"); m_aShapeStrokeWidthValues.Insert(4);
            m_ShapeStrokeWidthSpinBox.SetCycleMode(true);
            m_ShapeStrokeWidthSpinBox.SetCurrentItem(1);
        }

        if (m_ShapeFillSpinBox)
        {
            m_ShapeFillSpinBox.ClearAll();
            m_ShapeFillSpinBox.AddItem("No Fill");
            m_ShapeFillSpinBox.AddItem("Fill 40%");
            m_ShapeFillSpinBox.SetCycleMode(true);
            m_ShapeFillSpinBox.SetCurrentItem(0);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Re-arm the draw session with the current tool selection. Called on
    //! shape-mode entry and whenever the tool spinbox changes. Cancels any
    //! in-progress draw so switching tools mid-shape doesn't mix geometry.
    protected void ReArmShapeSession()
    {
        if (!m_ShapeDrawSession)
            return;

        PushShapeStyleToSession();
        AG0_ETDLShapeTool tool = ResolveCurrentShapeTool();
        m_ShapeDrawSession.Arm(tool);
    }

    protected void OnShapeToolChanged()
    {
        if (m_ShapeToolSpinBox)
            s_iLastShapeTool = m_ShapeToolSpinBox.GetCurrentIndex();
        ReArmShapeSession();
    }

    //! Pushes the current style state into the session so the next ghost
    //! rebuild reflects the user's pick. Stroke/fill changes during a draw
    //! update the live preview without resetting collected points.
    protected void OnShapeStyleChanged()
    {
        if (m_ShapeStrokeColorSpinBox)
            s_iLastShapeStrokeColor = m_ShapeStrokeColorSpinBox.GetCurrentIndex();
        if (m_ShapeStrokeWidthSpinBox)
            s_iLastShapeStrokeWidth = m_ShapeStrokeWidthSpinBox.GetCurrentIndex();
        if (m_ShapeFillSpinBox)
            s_iLastShapeFill = m_ShapeFillSpinBox.GetCurrentIndex();
        PushShapeStyleToSession();
    }

    protected void PushShapeStyleToSession()
    {
        if (!m_ShapeDrawSession)
            return;

        int strokeColor = ResolveCurrentStrokeColor();
        float strokeWidth = ResolveCurrentStrokeWidth();
        int fillColor = ResolveCurrentFillColor(strokeColor);
        string label = "";
        if (m_ShapeLabelEditBox)
            label = m_ShapeLabelEditBox.GetValue();

        m_ShapeDrawSession.SetStyle(strokeColor, strokeWidth, fillColor, label);
    }

    protected AG0_ETDLShapeTool ResolveCurrentShapeTool()
    {
        // Spinbox is 0-indexed (Circle=0), AG0_ETDLShapeTool reserves 0 for
        // UNKNOWN — shift by one so spinbox 0 maps to CIRCLE=1.
        if (!m_ShapeToolSpinBox)
            return AG0_ETDLShapeTool.UNKNOWN;
        int idx = m_ShapeToolSpinBox.GetCurrentIndex();
        if (idx < 0)
            return AG0_ETDLShapeTool.UNKNOWN;
        int shifted = idx + 1;
        if (shifted == AG0_ETDLShapeTool.CIRCLE)        return AG0_ETDLShapeTool.CIRCLE;
        if (shifted == AG0_ETDLShapeTool.RECTANGLE)     return AG0_ETDLShapeTool.RECTANGLE;
        if (shifted == AG0_ETDLShapeTool.POLYGON)       return AG0_ETDLShapeTool.POLYGON;
        if (shifted == AG0_ETDLShapeTool.SECTOR)        return AG0_ETDLShapeTool.SECTOR;
        if (shifted == AG0_ETDLShapeTool.RANGE_RINGS)   return AG0_ETDLShapeTool.RANGE_RINGS;
        if (shifted == AG0_ETDLShapeTool.ROUTE)         return AG0_ETDLShapeTool.ROUTE;
        if (shifted == AG0_ETDLShapeTool.FREEHAND)      return AG0_ETDLShapeTool.FREEHAND;
        return AG0_ETDLShapeTool.UNKNOWN;
    }

    protected int ResolveCurrentStrokeColor()
    {
        if (!m_ShapeStrokeColorSpinBox || m_aShapeStrokeColorValues.IsEmpty())
            return 0xFFFF0000;
        int idx = m_ShapeStrokeColorSpinBox.GetCurrentIndex();
        if (idx < 0 || idx >= m_aShapeStrokeColorValues.Count())
            return m_aShapeStrokeColorValues[0];
        return m_aShapeStrokeColorValues[idx];
    }

    protected float ResolveCurrentStrokeWidth()
    {
        if (!m_ShapeStrokeWidthSpinBox || m_aShapeStrokeWidthValues.IsEmpty())
            return 2.0;
        int idx = m_ShapeStrokeWidthSpinBox.GetCurrentIndex();
        if (idx < 0 || idx >= m_aShapeStrokeWidthValues.Count())
            return 2.0;
        return m_aShapeStrokeWidthValues[idx];
    }

    //! Build a fill ARGB int from the stroke colour and the fill spinbox.
    //! "No Fill" returns 0 (transparent — the renderer skips fill when
    //! m_iFillColor == 0). "Fill 40%" reuses the stroke hue with alpha 0x66
    //! so the fill reads as a tinted region without obscuring the underlying
    //! map detail.
    protected int ResolveCurrentFillColor(int strokeColor)
    {
        if (!m_ShapeFillSpinBox)
            return 0;
        int idx = m_ShapeFillSpinBox.GetCurrentIndex();
        if (idx <= 0)
            return 0;
        return (0x66 << 24) | (strokeColor & 0x00FFFFFF);
    }

    //! Convert an engine Color (0..1 floats) to the ARGB packed int the
    //! shape renderer expects. Matches the format used by AG0_TDLMapShape
    //! style fields and the API's strokeColor/fillColor.
    protected int ColorToArgbInt(Color c)
    {
        int a = Math.Clamp((int)(c.A() * 255), 0, 255);
        int r = Math.Clamp((int)(c.R() * 255), 0, 255);
        int g = Math.Clamp((int)(c.G() * 255), 0, 255);
        int b = Math.Clamp((int)(c.B() * 255), 0, 255);
        return (a << 24) | (r << 16) | (g << 8) | b;
    }

    //------------------------------------------------------------------------------------------------
    //! Forwarded from AG0_TDLShapeDrawSession.m_OnCommitted when a draw
    //! commits. Re-fires through the panel-level invoker so the menu
    //! controller can attach creator identity / network context before
    //! routing to the AskCreateShape RPC — the panel doesn't know about
    //! either of those.
    protected void OnShapeSessionCommitted(AG0_TDLMapShape draft, AG0_ETDLShapeTool tool)
    {
        if (!draft)
            return;

        m_OnShapeDrawCommitted.Invoke(draft, tool);

        // Auto re-arm with the same tool so the user can keep drawing without
        // round-tripping through the spinbox. Matches the marker tool's
        // "place, place again" flow.
        ReArmShapeSession();
    }
}

//------------------------------------------------------------------------------------------------
//! Tiny Managed wrapper so we can attach an int as item-data on a combo box.
//! SCR_ComboBoxComponent.AddItem(label, last, data) takes Managed; primitive
//! ints aren't Managed so we box them. Used by the Type 1 / Type 2 icon-flag
//! combos to associate each label with its bit value.
//------------------------------------------------------------------------------------------------
class AG0_TDLBoxedInt : Managed
{
    int m_iValue;
}

//------------------------------------------------------------------------------------------------
//! Per-entry click handler for the player-marker scroll list. Modelled on
//! AG0_TDLMemberCardHandler — extends ScriptedWidgetComponent so we can
//! attach it via Widget.AddHandler and have the engine invoke OnClick when
//! the player presses A / left-clicks the card. We picked this pattern
//! over SCR_ModularButtonComponent.m_OnClicked.Insert(handler.method)
//! because Enfusion's ScriptInvoker doesn't reliably bind a method to an
//! instance other than the caller's `this`, so the cross-instance hookup
//! silently no-op'd in testing.
//!
//! Lifetime is pinned by AG0_TDLMarkerToolPanel.m_aMarkerEntryHandlers; the
//! ref array is cleared (and these instances released) inside ClearMarkerList
//! before any new cards are spawned. The widget's own AddHandler keeps a
//! parallel reference, so the handler outlives any one Refresh cycle even
//! while it's still attached.
//------------------------------------------------------------------------------------------------
class AG0_TDLMarkerEntryHandler : ScriptedWidgetComponent
{
    //! Cached at create time as a hint for log/debug; not used as the
    //! authoritative ID for the delete RPC (the panel re-reads off the live
    //! marker, in case the server's assigned ID drifts from our local-side
    //! capture). Stored alongside the marker reference so we can fall back
    //! to the cached ID if the marker pointer goes stale.
    int m_iMarkerId;
    //! Live marker reference — kept so OnMarkerEntryClicked can call
    //! GetMarkerID() against the authoritative object instead of trusting
    //! the cached integer. The marker manager's m_aStaticMarkers /
    //! m_aDisabledMarkers hold strong refs to the same instance, so this
    //! pointer stays valid as long as the marker exists in either list.
    SCR_MapMarkerBase m_Marker;
    AG0_TDLMarkerToolPanel m_Panel;

    //! Engine-invoked when the user clicks the entry (A / left-mouse).
    //! Returns true to consume the event so it doesn't propagate.
    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (m_Panel)
            m_Panel.OnMarkerEntryClicked(m_Marker, m_iMarkerId);
        return true;
    }
}
