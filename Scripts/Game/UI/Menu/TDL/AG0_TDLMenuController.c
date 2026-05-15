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
    protected Widget m_wBloodhoundReadout;
    protected TextWidget m_wBloodhoundGrid;
    protected TextWidget m_wBloodhoundElev;
    protected TextWidget m_wBloodhoundDist;
    protected TextWidget m_wBloodhoundAz;

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
    protected Widget m_wMarkerCrosshair;

    // ============================================
    // PLUGIN TOOLBAR BUTTON LAYOUT
    // ============================================
    protected const ResourceName PLUGIN_TOOLBAR_BUTTON_LAYOUT = "{7DEC0DEDA7AB1E01}UI/layouts/Menus/TDL/PluginToolbarButton.layout";
    protected const ResourceName MESSAGE_CARD_LAYOUT = "{2507AC45B21BBC57}UI/layouts/Menus/TDL/TDLMessageUI.layout";

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

    //! Tracks the card list count from the most recent AttachCardHandlers
    //! pass so Tick can re-attach handlers when the display controller
    //! rebuilds the list (1Hz membership refresh).
    protected int m_iLastCardCount = 0;

    // ============================================
    // ACCESSORS
    // ============================================
    Widget GetRoot() { return m_wRoot; }
    ETDLPanelContent GetActivePanel() { return m_eActivePanel; }
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
        m_wBloodhoundReadout = m_wRoot.FindAnyWidget("BloodhoundReadout");
        m_wBloodhoundGrid    = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundGrid"));
        m_wBloodhoundElev    = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundElev"));
        m_wBloodhoundDist    = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundDist"));
        m_wBloodhoundAz      = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundAz"));

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
        if (m_wBloodhoundReadout)
            m_wBloodhoundReadout.SetVisible(false);

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
        if (m_wMarkerToolButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wMarkerToolButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnMarkerToolButtonClicked);
        }
        if (m_wBloodhoundButton)
        {
            SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.Cast(
                m_wBloodhoundButton.FindHandler(SCR_ModularButtonComponent));
            if (comp)
                comp.m_OnClicked.Insert(OnBloodhoundButtonClicked);
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
    }

    void Cleanup()
    {
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
        bool showMarkerTool = (content == ETDLPanelContent.MARKER_TOOL);
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
                m_MarkerToolPanel.OnPanelShown();
            else
                m_MarkerToolPanel.OnPanelHidden();
        }

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
                float dist = vector.Distance(player.GetOrigin(), m_SelectedMember.GetPosition());
                m_wDetailDistance.SetTextFormat("%1 m", Math.Round(dist).ToString());
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

    protected void OnMarkerToolButtonClicked()
    {
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
        // Reset the diagnostic one-shot so each enable produces a fresh
        // sample line in the log without needing a game restart.
        m_bBloodhoundFirstTickPrinted = false;

        // Disable handling: clear UI ONLY if no pin is active. A pinned
        // bloodhound is treated like a saved range/bearing overlay — it
        // outlives the tool toggle so the operator can stash a measurement,
        // turn the tool off to do other things on the map, then either
        // click the pin to dismiss it or re-enable the tool to drop a new
        // one (which auto-clears the previous pin in OnMapClickedForBloodhound).
        // When unpinned, behaviour is unchanged from before: tool off clears
        // the readout + line on the same frame.
        if (!s_bBloodhoundEnabled && !m_bBloodhoundPinned)
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
        if (!m_DisplayController || !m_wRoot)
            return;

        // Tool is active. Pin set → click unpins (fast path, no projection).
        if (m_bBloodhoundPinned)
        {
            m_bBloodhoundPinned = false;
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
        m_vBloodhoundPinPos = worldPos;
        m_bBloodhoundPinned = true;
    }

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
    //   3. Samples terrain elevation at the cursor via ChimeraWorld.
    //   4. Computes distance + bearing (clockwise from world-Z = north).
    //   5. Updates the four readout TextWidgets, positions the readout at
    //      the cursor in layout space, and pushes the line endpoints to the
    //      map view for DrawBloodhound() to consume.
    // ============================================

    //! World-space frontend pushes its cursor's world position here each tick.
    //! Menu doesn't push — UpdateBloodhound falls back to map centre when
    //! m_bBloodhoundCursorOverrideValid is false.
    protected vector m_vBloodhoundCursorOverride;
    protected bool   m_bBloodhoundCursorOverrideValid;
    //! One-shot diagnostic — flipped true after the first UpdateBloodhound
    //! tick prints widget-ref + distance/bearing values. Reset to false in
    //! OnBloodhoundButtonClicked so each enable produces a fresh sample
    //! line in the log (handy when iterating without restarting the game).
    protected bool   m_bBloodhoundFirstTickPrinted;

    //! Bloodhound pin state — when set, UpdateBloodhound sources the cursor
    //! world position from m_vBloodhoundPinPos instead of the per-frame
    //! override / map centre. Toggled by OnMapClickedForBloodhound: first
    //! click pins at the click's world position, second click unpins and
    //! the readout resumes following the cursor / map centre. Cleared on
    //! tool disable so toggling Bloodhound off-then-on doesn't restore a
    //! stale pin from the previous enable.
    protected bool   m_bBloodhoundPinned;
    protected vector m_vBloodhoundPinPos;
    void SetBloodhoundCursorWorld(vector worldPos)
    {
        m_vBloodhoundCursorOverride = worldPos;
        m_bBloodhoundCursorOverrideValid = true;
    }
    void ClearBloodhoundCursorWorld()
    {
        m_bBloodhoundCursorOverrideValid = false;
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
        // below picks m_vBloodhoundPinPos when m_bBloodhoundPinned, so the
        // pin-only render path is just "fall through with pin as cursor".
        if (!s_bBloodhoundEnabled && !m_bBloodhoundPinned)
        {
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            if (m_DisplayController)
            {
                AG0_TDLMapView mv = m_DisplayController.GetMapView();
                if (mv)
                    mv.SetBloodhound(false, vector.Zero, vector.Zero);
            }
            return;
        }

        if (!m_DisplayController)
            return;
        AG0_TDLMapView mapView = m_DisplayController.GetMapView();
        if (!mapView)
            return;

        // Resolve cursor world position. Priority order:
        //   1. Pinned position (user clicked to lock it) — survives map pan
        //      and cursor movement until a second click unpins.
        //   2. Frontend-supplied live cursor override (world-space variant
        //      pushes its m_fCursorX/Y-derived world pos each tick).
        //   3. Map centre — full-screen menu has no real cursor on the map.
        vector cursorWorld;
        if (m_bBloodhoundPinned)
            cursorWorld = m_vBloodhoundPinPos;
        else if (m_bBloodhoundCursorOverrideValid)
            cursorWorld = m_vBloodhoundCursorOverride;
        else
            cursorWorld = mapView.GetCenter();

        // Resolve device (= local player) world position. Prefer the local
        // player's controlled entity since that's consistent with how the
        // rest of the TDL UI treats "self" (self marker, GPS panel). Falls
        // back to the active device's owner entity if no controlled entity
        // is available (loading, dead, etc.).
        vector deviceWorld = vector.Zero;
        bool haveDevicePos = false;
        PlayerController pc = GetGame().GetPlayerController();
        if (pc)
        {
            IEntity controlled = pc.GetControlledEntity();
            if (controlled)
            {
                deviceWorld = controlled.GetOrigin();
                haveDevicePos = true;
            }
        }
        if (!haveDevicePos && m_ActiveDevice)
        {
            IEntity owner = m_ActiveDevice.GetOwner();
            if (owner)
            {
                deviceWorld = owner.GetOrigin();
                haveDevicePos = true;
            }
        }
        if (!haveDevicePos)
        {
            // Without a self position, distance/azimuth + the line all break
            // down. Bail without rendering rather than show garbage.
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            mapView.SetBloodhound(false, vector.Zero, vector.Zero);
            return;
        }

        // Snap cursor to terrain elevation so distance/azimuth are sampled
        // against the terrain surface (cursor came in at Y=0 from
        // ScreenToWorld / GetCenter, both of which return zero-Y points).
        float cursorElev = 0;
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (world)
            cursorElev = world.GetSurfaceY(cursorWorld[0], cursorWorld[2]);
        cursorWorld[1] = cursorElev;

        // Distance + bearing in the horizontal plane.
        float dx = cursorWorld[0] - deviceWorld[0];
        float dz = cursorWorld[2] - deviceWorld[2];
        float distance = Math.Sqrt(dx * dx + dz * dz);
        // Military bearing — clockwise from world-Z (north), 0..360.
        // Math.Atan2(y, x) returns atan2 in radians; passing (dx, dz) gives
        // the angle measured clockwise from +Z which is exactly what we want.
        float bearingRad = Math.Atan2(dx, dz);
        float bearingDeg = bearingRad * Math.RAD2DEG;
        if (bearingDeg < 0)
            bearingDeg = bearingDeg + 360.0;

        // Update text widgets. Switched to SetTextFormat (the pattern used by
        // the existing GPS panel — m_wHeading.SetTextFormat("%1°M", ...)) so
        // the formatting path matches a known-working call exactly, and so we
        // sidestep any string.Format quirks around the substitution being
        // assigned through a local before SetText.
        //
        // One-shot diagnostic Print on first frame after enable — set
        // m_bBloodhoundFirstTickPrinted back to false from anywhere if you
        // need another sample. Tells us whether the widget refs are non-null
        // and what the math is actually producing so we don't have to guess.
        if (!m_bBloodhoundFirstTickPrinted)
        {
            Print(string.Format("[Bloodhound] dist=%1 brg=%2 distRef=%3 azRef=%4",
                distance, bearingDeg,
                m_wBloodhoundDist != null,
                m_wBloodhoundAz != null), LogLevel.NORMAL);
            m_bBloodhoundFirstTickPrinted = true;
        }

        if (m_wBloodhoundGrid)
            m_wBloodhoundGrid.SetText(AG0_MGRSGridUtils.GetFullMGRS(cursorWorld, 4));
        if (m_wBloodhoundElev)
            m_wBloodhoundElev.SetTextFormat("ELEV %1 m", Math.Round(cursorElev).ToString());

        // Distance readout — whole metres below 1km, one-decimal km above.
        // The km branch uses int math (deciKm / 10 + deciKm % 10) instead of
        // a float divide + ToString — Enfusion's float.ToString prints full
        // precision ("1.20000"), which reads like measurement noise in a
        // tactical readout. We do the rounding at 100m granularity so the
        // last digit is meaningful (1.2 km = 1200m, not 1.2345 km).
        if (m_wBloodhoundDist)
        {
            if (distance < 1000.0)
            {
                m_wBloodhoundDist.SetTextFormat("DIST %1 m", Math.Round(distance).ToString());
            }
            else
            {
                int deciKm = Math.Round(distance / 100.0); // tenths-of-a-km
                int whole  = deciKm / 10;
                int tenths = deciKm % 10;
                m_wBloodhoundDist.SetTextFormat("DIST %1.%2 km", whole.ToString(), tenths.ToString());
            }
        }

        // Bearing — clamp NaN (Atan2(0,0) is implementation-defined) so
        // SetTextFormat never sees a NaN-derived ToString() result. With the
        // cursor sitting exactly on the device (menu's default when tracking
        // is on), dx==dz==0 and bearing is undefined; show "---" in that case
        // rather than whatever the engine returns for NaN.
        if (m_wBloodhoundAz)
        {
            if (distance < 0.5)
                m_wBloodhoundAz.SetText("BRG --- deg");
            else
                m_wBloodhoundAz.SetTextFormat("BRG %1 deg", Math.Round(bearingDeg).ToString());
        }

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
        mapView.SetBloodhound(true, cursorWorld, deviceWorld);
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
    // SHARED FRONTEND BEHAVIOUR — empty stubs
    //
    // Phase 1 of the world-space parity refactor: these are the public
    // surface the frontends will call into. Bodies arrive in later phases —
    // for now they're inert so callers can compile against the new API
    // without behavior change.
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

        // Bloodhound — cursor info + range/bearing line. No-op (with cheap
        // hide-readout fall-through) when disabled. Runs after the map view
        // has been Update()'d by the frontend's display-controller tick so
        // GetCenter() / WorldToLayout reflect this frame's view state.
        UpdateBloodhound();

        // Marker tool placement actions (gamepad A / keyboard Enter / X / R).
        // Polled here because the menu's focus chain consumes the actions
        // before InputManager listeners fire — but GetActionTriggered still
        // works in this context.
        if (m_eActivePanel == ETDLPanelContent.MARKER_TOOL && m_MarkerToolPanel && im)
            m_MarkerToolPanel.TickPlaceActionPoll(im);

        // Re-attach member card handlers when the display controller rebuilds
        // its card list (~1Hz membership refresh). Cheap when no rebuild has
        // happened — just a count comparison.
        if (m_DisplayController)
        {
            array<Widget> cards = m_DisplayController.GetMemberCards();
            if (cards && cards.Count() != m_iLastCardCount)
            {
                AttachCardHandlers();
                m_iLastCardCount = cards.Count();
            }
        }
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

            // Per-button relay captures the plugin + menu root and exposes a
            // zero-arg OnClick() that dispatches OnToolActivated. Held in
            // m_aPluginClickRelays so it lives as long as the button does.
            SCR_ModularButtonComponent btnComp = SCR_ModularButtonComponent.FindComponent(btnRoot);
            if (btnComp)
            {
                AG0_PluginButtonClickRelay relay = new AG0_PluginButtonClickRelay(plugin, m_wRoot);
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

        m_MarkerToolPanel.PlaceCurrentMarker(mapView.GetCenter(), isLocal);
    }

    //! Mouse click on the dedicated panel-level Place button. Always a public
    //! placement at canvas centre — mirrors the gamepad TDLPlaceMarker
    //! action's behaviour.
    void OnMarkerToolPlaceButtonClicked()
    {
        OnMarkerToolPlaceRequested(false);
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

        m_wMarkerCrosshair.SetVisible(m_eActivePanel == ETDLPanelContent.MARKER_TOOL);
    }

    //! Fired by AG0_TDLMapCanvasDragHandler when the user left-clicks
    //! MapDragSurface without significant drag (KBM placement path).
    //! Converts the absolute workspace mouse coords into canvas-local pixels,
    //! runs them through AG0_TDLMapView.ScreenToWorld, and places the marker
    //! there. Only active while the marker tool panel is visible.
    void OnMapClickedForMarkerPlacement(int absMouseX, int absMouseY)
    {
        if (m_eActivePanel != ETDLPanelContent.MARKER_TOOL)
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

        m_MarkerToolPanel.PlaceCurrentMarker(worldPos, false);
    }
}
