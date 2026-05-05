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

    // Type spinbox indices — keep in sync with the m_aElementNames in TDLMenuUI.layout
    protected static const int TYPE_INDEX_PLACED   = 0;
    protected static const int TYPE_INDEX_MILITARY = 1;

    //------------------------------------------------------------------------------------------------
    // ROOT WIDGETS
    //------------------------------------------------------------------------------------------------

    // The MarkerToolContent panel root and its children we look up in Init
    protected Widget m_wRoot;
    protected Widget m_wPlacedSection;
    protected Widget m_wMilitarySection;
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

    // Roots of the two spawned edit-box layouts (null until OnPanelShown)
    protected Widget m_wPlacedEditBox;
    protected Widget m_wMilitaryEditBox;

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

        // Repopulate the player-owned marker scroll every time we re-enter
        // the panel — markers may have been added/removed by other paths
        // (other players' deletions, API edits, etc.) since we last showed.
        RefreshMarkerList();
    }

    //------------------------------------------------------------------------------------------------
    //! Called when panel is hidden — v1 keeps state alive, no-op.
    void OnPanelHidden()
    {
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
    //! MarkerToolBackButton in MarkerToolContent fires this. Routes through
    //! m_OnCancelRequested so the menu side can flip the active panel back
    //! to NETWORK_LIST without the panel itself owning panel-state.
    protected void OnBackButtonClicked()
    {
        m_OnCancelRequested.Invoke();
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
