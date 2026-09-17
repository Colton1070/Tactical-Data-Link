// AG0_TDLMapPeripheral.c
// Always-on HUD mirror of the ATAK map, docked bottom-centre. Synchronised to whatever the
// operator's real ATAK surface is doing, and never a source of that state.
//
// This is a second AG0_TDLDisplayController and nothing else — no AG0_TDLMenuController, so
// no plugins, chat subscriptions, radial menu or input contexts. Interactivity in TDL is
// opt-in per frontend: a widget tree nobody calls Tick / SetZoneInputActive / DriveXFromInput
// on is inert by construction, which is why there is no "read-only" switch to throw here.

class AG0_TDLMapPeripheral
{
    //! Session-only, matching s_fBrightness and s_bBloodhoundEnabled — $profile is PC-only,
    //! so there is nowhere to persist this across a restart yet.
    static protected bool s_bEnabled;

    //! Peripheral vision does not resolve detail. ~18 Hz reads the same as 60 at a glance and
    //! costs a quarter of the per-frame command rebuild (satellite, overlays, roads,
    //! structures, grid, shapes, markers) that a second full map would otherwise add on top
    //! of the operator's own. Raise it if the map visibly steps at vehicle speed.
    protected const float REDRAW_INTERVAL = 0.055;

    //! HasATAKDevice walks the gadget manager, inventory storage and the equipped loadout on
    //! every call — the player controller keeps a 1 Hz cache precisely because that is too
    //! heavy per frame. Sampled on its own timer rather than off GetHeldDevicesCached: the
    //! cache can be a full second stale, and this gate decides whether a widget tree exists.
    protected const float DEVICE_CHECK_INTERVAL = 0.5;

    protected const ResourceName PERIPHERAL_LAYOUT = "{4DE4394061BF31B4}UI/layouts/Menus/TDL/TDLMapPeripheral.layout";

    protected Widget m_wRoot;
    protected ref AG0_TDLDisplayController m_DisplayController;
    protected float m_fRedrawAccum;
    protected float m_fDeviceCheckAccum;
    protected bool m_bHasDevice;

    //! Latched so a layout that will not load logs once per enable rather than once per frame.
    protected bool m_bBuildFailed;

    //------------------------------------------------------------------------------------------------
    // TOGGLE — written by the keybind handler and the ATAK settings button
    //------------------------------------------------------------------------------------------------

    static bool GetEnabled()
    {
        return s_bEnabled;
    }

    static void SetEnabled(bool enabled)
    {
        s_bEnabled = enabled;
    }

    static void Toggle()
    {
        s_bEnabled = !s_bEnabled;
    }

    //------------------------------------------------------------------------------------------------
    //! Driven from the local player controller's OnUpdate.
    void Update(float tDelta)
    {
        if (!s_bEnabled)
        {
            Cleanup();

            // Latch cleared on the disable transition only, never on the device gate below:
            // the device condition goes false on every holster, disarm, death and respawn, and
            // resetting there would re-run a doomed Build (and re-log it) on every re-equip.
            // Clearing here rather than in SetEnabled because the toggle is static and has no
            // instance to reach.
            m_bBuildFailed = false;
            ForceDeviceRecheck();
            return;
        }

        // Every frame and deliberately outside the throttle: m_wRoot is a raw handle into the
        // HUD manager's tree, which is rebuilt on death and respawn. Anything slower leaves a
        // window where SetVisible runs against a freed root.
        if (!HasLivePlayer())
        {
            Cleanup();
            ForceDeviceRecheck();
            return;
        }

        if (!HasDevice(tDelta))
        {
            Cleanup();
            return;
        }

        if (!m_DisplayController)
        {
            if (m_bBuildFailed)
                return;

            if (!Build())
            {
                m_bBuildFailed = true;
                return;
            }
        }

        bool draw = ShouldDraw();
        m_wRoot.SetVisible(draw);

        // The tree is kept alive across a hidden spell rather than torn down: rebuilding it
        // means AG0_TDLMapView.Init reloading the satellite raster, and the common hidden
        // case is the operator opening and closing their own ATAK menu.
        if (!draw)
            return;

        m_fRedrawAccum += tDelta;
        if (m_fRedrawAccum < REDRAW_INTERVAL)
            return;

        // Accumulated delta, not tDelta: the display controller runs its own 0.5s periodic
        // block (network status, member cards) off whatever it is handed, and feeding it a
        // single frame's worth would stretch that interval by the throttle ratio.
        m_DisplayController.Update(m_fRedrawAccum);
        m_fRedrawAccum = 0;
    }

    //------------------------------------------------------------------------------------------------
    void Cleanup()
    {
        // Before the tree goes. The 3D view may be hosting its pane inside it — the peripheral
        // inherits the pane whenever no real surface is alive to hold it, which is most of the
        // time it is on screen.
        if (m_wRoot)
            AG0_TDLMap3DView.ReleaseHostForSurface(m_wRoot);

        if (m_DisplayController)
        {
            m_DisplayController.Cleanup();
            m_DisplayController = null;
        }

        if (m_wRoot)
        {
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
        }

        m_fRedrawAccum = 0;
    }

    //------------------------------------------------------------------------------------------------
    // GATING
    //------------------------------------------------------------------------------------------------

    protected bool HasLivePlayer()
    {
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return false;

        return pc.GetControlledEntity() != null;
    }

    //! Cached between samples so the inventory walk runs twice a second rather than every
    //! frame. Half a second of lag on "the operator stowed their EUD" is invisible; the same
    //! lag on the death path is not, which is why HasLivePlayer stays unthrottled.
    protected bool HasDevice(float tDelta)
    {
        m_fDeviceCheckAccum += tDelta;
        if (m_fDeviceCheckAccum < DEVICE_CHECK_INTERVAL)
            return m_bHasDevice;

        m_fDeviceCheckAccum = 0;

        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
        {
            m_bHasDevice = false;
            return false;
        }

        m_bHasDevice = pc.HasATAKDevice();
        return m_bHasDevice;
    }

    //! Arms the accumulator past its interval so the next eligible frame samples immediately —
    //! a player who just respawned with a device shouldn't wait out a stale cached false.
    protected void ForceDeviceRecheck()
    {
        m_fDeviceCheckAccum = DEVICE_CHECK_INTERVAL;
        m_bHasDevice = false;
    }

    //! Hide-only conditions — transient, and cheap to re-enter.
    protected bool ShouldDraw()
    {
        // One check covers both "the operator's own ATAK is up" (mirroring a map they are
        // already looking at, at double the cost) and "some unrelated menu is up".
        MenuManager menuMgr = GetGame().GetMenuManager();
        if (menuMgr && menuMgr.IsAnyMenuOpen())
            return false;

        SCR_HUDManagerComponent hud = SCR_HUDManagerComponent.GetHUDManager();
        if (hud && !hud.IsVisible())
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    // BUILD
    //------------------------------------------------------------------------------------------------

    protected bool Build()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
            return false;

        // Parented under the HUD root rather than the raw workspace so the peripheral sits in
        // the same z-order band as every other HUD element instead of over all of them.
        // Falls back to the workspace root: the manager is absent in some world states, and a
        // peripheral in the wrong band still beats no peripheral.
        Widget hudRoot;
        SCR_HUDManagerComponent hud = SCR_HUDManagerComponent.GetHUDManager();
        if (hud)
            hudRoot = hud.GetHUDRootWidget();

        if (hudRoot)
            m_wRoot = workspace.CreateWidgets(PERIPHERAL_LAYOUT, hudRoot);
        else
            m_wRoot = workspace.CreateWidgets(PERIPHERAL_LAYOUT);

        if (!m_wRoot)
        {
            Print("[TDL_PERIPHERAL] CreateWidgets returned null for " + PERIPHERAL_LAYOUT, LogLevel.ERROR);
            return false;
        }

        m_DisplayController = new AG0_TDLDisplayController();

        // Before Init, so the map view is born passive — a view that claimed s_ActiveView even
        // for its first frame could eat a 3D toggle pressed on that frame.
        m_DisplayController.SetFollower(true);

        if (!m_DisplayController.Init(m_wRoot))
        {
            Print("[TDL_PERIPHERAL] display controller Init failed", LogLevel.ERROR);
            Cleanup();
            return false;
        }

        // Init reports success even with no MapCanvas in the tree — it just skips building the
        // view. A peripheral with no map is an authoring error, not a degraded mode worth running.
        if (!m_DisplayController.GetMapView())
        {
            Print("[TDL_PERIPHERAL] layout is missing a CanvasWidget named MapCanvas", LogLevel.ERROR);
            Cleanup();
            return false;
        }

        // After Init, so it also covers the marker overlay and self marker the controller just
        // spawned. Recursive because the flag is per widget: the root alone still leaves every
        // child catching the cursor, which in world-space device focus mode means the
        // peripheral's canvas swallowing hovers and clicks aimed past it.
        DisableCursorRecursive(m_wRoot);

        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void DisableCursorRecursive(Widget root)
    {
        if (!root)
            return;

        root.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);

        Widget child = root.GetChildren();
        while (child)
        {
            DisableCursorRecursive(child);
            child = child.GetSibling();
        }
    }
}
