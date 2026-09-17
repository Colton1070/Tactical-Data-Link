//------------------------------------------------------------------------------------------------
//! Shared TDL menu controller.
//!
//! Owns the side-panel state machine (which content is active, who the
//! selected member is, which plugin owns the plugin slot) and the navigation
//! button handlers (Network / Back / Settings / MarkerTool / Settings Back).
//! Both the full-screen menu (AG0_TDLMenuUI) and the world-space device
//! display (TDL_WorldSpaceDisplayComponent) instantiate one per frontend so
//! the same layout — TDLMenuUI.layout — drives the same panel logic on both.
//!
//! State persistence happens via class-level statics so the same panel /
//! selection / plugin restoration survives menu open/close.
//!
//! Frontend-specific side effects (gamepad focus, chat view repopulation,
//! marker tool sub-panel lifecycle, crosshair visibility) are NOT in the
//! controller — frontends subscribe to m_OnPanelChanged and handle their
//! own widgets in response to the state transition.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Which region of the frontend currently owns directional input.
//!
//! A gamepad has one stick to spare and four things that want it, so the map and the
//! control regions cannot both be live at once. The zone says which one is, and it is the
//! map by default because that is what a player opens the device to look at.
//!
//! Ordered so MAP is zero: the default-constructed value is the safe one.
//------------------------------------------------------------------------------------------------
enum ETDLFocusZone
{
    MAP,        // The map surface itself — sticks drive the view, not widget focus
    CONTROLS,   // Left stack: compass, track, 2D/3D, zoom
    TOOLBAR,    // Top strip: menu, network, camera, marker tool, bloodhound
    PANEL       // Right side drawer, whatever ETDLPanelContent is showing
}

//------------------------------------------------------------------------------------------------
//! A solved bloodhound measurement, handed to surfaces that render it without owning the
//! controller that computed it.
//!
//! Carries the finished endpoints and readout strings rather than the inputs: the cursor a
//! solution came from is per-frontend state (the world-space device pushes its own cursor
//! world position each tick, the menu falls back to its map centre), so a mirror could not
//! reproduce it. Handing over the result also keeps the terrain sample to one per solve and
//! leaves exactly one copy of the distance/bearing formatting.
//------------------------------------------------------------------------------------------------
class AG0_TDLBloodhoundSolution
{
    vector m_vCursor;
    vector m_vDevice;
    string m_sGrid;
    string m_sElev;
    string m_sDist;
    string m_sAz;
    float  m_fDistance;
    float  m_fBearingDeg;
}

//------------------------------------------------------------------------------------------------
class AG0_TDLMenuController
{
    // ============================================
    // PERSISTENT STATE — survives menu open/close
    // ============================================
    static protected ETDLPanelContent s_eLastPanel = ETDLPanelContent.NETWORK_LIST;
    static protected RplId s_LastSelectedDeviceId = RplId.Invalid();
    static protected RplId s_LastChatContactRplId = RplId.Invalid();
    static protected string s_sLastChatContactName;
    static protected string s_sLastPanelPluginID;

    //! Bloodhound (cursor info + range/bearing line) toolbar tool toggle.
    //! Static so the menu and the world-space variant agree on the same
    //! enabled state, and so the toggle survives menu open/close.
    static protected bool s_bBloodhoundEnabled = false;

    //! Screen-brightness slider value (0-100). 100 = fully transparent overlay
    //! (no dimming); 0 = max dim (capped at MAX_BRIGHTNESS_ALPHA below). Static
    //! so the menu and world-space share the same value and it survives menu
    //! open/close. Session-only — does not persist across game restarts
    //! ($profile is PC-only per the existing constraints note).
    static protected float s_fBrightness = 100.0;

    //! Hard cap on overlay alpha so a user dragging to 0 never blackens the
    //! screen enough to lose the slider itself. 
    protected const float MAX_BRIGHTNESS_ALPHA = 0.90;

    //! Fired whenever any instance's slider updates s_fBrightness. Both the
    //! menu and the world-space controller subscribe in Init so the off-
    //! screen instance re-paints its BrightnessImage (and resyncs its slider
    //! widget) when the on-screen one changes the value. Without this, the
    //! world-space dim stays stale after the menu was the editor.
    static ref ScriptInvoker s_OnBrightnessChanged = new ScriptInvoker();

    //! Re-entry guard for s_OnBrightnessChanged. The subscriber calls
    //! SCR_SliderComponent.SetValue to keep the local slider widget visually
    //! in sync — SetValue can re-fire m_OnChanged, which would loop us back
    //! through OnBrightnessSliderChanged → invoker → SetValue again. The flag
    //! short-circuits the second pass. Process-static because the invoker
    //! fires synchronously across all subscribed instances.
    static protected bool s_bSyncingBrightness = false;

    static ETDLPanelContent GetLastPanel() { return s_eLastPanel; }
    static void SetLastPanel(ETDLPanelContent p) { s_eLastPanel = p; }
    static RplId GetLastSelectedDeviceId() { return s_LastSelectedDeviceId; }
    static void SetLastSelectedDeviceId(RplId id) { s_LastSelectedDeviceId = id; }
    static RplId GetLastChatContactRplId() { return s_LastChatContactRplId; }
    static void SetLastChatContactRplId(RplId id) { s_LastChatContactRplId = id; }
    static string GetLastChatContactName() { return s_sLastChatContactName; }
    static void SetLastChatContactName(string n) { s_sLastChatContactName = n; }
    static string GetLastPanelPluginID() { return s_sLastPanelPluginID; }
    static void SetLastPanelPluginID(string id) { s_sLastPanelPluginID = id; }

    static bool GetBloodhoundEnabled() { return s_bBloodhoundEnabled; }
    static void SetBloodhoundEnabled(bool enabled) { s_bBloodhoundEnabled = enabled; }

    static float GetBrightness() { return s_fBrightness; }
    static void SetBrightness(float v) { s_fBrightness = v; }

    // ============================================
    // INSTANCE STATE
    // ============================================
    protected Widget m_wRoot;
    protected ETDLPanelContent m_eActivePanel = ETDLPanelContent.NETWORK_LIST;

    //! Kept as a kill switch rather than inlined: the exclusive context this arms consumes
    //! physical input, and the RB-held d-pad combos in TDLMenuContext share buttons with the
    //! zone entries. If that collision turns out to cost a player their movement controls,
    //! flipping this off is a one-line rollback that does not touch the input configs.
    static const bool ZONE_NAV_ENABLED = true;

    //! Exclusive, so while the map holds focus it consumes the d-pad before vanilla widget
    //! navigation sees it. That is the whole reason it exists: on the map those presses mean
    //! "go to a control region", and vanilla would otherwise spend them moving widget focus
    //! somewhere the player cannot see.
    //!
    //! Peer priority with TDLScreenContext and TDLFocusContext, deliberately — and that
    //! peerage is load-bearing. An exclusive context suppresses every lower-priority context
    //! wholesale, not just the inputs it binds, so the only safe place for one is level with
    //! the contexts it means to sit beside rather than above them. TDLFocusContext has run at
    //! 100 with the same Exclusive flag on the device for as long as focus mode has existed;
    //! this is that arrangement, not a new bet. The context that took the mouse down with it
    //! was a different one, raised to 600, where it outranked the menu itself. Those two
    //! already coexist at 100 with one of them Exclusive, which is the arrangement known to
    //! work here. Raising this above them would put every action they carry behind a
    //! question nobody has answered.
    protected static const string MAP_ZONE_CONTEXT = "TDLMapZoneContext";

    protected ETDLFocusZone m_eFocusZone = ETDLFocusZone.MAP;

    //! Last containment mode applied, so the per-frame check re-walks the zone subtrees only
    //! when the operator actually swaps between a pad and a mouse.
    protected bool m_bDirectionalNavApplied = true;
    protected bool m_bZoneListenersHooked;

    //! Set by the frontend, per frame, while its map surface is genuinely being driven.
    //! Holding a device is not enough: the world-space controller ticks every frame the
    //! device is held, including while the player is running or shooting, and an Exclusive
    //! context standing through gameplay would eat the d-pad out from under them.
    protected bool m_bZoneInputActive;

    //! The map's context menu. One per frontend, like the panel state beside it.
    //! Crosshair offset from canvas centre, in canvas pixels. The crosshair is the console
    //! cursor: a mouse user aims a pointer, a pad user aims the map under a fixed reticle
    //! and then nudges the reticle itself. Held as an offset rather than an absolute so it
    //! survives a canvas resize without landing off-screen.
    protected float m_fCrosshairOffsetX;
    protected float m_fCrosshairOffsetY;
    protected bool m_bCrosshairActive;

    protected ref AG0_TDLRadialMenu m_RadialMenu;

    //! Where the hosting frontend's own pointer is, in its root's screen pixels. Only the
    //! world-space device sets this — it draws and drives a cursor the controller knows
    //! nothing about, and the wheel has to open under that cursor rather than under the
    //! crosshair the fullscreen menu uses.
    protected float m_fDevicePointerX;
    protected float m_fDevicePointerY;
    protected bool m_bHasDevicePointer;
    protected bool m_bRadialListenerHooked;
    protected Widget m_wZoneControls;
    protected Widget m_wZoneToolbar;
    protected Widget m_wZonePanel;
    protected AG0_ATAKPluginBase m_ActivePanelPlugin;
    protected AG0_TDLNetworkMember m_SelectedMember;
    protected RplId m_SelectedDeviceId;
    protected RplId m_ChatContactRplId;
    protected string m_sChatContactName;

    // ============================================
    // SHARED FRONTEND STATE
    //
    // State that previously lived on AG0_TDLMenuUI but is needed by both
    // frontends (fullscreen menu + world-space display) to drive plugins,
    // chat, callsign save, camera broadcast toggle, and marker placement.
    // Pushed in by the frontend during init; the controller owns the logic.
    // ============================================
    // Device refs — pushed by the frontend after its own device discovery.
    protected AG0_TDLDeviceComponent m_ActiveDevice;       //!< ATAK-capable device the frontend is driving (fullscreen) or that *is* the gadget (world-space)
    protected AG0_TDLDeviceComponent m_NetworkDevice;      //!< Device used for chat send + callsign save (must have NETWORK_ACCESS)

    // Display controller ref — needed for marker placement (mapView.GetCenter)
    // and marker delete (DropVanillaMarkerWidgetById). Frontend owns the
    // display controller; we hold a non-owning reference.
    protected AG0_TDLDisplayController m_DisplayController;

    // Plugin lifecycle — owned by the controller so both frontends share
    // the same plugin instances and toolbar buttons. Built from
    // m_ActiveDevice.GetAvailablePlugins() in RefreshPlugins().
    protected ref array<ref AG0_ATAKPluginBase> m_aActivePlugins = {};
    protected ref array<Widget> m_aPluginToolbarButtons = {};
    protected ref array<ref AG0_PluginButtonClickRelay> m_aPluginClickRelays = {};

    // Marker tool side-panel — owns the type/subtype/colour/text pickers
    // and the place button. Spawned into m_wMarkerToolContent at Init.
    protected ref AG0_TDLMarkerToolPanel m_MarkerToolPanel;

    // Chat message widget tracking — parallel arrays indexed in lockstep.
    // See AG0_TDLMenuUI declaration comments for the rendering protocol;
    // moved here verbatim so PopulateChatView/DriveImageCardRendering can
    // run from either frontend's tick.
    protected ref array<Widget>                   m_aChatMessageWidgets = {};
    protected ref array<ref AG0_TDLPhotoRenderer> m_aChatMessageRenderers = {};
    protected ref array<string>                   m_aChatMessageDeliveryIds = {};
    protected ref array<CanvasWidget>             m_aChatMessageImageCanvases = {};
    protected bool m_bScrollToBottom = false;
    protected float m_fImageRedrawAccum = 0;
    protected const float IMAGE_REDRAW_INTERVAL = 30.0;

    // ============================================
    // CACHED WIDGET REFS
    // ============================================
    // Panel structure
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wNetworkContent;
    protected Widget m_wDetailContent;
    protected Widget m_wSettingsContent;
    protected Widget m_wMarkerToolContent;
    protected Widget m_wChatContent;
    protected Widget m_wPluginToolPanel;

    // Detail content widgets
    protected TextWidget m_wDetailPlayerName;
    protected TextWidget m_wDetailSignalStrength;
    protected TextWidget m_wDetailNetworkIP;
    protected TextWidget m_wDetailGrid;
    protected TextWidget m_wDetailDistance;
    protected TextWidget m_wDetailCapabilities;
    protected Widget m_wViewFeedButton;

    // Navigation buttons (we own these handlers)
    protected Widget m_wNetworkButton;
    protected Widget m_wBackButton;
    protected Widget m_wSettingsButton;
    protected Widget m_wSettingsBackButton;
    protected Widget m_wMarkerToolButton;
    //! Null until the layout carries a DrawToolButton. Inert while null, so the drawing tool is
    //! still reachable from the radial in the meantime.
    protected Widget m_wDrawToolButton;

    // Optional ref the menu may inject so the controller can update its
    // last-message-preview text after chat send. Not required.
    protected Widget m_wChatContactName;

    // ============================================
    // SHARED WIDGET REFS — toolbar / chat / callsign / marker
    //
    // Cached at Init time from the same layout instance the controller is
    // driving. All FindAnyWidget calls are null-tolerant so the controller
    // compiles + runs even before the layout sections are authored.
    // ============================================
    // Toolbar
    protected Widget m_wToolbar;
    protected Widget m_wCameraButton;
    protected Widget m_wMenuButton;
    protected Widget m_wViewChatButton;
    //! Bloodhound tool — toggle button + the floating cursor-info readout
    //! widget (4 stacked TextWidgets that ride with the cursor). The readout
    //! itself is parented into the MarkerOverlay; the controller drives its
    //! position + text per frame from frontend tick when s_bBloodhoundEnabled.
    protected Widget m_wBloodhoundButton;
    //! 2D/3D map mode toggle. Lives on the controller rather than the menu because both
    //! frontends spawn this layout, and the world-space device should get the mode switch
    //! without a second copy of the wiring.
    protected Widget m_wMap3DButton;
    //! HUD map peripheral toggle, in the Settings panel. The button only writes the static on
    //! AG0_TDLMapPeripheral — the peripheral's own gating decides whether anything appears.
    protected Widget m_wPeripheralButton;

    //! Guarded for the same reason the radial and zone listeners are: the world-space device
    //! re-runs Init on every re-equip against a widget tree that outlives the controller, and
    //! a second subscription turns one press into two toggles.
    protected bool m_bMap3DButtonHooked;
    //! Same hazard, same guard — the peripheral button is a toggle too, so a doubled
    //! subscription reads as a dead button rather than a glitch.
    protected bool m_bPeripheralButtonHooked;
    protected Widget m_wBloodhoundReadout;
    protected TextWidget m_wBloodhoundGrid;
    protected TextWidget m_wBloodhoundElev;
    protected TextWidget m_wBloodhoundDist;
    protected TextWidget m_wBloodhoundAz;

    //! Anchored point readout — ATAK's top-right position block. Distinct from the bloodhound
    //! readout in every way that matters: it is chrome rather than a tool output, it sits in a
    //! fixed corner instead of following the cursor, and it never draws over the map.
    //! Null until the layout carries these names, and inert while null.
    protected Widget m_wPointReadout;
    protected TextWidget m_wPointReadoutLabel;
    protected TextWidget m_wPointReadoutGrid;
    protected TextWidget m_wPointReadoutElev;
    protected TextWidget m_wPointReadoutRange;

    // Chat content (m_wChatContent already declared above in the panel
    // structure block — controller drove ChatContent visibility well before
    // the chat-infra migration brought its sibling widgets along).
    protected ScrollLayoutWidget m_wChatScrollLayout;
    protected Widget m_wChatMessageList;
    protected Widget m_wChatEditBoxRoot;
    protected ref AG0_EditBoxComponent m_ChatEditBox;
    protected Widget m_wChatSendButton;

    // Callsign (Settings panel)
    protected Widget m_wCallsignEditBoxRoot;
    protected ref AG0_EditBoxComponent m_CallsignEditBox;
    protected Widget m_wCallsignSaveButton;

    // Brightness (Settings panel)
    // BrightnessSlider lives inside SettingsContent → BrightnessEditFrame and
    // is built from WLib_Slider.layout, so it carries a SCR_SliderComponent.
    // BrightnessImage is the rootFrame-level OverlayWidget child whose alpha
    // we drive to dim the screen. Both null-tolerant in older layouts.
    protected Widget m_wBrightnessSlider;
    protected ImageWidget m_wBrightnessImage;
    // No `ref` — Cast(widget.FindHandler) returns an existing component on
    // the widget (matches SCR_SpinBoxComponent pattern in AG0_TDLMarkerToolPanel).
    protected SCR_SliderComponent m_BrightnessSliderComp;
    //! Set by the frontend via SetApplyBrightness(true). Only the world-space
    //! frontend opts in — the fullscreen menu is already its own black canvas
    //! and dimming it would just hide its own controls.
    protected bool m_bApplyBrightness = false;

    // Marker tool
    protected Widget m_wMarkerToolBackButton;
    protected Widget m_wMarkerToolPlaceButton;
    // Shape-only confirm button — sibling of the place button in
    // MarkerToolContent. Visible only when the active shape-draw session
    // has a variadic tool armed and enough points to commit. Provides the
    // explicit "I'm done" gesture for range_rings / route (which have no
    // geometric close-the-loop) and as a backup commit for polygon.
    protected Widget m_wMarkerToolShapeConfirmButton;
    protected Widget m_wMarkerCrosshair;

    // Edge-detect TDLDraw transitions for freehand commit. True while the
    // user was actively drawing on the previous frame; a true→false flip
    // on this frame is the "I'm done drawing" gesture that commits the
    // shape via OnShapeFreehandEnd. Polled per-frame in TickFreehandDraw.
    protected bool m_bFreehandDrawHeldLastFrame;

    // World time of the most recent map-pan input deflection (gamepad
    // stick / keyboard arrows on the menu, etc.). Stays "active" for a
    // short window past the last deflection so brief drops through the
    // deadzone — stick wobble between motions, direction changes — don't
    // flip the cursor source back to the stale OS mouse position. The
    // glitch this prevents: freehand samples spike toward the OS mouse
    // position (typically top-left of screen for gamepad users) on any
    // frame the stick is momentarily at zero.
    protected float m_fLastMapPanInputTime;

    // ============================================
    // PLUGIN TOOLBAR BUTTON LAYOUT
    // ============================================
    protected const ResourceName PLUGIN_TOOLBAR_BUTTON_LAYOUT = "{7DEC0DEDA7AB1E01}UI/layouts/Menus/TDL/PluginToolbarButton.layout";
    protected const ResourceName MESSAGE_CARD_LAYOUT = "{2507AC45B21BBC57}UI/layouts/Menus/TDL/TDLMessageUI.layout";

    // ============================================
    // WEB MIRROR UPLINK
    //
    // Per-class live registry + last-sent snapshot so the inbound mirror command
    // dispatcher can reach any live controller (panel switches need an instance
    // because SetPanelContent fires widget side-effects), and so the change-detect
    // loop in Tick() can compare against the previous send. With multiple held
    // ATAK devices the world-space pathway can spawn more than one frontend, so
    // the registry can have any positive count — the dispatcher just needs the
    // first live entry.
    //
    // Element type is `ref` (strong reference). Without it, when a frontend's
    // strong reference drops (menu UI destroyed after OnMenuClose) the controller
    // is freed and the array entry becomes a dangling null. Cleanup() always
    // removes from the array first, so a strong ref here can't leak as long as
    // Cleanup runs — and dispatcher / uplink stay null-tolerant as a backstop.
    // ============================================
    static protected ref array<ref AG0_TDLMenuController> s_aLiveControllers = {};
    static protected ref AG0_TDLMirrorSnapshot s_LastSentMirrorSnapshot;
    static protected float s_fMirrorLastSendTimeMs;
    //! 10 Hz uplink cap. Map drag mutates pan/zoom every frame; without this the
    //! reliable RPC channel would saturate. Discrete events (panel switch, chat
    //! contact change) still send immediately because the change-detect path
    //! ignores the cap when nothing has been sent in this window.
    protected const float MIRROR_MIN_SEND_INTERVAL_MS = 100.0;

    static array<ref AG0_TDLMenuController> GetLiveControllers() { return s_aLiveControllers; }

    //! Clear the last-sent snapshot baseline so the next TickMirrorUplink
    //! unconditionally pushes a fresh snapshot. Called from the player
    //! controller's RpcDo_InvalidateMirrorSnapshotBaseline when the server
    //! has just registered a mirror subscribe and needs the client's current
    //! state immediately (no waiting for a state-change-triggered uplink).
    static void ResetMirrorUplinkBaseline()
    {
        s_LastSentMirrorSnapshot = null;
        s_fMirrorLastSendTimeMs = 0;
    }

    //! Drop every entry from the live-controller registry and reset the
    //! uplink baseline. Called from AG0_TDLSystem's destructor on world
    //! unload — the registry's strong refs would otherwise pin controllers
    //! from the previous world session alive across a restart, and the next
    //! world's controllers would stack on top instead of starting clean.
    //! Statics persist for the lifetime of the script process; only this
    //! kind of explicit hook can reset them between worlds.
    static void ClearLiveRegistryOnWorldUnload()
    {
        if (s_aLiveControllers)
            s_aLiveControllers.Clear();
        s_LastSentMirrorSnapshot = null;
        s_fMirrorLastSendTimeMs = 0;

        // The one path that drains the registry without going through Cleanup, so the
        // empty-registry clear there never fires. A solution carries world coordinates and an
        // MGRS string from the world being unloaded; a mirror only needs a device in the ruck
        // to draw, so it would render the old world's range and bearing in the new one until
        // some frontend happened to open.
        ClearBloodhoundSolution();
    }

    //! First non-null entry in the live registry. Used by the dispatcher to find
    //! any working frontend, and by the uplink gate to pick the primary. Null
    //! filtering is defensive against weak-ref drift even though strong refs
    //! above should prevent it.
    static AG0_TDLMenuController FindPrimaryLiveController()
    {
        foreach (AG0_TDLMenuController c : s_aLiveControllers)
        {
            if (c)
                return c;
        }
        return null;
    }

    // ============================================
    // EVENTS
    // ============================================
    //! Fired after SetPanelContent settles. Frontend handlers query
    //! GetActivePanel() (or other accessors) and do whatever frontend-specific
    //! work they need — chat view repopulation, marker tool sub-panel
    //! lifecycle, crosshair visibility, gamepad focus, etc.
    ref ScriptInvoker m_OnPanelChanged = new ScriptInvoker();

    //! Fired when ShowDetailView completes. Menu listens to refresh detail
    //! widgets the controller didn't populate (e.g. view-feed button visibility).
    ref ScriptInvoker m_OnDetailShown = new ScriptInvoker();

    //! Fired by member-card handlers on focus (D-pad / hover). Menu listens
    //! so it can track the focused-card index for gamepad restore. World-
    //! space doesn't subscribe — its cursor handles focus differently.
    //! Signature: (RplId memberId).
    ref ScriptInvoker m_OnMemberCardFocused = new ScriptInvoker();

    // ============================================
    // ACCESSORS
    // ============================================
    Widget GetRoot() { return m_wRoot; }
    ETDLPanelContent GetActivePanel() { return m_eActivePanel; }
    ETDLFocusZone GetFocusZone() { return m_eFocusZone; }
    AG0_ATAKPluginBase GetActivePanelPlugin() { return m_ActivePanelPlugin; }
    AG0_TDLNetworkMember GetSelectedMember() { return m_SelectedMember; }
    RplId GetSelectedDeviceId() { return m_SelectedDeviceId; }
    RplId GetChatContactRplId() { return m_ChatContactRplId; }
    string GetChatContactName() { return m_sChatContactName; }

    void SetSelectedMember(AG0_TDLNetworkMember m) { m_SelectedMember = m; }
    void SetSelectedDeviceId(RplId id) { m_SelectedDeviceId = id; }
    void SetChatContact(RplId id, string name)
    {
        m_ChatContactRplId = id;
        m_sChatContactName = name;
        // Eager persist — chat state is restored on next menu open even if
        // the menu didn't run a normal save path.
        s_LastChatContactRplId = id;
        s_sLastChatContactName = name;
    }

    // ============================================
    // SHARED FRONTEND STATE — accessors / setters
    // ============================================
    AG0_TDLDeviceComponent GetActiveDevice() { return m_ActiveDevice; }
    AG0_TDLDeviceComponent GetNetworkDevice() { return m_NetworkDevice; }
    AG0_TDLDisplayController GetDisplayController() { return m_DisplayController; }
    array<ref AG0_ATAKPluginBase> GetActivePlugins() { return m_aActivePlugins; }
    AG0_TDLMarkerToolPanel GetMarkerToolPanel() { return m_MarkerToolPanel; }
    //! Edit boxes exposed so the menu's SetPanelFocus can route gamepad focus
    //! to them when transitioning into DIRECT_CHAT / SETTINGS. World-space
    //! frontend doesn't use gamepad focus (relies on its own cursor) so it
    //! ignores these.
    AG0_EditBoxComponent GetChatEditBox() { return m_ChatEditBox; }
    AG0_EditBoxComponent GetCallsignEditBox() { return m_CallsignEditBox; }

    //! Pushed by the frontend after its own device-discovery pass. World-space
    //! pushes the gadget's own device; menu pushes whichever held device has
    //! ATAK_DEVICE capability (FindActiveDevice path).
    void SetActiveDevice(AG0_TDLDeviceComponent device) { m_ActiveDevice = device; }
    void SetNetworkDevice(AG0_TDLDeviceComponent device) { m_NetworkDevice = device; }

    //! Frontend's display controller — needed for marker placement
    //! (map view's GetCenter) and marker deletion (drop vanilla marker widget).
    void SetDisplayController(AG0_TDLDisplayController dc) { m_DisplayController = dc; }

    //! Frontend opt-in for the brightness overlay. The slider in the settings
    //! panel is bound in both frontends (so the value-display reads back
    //! correctly and edits persist into the static), but only frontends that
    //! call SetApplyBrightness(true) actually drive the BrightnessImage alpha
    //! on their own root. World-space sets this true after Init; the
    //! fullscreen menu leaves it false because dimming a full-screen UI just
    //! obscures its own controls.
    //!
    //! Safe to call before OR after Init: if called after, we apply the
    //! current persisted value immediately so the overlay catches up.
    void SetApplyBrightness(bool b)
    {
        m_bApplyBrightness = b;
        ApplyBrightnessToOverlay();
    }

    // ============================================
    // INIT / CLEANUP
    // ============================================
    bool Init(Widget root)
    {
        if (!root)
            return false;
        m_wRoot = root;

        // Panel structure
        m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
        m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
        m_wNetworkContent = m_wRoot.FindAnyWidget("NetworkContent");
        m_wDetailContent = m_wRoot.FindAnyWidget("DetailContent");
        m_wSettingsContent = m_wRoot.FindAnyWidget("SettingsContent");
        m_wMarkerToolContent = m_wRoot.FindAnyWidget("MarkerToolContent");
        m_wChatContent = m_wRoot.FindAnyWidget("ChatContent");
        m_wPluginToolPanel = m_wRoot.FindAnyWidget("PluginToolPanel");

        // Detail content widgets
        m_wDetailPlayerName = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailPlayerName"));
        m_wDetailSignalStrength = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailSignalStrength"));
        m_wDetailNetworkIP = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailNetworkIP"));
        m_wDetailGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailGrid"));
        m_wDetailDistance = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailDistance"));
        m_wDetailCapabilities = TextWidget.Cast(m_wRoot.FindAnyWidget("DetailCapabilities"));
        m_wViewFeedButton = m_wRoot.FindAnyWidget("ViewFeedButton");

        // Navigation buttons we own
        m_wNetworkButton = m_wRoot.FindAnyWidget("NetworkButton");
        m_wBackButton = m_wRoot.FindAnyWidget("BackButton");
        m_wSettingsButton = m_wRoot.FindAnyWidget("SettingsButton");
        m_wSettingsBackButton = m_wRoot.FindAnyWidget("SettingsBackButton");
        m_wMarkerToolButton = m_wRoot.FindAnyWidget("MarkerToolButton");
        m_wDrawToolButton = m_wRoot.FindAnyWidget("DrawToolButton");

        m_wChatContactName = m_wRoot.FindAnyWidget("ContactName");

        // (Marker tool panel built after widget refs are cached — see end of Init.)

        // Toolbar
        m_wToolbar = m_wRoot.FindAnyWidget("Toolbar");
        m_wCameraButton = m_wRoot.FindAnyWidget("CameraButton");
        m_wMenuButton = m_wRoot.FindAnyWidget("MenuButton");
        m_wViewChatButton = m_wRoot.FindAnyWidget("ViewChatButton");

        // Bloodhound — toolbar button + readout overlay. All FindAnyWidget
        // calls are null-tolerant so older layouts (without the new widgets)
        // still load cleanly.
        m_wBloodhoundButton  = m_wRoot.FindAnyWidget("BloodhoundButton");
        m_wMap3DButton       = m_wRoot.FindAnyWidget("Map3DButton");
        m_wPeripheralButton  = m_wRoot.FindAnyWidget("PeripheralButton");

        // A freshly built tree shows 2D until AG0_TDLMap3DView claims the pane. The
        // authored RenderTargetWidget clears to opaque black with no world bound, so a
        // pane left visible here covers the map canvas with a blank rectangle — which is
        // what a newly opened menu or a newly equipped EUD would otherwise render. The
        // layout ships it hidden; this re-asserts that for any tree built while the 3D
        // view is live on the OTHER surface, and CreatePane shows it again when this
        // surface wins arbitration.
        m_wZoneControls = m_wRoot.FindAnyWidget("ZoomControls");
        m_wZoneToolbar  = m_wRoot.FindAnyWidget("Toolbar");
        m_wZonePanel    = m_wRoot.FindAnyWidget("SidePanel");

        Widget map3DPane = m_wRoot.FindAnyWidget("RenderTarget0");
        if (map3DPane)
            map3DPane.SetVisible(false);
        m_wBloodhoundReadout = m_wRoot.FindAnyWidget("BloodhoundReadout");
        m_wBloodhoundGrid    = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundGrid"));
        m_wBloodhoundElev    = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundElev"));
        m_wBloodhoundDist    = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundDist"));
        m_wBloodhoundAz      = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundAz"));

        m_wPointReadout      = m_wRoot.FindAnyWidget("PointReadout");
        m_wPointReadoutLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("PointReadoutLabel"));
        m_wPointReadoutGrid  = TextWidget.Cast(m_wRoot.FindAnyWidget("PointReadoutGrid"));
        m_wPointReadoutElev  = TextWidget.Cast(m_wRoot.FindAnyWidget("PointReadoutElev"));
        m_wPointReadoutRange = TextWidget.Cast(m_wRoot.FindAnyWidget("PointReadoutRange"));

        // Hidden until the first tick fills it, so a freshly built tree doesn't flash an empty
        // box in the corner.
        if (m_wPointReadout)
            m_wPointReadout.SetVisible(false);

        // Tint readouts lime — the layout authors the widgets with the
        // engine's default text colour because layout-side "Color"/"Outline"
        // attributes on TextWidget don't reliably load through Enfusion's
        // binary parser (silent attribute drop on unknown keys breaks the
        // whole widget block). Setting the colour from script after Cast
        // gives us the same visual without depending on that parser path.
        Color lime = Color.FromRGBA(192, 255, 77, 255); // matches BLOODHOUND_COLOR on the map view
        if (m_wBloodhoundGrid) m_wBloodhoundGrid.SetColor(lime);
        if (m_wBloodhoundElev) m_wBloodhoundElev.SetColor(lime);
        if (m_wBloodhoundDist) m_wBloodhoundDist.SetColor(lime);
        if (m_wBloodhoundAz)   m_wBloodhoundAz.SetColor(lime);

        // Chat content — m_wChatContent already cached above in the panel
        // structure block; here we just grab its siblings.
        m_wChatMessageList = m_wRoot.FindAnyWidget("MessageList");
        m_wChatEditBoxRoot = m_wRoot.FindAnyWidget("MessageEditBox");
        if (m_wChatEditBoxRoot)
            m_ChatEditBox = AG0_EditBoxComponent.FindComponent(m_wChatEditBoxRoot);
        m_wChatSendButton = m_wRoot.FindAnyWidget("ChatSendButton");
        if (m_wChatContent)
            m_wChatScrollLayout = ScrollLayoutWidget.Cast(m_wChatContent.FindAnyWidget("ScrollLayout"));

        // Callsign (Settings)
        m_wCallsignEditBoxRoot = m_wRoot.FindAnyWidget("CallsignEditBox");
        if (m_wCallsignEditBoxRoot)
            m_CallsignEditBox = AG0_EditBoxComponent.FindComponent(m_wCallsignEditBoxRoot);
        m_wCallsignSaveButton = m_wRoot.FindAnyWidget("CallsignSaveButton");

        // Brightness slider (Settings) + brightness overlay image (rootFrame).
        // Both lookups null-tolerant; older layouts without these authored
        // simply leave the feature inert.
        m_wBrightnessSlider = m_wRoot.FindAnyWidget("BrightnessSlider");
        m_wBrightnessImage  = ImageWidget.Cast(m_wRoot.FindAnyWidget("BrightnessImage"));
        if (m_wBrightnessSlider)
        {
            m_BrightnessSliderComp = SCR_SliderComponent.Cast(
                m_wBrightnessSlider.FindHandler(SCR_SliderComponent));
            if (m_BrightnessSliderComp)
            {
                // Force the range explicitly so we don't depend on layout
                // defaults that might change as authoring evolves. Format
                // text is already set in the layout (#AR-ValueUnit_Percentage).
                m_BrightnessSliderComp.SetSliderSettings(0, 100, 1, "#AR-ValueUnit_Percentage");
                // Seed the slider widget from the persisted static so the
                // settings panel shows the current value on every open.
                m_BrightnessSliderComp.SetValue(s_fBrightness);
                m_BrightnessSliderComp.m_OnChanged.Insert(OnBrightnessSliderChanged);
            }
        }
        // Subscribe to the cross-instance invoker so a slider change in
        // *another* controller (e.g. the menu adjusting brightness while
        // the world-space frontend is also alive) propagates here. Paired
        // with the Remove() call in Cleanup so dead instances don't keep
        // firing their callback against null widget refs.
        s_OnBrightnessChanged.Insert(OnSharedBrightnessChanged);

        // Apply the persisted value to whichever overlay this frontend opted
        // into — no-op for the fullscreen menu (m_bApplyBrightness defaults
        // false), real work for world-space once SetApplyBrightness(true) runs.
        ApplyBrightnessToOverlay();

        // Marker tool
        m_wMarkerToolBackButton = m_wRoot.FindAnyWidget("MarkerToolBackButton");
        m_wMarkerToolPlaceButton = m_wRoot.FindAnyWidget("MarkerToolPlaceButton");
        m_wMarkerToolShapeConfirmButton = m_wRoot.FindAnyWidget("MarkerToolShapeConfirmButton");
        if (m_wMarkerToolShapeConfirmButton)
            m_wMarkerToolShapeConfirmButton.SetVisible(false);
        m_wMarkerCrosshair = m_wRoot.FindAnyWidget("MarkerCrosshair");

        HookButtonHandlers();

        // Marker tool panel — lazy sub-form layouts spawn on first
        // OnPanelShown, so this is cheap. Subscribes to its events so the
        // controller routes place/cancel/delete through itself.
        InitMarkerToolPanel();

        // Bloodhound visual on first open should reflect the sticky state.
        // The readout itself stays hidden until the first Tick computes
        // valid cursor + device positions and shows it.
        UpdateBloodhoundButtonVisual();
        UpdateMap3DButtonVisual();
        UpdatePeripheralButtonVisual();
        UpdateFocusZoneVisual();
        if (ZONE_NAV_ENABLED)
        {
            ApplyZoneFocusContainment();
            m_bDirectionalNavApplied = IsDirectionalNavActive();
        }

        HookZoneActionListeners();

        m_RadialMenu = new AG0_TDLRadialMenu(this);
        HookRadialActionListener();
        if (m_wBloodhoundReadout)
            m_wBloodhoundReadout.SetVisible(false);

        // Web mirror — register this controller so inbound mirror commands can
        // dispatch panel/chat changes through a live frontend. Deregistered in
        // Cleanup. Order does not matter for the dispatcher (first hit wins).
        if (s_aLiveControllers.Find(this) == -1)
            s_aLiveControllers.Insert(this);

        return true;
    }

    protected void HookButtonHandlers()
    {
        // Inlined per button — Enfusion can't take a method-pointer type as a
        // function parameter, so the helper had to go. The repetition is fine
        // for five buttons.
        if (m_wNetworkButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wNetworkButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnNetworkButtonClicked);
        }
        if (m_wBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnBackClicked);
        }
        if (m_wSettingsButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsClicked);
        }
        if (m_wSettingsBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wSettingsBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnSettingsBackClicked);
        }
        if (m_wPeripheralButton && !m_bPeripheralButtonHooked)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wPeripheralButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
            {
                comp.m_OnClicked.Insert(OnPeripheralButtonClicked);
                m_bPeripheralButtonHooked = true;
            }
        }
        if (m_wMarkerToolButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolButtonClicked);
        }
        if (m_wDrawToolButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wDrawToolButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnDrawToolButtonClicked);
        }
        if (m_wBloodhoundButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wBloodhoundButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnBloodhoundButtonClicked);
        }
        if (m_wMap3DButton && !m_bMap3DButtonHooked)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMap3DButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
            {
                comp.m_OnClicked.Insert(OnMap3DButtonClicked);
                m_bMap3DButtonHooked = true;
            }
        }

        // -------- Shared frontend buttons (chat / callsign / camera / marker tool) --------
        // Migrated from AG0_TDLMenuUI.HookButtonHandlers so both frontends
        // share the same button-handler set. World-space gets these for free.
        if (m_wChatSendButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wChatSendButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnChatSendClicked);
        }
        if (m_wViewChatButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wViewChatButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnViewDirectChatClicked);
        }
        if (m_wCallsignSaveButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCallsignSaveButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnCallsignSaveClicked);
        }
        if (m_wCameraButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wCameraButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnCameraButtonClicked);
        }
        if (m_wMarkerToolBackButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolBackButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolCancelRequested);
        }
        // Dedicated panel-level Place button — separate from the in-edit-box
        // ButtonPublic widgets so pressing A while navigating spinboxes /
        // combos doesn't accidentally place. Mouse click routes through the
        // place-requested path.
        if (m_wMarkerToolPlaceButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolPlaceButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolPlaceButtonClicked);
        }

        if (m_wMarkerToolShapeConfirmButton)
        {
            SCR_ModularButtonComponent confirmComp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolShapeConfirmButton.FindHandler(SCR_ModularButtonComponent));
            if (confirmComp)
                confirmComp.m_OnClicked.Insert(OnMarkerToolShapeConfirmButtonClicked);
        }
    }

    void Cleanup()
    {
        // Pair with the Init() registration so the mirror dispatcher never
        // tries to drive a torn-down controller. Run before plugin teardown
        // so any plugin tick reentering the dispatcher during Disable sees
        // an already-unregistered frontend.
        int idx = s_aLiveControllers.Find(this);
        if (idx != -1)
            s_aLiveControllers.Remove(idx);

        // With no frontend left, nothing recomputes the bloodhound — the last solution is a
        // snapshot of wherever the operator's cursor happened to be when they put the device
        // away. A mirror left drawing it would show a measurement no live surface agrees with,
        // so the publication dies with the last publisher. Re-equipping recomputes it, pin
        // included, on the next tick.
        if (s_aLiveControllers.IsEmpty())
            ClearBloodhoundSolution();

        // Tear down plugins first so OnPanelHidden + OnMenuClosed run while
        // the widget tree is still live. Idempotent if the frontend already
        // called DisablePlugins (e.g. menu's OnMenuClose path).
        DisablePlugins();

        // Drop the cross-instance brightness subscription so the dead
        // controller's callback doesn't run against a null widget tree on
        // the next sibling's slider change. Paired with the Insert() at
        // the end of Init.
        s_OnBrightnessChanged.Remove(OnSharedBrightnessChanged);

        // Don't touch statics — those are intentionally persistent. Just drop
        // instance refs so the controller can be safely re-created on next
        // open without lingering widget pointers.
        m_wRoot = null;
        m_ActivePanelPlugin = null;
        m_SelectedMember = null;
        m_ActiveDevice = null;
        m_NetworkDevice = null;
        m_DisplayController = null;
        m_wBrightnessSlider = null;
        m_wBrightnessImage = null;
        m_BrightnessSliderComp = null;
        UnhookRadialActionListener();
        if (m_RadialMenu)
            m_RadialMenu.Close();
        m_RadialMenu = null;

        UnhookZoneActionListeners();
        m_wZoneControls = null;
        m_wZoneToolbar = null;
        m_wZonePanel = null;
    }

    // ============================================
    // PANEL SWITCHING
    // ============================================
    void SetPanelContent(ETDLPanelContent content)
    {
        // Guard: PLUGIN_TOOL requires an active plugin. Fall back to
        // NETWORK_LIST to keep the menu usable if a stale state slips through
        // (e.g. s_eLastPanel restored across menu open with no plugin owner).
        if (content == ETDLPanelContent.PLUGIN_TOOL && !m_ActivePanelPlugin)
            content = ETDLPanelContent.NETWORK_LIST;

        bool wasPluginTool = (m_eActivePanel == ETDLPanelContent.PLUGIN_TOOL);
        bool willBePluginTool = (content == ETDLPanelContent.PLUGIN_TOOL);

        m_eActivePanel = content;

        // Eagerly persist the panel state to the cross-frontend statics so
        // that opening the menu while world-space is mid-action restores the
        // same panel (and vice versa). Used to be SaveState-on-close only,
        // which made the menu out of sync with world-space changes. Plugin ID
        // is captured here so PLUGIN_TOOL panels survive the swap too — if
        // a plugin is the new owner, its ID; otherwise empty.
        s_eLastPanel = content;
        if (willBePluginTool && m_ActivePanelPlugin)
            s_sLastPanelPluginID = m_ActivePanelPlugin.GetPluginID();
        else
            s_sLastPanelPluginID = "";

        bool showPanel = (content != ETDLPanelContent.NONE);
        bool showNetwork = (content == ETDLPanelContent.NETWORK_LIST);
        bool showDetail = (content == ETDLPanelContent.MEMBER_DETAIL);
        bool showChat = (content == ETDLPanelContent.DIRECT_CHAT);
        bool showSettings = (content == ETDLPanelContent.SETTINGS);
        // Both tool modes render into the same content frame — they are one panel wearing two
        // hats, differing in which categories its type spinbox offers.
        bool showMarkerTool = (content == ETDLPanelContent.MARKER_TOOL || content == ETDLPanelContent.DRAW_TOOL);
        bool showPluginTool = willBePluginTool;

        string title = "CONTACTS";
        switch (content)
        {
            case ETDLPanelContent.MEMBER_DETAIL:
                title = "CONTACT DETAILS";
                break;
            case ETDLPanelContent.SETTINGS:
                title = "SETTINGS";
                break;
            case ETDLPanelContent.DIRECT_CHAT:
                title = m_sChatContactName;
                break;
            case ETDLPanelContent.MARKER_TOOL:
                title = "MARKER TOOL";
                break;
            case ETDLPanelContent.DRAW_TOOL:
                title = "DRAW TOOL";
                break;
            case ETDLPanelContent.PLUGIN_TOOL:
                if (m_ActivePanelPlugin)
                    title = m_ActivePanelPlugin.GetDisplayName();
                break;
        }

        // Sync the world-space visibility statics so the device display tracks
        // the menu's current panel content.
        AG0_TDLDisplayController.SetPanelState(showPanel, showNetwork, showDetail, showSettings, showMarkerTool, title);

        if (m_wSidePanel)
            m_wSidePanel.SetVisible(showPanel);

        if (showPanel)
        {
            if (m_wNetworkContent)
                m_wNetworkContent.SetVisible(showNetwork);
            if (m_wDetailContent)
                m_wDetailContent.SetVisible(showDetail);
            if (m_wSettingsContent)
                m_wSettingsContent.SetVisible(showSettings);
            if (m_wMarkerToolContent)
                m_wMarkerToolContent.SetVisible(showMarkerTool);
            if (m_wPluginToolPanel)
                m_wPluginToolPanel.SetVisible(showPluginTool);
            if (m_wChatContent)
                m_wChatContent.SetVisible(showChat);

            if (m_wPanelTitle)
                m_wPanelTitle.SetText(title);

            if (showChat && m_wChatContactName)
            {
                TextWidget chatName = TextWidget.Cast(m_wChatContactName);
                if (chatName)
                    chatName.SetText(m_sChatContactName);
            }
        }

        // Plugin panel lifecycle. OnPanelHidden fires before clearing the
        // active-plugin reference, so the plugin can still call back into the
        // menu during teardown. OnPanelShown fires after the slot is made
        // visible so the plugin can measure / focus into a live widget.
        if (wasPluginTool && !willBePluginTool && m_ActivePanelPlugin)
        {
            AG0_ATAKPluginBase exiting = m_ActivePanelPlugin;
            m_ActivePanelPlugin = null;
            exiting.OnPanelHidden();
        }
        if (willBePluginTool && m_ActivePanelPlugin)
            m_ActivePanelPlugin.OnPanelShown(m_wPluginToolPanel);

        // Chat repopulate when transitioning into DIRECT_CHAT — controller
        // owns chat infra now, so both frontends get this for free.
        if (showChat)
            PopulateChatView();

        // Callsign edit box pre-populated from current device name when the
        // Settings panel opens. Controller owns m_NetworkDevice and
        // m_CallsignEditBox, so both frontends get this for free.
        if (showSettings && m_CallsignEditBox && m_NetworkDevice)
            m_CallsignEditBox.SetText(m_NetworkDevice.GetDisplayName());

        // Brightness slider re-seeded from the shared static whenever the
        // settings panel opens. The cross-instance invoker only pushes the
        // overlay alpha (it can't safely write back to the slider — see
        // OnSharedBrightnessChanged), so if the *other* frontend was the
        // most recent editor, our slider widget is stale until this point.
        // Guard around SetValue so OnBrightnessSliderChanged early-returns
        // and we don't re-broadcast the same value.
        if (showSettings && m_BrightnessSliderComp)
        {
            s_bSyncingBrightness = true;
            m_BrightnessSliderComp.SetValue(s_fBrightness);
            s_bSyncingBrightness = false;
        }

        // Marker tool sub-panel lifecycle: lazy sub-form spawn on first show,
        // teardown on hide. Both frontends share this.
        if (m_MarkerToolPanel)
        {
            if (showMarkerTool)
            {
                // Tell the panel which hat it is wearing BEFORE showing it, so the type spinbox
                // is already rebuilt for this mode when the sub-forms spawn against it.
                m_MarkerToolPanel.SetToolMode(content == ETDLPanelContent.DRAW_TOOL);
                m_MarkerToolPanel.OnPanelShown();
            }
            else
            {
                m_MarkerToolPanel.OnPanelHidden();
            }
        }

        // Panel content spawns bare-focusable, so containment has to be re-stated whenever
        // the panel is rebuilt or the region would leak focus again on the next d-pad press.
        if (ZONE_NAV_ENABLED)
            ApplyZoneFocusContainment();

        // Centre-screen crosshair tracking.
        UpdateMarkerCrosshairVisibility();

        // Notify the frontend so it can do its menu-specific reactions
        // (marker tool sub-panel, chat view repopulate, crosshair, gamepad
        // focus). Frontends that don't have those concerns (the world-space
        // device display) just ignore the event.
        m_OnPanelChanged.Invoke();
    }

    //------------------------------------------------------------------------------------------------
    void ToggleSidePanel()
    {
        if (m_eActivePanel == ETDLPanelContent.NONE)
            SetPanelContent(ETDLPanelContent.NETWORK_LIST);
        else
            SetPanelContent(ETDLPanelContent.NONE);
    }

    //------------------------------------------------------------------------------------------------
    void ShowDetailView(AG0_TDLNetworkMember member, RplId deviceId)
    {
        m_SelectedMember = member;
        m_SelectedDeviceId = deviceId;
        s_LastSelectedDeviceId = deviceId;

        PopulateDetailView();
        SetPanelContent(ETDLPanelContent.MEMBER_DETAIL);

        m_OnDetailShown.Invoke();
    }

    //------------------------------------------------------------------------------------------------
    //! Plugin entry point for claiming the side panel. Toggles like the
    //! navigation buttons: if the calling plugin is already the active panel
    //! owner, close back to the map (NONE); otherwise install this plugin as
    //! the owner and switch to PLUGIN_TOOL.
    void RequestPluginPanel(AG0_ATAKPluginBase plugin)
    {
        if (!plugin)
            return;

        bool alreadyActive = (m_eActivePanel == ETDLPanelContent.PLUGIN_TOOL && m_ActivePanelPlugin == plugin);
        if (alreadyActive)
        {
            SetPanelContent(ETDLPanelContent.NONE);
            return;
        }

        m_ActivePanelPlugin = plugin;
        SetPanelContent(ETDLPanelContent.PLUGIN_TOOL);
    }

    // ============================================
    // DETAIL POPULATION
    // ============================================
    void PopulateDetailView()
    {
        if (m_SelectedDeviceId != RplId.Invalid())
            m_SelectedMember = GetNetworkMemberById(m_SelectedDeviceId);

        if (!m_SelectedMember)
            return;

        if (m_wDetailPlayerName)
            m_wDetailPlayerName.SetText(m_SelectedMember.GetPlayerName());

        if (m_wDetailSignalStrength)
            m_wDetailSignalStrength.SetTextFormat("%1 dBm", m_SelectedMember.GetSignalStrength().ToString());

        if (m_wDetailNetworkIP)
            m_wDetailNetworkIP.SetText("192.168.0." + m_SelectedMember.GetNetworkIP().ToString());

        if (m_wDetailGrid)
        {
            vector memberPos = m_SelectedMember.GetPosition();
            m_wDetailGrid.SetText(AG0_MGRSGridUtils.GetFullMGRS(memberPos, 5));
        }

        if (m_wDetailDistance)
        {
            IEntity player = GetGame().GetPlayerController().GetControlledEntity();
            if (player)
            {
                // Same horizontal-plane solve and same formatter as the bloodhound, so the
                // detail card and the map readout never disagree about the same pair of points.
                vector memberWorld = m_SelectedMember.GetPosition();
                float dist    = HorizontalDistance(player.GetOrigin(), memberWorld);
                float bearing = MilitaryBearingDeg(player.GetOrigin(), memberWorld);
                m_wDetailDistance.SetTextFormat("%1 / BRG %2 deg", FormatDistanceValue(dist), FormatBearingValue(dist, bearing));
            }
        }

        if (m_wDetailCapabilities)
        {
            string caps = BuildCapabilitiesString(m_SelectedMember.GetCapabilities());
            m_wDetailCapabilities.SetText(caps);
        }

        if (m_wViewFeedButton)
        {
            RplId videoSourceId = m_SelectedMember.GetVideoSourceRplId();
            bool isBroadcasting = videoSourceId != RplId.Invalid();
            m_wViewFeedButton.SetVisible(isBroadcasting);
        }
    }

    protected string BuildCapabilitiesString(int caps)
    {
        string result = "";
        if ((caps & AG0_ETDLDeviceCapability.GPS_PROVIDER) != 0) result += "[GPS] ";
        if ((caps & AG0_ETDLDeviceCapability.VIDEO_SOURCE) != 0) result += "[CAM] ";
        if ((caps & AG0_ETDLDeviceCapability.DISPLAY_OUTPUT) != 0) result += "[DISP] ";
        if ((caps & AG0_ETDLDeviceCapability.INFORMATION) != 0) result += "[INFO] ";
        return result;
    }

    //------------------------------------------------------------------------------------------------
    // ============================================
    // MEMBER CARD HANDLERS
    //
    // Cards are spawned by AG0_TDLDisplayController on its membership-refresh
    // tick (~1Hz). Handlers (AG0_TDLMemberCardHandler) need to be attached
    // each time the card list rebuilds so click/focus events route through
    // this controller. Both frontends drive AttachCardHandlers via Tick.
    // ============================================

    //! Walks the display controller's current card list, attaches a handler
    //! to each card that doesn't already have one. Also refreshes notification
    //! badges and (for gamepad ergonomics) sets the first card's UP navigation
    //! target to the SettingsButton so D-pad back from the contact list lands
    //! somewhere sensible.
    void AttachCardHandlers()
    {
        if (!m_DisplayController)
            return;

        array<Widget> cards = m_DisplayController.GetMemberCards();
        array<RplId> cardIds = m_DisplayController.GetMemberCardIds();

        if (!cards || !cardIds)
            return;

        for (int i = 0; i < cards.Count(); i++)
        {
            Widget card = cards[i];
            if (!card || i >= cardIds.Count())
                continue;

            RplId memberId = cardIds[i];

            ButtonWidget button = ButtonWidget.Cast(card);
            if (!button)
                continue;

            // Skip if handler already exists (idempotent across Tick calls).
            AG0_TDLMemberCardHandler existingHandler = AG0_TDLMemberCardHandler.Cast(
                button.FindHandler(AG0_TDLMemberCardHandler));
            if (existingHandler)
                continue;

            AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);

            AG0_TDLMemberCardHandler handler = new AG0_TDLMemberCardHandler();
            handler.Init(this, memberId, member);
            button.AddHandler(handler);

            // First card: gamepad UP navigation lands on SettingsButton.
            if (i == 0)
                button.SetNavigation(WidgetNavigationDirection.UP, WidgetNavigationRuleType.EXPLICIT, "SettingsButton");
        }

        // Cards may have been created with stale notification state — refresh badges.
        UpdateMemberCardBadges();
    }

    //! Fired by AG0_TDLMemberCardHandler.OnClick. Drives the panel transition
    //! to MEMBER_DETAIL with the clicked contact as the selected member.
    void OnMemberCardClicked(RplId memberId, int button)
    {
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberId);
        if (!member)
            return;

        ShowDetailView(member, memberId);
    }

    //! Fired by AG0_TDLMemberCardHandler.OnFocus. Forwards to subscribers
    //! (menu uses this to remember the focused card index for gamepad
    //! initial-focus restore when the panel switches back to NETWORK_LIST).
    void OnMemberCardFocused(RplId memberId)
    {
        m_OnMemberCardFocused.Invoke(memberId);
    }

    //! Member lookup via the player controller's aggregated TDL membership.
    //! Same path AG0_TDLDisplayController uses internally — duplicated here so
    //! the controller can resolve members independently of either frontend.
    AG0_TDLNetworkMember GetNetworkMemberById(RplId rplId)
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return null;
        AG0_TDLNetworkMembers data = pc.GetAggregatedTDLMembers();
        if (!data)
            return null;
        return data.GetByRplId(rplId);
    }

    // ============================================
    // NAVIGATION BUTTON HANDLERS
    // ============================================
    protected void OnNetworkButtonClicked()
    {
        ToggleSidePanel();
    }

    protected void OnBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    protected void OnSettingsClicked()
    {
        SetPanelContent(ETDLPanelContent.SETTINGS);
    }

    protected void OnSettingsBackClicked()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    //! Live-fired by SCR_SliderComponent.m_OnChanged while the user drags the
    //! brightness slider. Parameterless handler — read the value off the
    //! cached component ref (matches the SpinBox pattern in
    //! AG0_TDLMarkerToolPanel).
    //!
    //! After updating the shared static and our local overlay, fire the
    //! cross-instance invoker so the *other* frontend (menu while world-
    //! space is held, or vice versa) re-paints its overlay and resyncs its
    //! slider widget. s_bSyncingBrightness short-circuits reentry that
    //! arises when the sibling subscriber writes back to its own slider.
    protected void OnBrightnessSliderChanged()
    {
        if (s_bSyncingBrightness)
            return;
        if (!m_BrightnessSliderComp)
            return;
        s_fBrightness = m_BrightnessSliderComp.GetValue();
        ApplyBrightnessToOverlay();
        s_OnBrightnessChanged.Invoke();
    }

    //! Invoker subscriber — runs on *every* live controller when any of them
    //! changes the brightness. Only repaints this frontend's overlay alpha.
    //!
    //! We intentionally do NOT touch the local slider widget here. This
    //! handler runs inside SCR_SliderComponent.m_OnChanged.Invoke (started
    //! by SetValue on the *source* slider), and calling SetValue on a
    //! sibling SCR_SliderComponent would recursively Invoke the slider's
    //! m_OnChanged — which the engine's ScriptInvoker rejects with
    //! "Recursive call of Invoke!". The slider widget on this instance
    //! gets re-seeded from s_fBrightness in the SetPanelContent path when
    //! the settings panel becomes visible, which is the only time the user
    //! can actually look at it.
    protected void OnSharedBrightnessChanged()
    {
        ApplyBrightnessToOverlay();
    }

    //------------------------------------------------------------------------------------------------
    //! World-space cursor-drag entry point. The world-space frontend's custom
    //! cursor doesn't emit drag events to the SliderWidget (the engine only
    //! handles slider drag from workspace-level mouse input), so the
    //! frontend calls this every frame the cursor is held over the
    //! brightness slider. We map the cursor's screen X onto the inner
    //! SliderWidget's range and drive SetValue, which fires m_OnChanged →
    //! OnBrightnessSliderChanged through the normal path.
    //!
    //! Uses the *inner* SliderWidget's screen bounds (not the wrapping
    //! BrightnessSlider ButtonWidget), because the button widget includes
    //! the label region on the left; cursor on the label would otherwise
    //! map to value=0 even though it's nowhere near the track.
    void DriveBrightnessSliderFromCursorX(float cursorX)
    {
        if (!m_BrightnessSliderComp || !m_wBrightnessSlider)
            return;
        Widget innerSlider = m_wBrightnessSlider.FindAnyWidget("Slider");
        if (!innerSlider)
            return;
        float sX, sY, sW, sH;
        innerSlider.GetScreenPos(sX, sY);
        innerSlider.GetScreenSize(sW, sH);
        if (sW <= 0)
            return;
        float t = (cursorX - sX) / sW;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float min = m_BrightnessSliderComp.GetMin();
        float max = m_BrightnessSliderComp.GetMax();
        float value = min + t * (max - min);
        // SetValue fires m_OnChanged → OnBrightnessSliderChanged, which is
        // where the static update + overlay repaint + cross-instance invoker
        // dispatch happens. No defensive duplicate call — verified via the
        // "Recursive call of Invoke!" crash that the engine DOES fire
        // m_OnChanged on SetValue, so a second pass here would just churn.
        m_BrightnessSliderComp.SetValue(value);
    }

    //! True when `w` is the brightness slider's outer ButtonWidget (the
    //! click target the world-space cursor's FindClickableRecursive returns).
    //! Used by the world-space input dispatcher to route cursor-drag to
    //! DriveBrightnessSliderFromCursorX instead of TriggerClick.
    bool IsBrightnessSlider(Widget w)
    {
        return w && w == m_wBrightnessSlider;
    }

    //! Push the current persisted brightness onto BrightnessImage's alpha.
    //! No-op unless this controller has opted in (m_bApplyBrightness) AND the
    //! image widget was found in the layout. Slider value 0 → MAX_ALPHA
    //! (heavy dim, capped so the slider stays clickable); slider value 100 →
    //! alpha 0 (fully transparent, no dimming). RGB stays black throughout.
    protected void ApplyBrightnessToOverlay()
    {
        if (!m_bApplyBrightness)
            return;
        if (!m_wBrightnessImage)
            return;
        float clamped = s_fBrightness;
        if (clamped < 0)   clamped = 0;
        if (clamped > 100) clamped = 100;
        float alpha = MAX_BRIGHTNESS_ALPHA * (100.0 - clamped) / 100.0;
        // Explicit cast — Color.FromRGBA wants ints, and Enfusion is strict
        // about implicit narrowing. Floor matches the SetColor float-encoded
        // semantics closely enough for an 8-bit alpha channel.
        int alphaByte = (int)(alpha * 255.0);
        m_wBrightnessImage.SetColor(Color.FromRGBA(0, 0, 0, alphaByte));
    }

    //! Toggles, the way the contacts button does. A button that only ever opens leaves the
    //! operator hunting for another panel to close this one with, and leaving the marker tool
    //! open is what strands a half-drawn shape on the map.
    //! True while either tool mode owns the panel. The marker tool and the drawing tool are one
    //! panel wearing two hats, so everything downstream of "is a placement tool open" — the
    //! crosshair, the place-action poll, the shape ghost, the delete sweep — has to accept both.
    //! Only the toolbar buttons care which of the two it is, because each toggles its own.
    bool IsToolPanelActive()
    {
        return m_eActivePanel == ETDLPanelContent.MARKER_TOOL
            || m_eActivePanel == ETDLPanelContent.DRAW_TOOL;
    }

    //------------------------------------------------------------------------------------------------
    protected void OnDrawToolButtonClicked()
    {
        if (m_eActivePanel == ETDLPanelContent.DRAW_TOOL)
        {
            SetPanelContent(ETDLPanelContent.NONE);
            return;
        }

        SetPanelContent(ETDLPanelContent.DRAW_TOOL);
    }

    //------------------------------------------------------------------------------------------------
    protected void OnMarkerToolButtonClicked()
    {
        if (m_eActivePanel == ETDLPanelContent.MARKER_TOOL)
        {
            SetPanelContent(ETDLPanelContent.NONE);
            return;
        }

        SetPanelContent(ETDLPanelContent.MARKER_TOOL);
    }

    //! Bloodhound toolbar toggle — flips the sticky static, repaints the
    //! button visual, and clears the line/readout on disable so the canvas
    //! doesn't show a stale stroke on the next Draw(). The per-frame tick
    //! (UpdateBloodhound) does the real work while enabled.
    protected void OnBloodhoundButtonClicked()
    {
        s_bBloodhoundEnabled = !s_bBloodhoundEnabled;
        UpdateBloodhoundButtonVisual();

        // Disable handling: clear UI ONLY if no pin is active. A pinned
        // bloodhound is treated like a saved range/bearing overlay — it
        // outlives the tool toggle so the operator can stash a measurement,
        // turn the tool off to do other things on the map, then either
        // click the pin to dismiss it or re-enable the tool to drop a new
        // one (which auto-clears the previous pin in OnMapClickedForBloodhound).
        if (!s_bBloodhoundEnabled && !s_bBloodhoundPinned)
        {
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            if (m_DisplayController)
            {
                AG0_TDLMapView mapView = m_DisplayController.GetMapView();
                if (mapView)
                    mapView.SetBloodhound(false, vector.Zero, vector.Zero);
            }
        }
    }

    //! Bloodhound click-to-pin / click-to-unpin. Routed from each frontend's
    //! click handler — the menu's drag handler m_OnClick (which fires only
    //! when there's no significant drag) and the world-space's release-without-
    //! drag path. Drag events never reach here, so panning the map keeps the
    //! readout following the cursor as before.
    //!
    //! Coordinate conversion mirrors OnMapClickedForMarkerPlacement: the
    //! click's absolute mouse/cursor pixels become MapCanvas-local, then
    //! ScreenToWorld lifts that into a world position the bloodhound can
    //! freeze on. No-op when the tool is disabled.
    void OnMapClickedForBloodhound(int absMouseX, int absMouseY)
    {
        if (IsRadialConsumingClicks())
            return;

        // Both pin AND unpin require the tool to be active — this handler
        // gets called on every map click (both frontends route every click
        // here so each tool can decide whether to act), and if unpin ran
        // without a tool gate then a click from a different tool (marker
        // placement, etc.) would silently clear a persisted pin. To dismiss
        // a pin while the tool is off, the operator re-enables the tool
        // and clicks once — the unpin branch below picks up the existing
        // pin and clears it.
        if (!s_bBloodhoundEnabled)
            return;

        // Yield to the marker tool side panel — its placement path is what
        // owns the click when the marker tool is open, and we don't want a
        // single click to both drop a marker AND set/clear a bloodhound pin.
        // The bloodhound readout itself keeps tracking (the tool's "active"
        // state isn't paused) — only the click→pin interaction is suppressed.
        if (IsToolPanelActive())
            return;
        if (!m_DisplayController || !m_wRoot)
            return;

        // Tool is active. Pin set → click unpins (fast path, no projection).
        if (s_bBloodhoundPinned)
        {
            s_bBloodhoundPinned = false;
            return;
        }

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;
        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasScreenX, canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);
        float localX = absMouseX - canvasScreenX;
        float localY = absMouseY - canvasScreenY;

        vector worldPos;
        mapView.ScreenToWorld(localX, localY, worldPos);
        s_vBloodhoundPinPos = worldPos;
        s_bBloodhoundPinned = true;
    }

    //------------------------------------------------------------------------------------------------
    //! Routed through a map view rather than AG0_TDLMap3DView directly: only the view knows
    //! which canvas host is live. This controller's OWN view is preferred over the active-view
    //! static, because with both frontends alive the static names whichever drew last, which
    //! is not necessarily the surface whose button was pressed.
    protected void OnMap3DButtonClicked()
    {
        AG0_TDLMapView mapView;
        if (m_DisplayController)
            mapView = m_DisplayController.GetMapView();

        if (mapView)
            mapView.ToggleMap3D();
        else
            AG0_TDLMapView.ToggleMap3DOnActiveView();

        UpdateMap3DButtonVisual();
    }

    //------------------------------------------------------------------------------------------------
    //! Amber while 3D is up, matching the bloodhound button's engaged tint — the toolbar's
    //! cyan hover colour cannot also mean "this mode is active" without reading as hover.
    void UpdateMap3DButtonVisual()
    {
        if (!m_wMap3DButton)
            return;
        ImageWidget icon = ImageWidget.Cast(m_wMap3DButton.FindAnyWidget("Map3DImage"));
        if (!icon)
            return;
        if (AG0_TDLMap3DView.IsViewOpen())
            icon.SetColor(Color.FromRGBA(255, 200, 50, 255));
        else
            icon.SetColor(Color.FromRGBA(191, 191, 191, 255));
    }

    AG0_TDLRadialMenu GetRadialMenu() { return m_RadialMenu; }

    //------------------------------------------------------------------------------------------------
    //! Move the crosshair with the right stick, and report whether it is being driven.
    //!
    //! This is what makes the right stick a cursor rather than a second pan axis. Left stick
    //! moves the map, right stick moves the reticle over it — the vanilla map's split, with
    //! the reticle standing in for the pointer a pad does not have.
    //!
    //! Clamped to the canvas so the reticle can never leave the surface it is aiming at, and
    //! it stays where it was left rather than springing back to centre, because an operator
    //! placing several markers along a road should not have to re-aim from the middle each
    //! time.
    //! Wheel and triggers zoom the 2D map.
    //!
    //! Called by the frontends rather than from Tick, and only while they are actually
    //! driving their surface — the world-space device ticks this controller whenever the
    //! player merely has a device in hand, and zooming a map nobody is looking at is not a
    //! feature. The 3D view polls the same two actions itself and SetZoom hands off to it
    //! while its pane is up, so this stands down then rather than stepping twice per notch.
    void DriveZoomFromInput(float tDelta, InputManager im)
    {
        if (!im || AG0_TDLMap3DView.IsViewOpen())
            return;

        if (IsRadialConsumingClicks() || !m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        // Both read as values and subtracted into a signed axis, so a device reporting both
        // at once cancels instead of fighting itself.
        float axis = im.GetActionValue(ZOOM_IN_ACTION) - im.GetActionValue(ZOOM_OUT_ACTION);

        if (Math.AbsFloat(axis) < ZOOM_DEADZONE)
        {
            m_bZoomLatched = false;
            m_fZoomHeldS = 0;
            return;
        }

        // A wheel notch should step once; a held trigger should keep going. An edge latch
        // plus a repeat delay serves both without having to know which device fired — the
        // same arrangement the 3D view settled on.
        bool step = !m_bZoomLatched;
        if (m_bZoomLatched)
        {
            m_fZoomHeldS = m_fZoomHeldS + tDelta;
            if (m_fZoomHeldS >= ZOOM_REPEAT_S)
            {
                m_fZoomHeldS = 0;
                step = true;
            }
        }

        m_bZoomLatched = true;
        if (!step)
            return;

        if (axis > 0)
            mapView.ZoomIn(ZOOM_STEP);
        else
            mapView.ZoomOut(ZOOM_STEP);
    }

    protected static const string ZOOM_IN_ACTION = "TDLMapZoomIn";
    protected static const string ZOOM_OUT_ACTION = "TDLMapZoomOut";

    //! Low because a wheel notch reports a small value even amplified by the action's
    //! Multiplier, where a trigger reports most of its 0..1 range. One threshold serves both
    //! only if it clears wheel noise without demanding a hard scroll.
    protected static const float ZOOM_DEADZONE = 0.02;
    protected static const float ZOOM_REPEAT_S = 0.12;

    //! Same notch the on-screen zoom buttons take, so the two routes agree.
    protected static const float ZOOM_STEP = 0.05;

    protected bool m_bZoomLatched;
    protected float m_fZoomHeldS;

    //------------------------------------------------------------------------------------------------
    void DriveCrosshairFromStick(float tDelta, InputManager im)
    {
        if (!im || !m_wRoot)
            return;

        // The wheel takes the right stick while it is up — it is picking a slice with it,
        // and a reticle sliding out from under the wheel would move the target the entries
        // were built for.
        if (m_RadialMenu && m_RadialMenu.IsOpen())
            return;

        float stickX = im.GetActionValue("TDLPanHorizontal");
        float stickY = im.GetActionValue("TDLPanVertical");

        // Confirm held plus the right stick is the pad's version of the mouse's click-drag,
        // which is the only gesture that orbits. Without this a controller cannot turn the 3D
        // view at all — OrbitInput was reachable from a mouse delta and nothing else.
        if (DriveOrbitFromStick(tDelta, im, stickX, stickY))
            return;

        if (Math.AbsFloat(stickX) <= CROSSHAIR_DEADZONE && Math.AbsFloat(stickY) <= CROSSHAIR_DEADZONE)
        {
            UpdateMarkerCrosshairVisibility();
            return;
        }

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasW;
        float canvasH;
        canvasWidget.GetScreenSize(canvasW, canvasH);
        if (canvasW <= 0 || canvasH <= 0)
            return;

        // Same quadratic response the map pan uses, so fine aiming is possible near centre
        // stick without making long sweeps feel sluggish.
        float curvedX = stickX * Math.AbsFloat(stickX);
        float curvedY = stickY * Math.AbsFloat(stickY);

        m_fCrosshairOffsetX = m_fCrosshairOffsetX + curvedX * CROSSHAIR_SPEED * tDelta;
        m_fCrosshairOffsetY = m_fCrosshairOffsetY - curvedY * CROSSHAIR_SPEED * tDelta;

        float halfW = canvasW * 0.5;
        float halfH = canvasH * 0.5;
        m_fCrosshairOffsetX = Math.Clamp(m_fCrosshairOffsetX, -halfW, halfW);
        m_fCrosshairOffsetY = Math.Clamp(m_fCrosshairOffsetY, -halfH, halfH);

        m_bCrosshairActive = true;

        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (m_wMarkerCrosshair && workspace)
            FrameSlot.SetPos(m_wMarkerCrosshair,
                workspace.DPIUnscale(m_fCrosshairOffsetX),
                workspace.DPIUnscale(m_fCrosshairOffsetY));

        UpdateMarkerCrosshairVisibility();
    }

    protected static const float CROSSHAIR_SPEED = 900.0;
    protected static const float CROSSHAIR_DEADZONE = 0.15;

    //! The marker currently being carried, if any. A ref rather than a plain handle: the
    //! commit removes it from the manager before adding it back, and without an owner of our
    //! own the instance is collected in between — leaving the re-add holding nothing.
    protected ref SCR_MapMarkerBase m_GrabbedMarker;
    protected bool m_bGrabLatched;

    //------------------------------------------------------------------------------------------------
    //! Pick up the marker under the pointer, carry it, and drop it where the button is
    //! released.
    //!
    //! The carry is genuinely local: every marker's widget is positioned from its own world
    //! position each frame, so moving the instance IS the ghost — nothing has to draw a
    //! preview, and nobody else sees anything until the drop. What the drop costs is a new
    //! marker id, because the vanilla sync component offers only add and remove; there is no
    //! move, so a move is one of each. See CommitMarkerMove.
    void DriveMarkerGrabFromInput(float tDelta, InputManager im)
    {
        if (!im || !m_MarkerToolPanel)
            return;

        bool held = im.GetActionValue("TDLMarkerGrab") > 0.5;

        if (held && !m_bGrabLatched)
        {
            m_bGrabLatched = true;
            m_GrabbedMarker = m_MarkerToolPanel.FindOwnedMarkerAt(GetCrosshairWorld());
            return;
        }

        if (held)
        {
            if (m_GrabbedMarker)
                m_MarkerToolPanel.SetMarkerWorldPosLocal(m_GrabbedMarker, GetCrosshairWorld());

            return;
        }

        m_bGrabLatched = false;

        if (!m_GrabbedMarker)
            return;

        m_MarkerToolPanel.CommitMarkerMove(m_GrabbedMarker);
        m_GrabbedMarker = null;
    }

    //! Screen pixels of equivalent drag per second at full stick, chosen so a full sweep
    //! turns the view about as far as a firm mouse drag across the pane does.
    protected static const float ORBIT_STICK_SPEED = 700.0;

    //------------------------------------------------------------------------------------------------
    //! Orbit the 3D view from the right stick while confirm is held, reporting whether it
    //! took the stick. Returns false in 2D and whenever confirm is up, which leaves the stick
    //! to the crosshair — the same split the mouse has, where a plain move aims and a drag
    //! turns the world.
    protected bool DriveOrbitFromStick(float tDelta, InputManager im, float stickX, float stickY)
    {
        if (!AG0_TDLMap3DView.IsViewOpen())
            return false;

        if (im.GetActionValue("TDLScreenClick") <= 0.5)
            return false;

        if (Math.AbsFloat(stickX) <= CROSSHAIR_DEADZONE && Math.AbsFloat(stickY) <= CROSSHAIR_DEADZONE)
            return true;

        if (!m_DisplayController)
            return true;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return true;

        // Pan is the orbit seam while the 3D pane is up — see AG0_TDLMapView.Pan.
        mapView.Pan(stickX * ORBIT_STICK_SPEED * tDelta, -stickY * ORBIT_STICK_SPEED * tDelta);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! World position under the crosshair — the console equivalent of "under the cursor".
    //! Falls back to the map centre when the reticle has never been moved, which is also
    //! where it is drawn, so the two answers agree.
    vector GetCrosshairWorld()
    {
        AG0_TDLMapView mapView;
        if (m_DisplayController)
            mapView = m_DisplayController.GetMapView();

        if (!mapView)
            return vector.Zero;

        float localX;
        float localY;
        if (m_bHasDevicePointer)
        {
            if (!ResolveCanvasLocal(m_fDevicePointerX, m_fDevicePointerY, localX, localY))
                return mapView.GetCenter();
        }
        else if (!GetCrosshairCanvasPos(localX, localY))
        {
            return mapView.GetCenter();
        }

        vector worldPos;
        mapView.ScreenToWorld(localX, localY, worldPos);
        return worldPos;
    }

    //------------------------------------------------------------------------------------------------
    //! World position under whatever is actually pointing right now — device cursor, mouse, or
    //! gamepad reticle, in that order.
    //!
    //! Kept separate from GetCrosshairWorld because that one feeds marker PLACEMENT, and
    //! placement must not follow the mouse: the panel's Place button is clicked with the pointer
    //! over the button, so a mouse-following placement would drop the marker under the side
    //! panel. A readout has no such hazard, and reporting the map centre while the operator
    //! moves the mouse across the map is useless.
    protected bool ResolvePointerWorld(AG0_TDLMapView mapView, out vector worldPos)
    {
        worldPos = vector.Zero;
        if (!mapView)
            return false;

        float localX;
        float localY;

        if (m_bHasDevicePointer)
        {
            if (!ResolveCanvasLocal(m_fDevicePointerX, m_fDevicePointerY, localX, localY))
                return false;

            mapView.ScreenToWorld(localX, localY, worldPos);
            return true;
        }

        if (ResolveMouseCanvasLocal(localX, localY))
        {
            mapView.ScreenToWorld(localX, localY, worldPos);
            return true;
        }

        if (GetCrosshairCanvasPos(localX, localY))
        {
            mapView.ScreenToWorld(localX, localY, worldPos);
            return true;
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Mouse position in MapCanvas-local pixels, rejected when the pointer is outside the canvas.
    //!
    //! The bounds test is the point of it: WidgetManager.GetMousePos is global, so without it a
    //! pointer resting over the side panel or the toolbar would still drive a map readout.
    protected bool ResolveMouseCanvasLocal(out float outX, out float outY)
    {
        outX = 0;
        outY = 0;

        if (!m_wRoot)
            return false;

        // On a pad the mouse is parked wherever it was last left, so it is not a pointer.
        InputManager im = GetGame().GetInputManager();
        if (im && !im.IsUsingMouseAndKeyboard())
            return false;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return false;

        int mouseX;
        int mouseY;
        WidgetManager.GetMousePos(mouseX, mouseY);

        float canvasScreenX;
        float canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);

        float canvasW;
        float canvasH;
        canvasWidget.GetScreenSize(canvasW, canvasH);
        if (canvasW <= 0 || canvasH <= 0)
            return false;

        outX = mouseX - canvasScreenX;
        outY = mouseY - canvasScreenY;

        return outX >= 0 && outY >= 0 && outX <= canvasW && outY <= canvasH;
    }

    //------------------------------------------------------------------------------------------------
    //! Screen pixels to MapCanvas-local pixels. The device reports its cursor in its root's
    //! space, which is the same conversion a mouse click already goes through.
    protected bool ResolveCanvasLocal(float screenX, float screenY, out float outX, out float outY)
    {
        outX = 0;
        outY = 0;

        if (!m_wRoot)
            return false;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return false;

        float canvasScreenX;
        float canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);

        outX = screenX - canvasScreenX;
        outY = screenY - canvasScreenY;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Where the crosshair sits in MapCanvas-local physical pixels. False when there is no
    //! canvas to measure against or the reticle has never been driven, which is the caller's
    //! cue to fall back to the map centre — the same point the reticle is drawn at.
    bool GetCrosshairCanvasPos(out float outX, out float outY)
    {
        outX = 0;
        outY = 0;

        if (!m_bCrosshairActive || !m_wRoot)
            return false;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return false;

        float canvasW;
        float canvasH;
        canvasWidget.GetScreenSize(canvasW, canvasH);
        if (canvasW <= 0 || canvasH <= 0)
            return false;

        outX = canvasW * 0.5 + m_fCrosshairOffsetX;
        outY = canvasH * 0.5 + m_fCrosshairOffsetY;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Centre of the map canvas in its own physical pixels — where the wheel opens when
    //! nothing more specific named the target.
    protected bool GetCanvasCentre(out float outX, out float outY)
    {
        outX = 0;
        outY = 0;

        if (!m_wRoot)
            return false;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return false;

        float canvasW;
        float canvasH;
        canvasWidget.GetScreenSize(canvasW, canvasH);
        if (canvasW <= 0 || canvasH <= 0)
            return false;

        outX = canvasW * 0.5;
        outY = canvasH * 0.5;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! True while the context menu owns the confirm button on this frontend. Map click
    //! handling asks this rather than IsOpen so the press that chose an entry cannot also
    //! land as a map click when it is released.
    bool IsRadialConsumingClicks()
    {
        return m_RadialMenu && m_RadialMenu.IsConsumingClicks();
    }

    //------------------------------------------------------------------------------------------------
    //! Give the context menu first refusal on a Back press. True means it took it, and the
    //! frontend's own Back chain must not also run — one tap dismisses one thing.
    bool CloseRadialOnBack()
    {
        return m_RadialMenu && m_RadialMenu.CloseOnBack();
    }

    //------------------------------------------------------------------------------------------------
    //! Recentre once, without arming tracking — the wheel's "Centre On Me" answers "where am
    //! I", which is a different question from "follow me", and that one has its own button.
    //! Declared by a frontend that draws its own cursor, every frame it drives one. Latched
    //! rather than consumed: the world-space device keeps drawing its cursor between ticks,
    //! and the fullscreen menu simply never calls this, so the flag reads "which surface am
    //! I hosted on" without either having to say so explicitly.
    void SetDevicePointer(float screenX, float screenY)
    {
        m_fDevicePointerX = screenX;
        m_fDevicePointerY = screenY;
        m_bHasDevicePointer = true;
    }

    //------------------------------------------------------------------------------------------------
    //! Drop a range/bearing pin on a picked point, so "measure to this" is one wheel entry
    //! instead of arm-then-click-the-right-pixel. The pin is what makes the reading survive
    //! panning away from the target.
    //!
    //! The tool is left DISARMED on purpose. A pin renders on its own — UpdateBloodhound's gate
    //! passes on either armed or pinned — while OnMapClickedForBloodhound ignores every click
    //! with the tool off. So the measurement stays exactly where the operator put it, and the
    //! next map click goes to whatever they actually meant it for instead of dragging the pin.
    //! Re-arming the tool is what makes the pin movable again.
    void PinRangeBearingAt(vector worldPos)
    {
        s_vBloodhoundPinPos = worldPos;
        s_bBloodhoundPinned = true;
        s_bBloodhoundEnabled = false;
        ClearBloodhoundSolution();
        UpdateBloodhoundButtonVisual();
    }

    //------------------------------------------------------------------------------------------------
    //! Open a contact's detail panel straight from the map rather than making the operator find
    //! the same person again in the contacts list.
    void ShowDetailForMember(RplId memberRplId)
    {
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberRplId);
        if (!member)
            return;

        ShowDetailView(member, memberRplId);
    }

    //------------------------------------------------------------------------------------------------
    void OpenChatForMember(RplId memberRplId)
    {
        AG0_TDLNetworkMember member = GetNetworkMemberById(memberRplId);
        if (!member)
            return;

        OpenDirectChat(memberRplId, member.GetPlayerName());
    }

    //------------------------------------------------------------------------------------------------
    void CentreMapOnPlayer()
    {
        if (!m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (mapView)
            mapView.CenterOnPlayer();
    }

    //------------------------------------------------------------------------------------------------
    //! True while any live frontend has its context menu up.
    //!
    //! Static because the consumers are pan and zoom on AG0_TDLMapView, which has no
    //! controller reference and should not grow one just to ask this. Walking the live
    //! controllers is the same route the bloodhound state sync already takes.
    static bool IsAnyRadialOpen()
    {
        if (!s_aLiveControllers)
            return false;

        foreach (AG0_TDLMenuController c : s_aLiveControllers)
        {
            if (c && c.m_RadialMenu && c.m_RadialMenu.IsOpen())
                return true;
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Guarded for the same reason the zone listeners are: the world-space device re-runs
    //! Init on every re-equip, and a second subscription would open and immediately close
    //! the radial on one press.
    protected void HookRadialActionListener()
    {
        if (m_bRadialListenerHooked)
            return;

        InputManager im = GetGame().GetInputManager();
        if (!im)
            return;

        im.AddActionListener("TDLRadial", EActionTrigger.DOWN, OnRadialAction);
        m_bRadialListenerHooked = true;
    }

    //------------------------------------------------------------------------------------------------
    protected void UnhookRadialActionListener()
    {
        if (!m_bRadialListenerHooked)
            return;

        InputManager im = GetGame().GetInputManager();
        if (im)
            im.RemoveActionListener("TDLRadial", EActionTrigger.DOWN, OnRadialAction);

        m_bRadialListenerHooked = false;
    }

    //------------------------------------------------------------------------------------------------
    //! The pad's road in. TDLRadial is bound to gamepad X alone — the mouse deliberately
    //! does not reach the wheel through an input action, because the drag handler is the
    //! only thing that can tell a right-click from a right-drag orbit, and two roads to the
    //! same wheel means one press opens and closes it in the same frame.
    protected void OnRadialAction()
    {
        ToggleRadial();
    }

    //------------------------------------------------------------------------------------------------
    //! Aims at the crosshair, which is the pad's cursor. Falls back to the middle of the
    //! canvas when the reticle has never been driven, which is where it is drawn anyway.
    void ToggleRadial()
    {
        if (!m_RadialMenu || !m_DisplayController)
            return;

        // A frontend with its own pointer has already told us where it is, and that is the
        // thing the operator is aiming with on that surface.
        if (m_bHasDevicePointer)
        {
            ToggleRadialAtScreen(m_fDevicePointerX, m_fDevicePointerY);
            return;
        }

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        float localX;
        float localY;
        if (!GetCrosshairCanvasPos(localX, localY) && !GetCanvasCentre(localX, localY))
            return;

        vector worldPos;
        mapView.ScreenToWorld(localX, localY, worldPos);
        m_RadialMenu.Toggle(worldPos, localX, localY);
    }

    //------------------------------------------------------------------------------------------------
    //! The mouse road in. Takes the click's absolute screen pixels because on a desk the
    //! pointer is the thing being aimed, not the crosshair — right-clicking a spot has to
    //! act on that spot and open the wheel over it.
    void ToggleRadialAtScreen(float absMouseX, float absMouseY)
    {
        if (!m_RadialMenu || !m_DisplayController || !m_wRoot)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasScreenX;
        float canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);

        float localX = absMouseX - canvasScreenX;
        float localY = absMouseY - canvasScreenY;

        vector worldPos;
        mapView.ScreenToWorld(localX, localY, worldPos);
        m_RadialMenu.Toggle(worldPos, localX, localY);
    }

    //------------------------------------------------------------------------------------------------
    //! Routed through the map view for the same reason the toolbar button is — only the view
    //! knows which canvas host is live.
    void ToggleMap3DFromRadial()
    {
        AG0_TDLMapView mapView;
        if (m_DisplayController)
            mapView = m_DisplayController.GetMapView();

        if (mapView)
            mapView.ToggleMap3D();
        else
            AG0_TDLMapView.ToggleMap3DOnActiveView();

        UpdateMap3DButtonVisual();
    }

    //------------------------------------------------------------------------------------------------
    //! Subscribe the d-pad zone-entry actions.
    //!
    //! Hooked once per frontend rather than per frame, and guarded because a world-space
    //! device re-runs Init on every re-equip against a widget tree that outlives the
    //! controller — double-subscribing would move two zones per press.
    protected void HookZoneActionListeners()
    {
        if (m_bZoneListenersHooked)
            return;

        InputManager im = GetGame().GetInputManager();
        if (!im)
            return;

        if (!ZONE_NAV_ENABLED)
            return;

        im.AddActionListener("TDLZoneControls", EActionTrigger.DOWN, OnZoneControls);
        im.AddActionListener("TDLZoneToolbar", EActionTrigger.DOWN, OnZoneToolbar);
        im.AddActionListener("TDLZonePanel", EActionTrigger.DOWN, OnZonePanel);
        m_bZoneListenersHooked = true;
    }

    //------------------------------------------------------------------------------------------------
    protected void UnhookZoneActionListeners()
    {
        if (!m_bZoneListenersHooked)
            return;

        InputManager im = GetGame().GetInputManager();
        if (im)
        {
            im.RemoveActionListener("TDLZoneControls", EActionTrigger.DOWN, OnZoneControls);
            im.RemoveActionListener("TDLZoneToolbar", EActionTrigger.DOWN, OnZoneToolbar);
            im.RemoveActionListener("TDLZonePanel", EActionTrigger.DOWN, OnZonePanel);
        }

        m_bZoneListenersHooked = false;
    }

    protected void OnZoneControls() { SetFocusZone(ETDLFocusZone.CONTROLS); }
    protected void OnZoneToolbar()  { SetFocusZone(ETDLFocusZone.TOOLBAR); }
    protected void OnZonePanel()    { SetFocusZone(ETDLFocusZone.PANEL); }

    //------------------------------------------------------------------------------------------------
    //! Hand directional input back to the map. Returns true when it actually moved, so a
    //! caller handling a Back press can consume it — otherwise leaving a control region and
    //! closing the panel behind it would both happen on one press.
    void SetZoneInputActive()
    {
        m_bZoneInputActive = true;
    }

    //------------------------------------------------------------------------------------------------
    //! Hand directional input back to the map. Returns true when it actually moved, so a
    //! caller handling a Back press can consume it.
    bool ReturnToMapZone()
    {
        if (m_eFocusZone == ETDLFocusZone.MAP)
            return false;

        SetFocusZone(ETDLFocusZone.MAP);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Move directional input to another region. No-op when it is already there, so callers
    //! can push the current zone every frame without churning the widget tree.
    void SetFocusZone(ETDLFocusZone zone)
    {
        if (m_eFocusZone == zone)
            return;

        m_eFocusZone = zone;
        UpdateFocusZoneVisual();
        ApplyZoneFocusContainment();
        FocusZoneEntryWidget();
    }

    //------------------------------------------------------------------------------------------------
    //! Make exactly one region reachable by directional input, and nothing else.
    //!
    //! This is what makes B the only way out. Placing focus on an entry widget is not enough
    //! on its own — vanilla's navigation solver looks at every focusable widget on the screen,
    //! so the next d-pad press simply walks out of the region and into whatever happens to lie
    //! that way. Taking NOFOCUS off the live region and putting it on the others leaves the
    //! solver nowhere to wander to, which turns "the region has focus" from a highlight into
    //! a fact.
    //!
    //! With the map zone live, every region is closed and focus is cleared outright. The map
    //! has no widgets to navigate, so anything focused there is somewhere the operator did not
    //! put it and cannot see.
    //!
    //! Gamepad only. NOFOCUS leaves a widget clickable, so the original read was that this
    //! costs a mouse player nothing — but it also stops a widget ever becoming the FOCUSED
    //! widget, and that is what an EditBoxWidget needs before it will accept a keystroke. With
    //! the map zone live by default and no d-pad to leave it with, a mouse player got a
    //! callsign box that highlights, clicks, and silently swallows every character typed into
    //! it. Nothing to contain without a directional solver driving, so nothing is contained.
    protected void ApplyZoneFocusContainment()
    {
        if (!IsDirectionalNavActive())
        {
            SetSubtreeFocusable(m_wZoneControls, true);
            SetSubtreeFocusable(m_wZoneToolbar,  true);
            SetSubtreeFocusable(m_wZonePanel,    true);
            return;
        }

        SetSubtreeFocusable(m_wZoneControls, m_eFocusZone == ETDLFocusZone.CONTROLS);
        SetSubtreeFocusable(m_wZoneToolbar,  m_eFocusZone == ETDLFocusZone.TOOLBAR);
        SetSubtreeFocusable(m_wZonePanel,    m_eFocusZone == ETDLFocusZone.PANEL);

        if (m_eFocusZone != ETDLFocusZone.MAP)
            return;

        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (workspace)
            workspace.SetFocusedWidget(null);
    }

    //------------------------------------------------------------------------------------------------
    //! Whether vanilla's directional navigation solver is the thing moving focus right now.
    //! Defaults to true when there is no input manager to ask, so a failure to read the device
    //! leaves the gamepad containment intact rather than silently unfencing it.
    protected bool IsDirectionalNavActive()
    {
        InputManager im = GetGame().GetInputManager();
        if (!im)
            return true;

        return !im.IsUsingMouseAndKeyboard();
    }

    //------------------------------------------------------------------------------------------------
    //! Open or close a whole subtree to directional navigation. Recursive because the flag is
    //! per widget rather than inherited, so a container alone would leave every button under
    //! it exactly as reachable as before.
    protected void SetSubtreeFocusable(Widget root, bool focusable)
    {
        if (!root)
            return;

        if (focusable)
            root.ClearFlags(WidgetFlags.NOFOCUS);
        else
            root.SetFlags(WidgetFlags.NOFOCUS);

        Widget child = root.GetChildren();
        while (child)
        {
            SetSubtreeFocusable(child, focusable);
            child = child.GetSibling();
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Put gamepad focus on a known control when a region takes over, so the first d-pad
    //! press inside it moves from somewhere the player can see. Without this the region
    //! lights up while focus is still wherever it was left, and the pad appears dead.
    //!
    //! One entry point per region rather than the last-focused control: returning to a
    //! predictable place is worth more than resuming, and it is what makes the region
    //! learnable. The side panel is the exception — its content changes, so the frontends
    //! own that through m_OnPanelChanged and this leaves their choice alone.
    protected void FocusZoneEntryWidget()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
            return;

        // The side panel's focusable content depends on which panel is showing, and the
        // frontends already resolve that on panel change. Re-firing that rather than naming
        // a widget here keeps one implementation of "where does focus go in the panel".
        if (m_eFocusZone == ETDLFocusZone.PANEL)
        {
            m_OnPanelChanged.Invoke();
            return;
        }

        Widget entry;
        if (m_eFocusZone == ETDLFocusZone.CONTROLS && m_wZoneControls)
            entry = m_wZoneControls.FindAnyWidget("CompassButton");
        else if (m_eFocusZone == ETDLFocusZone.TOOLBAR && m_wZoneToolbar)
            entry = m_wZoneToolbar.FindAnyWidget("MenuButton");

        if (entry)
            workspace.SetFocusedWidget(entry);
    }

    //------------------------------------------------------------------------------------------------
    //! Dim the regions that are not taking directional input.
    //!
    //! Whether the right stick is about to orbit the world or step through a list is the one
    //! thing a pad player must never have to guess, and it cannot be inferred from the map —
    //! the map looks identical either way. Opacity carries it rather than colour because
    //! SCR_ButtonEffectColor drives the colour of every button in these regions on hover and
    //! focus events, and would overwrite a tint on the next mouse move.
    //!
    //! Called on change rather than per frame for that same reason: nothing else writes
    //! opacity, so unlike the button tints beside it this does not need re-asserting.
    protected void UpdateFocusZoneVisual()
    {
        ApplyZoneOpacity(m_wZoneControls, m_eFocusZone == ETDLFocusZone.CONTROLS);
        ApplyZoneOpacity(m_wZoneToolbar,  m_eFocusZone == ETDLFocusZone.TOOLBAR);
        ApplyZoneOpacity(m_wZonePanel,    m_eFocusZone == ETDLFocusZone.PANEL);
    }

    //------------------------------------------------------------------------------------------------
    //! Unfocused regions stay legible rather than fading out — an operator still needs to read
    //! the member list while driving the map, so this marks which region is live without
    //! hiding the others.
    protected void ApplyZoneOpacity(Widget zone, bool focused)
    {
        if (!zone)
            return;

        float opacity = ZONE_OPACITY_UNFOCUSED;
        if (focused || m_eFocusZone == ETDLFocusZone.MAP)
            opacity = 1.0;

        zone.SetOpacity(opacity);
    }

    //! Deep enough to read at a glance on a device screen rendered into a world-space
    //! render target, where the whole panel is already small and often off-angle.
    protected static const float ZONE_OPACITY_UNFOCUSED = 0.55;

    //------------------------------------------------------------------------------------------------
    //! Settings-panel toggle for the HUD map peripheral. Same amber-engaged convention as the
    //! bloodhound button below so the two read as the same kind of control.
    protected void OnPeripheralButtonClicked()
    {
        AG0_TDLMapPeripheral.Toggle();
        UpdatePeripheralButtonVisual();
    }

    void UpdatePeripheralButtonVisual()
    {
        if (!m_wPeripheralButton)
            return;
        ImageWidget icon = ImageWidget.Cast(m_wPeripheralButton.FindAnyWidget("PeripheralImage"));
        if (!icon)
            return;
        if (AG0_TDLMapPeripheral.GetEnabled())
            icon.SetColor(Color.FromRGBA(255, 200, 50, 255));
        else
            icon.SetColor(Color.FromRGBA(191, 191, 191, 255));
    }

    //------------------------------------------------------------------------------------------------
    //! Tint the BloodhoundImage amber when active, default gray when inactive.
    //! Mirrors UpdateCameraButtonState's pattern — SCR_ModularButtonComponent
    //! has hover/focus colour effects but no built-in "toggle held" state, so
    //! we drive the image colour directly.
    //!
    //! Amber chosen for visual coherence with the line/readout (lime green)
    //! and with the broader ATAK convention of warm-tone "tool engaged" cues
    //! (vs cyan which doubles as the default hover tint on the rest of the
    //! toolbar — cyan-on-cyan made it ambiguous whether the button was
    //! activated or merely hovered). High-saturation amber reads at a glance.
    void UpdateBloodhoundButtonVisual()
    {
        if (!m_wBloodhoundButton)
            return;
        ImageWidget icon = ImageWidget.Cast(m_wBloodhoundButton.FindAnyWidget("BloodhoundImage"));
        if (!icon)
            return;
        if (s_bBloodhoundEnabled)
            icon.SetColor(Color.FromRGBA(255, 200, 50, 255));   // amber — tool engaged
        else
            icon.SetColor(Color.FromRGBA(191, 191, 191, 255));  // default-gray (0.75 0.75 0.75)
    }

    // ============================================
    // BLOODHOUND PER-FRAME UPDATE
    //
    // Driven from Tick(). When s_bBloodhoundEnabled is true the controller:
    //   1. Picks the cursor world position — frontend-supplied override on
    //      world-space (its cursor lives in ContentFrame screen coords) or
    //      the map view's centre on the full-screen menu (no cursor there).
    //   2. Reads the local player's position as the device endpoint.
    //   3. Samples terrain elevation at the cursor via the shared height source.
    //   4. Computes distance + bearing (clockwise from world-Z = north).
    //   5. Updates the four readout TextWidgets, positions the readout at
    //      the cursor in layout space, and pushes the line endpoints to the
    //      map view for DrawBloodhound() to consume.
    // ============================================

    //! World-space frontend pushes its cursor's world position here each tick, because that
    //! surface has a real cursor the controller cannot see. The menu doesn't push — with no
    //! override, UpdateBloodhound resolves the crosshair itself.
    protected vector m_vBloodhoundCursorOverride;
    protected bool   m_bBloodhoundCursorOverrideValid;

    //! Bloodhound pin state — when set, UpdateBloodhound sources the cursor
    //! world position from s_vBloodhoundPinPos instead of the per-frame
    //! override / crosshair. Toggled by OnMapClickedForBloodhound: first
    //! click pins at the click's world position, second click unpins.
    //!
    //! STATIC so that a pin set on the world-space display is visible on
    //! the full-screen menu (and vice versa) — both frontends each have
    //! their own AG0_TDLMenuController instance, so per-instance state
    //! desyncs the moment one frontend touches it. Mirrors how
    //! s_bBloodhoundEnabled (the tool toggle) is already shared.
    static protected bool   s_bBloodhoundPinned;
    static protected vector s_vBloodhoundPinPos;

    //! Last solution computed by whichever frontend is live, published for surfaces that render
    //! the map without owning a controller (the HUD peripheral).
    static protected ref AG0_TDLBloodhoundSolution s_BloodhoundSolution;

    //! Pin-fallback cache, kept separate from the live publication so clearing one doesn't
    //! invalidate the other. Re-solved when the player has moved a metre.
    static protected ref AG0_TDLBloodhoundSolution s_PinSolution;
    static protected vector s_vPinSolutionDevice;
    protected static const float PIN_RESOLVE_DISTANCE_M = 1.0;

    //! Below this separation a bearing is meaningless and Atan2 is undefined, so readouts
    //! show "---" instead.
    protected static const float BEARING_MIN_DISTANCE_M = 0.5;

    protected static void ClearBloodhoundSolution()
    {
        s_BloodhoundSolution = null;

        // The pin cache goes too: the events that clear the live publication are teardown and
        // world unload, and a cached solve from the old world would outlive its coordinates.
        s_PinSolution = null;
    }

    //! Entry point for mirror surfaces. Prefers the live solution; falls back to solving the pin
    //! itself when no frontend is computing one.
    //!
    //! The fallback is the whole reason a pin is worth having. A pin is a saved measurement, and
    //! the moment the operator closes the menu or stows the device the last controller tears
    //! down — which is exactly when the peripheral is the only surface still up. Without this it
    //! would drop the measurement at the instant it becomes the only thing showing it.
    static AG0_TDLBloodhoundSolution GetBloodhoundSolutionForMirror()
    {
        if (s_BloodhoundSolution)
            return s_BloodhoundSolution;

        if (!s_bBloodhoundPinned)
            return null;

        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return null;

        IEntity controlled = pc.GetControlledEntity();
        if (!controlled)
            return null;

        // Cached on the device position because a pin is stationary by definition, so the only
        // input that moves is the player. Without this the mirror re-runs a terrain sample, an
        // MGRS conversion and three string allocations every tick for a measurement that is
        // unchanged for as long as the operator stands still — and this fallback's whole reason
        // to exist is the stowed-device case, where it would run forever.
        vector devicePos = controlled.GetOrigin();
        if (s_PinSolution)
        {
            float moved = vector.Distance(devicePos, s_vPinSolutionDevice);
            if (moved < PIN_RESOLVE_DISTANCE_M)
                return s_PinSolution;
        }

        s_vPinSolutionDevice = devicePos;
        s_PinSolution = SolveBloodhound(s_vBloodhoundPinPos, devicePos);
        return s_PinSolution;
    }

    //------------------------------------------------------------------------------------------------
    //! Horizontal-plane separation. Every TDL range readout measures on the ground plane, so a
    //! contact on a hillside above you reads the same distance everywhere in the UI — a 3D
    //! straight-line answer alongside a plane answer looks like one of the two is broken.
    static float HorizontalDistance(vector fromPos, vector toPos)
    {
        float dx = toPos[0] - fromPos[0];
        float dz = toPos[2] - fromPos[2];
        return Math.Sqrt(dx * dx + dz * dz);
    }

    //------------------------------------------------------------------------------------------------
    //! Military bearing — clockwise from world +Z (north), 0..360. Math.Atan2(dx, dz) measures
    //! clockwise from +Z, which is already the convention we want.
    static float MilitaryBearingDeg(vector fromPos, vector toPos)
    {
        float dx = toPos[0] - fromPos[0];
        float dz = toPos[2] - fromPos[2];
        float bearingDeg = Math.Atan2(dx, dz) * Math.RAD2DEG;
        if (bearingDeg < 0)
            bearingDeg = bearingDeg + 360.0;

        return bearingDeg;
    }

    //------------------------------------------------------------------------------------------------
    //! Whole metres below 1 km, one decimal of km above. The km branch uses int math rather than
    //! a float divide plus ToString because Enfusion prints full precision ("1.20000"), which
    //! reads like measurement noise in a tactical readout. Rounding at 100 m granularity keeps
    //! the last digit meaningful — 1.2 km means 1200 m, not 1.2345 km.
    static string FormatDistanceValue(float distance)
    {
        if (distance < 1000.0)
            return string.Format("%1 m", Math.Round(distance).ToString());

        int deciKm = Math.Round(distance / 100.0);
        int whole  = deciKm / 10;
        int tenths = deciKm % 10;
        return string.Format("%1.%2 km", whole.ToString(), tenths.ToString());
    }

    //------------------------------------------------------------------------------------------------
    //! Bearing text, guarding the coincident-points case. Atan2(0,0) is implementation-defined,
    //! and the menu's default cursor sits exactly on the device whenever tracking is on, so the
    //! guard fires in ordinary use rather than only at an edge.
    static string FormatBearingValue(float distance, float bearingDeg)
    {
        if (distance < BEARING_MIN_DISTANCE_M)
            return "---";

        return Math.Round(bearingDeg).ToString();
    }

    //------------------------------------------------------------------------------------------------
    //! Pure solve — no instance state, so a surface with no controller can run it. Caller supplies
    //! the device position because the frontends resolve it differently (the live path falls back
    //! to the active device's owner when there's no controlled entity; a mirror has no such
    //! fallback and wouldn't be drawing without one anyway).
    static AG0_TDLBloodhoundSolution SolveBloodhound(vector cursorWorld, vector deviceWorld)
    {
        AG0_TDLBloodhoundSolution sol = new AG0_TDLBloodhoundSolution();

        // Snap cursor to terrain elevation so distance/azimuth are sampled against the terrain
        // surface (cursor came in at Y=0 from ScreenToWorld / GetCenter, both of which return
        // zero-Y points). Goes through the shared sampler rather than GetSurfaceY directly so
        // the readout agrees with the 3D mesh and inherits its off-terrain sentinel clamp —
        // a raw sample over water used to report the engine's ~-256 rather than 0.
        float cursorElev = AG0_TDLMap3DView.SampleTerrainY(cursorWorld[0], cursorWorld[2]);
        cursorWorld[1] = cursorElev;

        sol.m_vCursor = cursorWorld;
        sol.m_vDevice = deviceWorld;

        float distance   = HorizontalDistance(deviceWorld, cursorWorld);
        float bearingDeg = MilitaryBearingDeg(deviceWorld, cursorWorld);

        sol.m_fDistance = distance;
        sol.m_fBearingDeg = bearingDeg;

        sol.m_sGrid = AG0_MGRSGridUtils.GetFullMGRS(cursorWorld, 4);
        sol.m_sElev = string.Format("ELEV %1 m", Math.Round(cursorElev).ToString());
        sol.m_sDist = string.Format("DIST %1", FormatDistanceValue(distance));
        sol.m_sAz   = string.Format("BRG %1 deg", FormatBearingValue(distance, bearingDeg));

        return sol;
    }
    void SetBloodhoundCursorWorld(vector worldPos)
    {
        m_vBloodhoundCursorOverride = worldPos;
        m_bBloodhoundCursorOverrideValid = true;
    }
    void ClearBloodhoundCursorWorld()
    {
        m_bBloodhoundCursorOverrideValid = false;
    }

    //! Pick radius in screen pixels. Screen-space rather than world-space because a fixed metre
    //! radius is unhittable zoomed in and indiscriminate zoomed out — the 30 m the delete sweep
    //! uses covers most of a town at low zoom.
    protected static const float PICK_RADIUS_PX = 22.0;
    protected static const float PICK_RADIUS_MIN_M = 4.0;
    protected static const float PICK_RADIUS_MAX_M = 300.0;

    //------------------------------------------------------------------------------------------------
    //! What is under a world position: self, a contact, a marker, a shape, or nothing.
    //!
    //! The primitive the 2D map never had. Everything context-sensitive needs it — the radial
    //! choosing its ring, the position block naming what it describes, and later marker details.
    //! Resolution is most-specific-first (see AG0_ETDLPickKind), and within markers the nearest
    //! wins, so a cluster resolves to the one the operator was actually aiming at.
    //!
    //! Unowned markers resolve too. Delete stays owner-gated, but inspecting somebody else's
    //! marker is a core action and the old FindOwnedMarkerAt could not express it.
    AG0_TDLMapPickResult ResolveTargetAt(vector worldPos)
    {
        AG0_TDLMapPickResult result = new AG0_TDLMapPickResult();

        AG0_TDLMapView mapView;
        if (m_DisplayController)
            mapView = m_DisplayController.GetMapView();

        if (!mapView)
            return result;

        float radius = PICK_RADIUS_PX * mapView.GetWorldUnitsPerPixel();
        if (radius < PICK_RADIUS_MIN_M)
            radius = PICK_RADIUS_MIN_M;
        if (radius > PICK_RADIUS_MAX_M)
            radius = PICK_RADIUS_MAX_M;

        float radiusSq = radius * radius;
        float bestSq = radiusSq;

        // Self first — the operator's own position outranks anything drawn on top of it.
        vector selfWorld;
        if (ResolveSelfWorldPos(selfWorld))
        {
            float selfSq = FlatDistanceSq(selfWorld, worldPos);
            if (selfSq <= bestSq)
            {
                bestSq = selfSq;
                result.m_eKind = AG0_ETDLPickKind.SELF;
                result.m_sLabel = "SELF";
                result.m_vWorldPos = selfWorld;
            }
        }

        // Each stage returns the narrowed best rather than taking it by reference — `inout` on a
        // script-side method has no precedent in this codebase and is not worth a compile to test.
        bestSq = PickMemberAt(worldPos, bestSq, result);
        bestSq = PickMarkerAt(worldPos, bestSq, result);
        PickShapeAt(worldPos, radius, result);

        return result;
    }

    //------------------------------------------------------------------------------------------------
    //! Horizontal squared separation. Squared so the hot path never takes a square root, and
    //! horizontal for the same reason every other TDL range is: the map is a plan view.
    protected static float FlatDistanceSq(vector a, vector b)
    {
        float dx = a[0] - b[0];
        float dz = a[2] - b[2];
        return dx * dx + dz * dz;
    }

    //------------------------------------------------------------------------------------------------
    protected float PickMemberAt(vector worldPos, float bestSq, AG0_TDLMapPickResult result)
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return bestSq;

        AG0_TDLNetworkMembers data = pc.GetAggregatedTDLMembers();
        if (!data)
            return bestSq;

        int count = data.Count();
        for (int i = 0; i < count; i++)
        {
            AG0_TDLNetworkMember member = data.Get(i);
            if (!member)
                continue;

            vector memberWorld = member.GetPosition();
            float dSq = FlatDistanceSq(memberWorld, worldPos);
            if (dSq > bestSq)
                continue;

            bestSq = dSq;
            result.m_eKind = AG0_ETDLPickKind.MEMBER;
            result.m_sLabel = member.GetPlayerName();
            result.m_vWorldPos = memberWorld;
            result.m_MemberRplId = member.GetRplId();
        }

        return bestSq;
    }

    //------------------------------------------------------------------------------------------------
    protected float PickMarkerAt(vector worldPos, float bestSq, AG0_TDLMapPickResult result)
    {
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
            return bestSq;

        int selfPlayerId = -1;
        PlayerController pc = GetGame().GetPlayerController();
        if (pc)
            selfPlayerId = pc.GetPlayerId();

        array<SCR_MapMarkerBase> markers = CollectPickableMarkers(markerMgr);
        foreach (SCR_MapMarkerBase marker : markers)
        {
            if (!marker)
                continue;

            int type = marker.GetType();
            if (type != SCR_EMapMarkerType.PLACED_CUSTOM && type != SCR_EMapMarkerType.PLACED_MILITARY)
                continue;

            // GetWorldPos writes an int[2] out-array: index 0 is world X, index 1 is world Z.
            // Markers live on the map plane and carry no Y.
            int mPos[2];
            marker.GetWorldPos(mPos);
            vector markerWorld = Vector(mPos[0], 0, mPos[1]);

            float dSq = FlatDistanceSq(markerWorld, worldPos);
            if (dSq > bestSq)
                continue;

            bestSq = dSq;
            result.m_vWorldPos = markerWorld;
            result.m_Marker = marker;
            result.m_iMarkerId = marker.GetMarkerID();
            result.m_MemberRplId = RplId.Invalid();

            if (marker.GetMarkerOwnerID() == selfPlayerId)
                result.m_eKind = AG0_ETDLPickKind.OWN_MARKER;
            else
                result.m_eKind = AG0_ETDLPickKind.OTHER_MARKER;

            string customText = marker.GetCustomText();
            if (customText.IsEmpty())
                result.m_sLabel = string.Format("MARKER #%1", marker.GetMarkerID());
            else
                result.m_sLabel = customText;
        }

        return bestSq;
    }

    //------------------------------------------------------------------------------------------------
    //! Union of static and disabled lists, mirroring the marker tool's own collection — vanilla
    //! shuffles off-frame markers into m_aDisabledMarkers, so the static list alone goes blind
    //! to anything currently scrolled out of the M-map's view.
    protected array<SCR_MapMarkerBase> CollectPickableMarkers(SCR_MapMarkerManagerComponent markerMgr)
    {
        array<SCR_MapMarkerBase> all = {};
        if (!markerMgr)
            return all;

        array<SCR_MapMarkerBase> statics = markerMgr.GetStaticMarkers();
        if (statics)
        {
            foreach (SCR_MapMarkerBase s : statics)
            {
                if (s)
                    all.Insert(s);
            }
        }

        array<SCR_MapMarkerBase> disabled = markerMgr.GetDisabledMarkers();
        if (disabled)
        {
            foreach (SCR_MapMarkerBase d : disabled)
            {
                if (d && all.Find(d) == -1)
                    all.Insert(d);
            }
        }

        return all;
    }

    //------------------------------------------------------------------------------------------------
    //! Shapes resolve last and only onto bare ground. A shape is a large area, so letting it win
    //! on proximity would mask every marker drawn inside it.
    protected void PickShapeAt(vector worldPos, float radius, AG0_TDLMapPickResult result)
    {
        if (!result.IsEmpty())
            return;

        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return;

        AG0_TDLMapShapeManager shapeMgr = pc.GetTDLShapeManager();
        if (!shapeMgr)
            return;

        array<ref AG0_TDLMapShape> shapes = shapeMgr.GetShapes();
        if (!shapes)
            return;

        foreach (AG0_TDLMapShape shape : shapes)
        {
            if (!shape || shape.m_sId.IsEmpty())
                continue;

            if (!AG0_TDLMarkerToolPanel.IsShapeUnderCursor(shape, worldPos, radius))
                continue;

            result.m_eKind = AG0_ETDLPickKind.SHAPE;
            result.m_vWorldPos = shape.m_vCenter;
            result.m_Shape = shape;
            result.m_sShapeId = shape.m_sId;

            if (shape.m_sLabel.IsEmpty())
                result.m_sLabel = "SHAPE";
            else
                result.m_sLabel = shape.m_sLabel;

            return;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Fill ATAK's top-right position block: what the pointer is over, its grid, its elevation,
    //! and its range/bearing from self.
    //!
    //! Deliberately NOT part of UpdateBloodhound. That is a measurement tool the operator arms
    //! and disarms; this is chrome that reports where the pointer is. Sharing one code path is
    //! what led to the tool's readout being forced permanently on — the two have different
    //! lifetimes and belong apart even though they run the same solve.
    //!
    //! Real ATAK shows this block on selection and clears it on deselect. TDL has no click-to-
    //! select yet, so this tracks the pointer instead and names whatever it is over — which is
    //! the same information, arrived at without an extra click.
    void UpdatePointReadout()
    {
        if (!m_wPointReadout)
            return;

        if (!m_DisplayController)
        {
            m_wPointReadout.SetVisible(false);
            return;
        }

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView || !mapView.IsReady())
        {
            m_wPointReadout.SetVisible(false);
            return;
        }

        vector cursorWorld;
        if (!ResolvePointerWorld(mapView, cursorWorld))
        {
            m_wPointReadout.SetVisible(false);
            return;
        }

        // Range and bearing are measured from self, so with no self position there is nothing
        // to measure against and the block would be half-empty. Hide rather than show blanks.
        vector deviceWorld;
        if (!ResolveSelfWorldPos(deviceWorld))
        {
            m_wPointReadout.SetVisible(false);
            return;
        }

        // Snap to whatever the pointer is over, so the readout describes the contact rather than
        // the patch of ground next to it — this is what ATAK's block does on selection.
        AG0_TDLMapPickResult pick = ResolveTargetAt(cursorWorld);
        if (!pick.IsEmpty())
            cursorWorld = pick.m_vWorldPos;

        AG0_TDLBloodhoundSolution sol = SolveBloodhound(cursorWorld, deviceWorld);

        if (m_wPointReadoutLabel)
            m_wPointReadoutLabel.SetText(ResolvePointReadoutLabel(pick));
        if (m_wPointReadoutGrid)
            m_wPointReadoutGrid.SetText(sol.m_sGrid);
        if (m_wPointReadoutElev)
            m_wPointReadoutElev.SetText(sol.m_sElev);
        if (m_wPointReadoutRange)
        {
            m_wPointReadoutRange.SetTextFormat("BRG %1  DIST %2",
                FormatBearingValue(sol.m_fDistance, sol.m_fBearingDeg),
                FormatDistanceValue(sol.m_fDistance));
        }

        m_wPointReadout.SetVisible(true);
    }

    //------------------------------------------------------------------------------------------------
    //! What the readout is describing. Bare ground reports the pointer itself rather than going
    //! blank, because an empty first line reads as a broken widget.
    protected string ResolvePointReadoutLabel(AG0_TDLMapPickResult pick)
    {
        if (pick && !pick.IsEmpty() && !pick.m_sLabel.IsEmpty())
            return pick.m_sLabel;

        return "CURSOR";
    }

    //------------------------------------------------------------------------------------------------
    //! Local player position, preferring the controlled entity because that is how the rest of
    //! the TDL UI defines "self" (self marker, GPS panel). Falls back to the active device's
    //! owner when there is no controlled entity — loading, dead, in a vehicle transition.
    protected bool ResolveSelfWorldPos(out vector worldPos)
    {
        worldPos = vector.Zero;

        PlayerController pc = GetGame().GetPlayerController();
        if (pc)
        {
            IEntity controlled = pc.GetControlledEntity();
            if (controlled)
            {
                worldPos = controlled.GetOrigin();
                return true;
            }
        }

        if (m_ActiveDevice)
        {
            IEntity owner = m_ActiveDevice.GetOwner();
            if (owner)
            {
                worldPos = owner.GetOrigin();
                return true;
            }
        }

        return false;
    }

    void UpdateBloodhound()
    {
        // Disabled — clear MapView state and hide readout. Frontends call
        // ClearBloodhoundCursorWorld() in the same tick, but doing it here
        // is idempotent and protects against stale enable/disable races.
        //
        // A live pin keeps the readout/line drawing even after the tool
        // toggle is off, so the early-return only fires when both the tool
        // is disabled AND nothing is pinned. The cursor-priority logic
        // below picks s_vBloodhoundPinPos when s_bBloodhoundPinned, so the
        // pin-only render path is just "fall through with pin as cursor".
        if (!s_bBloodhoundEnabled && !s_bBloodhoundPinned)
        {
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            if (m_DisplayController)
            {
                AG0_TDLMapView mv = m_DisplayController.GetMapView();
                if (mv)
                    mv.SetBloodhound(false, vector.Zero, vector.Zero);
            }
            ClearBloodhoundSolution();
            return;
        }

        // Cleared on every bail: a frontend whose display controller failed to build goes
        // silent while still counting as a live controller, so the empty-registry clear in
        // Cleanup never fires and a mirror would render the last solution indefinitely.
        if (!m_DisplayController)
        {
            ClearBloodhoundSolution();
            return;
        }
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
        {
            ClearBloodhoundSolution();
            return;
        }

        // IsReady gates on a measured canvas, and this runs before Draw() refreshes that
        // measurement — so on the first tick after a build ScreenToWorld and WorldToLayout
        // both answer from a zero-width canvas and the readout flashes at the frame origin
        // for a frame. The mirror already guarded this; the menu can hit it too on the first
        // tick after arming.
        if (!mapView.IsReady())
        {
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            mapView.SetBloodhound(false, vector.Zero, vector.Zero);
            ClearBloodhoundSolution();
            return;
        }

        // Resolve cursor world position. Priority order:
        //   1. Pinned position (user clicked to lock it) — survives map pan
        //      and cursor movement until a second click unpins.
        //   2. Frontend-supplied live cursor override (world-space variant
        //      pushes its m_fCursorX/Y-derived world pos each tick).
        //   3. The crosshair, which resolves the device pointer when there is one and
        //      otherwise the reticle, falling back to map centre before it has been moved.
        //      Using it rather than the bare centre is what makes the readout track the
        //      right stick instead of reporting wherever the map happens to be scrolled.
        vector cursorWorld;
        if (s_bBloodhoundPinned)
            cursorWorld = s_vBloodhoundPinPos;
        else if (m_bBloodhoundCursorOverrideValid)
            cursorWorld = m_vBloodhoundCursorOverride;
        else
            cursorWorld = GetCrosshairWorld();

        vector deviceWorld;
        if (!ResolveSelfWorldPos(deviceWorld))
        {
            // Without a self position, distance/azimuth + the line all break
            // down. Bail without rendering rather than show garbage.
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            mapView.SetBloodhound(false, vector.Zero, vector.Zero);
            ClearBloodhoundSolution();
            return;
        }

        AG0_TDLBloodhoundSolution sol = SolveBloodhound(cursorWorld, deviceWorld);
        cursorWorld = sol.m_vCursor;

        if (m_wBloodhoundGrid)
            m_wBloodhoundGrid.SetText(sol.m_sGrid);
        if (m_wBloodhoundElev)
            m_wBloodhoundElev.SetText(sol.m_sElev);
        if (m_wBloodhoundDist)
            m_wBloodhoundDist.SetText(sol.m_sDist);
        if (m_wBloodhoundAz)
            m_wBloodhoundAz.SetText(sol.m_sAz);

        // Position the floating readout at the cursor in layout coordinates.
        // WorldToLayout already DPIUnscales the result for us.
        if (m_wBloodhoundReadout)
        {
            float layoutX, layoutY;
            mapView.WorldToLayout(cursorWorld, layoutX, layoutY);
            FrameSlot.SetPos(m_wBloodhoundReadout, layoutX, layoutY);
            m_wBloodhoundReadout.SetVisible(true);
        }

        // Hand the endpoints to the map view for line + tick rendering.
        mapView.SetBloodhound(true, sol.m_vCursor, sol.m_vDevice);

        // Publish for mirror surfaces. Endpoints are world-space, so a mirror at a different
        // centre, zoom and rotation projects them itself and lands on the same ground.
        s_BloodhoundSolution = sol;
    }

    // ============================================
    // STATE SAVE / RESTORE
    // ============================================
    //! Called by the menu just before close so the next open restores state.
    //! Captures the current plugin ID before transitioning out so it can be
    //! re-installed on next open if the plugin is still enabled.
    void SaveState()
    {
        s_LastSelectedDeviceId = m_SelectedDeviceId;
        s_LastChatContactRplId = m_ChatContactRplId;
        s_sLastChatContactName = m_sChatContactName;

        if (m_eActivePanel == ETDLPanelContent.PLUGIN_TOOL)
        {
            string pluginID = "";
            if (m_ActivePanelPlugin)
                pluginID = m_ActivePanelPlugin.GetPluginID();

            SetPanelContent(ETDLPanelContent.NONE);   // fires OnPanelHidden cleanly

            s_eLastPanel = ETDLPanelContent.PLUGIN_TOOL;
            s_sLastPanelPluginID = pluginID;
        }
        else
        {
            s_eLastPanel = m_eActivePanel;
            s_sLastPanelPluginID = "";
        }
    }

    //! Called by the frontend after RefreshPlugins() so plugins are enabled
    //! and available for the PLUGIN_TOOL restore lookup. Reads m_aActivePlugins
    //! directly — plugin lifecycle is controller-owned now.
    void RestoreState()
    {
        m_SelectedDeviceId = s_LastSelectedDeviceId;
        m_ChatContactRplId = s_LastChatContactRplId;
        m_sChatContactName = s_sLastChatContactName;

        if (m_SelectedDeviceId != RplId.Invalid())
            m_SelectedMember = GetNetworkMemberById(m_SelectedDeviceId);

        // Plugin panel restore: if last session ended on a plugin panel, see
        // whether that plugin is still enabled now and re-install it as the
        // panel owner so SetPanelContent(PLUGIN_TOOL) doesn't fall back.
        if (s_eLastPanel == ETDLPanelContent.PLUGIN_TOOL && !s_sLastPanelPluginID.IsEmpty())
        {
            foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
            {
                if (plugin && plugin.GetPluginID() == s_sLastPanelPluginID)
                {
                    m_ActivePanelPlugin = plugin;
                    break;
                }
            }
        }

        SetPanelContent(s_eLastPanel);
    }

    // ============================================
    // SHARED FRONTEND BEHAVIOUR
    //
    // The public surface both frontends call into, so the fullscreen menu and
    // the world-space device drive identical behaviour from their own loops.
    // ============================================

    // -------- Per-frame tick --------
    //! Drives plugin per-frame updates, image-card rendering, periodic canvas
    //! redraw, scroll-to-bottom on new chat, camera button visibility, marker
    //! tool action-poll. Called by each frontend from its own per-frame loop
    //! (OnMenuUpdate for fullscreen, EOnFrame for world-space).
    void Tick(float tDelta, InputManager im)
    {
        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
        {
            if (plugin)
                plugin.OnMenuUpdate(tDelta);
        }

        // Image-card rendering — walks the parallel arrays once per tick.
        // Cheap when nothing's pending (a few null checks).
        DriveImageCardRendering();

        // Periodic re-Draw of already-rendered canvases. Image canvases lose
        // their draw commands after several minutes of idle time (engine-side
        // cleanup of CanvasWidgetCommand arrays — symptom: blank canvases on
        // otherwise-still-alive image-messages). Re-Draw rebuilds commands
        // from live m_PhotoData.
        if (m_aChatMessageRenderers && m_aChatMessageRenderers.Count() > 0)
        {
            m_fImageRedrawAccum += tDelta;
            if (m_fImageRedrawAccum >= IMAGE_REDRAW_INTERVAL)
            {
                m_fImageRedrawAccum = 0;
                foreach (AG0_TDLPhotoRenderer r : m_aChatMessageRenderers)
                {
                    if (r)
                        r.Draw();
                }
            }
        }
        else
        {
            m_fImageRedrawAccum = 0;
        }

        // Scroll-to-bottom one-shot after PopulateChatView or new message
        // arrived. Applied on the tick following the populate so the scroll
        // layout has settled before we slam its slider.
        if (m_bScrollToBottom && m_wChatScrollLayout)
        {
            m_wChatScrollLayout.SetSliderPos(0, 1.0);
            m_bScrollToBottom = false;
        }

        // Detail view live refresh — controller owns the widget refs.
        if (m_eActivePanel == ETDLPanelContent.MEMBER_DETAIL)
            PopulateDetailView();

        // Camera button visibility/tint — cheap, runs every frame so the
        // toolbar reflects remote toggles immediately.
        UpdateCameraButtonState();

        // Bloodhound button tint — has to run every frame for the same
        // reason UpdateCameraButtonState does: SCR_ButtonEffectColor on
        // each toolbar button animates colour on hover/focus events back
        // to layout-declared values (m_cDefault / m_cFocusLost), which
        // overwrites our script-set amber on unhover. Re-applying each
        // frame keeps the active tint visible.
        UpdateBloodhoundButtonVisual();

        // Same per-frame re-apply as the bloodhound tint, with the extra reason that the
        // keybind can toggle 3D without this button being touched — reading the mode back
        // from the view every frame is what keeps the two entry points agreeing.
        UpdateMap3DButtonVisual();

        // Both reasons again: the effect component repaints on unhover, and TDLPeripheralToggle
        // can flip the static from outside the menu entirely.
        UpdatePeripheralButtonVisual();

        // Re-activated every frame it should hold, the way the world-space focus context is.
        // Deliberately NOT activated while a control region has focus: that is what lets
        // vanilla widget navigation drive the d-pad inside a panel, so only the trip out of
        // the map is TDL's to own.
        // The frontend gate is the other half of this — see m_bZoneInputActive. Consumed
        // rather than latched, so a frontend that stops driving its surface stops holding
        // the context without having to remember to clear it.
        // Picking up a pad mid-session has to re-fence the zones, and putting it down has to
        // un-fence them. Checked every frame but acted on only at the transition — the apply
        // walks three whole subtrees, and the answer changes about as often as the operator
        // changes hands.
        if (ZONE_NAV_ENABLED)
        {
            bool directionalNow = IsDirectionalNavActive();
            if (directionalNow != m_bDirectionalNavApplied)
            {
                m_bDirectionalNavApplied = directionalNow;
                ApplyZoneFocusContainment();
            }
        }

        if (ZONE_NAV_ENABLED && im && m_bZoneInputActive && m_eFocusZone == ETDLFocusZone.MAP)
            im.ActivateContext(MAP_ZONE_CONTEXT);

        m_bZoneInputActive = false;

        // Pumped every frame, not only while the wheel is up: it also has to watch the
        // confirm button come back up after an entry fires — see IsConsumingClicks.
        if (m_RadialMenu)
            m_RadialMenu.Tick(tDelta, im);

        // Bloodhound — cursor info + range/bearing line. No-op (with cheap
        // hide-readout fall-through) when disabled. Runs after the map view
        // has been Update()'d by the frontend's display-controller tick so
        // ScreenToWorld / WorldToLayout reflect this frame's view state.
        UpdateBloodhound();

        // Anchored position block. Runs regardless of the bloodhound's state — it is chrome,
        // not tool output — and no-ops entirely until the layout carries the PointReadout names.
        UpdatePointReadout();

        // Marker tool placement actions (gamepad A / keyboard Enter / X / R).
        // Polled here because the menu's focus chain consumes the actions
        // before InputManager listeners fire — but GetActionTriggered still
        // works in this context.
        //
        // Gated on the MAP zone. A is deliberately overloaded — on the map it places at the
        // crosshair, inside a control region it activates the focused widget — and this poll
        // reads the raw action, so without the gate both meanings fire at once: focus the
        // toolbar, the left controls or the side panel, press A to work the widget under
        // focus, and a marker lands on the map behind it. The panel's own Place button is
        // unaffected because it routes through OnMarkerToolPlaceButtonClicked, not this poll,
        // so placing from a focused panel still works exactly as before.
        //
        // Safe for mouse and keyboard: m_eFocusZone only ever leaves MAP through the three
        // d-pad zone-entry actions, so a player who never touches a pad is always in MAP.
        if (IsToolPanelActive() && m_MarkerToolPanel && im
            && m_eFocusZone == ETDLFocusZone.MAP
            && !IsRadialConsumingClicks() && !m_bHasDevicePointer)
            m_MarkerToolPanel.TickPlaceActionPoll(im);

        // Shape ghost preview — refresh every frame so the rubber-band
        // tracks the cursor even when the player is just hovering. The
        // function null-clears the renderer when shape mode isn't active,
        // which is how the ghost disappears after a commit or cancel.
        UpdateShapeGhost();

        // Gamepad-scroll focus gate — every frame, enable each scroll
        // layout's SCR_GamepadScrollComponent only when the gamepad focus
        // chain is currently within that scroll subtree. Otherwise the
        // right stick scrolls every layout in view in parallel with
        // moving the cursor, which is upsetting on console.
        TickScrollFocusGate();

        // Freehand draw — TDLDraw-held drives per-frame cursor sampling
        // and release-transition drives commit. Same logic on menu KBM,
        // world-space normal mode, and world-space focus mode because the
        // cursor world position is pushed via SetShapeCursorWorld from
        // whichever frontend is active.
        TickFreehandDraw(im);

        // Delete sweep — same TDLDraw-held model but in the panel's
        // delete sub-mode. Each frame the action is held, sweep-delete
        // the player's own markers within radius of the cursor world.
        TickDeleteSweep(im);

        // Shape confirm button visibility — hidden unless a variadic shape
        // is mid-draw with enough points to commit. Pairs with the ghost
        // refresh so both the in-progress preview and the commit affordance
        // update in lock-step.
        UpdateShapeConfirmButtonVisibility();

        // Re-attach member card handlers each tick. The previous count-based
        // skip missed the case where AG0_TDLDisplayController.RebuildMemberCards
        // produces new widget instances at the same total count (one member
        // leaves, another joins in the same tick) — RebuildMemberCards fires
        // on cached-id mismatch, not just count change, so a count comparison
        // can't detect every rebuild. AttachCardHandlers is idempotent: it
        // skips cards that already have a handler attached, so the per-tick
        // cost is one FindHandler call per card (single-digit members; cheap).
        if (m_DisplayController)
            AttachCardHandlers();

        // Web mirror — build a fresh snapshot from current UI state, compare to
        // the last sent one, and dispatch up to the server when anything user-
        // visible changed (subject to the 10 Hz cap). Cheap when the device
        // isn't mirrored because the server simply ignores the payload.
        TickMirrorUplink();
    }

    // -------- Plugin lifecycle --------
    //! Tear down existing plugins (Disable + OnMenuClosed) and rebuild from
    //! m_ActiveDevice.GetAvailablePlugins() filtered by the player's held
    //! devices. Spawns toolbar buttons + fires OnMenuOpened. m_ActiveDevice
    //! must be set (via SetActiveDevice) before calling.
    void RefreshPlugins()
    {
        // Tear down old toolbar buttons before the plugins behind them are
        // disabled — the click handler dereferences the plugin via the relay,
        // so removing buttons first guarantees no stale dispatch.
        ClearPluginToolbarButtons();

        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
        {
            if (plugin)
                plugin.Disable();
        }
        m_aActivePlugins.Clear();

        if (!m_ActiveDevice || !m_ActiveDevice.HasCapability(AG0_ETDLDeviceCapability.ATAK_DEVICE))
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        array<AG0_TDLDeviceComponent> heldDevices = controller.GetHeldDevicesCached();

        // Union all plugin IDs supported by the player's held devices. A plugin
        // only enables if at least one held device claims to support it.
        set<string> supportedPluginIDs = new set<string>();
        foreach (AG0_TDLDeviceComponent device : heldDevices)
        {
            array<string> devicePlugins = device.GetSupportedATAKPlugins();
            if (!devicePlugins)
                continue;

            foreach (string pluginID : devicePlugins)
                supportedPluginIDs.Insert(pluginID);
        }

        array<ref AG0_ATAKPluginBase> availablePlugins = m_ActiveDevice.GetAvailablePlugins();
        foreach (AG0_ATAKPluginBase plugin : availablePlugins)
        {
            if (!supportedPluginIDs.Contains(plugin.GetPluginID()))
                continue;

            IEntity sourceDevice = FindSourceDeviceForPlugin(plugin.GetPluginID(), heldDevices);
            plugin.Enable(m_ActiveDevice, sourceDevice, this);
            m_aActivePlugins.Insert(plugin);
        }

        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
        {
            if (plugin)
                plugin.OnMenuOpened(m_wRoot);
        }

        // Plugins are now Enabled and have their menu root — render their
        // toolbar buttons. Runs after OnMenuOpened so plugins can finish any
        // setup (e.g. cached widget refs) before their button is clickable.
        BuildPluginToolbarButtons();
    }

    //! Fire OnMenuClosed + Disable on every active plugin and clear the array.
    //! Toolbar buttons are torn down first (same ordering reason as
    //! RefreshPlugins). Called by frontends on close / unmount.
    void DisablePlugins()
    {
        ClearPluginToolbarButtons();

        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
        {
            if (!plugin)
                continue;
            plugin.OnMenuClosed();
            plugin.Disable();
        }
        m_aActivePlugins.Clear();
    }

    //! Resolve the entity that owns the device that supports the given plugin
    //! ID. Used to pass a sourceDevice into Plugin.Enable so the plugin can
    //! find its backing radio / camera / etc. component.
    IEntity FindSourceDeviceForPlugin(string pluginID, array<AG0_TDLDeviceComponent> devices)
    {
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            array<string> supported = device.GetSupportedATAKPlugins();
            if (supported && supported.Contains(pluginID))
                return device.GetOwner();
        }
        return null;
    }

    // ============================================
    // PLUGIN TOOLBAR BUTTONS
    //
    // Walks active plugins, spawns PluginToolbarButton.layout into m_wToolbar
    // for each plugin returning ProvidesToolbarTool() == true, sets icon from
    // GetToolIcon(), and routes clicks to OnToolActivated(m_wRoot) via a
    // per-button AG0_PluginButtonClickRelay (necessary because m_OnClicked is
    // untyped — fires zero-arg, so the handler can't disambiguate by sender).
    // ============================================
    void BuildPluginToolbarButtons()
    {
        if (!m_wToolbar)
            return;

        foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
        {
            if (!plugin || !plugin.ProvidesToolbarTool())
                continue;

            Widget btnRoot = GetGame().GetWorkspace().CreateWidgets(PLUGIN_TOOLBAR_BUTTON_LAYOUT, m_wToolbar);
            if (!btnRoot)
            {
                Print(string.Format("[TDLMenuController] Failed to spawn toolbar button for plugin '%1'", plugin.GetPluginID()), LogLevel.WARNING);
                continue;
            }

            ImageWidget icon = ImageWidget.Cast(btnRoot.FindAnyWidget("PluginIcon"));
            ResourceName toolIcon = plugin.GetToolIcon();
            if (icon && !toolIcon.IsEmpty())
                icon.LoadImageTexture(0, toolIcon);

            // Per-button relay captures the plugin + menu root + this
            // controller. OnClick re-binds the plugin's m_Controller back
            // to this frontend before dispatching OnToolActivated, so the
            // panel opens on the frontend that owns the button regardless
            // of cross-frontend interference (menu open/close cycles
            // sharing the same plugin instance). Held in
            // m_aPluginClickRelays so it lives as long as the button does.
            SCR_ModularButtonComponent btnComp = SCR_ModularButtonComponent.FindComponent(btnRoot);
            if (btnComp)
            {
                AG0_PluginButtonClickRelay relay = new AG0_PluginButtonClickRelay(plugin, m_wRoot, this);
                m_aPluginClickRelays.Insert(relay);
                btnComp.m_OnClicked.Insert(relay.OnClick);
            }

            m_aPluginToolbarButtons.Insert(btnRoot);
        }
    }

    void ClearPluginToolbarButtons()
    {
        foreach (Widget btn : m_aPluginToolbarButtons)
        {
            if (btn)
                btn.RemoveFromHierarchy();
        }
        m_aPluginToolbarButtons.Clear();
        m_aPluginClickRelays.Clear();
    }

    // ============================================
    // CHAT — subscriptions / send / populate / image card rendering
    //
    // Migrated from AG0_TDLMenuUI as part of the world-space parity refactor.
    // Subscribe is tied to the frontend's lifecycle (menu open/close +
    // world-space mount/unmount) so both surfaces can subscribe independently
    // without one closing breaking the other. Send + populate route through
    // SCR_PlayerController which is global per-client.
    // ============================================
    void SubscribeToMessageUpdates()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        controller.GetOnMessagesUpdated().Insert(OnMessagesUpdated);
        controller.GetOnNewMessageReceived().Insert(OnNewMessageReceived);

        // Image-message rendering doesn't subscribe to the photo manager
        // invokers. DriveImageCardRendering (called every Tick) is the
        // deterministic path. Invokers were unreliable across the MP
        // replication boundary.
    }

    void UnsubscribeFromMessageUpdates()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        controller.GetOnMessagesUpdated().Remove(OnMessagesUpdated);
        controller.GetOnNewMessageReceived().Remove(OnNewMessageReceived);
    }

    void OnMessagesUpdated(int networkId)
    {
        if (m_eActivePanel == ETDLPanelContent.DIRECT_CHAT)
        {
            if (m_NetworkDevice && m_NetworkDevice.GetCurrentNetworkID() == networkId)
                PopulateChatView();
        }
        // Badges refresh regardless of active panel — the contact list is
        // visible on the network/member panels, and we want unread counters
        // to reflect new arrivals even when the user isn't in chat view.
        UpdateMemberCardBadges();
    }

    void OnNewMessageReceived(int networkId, int messageId)
    {
        if (m_eActivePanel == ETDLPanelContent.DIRECT_CHAT)
            m_bScrollToBottom = true;
    }

    void OnChatSendClicked()
    {
        SendChatMessage();
    }

    void OnViewDirectChatClicked()
    {
        if (!m_SelectedMember)
            return;

        OpenDirectChat(m_SelectedDeviceId, m_SelectedMember.GetPlayerName());
    }

    void OpenDirectChat(RplId contactRplId, string contactName)
    {
        SetChatContact(contactRplId, contactName);
        SetPanelContent(ETDLPanelContent.DIRECT_CHAT);
    }

    void SendChatMessage()
    {
        if (!m_ChatEditBox || !m_NetworkDevice)
            return;

        string content = m_ChatEditBox.GetText();
        if (content.IsEmpty())
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        RplId senderDeviceRplId = m_NetworkDevice.GetDeviceRplId();

        if (m_ChatContactRplId != RplId.Invalid())
            SCR_PlayerController.RequestSendDirectMessage(controller, senderDeviceRplId, content, m_ChatContactRplId);

        m_ChatEditBox.SetText("");
        m_bScrollToBottom = true;
    }

    void PopulateChatView()
    {
        if (!m_wChatMessageList)
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller || !m_NetworkDevice)
            return;

        int networkId = m_NetworkDevice.GetCurrentNetworkID();
        RplId myDeviceRplId = m_NetworkDevice.GetDeviceRplId();

        array<ref AG0_TDLMessageClient> messages = controller.GetDirectMessages(networkId, myDeviceRplId, m_ChatContactRplId);

        ClearChatMessages();

        foreach (AG0_TDLMessageClient msg : messages)
            CreateMessageWidget(msg, myDeviceRplId);

        foreach (AG0_TDLMessageClient msg : messages)
        {
            if (!msg.IsOutgoing(myDeviceRplId) && !controller.IsMessageLocallyRead(msg.messageId))
            {
                controller.MarkMessageLocallyRead(msg.messageId);
                SCR_PlayerController.RequestMarkMessageRead(controller, myDeviceRplId, msg.messageId);
            }
        }

        // Marking-as-read above changed the unread count for the active
        // contact; refresh badges so the dot clears (or count decrements)
        // without waiting for the next OnMessagesUpdated tick.
        UpdateMemberCardBadges();

        m_bScrollToBottom = true;
    }

    void ClearChatMessages()
    {
        foreach (Widget w : m_aChatMessageWidgets)
        {
            if (w)
                w.RemoveFromHierarchy();
        }
        m_aChatMessageWidgets.Clear();
        // Renderers are ref-counted; clearing the array drops the last ref so
        // they release their CanvasWidget references. Their canvases are
        // destroyed by the RemoveFromHierarchy() pass above, so we rely on
        // this ordering — never the other way around.
        m_aChatMessageRenderers.Clear();
        m_aChatMessageDeliveryIds.Clear();
        m_aChatMessageImageCanvases.Clear();
    }

    void CreateMessageWidget(AG0_TDLMessageClient msg, RplId viewerRplId)
    {
        if (!m_wChatMessageList)
            return;

        Widget card = GetGame().GetWorkspace().CreateWidgets(MESSAGE_CARD_LAYOUT, m_wChatMessageList);
        if (!card)
            return;

        m_aChatMessageWidgets.Insert(card);

        TextWidget playerName = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
        TextWidget messageContent = TextWidget.Cast(card.FindAnyWidget("MessageContent"));
        TextWidget timeWidget = TextWidget.Cast(card.FindAnyWidget("Time"));
        ImageWidget statusDot = ImageWidget.Cast(card.FindAnyWidget("StatusDot"));

        if (playerName)
            playerName.SetText(msg.senderCallsign);

        if (messageContent)
            messageContent.SetText(msg.content);

        if (timeWidget)
            timeWidget.SetText(FormatTimestamp(msg.timestamp));

        bool isOutgoing = msg.IsOutgoing(viewerRplId);
        if (statusDot)
        {
            if (isOutgoing)
            {
                switch (msg.status)
                {
                    case ETDLMessageStatus.PENDING:
                        statusDot.SetColor(Color.Gray);
                        break;
                    case ETDLMessageStatus.DELIVERED:
                        statusDot.SetColor(Color.FromInt(0xFF33CCCC));
                        break;
                    case ETDLMessageStatus.READ:
                        statusDot.SetColor(Color.FromInt(0xFF00FF00));
                        break;
                }
            }
            else
            {
                statusDot.SetVisible(false);
            }
        }

        // ----- Image-message setup -----
        // TEXT messages: remove ImageOverlay so the card collapses to
        // header+content. IMAGE messages: stash canvas + deliveryId in the
        // parallel arrays. Actual rendering happens in DriveImageCardRendering
        // on the next Tick (or any subsequent tick once the photo lands in
        // the manager's cache). Show a placeholder caption meanwhile.
        string trackedDeliveryId = "";
        CanvasWidget trackedCanvas = null;

        if (msg.contentType != ETDLMessageContentType.IMAGE)
        {
            Widget imageOverlayKill = card.FindAnyWidget("ImageOverlay");
            if (imageOverlayKill)
                imageOverlayKill.RemoveFromHierarchy();
        }
        else
        {
            Widget imageOverlay = card.FindAnyWidget("ImageOverlay");
            CanvasWidget imageCanvas = CanvasWidget.Cast(card.FindAnyWidget("ImageCanvas"));

            if (imageOverlay && imageCanvas)
            {
                imageOverlay.SetVisible(true);
                imageCanvas.SetVisible(false);

                trackedDeliveryId = msg.imageDeliveryId;
                trackedCanvas = imageCanvas;

                string statusText;
                if (msg.imageTransferState == ETDLImageTransferState.FAILED)
                    statusText = "[image — transfer failed]";
                else
                    statusText = "[image — incoming…]";

                if (messageContent)
                {
                    string combined = msg.content;
                    if (!combined.IsEmpty())
                        combined = combined + " ";
                    combined = combined + statusText;
                    messageContent.SetText(combined);
                }
            }
        }

        // Append to all four parallel arrays in lockstep.
        m_aChatMessageRenderers.Insert(null);
        m_aChatMessageDeliveryIds.Insert(trackedDeliveryId);
        m_aChatMessageImageCanvases.Insert(trackedCanvas);
    }

    //! Walks the per-card parallel arrays. For each image-message card with a
    //! null renderer, look up the photo by deliveryId. If it's in the manager's
    //! decoded cache, render into the card's canvas — size the SizeLayout to
    //! the photo's aspect, instantiate the renderer, defer Init+Draw to next
    //! frame so the layout pass settles.
    void DriveImageCardRendering()
    {
        int n = m_aChatMessageWidgets.Count();
        if (n == 0)
            return;
        if (m_aChatMessageRenderers.Count() != n) return;
        if (m_aChatMessageDeliveryIds.Count() != n) return;
        if (m_aChatMessageImageCanvases.Count() != n) return;

        AG0_TDLPhotoManager photoMgr = AG0_TDLPhotoManager.GetActiveInstance();
        if (!photoMgr) return;

        for (int i = 0; i < n; i++)
        {
            if (m_aChatMessageRenderers[i] != null) continue;

            string deliveryId = m_aChatMessageDeliveryIds[i];
            if (deliveryId.IsEmpty()) continue;

            CanvasWidget canvas = m_aChatMessageImageCanvases[i];
            if (!canvas) continue;

            AG0_TDLPhotoData photo = photoMgr.GetDecodedPhoto(deliveryId);
            if (!photo) continue;

            Widget card = m_aChatMessageWidgets[i];
            if (card)
            {
                SizeLayoutWidget sizeLayout = SizeLayoutWidget.Cast(card.FindAnyWidget("ImageContainer"));
                if (sizeLayout && photo.m_iWidth > 0 && photo.m_iHeight > 0)
                    SizePhotoCanvas(sizeLayout, photo.m_iWidth, photo.m_iHeight);
            }

            canvas.SetVisible(true);

            AG0_TDLPhotoRenderer renderer = new AG0_TDLPhotoRenderer();
            m_aChatMessageRenderers[i] = renderer;
            GetGame().GetCallqueue().CallLater(InitAndDrawPhotoRenderer, 0, false,
                renderer, canvas, photo);
        }
    }

    protected void SizePhotoCanvas(SizeLayoutWidget sizeLayout, int photoW, int photoH)
    {
        const float MAX_W = 384.0;
        const float MAX_H = 384.0;
        const float MIN_DIM = 96.0;

        float pw = photoW;
        float ph = photoH;
        float aspect = pw / ph;

        float targetW = MAX_W;
        float targetH = targetW / aspect;
        if (targetH > MAX_H)
        {
            targetH = MAX_H;
            targetW = targetH * aspect;
        }
        if (targetW < MIN_DIM) targetW = MIN_DIM;
        if (targetH < MIN_DIM) targetH = MIN_DIM;

        sizeLayout.SetMinDesiredWidth(targetW);
        sizeLayout.SetMaxDesiredWidth(targetW);
        sizeLayout.SetMinDesiredHeight(targetH);
        sizeLayout.SetMaxDesiredHeight(targetH);
    }

    protected void InitAndDrawPhotoRenderer(AG0_TDLPhotoRenderer renderer, CanvasWidget canvas, AG0_TDLPhotoData photo)
    {
        if (!renderer || !canvas || !photo)
            return;
        if (!renderer.Init(canvas))
            return;
        renderer.SetPhotoData(photo);
        renderer.Draw();
    }

    protected string FormatTimestamp(int timestamp)
    {
        int now = System.GetUnixTime();
        int diff = now - timestamp;

        if (diff < 60)
            return "Just now";
        else if (diff < 3600)
            return string.Format("%1m ago", diff / 60);
        else if (diff < 86400)
            return string.Format("%1h ago", diff / 3600);
        else
            return string.Format("%1d ago", diff / 86400);
    }

    //! Update each member-card's notification dot + count based on per-contact
    //! unread direct messages. Called on initial card attach, when new
    //! messages arrive, and after PopulateChatView marks the active
    //! conversation read.
    void UpdateMemberCardBadges()
    {
        if (!m_DisplayController || !m_NetworkDevice)
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        int networkId = m_NetworkDevice.GetCurrentNetworkID();
        RplId myDeviceRplId = m_NetworkDevice.GetDeviceRplId();

        array<Widget> cards = m_DisplayController.GetMemberCards();
        array<RplId> cardIds = m_DisplayController.GetMemberCardIds();
        if (!cards || !cardIds)
            return;

        for (int i = 0; i < cards.Count(); i++)
        {
            Widget card = cards[i];
            if (!card || i >= cardIds.Count())
                continue;

            RplId contactId = cardIds[i];
            int unread = controller.GetDirectChatUnreadCount(networkId, myDeviceRplId, contactId);

            Widget notifDot = card.FindAnyWidget("NotificationDot");
            if (!notifDot)
                continue;

            if (unread <= 0)
            {
                notifDot.SetVisible(false);
                continue;
            }

            notifDot.SetVisible(true);
            TextWidget numText = TextWidget.Cast(card.FindAnyWidget("NotificationNumberText"));
            if (numText)
                numText.SetText(unread.ToString());
        }
    }

    // ============================================
    // CALLSIGN (Settings panel)
    //
    // Migrated from AG0_TDLMenuUI. Reads the callsign edit box, requests a
    // SetDeviceCallsign RPC against m_NetworkDevice, returns to the network
    // list. RPC routing is global per-client — host-independent.
    // ============================================
    void OnCallsignSaveClicked()
    {
        if (!m_CallsignEditBox || !m_NetworkDevice)
            return;

        string newCallsign = m_CallsignEditBox.GetText();
        if (newCallsign.IsEmpty())
            return;

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        RplId deviceId = m_NetworkDevice.GetDeviceRplId();
        if (deviceId != RplId.Invalid())
            controller.RequestSetDeviceCallsign(deviceId, newCallsign);

        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    // ============================================
    // CAMERA BROADCAST TOGGLE
    //
    // Toggles the local camera-capable device's broadcasting state via RPC.
    // Remote feed view (swap render camera) is intentionally NOT here — that
    // path stays menu-only because swapping the player's active render camera
    // doesn't make sense from a world-space ATAK they're physically looking
    // at. World-space gets the broadcasting toggle but not view-feed swap.
    // ============================================
    void OnCameraButtonClicked()
    {
        AG0_TDLDeviceComponent cameraDevice = GetLocalCameraDevice();
        if (!cameraDevice)
            return;

        bool newState = !cameraDevice.IsCameraBroadcasting();

        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (controller)
        {
            RplId deviceRplId = cameraDevice.GetDeviceRplId();
            if (deviceRplId != RplId.Invalid())
                controller.RequestSetCameraBroadcasting(deviceRplId, newState);
        }
    }

    AG0_TDLDeviceComponent GetLocalCameraDevice()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return null;

        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
                return device;
        }
        return null;
    }

    //! Refresh the camera toolbar button: visible iff the player has a
    //! camera-capable device, tinted red while broadcasting. Driven from
    //! Tick — cheap enough to run every frame and avoids missing toggles
    //! initiated remotely or by an admin RPC.
    void UpdateCameraButtonState()
    {
        if (!m_wCameraButton)
            return;

        AG0_TDLDeviceComponent cameraDevice = GetLocalCameraDevice();
        bool hasCamera = cameraDevice != null;

        m_wCameraButton.SetVisible(hasCamera);

        if (hasCamera)
        {
            bool isBroadcasting = cameraDevice.IsCameraBroadcasting();
            ImageWidget icon = ImageWidget.Cast(m_wCameraButton.FindAnyWidget("CameraImage"));
            if (icon)
            {
                if (isBroadcasting)
                    icon.SetColor(Color.FromRGBA(255, 100, 100, 255));
                else
                    icon.SetColor(Color.FromRGBA(255, 255, 255, 255));
            }
        }
    }

    // ============================================
    // MARKER TOOL
    //
    // Owns the marker tool side-panel + the centre-screen crosshair widget.
    // Built lazily — sub-form layouts spawn on first OnPanelShown so we
    // don't pay creation cost when the player never opens the tool.
    // Place button uses m_DisplayController to read the map view's centre;
    // marker delete drops the corresponding vanilla marker widget by id
    // (so the on-map icon disappears the same frame as the scroll card,
    // not next-frame after the manager's broadcast).
    // ============================================
    void InitMarkerToolPanel()
    {
        if (!m_wMarkerToolContent)
            return;

        m_MarkerToolPanel = new AG0_TDLMarkerToolPanel();
        if (m_MarkerToolPanel.Init(m_wMarkerToolContent))
        {
            m_MarkerToolPanel.m_OnCancelRequested.Insert(OnMarkerToolCancelRequested);
            m_MarkerToolPanel.m_OnPlaceRequested.Insert(OnMarkerToolPlaceRequested);
            m_MarkerToolPanel.m_OnMarkerDeleted.Insert(OnMarkerToolMarkerDeleted);
            m_MarkerToolPanel.m_OnShapeDrawCommitted.Insert(OnShapeDrawCommitted);
        }
        else
        {
            m_MarkerToolPanel = null;
        }
    }

    //! Fired by the marker-tool panel's Cancel/Back buttons (sub-form +
    //! side-panel back). Returns to the contacts list.
    void OnMarkerToolCancelRequested()
    {
        SetPanelContent(ETDLPanelContent.NETWORK_LIST);
    }

    //! Fired by the marker-tool panel's Place Public/Private buttons + the
    //! action-poll path (gamepad A / keyboard Enter / X / R). Always places
    //! at the map view's centre — the player has already framed the map to
    //! where they want the marker via pan, and the centre is the "crosshair"
    //! the ghost widget aligns to on console.
    void OnMarkerToolPlaceRequested(bool isLocal)
    {
        if (!m_MarkerToolPanel || !m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        // Everything lands under the crosshair, which is the pad's cursor. It answers with
        // the map centre until the right stick has actually moved it, so a player who never
        // touches the stick gets the framed-in centre this used to hardcode.
        vector target = GetCrosshairWorld();

        if (m_MarkerToolPanel.IsShapeModeActive())
        {
            m_MarkerToolPanel.OnShapeClick(target);
            return;
        }

        // Delete mode treats the place action as "remove what is under the crosshair" — same
        // ergonomics, inverted in intent.
        if (m_MarkerToolPanel.IsDeleteModeActive())
        {
            m_MarkerToolPanel.SweepDeleteAt(target);
            return;
        }

        m_MarkerToolPanel.PlaceCurrentMarker(target, isLocal);
    }

    //! Mouse click on the dedicated panel-level Place button. Always a public placement,
    //! under the crosshair — mirrors the gamepad TDLPlaceMarker action's behaviour.
    void OnMarkerToolPlaceButtonClicked()
    {
        OnMarkerToolPlaceRequested(false);
    }

    //! Mouse / focus-A on the shape-only confirm button (sibling of the
    //! place button in MarkerToolContent). Forwards to the panel's variadic
    //! commit path, which is a no-op when the session isn't ready — the
    //! visibility gate in UpdateShapeConfirmButtonVisibility already hides
    //! the button in that case, but the redundant check keeps the click
    //! handler self-contained.
    void OnMarkerToolShapeConfirmButtonClicked()
    {
        if (m_MarkerToolPanel)
            m_MarkerToolPanel.OnShapeCommitAction();
    }

    //! Forwarded from AG0_TDLMarkerToolPanel.m_OnMarkerDeleted right after the
    //! AskRemoveStaticMarker RPC fires. Display controller's 3D widget map is
    //! keyed by SCR_MapMarkerBase reference and only prunes during
    //! UpdateVanillaMarkers — which happens next frame and sees the manager's
    //! *current* state, which still has the marker until the server's
    //! broadcast back. Kicking the prune now removes the on-map icon the same
    //! frame as the scroll card.
    void OnMarkerToolMarkerDeleted(int markerId)
    {
        if (m_DisplayController)
            m_DisplayController.DropVanillaMarkerWidgetById(markerId);
    }

    //! Crosshair visible iff the marker tool panel is the active side panel.
    void UpdateMarkerCrosshairVisibility()
    {
        if (!m_wMarkerCrosshair)
            return;

        // Shown for the marker tool as before, and additionally whenever the reticle is
        // being used as a cursor — otherwise a pad user would be aiming something invisible.
        m_wMarkerCrosshair.SetVisible(IsToolPanelActive()
            || m_bCrosshairActive);
    }

    //! Tap-to-recenter for the 3D view. Rides the same per-click dispatch as
    //! the other tools and yields to every one of them: a click the marker
    //! tool or bloodhound would act on must not ALSO swing the camera. What
    //! remains — a bare tap on terrain while the 3D pane is up — re-aims the
    //! orbit focus at the tapped point. This is the world-space surface's pan
    //! (it has only one drag axis, and that one orbits); on the menu it
    //! complements right-drag.
    void OnMapClickedForMap3DFocus(int absMouseX, int absMouseY)
    {
        if (IsRadialConsumingClicks())
            return;
        if (IsToolPanelActive())
            return;
        if (s_bBloodhoundEnabled)
            return;
        if (!m_DisplayController || !m_wRoot)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasScreenX;
        float canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);
        mapView.TapFocusMap3D(absMouseX - canvasScreenX, absMouseY - canvasScreenY);
    }

    //------------------------------------------------------------------------------------------------
    //! Fired by AG0_TDLMapCanvasDragHandler when the user left-clicks
    //! MapDragSurface without significant drag (KBM placement path).
    //! Converts the absolute workspace mouse coords into canvas-local pixels,
    //! runs them through AG0_TDLMapView.ScreenToWorld, and places the marker
    //! there. Only active while the marker tool panel is visible.
    void OnMapClickedForMarkerPlacement(int absMouseX, int absMouseY)
    {
        if (IsRadialConsumingClicks())
            return;
        if (!IsToolPanelActive())
            return;
        if (!m_MarkerToolPanel || !m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasScreenX, canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);

        float localX = absMouseX - canvasScreenX;
        float localY = absMouseY - canvasScreenY;

        vector worldPos;
        mapView.ScreenToWorld(localX, localY, worldPos);

        if (m_MarkerToolPanel.IsShapeModeActive())
        {
            m_MarkerToolPanel.OnShapeClick(worldPos);
            return;
        }

        if (m_MarkerToolPanel.IsDeleteModeActive())
        {
            m_MarkerToolPanel.SweepDeleteAt(worldPos);
            return;
        }

        m_MarkerToolPanel.PlaceCurrentMarker(worldPos, false);
    }

    //------------------------------------------------------------------------------------------------
    //! True when the marker tool panel is active AND shape sub-mode is on
    //! AND the active tool is FREEHAND. Menu UI polls this each frame to
    //! tell the drag handler whether to emit freehand samples or treat the
    //! drag as a pan. Cheap chain of accessors so per-frame polling is
    //! fine — no caching needed.
    bool IsFreehandModeActive()
    {
        if (!IsToolPanelActive() || !m_MarkerToolPanel)
            return false;
        if (!m_MarkerToolPanel.IsShapeModeActive())
            return false;
        AG0_TDLShapeDrawSession session = m_MarkerToolPanel.GetShapeDrawSession();
        if (!session)
            return false;
        return session.GetActiveTool() == AG0_ETDLShapeTool.FREEHAND;
    }

    //------------------------------------------------------------------------------------------------
    //! True while TDLDraw is held AND the marker tool panel has FREEHAND
    //! armed. Frontends poll this to gate pan suppression on the drag
    //! handler; Tick polls it to drive per-frame cursor sampling and to
    //! detect release-transitions for commit. Returns false when the
    //! InputManager is null (e.g. session not fully bound yet) so callers
    //! treat that as "not drawing" rather than an error.
    bool IsFreehandDrawActive(InputManager im)
    {
        if (!im)
            return false;
        if (!IsFreehandModeActive())
            return false;
        return im.GetActionValue("TDLDraw") > 0.5;
    }

    //------------------------------------------------------------------------------------------------
    //! True while TDLDraw is held AND the marker tool panel has the
    //! Delete sub-mode active. Frontends poll this to extend pan
    //! suppression to delete sweeps (same reasoning as freehand — user
    //! is interacting with the map content, not the view); Tick polls it
    //! to drive per-frame sweep-delete dispatch.
    bool IsDeleteSweepActive(InputManager im)
    {
        if (!im)
            return false;
        if (!IsToolPanelActive() || !m_MarkerToolPanel)
            return false;
        if (!m_MarkerToolPanel.IsDeleteModeActive())
            return false;
        return im.GetActionValue("TDLDraw") > 0.5;
    }

    //------------------------------------------------------------------------------------------------
    //! Combined "any draw-style input action is active" predicate that
    //! menu UI / world-space use to suppress map pan. Currently TDLDraw
    //! is the only such action and it serves both freehand sampling and
    //! delete sweeping — pan stays suppressed for either.
    bool IsAnyDrawInteractionActive(InputManager im)
    {
        return IsFreehandDrawActive(im) || IsDeleteSweepActive(im);
    }

    //------------------------------------------------------------------------------------------------
    //! Cursor-move sample forwarded from the menu UI / world-space frontend.
    //! Delete mode also needs cursor-world updates so SweepDeleteAt has a
    //! position to test against; without this branch the sweep tick reads
    //! a stale/unknown cursor and the delete tool no-ops every frame.
    void OnMapCursorMovedForShape(int absMouseX, int absMouseY)
    {
        if (!IsToolPanelActive())
            return;
        if (!m_MarkerToolPanel)
            return;
        if (!m_MarkerToolPanel.IsShapeModeActive() && !m_MarkerToolPanel.IsDeleteModeActive())
            return;
        if (!m_DisplayController)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;
        Widget canvasWidget = m_wRoot.FindAnyWidget("MapCanvas");
        if (!canvasWidget)
            return;

        float canvasScreenX, canvasScreenY;
        canvasWidget.GetScreenPos(canvasScreenX, canvasScreenY);

        vector worldPos;
        mapView.ScreenToWorld(absMouseX - canvasScreenX, absMouseY - canvasScreenY, worldPos);
        m_MarkerToolPanel.SetShapeCursorWorld(worldPos);
    }

    //------------------------------------------------------------------------------------------------
    //! World-space frontend equivalent of OnMapCursorMovedForShape. The
    //! world-space cursor already lives in world coordinates, so it pushes
    //! directly without the canvas → screen → world conversion step the KBM
    //! path needs. Same gating as the KBM path.
    void OnShapeCursorWorldFromWorldSpace(vector worldPos)
    {
        if (!IsToolPanelActive())
            return;
        if (!m_MarkerToolPanel)
            return;
        if (!m_MarkerToolPanel.IsShapeModeActive() && !m_MarkerToolPanel.IsDeleteModeActive())
            return;
        m_MarkerToolPanel.SetShapeCursorWorld(worldPos);
    }

    //------------------------------------------------------------------------------------------------
    //! Fallback cursor-world push for fullscreen menu gamepad / keyboard
    //! input. Pushes canvas-center as the cursor world so the shape ghost
    //! and freehand sampling track where TDLPlaceMarker would actually
    //! drop a point, instead of trailing the parked OS mouse cursor.
    //!
    //! Sticky pan-input detection — once TDLPanHorizontal/Vertical has been
    //! deflected within the last 500 ms, the gamepad/keyboard user is
    //! considered active and canvas-center is pushed every frame. Crucially
    //! this survives single-frame drops through the deadzone (stick wobble
    //! between motions), eliminating the up-left-then-back glitch where
    //! freehand samples would spike to the OS mouse position any frame the
    //! stick happened to read zero.
    //!
    //! Mouse users never deflect the pan stick, so this gate never engages
    //! for them — their cursor world stays whatever the OnMapCursorMoved
    //! path pushed, untouched.
    void PushFallbackCursorIfNeeded(InputManager im)
    {
        if (!im || !m_MarkerToolPanel || !m_DisplayController)
            return;
        if (!IsToolPanelActive())
            return;
        if (!m_MarkerToolPanel.IsShapeModeActive() && !m_MarkerToolPanel.IsDeleteModeActive())
            return;

        float now = GetGame().GetWorld().GetWorldTime();
        bool panInputActive = Math.AbsFloat(im.GetActionValue("TDLPanHorizontal")) > 0.1
                           || Math.AbsFloat(im.GetActionValue("TDLPanVertical")) > 0.1;
        if (panInputActive)
            m_fLastMapPanInputTime = now;

        bool panRecentlyActive = (now - m_fLastMapPanInputTime) < 0.5;
        if (!panRecentlyActive)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView || !mapView.IsReady())
            return;

        m_MarkerToolPanel.SetShapeCursorWorld(mapView.GetCenter());
    }

    //------------------------------------------------------------------------------------------------
    //! Fired by AG0_TDLMarkerToolPanel.m_OnShapeDrawCommitted. Serialises the
    //! draft + adds whatever creator context the player controller can
    //! resolve, then RPCs to the server. The local player isn't authoritative
    //! over the shape — the server will fill in id / version / createdAt /
    //! networkId / createdBy / createdByPlayerIdentityId from the live
    //! BackendApi identity and the player's current network membership.
    void OnShapeDrawCommitted(AG0_TDLMapShape draft, AG0_ETDLShapeTool tool)
    {
        if (!draft)
            return;

        // AskCreateShape lives on the modded SCR_PlayerController (see
        // AG0_PlayerController_TDL.c — modded class, not a subtype). Cast
        // through SCR_PlayerController is enough; the modded method is
        // resolved at the engine level.
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return;

        // Client serialises with empty server fields; server fills them in
        // before persisting and broadcasting. Sentinel m_iNetworkId stays at
        // 0 here; server replaces it from the player's BackendApi state.
        string draftJson = draft.ToJsonString();
        if (draftJson.IsEmpty())
            return;

        controller.AskCreateShape(draftJson);
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame ghost refresh. Called from Tick when shape mode is active so
    //! the map view sees the latest in-progress preview. Passing null when no
    //! ghost is available clears any stale preview from the renderer.
    protected void UpdateShapeGhost()
    {
        if (!m_DisplayController || !m_MarkerToolPanel)
            return;

        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        AG0_TDLShapeDrawSession session = m_MarkerToolPanel.GetShapeDrawSession();
        if (!session)
        {
            mapView.SetGhostShape(null);
            return;
        }

        AG0_TDLMapShape ghost;
        if (m_MarkerToolPanel.IsShapeModeActive())
            ghost = session.BuildGhostShape();
        mapView.SetGhostShape(ghost);
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame freehand draw pump. Two paths:
    //!   - Held this frame  → sample the current cursor world position into
    //!     the session. Distance filter + hard cap inside AddFreehandSample
    //!     handle dedup and overflow.
    //!   - Released this frame, was held last frame  → commit via
    //!     OnShapeFreehandEnd. CommitVariadic fires the create RPC if
    //!     enough samples were collected, or no-ops otherwise (session
    //!     stays armed for the next attempt).
    //! Works on every frontend because the cursor world comes from
    //! whichever push path the active frontend has wired (mouse cursor on
    //! menu KBM, m_fCursorX/Y on world-space). The session is always armed
    //! while shape mode + FREEHAND is selected (panel's ReArmShapeSession
    //! handles tool selection + post-commit re-arm), so no press-transition
    //! arm is needed here.
    protected void TickFreehandDraw(InputManager im)
    {
        bool held = IsFreehandDrawActive(im);

        if (held && m_MarkerToolPanel)
        {
            AG0_TDLShapeDrawSession session = m_MarkerToolPanel.GetShapeDrawSession();
            if (session)
                session.TickFreehandFromCursor();
        }
        else if (!held && m_bFreehandDrawHeldLastFrame && m_MarkerToolPanel)
        {
            m_MarkerToolPanel.OnShapeFreehandEnd();
        }

        m_bFreehandDrawHeldLastFrame = held;
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame walk of the controller's widget tree that toggles each
    //! SCR_GamepadScrollComponent's enabled state based on whether the
    //! currently focused widget lives inside that scroll layout. Scroll
    //! layouts whose subtree doesn't contain the focus get their gamepad
    //! scroll component disabled, so right-stick input drives only the
    //! scroll layout the user has actually navigated into.
    //!
    //! Walks the tree every frame rather than caching at init — plugin
    //! panels (MPU5 management, future plugins) spawn scroll widgets after
    //! their OnPanelShown, and dynamic re-collect-on-change would need a
    //! hook on every potential spawn point. The walk is cheap (single-
    //! digit microseconds for our layout) and avoids that bookkeeping.
    protected void TickScrollFocusGate()
    {
        if (!m_wRoot)
            return;

        Widget focused = GetGame().GetWorkspace().GetFocusedWidget();
        GateScrollComponentsRecursive(m_wRoot, focused);
    }

    //------------------------------------------------------------------------------------------------
    //! Recursive helper for TickScrollFocusGate. For every ScrollLayoutWidget
    //! it encounters, call SetEnabled on the attached SCR_GamepadScrollComponent
    //! based on whether `focused` is in that scroll's subtree. Tolerated as a
    //! no-op when the scroll layout has no gamepad component.
    protected void GateScrollComponentsRecursive(Widget root, Widget focused)
    {
        if (!root)
            return;

        ScrollLayoutWidget scroll = ScrollLayoutWidget.Cast(root);
        if (scroll)
        {
            SCR_GamepadScrollComponent comp = SCR_GamepadScrollComponent.Cast(
                scroll.FindHandler(SCR_GamepadScrollComponent));
            if (comp)
                comp.SetEnabled(IsWidgetInSubtree(focused, scroll));
        }

        Widget child = root.GetChildren();
        while (child)
        {
            GateScrollComponentsRecursive(child, focused);
            child = child.GetSibling();
        }
    }

    //------------------------------------------------------------------------------------------------
    //! True when `candidate` is `treeRoot` or any descendant of it. Walks
    //! up the parent chain from candidate rather than the (typically much
    //! larger) child tree from treeRoot — cheaper for the focused-widget
    //! case where the parent chain is bounded by layout depth.
    protected bool IsWidgetInSubtree(Widget candidate, Widget treeRoot)
    {
        if (!candidate || !treeRoot)
            return false;
        Widget cur = candidate;
        while (cur)
        {
            if (cur == treeRoot)
                return true;
            cur = cur.GetParent();
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame delete-sweep pump. While TDLDraw is held in delete mode,
    //! pull the cursor world from the shape draw session (same source
    //! freehand uses — already populated by mouse-move / world-space
    //! EOnFrame / gamepad fallback) and hand it to SweepDeleteAt. Owner +
    //! distance gating happens inside SweepDeleteAt, and m_aPendingDeletes
    //! prevents the same marker from being re-deleted on subsequent
    //! frames while the AskRemoveStaticMarker RPC is still in flight.
    protected void TickDeleteSweep(InputManager im)
    {
        if (!IsDeleteSweepActive(im))
            return;
        if (!m_MarkerToolPanel)
            return;
        AG0_TDLShapeDrawSession session = m_MarkerToolPanel.GetShapeDrawSession();
        if (!session || !session.IsCursorWorldKnown())
            return;
        m_MarkerToolPanel.SweepDeleteAt(session.GetCursorWorld());
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame visibility update for MarkerToolShapeConfirmButton. Shown
    //! when the marker tool panel is the active side panel, shape sub-mode
    //! is selected, the session has a variadic tool armed, and enough
    //! points have been placed to commit — i.e. exactly when the user has
    //! a meaningful "I'm done" action to take. Hidden in every other state
    //! so it doesn't clutter the placed/military sub-forms or sit visible
    //! during a fixed-point tool (circle/rectangle/sector) that auto-commits.
    protected void UpdateShapeConfirmButtonVisibility()
    {
        if (!m_wMarkerToolShapeConfirmButton)
            return;

        bool show = false;
        if (IsToolPanelActive()
            && m_MarkerToolPanel
            && m_MarkerToolPanel.IsShapeModeActive())
        {
            AG0_TDLShapeDrawSession session = m_MarkerToolPanel.GetShapeDrawSession();
            if (session && session.CanCommit())
                show = true;
        }

        m_wMarkerToolShapeConfirmButton.SetVisible(show);
    }

    // ============================================
    // WEB MIRROR — snapshot capture + uplink + inbound apply
    // ============================================

    //! Sample current UI state into a fresh snapshot. Every field comes from
    //! a canonical cross-frontend source — class-level statics for panel /
    //! chat / settings / map view, the controller's own m_NetworkDevice for
    //! device fields. We deliberately avoid instance fields like m_eActivePanel
    //! / m_ChatContactRplId because SetPanelContent / SetChatContact only
    //! update the calling frontend's instance — the other frontend's instance
    //! stays stale until its own input fires, which made the web mirror
    //! alternate between two different "current panel" values frame to frame.
    //!
    //! Server-side fields (playerWorldX/Y/Z, MGRS) stay zero — the server fills
    //! them in before forwarding to the API so the holding client doesn't
    //! recompute every tick.
    AG0_TDLMirrorSnapshot BuildMirrorSnapshot()
    {
        AG0_TDLMirrorSnapshot s = new AG0_TDLMirrorSnapshot();

        s.panel = s_eLastPanel;
        s.panelPluginID = s_sLastPanelPluginID;
        s.chatContactRplId = s_LastChatContactRplId;
        s.chatContactName = s_sLastChatContactName;
        s.selectedDeviceRplId = s_LastSelectedDeviceId;

        // Read map state from the cross-frontend statics rather than this
        // controller's m_DisplayController.GetMapView() — both AG0_TDLDisplayController
        // instances run divergence-sync each frame, so the statics are the
        // single canonical "what's the current map view" answer. Reading from
        // an instance map view would tie the snapshot to whichever frontend
        // happens to be the uplink primary, masking changes the user made on
        // the sibling frontend until the next propagator pass settled.
        vector sharedCenter = AG0_TDLDisplayController.GetSavedCenter();
        s.mapCenterX = sharedCenter[0];
        s.mapCenterZ = sharedCenter[2];
        s.mapZoom = AG0_TDLDisplayController.GetSavedZoom();
        s.playerTracking = AG0_TDLDisplayController.GetPlayerTracking();
        s.trackUp = AG0_TDLDisplayController.GetTrackUp();

        s.brightness = s_fBrightness;
        s.bloodhoundEnabled = s_bBloodhoundEnabled;
        s.bloodhoundPinned = s_bBloodhoundPinned;
        s.bloodhoundPinX = s_vBloodhoundPinPos[0];
        s.bloodhoundPinZ = s_vBloodhoundPinPos[2];

        // Use the player's actual NETWORK_ACCESS device (the radio — MPU5 etc.)
        // rather than the frontend's m_NetworkDevice. World-space's m_NetworkDevice
        // is the gadget the EUD/CDU itself is, which usually does NOT have
        // NETWORK_ACCESS — that's a separate held radio. The in-game settings
        // panel reads callsign from the network device too (see
        // AG0_TDLMenuUI.FindNetworkDevice), so this mirrors what the user
        // actually sees in-game.
        AG0_TDLDeviceComponent networkDevice = FindLocalNetworkDevice();
        if (networkDevice)
        {
            s.deviceCallsign = networkDevice.GetDisplayName();
            s.networkId = networkDevice.GetCurrentNetworkID();
            s.deviceRplId = networkDevice.GetDeviceRplId();
            s.cameraBroadcasting = networkDevice.IsCameraBroadcasting();
        }
        else if (m_NetworkDevice)
        {
            // Fallback to whatever the frontend chose so an EUD-only player
            // (no radio held) still has something on the panel.
            s.deviceCallsign = m_NetworkDevice.GetDisplayName();
            s.networkId = m_NetworkDevice.GetCurrentNetworkID();
            s.deviceRplId = m_NetworkDevice.GetDeviceRplId();
            s.cameraBroadcasting = m_NetworkDevice.IsCameraBroadcasting();
        }

        return s;
    }

    //! Mirror the in-game AG0_TDLMenuUI.FindNetworkDevice logic on the client
    //! side so the snapshot's deviceCallsign / deviceRplId / networkId always
    //! refer to the player's actual radio (NETWORK_ACCESS), not whatever
    //! gadget the frontend happens to be representing. Preference order:
    //! NETWORK_ACCESS + IsInNetwork > NETWORK_ACCESS alone > nothing.
    protected AG0_TDLDeviceComponent FindLocalNetworkDevice()
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return null;
        array<AG0_TDLDeviceComponent> held = pc.GetHeldDevicesCached();
        if (!held)
            return null;
        AG0_TDLDeviceComponent fallback = null;
        foreach (AG0_TDLDeviceComponent device : held)
        {
            if (!device)
                continue;
            if (!device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS))
                continue;
            if (device.IsInNetwork())
                return device;
            if (!fallback)
                fallback = device;
        }
        return fallback;
    }

    //! Per-tick uplink — early-exits when nothing has changed since the last
    //! send, and enforces the 10 Hz cap by tracking world time in ms. World
    //! time (not real wall-clock) so that paused or slowed-down game time
    //! doesn't break the cadence assumption.
    //!
    //! Only the first live controller drives the uplink each frame. Without
    //! this gate, multiple frontends (world-space per held device + fullscreen
    //! menu) both run TickMirrorUplink every frame and race against the static
    //! last-sent snapshot. Use FindPrimaryLiveController so dangling/null
    //! entries in the registry don't strand the uplink with no driver.
    protected void TickMirrorUplink()
    {
        if (FindPrimaryLiveController() != this)
            return;

        PlayerController pcBase = GetGame().GetPlayerController();
        if (!pcBase)
            return;
        SCR_PlayerController pc = SCR_PlayerController.Cast(pcBase);
        if (!pc)
            return;

        AG0_TDLMirrorSnapshot snap = BuildMirrorSnapshot();
        if (!snap)
            return;

        bool changed = !s_LastSentMirrorSnapshot || !snap.EqualsForClientUplink(s_LastSentMirrorSnapshot);
        if (!changed)
            return;

        World world = GetGame().GetWorld();
        if (!world)
            return;
        float nowMs = world.GetWorldTime();
        if (s_fMirrorLastSendTimeMs > 0 && (nowMs - s_fMirrorLastSendTimeMs) < MIRROR_MIN_SEND_INTERVAL_MS)
            return;
        s_fMirrorLastSendTimeMs = nowMs;

        string payload = snap.ToJson();
        pc.PushATAKPanelStateToServer(payload);

        if (!s_LastSentMirrorSnapshot)
            s_LastSentMirrorSnapshot = new AG0_TDLMirrorSnapshot();
        s_LastSentMirrorSnapshot.CopyFrom(snap);
    }

    //! Inbound command application — called by AG0_TDLMirrorCommandDispatcher
    //! after the queue handler resolves the target identity to this client.
    //! Always runs against a live frontend (the dispatcher walks
    //! s_aLiveControllers and bails when empty).
    void ApplyMirrorSetPanel(int panel, string pluginId, int selectedDeviceRplIdInt = 0)
    {
        // Explicit cast — Enfusion is strict about enum<->int and the dispatcher
        // hands us a raw int parsed from JSON. Same pattern below.
        ETDLPanelContent target = panel;

        // Plugin-tool requires re-binding the active plugin before SetPanelContent
        // so the fallback guard there doesn't kick us back to NETWORK_LIST.
        if (target == ETDLPanelContent.PLUGIN_TOOL && !pluginId.IsEmpty())
        {
            foreach (AG0_ATAKPluginBase plugin : m_aActivePlugins)
            {
                if (plugin && plugin.GetPluginID() == pluginId)
                {
                    m_ActivePanelPlugin = plugin;
                    break;
                }
            }
        }

        // Route MEMBER_DETAIL switches through ShowDetailView so the same path
        // an in-game card click takes runs: populates m_SelectedMember, fires
        // the detail-shown event, and persists s_LastSelectedDeviceId. Without
        // this, the web mirror's `mirror_set_panel` ended up at the detail
        // panel with whatever member was last clicked in-game (or none), which
        // surfaced on the website as a null detail view for self.
        if (target == ETDLPanelContent.MEMBER_DETAIL && selectedDeviceRplIdInt != 0)
        {
            RplId deviceId = selectedDeviceRplIdInt;
            AG0_TDLNetworkMember member = GetNetworkMemberById(deviceId);
            if (member)
            {
                ShowDetailView(member, deviceId);
                return;
            }
            // Member lookup miss — fall through to the plain SetPanelContent so
            // the panel still switches; PopulateDetailView's own
            // m_SelectedDeviceId-based refresh will pick the member up on the
            // next aggregated-members tick once it's available.
            m_SelectedDeviceId = deviceId;
            s_LastSelectedDeviceId = deviceId;
        }

        SetPanelContent(target);
    }

    void ApplyMirrorSetChatContact(int rplIdInt, string name)
    {
        RplId rpl = rplIdInt;
        SetChatContact(rpl, name);
        // Open the chat panel to surface the change visually — matches the
        // in-game flow of clicking a contact to start a thread.
        SetPanelContent(ETDLPanelContent.DIRECT_CHAT);
    }

    void ApplyMirrorSetBrightness(float value)
    {
        if (value < 0)   value = 0;
        if (value > 100) value = 100;
        s_fBrightness = value;
        if (m_BrightnessSliderComp)
        {
            s_bSyncingBrightness = true;
            m_BrightnessSliderComp.SetValue(value);
            s_bSyncingBrightness = false;
        }
        ApplyBrightnessToOverlay();
        // Fan-out to the other frontend's overlay (menu vs world-space) so the
        // dim state stays coherent across both surfaces.
        s_OnBrightnessChanged.Invoke();
    }

    void ApplyMirrorSetMapView(float centerX, float centerZ, float zoom, bool tracking, bool trackUp)
    {
        AG0_TDLDisplayController.SetPlayerTracking(tracking);
        AG0_TDLDisplayController.SetTrackUp(trackUp);
        // Route through the fan-out so both frontends' map views sync to the same
        // applied state. The dispatcher already iterates live controllers to call
        // this method, but each per-controller call would otherwise only touch
        // its own m_DisplayController — propagator covers everyone in one shot
        // so the dispatcher's redundant loop is idempotent rather than partial.
        // Center only propagated when tracking is off (in-game CenterOnPlayer
        // overrides it every frame otherwise).
        AG0_TDLDisplayController.PropagateMapState(
            Vector(centerX, 0, centerZ),
            zoom,
            !tracking,
            true);
    }

    void ApplyMirrorToggleBloodhound(bool enabled)
    {
        if (s_bBloodhoundEnabled == enabled)
            return;
        s_bBloodhoundEnabled = enabled;
        UpdateBloodhoundButtonVisual();
        if (!enabled && !s_bBloodhoundPinned)
        {
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            if (m_DisplayController)
            {
                AG0_TDLMapView mv = m_DisplayController.GetMapView();
                if (mv)
                    mv.SetBloodhound(false, vector.Zero, vector.Zero);
            }
        }
    }
}
