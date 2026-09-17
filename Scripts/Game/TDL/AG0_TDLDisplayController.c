// AG0_TDLDisplayController.c
// Shared controller for ATAK display - used by both fullscreen menu and world-space device
// Handles: map view, map markers, self info panel, network status, member cards
// State is STATIC so multiple instances (menu + device) stay synchronized

class AG0_TDLDisplayController
{
    // STATIC STATE - shared across all instances for synchronization
    static bool s_bPlayerTracking = true;
    static bool s_bTrackUp = true;
    static float s_fZoom = 0.15;
    static vector s_vCenter;
    static bool s_bHasState = false;
    
    // STATIC PANEL STATE - shared so menu and device show same panels
    static bool s_bSidePanelVisible = true;
    static bool s_bNetworkContentVisible = true;
    static bool s_bDetailContentVisible = false;
    static bool s_bSettingsContentVisible = false;
    static bool s_bMarkerToolContentVisible = false;
    static string s_sPanelTitle = "CONTACTS";
    
    // Instance widgets
    protected Widget m_wRoot;
    protected ref AG0_TDLMapView m_MapView;
    protected CanvasWidget m_wMapCanvas;

    //! A follower renders from the shared statics but is never a source of them.
    //! PropagateMapState only reaches frontends registered through AG0_TDLMenuController,
    //! so an instance without one never receives a zoom change — and its own divergence
    //! check would then push its stale value back over the operator's real map. Followers
    //! pull instead, and never write the persisted state on Cleanup.
    protected bool m_bFollower;
    
    // Map marker overlay
    protected Widget m_wMarkerOverlay;
    protected Widget m_wSelfMapMarker;
    protected ref map<RplId, Widget> m_mMemberMarkers = new map<RplId, Widget>();
    protected const float MARKER_SIZE = 64.0;

    //! Authored size of MarkerImage in TDLMenuSelfMarker.layout — mirrored here because the
    //! scale below is relative and the layout can't be read back before the widget lays out.
    protected const float SELF_MARKER_ICON_PX = 64.0;
    protected const float FOLLOWER_MARKER_SCALE = 0.333;

    // Vanilla map markers (PLACED_MILITARY + TDL PLACED_CUSTOM) — widget cache
    // keyed by marker.GetMarkerID(). Each widget is built from the marker's
    // own SCR_MapMarkerEntryConfig.GetMarkerLayout() so the ATAK presentation
    // matches the vanilla M map exactly (same imagesets, same widget components).
    // Source: SCR_MapMarkerManagerComponent's static + disabled lists, unioned —
    // the base game shuffles markers off m_aStaticMarkers when they leave the
    // M map's visible frame, so the union is what frees the ATAK from the
    // "open the map and zoom" workaround.
    //
    // Why ID-keyed instead of SCR_MapMarkerBase-keyed: when the server
    // auto-deletes a marker (e.g. SCR_MapMarkerSyncComponent's
    // m_iPlacedMarkerLimit kicks in and the oldest marker gets dropped
    // when a new placement pushes past 10), the broadcast back removes
    // the marker from both manager lists and frees the SCR_MapMarkerBase
    // object. A reference-keyed map would then have a dangling/null key,
    // and the prune-phase foreach silently misses it — the widget gets
    // orphaned: stuck on the canvas, no longer position-updated, no
    // longer cleaned up until the menu reopens. Keying by int ID avoids
    // that entirely; the manager's ID is stable over the marker's
    // lifetime and gone when the marker is gone.
    protected ref map<int, Widget> m_mVanillaMarkerWidgets = new map<int, Widget>();
    
    //! Bloodhound readout, driven only in follower mode. Non-followers leave these alone —
    //! their AG0_TDLMenuController owns widgets of these exact names in the same tree, and two
    //! writers to one readout is a race over whose cursor wins. The refs are resolved on every
    //! instance, so the single `if (m_bFollower)` around UpdateFollowerBloodhound is the only
    //! thing keeping them apart: any future write added outside that gate collides silently.
    protected Widget m_wBloodhoundReadout;
    protected TextWidget m_wBloodhoundGrid;
    protected TextWidget m_wBloodhoundElev;
    protected TextWidget m_wBloodhoundDist;
    protected TextWidget m_wBloodhoundAz;

    // Self marker info panel
    protected TextWidget m_wGPSStatus;
    protected TextWidget m_wCallsign;
    protected TextWidget m_wGrid;
    protected TextWidget m_wAltitude;
    protected TextWidget m_wHeading;
    protected TextWidget m_wSpeed;
    protected TextWidget m_wError;
    protected Widget m_wHeadingIndicator;
    
    // Network panel
    protected Widget m_wSidePanel;
    protected TextWidget m_wPanelTitle;
    protected Widget m_wNetworkContent;
    protected Widget m_wDetailContent;
    protected Widget m_wSettingsContent;
    protected Widget m_wMarkerToolContent;
    protected Widget m_wMemberList;
    protected TextWidget m_wDeviceName;
    protected TextWidget m_wNetworkStatus;
    
    // Member cards (display only - menu adds click handlers separately)
    protected ref array<Widget> m_aMemberCards = {};
    protected ref array<RplId> m_aCachedMemberIds = {};
    
    // Update timing
    protected float m_fUpdateTimer = 0;
    protected const float UPDATE_INTERVAL = 0.5;
    
    // Colors
    protected ref Color COLOR_CYAN = new Color(0.2, 0.8, 0.8, 1.0);
    //protected ref Color COLOR_RED = new Color(1.0, 0.2, 0.2, 1.0);
    
    // Layout paths
    protected const ResourceName SELF_MARKER_LAYOUT = "{A242BD2B06D27E00}UI/layouts/Menus/TDL/TDLMenuSelfMarker.layout";
    protected const ResourceName MEMBER_MARKER_LAYOUT = "{23872C52B88FDB59}UI/layouts/Menus/TDL/TDLMenuBuddyMarker.layout";
    protected const ResourceName MEMBER_CARD_LAYOUT = "{7C025C99261C96C5}UI/layouts/Menus/TDL/TDLMemberCardUI.layout";
    
    //------------------------------------------------------------------------------------------------
    // PUBLIC API
    //------------------------------------------------------------------------------------------------
    
    bool Init(Widget root)
    {
        if (!root)
            return false;
        
        m_wRoot = root;
        
        // Map canvas
        m_wMapCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("MapCanvas"));
        
        // Self marker info panel
        m_wGPSStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("GPSStatus"));
        m_wCallsign = TextWidget.Cast(m_wRoot.FindAnyWidget("Callsign"));
        m_wGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("Grid"));
        m_wAltitude = TextWidget.Cast(m_wRoot.FindAnyWidget("Altitude"));
        m_wHeading = TextWidget.Cast(m_wRoot.FindAnyWidget("Heading"));
        m_wSpeed = TextWidget.Cast(m_wRoot.FindAnyWidget("Speed"));
        m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("Error"));
        m_wHeadingIndicator = m_wRoot.FindAnyWidget("HeadingIndicator");

        // Resolved unconditionally rather than behind m_bFollower so the refs survive
        // SetFollower being called either side of Init. UpdateFollowerBloodhound is the only
        // thing that writes them, and it is follower-gated.
        m_wBloodhoundReadout = m_wRoot.FindAnyWidget("BloodhoundReadout");
        m_wBloodhoundGrid = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundGrid"));
        m_wBloodhoundElev = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundElev"));
        m_wBloodhoundDist = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundDist"));
        m_wBloodhoundAz = TextWidget.Cast(m_wRoot.FindAnyWidget("BloodhoundAz"));

        // Tinted here as well as on AG0_TDLMenuController because neither layout authors a
        // colour on these — a mirror would otherwise draw a lime line and ticks under white
        // text while the menu showed both in lime.
        if (m_bFollower)
        {
            Color bloodhoundLime = Color.FromRGBA(192, 255, 77, 255);
            if (m_wBloodhoundGrid) m_wBloodhoundGrid.SetColor(bloodhoundLime);
            if (m_wBloodhoundElev) m_wBloodhoundElev.SetColor(bloodhoundLime);
            if (m_wBloodhoundDist) m_wBloodhoundDist.SetColor(bloodhoundLime);
            if (m_wBloodhoundAz)   m_wBloodhoundAz.SetColor(bloodhoundLime);
        }
        
        // Network panel
        m_wSidePanel = m_wRoot.FindAnyWidget("SidePanel");
        m_wPanelTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("PanelTitle"));
        m_wNetworkContent = m_wRoot.FindAnyWidget("NetworkContent");
        m_wDetailContent = m_wRoot.FindAnyWidget("DetailContent");
        m_wSettingsContent = m_wRoot.FindAnyWidget("SettingsContent");
        m_wMarkerToolContent = m_wRoot.FindAnyWidget("MarkerToolContent");
        m_wMemberList = m_wRoot.FindAnyWidget("MemberList");
        m_wDeviceName = TextWidget.Cast(m_wRoot.FindAnyWidget("DeviceName"));
        m_wNetworkStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("NetworkStatus"));
        
        // Apply colors
        ApplySelfPanelColors();
        
        // Initialize map view
        if (m_wMapCanvas)
        {
            m_MapView = new AG0_TDLMapView();
            if (!m_MapView.Init(m_wMapCanvas))
            {
                Print("[TDLDisplayController] Failed to init map view", LogLevel.WARNING);
                return false;
            }
            
            // Set here rather than left to SetFollower so the flag survives being declared
            // before Init — the view it forwards to does not exist until this point.
            m_MapView.SetPassive(m_bFollower);

            // Restore from static state or initialize
            if (s_bHasState)
            {
                // Follower goes through the raw write for the same reason UpdateMapView does:
                // SetZoom forwards to the 3D view as a relative step whenever the pane is open,
                // so a peripheral built while the operator is orbiting would jolt their camera
                // distance once per enable. SetCenter is a plain assignment and is safe.
                if (m_bFollower)
                    m_MapView.ApplyMirrorZoom(s_fZoom);
                else
                    m_MapView.SetZoom(s_fZoom);

                m_MapView.SetCenter(s_vCenter);
            }
            // A follower is skipped: it is the frontend most likely to Init first — always-on,
            // gated only on carrying a device, where the menu and the device screen both wait
            // on the operator — so letting it seed would make the mirror the source of the
            // state it exists to follow. It renders from the defaults until a real surface
            // seeds, which is what the pull in UpdateMapView already does.
            else if (!m_bFollower)
            {
                m_MapView.CenterOnPlayer();
                m_MapView.SetZoom(0.15);

                // Seeded only when CenterOnPlayer had an entity to centre on. It returns
                // without writing when GetControlledEntity is null, and flagging s_bHasState
                // against the zero centre that leaves behind is exactly the poisoned state a
                // later Init would restore from.
                IEntity controlled;
                PlayerController playerController = GetGame().GetPlayerController();
                if (playerController)
                    controlled = playerController.GetControlledEntity();

                if (controlled)
                {
                    s_vCenter = m_MapView.GetCenter();
                    s_fZoom = 0.15;
                    s_bHasState = true;
                }
            }
        }
        
        // Marker overlay
        m_wMarkerOverlay = m_wRoot.FindAnyWidget("MarkerOverlay");
        if (m_wMarkerOverlay)
        {
            DisableCursorRecursive(m_wMarkerOverlay);
            m_wSelfMapMarker = GetGame().GetWorkspace().CreateWidgets(SELF_MARKER_LAYOUT, m_wMarkerOverlay);
            DisableCursorRecursive(m_wSelfMapMarker);
            ScaleSelfMarkerForFollower();
        }
        
        // Hidden until the first tick resolves a solution, matching how the menu's own readout
        // comes up — the layout ships it visible so a mirror would otherwise flash an empty
        // frame at its authored position on the frame it is built.
        if (m_bFollower && m_wBloodhoundReadout)
            m_wBloodhoundReadout.SetVisible(false);

        // Show contacts panel by default - use static state
        ApplyPanelState();
        
        Print("[TDLDisplayController] Init complete", LogLevel.DEBUG);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    // RENDER CADENCE
    //
    // Set by a frontend that knows whether anyone can actually see its surface — today that
    // is the world-space device, which drives this from a camera visibility test. The
    // fullscreen menu and the peripheral leave it at the default and are unaffected.
    //
    // What is gated is the DRAW, not the STATE. The pose sync and the shape/terrain handoff
    // keep running at full rate on a suspended frontend, because if a suspended view stopped
    // tracking the shared pose it would drift from s_vCenter — and the divergence check in
    // UpdateMapView would then PROPAGATE that stale local pose on wake and clobber whichever
    // frontend the operator was actually using. Keeping state current means a suspended view
    // can never become the source of a bogus propagation.
    //
    // It also means a view that was parked and not moved wakes to a matching static stamp and
    // re-submits its cached command prefix instead of rebuilding it — which is what keeps the
    // gate from trading a steady frame cost for a hitch every time the operator glances down.
    protected float m_fRenderInterval;      //!< 0 = draw every tick. >0 = seconds between draws.
    protected bool  m_bRenderSuspended;     //!< true = do not draw at all
    protected float m_fSinceMapDraw;

    //! interval: seconds between map draws; 0 draws every tick.
    //! suspended: skip drawing entirely (out of view / nobody can see this surface).
    void SetRenderCadence(float interval, bool suspended)
    {
        // Resuming, or speeding up, must land on THIS tick rather than after the old (longer)
        // interval has elapsed — otherwise looking at a device that was gated off costs up to
        // a full interval of staleness before the first repaint.
        bool resuming = m_bRenderSuspended && !suspended;
        if (resuming || interval < m_fRenderInterval)
            m_fSinceMapDraw = interval;

        m_bRenderSuspended = suspended;
        m_fRenderInterval = interval;
    }

    //------------------------------------------------------------------------------------------------
    void Update(float tDelta)
    {
        UpdateMapView(tDelta);
        UpdateSelfMarker();
        
        // Apply panel state each frame so device syncs with menu
        ApplyPanelState();
        
        // Periodic updates
        m_fUpdateTimer += tDelta;
        if (m_fUpdateTimer >= UPDATE_INTERVAL)
        {
            m_fUpdateTimer = 0;
            UpdateNetworkStatus();
            RefreshMemberCards();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void Cleanup()
    {
        // Save state to static before cleanup. A follower is skipped: its view is a copy of
        // the statics, so writing them back on teardown would let a peripheral that happened
        // to be one frame stale become the persisted value.
        if (m_MapView && !m_bFollower)
        {
            s_fZoom = m_MapView.GetZoom();
            s_vCenter = m_MapView.GetCenter();
        }
        
        // Cleanup self marker
        if (m_wSelfMapMarker)
        {
            m_wSelfMapMarker.RemoveFromHierarchy();
            m_wSelfMapMarker = null;
        }
        
        // Cleanup member map markers
        foreach (RplId id, Widget marker : m_mMemberMarkers)
        {
            if (marker)
                marker.RemoveFromHierarchy();
        }
        m_mMemberMarkers.Clear();

        // Cleanup vanilla map marker widgets
        foreach (int id, Widget w : m_mVanillaMarkerWidgets)
        {
            if (w)
                w.RemoveFromHierarchy();
        }
        m_mVanillaMarkerWidgets.Clear();
        
        // Cleanup member cards
        foreach (Widget card : m_aMemberCards)
        {
            if (card)
                card.RemoveFromHierarchy();
        }
        m_aMemberCards.Clear();
        m_aCachedMemberIds.Clear();
        
        m_MapView = null;
    }
    
    //------------------------------------------------------------------------------------------------
    // STATIC STATE ACCESSORS - changes affect all instances
    //------------------------------------------------------------------------------------------------
    
    //! Passed down rather than sniffed: only the thing that constructed this controller knows
    //! which surface it is drawing.
    void SetWorldSpaceSurface(bool isWorldSpace)
    {
        if (m_MapView)
            m_MapView.SetWorldSpaceSurface(isWorldSpace);
    }

    //------------------------------------------------------------------------------------------------
    //! Safe either side of Init: before, the flag is picked up when the view is built;
    //! after, it is forwarded directly.
    void SetFollower(bool follower)
    {
        m_bFollower = follower;
        if (m_MapView)
            m_MapView.SetPassive(follower);
    }

    //------------------------------------------------------------------------------------------------
    AG0_TDLMapView GetMapView()
    {
        return m_MapView;
    }
    
    static void SetPlayerTracking(bool tracking)
    {
        // Log every transition so we can see WHO and WHEN someone flips tracking
        // back on mid-pan — if the web mirror snaps to player position after a
        // user-issued pan with tracking:false, this log fires for the offending
        // call. Same-value writes don't log (no transition, no noise).
        if (s_bPlayerTracking != tracking)
        {
            Print(string.Format("[TDL_MIRROR_TRACKSET] s_bPlayerTracking %1 -> %2",
                s_bPlayerTracking, tracking), LogLevel.DEBUG);
        }
        s_bPlayerTracking = tracking;
    }

    static bool GetPlayerTracking()
    {
        return s_bPlayerTracking;
    }

    static void SetTrackUp(bool trackUp)
    {
        s_bTrackUp = trackUp;
    }

    static bool GetTrackUp()
    {
        return s_bTrackUp;
    }

    //! Cross-class accessors for the persisted map view state. Used by the web-
    //! mirror command dispatcher to fall back to current values when a partial
    //! mirror_set_map_view payload omits a field — direct static access from
    //! outside the class isn't safe under Enfusion's default member visibility.
    static float GetSavedZoom() { return s_fZoom; }
    static vector GetSavedCenter() { return s_vCenter; }

    //! Fan-out helper: push a map view mutation to every live frontend's
    //! AG0_TDLMapView so world-space and fullscreen menu stay coherent. Both
    //! frontends carry their own AG0_TDLMapView instance; without explicit
    //! propagation, an input on one (in-game drag, web command, gamepad pan)
    //! leaves the sibling stale, the snapshot reflects whichever frontend the
    //! BuildMirrorSnapshot primary happens to be, and the user sees state
    //! flicker or stick depending on which surface they're looking at.
    //!
    //! `centerValid` / `zoomValid` let callers send partial updates — e.g. a
    //! zoom-only mutation passes `centerValid=false, zoomValid=true` and the
    //! center field is ignored on every frontend.
    static void PropagateMapState(vector center, float zoom, bool centerValid, bool zoomValid)
    {
        array<ref AG0_TDLMenuController> live = AG0_TDLMenuController.GetLiveControllers();
        int frontendsTotal = 0;
        int displayControllersFound = 0;
        int mapViewsFound = 0;
        int mapViewsUpdated = 0;
        foreach (AG0_TDLMenuController c : live)
        {
            if (!c)
                continue;
            frontendsTotal = frontendsTotal + 1;
            AG0_TDLDisplayController dc = c.GetDisplayController();
            if (!dc)
                continue;
            displayControllersFound = displayControllersFound + 1;
            AG0_TDLMapView mv = dc.GetMapView();
            if (!mv)
                continue;
            mapViewsFound = mapViewsFound + 1;
            if (centerValid)
                mv.SetCenter(center);
            if (zoomValid)
                mv.SetZoom(zoom);
            mapViewsUpdated = mapViewsUpdated + 1;
        }
        // Also write to the persisted statics so Cleanup/Init restore round-trips
        // through the same canonical values.
        if (centerValid)
            s_vCenter = center;
        if (zoomValid)
            s_fZoom = zoom;

        // Diagnostic — only fires when NO frontend received the update. A
        // partial fan-out (some frontend in the registry without a display
        // controller, common when multiple held devices spin up at different
        // times) is fine as long as at least one visible frontend got the
        // change. Total-zero is the real failure mode where the user wouldn't
        // see anything happen in-game.
        if (mapViewsUpdated == 0 && frontendsTotal > 0)
        {
            Print(string.Format("[TDL_MIRROR_PROP] TOTAL FAILURE no frontend updated center=%1 zoom=%2 frontends=%3 displayCtrls=%4 mapViews=%5",
                center, zoom, frontendsTotal, displayControllersFound, mapViewsFound),
                LogLevel.WARNING);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // STATIC PANEL STATE ACCESSORS
    //------------------------------------------------------------------------------------------------
    
    static void SetPanelState(bool sideVisible, bool networkVisible, bool detailVisible, bool settingsVisible, bool markerToolVisible, string title)
    {
        s_bSidePanelVisible = sideVisible;
        s_bNetworkContentVisible = networkVisible;
        s_bDetailContentVisible = detailVisible;
        s_bSettingsContentVisible = settingsVisible;
        s_bMarkerToolContentVisible = markerToolVisible;
        s_sPanelTitle = title;
    }

    static bool GetMarkerToolContentVisible()
    {
        return s_bMarkerToolContentVisible;
    }
    
    static bool GetSidePanelVisible()
    {
        return s_bSidePanelVisible;
    }
    
    //------------------------------------------------------------------------------------------------
    // Apply static panel state to this instance's widgets
    void ApplyPanelState()
    {
        if (m_wSidePanel)
            m_wSidePanel.SetVisible(s_bSidePanelVisible);

        if (m_wNetworkContent)
            m_wNetworkContent.SetVisible(s_bNetworkContentVisible);

        if (m_wDetailContent)
            m_wDetailContent.SetVisible(s_bDetailContentVisible);

        if (m_wSettingsContent)
            m_wSettingsContent.SetVisible(s_bSettingsContentVisible);

        if (m_wMarkerToolContent)
            m_wMarkerToolContent.SetVisible(s_bMarkerToolContentVisible);

        if (m_wPanelTitle)
            m_wPanelTitle.SetText(s_sPanelTitle);
    }
    
    //------------------------------------------------------------------------------------------------
    // Get member cards array so menu can add click handlers
    array<Widget> GetMemberCards()
    {
        return m_aMemberCards;
    }
    
    array<RplId> GetMemberCardIds()
    {
        return m_aCachedMemberIds;
    }
    
    //------------------------------------------------------------------------------------------------
    // PROTECTED IMPLEMENTATION
    //------------------------------------------------------------------------------------------------
    
    protected void ApplySelfPanelColors()
    {
        if (m_wCallsign) m_wCallsign.SetColor(COLOR_CYAN);
        if (m_wGrid) m_wGrid.SetColor(COLOR_CYAN);
        if (m_wAltitude) m_wAltitude.SetColor(COLOR_CYAN);
        if (m_wHeading) m_wHeading.SetColor(COLOR_CYAN);
        if (m_wSpeed) m_wSpeed.SetColor(COLOR_CYAN);
        if (m_wError) m_wError.SetColor(COLOR_CYAN);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMapView(float tDelta)
    {
        if (!m_MapView)
            return;
        
        IEntity player = GetGame().GetPlayerController().GetControlledEntity();
        if (!player)
            return;
        
        // Tracking-mode auto-center: re-pin the map view to the player every
        // frame the static flag is on. Matches the in-game ATAK behavior so
        // when the player moves, the held device's map follows.
        if (s_bPlayerTracking)
            m_MapView.CenterOnPlayer();
        
        if (s_bTrackUp)
        {
            vector angles = player.GetYawPitchRoll();
            m_MapView.SetTrackUp(angles[0]);
        }
        else
        {
            m_MapView.SetRotation(0);
        }

        // Divergence sync — propagate local m_MapView state to other live
        // frontends when it changed. SKIPPED when tracking is on: the
        // CenterOnPlayer call above intentionally diverges every frame as
        // the player moves, and both frontends do their own CenterOnPlayer
        // so they stay synced without us writing player position to s_vCenter
        // each frame (which would otherwise spam PropagateMapState 60×/sec
        // and clobber the "last explicit pan target" persisted in s_vCenter).
        // Zoom and tracking-off pan are the only sources of legitimate
        // divergence the propagator needs to handle.
        if (m_bFollower)
        {
            // Pull, never propagate. Tracking-on needs no centre write: the CenterOnPlayer
            // above already ran on this instance for the same reason it runs on every other.
            m_MapView.ApplyMirrorZoom(s_fZoom);
            if (!FollowMap3DFocus())
            {
                if (!s_bPlayerTracking)
                    m_MapView.SetCenter(s_vCenter);
            }
        }
        else if (!s_bPlayerTracking)
        {
            vector localCenter = m_MapView.GetCenter();
            float  localZoom   = m_MapView.GetZoom();
            bool centerDiff =
                Math.AbsFloat(localCenter[0] - s_vCenter[0]) > 0.1
             || Math.AbsFloat(localCenter[2] - s_vCenter[2]) > 0.1;
            bool zoomDiff = Math.AbsFloat(localZoom - s_fZoom) > 0.001;
            if (centerDiff || zoomDiff)
                PropagateMapState(localCenter, localZoom, centerDiff, zoomDiff);
        }
        else
        {
            // Tracking-on: zoom can still change via in-game zoom buttons
            // independently of the player following. Sync just the zoom.
            float localZoom = m_MapView.GetZoom();
            bool zoomDiff = Math.AbsFloat(localZoom - s_fZoom) > 0.001;
            if (zoomDiff)
                PropagateMapState(vector.Zero, localZoom, false, true);
        }

        // Update heading indicator
        ImageWidget headingImg = ImageWidget.Cast(m_wHeadingIndicator);
        if (headingImg)
            headingImg.SetRotation(m_MapView.GetRotation());
        
		// Feed shapes + terrain structures + terrain roads into map view from player controller
		SCR_PlayerController controller = SCR_PlayerController.Cast(
			GetGame().GetPlayerController()
		);
		if (controller)
		{
			AG0_TDLMapShapeManager shapeMgr = controller.GetTDLShapeManager();
			if (shapeMgr)
				m_MapView.SetShapes(shapeMgr.GetShapes());
			else
				m_MapView.SetShapes(null);

			// The payload hash is threaded through so the map view's static dirty gate can tell
			// a genuine dataset swap from the same array handed over again. Handle comparison
			// would never fire — the manager refills the same array object in place — and
			// GetVersion() is the wire format version, which is a constant.
			AG0_TDLTerrainStructureManager structMgr = controller.GetTDLTerrainStructureManager();
			if (structMgr)
				m_MapView.SetTerrainStructures(structMgr.GetStructures(), structMgr.GetLastSyncHash());
			else
				m_MapView.SetTerrainStructures(null);

			AG0_TDLTerrainRoadManager roadMgr = controller.GetTDLTerrainRoadManager();
			if (roadMgr)
				m_MapView.SetTerrainRoads(roadMgr.GetFeatures(), roadMgr.GetLastSyncHash());
			else
				m_MapView.SetTerrainRoads(null);
		}
		else
		{
			m_MapView.SetShapes(null);
			m_MapView.SetTerrainStructures(null);
			m_MapView.SetTerrainRoads(null);
		}

        // Render gate. Everything above this line is state and runs at full rate; everything
        // below is pixels. See SetRenderCadence.
        // Both non-painting exits below go through TickSuspended rather than a bare return.
        // The 3D pane's host arbitration doubles as this surface's liveness heartbeat and the
        // canvas measurement feeds ScreenToWorld/IsReady for callers that run every frame
        // regardless of paint cadence — dropping either to the paint cadence (or to zero) is
        // how a throttled or suspended surface silently loses the 3D pane to another one.
        if (m_bRenderSuspended)
        {
            m_MapView.TickSuspended(tDelta);
            return;
        }

        // Advanced only on live frames. If it ticked while suspended, a surface gated off for
        // longer than the keepalive interval would always come back with the keepalive already
        // expired and eat a full rebuild on the frame it becomes visible — exactly the hitch
        // the cached prefix exists to avoid.
        m_MapView.AdvanceStaticClock(tDelta);

        m_fSinceMapDraw += tDelta;
        if (m_fRenderInterval > 0 && m_fSinceMapDraw < m_fRenderInterval)
        {
            m_MapView.TickSuspended(tDelta);
            return;
        }
        m_fSinceMapDraw = 0;

        if (m_bFollower)
            UpdateFollowerBloodhound();

        // Draw map
        m_MapView.Draw();

        UpdateSelfMapMarker(player);
        UpdateMemberMapMarkers();

        // Inside the render gate: despite the name this only reads the base game's marker
        // list and populates OUR overlay widgets — it does not write to vanilla state, so
        // skipping it on an unwatched surface changes nothing outside this screen.
        UpdateVanillaMarkers(controller);

        // NOTE: Do NOT sync zoom/center to static state here.
        // Multiple instances (menu + world-space device) run UpdateMapView every frame,
        // each with their own independent zoom. Syncing here causes the world-space device
        // to overwrite menu zoom changes continuously. State is saved correctly in Cleanup().
    }

    //------------------------------------------------------------------------------------------------
    //! The arrow is authored at 64 px against a full-screen map, where it reads as a small
    //! cursor. On the peripheral that same 64 px is a large fraction of the container and the
    //! arrow swamps the terrain under it.
    //!
    //! Applied to the ImageWidget rather than the marker root: the root's slot is SizeToContent,
    //! so a size written there is recomputed from the children and thrown away.
    protected void ScaleSelfMarkerForFollower()
    {
        if (!m_bFollower || !m_wSelfMapMarker)
            return;

        ImageWidget icon = ImageWidget.Cast(m_wSelfMapMarker.FindAnyWidget("MarkerImage"));
        if (!icon)
            return;

        float scaled = SELF_MARKER_ICON_PX * FOLLOWER_MARKER_SCALE;
        icon.SetSize(scaled, scaled);
    }

    //------------------------------------------------------------------------------------------------
    //! Follow the 3D pane's orbit focus while it is up. The hosting surface rewrites its own
    //! m_vCenterWorld from GetFocusWorld() every frame but deliberately does not propagate —
    //! a PropagateMapState per frame of an orbit drag would be its own problem. So a mirror
    //! reads the focus straight off the singleton: pull-only, no fan-out, and it keeps the
    //! peripheral looking at what the operator is orbiting instead of the stale 2D centre.
    //!
    //! Rotation comes across too, matching what GetRotation() reports for the hosting surface,
    //! so the peripheral and the compass needle agree. Zoom deliberately does not — orbit
    //! distance has no 2D equivalent, and the last 2D zoom is the right glance scale.
    //!
    //! Returns whether the focus was applied, so the caller knows to skip the 2D centre pull.
    protected bool FollowMap3DFocus()
    {
        // IsHostLive, not IsViewOpen: the latter stays true after the hosting frontend is torn
        // down (by design — a reopened menu rehosts the same diorama), and following a focus
        // nobody is updating would pin the mirror to a stale point with no way back.
        if (!AG0_TDLMap3DView.IsHostLive())
            return false;

        AG0_TDLMap3DView view3D = AG0_TDLMap3DView.GetInstance();
        if (!view3D)
            return false;

        vector focusWorld = view3D.GetFocusWorld();
        m_MapView.SetCenter(Vector(focusWorld[0], 0, focusWorld[2]));
        m_MapView.SetRotation(-view3D.GetCameraYaw());
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Reproduce the live frontend's bloodhound measurement. The endpoints are world-space, so
    //! this surface projects them at its own centre/zoom/rotation and lands on the same ground;
    //! the readout strings are taken verbatim rather than recomputed so the two can't disagree
    //! about rounding, and so the terrain sample behind the elevation happens once per frame
    //! rather than once per surface.
    protected void UpdateFollowerBloodhound()
    {
        AG0_TDLBloodhoundSolution sol = AG0_TDLMenuController.GetBloodhoundSolutionForMirror();
        if (!sol)
        {
            m_MapView.SetBloodhound(false, vector.Zero, vector.Zero);
            if (m_wBloodhoundReadout)
                m_wBloodhoundReadout.SetVisible(false);
            return;
        }

        if (m_wBloodhoundGrid)
            m_wBloodhoundGrid.SetText(sol.m_sGrid);
        if (m_wBloodhoundElev)
            m_wBloodhoundElev.SetText(sol.m_sElev);
        if (m_wBloodhoundDist)
            m_wBloodhoundDist.SetText(sol.m_sDist);
        if (m_wBloodhoundAz)
            m_wBloodhoundAz.SetText(sol.m_sAz);

        if (m_wBloodhoundReadout)
        {
            // IsReady gates on a measured canvas. This runs before Draw(), which is what
            // refreshes the canvas size, so on the first tick after a build the width is still
            // zero — WorldToLayout then returns (0,0) and the readout flashes at the frame's
            // top-left for a whole throttle interval.
            if (!m_MapView.IsReady())
            {
                m_wBloodhoundReadout.SetVisible(false);
            }
            else
            {
                float layoutX, layoutY;
                m_MapView.WorldToLayout(sol.m_vCursor, layoutX, layoutY);

                // Bounds-checked the way member and vanilla markers are. The line itself is
                // clipped in canvas space, but the readout is a widget in the parent frame and
                // nothing stops it: on a short HUD canvas a cursor a few hundred metres off
                // centre puts it hundreds of pixels outside the peripheral, floating over the
                // player's view. The menu gets away without this only because its canvas is
                // full-screen.
                float canvasW, canvasH;
                m_wMapCanvas.GetScreenSize(canvasW, canvasH);

                WorkspaceWidget workspace = GetGame().GetWorkspace();
                float layoutCanvasW = workspace.DPIUnscale(canvasW);
                float layoutCanvasH = workspace.DPIUnscale(canvasH);

                bool inBounds = (layoutX >= 0 && layoutX <= layoutCanvasW
                              && layoutY >= 0 && layoutY <= layoutCanvasH);

                if (inBounds)
                {
                    FrameSlot.SetPos(m_wBloodhoundReadout, layoutX, layoutY);
                    m_wBloodhoundReadout.SetVisible(true);
                }
                else
                {
                    m_wBloodhoundReadout.SetVisible(false);
                }
            }
        }

        m_MapView.SetBloodhound(true, sol.m_vCursor, sol.m_vDevice);
    }

    //------------------------------------------------------------------------------------------------
    protected void UpdateSelfMarker()
    {
        IEntity player = GetGame().GetPlayerController().GetControlledEntity();
        if (!player)
            return;
        
        vector pos = player.GetOrigin();
        vector angles = player.GetYawPitchRoll();
        
        // GPS Status
        if (m_wGPSStatus)
        {
            bool hasGPS = HasGPSProvider();
            if (hasGPS)
            {
                m_wGPSStatus.SetText("GPS: 3D FIX");
                m_wGPSStatus.SetColor(COLOR_CYAN);
            }
            else
            {
                m_wGPSStatus.SetText("NO GPS");
                m_wGPSStatus.SetColor(Color(1.0, 0.2, 0.2, 1.0));
            }
        }
        
        // Callsign
        if (m_wCallsign)
            m_wCallsign.SetText(GetPlayerCallsign());
        
        // Grid
        if (m_wGrid)
            m_wGrid.SetText(AG0_MGRSGridUtils.GetFullMGRS(pos, 5));
        
        // Altitude
        if (m_wAltitude)
            m_wAltitude.SetTextFormat("%1 MSL", Math.Round(pos[1]).ToString());
        
        // Heading
        if (m_wHeading)
        {
            float hdg = angles[0];
            if (hdg < 0) hdg += 360;
            m_wHeading.SetTextFormat("%1°M", Math.Round(hdg).ToString());
        }
        
        // Speed
        if (m_wSpeed)
        {
            Physics phys = player.GetPhysics();
            if (phys)
            {
                float speed = phys.GetVelocity().Length() * 2.237;
                m_wSpeed.SetTextFormat("%1 MPH", Math.Round(speed).ToString());
            }
            else
            {
                m_wSpeed.SetText("-- MPH");
            }
        }
        
        // Error
        if (m_wError)
            m_wError.SetText("+/- 5m");
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateNetworkStatus()
    {
        AG0_TDLNetworkMembers memberData = GetNetworkMembersFromController();
        
        if (m_wDeviceName)
            m_wDeviceName.SetText(GetPlayerCallsign());
        
        if (m_wNetworkStatus)
        {
            if (memberData && memberData.Count() > 0)
                m_wNetworkStatus.SetTextFormat("CONNECTED (%1 nodes)", memberData.Count());
            else
                m_wNetworkStatus.SetText("NO NETWORK");
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateSelfMapMarker(IEntity player)
    {
        if (!m_wSelfMapMarker || !m_MapView || !m_wMarkerOverlay)
            return;
        
        vector playerPos = player.GetOrigin();
        float playerHeading = player.GetYawPitchRoll()[0];
        
        float layoutX, layoutY;
        m_MapView.WorldToLayout(playerPos, layoutX, layoutY, true);
        
        FrameSlot.SetPos(m_wSelfMapMarker, layoutX, layoutY);
        
        ImageWidget markerImage = ImageWidget.Cast(m_wSelfMapMarker.FindAnyWidget("MarkerImage"));
        if (markerImage)
        {
            // Position-aware rather than heading + GetRotation(): on the 3D pane the
            // on-screen angle of a world heading varies with where the marker sits
            // under perspective. The 2D path returns the same sum as before.
            float markerRotation = m_MapView.GetMarkerScreenHeading(playerPos, playerHeading);
            markerImage.SetRotation(markerRotation);
        }
        
        m_wSelfMapMarker.SetVisible(true);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMemberMapMarkers()
    {
        if (!m_MapView || !m_wMarkerOverlay)
            return;
        
        set<RplId> ownDeviceIds = GetPlayerOwnDeviceIds();
        array<ref AG0_TDLNetworkMember> members = GetMembersArray();
        
        ref set<RplId> processedIds = new set<RplId>();
        
        // Get canvas bounds
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        float canvasW, canvasH;
        m_wMapCanvas.GetScreenSize(canvasW, canvasH);
        float layoutCanvasW = workspace.DPIUnscale(canvasW);
        float layoutCanvasH = workspace.DPIUnscale(canvasH);
        float margin = MARKER_SIZE;
        
        foreach (AG0_TDLNetworkMember member : members)
        {
            RplId memberId = member.GetRplId();
            
            if (ownDeviceIds.Contains(memberId))
                continue;
            
            vector memberPos = member.GetPosition();
            float layoutX, layoutY;
            m_MapView.WorldToLayout(memberPos, layoutX, layoutY, true);
            
            bool isVisible = (layoutX >= -margin && layoutX <= layoutCanvasW + margin &&
                              layoutY >= -margin && layoutY <= layoutCanvasH + margin);
            
            if (!isVisible)
            {
                if (m_mMemberMarkers.Contains(memberId))
                {
                    Widget marker = m_mMemberMarkers.Get(memberId);
                    if (marker)
                        marker.SetVisible(false);
                }
                continue;
            }
            
            processedIds.Insert(memberId);
            
            Widget marker;
            if (m_mMemberMarkers.Contains(memberId))
            {
                marker = m_mMemberMarkers.Get(memberId);
            }
            else
            {
                marker = CreateMemberMapMarker(member);
                if (!marker)
                    continue;
                m_mMemberMarkers.Set(memberId, marker);
            }
            
            FrameSlot.SetPos(marker, layoutX, layoutY);
            marker.SetVisible(true);
            
            TextWidget label = TextWidget.Cast(marker.FindAnyWidget("DeviceIdentifier"));
            if (label)
                label.SetText(member.GetPlayerName());
        }
        
        // Cleanup orphaned
        array<RplId> toRemove = {};
        foreach (RplId id, Widget w : m_mMemberMarkers)
        {
            if (!processedIds.Contains(id))
                toRemove.Insert(id);
        }
        foreach (RplId id : toRemove)
        {
            Widget marker = m_mMemberMarkers.Get(id);
            if (marker)
                marker.RemoveFromHierarchy();
            m_mMemberMarkers.Remove(id);
        }
    }
    

    //------------------------------------------------------------------------------------------------
    //! Make a marker widget invisible to the pointer, all the way down.
    //!
    //! Marker widgets are spawned from vanilla marker layouts, which are authored for the M
    //! map where a marker is a click target. On the ATAK it is a drawing: the map surface
    //! underneath owns every click, and the panel is where a marker is selected. Left
    //! cursor-eligible they sit on top of the map and swallow whatever lands on them —
    //! right-clicks never reach the wheel, and a drag that starts on a marker is not a pan.
    //! Which markers eat input depends on where they happen to be, so the failure looks
    //! intermittent rather than structural.
    //!
    //! Recursive because the flag is per widget and these layouts are frames of images and
    //! text, so flagging the root alone leaves every child still catching the cursor.
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

    //------------------------------------------------------------------------------------------------
    protected Widget CreateMemberMapMarker(AG0_TDLNetworkMember member)
    {
        Widget marker = GetGame().GetWorkspace().CreateWidgets(MEMBER_MARKER_LAYOUT, m_wMarkerOverlay);
        if (!marker)
            return null;

        DisableCursorRecursive(marker);

        TextWidget label = TextWidget.Cast(marker.FindAnyWidget("DeviceIdentifier"));
        if (label)
            label.SetText(member.GetPlayerName());

        return marker;
    }

    //------------------------------------------------------------------------------------------------
    //! Tick the vanilla-marker widget cache: reconcile against the live marker
    //! list, create widgets for new markers, remove widgets for gone markers,
    //! reposition existing ones, hide off-canvas. Mirrors UpdateMemberMapMarkers's
    //! create/cull/position pattern but the widgets come from each marker's own
    //! SCR_MapMarkerEntryConfig.GetMarkerLayout() so the ATAK shows them with
    //! identical visuals to the vanilla M map.
    //!
    //! Source: SCR_MapMarkerManagerComponent.GetStaticMarkers() unioned with
    //! GetDisabledMarkers(). The base manager moves markers between these lists
    //! based on the M map's visible-frame test (SetStaticMarkerDisabled in
    //! Update()), so GetStaticMarkers() alone is incomplete whenever the M map
    //! is closed or zoomed in. The union is what makes the ATAK independent of
    //! M-map state — see project memory: vanilla_marker_enum.
    //!
    //! Filter:
    //!  - Type ∈ {PLACED_MILITARY, PLACED_CUSTOM} — naturally excludes the
    //!    TDL device PLI marker (TDL_RADIO is dynamic, not in either list).
    //!  - marker.GetBlocked() drops UGC-blocked markers (mirrors vanilla).
    //!  - TDL custom markers gated by GetTDLConnectedPlayers() to mirror the
    //!    visibility rule from modded SCR_MapMarkerBase.OnUpdate. Self always
    //!    visible. Non-TDL custom markers (vanilla user pins) are not gated.
    protected void UpdateVanillaMarkers(SCR_PlayerController controller)
    {
        if (!m_MapView || !m_wMarkerOverlay || !m_wMapCanvas)
            return;

        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
            return;

        array<int> connectedPlayers;
        int selfPlayerId = -1;
        if (controller)
        {
            connectedPlayers = controller.GetTDLConnectedPlayers();
            selfPlayerId = controller.GetPlayerId();
        }

        // Union both lists into a single keep-set candidate list.
        //
        // The union is built in a local array rather than appended onto the manager's
        // return value: GetStaticMarkers() hands back the live m_aStaticMarkers reference,
        // so inserting into it permanently promotes every disabled marker into the base
        // game's own static list. This runs every frame, which made it unbounded growth of
        // a vanilla container plus a per-frame O(n) walk over a list that never shrank.
        array<SCR_MapMarkerBase> allMarkers = {};
        array<SCR_MapMarkerBase> statics = markerMgr.GetStaticMarkers();
        if (statics)
        {
            foreach (SCR_MapMarkerBase s : statics)
                allMarkers.Insert(s);
        }

        array<SCR_MapMarkerBase> disabled = markerMgr.GetDisabledMarkers();
        if (disabled)
        {
            foreach (SCR_MapMarkerBase d : disabled)
            {
                if (d && allMarkers.Find(d) == -1)
                    allMarkers.Insert(d);
            }
        }

        // Phase 1 — apply filters, build the markers we want to render this tick
        array<SCR_MapMarkerBase> keep = {};
        foreach (SCR_MapMarkerBase marker : allMarkers)
        {
            if (!marker || marker.GetBlocked())
                continue;

            int t = marker.GetType();
            if (t != SCR_EMapMarkerType.PLACED_MILITARY && t != SCR_EMapMarkerType.PLACED_CUSTOM)
                continue;

            // TDL connectivity gate (TDL custom markers only — vanilla user
            // pins on PLACED_CUSTOM pass straight through, matching the
            // modded SCR_MapMarkerBase.OnUpdate which only gates TDL markers).
            if (t == SCR_EMapMarkerType.PLACED_CUSTOM && marker.IsTDLMarker())
            {
                int ownerID = marker.GetMarkerOwnerID();
                if (ownerID > 0 && ownerID != selfPlayerId)
                {
                    if (!connectedPlayers || !connectedPlayers.Contains(ownerID))
                        continue;
                }
            }

            keep.Insert(marker);
        }

        // Build the keep-set as an ID set so Phase 2 can do the prune
        // check against int keys (the new map type) instead of marker
        // references that may have been freed under us.
        set<int> keepIds = new set<int>();
        foreach (SCR_MapMarkerBase m : keep)
        {
            if (m)
                keepIds.Insert(m.GetMarkerID());
        }

        // Phase 2 — prune widgets whose marker IDs are no longer in keep
        // (deleted, blocked, filtered out, owner disconnected, or — the
        // failure mode this refactor was made for — server-side auto-
        // dropped due to placement-limit overflow).
        array<int> idsToRemove = {};
        foreach (int id, Widget w : m_mVanillaMarkerWidgets)
        {
            if (!keepIds.Contains(id))
                idsToRemove.Insert(id);
        }
        foreach (int id : idsToRemove)
        {
            Widget w = m_mVanillaMarkerWidgets.Get(id);
            if (w)
                w.RemoveFromHierarchy();
            m_mVanillaMarkerWidgets.Remove(id);
        }

        // Off-canvas culling math (in canvas-local layout coords) — same
        // pattern as UpdateMemberMapMarkers
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        float canvasW, canvasH;
        m_wMapCanvas.GetScreenSize(canvasW, canvasH);
        float layoutCanvasW = workspace.DPIUnscale(canvasW);
        float layoutCanvasH = workspace.DPIUnscale(canvasH);
        float margin = MARKER_SIZE;

        // Phase 3 — create widgets for new markers, position + show/hide for all
        foreach (SCR_MapMarkerBase marker : keep)
        {
            int mId = marker.GetMarkerID();
            Widget icon;
            if (m_mVanillaMarkerWidgets.Contains(mId))
            {
                icon = m_mVanillaMarkerWidgets.Get(mId);
            }
            else
            {
                icon = CreateVanillaMarkerWidget(marker);
                if (!icon)
                    continue;
                m_mVanillaMarkerWidgets.Set(mId, icon);
            }

            int wp[2];
            marker.GetWorldPos(wp);
            float layoutX, layoutY;
            m_MapView.WorldToLayout(Vector(wp[0], 0, wp[1]), layoutX, layoutY);

            bool isVisible = (layoutX >= -margin && layoutX <= layoutCanvasW + margin &&
                              layoutY >= -margin && layoutY <= layoutCanvasH + margin);

            if (isVisible)
            {
                FrameSlot.SetPos(icon, layoutX, layoutY);
                icon.SetVisible(true);
            }
            else
            {
                icon.SetVisible(false);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Public hook for the marker-tool panel: drop the 3D widget for a
    //! marker that's just been deleted, without waiting for the next
    //! UpdateVanillaMarkers tick. The panel calls this immediately after
    //! firing AskRemoveStaticMarker so the on-map icon disappears the
    //! same frame the scroll card does — otherwise the manager still has
    //! the marker for a network roundtrip and UpdateVanillaMarkers's
    //! "is in keep" check keeps the widget alive until the broadcast back.
    //!
    //! Now an O(1) lookup since the widget map is keyed by marker ID.
    void DropVanillaMarkerWidgetById(int markerId)
    {
        if (!m_mVanillaMarkerWidgets.Contains(markerId))
            return;

        Widget w = m_mVanillaMarkerWidgets.Get(markerId);
        if (w)
            w.RemoveFromHierarchy();
        m_mVanillaMarkerWidgets.Remove(markerId);
    }

    //------------------------------------------------------------------------------------------------
    //! Build a vanilla marker widget from the marker's own SCR_MapMarkerEntryConfig.
    //! Pattern adapted from AG0_EnhancedNightVisionComponent.CreateMarkerTacticalWidget:
    //!  - PLACED_MILITARY: SetMilitarySymbolMode(true), decode configID into
    //!    faction + dimension via the military config's FACTION_DETERMINATOR /
    //!    DIMENSION_DETERMINATOR constants, build a SCR_MilitarySymbol, and
    //!    apply the faction colour.
    //!  - PLACED_CUSTOM: defer to entryConfig.InitClientSettings which picks
    //!    the icon from the imageset using marker.GetIconEntry() and applies
    //!    the user-selected color entry.
    //!
    //! Author chrome and event listening are disabled — the ATAK markers are
    //! display-only mirrors. Without SetEventListening(false) they would
    //! intercept clicks meant for MapDragSurface (the overlay sits z-above
    //! the canvas, same as the existing self/member marker layouts).
    //! Caller positions and sets visibility after creation.
    protected Widget CreateVanillaMarkerWidget(SCR_MapMarkerBase marker)
    {
        SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
        if (!markerMgr)
            return null;

        SCR_MapMarkerConfig config = markerMgr.GetMarkerConfig();
        if (!config)
            return null;

        SCR_MapMarkerEntryConfig entryConfig = config.GetMarkerEntryConfigByType(marker.GetType());
        if (!entryConfig)
            return null;

        Widget markerWidget = GetGame().GetWorkspace().CreateWidgets(entryConfig.GetMarkerLayout(), m_wMarkerOverlay);
        if (!markerWidget)
            return null;

        DisableCursorRecursive(markerWidget);

        SCR_MapMarkerWidgetComponent widgetComp = SCR_MapMarkerWidgetComponent.Cast(markerWidget.FindHandler(SCR_MapMarkerWidgetComponent));
        if (widgetComp)
        {
            widgetComp.SetMarkerObject(marker);

            int t = marker.GetType();

            if (t == SCR_EMapMarkerType.PLACED_MILITARY)
            {
                SCR_MapMarkerEntryMilitary militaryConfig = SCR_MapMarkerEntryMilitary.Cast(entryConfig);
                if (militaryConfig)
                {
                    widgetComp.SetMilitarySymbolMode(true);

                    int configID = marker.GetMarkerConfigID();
                    int factionID = configID % militaryConfig.FACTION_DETERMINATOR;
                    int dimensionID = configID * militaryConfig.DIMENSION_DETERMINATOR;

                    array<ref SCR_MarkerMilitaryFactionEntry> factionEntries = militaryConfig.GetMilitaryFactionEntries();
                    array<ref SCR_MarkerMilitaryDimension> dimensions = militaryConfig.GetMilitaryDimensions();

                    if (factionEntries.IsIndexValid(factionID) && dimensions.IsIndexValid(dimensionID))
                    {
                        SCR_MilitarySymbol milSymbol = new SCR_MilitarySymbol();
                        SCR_MarkerMilitaryFactionEntry factionEntry = factionEntries[factionID];

                        milSymbol.SetIdentity(factionEntry.GetFactionIdentity());
                        milSymbol.SetDimension(dimensions[dimensionID].GetDimension());
                        milSymbol.SetIcons(marker.GetFlags());

                        widgetComp.UpdateMilitarySymbol(milSymbol);
                        widgetComp.SetColor(factionEntry.GetColor());
                    }
                }
            }
            else if (t == SCR_EMapMarkerType.PLACED_CUSTOM)
            {
                // Picks the imageset + quad based on marker.GetIconEntry()
                // and applies the marker's color entry. True = skip profanity
                // filter; the manager has already filtered text upstream.
                entryConfig.InitClientSettings(marker, widgetComp, true);
            }

            widgetComp.SetRotation(marker.GetRotation());
            widgetComp.SetText(marker.GetCustomText(), true);

            // Display-only: SetEventListening(false) so the marker widget's
            // backing frame doesn't absorb mouse-down/drag/click events that
            // should pass through to MapDragSurface. Without this, the
            // markers' wide hit-rects swallowed pan-drags and the new
            // click-to-place handler in marker tool mode. Tradeoff: the
            // hover-text behaviour from SCR_MapMarkerWidgetComponent's
            // OnMouseEnter/OnMouseLeave is also disabled — when we want
            // hover preview back, the path is to make the marker hit-rect
            // smaller (just the icon, not the surrounding frame) rather
            // than re-enable event listening.
            widgetComp.SetAuthorVisible(false);
            widgetComp.SetEventListening(false);
        }

        markerWidget.SetVisible(false);  // Caller flips visibility after positioning
        return markerWidget;
    }
    
    //------------------------------------------------------------------------------------------------
    // MEMBER CARDS (sidebar list)
    //------------------------------------------------------------------------------------------------
    
    protected void RefreshMemberCards()
    {
        if (!m_wMemberList)
            return;
        
        array<ref AG0_TDLNetworkMember> members = GetMembersArray();
        
        // Check if rebuild needed
        bool needsRebuild = false;
        if (members.Count() != m_aCachedMemberIds.Count())
        {
            needsRebuild = true;
        }
        else
        {
            foreach (int i, AG0_TDLNetworkMember member : members)
            {
                if (i >= m_aCachedMemberIds.Count() || m_aCachedMemberIds[i] != member.GetRplId())
                {
                    needsRebuild = true;
                    break;
                }
            }
        }
        
        if (needsRebuild)
            RebuildMemberCards(members);
        else
            UpdateMemberCards(members);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void RebuildMemberCards(array<ref AG0_TDLNetworkMember> members)
    {
        // Clear existing
        foreach (Widget card : m_aMemberCards)
        {
            if (card)
                card.RemoveFromHierarchy();
        }
        m_aMemberCards.Clear();
        m_aCachedMemberIds.Clear();
        
        if (!m_wMemberList)
            return;
        
        // Create cards
        foreach (AG0_TDLNetworkMember member : members)
        {
            Widget card = GetGame().GetWorkspace().CreateWidgets(MEMBER_CARD_LAYOUT, m_wMemberList);
            if (!card)
                continue;
            
            UpdateCardWidgets(card, member);
            m_aMemberCards.Insert(card);
            m_aCachedMemberIds.Insert(member.GetRplId());
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateMemberCards(array<ref AG0_TDLNetworkMember> members)
    {
        foreach (int i, Widget card : m_aMemberCards)
        {
            if (i >= members.Count())
                break;
            UpdateCardWidgets(card, members[i]);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateCardWidgets(Widget card, AG0_TDLNetworkMember member)
    {
        if (!card || !member)
            return;
        
        TextWidget nameText = TextWidget.Cast(card.FindAnyWidget("PlayerName"));
        if (nameText)
            nameText.SetText(member.GetPlayerName());
        
        TextWidget ipText = TextWidget.Cast(card.FindAnyWidget("NetworkIP"));
        if (ipText)
            ipText.SetText("192.168.0." + member.GetNetworkIP().ToString());
        
        ImageWidget statusDot = ImageWidget.Cast(card.FindAnyWidget("StatusDot"));
        if (statusDot)
        {
            float signal = member.GetSignalStrength();
            if (signal >= 60)
                statusDot.SetColor(Color.FromRGBA(0, 200, 0, 255));
            else if (signal >= 30)
                statusDot.SetColor(Color.FromRGBA(200, 200, 0, 255));
            else
                statusDot.SetColor(Color.FromRGBA(200, 0, 0, 255));
        }
    }
    
    //------------------------------------------------------------------------------------------------
    // HELPERS
    //------------------------------------------------------------------------------------------------
    
    protected array<ref AG0_TDLNetworkMember> GetMembersArray()
    {
        array<ref AG0_TDLNetworkMember> members = {};
        AG0_TDLNetworkMembers membersData = GetNetworkMembersFromController();
        if (membersData)
        {
            map<RplId, ref AG0_TDLNetworkMember> membersMap = membersData.ToMap();
            foreach (RplId rplId, AG0_TDLNetworkMember member : membersMap)
            {
                members.Insert(member);
            }
        }
        return members;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool HasGPSProvider()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return false;
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device && device.HasCapability(AG0_ETDLDeviceCapability.GPS_PROVIDER))
                return true;
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected string GetPlayerCallsign()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return "UNKNOWN";
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            if (device && device.HasCapability(AG0_ETDLDeviceCapability.NETWORK_ACCESS))
                return device.GetDisplayName();
        }
        return "UNKNOWN";
    }
    
    //------------------------------------------------------------------------------------------------
    protected AG0_TDLNetworkMembers GetNetworkMembersFromController()
    {
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return null;
        return controller.GetAggregatedTDLMembers();
    }
    
    //------------------------------------------------------------------------------------------------
    protected set<RplId> GetPlayerOwnDeviceIds()
    {
        set<RplId> deviceIds = new set<RplId>();
        SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!controller)
            return deviceIds;
        
        array<AG0_TDLDeviceComponent> devices = controller.GetHeldDevicesCached();
        foreach (AG0_TDLDeviceComponent device : devices)
        {
            RplId deviceId = device.GetDeviceRplId();
            if (deviceId != RplId.Invalid())
                deviceIds.Insert(deviceId);
        }
        return deviceIds;
    }
}