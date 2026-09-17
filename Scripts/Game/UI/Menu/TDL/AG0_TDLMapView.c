//------------------------------------------------------------------------------------------------
// AG0_TDLMapView.c
// Canvas-based map rendering for TDL menu - bypasses MapWidget limitations
//------------------------------------------------------------------------------------------------

class AG0_TDLMapView
{
	protected static const ResourceName MAP_SATELLITE_CONFIG = "{140BDA7A9C54113A}Configs/TDL/TDL_MapSatelliteConfig.conf";
	
    // Canvas and rendering
    protected CanvasWidget m_wCanvas;
    protected ref SharedItemRef m_pMapTexture;
    protected ref array<ref CanvasWidgetCommand> m_aDrawCommands = {};
    
    // Map data from MapEntity
    protected float m_fMapSizeX;
    protected float m_fMapSizeY;
    protected float m_fMapOffsetX;
    protected float m_fMapOffsetY;
    protected bool m_bTextureLoaded;
	
	// Overlay textures - loaded from config, drawn in order on top of satellite
    protected ref array<ref SharedItemRef> m_aOverlayTextures = {};
    protected ref array<float> m_aOverlayOpacities = {};
    protected ref array<string> m_aOverlayNames = {};
    protected ref array<bool> m_aOverlayEnabled = {};
    protected bool m_bHasStructureOverlay;  // When true, skip API-streamed structure render (baked overlay covers it)
    
    // View state
    protected vector m_vCenterWorld;      // World position we're centered on
    protected float m_fZoom = 1.0;        // Zoom level (1.0 = full map visible)
    protected float m_fRotation = 0;      // Rotation in degrees (0 = north up)
    protected float m_fMinZoom = 0.1;     // Most zoomed in
    protected float m_fMaxZoom = 1.0;     // Full map view
    
    // Canvas dimensions (cached)
    protected float m_fCanvasWidth;
    protected float m_fCanvasHeight;

    //------------------------------------------------------------------------------------------------
    // STATIC / DYNAMIC COMMAND SPLIT
    //
    // Draw() used to Clear() and rebuild the whole command array every frame, so a map
    // nobody had touched still cost a full projection pass over every road, structure and
    // grid line — and that cost scales with zoom-out, which is why a map parked at low
    // zoom stayed expensive for the rest of the session.
    //
    // The existing draw order already separates cleanly:
    //   STATIC  (pose + dataset only): satellite/fallback, overlays, edge mask,
    //           roads, structures, grid
    //   DYNAMIC (every tick):          shapes, markers, bloodhound, scale bar, grid legend
    //
    // So the static block is a contiguous PREFIX of m_aDrawCommands. When nothing that
    // feeds it has changed we Resize() the array back down to that prefix — which keeps
    // the existing command objects in place, no reallocation and not one re-projected
    // vertex — and rebuild only the cheap tail.
    //
    // The prefix also survives a suspended frontend untouched, which is what makes the
    // world-space visibility gate cheap to wake from: a device that was gated off and is
    // still parked on the same pose re-submits its cached prefix instead of rebuilding it.
    protected int m_iStaticCommandCount = -1;   //!< -1 = no valid cached prefix, must rebuild

    // Stamp of everything the static prefix was built against. Compared each Draw();
    // any mismatch forces a rebuild. Committed only on a successful rebuild, so a
    // suspended view can never end up with a stamp that disagrees with its cached commands.
    protected bool   m_bStaticStampValid;
    protected vector m_vStaticStampCenter;
    protected float  m_fStaticStampZoom;
    protected float  m_fStaticStampRotation;
    protected float  m_fStaticStampCanvasW;
    protected float  m_fStaticStampCanvasH;
    protected string m_sStaticStampRoadHash       = "<unset>";
    protected int    m_iStaticStampRoadCount      = -2;
    protected string m_sStaticStampStructHash     = "<unset>";
    protected int    m_iStaticStampStructCount    = -2;
    protected int    m_iStaticStampOverlayMask    = -1;
    protected int    m_iStaticStampSatRevision    = -1;
    protected bool   m_bStaticStampTextureLoaded;

    //! Set by anything that changes static content without changing the pose (overlay
    //! toggled, satellite swapped, dataset pointer replaced). Forces the next Draw() to rebuild.
    protected bool m_bStaticDirty = true;

    protected float m_fSinceStaticRebuild;

    //! Time since the last SetDrawCommands, accumulated on non-painting frames and reset on
    //! every submit. Separate from m_fSinceStaticRebuild, which is deliberately frozen while
    //! suspended so a gated-off surface does not wake with its keepalive already expired.
    protected float m_fSinceSubmit;

    //! How long a canvas may go without a submit before the next painted frame is forced to
    //! RECONSTRUCT its commands rather than re-submit the cached ones. Matches the 30 s
    //! interval AG0_TDLMenuController already uses against the same engine behaviour (it
    //! reaps a canvas's CanvasWidgetCommand array after a few minutes of idle).
    //!
    //! Deliberately much longer than the keepalive: at 1 s, every glance away and back would
    //! pay a full rebuild, which is the hitch this whole mechanism exists to avoid. A throttled
    //! surface never reaches this at all — it submits several times a second — so only a
    //! genuinely parked surface (stowed, or behind the ATAK menu for half a minute) pays it.
    protected static const float STATIC_RESUBMIT_MAX_IDLE = 30.0;

    //! NOTE — there is deliberately NO rate cap on rebuilds during motion.
    //!
    //! Capping the static rebuild to e.g. 30 Hz while the pose moves looks like free money,
    //! but the dynamic tail is re-projected at the CURRENT pose every tick while the cached
    //! prefix still holds screen coordinates from the pose it was built at. Any frame where
    //! those two disagree, every shape and marker slides off the road and building it was
    //! drawn on, then snaps back when the prefix catches up. At 144 fps and a fast drag that
    //! is tens of pixels of separation — far more visible than the frames it would save.
    //!
    //! The win this whole mechanism exists for is the SETTLED case: a map parked at a zoom
    //! level nobody is changing. Motion is transient and operator-driven; leaving it at
    //! frame-rate keeps a pan pixel-identical to the pre-split behaviour.

    //! Settled keepalive. Nothing should change without bumping a version or the dirty flag,
    //! but a 1 Hz backstop means a missed invalidation self-heals within a second instead of
    //! leaving a permanently stale map.
    protected static const float STATIC_KEEPALIVE_INTERVAL = 1.0;

    //! Pose-change epsilons. Center is compared in SCREEN PIXELS rather than world units so
    //! the threshold means the same thing at every zoom — a quarter-pixel of movement cannot
    //! change a single rasterized command. Without an epsilon here any asymptotic follow
    //! (player tracking, mirror pull) leaves the pose "moving" by a nanometre forever and
    //! the gate never engages at all.
    protected static const float STATIC_POSE_EPSILON_PX  = 0.25;
    protected static const float STATIC_ZOOM_EPSILON     = 0.0001;
    protected static const float STATIC_ROTATION_EPSILON = 0.05;   // degrees

    //! True only between the start and end of the 2D draw pass. WorldToScreen consults it to
    //! skip its GetHostedMap3DView() probe: reaching the 2D block means the 3D pane is either
    //! closed or hosted on the OTHER surface, so the probe provably returns null — and it is
    //! not free, it walks IsViewOpen -> GetInstance -> m_wCanvas.GetParent() -> IsHostedBy
    //! per projected point, i.e. thousands of native calls per frame once 3D has been opened once.
    protected bool m_bIn2DDrawPass;

    //------------------------------------------------------------------------------------------------
    // ROAD LEVEL OF DETAIL, DECIMATION AND BUDGET
    //
    // Roads are the one pass with nothing bounding it. Stroke width is floored to a minimum
    // per priority class, so unlike a sub-pixel building a road is never dropped for being too
    // thin to read — at full zoom-out the pass emits geometry for the entire network. These
    // four constants are what bound it.

    //! Zoom above which trails (priority <= 1) stop drawing, then paved (priority 2).
    //! m_fZoom is 1.0 at whole-map and m_fMinZoom (0.1) at closest, so LARGER = further out.
    //!
    //! This is the one visible behaviour change in the pass: zoomed out past the first
    //! threshold the map shows a road network rather than every footpath. That is the
    //! standard tactical-map treatment and it is what makes a zoomed-out map legible as well
    //! as affordable — but it IS a presentation decision, so these are the dials to argue with.
    //!
    //! Deliberately set high. The persisted default is 0.15 and normal working zooms sit well
    //! below 0.5, so a routine session never sheds anything; paved roads only go at 0.8, which
    //! is close enough to whole-map that individual streets are a few pixels long anyway.
    //! Earlier values of 0.35/0.60 put "highways only" across the top third of the zoom range
    //! including the fully-zoomed-out view, which is too aggressive to be the default.
    protected static const float ROAD_LOD_SHED_TRAILS_ZOOM = 0.50;
    protected static const float ROAD_LOD_SHED_PAVED_ZOOM  = 0.80;

    //! Dead-band around each threshold. Without it, a pan that hovers on a boundary flickers
    //! whole road classes in and out frame to frame.
    protected static const float ROAD_LOD_HYSTERESIS = 0.03;

    //! 0 = draw everything, 1 = drop trails, 2 = highways only. Sticky across frames; only
    //! UpdateRoadLodBand writes it.
    protected int m_iRoadLodBand;

    //! Hard ceiling on the work the road pass may emit in one rebuild, counted in VERTICES
    //! (plus one unit per join polygon) rather than in commands. Commands are the wrong unit
    //! once runs are batched: one command can be a 2,000-vertex polyline, so a command budget
    //! would bound nothing. The LOD band is the primary bound; this is the backstop that makes
    //! the worst case provable. Tune on hardware — the first number to lower if a console
    //! build is still GPU-bound.
    protected static const int ROAD_VERTEX_BUDGET = 12000;

    //! Draw every Nth vertex while the pose is moving. Clamped to >= 1 at the point of use;
    //! a zero or negative stride would not advance the polyline walk and would hang the frame.
    protected static const int ROAD_MOTION_VERTEX_STEP = 3;

    //! Motion score at or above which decimation kicks in.
    //!
    //! Without a score, any slow drift alternates: drift crosses the 0.25 px pose epsilon,
    //! that rebuild decimates and resets the stamp, the next frame is under the epsilon so it
    //! rebuilds full-detail, and the map shimmers between straightened and true geometry
    //! indefinitely. A walking player at mid zoom is exactly that case. A score means only a
    //! sustained gesture — a real pan or zoom — ever decimates, and the first build of a view
    //! (where the stamp is empty and everything reads as "moved") never does.
    protected static const int ROAD_MOTION_STREAK_FRAMES = 3;

    //! Ceiling on the score, so a long pan cannot bank so much credit that it keeps decimating
    //! for a visible stretch after the operator lets go.
    protected static const int ROAD_MOTION_STREAK_MAX = 6;

    //! How much a non-moving rebuild subtracts. Greater than the +1 for a moving one, so the
    //! score falls faster than it rises and a real stop converges in a couple of rebuilds.
    protected static const int ROAD_MOTION_STREAK_DECAY = 2;

    //! Motion score: +1 per moving frame, -DECAY per still one, clamped to [0, MAX].
    //!
    //! Deliberately a decaying score rather than a streak that any single still frame resets.
    //! A drag at 144 fps against a 125 Hz mouse produces a frame with no new delta roughly one
    //! in eight; a hard reset would drop out of decimation on each of those and produce the
    //! same geometry shimmer at a lower duty cycle. Decaying rides straight over them.
    protected int m_iRoadMotionStreak;

    //! ...but only when zoomed out past this, where consecutive vertices are a pixel or two
    //! apart anyway. Decimating a close-up road would visibly straighten curves the operator
    //! is using to navigate.
    protected static const float ROAD_MOTION_MIN_ZOOM = 0.30;

    //! Emit contiguous visible segments as one multi-vertex LineDrawCommand instead of one
    //! command per segment. Set false to revert to per-segment emission — see EmitRoadPolyline
    //! for why this is a flag and not just the behaviour.
    protected static const bool ROAD_BATCH_SEGMENTS = true;

    //! Round joins below this radius are skipped. Was effectively 1.0 (any stroke >= 2 px).
    protected static const float ROAD_JOIN_MIN_RADIUS = 2.0;

    //! Set for the duration of a static rebuild; read by the road pass. True means the
    //! operator has been panning or zooming for a sustained run of rebuilds.
    protected bool m_bRoadDecimating;

    //! Set BY the road pass when it actually decimated something. Distinct from
    //! m_bRoadDecimating, which is only an intent: on a terrain with no road dataset the pass
    //! early-returns, and treating that as "decimated" would force a pointless full rebuild of
    //! the satellite, overlays, structures and grid after every pan.
    protected bool m_bRoadDecimationApplied;

    //! Whether the cached static prefix was built decimated. Forces one full-detail rebuild
    //! once motion stops — otherwise the settled map would keep reusing a decimated prefix
    //! forever, since a settled pose never triggers a rebuild.
    protected bool m_bLastStaticWasDecimated;

    //! Rotation trig cache. WorldToScreen recomputed Math.Cos/Math.Sin of m_fRotation on every
    //! call — per road vertex, per structure corner, per grid endpoint, per shape vertex.
    //! Keyed on the rotation value rather than on the frame so callers outside the draw pass
    //! (marker placement via WorldToLayout) stay correct if rotation changed since.
    protected float m_fTrigForRotation = 99999;
    protected float m_fTrigCos = 1;
    protected float m_fTrigSin = 0;

    // Member markers
    protected ref array<ref AG0_TDLMapMarker> m_aMarkers = {};
	// Shape overlay (populated externally via SetShapes)
    protected ref array<ref AG0_TDLMapShape> m_aShapes;
	// In-progress shape being drawn by the user. Drawn after the committed
	// shape pass so it always renders on top, with a translucent stroke so it
	// reads as a preview rather than a real shape. Null = no active draw.
	protected ref AG0_TDLMapShape m_GhostShape;
	// Streamed terrain structures (populated externally via SetTerrainStructures).
	// Authoritative source for building footprints on the map view; API data is
	// broader and more accurate than the legacy runtime MapDescriptorComponent
	// query that previously sat here. Mirrors the m_aShapes pattern: ref to keep
	// the manager-owned array alive while we hold it.
	protected ref array<ref AG0_TDLTerrainStructureRecord> m_aTerrainStructures;

	// Streamed terrain roads (populated externally via SetTerrainRoads).
	// Drawn before structures so buildings render on top of road overlays.
	protected ref array<ref AG0_TDLTerrainRoadFeature> m_aTerrainRoads;

	// Dataset change signals feeding the static dirty gate. See SetTerrainStructures for why
	// the payload hash is used rather than the managers' GetVersion().
	protected string m_sTerrainStructureHash;
	protected int    m_iTerrainStructureCount = -1;
	protected string m_sTerrainRoadHash;
	protected int    m_iTerrainRoadCount      = -1;

    // Colors
    protected int m_iSelfMarkerColor = 0xFF00FF00;      // Green for self
    protected int m_iMemberMarkerColor = 0xFF00BFFF;    // Blue for network members
    protected int m_iMarkerOutlineColor = 0xFF000000;   // Black outline
    protected int m_iBuildingColor = 0xFF4A4A4A;        // Dark gray for buildings

    //------------------------------------------------------------------------------------------------
    // BLOODHOUND TOOL STATE
    //
    // When enabled, DrawBloodhound() renders a single solid stroke from the
    // device's world position through the cursor's world position, extended
    // outward in screen space to the canvas edge. A short perpendicular tick
    // marks the cursor intersection. Both endpoints are clipped to the canvas
    // rect so the line draws cleanly even when the device sits off-screen
    // (e.g. zoomed-in player view with the cursor on the far side of the map).
    //
    // Lime/yellow-green is the classic measurement-tool callout in ATAK,
    // distinct from the cyan map-network UI and the green self-marker so the
    // measurement line doesn't get confused with own-position elements.
    protected bool   m_bBloodhoundEnabled;
    protected vector m_vBloodhoundCursor;
    protected vector m_vBloodhoundDevice;
    protected const int   BLOODHOUND_COLOR     = 0xFFC0FF4D; // ARGB lime
    protected const float BLOODHOUND_WIDTH     = 2.0;
    protected const float BLOODHOUND_TICK_SIZE = 12.0;       // half-length of the cursor tick, pixels
    
    // Shape label styling
    protected static const float SHAPE_LABEL_SIZE = 10;          // Font size in pixels
    protected static const float SHAPE_LABEL_CHAR_WIDTH = 6.5;   // Approx px per character at label size
    protected static const float SHAPE_LABEL_HEIGHT = 12;        // Approx line height at label size
    protected static const float SHAPE_LABEL_PAD = 3;            // Background padding
    protected static const int SHAPE_LABEL_BG_COLOR = 0xCC000000; // Semi-transparent black background
    protected static const int SHAPE_LABEL_TEXT_COLOR = 0xFFFFFFFF; // White text

    // Grid line labelling
    protected static const float GRID_LABEL_SIZE = 11;              // Font size in pixels
    protected static const float GRID_LABEL_INSET_PX = 22;          // Distance walked along the line from its entry point
    protected static const float GRID_LABEL_EDGE_MARGIN_PX = 4;     // Clearance between the pill and the canvas edge
    protected static const float GRID_LABEL_MIN_CHORD_PX = 48;      // Shorter crossings only nick a corner — skip them
    protected static const float GRID_LABEL_MIN_SPACING_PX = 55;    // Below this, labels collide — omit them
    protected static const int GRID_LABEL_TEXT_COLOR = 0xFFE8E8E8;  // Slightly off-white, quieter than shape labels

    //------------------------------------------------------------------------------------------------
    void AG0_TDLMapView()
    {
    }
    
    //------------------------------------------------------------------------------------------------
    void ~AG0_TDLMapView()
    {
        // Claimed on every painted frame, so it has to be surrendered here: a destroyed view
        // that still holds the slot sends ToggleMap3DOnActiveView into a tree that is gone.
        if (s_ActiveView == this)
            s_ActiveView = null;

        m_pMapTexture = null;
        m_aDrawCommands = null;
        m_aMarkers = null;
    	m_aOverlayTextures = null;
    	m_aOverlayOpacities = null;
    	m_aOverlayNames = null;
    	m_aOverlayEnabled = null;
    }
    
    //------------------------------------------------------------------------------------------------
    // INITIALIZATION
    //------------------------------------------------------------------------------------------------
    bool Init(CanvasWidget canvas)
    {
        if (!canvas)
        {
            Print("[TDLMapView] Init failed - null canvas", LogLevel.WARNING);
            return false;
        }
        
        m_wCanvas = canvas;

        if (!LoadMapData())
        {
            Print("[TDLMapView] Failed to load map data from MapEntity", LogLevel.WARNING);
            return false;
        }
        
        if (!LoadMapTexture())
        {
            Print("[TDLMapView] Failed to load map texture", LogLevel.WARNING);
            // Continue anyway - we can still show markers without background
        }

    	LoadOverlays();

        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);

        m_vCenterWorld = Vector(
            m_fMapOffsetX + m_fMapSizeX * 0.5,
            0,
            m_fMapOffsetY + m_fMapSizeY * 0.5
        );
        
        Print(string.Format("[TDLMapView] Initialized - Map size: %1x%2, Offset: %3,%4", 
            m_fMapSizeX, m_fMapSizeY, m_fMapOffsetX, m_fMapOffsetY), LogLevel.DEBUG);
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool LoadMapData()
	{
	    SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
	    if (mapEntity)
	    {
	        vector size = mapEntity.Size();
	        vector offset = mapEntity.Offset();
	        
	        m_fMapSizeX = size[0];
	        m_fMapSizeY = size[2];
	        m_fMapOffsetX = offset[0];
	        m_fMapOffsetY = offset[2];
	    }
	    
	    // Fallback to world bounds
	    if (m_fMapSizeX <= 0 || m_fMapSizeY <= 0)
	    {
	        BaseWorld world = GetGame().GetWorld();
	        if (!world)
	            return false;
	        
	        vector mins, maxs;
	        world.GetBoundBox(mins, maxs);
	        
	        m_fMapSizeX = maxs[0] - mins[0];
	        m_fMapSizeY = maxs[2] - mins[2];
	        m_fMapOffsetX = mins[0];
	        m_fMapOffsetY = mins[2];
	    }
	    
	    return (m_fMapSizeX > 0 && m_fMapSizeY > 0);
	}
    
    //------------------------------------------------------------------------------------------------
	//! Satellite raster for whatever world is loaded, resolved without touching view state.
	//!
	//! Static because the 3D map drapes the same raster over its terrain and must resolve it
	//! identically — two lookups that could disagree would put a different picture on each
	//! surface of the same map.
	static ResourceName ResolveSatelliteTexture()
	{
	    ResourceName texturePath;

	    // Server's answer wins outright. It folds together the two sources an operator can
	    // actually change without a mod rebuild — api_config.json and the map record on
	    // tdl-api — so anything below is only reached when neither named a raster.
	    if (AG0_TDLMapSatelliteOverride.HasValue())
	    {
	        texturePath = AG0_TDLMapSatelliteOverride.Get();
	        return texturePath;
	    }

	    // Prefab lookup next: worlds that carry a properly configured map entity describe
	    // their own raster, so no per-world table entry is needed for them.
	    SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
	    if (mapEntity)
	    {
	        EntityPrefabData prefabData = mapEntity.GetPrefabData();
	        if (prefabData)
	        {
	            BaseContainer container = prefabData.GetPrefab();
	            if (container)
	                container.Get("Satellite background image", texturePath);
	        }
	    }

	    // Lookup table covers worlds configured on the instance rather than the prefab,
	    // where the property above resolves empty.
	    if (texturePath.IsEmpty())
	    {
	        texturePath = GetFallbackSatelliteTexture();
	        if (!texturePath.IsEmpty())
	            Print(string.Format("[TDLMapView] Using fallback texture: %1", texturePath), LogLevel.WARNING);
	    }

	    return texturePath;
	}

    //------------------------------------------------------------------------------------------------
	protected bool LoadMapTexture()
	{
	    if (!SCR_MapEntity.GetMapInstance())
	        return false;

	    ResourceName texturePath = ResolveSatelliteTexture();
	    if (texturePath.IsEmpty())
	    {
	        Print("[TDLMapView] Could not determine satellite texture path", LogLevel.WARNING);
	        return false;
	    }

	    Print(string.Format("[TDLMapView] Loading texture: %1", texturePath), LogLevel.DEBUG);
	    
	    m_pMapTexture = CanvasWidget.LoadTexture(texturePath);
	    if (!m_pMapTexture)
	    {
	        Print(string.Format("[TDLMapView] Failed to load texture: %1", texturePath), LogLevel.WARNING);
	        return false;
	    }

	    m_bTextureLoaded = true;
	    m_iSatelliteRevision = AG0_TDLMapSatelliteOverride.GetRevision();
	    return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Revision of the server-pushed raster this view's texture was built from.
	//! The push is not synchronised with the map opening — a player who joins and opens the
	//! map immediately will usually have drawn a frame or two before it lands.
	protected int m_iSatelliteRevision = -1;

	//------------------------------------------------------------------------------------------------
	//! Swap in a raster that arrived after Init(). Cheap when nothing changed: the revision
	//! only moves when the server pushed a genuinely different path.
	protected void RefreshSatelliteTextureIfStale()
	{
	    if (m_iSatelliteRevision == AG0_TDLMapSatelliteOverride.GetRevision())
	        return;

	    m_iSatelliteRevision = AG0_TDLMapSatelliteOverride.GetRevision();
	    m_pMapTexture = null;
	    m_bTextureLoaded = false;
	    LoadMapTexture();

	    // A different raster is different static content even though the pose has not moved.
	    // The revision is a stamp input too, so this is belt-and-braces — but the flag is what
	    // makes the intent explicit at the point of change.
	    MarkStaticDirty();

	    // The drape is baked once at slab build time, so it cannot notice this on its own —
	    // and a 3D surface still showing the previous raster while the 2D one has swapped is
	    // exactly the disagreement a single resolution point is meant to prevent.
	    AG0_TDLMap3DView view = GetHostedMap3DView();
	    if (view)
	        view.RefreshDrapeRaster();
	}
	
	//------------------------------------------------------------------------------------------------
    //! Load overlay textures from satellite config for current world
    protected void LoadOverlays()
    {
        AG0_MapSatelliteEntry mapEntry = AG0_MapSatelliteConfigHelper.GetMapEntryForCurrentWorld(MAP_SATELLITE_CONFIG);
        if (!mapEntry || !mapEntry.m_aOverlays || mapEntry.m_aOverlays.IsEmpty())
            return;
        
        // Sort by draw order
        array<ref AG0_MapOverlayEntry> sorted = {};
        foreach (AG0_MapOverlayEntry overlay : mapEntry.m_aOverlays)
        {
            if (!overlay)
                continue;
            
            // Insertion sort by draw order
            bool inserted = false;
            for (int i = 0; i < sorted.Count(); i++)
            {
                if (overlay.m_iDrawOrder < sorted[i].m_iDrawOrder)
                {
                    sorted.InsertAt(overlay, i);
                    inserted = true;
                    break;
                }
            }
            if (!inserted)
                sorted.Insert(overlay);
        }
        
        // Load each overlay texture
        foreach (AG0_MapOverlayEntry overlay : sorted)
        {
            if (overlay.m_OverlayTexture.IsEmpty())
            {
                Print(string.Format("[TDLMapView] Overlay '%1' has no texture, skipping", overlay.m_sLayerName), LogLevel.WARNING);
                continue;
            }
            
            SharedItemRef texture = CanvasWidget.LoadTexture(overlay.m_OverlayTexture);
            if (!texture)
            {
                Print(string.Format("[TDLMapView] Failed to load overlay texture: %1", overlay.m_OverlayTexture), LogLevel.WARNING);
                continue;
            }
            
            m_aOverlayTextures.Insert(texture);
            m_aOverlayOpacities.Insert(overlay.m_fOpacity);
            m_aOverlayNames.Insert(overlay.m_sLayerName);
            m_aOverlayEnabled.Insert(overlay.m_bEnabledByDefault);
            
            // Check if we have a structures overlay (to skip runtime building queries)
            string nameLower = overlay.m_sLayerName;
            nameLower.ToLower();
            if (nameLower.Contains("struct") || nameLower.Contains("building"))
                m_bHasStructureOverlay = true;
            
            Print(string.Format("[TDLMapView] Loaded overlay: '%1' (order %2, opacity %3)", 
                overlay.m_sLayerName, overlay.m_iDrawOrder, overlay.m_fOpacity), LogLevel.DEBUG);
        }
        
        Print(string.Format("[TDLMapView] Loaded %1 overlay layers", m_aOverlayTextures.Count()), LogLevel.DEBUG);
    }
	
	//------------------------------------------------------------------------------------------------
	// Fallback texture lookup for maps without proper prefab configuration
	protected static ResourceName GetFallbackSatelliteTexture()
	{
	    AG0_MapSatelliteConfig config = AG0_MapSatelliteConfigHelper.GetConfig(MAP_SATELLITE_CONFIG);
	    if (!config)
	    {
	        Print("[TDLMapView] Map satellite config not found, no fallback available", LogLevel.WARNING);
	        return ResourceName.Empty;
	    }
	    
	    string worldFile = GetGame().GetWorldFile();
	    ResourceName texture = config.GetSatelliteTexture(worldFile);
	    
	    if (texture.IsEmpty())
	        Print(string.Format("[TDLMapView] No satellite texture configured for world: %1", worldFile), LogLevel.WARNING);
	    
	    return texture;
	}
    
    //------------------------------------------------------------------------------------------------
    // VIEW CONTROL
    //------------------------------------------------------------------------------------------------
    void SetCenter(vector worldPos)
    {
        m_vCenterWorld = worldPos;
    }
    
    //------------------------------------------------------------------------------------------------
    void CenterOnPlayer()
    {
        IEntity player = GetGame().GetPlayerController().GetControlledEntity();
        if (!player)
            return;

        // Player tracking has to move the 3D orbit focus, not the 2D centre. While the
        // pane is up Draw() rewrites m_vCenterWorld from that focus every frame, so a
        // 2D-only write here is erased before anything reads it — which is why the track
        // button did nothing in 3D.
        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (view)
        {
            view.FocusOnWorld(player.GetOrigin());
            return;
        }

        m_vCenterWorld = player.GetOrigin();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Degrees of orbit per pixel of drag while the 3D map is up. See AG0_TDLMap3DView.c.
    protected static const float MAP3D_ORBIT_DEG_PER_PX = 0.25;

    //! Sentinel coordinate for points the 3D projection rejects (behind the camera).
    //! Far enough off-canvas that every consumer's visibility bounds check fails and
    //! even unchecked widgets (the self marker) land nowhere visible.
    protected static const float MAP3D_OFFSCREEN_PX = -100000;

    //! Tracks the view currently drawing so an input handler on the player controller
    //! can reach a live canvas host without the controller having to know how the menu
    //! and world-space frontends each build their widget tree.
    protected static AG0_TDLMapView s_ActiveView;

    //! Declared by whoever built this view, not inferred. The world-space device says so; the
    //! fullscreen menu leaves it false. Everything about pane arbitration hangs off this one
    //! bit, and it is not something to work out from the widget tree at runtime.
    protected bool m_bIsWorldSpaceSurface;

    //! A mirror surface paints continuously but must never be the view a keybind or the 3D
    //! pane resolves to — the operator means the map they are actually looking at. Without
    //! this, a peripheral drawing every tick wins s_ActiveView permanently and outbids both
    //! real surfaces in RequestHost, so the 3D toggle lands on the mirror instead.
    protected bool m_bPassive;

    //------------------------------------------------------------------------------------------------
    //! Entry point for the toggle keybind and the toolbar button, which both reach the
    //! active view rather than the 3D map directly — only the view knows which canvas host
    //! is currently live.
    void SetWorldSpaceSurface(bool isWorldSpace)
    {
        m_bIsWorldSpaceSurface = isWorldSpace;
    }

    bool IsWorldSpaceSurface()
    {
        return m_bIsWorldSpaceSurface;
    }

    //------------------------------------------------------------------------------------------------
    void SetPassive(bool passive)
    {
        m_bPassive = passive;
    }

    //------------------------------------------------------------------------------------------------
    //! Which tier this surface occupies when contending for the 3D pane. Derived rather than
    //! stored because both inputs are already declared by whoever built the view, and a third
    //! flag to keep in step with them would be a third thing to get wrong.
    protected int GetHostRank()
    {
        if (m_bPassive)
            return AG0_TDLMap3DView.HOST_RANK_MIRROR;

        if (m_bIsWorldSpaceSurface)
            return AG0_TDLMap3DView.HOST_RANK_WORLDSPACE;

        return AG0_TDLMap3DView.HOST_RANK_MENU;
    }

    //------------------------------------------------------------------------------------------------
    static void ToggleMap3DOnActiveView()
    {
        if (s_ActiveView)
            s_ActiveView.ToggleMap3D();
    }

    //------------------------------------------------------------------------------------------------
    //! The open 3D map, but only when THIS view's widget tree hosts its pane. Two views
    //! can tick per frame (menu + world-space device), and only the hosting one may
    //! reroute its projections — the other still describes a live 2D canvas.
    protected AG0_TDLMap3DView GetHostedMap3DView()
    {
        if (!m_wCanvas || !AG0_TDLMap3DView.IsViewOpen())
            return null;

        AG0_TDLMap3DView view = AG0_TDLMap3DView.GetInstance();
        if (!view)
            return null;

        // Same host resolution as ToggleMap3D, so the comparison is against the
        // widget the pane was actually parented to.
        Widget host = m_wCanvas.GetParent();
        if (!host)
            host = m_wCanvas;

        if (!view.IsHostedBy(host))
            return null;

        return view;
    }

    //------------------------------------------------------------------------------------------------
    //! Right-drag axis. Deliberately a no-op while the 3D map is closed:
    //! the 2D map has always been single-axis, and silently giving right-drag a second
    //! meaning there would change behaviour nobody asked to change.
    //! Pan the map in whichever mode is showing.
    //!
    //! Distinct from Pan(), which reroutes to orbit while 3D is up — that reroute exists so
    //! the drag gesture means "turn the view" in 3D, and it is the right answer for a drag.
    //! It is the wrong answer for a stick dedicated to panning, which has to keep meaning
    //! the same thing in both modes or the operator relearns it every time they toggle.
    void PanMap(float screenDeltaX, float screenDeltaY)
    {
        if (AG0_TDLMenuController.IsAnyRadialOpen())
            return;

        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (view)
        {
            view.PanInput(screenDeltaX, screenDeltaY);
            return;
        }

        Pan(screenDeltaX, screenDeltaY);
    }

    //------------------------------------------------------------------------------------------------
    void PanSecondary(float screenDeltaX, float screenDeltaY)
    {
        if (AG0_TDLMenuController.IsAnyRadialOpen())
            return;

        if (!AG0_TDLMap3DView.IsViewOpen())
            return;

        AG0_TDLMap3DView panView = AG0_TDLMap3DView.GetInstance();
        if (panView)
            panView.PanInput(screenDeltaX, screenDeltaY);
    }

    //------------------------------------------------------------------------------------------------
    void SetZoom(float zoom)
    {
        // The radial reads the same stick and wheel this does. Without swallowing them the
        // map zooms and slides underneath an open menu, which on a pad also drags the world
        // position the menu's entries were opened against.
        if (AG0_TDLMenuController.IsAnyRadialOpen())
            return;

        // The zoom control is shared so the existing wheel/stick binding drives orbit
        // distance while the 3D pane is up.
        if (AG0_TDLMap3DView.IsViewOpen())
        {
            AG0_TDLMap3DView zoomView = AG0_TDLMap3DView.GetInstance();
            if (zoomView)
            {
                float direction = -1;
                if (zoom < m_fZoom)
                    direction = 1;
                zoomView.ZoomInput(direction);
            }
            return;
        }

        m_fZoom = Math.Clamp(zoom, m_fMinZoom, m_fMaxZoom);
    }

    //------------------------------------------------------------------------------------------------
    //! Raw zoom write for a passive mirror, which reproduces a value rather than expressing an
    //! input. SetZoom is input-facing: it swallows the write while a radial is open, and while
    //! the 3D pane is up it forwards to ZoomInput as a relative step instead. A mirror pushing
    //! its target zoom every tick through that path would ratchet the operator's orbit distance
    //! to the clamp in about two seconds and freeze its own 2D zoom while doing it.
    void ApplyMirrorZoom(float zoom)
    {
        m_fZoom = Math.Clamp(zoom, m_fMinZoom, m_fMaxZoom);
    }

    //------------------------------------------------------------------------------------------------
    void ZoomIn(float amount = 0.1)
    {
        SetZoom(m_fZoom - amount);
    }
    
    //------------------------------------------------------------------------------------------------
    void ZoomOut(float amount = 0.1)
    {
        SetZoom(m_fZoom + amount);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Deliberately has no 3D counterpart. In 2D this is called with 0 every frame that
    //! track-up is off, to hold the map north-up; doing the same to camera yaw would
    //! fight the operator's orbit drag back to north on every frame. With track-up off,
    //! 3D yaw belongs to whoever last dragged it.
    void SetRotation(float degrees)
    {
        m_fRotation = degrees;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetTrackUp(float playerHeading)
    {
        // Track-up in 3D is camera yaw, not a canvas rotation: m_fRotation describes the
        // 2D map, which is not being drawn while the pane is up. Yaw is set to the raw
        // heading rather than its negation so that GetRotation()'s 3D branch (-cameraYaw)
        // hands the compass needle the same value 2D would have.
        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (view)
        {
            view.SetCameraYaw(playerHeading);
            return;
        }

        // Rotate map so player heading points up
        m_fRotation = -playerHeading;
    }
    
    //------------------------------------------------------------------------------------------------
    void Pan(float screenDeltaX, float screenDeltaY)
    {
        if (AG0_TDLMenuController.IsAnyRadialOpen())
            return;

        // Rerouting here rather than in the drag handler means every existing pan gesture
        // (mouse, gamepad, world-space cursor) drives the 3D orbit without a second input
        // path to keep in sync.
        if (AG0_TDLMap3DView.IsViewOpen())
        {
            // Taking manual yaw means leaving track-up, the same way a drag already
            // releases player tracking. Without this the operator orbits, track-up
            // re-pins yaw to their heading on the next frame, and the view appears to
            // fight back with no indication of why. 2D is unaffected: there a drag pans
            // and never touches rotation, so track-up and dragging do not compete.
            if (AG0_TDLDisplayController.GetTrackUp())
                AG0_TDLDisplayController.SetTrackUp(false);

            AG0_TDLMap3DView orbitView = AG0_TDLMap3DView.GetInstance();
            if (orbitView)
                orbitView.OrbitInput(screenDeltaX * MAP3D_ORBIT_DEG_PER_PX,
                    screenDeltaY * MAP3D_ORBIT_DEG_PER_PX);
            return;
        }

        float worldUnitsPerPixel = GetWorldUnitsPerPixel();

        float rotRad = m_fRotation * Math.DEG2RAD;
        float cosR = Math.Cos(rotRad);
        float sinR = Math.Sin(rotRad);

        float worldDeltaX = (screenDeltaX * cosR - screenDeltaY * sinR) * worldUnitsPerPixel;
        float worldDeltaZ = (screenDeltaX * sinR + screenDeltaY * cosR) * worldUnitsPerPixel;

        m_vCenterWorld[0] = m_vCenterWorld[0] - worldDeltaX;
        m_vCenterWorld[2] = m_vCenterWorld[2] - worldDeltaZ;

        ClampCenterToBounds();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ClampCenterToBounds()
	{
	    // Allow centering anywhere on the map, with some margin off edges
	    float margin = 200; // Allow 200m past map edge
	    
	    m_vCenterWorld[0] = Math.Clamp(m_vCenterWorld[0], 
	        m_fMapOffsetX - margin, 
	        m_fMapOffsetX + m_fMapSizeX + margin);
	    m_vCenterWorld[2] = Math.Clamp(m_vCenterWorld[2], 
	        m_fMapOffsetY - margin, 
	        m_fMapOffsetY + m_fMapSizeY + margin);
	}
    
    //------------------------------------------------------------------------------------------------
    //! Public because pick radii are authored in screen pixels — a hit-test radius fixed in
    //! metres is unhittable zoomed in and indiscriminate zoomed out.
    float GetWorldUnitsPerPixel()
    {
        // How many world units does one pixel represent at current zoom
        float viewSizeWorld = m_fMapSizeX * m_fZoom;
        return viewSizeWorld / m_fCanvasWidth;
    }
    
    //------------------------------------------------------------------------------------------------
    // COORDINATE CONVERSION
    //------------------------------------------------------------------------------------------------
    
    // World position to UV coordinates (0-1 range on texture)
    void WorldToUV(vector worldPos, out float u, out float v)
    {
        u = (worldPos[0] - m_fMapOffsetX) / m_fMapSizeX;
        // Flip V - texture Y is typically top-down, world Z is bottom-up
        v = 1.0 - ((worldPos[2] - m_fMapOffsetY) / m_fMapSizeY);
    }
    
    //------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	//! Refresh the cos/sin pair WorldToScreen uses, but only when m_fRotation actually moved.
	//! Keyed on the rotation value rather than on a frame counter so callers outside the draw
	//! pass (WorldToLayout for marker widgets, called after SetTrackUp may have rotated the
	//! map) can never read a pair belonging to a different heading.
	protected void EnsureRotationTrig()
	{
	    if (m_fTrigForRotation == m_fRotation)
	        return;

	    float rotRad = -m_fRotation * Math.DEG2RAD;
	    m_fTrigCos = Math.Cos(rotRad);
	    m_fTrigSin = Math.Sin(rotRad);
	    m_fTrigForRotation = m_fRotation;
	}

	//------------------------------------------------------------------------------------------------
	// World position to screen position (SCREEN PIXELS for canvas drawing)
	//! `useAltitude` is passed through to the 3D projection: it says this caller's Y is a real
	//! height and not the zero a map-placed marker carries. Ignored in 2D, where there is no
	//! third dimension to be wrong about.
	void WorldToScreen(vector worldPos, out float screenX, out float screenY, bool useAltitude = false)
	{
	    // While the 3D pane is up, this seam IS the parity mechanism: every widget-based
	    // consumer (self/member/vanilla markers and their labels) positions through
	    // WorldToLayout -> here, so routing the projection through the 3D map puts all of
	    // them onto the 3D terrain with no second ingestion path to keep in sync. The
	    // pane, the canvas and the marker overlay all fill the same frame in the layout,
	    // so pane pixels and canvas pixels are the same space.
	    // Skipped during the 2D draw pass. Reaching BuildStaticCommands/BuildDynamicCommands
	    // means this surface lost or declined the host arbitration, so IsHostedBy is provably
	    // false and this probe can only return null — at the cost of IsViewOpen -> GetInstance
	    // -> GetParent -> IsHostedBy per projected point. With a few thousand road vertices in
	    // frame that was thousands of native calls per frame for a constant answer, and it was
	    // paid on every frame of the session once the 3D map had been opened once.
	    if (!m_bIn2DDrawPass)
	    {
	        AG0_TDLMap3DView view = GetHostedMap3DView();
	        if (view)
	        {
	            if (view.ProjectWorldToPane(worldPos, screenX, screenY, useAltitude))
	                return;

	            // Behind-camera and unmeasurable-pane points land far off-canvas instead of
	            // reporting failure: this signature has no validity channel, and every marker
	            // caller already bounds-checks against the canvas, so an impossible coordinate
	            // is what hides them.
	            screenX = MAP3D_OFFSCREEN_PX;
	            screenY = MAP3D_OFFSCREEN_PX;
	            return;
	        }
	    }

	    float offsetX = worldPos[0] - m_vCenterWorld[0];
	    float offsetZ = worldPos[2] - m_vCenterWorld[2];

	    // Apply rotation (negated to match texture rotation direction).
	    // Cached rather than recomputed: this is called per road vertex, per structure
	    // corner, per grid endpoint and per shape vertex, so the two transcendentals were
	    // being evaluated thousands of times a frame for a value that changes at most once.
	    EnsureRotationTrig();
	    float cosR = m_fTrigCos;
	    float sinR = m_fTrigSin;

	    float rotatedX = offsetX * cosR - offsetZ * sinR;
	    float rotatedZ = offsetX * sinR + offsetZ * cosR;

	    // Use aspect-corrected view size (same as DrawMapTexture)
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom;
	    float pixelsPerWorldUnit = m_fCanvasWidth / viewWorldSizeX;

	    screenX = (m_fCanvasWidth * 0.5) + (rotatedX * pixelsPerWorldUnit);
	    // Flip Y for screen coordinates
	    screenY = (m_fCanvasHeight * 0.5) - (rotatedZ * pixelsPerWorldUnit);
	}
	
	//------------------------------------------------------------------------------------------------
	// World position to LAYOUT coordinates (for widget positioning)
	void WorldToLayout(vector worldPos, out float layoutX, out float layoutY, bool useAltitude = false)
	{
	    float screenX, screenY;
	    WorldToScreen(worldPos, screenX, screenY, useAltitude);

	    WorkspaceWidget workspace = GetGame().GetWorkspace();
	    layoutX = workspace.DPIUnscale(screenX);
	    layoutY = workspace.DPIUnscale(screenY);
	}
    
    //------------------------------------------------------------------------------------------------
    //! Pre-flight check for callers that produce per-frame cursor samples.
    //! Returns true when m_fCanvasWidth, m_fMapSizeX and m_fZoom are all
    //! populated — i.e. the display controller has sized the view and the
    //! arithmetic in ScreenToWorld / WorldToScreen will yield real results.
    //! Callers (notably world-space's cursor push) should skip their push
    //! entirely when this returns false, so the session's m_bCursorWorldKnown
    //! stays false and freehand samples / ghost rendering don't latch a
    //! bogus value from ScreenToWorld's fallback.
    bool IsReady()
    {
        return m_fCanvasWidth > 0 && m_fMapSizeX > 0 && m_fZoom > 0;
    }

    //------------------------------------------------------------------------------------------------
    //! Terrain height under a world position, from the same source the 3D map builds its mesh
    //! from — so a grid reference reads one elevation everywhere in TDL, on either surface.
    //!
    //! Kept separate from ScreenToWorld rather than folded into it: that method's zero-Y
    //! contract is relied on by every caller it has, and elevation is a readout concern, so
    //! the callers that want it ask for it.
    float SampleElevation(vector worldPos)
    {
        return AG0_TDLMap3DView.SampleTerrainY(worldPos[0], worldPos[2]);
    }

    //------------------------------------------------------------------------------------------------
    // Screen position to world position
    void ScreenToWorld(float screenX, float screenY, out vector worldPos)
    {
        // Placement parity for the 3D pane: the 2D inverse below answers from pan/zoom
        // state that is not being rendered while the 3D map is up, so a click would land
        // wherever the hidden 2D view happens to be aimed — the wrong-position marker
        // bug. The 3D map picks against the rendered terrain instead and returns GAME
        // world coordinates (diorama coords never leave that class). Y is zeroed to keep
        // this method's contract — callers snap elevation themselves.
        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (view)
        {
            vector picked;
            if (view.PickWorldFromPane(screenX, screenY, picked))
            {
                worldPos = Vector(picked[0], 0, picked[2]);
                return;
            }

            // Above-the-horizon clicks have no ground under them; the focus is the
            // only sane stand-in and matches what the crosshair path would place.
            vector focusWorld = view.GetFocusWorld();
            worldPos = Vector(focusWorld[0], 0, focusWorld[2]);
            return;
        }

        // Early-frame guard: callers on a per-frame tick (e.g. world-space
        // cursor push) can invoke this before the map view has been sized
        // or before m_fMapSizeX / m_fZoom have been populated by the display
        // controller. Without this guard the division below throws a VM
        // exception and the caller gets garbage anyway. Falling back to the
        // current map center keeps the result well-defined; callers that
        // need to know they got a "not ready" answer should call IsReady()
        // before invoking this function.
        if (m_fCanvasWidth <= 0 || m_fMapSizeX <= 0 || m_fZoom <= 0)
        {
            worldPos = m_vCenterWorld;
            return;
        }

        float canvasCenterX = m_fCanvasWidth * 0.5;
        float canvasCenterY = m_fCanvasHeight * 0.5;

        float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);

        float offsetX = (screenX - canvasCenterX) / pixelsPerWorldUnit;
        float offsetZ = -(screenY - canvasCenterY) / pixelsPerWorldUnit; // Flip Y

        // Inverse rotation — WorldToScreen applies rotation by -m_fRotation
        // (negated for texture-direction match), so the inverse must apply
        // +m_fRotation. Previously this used the SAME negated value as
        // WorldToScreen, which only cancelled correctly when rotation = 0
        // (north-up); track-up mode produced clicks at wrong world coords.
        float rotRad = m_fRotation * Math.DEG2RAD;
        float cosR = Math.Cos(rotRad);
        float sinR = Math.Sin(rotRad);

        float worldX = offsetX * cosR - offsetZ * sinR;
        float worldZ = offsetX * sinR + offsetZ * cosR;
        
        worldPos = Vector(
            m_vCenterWorld[0] + worldX,
            0,
            m_vCenterWorld[2] + worldZ
        );
    }
    
    //------------------------------------------------------------------------------------------------
    // MARKER MANAGEMENT
    //------------------------------------------------------------------------------------------------
    void ClearMarkers()
    {
        m_aMarkers.Clear();
    }
    
    //------------------------------------------------------------------------------------------------
    void AddMarker(vector worldPos, int color, float size = 8, string label = "")
    {
        AG0_TDLMapMarker marker = new AG0_TDLMapMarker();
        marker.m_vWorldPos = worldPos;
        marker.m_iColor = color;
        marker.m_fSize = size;
        marker.m_sLabel = label;
        m_aMarkers.Insert(marker);
    }
	
	//------------------------------------------------------------------------------------------------
	//! Set shapes to render from the shape manager
	//! Call each frame or when shapes update — the array is read during Draw()
	void SetShapes(array<ref AG0_TDLMapShape> shapes)
	{
		m_aShapes = shapes;
	}

	//------------------------------------------------------------------------------------------------
	//! Push the in-progress draw shape. Pass null to clear the preview when the
	//! user cancels or commits. The draw session rebuilds this each frame
	//! during COLLECTING so cursor-tracked rubber-band geometry stays live
	//! without per-tick allocations escaping the session.
	void SetGhostShape(AG0_TDLMapShape ghost)
	{
		m_GhostShape = ghost;
	}

	//------------------------------------------------------------------------------------------------
	//! Set terrain structure records to render. Pass null or an empty array
	//! when no API dataset is available (no buildings will draw on the map view).
	//! The array is read during Draw(); call each frame from the controller.
	//!
	//! syncHash: the manager's payload hash. Structures live in the STATIC command prefix, so
	//! the dirty gate needs to know when the dataset actually changed — and it cannot find that
	//! out by comparing the array handle, because the manager refills the SAME array object in
	//! place on every payload.
	//!
	//! The hash rather than GetVersion(): both terrain managers reject any payload whose wire
	//! version is not SUPPORTED_VERSION and then assign that same constant, so GetVersion()
	//! returns 1 forever and would never move. The hash is the real change token — without it
	//! a re-exported dataset with the same feature count (moved geometry, same building count)
	//! would not invalidate the cached prefix and the map would keep drawing the old terrain
	//! until the 1 Hz keepalive happened to fire.
	//!
	//! Count is kept alongside it so callers that pass no hash still get the arrival/clear
	//! transitions, which is what the pre-existing call sites relied on.
	void SetTerrainStructures(array<ref AG0_TDLTerrainStructureRecord> structures, string syncHash = "")
	{
		m_aTerrainStructures = structures;
		m_sTerrainStructureHash = syncHash;

		if (structures)
			m_iTerrainStructureCount = structures.Count();
		else
			m_iTerrainStructureCount = -1;   // distinct from an empty-but-present dataset
	}

	//------------------------------------------------------------------------------------------------
	//! Set terrain road features to render. Pass null/empty for no roads.
	//! Read during Draw(); refreshed each frame from the controller.
	//! syncHash: see SetTerrainStructures.
	void SetTerrainRoads(array<ref AG0_TDLTerrainRoadFeature> roads, string syncHash = "")
	{
		m_aTerrainRoads = roads;
		m_sTerrainRoadHash = syncHash;

		if (roads)
			m_iTerrainRoadCount = roads.Count();
		else
			m_iTerrainRoadCount = -1;
	}

    
    //------------------------------------------------------------------------------------------------
    void AddSelfMarker(vector worldPos, float heading)
    {
        AG0_TDLMapMarker marker = new AG0_TDLMapMarker();
        marker.m_vWorldPos = worldPos;
        marker.m_iColor = m_iSelfMarkerColor;
        marker.m_fSize = 6;
        marker.m_fHeading = heading;
        marker.m_bShowHeading = true;
        marker.m_sLabel = "YOU";
        m_aMarkers.Insert(marker);
    }
    
    //------------------------------------------------------------------------------------------------
    void AddMemberMarker(vector worldPos, string playerName, float signalStrength)
    {
        AG0_TDLMapMarker marker = new AG0_TDLMapMarker();
        marker.m_vWorldPos = worldPos;
        marker.m_iColor = m_iMemberMarkerColor;
        marker.m_fSize = 5;
        marker.m_sLabel = playerName;
        m_aMarkers.Insert(marker);
    }
    
    //------------------------------------------------------------------------------------------------
    // RENDERING
    //------------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------
    //! Switch this surface between 2D and 3D.
    //!
    //! The pane is parented to the map canvas's own parent rather than to the canvas
    //! itself, so it is a sibling that can be z-ordered above the 2D surface without
    //! becoming a child of a widget whose draw commands are rebuilt every frame.
    void ToggleMap3D()
    {
        if (!m_wCanvas)
            return;

        Widget host = m_wCanvas.GetParent();
        if (!host)
            host = m_wCanvas;

        // The 2D centre seeds the 3D focus so the switch reads as the same map
        // standing up. Y is dropped — the 3D map seats the focus on its own terrain.
        AG0_TDLMap3DView.Toggle(host, GetHostRank(),
            Vector(m_vCenterWorld[0], 0, m_vCenterWorld[2]));
    }

    //------------------------------------------------------------------------------------------------
    //! Tap-to-recenter: aim the 3D orbit focus at the terrain under a bare map click.
    //! The world-space surface has only one drag axis (orbit), so a tap is its pan; on
    //! the menu it complements right-drag. Deliberately a no-op in 2D — a bare 2D click
    //! has never panned, and silently giving it that meaning would change behaviour
    //! nobody asked to change. Returns whether the tap was consumed.
    bool TapFocusMap3D(float screenX, float screenY)
    {
        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (!view)
            return false;

        vector picked;
        if (!view.PickWorldFromPane(screenX, screenY, picked))
            return false;

        view.FocusOnWorld(picked);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    // STATIC DIRTY GATE
    //------------------------------------------------------------------------------------------------

    //! Force a full static rebuild on the next Draw(). Call from anything that changes what
    //! the static prefix should contain without moving the pose: overlay toggles, satellite
    //! swap, dataset replacement, 3D pane handover.
    void MarkStaticDirty()
    {
        m_bStaticDirty = true;
    }

    //! Throw the cached prefix away entirely. Stronger than MarkStaticDirty: used when the
    //! commands themselves can no longer be trusted (the 3D pane took the canvas and
    //! submitted its own list, so our prefix is no longer what the canvas is holding).
    void InvalidateStaticCache()
    {
        m_iStaticCommandCount = -1;
        m_bStaticStampValid = false;
        m_bStaticDirty = true;

        // The motion score is only meaningful relative to a live stamp. Leaving it banked
        // across a 3D-pane session means the first 2D frame back — where TickHostedPane has
        // rewritten m_vCenterWorld from the pane focus, so the pose always reads as moved —
        // decimates immediately on a view the operator has only just started looking at.
        m_iRoadMotionStreak = 0;
    }

    //! Bitmask of which overlay layers are on. Cheap enough to recompute per frame and it
    //! removes the need to trust every future overlay-toggle path to call MarkStaticDirty.
    protected int ComputeOverlayMask()
    {
        int mask = 0;
        if (!m_aOverlayEnabled)
            return mask;

        int n = m_aOverlayEnabled.Count();
        if (n > 31)
            n = 31;

        for (int i = 0; i < n; i++)
        {
            if (m_aOverlayEnabled[i])
                mask = mask | (1 << i);
        }
        return mask;
    }

    //! True when the pose has moved far enough to change a rasterized pixel. Center is
    //! converted to screen pixels first so one epsilon works at every zoom level.
    protected bool StaticPoseMoved()
    {
        if (Math.AbsFloat(m_fZoom - m_fStaticStampZoom) > STATIC_ZOOM_EPSILON)
            return true;

        if (Math.AbsFloat(m_fRotation - m_fStaticStampRotation) > STATIC_ROTATION_EPSILON)
            return true;

        float viewWorldSizeX = m_fMapSizeX * m_fZoom;
        if (viewWorldSizeX <= 0)
            return true;

        float pixelsPerWorldUnit = m_fCanvasWidth / viewWorldSizeX;
        float dxPx = Math.AbsFloat(m_vCenterWorld[0] - m_vStaticStampCenter[0]) * pixelsPerWorldUnit;
        float dzPx = Math.AbsFloat(m_vCenterWorld[2] - m_vStaticStampCenter[2]) * pixelsPerWorldUnit;

        return dxPx > STATIC_POSE_EPSILON_PX || dzPx > STATIC_POSE_EPSILON_PX;
    }

    //! Two outcomes:
    //!   REBUILD — no valid cache, explicit dirty flag, canvas resized, dataset / overlay /
    //!             satellite change, or the pose moved. Always this frame, never rate-capped
    //!             (see the note on STATIC_KEEPALIVE_INTERVAL above for why).
    //!   REUSE   — nothing moved. Keep the prefix; a 1 Hz keepalive is the backstop against a
    //!             missed invalidation leaving a permanently stale map.
    protected bool ShouldRebuildStatic(int overlayMask, bool poseMoved)
    {
        if (m_iStaticCommandCount < 0 || !m_bStaticStampValid || m_bStaticDirty)
            return true;

        // The cached prefix was built with decimated road geometry, which is only acceptable
        // while the operator is actually moving. Once they stop, a settled pose would never
        // trigger another rebuild — so the map would sit there permanently showing straightened
        // roads. Force exactly one full-detail rebuild on the frame motion ends.
        if (m_bLastStaticWasDecimated)
            return true;

        if (m_fCanvasWidth != m_fStaticStampCanvasW || m_fCanvasHeight != m_fStaticStampCanvasH)
            return true;

        if (m_sTerrainRoadHash != m_sStaticStampRoadHash
         || m_iTerrainRoadCount != m_iStaticStampRoadCount)
            return true;

        if (m_sTerrainStructureHash != m_sStaticStampStructHash
         || m_iTerrainStructureCount != m_iStaticStampStructCount)
            return true;

        if (overlayMask != m_iStaticStampOverlayMask)
            return true;

        if (m_iSatelliteRevision != m_iStaticStampSatRevision)
            return true;

        if (m_bTextureLoaded != m_bStaticStampTextureLoaded)
            return true;

        if (poseMoved)
            return true;

        return m_fSinceStaticRebuild >= STATIC_KEEPALIVE_INTERVAL;
    }

    //! Record what the freshly built prefix was built against. Only ever called immediately
    //! after a successful rebuild, so stamp and commands cannot drift apart.
    protected void CommitStaticStamp(int overlayMask)
    {
        m_vStaticStampCenter          = m_vCenterWorld;
        m_fStaticStampZoom            = m_fZoom;
        m_fStaticStampRotation        = m_fRotation;
        m_fStaticStampCanvasW         = m_fCanvasWidth;
        m_fStaticStampCanvasH         = m_fCanvasHeight;
        m_sStaticStampRoadHash        = m_sTerrainRoadHash;
        m_iStaticStampRoadCount       = m_iTerrainRoadCount;
        m_sStaticStampStructHash      = m_sTerrainStructureHash;
        m_iStaticStampStructCount     = m_iTerrainStructureCount;
        m_iStaticStampOverlayMask     = overlayMask;
        m_iStaticStampSatRevision     = m_iSatelliteRevision;
        m_bStaticStampTextureLoaded   = m_bTextureLoaded;
        m_bStaticStampValid           = true;
        m_bStaticDirty                = false;
        m_fSinceStaticRebuild         = 0;
    }

    //! Seconds since the last static rebuild. Driven from the controller so the keepalive is
    //! wall-clock rather than frame-count, and so a frontend being ticked at 5 Hz by the
    //! visibility gate still measures real time between rebuilds. The controller advances it
    //! only on live frames — see the suspend branch in UpdateMapView.
    void AdvanceStaticClock(float tDelta)
    {
        m_fSinceStaticRebuild += tDelta;
    }

    void Draw()
    {
        if (!m_wCanvas)
            return;

        // Refreshed every frame rather than only at Init: whichever view is actually
        // drawing is the one a keybind should act on, and a view that stops drawing
        // stops claiming the slot on the next frame the live one paints. A passive
        // mirror is excluded: it draws unconditionally and would hold the slot forever.
        if (!m_bPassive)
            s_ActiveView = this;

        // Measured before the hosting block, not after it. Every consumer of canvas-relative
        // projection — IsReady, WorldToLayout, the follower's bloodhound readout — needs a
        // non-zero width, and the surfaces that predate the mirror always painted 2D for a
        // frame before they could host, so they were never measured only inside the 2D path.
        // A mirror inherits an already-open view and can host from its very first frame, where
        // Init's own GetScreenSize ran against a widget that had not been laid out yet.
        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);

        if (TickHostedPane())
            return;

	    // Guard against zero dimensions (widget not yet laid out)
	    if (m_fCanvasHeight <= 0 || m_fCanvasWidth <= 0)
	        return;

        Build2DFrame();
    }

    //------------------------------------------------------------------------------------------------
    //! Host arbitration + tick for the 3D pane. Split out of Draw() because a frontend whose
    //! 2D painting has been gated off by the visibility check STILL has to run this every
    //! frame: RequestHost doubles as this surface's liveness heartbeat, and a surface that
    //! stops sending it is treated as dead — the pane would be handed to the next-ranked
    //! contender (the HUD peripheral) mid-session, and view.Tick() would stop entirely.
    //! Gating pixels must not gate the pane's lifecycle.
    //!
    //! Returns true when the pane took this surface, i.e. the caller must not build 2D commands.
    bool TickHostedPane()
    {
        if (!m_wCanvas)
            return false;

        // While the 3D map is up on THIS surface its pane is opaque and covers this
        // canvas entirely — rebuilding the 2D command list underneath it costs a full
        // projection pass per frame for pixels nobody can see. A surface the 3D map
        // refuses (RequestHost false) falls through and keeps its 2D map: the pane
        // lives on the other surface, and blanking this one too would leave the
        // operator staring at nothing.
        // A passive mirror contends too, but from the bottom tier — it inherits the pane only
        // once no real surface is alive to hold it, which is exactly the case where it is the
        // only thing still on screen. It never toggles 3D itself (s_ActiveView is still
        // menu/device only), so this is inheritance, not a fourth way to enter 3D.
        if (AG0_TDLMap3DView.IsViewOpen())
        {
            AG0_TDLMap3DView view = AG0_TDLMap3DView.GetInstance();
            Widget host = m_wCanvas.GetParent();
            if (!host)
                host = m_wCanvas;

            // Arbitrated, not unconditional: the old per-frame Rehost let two live
            // surfaces steal the pane from each other every frame. RequestHost also
            // serves as this surface's liveness heartbeat.
            if (view && view.RequestHost(host, GetHostRank()))
            {
                // The pane owns the canvas from here, so whatever command list we last
                // submitted is gone. Dropping the cache means the frame we fall back to
                // 2D rebuilds from scratch instead of Resize()-ing down into a prefix the
                // canvas is no longer holding.
                InvalidateStaticCache();

                view.Tick();

                // Shapes render on the drape, not the suppressed 2D canvas — hand over
                // the same per-frame data DrawShapes would have read. The controller
                // refreshed m_aShapes before this call and the draw session keeps
                // m_GhostShape current, so the 3D map sees exactly what 2D would.
                view.UpdateDrapeShapes(m_aShapes, m_GhostShape);
                view.SetBloodhound(m_bBloodhoundEnabled, m_vBloodhoundCursor,
                    m_vBloodhoundDevice);

                // The 2D centre rides the 3D orbit focus: crosshair placement, sweep
                // delete and the bloodhound's menu cursor all read GetCenter as "the
                // point under the middle of the view", and while the pane is up that
                // point is the focus, not wherever the hidden 2D view was left. Side
                // benefit: leaving 3D drops the 2D map on whatever the operator was
                // orbiting. Clamped like every other centre write.
                vector focusWorld = view.GetFocusWorld();
                m_vCenterWorld = Vector(focusWorld[0], 0, focusWorld[2]);
                ClampCenterToBounds();
                return true;
            }
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Called instead of Draw() on any frame a frontend is not painting — whether it is fully
    //! suspended or just between ticks of a throttled cadence. Keeps the canvas measurement
    //! current (IsReady / ScreenToWorld / WorldToLayout are queried every frame by the cursor
    //! and bloodhound paths regardless of paint cadence) and keeps the 3D pane's host heartbeat
    //! alive. Deliberately does NOT claim s_ActiveView: a surface nobody can see should not be
    //! the one a keybind acts on.
    void TickSuspended(float tDelta)
    {
        if (!m_wCanvas)
            return;

        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);
        TickHostedPane();

        // Nobody is watching, so nobody is panning. Banking motion credit across a suspension
        // would decimate the first frame after the operator looks back.
        m_iRoadMotionStreak = 0;

        // The engine reaps a canvas's CanvasWidgetCommand array after a few minutes without a
        // submit — see the note on the 30 s image-canvas re-Draw in AG0_TDLMenuController,
        // whose remedy is specifically to RECONSTRUCT the commands rather than re-submit them.
        // This path never submits, so a long park could otherwise wake straight onto the
        // Resize-reuse branch and hand the canvas a prefix the engine has already let go.
        m_fSinceSubmit += tDelta;
        if (m_fSinceSubmit >= STATIC_RESUBMIT_MAX_IDLE)
            MarkStaticDirty();
    }

    //------------------------------------------------------------------------------------------------
    //! The 2D command build. Split from Draw() only so the hosting/measurement preamble above
    //! stays readable; behaviour is identical to the pre-split single pass.
    protected void Build2DFrame()
    {
        // Runs before the gate, not inside the rebuild: it can flip m_bTextureLoaded and
        // m_iSatelliteRevision, and both are stamp inputs the gate is about to read.
        RefreshSatelliteTextureIfStale();

        int overlayMask = ComputeOverlayMask();

        // Computed once and passed in rather than queried inside the gate, because the road
        // pass needs the same answer: "the pose moved since the cached prefix was built" is
        // exactly the condition under which decimating road geometry is invisible.
        bool poseMoved = StaticPoseMoved();
        bool rebuildStatic = ShouldRebuildStatic(overlayMask, poseMoved);

        // Suppresses the per-point GetHostedMap3DView() probe inside WorldToScreen for the
        // whole pass. Reaching here means this surface is NOT hosting the pane.
        m_bIn2DDrawPass = true;

        // Sustained-motion score. Reuse frames count as still — if we are reusing the prefix,
        // the pose by definition has not moved.
        if (poseMoved)
            m_iRoadMotionStreak = m_iRoadMotionStreak + 1;
        else
            m_iRoadMotionStreak = m_iRoadMotionStreak - ROAD_MOTION_STREAK_DECAY;

        m_iRoadMotionStreak = Math.ClampInt(m_iRoadMotionStreak, 0, ROAD_MOTION_STREAK_MAX);

        if (rebuildStatic)
        {
            // Decimate road vertices only during a SUSTAINED pan or zoom, and only when zoomed
            // out far enough that consecutive vertices are a pixel or two apart. The streak
            // requirement is what keeps slow drift and first-build (empty stamp) out of it.
            m_bRoadDecimating = m_iRoadMotionStreak >= ROAD_MOTION_STREAK_FRAMES
                             && m_fZoom > ROAD_MOTION_MIN_ZOOM;
            m_bRoadDecimationApplied = false;

            m_aDrawCommands.Clear();

            BuildStaticCommands();

            m_iStaticCommandCount = m_aDrawCommands.Count();
            CommitStaticStamp(overlayMask);

            // The pass's own report, not the intent — see m_bRoadDecimationApplied.
            m_bLastStaticWasDecimated = m_bRoadDecimationApplied;
            m_bRoadDecimating = false;
        }
        else
        {
            // Keep the cached static prefix, drop last frame's dynamic tail. Resize() down
            // releases the tail's command refs and leaves the prefix objects untouched —
            // no reallocation, and not one road vertex re-projected.
            m_aDrawCommands.Resize(m_iStaticCommandCount);
        }

        BuildDynamicCommands();

        m_bIn2DDrawPass = false;

        // Submit draw commands
        m_wCanvas.SetDrawCommands(m_aDrawCommands);
        m_fSinceSubmit = 0;
    }

    //------------------------------------------------------------------------------------------------
    //! Everything that depends only on pose (centre/zoom/rotation), canvas size and the
    //! streamed datasets. This is the expensive half and the half the dirty gate protects.
    //! Order is unchanged from the original Draw().
    protected void BuildStaticCommands()
    {
        if (m_bTextureLoaded && m_pMapTexture)
            DrawMapTexture();
        else
            DrawFallbackBackground();

        // Draw overlay layers (structures, roads, water, contours)
        DrawOverlays();

        // Mask the 8 ghost tiles produced by the texture sampler's Repeat mode
        // when the view extends past the map's world extent. Runs AFTER the
        // satellite + overlay draws (both use the same unclamped UV math and
        // therefore both tile), and BEFORE buildings/shapes/markers so on-map
        // content is not occluded. No-op when we have no satellite loaded
        // (DrawFallbackBackground already fills the whole canvas in that case).
        if (m_bTextureLoaded && m_pMapTexture)
            DrawMapEdgeMask();

        // Roads first so they sit under buildings (real-world layering).
        DrawApiTerrainRoads();

        // Building draw priority:
        //   1. Baked structure overlay texture (set in MapSatelliteConfig) — drawn above
        //   2. API-streamed terrain structures (from /api/mod/terrain/structures)
        // The legacy runtime MapDescriptorComponent query has been removed; the
        // API path is authoritative and the per-frame entity query was both more
        // expensive and less accurate (and shared the rotation bug fixed below).
        if (!m_bHasStructureOverlay)
            DrawApiTerrainStructures();

		//Draw grid (over buildings)
		DrawGrid();
    }

    //------------------------------------------------------------------------------------------------
    //! The cheap tail: everything driven by things that genuinely change every tick —
    //! member positions, the draw-session ghost, the cursor. Rebuilt unconditionally and
    //! appended after the (possibly cached) static prefix, so the final command order is
    //! byte-identical to what the single-pass Draw() produced.
    protected void BuildDynamicCommands()
    {
		//Draw TDL shapes
		DrawShapes();

        // Draw markers (on top)
        DrawMarkers();

        // Bloodhound line — drawn after markers so the range/bearing stroke
        // sits visibly above terrain features, structures, and member markers.
        // No-op when the tool is disabled.
        if (m_bBloodhoundEnabled)
            DrawBloodhound();

        // Scale bar — last so it always sits on top of every other canvas
        // element. Screen-space only (does not rotate with track-up), which
        // matches the standard military/ATAK affordance of "this is a property
        // of the view, not the map".
        DrawScaleBar();

        // Grid legend — same reasoning as the scale bar, and it sits with it because the two
        // answer the same question: the zone and 100 km square are what turn a bare "05" on a
        // grid line into a reference somebody can read over the radio.
        DrawGridLegend();
    }

    //------------------------------------------------------------------------------------------------
    //! Push the bloodhound's per-frame state. The frontend computes the cursor
    //! world position (menu = map center, world-space = ScreenToWorld of the
    //! device cursor on the MapCanvas) and the device world position (player /
    //! gadget origin). DrawBloodhound() reads these on the next Draw().
    void SetBloodhound(bool enabled, vector cursorWorld, vector deviceWorld)
    {
        m_bBloodhoundEnabled = enabled;
        m_vBloodhoundCursor  = cursorWorld;
        m_vBloodhoundDevice  = deviceWorld;
    }

    //------------------------------------------------------------------------------------------------
    //! Project device + cursor to screen, extend from the device through the
    //! cursor to the canvas edge, and emit the line + endpoint tick.
    //!
    //! Why clip in screen space rather than at the world map boundary: the
    //! canvas edge is what the player can actually see, so terminating there
    //! gives a "line points off-screen toward the cursor's bearing" affordance
    //! that's visually unambiguous at every zoom level. Clipping at the world
    //! map AABB would either get hidden by the edge mask (DrawMapEdgeMask) or
    //! disappear when the cursor is well inside the visible viewport.
    protected void DrawBloodhound()
    {
        float dx, dy;
        float cx, cy;
        WorldToScreen(m_vBloodhoundDevice, dx, dy);
        WorldToScreen(m_vBloodhoundCursor, cx, cy);

        // Degenerate case — device == cursor (can happen on the menu when the
        // map is centered exactly on the player and tracking is on). Nothing
        // meaningful to draw; bail out.
        float ddx = cx - dx;
        float ddy = cy - dy;
        if (Math.AbsFloat(ddx) < 0.5 && Math.AbsFloat(ddy) < 0.5)
            return;

        // Compute the line's screen-space exit point — extend from the cursor
        // along (cursor - device) until we hit the canvas rect. Parameterise
        // as P(t) = cursor + t * (cursor - device) and solve for the smallest
        // positive t that intersects any of the four canvas edges.
        float exitX, exitY;
        ExtendLineToCanvasEdge(cx, cy, ddx, ddy, exitX, exitY);

        // Clip the device-side endpoint to the canvas rect too — when the
        // player is off-screen (zoomed in past the device's tile), starting
        // the stroke at dx,dy would draw outside the canvas. CanvasWidget
        // tolerates this but it costs a stroke segment that's not visible.
        // Use the same parametric extension in the reverse direction.
        float startX, startY;
        ClipDeviceEndpointToCanvas(dx, dy, cx, cy, startX, startY);

        // Main stroke: device → cursor → canvas edge (single segment so the
        // line looks continuous through the cursor intersection).
        LineDrawCommand mainLine = new LineDrawCommand();
        mainLine.m_iColor = BLOODHOUND_COLOR;
        mainLine.m_fWidth = BLOODHOUND_WIDTH;
        mainLine.m_Vertices = {startX, startY, exitX, exitY};
        m_aDrawCommands.Insert(mainLine);

        // Perpendicular tick at the cursor intersection. Normal = (-ddy, ddx)
        // normalised to BLOODHOUND_TICK_SIZE on each side.
        float len = Math.Sqrt(ddx * ddx + ddy * ddy);
        if (len <= 0)
            return;
        float nx = -ddy / len;
        float ny =  ddx / len;
        float tx0 = cx - nx * BLOODHOUND_TICK_SIZE;
        float ty0 = cy - ny * BLOODHOUND_TICK_SIZE;
        float tx1 = cx + nx * BLOODHOUND_TICK_SIZE;
        float ty1 = cy + ny * BLOODHOUND_TICK_SIZE;

        LineDrawCommand tick = new LineDrawCommand();
        tick.m_iColor = BLOODHOUND_COLOR;
        tick.m_fWidth = BLOODHOUND_WIDTH;
        tick.m_Vertices = {tx0, ty0, tx1, ty1};
        m_aDrawCommands.Insert(tick);
    }

    //------------------------------------------------------------------------------------------------
    //! Extend P(t) = (cx,cy) + t * (dirX,dirY) outward (t >= 0) to the first
    //! canvas-edge intersection. Returns the intersection point in outX,outY.
    //! Assumes (dirX,dirY) is non-zero — the caller already guards against
    //! the degenerate device==cursor case.
    protected void ExtendLineToCanvasEdge(float cx, float cy, float dirX, float dirY, out float outX, out float outY)
    {
        // For each of the four edges, solve for the parametric t and keep the
        // smallest positive one. A safety upper bound prevents runaway when
        // the line is nearly parallel to an edge (zero or near-zero divisor).
        float bestT = 1e9;
        if (dirX > 0.0001)
        {
            float t = (m_fCanvasWidth - cx) / dirX;
            if (t > 0 && t < bestT) bestT = t;
        }
        else if (dirX < -0.0001)
        {
            float t = (0 - cx) / dirX;
            if (t > 0 && t < bestT) bestT = t;
        }
        if (dirY > 0.0001)
        {
            float t = (m_fCanvasHeight - cy) / dirY;
            if (t > 0 && t < bestT) bestT = t;
        }
        else if (dirY < -0.0001)
        {
            float t = (0 - cy) / dirY;
            if (t > 0 && t < bestT) bestT = t;
        }
        // Fallback for pathological cases — clamp to canvas diagonal length.
        if (bestT >= 1e9)
            bestT = 2.0;
        outX = cx + dirX * bestT;
        outY = cy + dirY * bestT;
    }

    //------------------------------------------------------------------------------------------------
    //! If the device-side endpoint (dx,dy) is inside the canvas, return it as-is.
    //! Otherwise, walk backwards from the cursor along (device-cursor) until we
    //! hit the canvas edge — that becomes the new start. Symmetric to
    //! ExtendLineToCanvasEdge but in the opposite direction.
    protected void ClipDeviceEndpointToCanvas(float dx, float dy, float cx, float cy, out float outX, out float outY)
    {
        if (dx >= 0 && dx <= m_fCanvasWidth && dy >= 0 && dy <= m_fCanvasHeight)
        {
            outX = dx;
            outY = dy;
            return;
        }
        float rdx = dx - cx;
        float rdy = dy - cy;
        if (Math.AbsFloat(rdx) < 0.5 && Math.AbsFloat(rdy) < 0.5)
        {
            outX = cx;
            outY = cy;
            return;
        }
        ExtendLineToCanvasEdge(cx, cy, rdx, rdy, outX, outY);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawMapTexture()
	{
	    ImageDrawCommand cmd = new ImageDrawCommand();
	    cmd.m_pTexture = m_pMapTexture;
	    
	    // Calculate view size in world units at current zoom
	    float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom;
	    float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
	    
	    // Diagonal of the view rectangle (covers any rotation)
	    float diagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ);
	    
	    // Sample square region sized to diagonal, centered on view center
	    float viewMinX = m_vCenterWorld[0] - diagonal * 0.5;
	    float viewMaxX = m_vCenterWorld[0] + diagonal * 0.5;
	    float viewMinZ = m_vCenterWorld[2] - diagonal * 0.5;
	    float viewMaxZ = m_vCenterWorld[2] + diagonal * 0.5;
	    
	    // Convert to UVs
	    float u0, v0, u1, v1;
	    WorldToUV(Vector(viewMinX, 0, viewMaxZ), u0, v0);
	    WorldToUV(Vector(viewMaxX, 0, viewMinZ), u1, v1);
	    
	    cmd.m_fUV[0] = u0;
	    cmd.m_fUV[1] = v0;
	    cmd.m_fUV[2] = u1;
	    cmd.m_fUV[3] = v1;
	    
	    // Draw size matches the diagonal in screen space
	    float pixelsPerWorldUnit = m_fCanvasWidth / viewWorldSizeX;
	    float drawSize = diagonal * pixelsPerWorldUnit;
	    float halfDraw = drawSize * 0.5;
	    
	    // Pivot at origin - we'll manually compute offset so rotated center lands at canvas center
	    float rotRad = m_fRotation * Math.DEG2RAD;
	    float cosR = Math.Cos(rotRad);
	    float sinR = Math.Sin(rotRad);
	    
	    // Image center is at (halfDraw, halfDraw) from pivot
	    // After rotation around pivot (0,0), center moves to:
	    //   x' = halfDraw * cos - halfDraw * sin
	    //   y' = halfDraw * sin + halfDraw * cos
	    // We want position + rotatedCenter = canvasCenter
	    float offsetX = (m_fCanvasWidth * 0.5) - halfDraw * (cosR - sinR);
	    float offsetY = (m_fCanvasHeight * 0.5) - halfDraw * (sinR + cosR);
	    
	    cmd.m_Position = Vector(offsetX, offsetY, 0);
	    cmd.m_Size = Vector(drawSize, drawSize, 0);
	    cmd.m_Pivot = Vector(0, 0, 0);
	    cmd.m_fRotation = m_fRotation;
	    cmd.m_iColor = 0xFFFFFFFF;
	    cmd.m_iFlags = WidgetFlags.STRETCH;
	    
	    m_aDrawCommands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
    //! Draw a single overlay texture using the same coordinate mapping as the satellite
    //! The overlay images are rasterized to the exact same world extent as the satellite,
    //! so they share identical UV mapping and transform math.
    protected void DrawOverlay(SharedItemRef texture, float opacity)
    {
        if (!texture)
            return;
        
        ImageDrawCommand cmd = new ImageDrawCommand();
        cmd.m_pTexture = texture;
        
        // --- Same coordinate math as DrawMapTexture() ---
        float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
        float viewWorldSizeX = m_fMapSizeX * m_fZoom;
        float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
        
        float diagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ);
        
        float viewMinX = m_vCenterWorld[0] - diagonal * 0.5;
        float viewMaxX = m_vCenterWorld[0] + diagonal * 0.5;
        float viewMinZ = m_vCenterWorld[2] - diagonal * 0.5;
        float viewMaxZ = m_vCenterWorld[2] + diagonal * 0.5;
        
        float u0, v0, u1, v1;
        WorldToUV(Vector(viewMinX, 0, viewMaxZ), u0, v0);
        WorldToUV(Vector(viewMaxX, 0, viewMinZ), u1, v1);
        
        cmd.m_fUV[0] = u0;
        cmd.m_fUV[1] = v0;
        cmd.m_fUV[2] = u1;
        cmd.m_fUV[3] = v1;
        
        float pixelsPerWorldUnit = m_fCanvasWidth / viewWorldSizeX;
        float drawSize = diagonal * pixelsPerWorldUnit;
        float halfDraw = drawSize * 0.5;
        
        float rotRad = m_fRotation * Math.DEG2RAD;
        float cosR = Math.Cos(rotRad);
        float sinR = Math.Sin(rotRad);
        
        float offsetX = (m_fCanvasWidth * 0.5) - halfDraw * (cosR - sinR);
        float offsetY = (m_fCanvasHeight * 0.5) - halfDraw * (sinR + cosR);
        
        cmd.m_Position = Vector(offsetX, offsetY, 0);
        cmd.m_Size = Vector(drawSize, drawSize, 0);
        cmd.m_Pivot = Vector(0, 0, 0);
        cmd.m_fRotation = m_fRotation;
        
        // Apply opacity via color alpha channel (ARGB)
        int alpha = Math.ClampInt(opacity * 255, 0, 255);
        cmd.m_iColor = (alpha << 24) | 0x00FFFFFF;
        
        cmd.m_iFlags = WidgetFlags.STRETCH | WidgetFlags.BLEND;
        
        m_aDrawCommands.Insert(cmd);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Draw all enabled overlay layers in order
    protected void DrawOverlays()
    {
        for (int i = 0; i < m_aOverlayTextures.Count(); i++)
        {
            if (!m_aOverlayEnabled[i])
                continue;
            
            DrawOverlay(m_aOverlayTextures[i], m_aOverlayOpacities[i]);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Draws 8 opaque map-sized polygons tiled in the world positions immediately
    //! N, NE, E, SE, S, SW, W, NW of the real map. These cover the ghost satellite
    //! and overlay tiles produced by the texture sampler's default Repeat wrap mode
    //! when the diagonal-sized sample rect in DrawMapTexture/DrawOverlay extends
    //! past the map's world extent (particularly visible on small display surfaces
    //! like the wrist tablet and Garmin GPS mods).
    //!
    //! Every corner is transformed individually through WorldToScreen so the mask
    //! inherits the exact rotation/zoom/Y-flip pipeline as the underlying view.
    //! This sidesteps the sign mismatches that arise from rotating screen-space
    //! corners around a tile center (tiles appeared to "drift" with rotation when
    //! we did that, because screen-space rotation direction doesn't match the
    //! combined WorldToScreen rotation + Y-flip). With this approach the mask's
    //! inner edge lands on the real map's outer edge at any rotation.
    protected void DrawMapEdgeMask()
    {
        // World-space geometry of the real map.
        float mapCenterX = m_fMapOffsetX + m_fMapSizeX * 0.5;
        float mapCenterZ = m_fMapOffsetY + m_fMapSizeY * 0.5;
        float halfSizeX = m_fMapSizeX * 0.5;
        float halfSizeZ = m_fMapSizeY * 0.5;

        // Early-out when no ghost tile can be on screen. The sampler only wraps where the
        // view's diagonal sample rect reaches past the map's world extent, so a view sitting
        // comfortably inside the terrain has nothing to mask — and unconditionally emitting
        // 8 map-sized polygons plus 32 projections for that case is pure waste at every zoom
        // level except the most zoomed-out ones.
        //
        // Half-diagonal because the sample rect is rotation-safe: DrawMapTexture sizes it to
        // the view diagonal so a rotated map never samples outside the quad, and this test has
        // to use the same reach or it would clear the mask while a corner still wraps.
        float viewWorldSizeX = m_fMapSizeX * m_fZoom;
        float viewWorldSizeZ = viewWorldSizeX;
        if (m_fCanvasWidth > 0)
            viewWorldSizeZ = viewWorldSizeX * (m_fCanvasHeight / m_fCanvasWidth);

        float halfDiagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ) * 0.5;

        bool reachesEdge =
               (m_vCenterWorld[0] - halfDiagonal) < m_fMapOffsetX
            || (m_vCenterWorld[0] + halfDiagonal) > (m_fMapOffsetX + m_fMapSizeX)
            || (m_vCenterWorld[2] - halfDiagonal) < m_fMapOffsetY
            || (m_vCenterWorld[2] + halfDiagonal) > (m_fMapOffsetY + m_fMapSizeY);

        if (!reachesEdge)
            return;

        // Unrotated corner offsets from a tile's world-space center, in world units.
        // Order: SW, SE, NE, NW — counter-clockwise in world coords. (Screen winding
        // after the Y-flip inside WorldToScreen ends up clockwise, matching the
        // convention used by every other PolygonDrawCommand in this class.)
        array<float> cornerOffsetX = {-halfSizeX,  halfSizeX, halfSizeX, -halfSizeX};
        array<float> cornerOffsetZ = {-halfSizeZ, -halfSizeZ, halfSizeZ,  halfSizeZ};

        // Color matches DrawFallbackBackground so the off-map region reads the same
        // whether the satellite loaded or not.
        const int MASK_COLOR = 0xFF1A1A1A;

        // Iterate the 3x3 neighborhood, skipping the center (the real map).
        // Zoom is clamped to [m_fMinZoom, m_fMaxZoom=1.0] and the sampler's
        // diagonal sample rect is at most sqrt(2)*mapSize, so only first-ring
        // ghosts are ever visible — 8 tiles is sufficient.
        for (int j = -1; j <= 1; j++)
        {
            for (int k = -1; k <= 1; k++)
            {
                if (k == 0 && j == 0)
                    continue; // Center tile is the real map; do not occlude it.

                float tileCenterX = mapCenterX + k * m_fMapSizeX;
                float tileCenterZ = mapCenterZ + j * m_fMapSizeY;

                array<float> verts = {};
                for (int i = 0; i < 4; i++)
                {
                    vector worldCorner = Vector(
                        tileCenterX + cornerOffsetX[i],
                        0,
                        tileCenterZ + cornerOffsetZ[i]
                    );

                    float sx, sy;
                    WorldToScreen(worldCorner, sx, sy);
                    verts.Insert(sx);
                    verts.Insert(sy);
                }

                PolygonDrawCommand mask = new PolygonDrawCommand();
                mask.m_iColor = MASK_COLOR;
                mask.m_Vertices = verts;
                m_aDrawCommands.Insert(mask);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void DrawFallbackBackground()
    {
        // Dark background when texture isn't available
        PolygonDrawCommand bg = new PolygonDrawCommand();
        bg.m_iColor = 0xFF1A1A1A;
        bg.m_Vertices = {
            0, 0,
            m_fCanvasWidth, 0,
            m_fCanvasWidth, m_fCanvasHeight,
            0, m_fCanvasHeight
        };
        m_aDrawCommands.Insert(bg);
    }
	
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	//! Pick the road LOD band for the current zoom, with hysteresis.
	//!
	//! Written as four independent one-way transitions rather than a chain of else-ifs so a
	//! zoom jump straight across both thresholds (a zoom-to-fit, or a mirror pulling a new
	//! pose) lands on the right band in one call instead of stepping one band per frame.
	protected void UpdateRoadLodBand()
	{
	    float z = m_fZoom;

	    // Shedding more detail — only past the far side of the dead-band.
	    if (m_iRoadLodBand < 1 && z > ROAD_LOD_SHED_TRAILS_ZOOM + ROAD_LOD_HYSTERESIS)
	        m_iRoadLodBand = 1;
	    if (m_iRoadLodBand < 2 && z > ROAD_LOD_SHED_PAVED_ZOOM + ROAD_LOD_HYSTERESIS)
	        m_iRoadLodBand = 2;

	    // Restoring detail — only past the near side.
	    if (m_iRoadLodBand > 1 && z < ROAD_LOD_SHED_PAVED_ZOOM - ROAD_LOD_HYSTERESIS)
	        m_iRoadLodBand = 1;
	    if (m_iRoadLodBand > 0 && z < ROAD_LOD_SHED_TRAILS_ZOOM - ROAD_LOD_HYSTERESIS)
	        m_iRoadLodBand = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Lowest road priority that draws in the current band. Priorities are 3=highway,
	//! 2=paved, 1=trail; anything below 1 is treated as a trail.
	protected int RoadMinPriority()
	{
	    if (m_iRoadLodBand >= 2)
	        return 3;
	    if (m_iRoadLodBand >= 1)
	        return 2;

	    // Band 0 means draw EVERYTHING, so the floor has to be below any priority the wire can
	    // carry — not 1. m_iPriority is copied verbatim from the payload with no clamp
	    // (AG0_TDLTerrainRoadManager: feat.m_iPriority = pr[f]), and the style switch's default
	    // arm already treats anything below 1 as a trail. Returning 1 here would have silently
	    // culled an entire unclassified road layer at every zoom, with no log line to say so.
	    return -1000000;
	}

	//------------------------------------------------------------------------------------------------
	//! Draw road network from /api/mod/terrain/roads.
	//!
	//! Each AG0_TDLTerrainRoadFeature is a polyline. Consecutive on-screen segments are
	//! emitted as ONE multi-vertex LineDrawCommand rather than one command per segment
	//! (see ROAD_BATCH_SEGMENTS), and at interior vertices of thick roads we drop a small
	//! filled circle ("round join") so segments meeting at an angle share a continuous
	//! outline instead of leaving a gap on the outside of the bend.
	//!
	//! Performance. Roads are the one pass with no natural size cull: stroke width is
	//! floored to minStroke, so a road is NEVER dropped for being too thin to read the way
	//! a sub-pixel building is. At full zoom-out that means the entire network, every
	//! rebuild. Four things bound it, cheapest first:
	//!   1. Per-frame world-space viewport AABB, computed once.
	//!   2. Per-feature AABB reject — no WorldToScreen at all for off-screen features.
	//!   3. Per-segment screen-space reject for partially-on-screen roads.
	//!   4. Zoom-band LOD, motion decimation, run batching and a hard command budget —
	//!      all four added together; see the constants above each.
	protected void DrawApiTerrainRoads()
	{
	    if (!m_aTerrainRoads || m_aTerrainRoads.IsEmpty())
	        return;

	    UpdateRoadLodBand();
	    int minPriority = RoadMinPriority();

	    float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);

	    // World-space viewport AABB (rotation-safe via the diagonal-sized square,
	    // same trick DrawMapTexture uses for satellite UV sampling). Off-screen
	    // by less than ROAD_CULL_MARGIN world meters still draws so wide roads
	    // crossing the edge don't pop in.
	    float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom;
	    float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
	    float diagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ);
	    const float ROAD_CULL_MARGIN = 50.0;
	    float viewMinX = m_vCenterWorld[0] - diagonal * 0.5 - ROAD_CULL_MARGIN;
	    float viewMaxX = m_vCenterWorld[0] + diagonal * 0.5 + ROAD_CULL_MARGIN;
	    float viewMinZ = m_vCenterWorld[2] - diagonal * 0.5 - ROAD_CULL_MARGIN;
	    float viewMaxZ = m_vCenterWorld[2] + diagonal * 0.5 + ROAD_CULL_MARGIN;

	    // Work budget, counted in vertices plus joins. The LOD band is the primary bound; this
	    // is the backstop that makes the worst case PROVABLE rather than merely unlikely — a dataset denser than any we
	    // have tested, or a zoom band that turns out to be tuned wrong, still cannot run away.
	    //
	    // Per-class caps rather than one pool: without them a network with thousands of trails
	    // would spend the whole budget on trails before reaching a single highway, and the map
	    // would lose exactly the features an operator navigates by. Highways are uncapped and
	    // therefore take whatever the lower classes leave.
	    int capTrail = ROAD_VERTEX_BUDGET / 4;
	    int capPaved = ROAD_VERTEX_BUDGET / 3;
	    int emittedTotal = 0;
	    int emittedTrail = 0;
	    int emittedPaved = 0;

	    // Vertex decimation while the pose is moving. m_bRoadDecimating is set per static
	    // rebuild in Build2DFrame from the motion score (see ROAD_MOTION_STREAK_FRAMES), so it
	    // means "the operator has been panning or zooming for a sustained run of rebuilds" —
	    // not merely "this one rebuild saw movement". Straightened curves are not
	    // perceptible at pan speed, and the
	    // frame after motion stops is forced to rebuild at full detail (see
	    // m_bLastStaticWasDecimated in ShouldRebuildStatic) so the settled map is never
	    // left decimated.
	    int vertexStep = 1;
	    if (m_bRoadDecimating)
	    {
	        // Clamped: a stride of zero would leave the polyline walk unable to advance and
	        // hang the game thread, and this constant sits next to ones the comments invite
	        // tuning. A hard lock is not an acceptable failure mode for a tuning typo.
	        vertexStep = ROAD_MOTION_VERTEX_STEP;
	        if (vertexStep < 1)
	            vertexStep = 1;
	    }

	    foreach (AG0_TDLTerrainRoadFeature road : m_aTerrainRoads)
	    {
	        if (!road)
	            continue;

	        // Zoom-band LOD. Checked before anything else: it is one integer compare and at
	        // zoom-out it is what removes most of the network.
	        if (road.m_iPriority < minPriority)
	            continue;

	        if (emittedTotal >= ROAD_VERTEX_BUDGET)
	            break;

	        int rawCount = road.m_aPoints.Count();
	        if (rawCount < 4)
	            continue; // need at least 2 points (4 floats)

	        // World-space AABB-vs-viewport reject. Skips the whole feature
	        // before any per-vertex work.
	        if (road.m_fMaxX < viewMinX || road.m_fMinX > viewMaxX ||
	            road.m_fMaxZ < viewMinZ || road.m_fMinZ > viewMaxZ)
	            continue;

	        // Per-class budget. Deliberately after the culls so an off-screen trail does not
	        // consume trail budget that an on-screen one needs.
	        if (road.m_iPriority <= 1 && emittedTrail >= capTrail)
	            continue;
	        if (road.m_iPriority == 2 && emittedPaved >= capPaved)
	            continue;

	        // Style by priority: 3=highway thickest/lightest, 1=trail thinnest/dimmest.
	        // Stroke is the road's actual width in world meters scaled to screen,
	        // floored so it stays readable when zoomed out.
	        float baseWidthPx = road.m_fWidth * pixelsPerWorldUnit;
	        float minStroke;
	        int color;
	        switch (road.m_iPriority)
	        {
	            case 3:
	                minStroke = 2.5;
	                color = 0xFFE8C57A; // warm tan — highways
	                break;
	            case 2:
	                minStroke = 1.8;
	                color = 0xFFC9B98A; // muted sand — paved/road
	                break;
	            default:
	                minStroke = 1.2;
	                color = 0xFF9C8964; // dim olive — trails
	                break;
	        }
	        float stroke = Math.Max(baseWidthPx, minStroke);

	        // Round-join radius — half the stroke width covers the gap on the
	        // outside of the bend exactly.
	        //
	        // Threshold raised from 1 px to ROAD_JOIN_MIN_RADIUS: with run batching a bend
	        // inside a single polyline command no longer leaves a per-segment seam, so joins
	        // only earn their cost on genuinely thick roads. They are also skipped entirely
	        // while decimating — a 1-2 px seam is not perceptible mid-pan, and joins are one
	        // tessellated polygon each, which makes them the single largest command source in
	        // a dense network.
	        // With batching OFF the raised threshold would be a behaviour change of its own —
	        // every minStroke floor yields a join radius under 2.0, so highways would lose the
	        // joins they always had. Tying it to the flag keeps ROAD_BATCH_SEGMENTS a genuine
	        // one-line revert rather than a half-revert that trades a zigzag for notched bends.
	        float joinMinRadius = ROAD_JOIN_MIN_RADIUS;
	        if (!ROAD_BATCH_SEGMENTS)
	            joinMinRadius = 1.0;

	        float joinRadius = stroke * 0.5;
	        bool drawJoins = (joinRadius >= joinMinRadius) && !m_bRoadDecimating;
	        // Adaptive segment count — small circles get fewer triangles.
	        int joinSegments = 6;
	        if (joinRadius >= 4.0) joinSegments = 10;
	        if (joinRadius >= 8.0) joinSegments = 14;

	        // The remaining budget is handed down, not just checked between features: a single
	        // pathological 20,000-vertex feature would otherwise blow straight through the cap
	        // in one call, since the loop above only re-checks at the next feature.
	        //
	        // Clamped to the CLASS remainder, not just the pool. The per-class caps above only
	        // gate entry to a feature, so handing down the whole pool would let one long merged
	        // trail polyline return the entire budget and starve every paved road and highway
	        // behind it — the exact failure the caps exist to prevent, made easier to hit by
	        // counting vertices instead of commands.
	        int avail = ROAD_VERTEX_BUDGET - emittedTotal;
	        if (road.m_iPriority <= 1)
	            avail = Math.Min(avail, capTrail - emittedTrail);
	        else if (road.m_iPriority == 2)
	            avail = Math.Min(avail, capPaved - emittedPaved);

	        int added = EmitRoadPolyline(road, rawCount, vertexStep, stroke, color, joinRadius,
	            joinSegments, drawJoins, avail);

	        // Reported by the pass rather than assumed from the intent flag: a rebuild in which
	        // every road was culled by the LOD band or the viewport AABB has not decimated
	        // anything, and must not force a full rebuild of satellite, overlays, structures
	        // and grid once the pan ends.
	        if (vertexStep > 1 && added > 0)
	            m_bRoadDecimationApplied = true;

	        emittedTotal += added;
	        if (road.m_iPriority <= 1)
	            emittedTrail += added;
	        else if (road.m_iPriority == 2)
	            emittedPaved += added;
	    }
	}

	//------------------------------------------------------------------------------------------------
	//! Walk one road's polyline and emit its visible geometry.
	//!
	//! The old version emitted one two-point LineDrawCommand per segment, so a 60-vertex road
	//! cost 59 command objects and 59 four-float arrays every rebuild — and per the map audit
	//! the script-heap allocation, not the GPU, is what actually hurts here. This accumulates
	//! CONTIGUOUS visible segments into a single multi-vertex command and only breaks the run
	//! where a segment is culled, which is the same shape the GRS renderer uses.
	//! Returns the number of budget units emitted: one per polyline vertex, one per join.
	protected int EmitRoadPolyline(AG0_TDLTerrainRoadFeature road, int rawCount, int vertexStep,
	                               float stroke, int color, float joinRadius, int joinSegments,
	                               bool drawJoins, int budgetRemaining)
	{
	    // Index of the first float of the LAST point. Points are (x, z) pairs, so this is the
	    // largest even index with a partner — matching the old loop's `i + 1 < rawCount` bound
	    // for both even and odd rawCount.
	    int lastIndex = rawCount - 2;
	    if (lastIndex % 2 != 0)
	        lastIndex = lastIndex - 1;
	    if (lastIndex < 2)
	        return 0;

	    if (budgetRemaining <= 0)
	        return 0;

	    int units = 0;
	    int stride = 2 * vertexStep;

	    array<float> run = null;
	    float prevSX, prevSY;
	    bool havePrev = false;
	    bool prevSegmentDrawn = false;

	    int i = 0;
	    bool done = false;
	    while (!done)
	    {
	        // The final vertex is always visited, whatever the stride lands on. Without this a
	        // decimated road would render visibly short of its own end, which reads as the
	        // network breaking up rather than as a level of detail.
	        // Checked at the top, not only after a successful emit: the emit-side check lives
	        // inside `if (segVisible)`, so a feature that runs off-screen after exhausting its
	        // budget would otherwise keep paying a WorldToScreen per remaining vertex — and the
	        // per-vertex projection cost is the thing this pass is being bounded for.
	        if (units >= budgetRemaining)
	            break;

	        if (i >= lastIndex)
	        {
	            i = lastIndex;
	            done = true;
	        }

	        float sx, sy;
	        WorldToScreen(Vector(road.m_aPoints[i], 0, road.m_aPoints[i + 1]), sx, sy);

	        if (havePrev)
	        {
	            // Per-segment screen-space reject (handles long polylines
	            // partially on-screen).
	            bool offLeft   = (prevSX < -20 && sx < -20);
	            bool offRight  = (prevSX > m_fCanvasWidth + 20 && sx > m_fCanvasWidth + 20);
	            bool offTop    = (prevSY < -20 && sy < -20);
	            bool offBottom = (prevSY > m_fCanvasHeight + 20 && sy > m_fCanvasHeight + 20);

	            bool segVisible = !(offLeft || offRight || offTop || offBottom);

	            if (segVisible)
	            {
	                if (!run)
	                {
	                    run = {};
	                    run.Insert(prevSX);
	                    run.Insert(prevSY);
	                    units = units + 1;   // the run's opening vertex counts too
	                }
	                run.Insert(sx);
	                run.Insert(sy);
	                units = units + 1;

	                // One-line revert: with batching off this flushes after every segment, so
	                // the emitted commands are identical to the pre-batching behaviour —
	                // including the line-then-join ordering below, which is why the flush sits
	                // above the join and not after it. Kept because nothing else in this mod
	                // feeds a LineDrawCommand more than two points: if the engine turns out to
	                // treat m_Vertices as disconnected pairs rather than a polyline, roads will
	                // render as a zigzag and this constant is the fix.
	                if (!ROAD_BATCH_SEGMENTS)
	                {
	                    FlushRoadRun(run, color, stroke);
	                    run = null;
	                }

	                // Round-join at the SHARED vertex between this segment
	                // and the previous one (i.e. at prevSX/prevSY), but only
	                // if there actually was a previous segment drawn — no
	                // join at the very first endpoint of the polyline.
	                if (drawJoins && prevSegmentDrawn)
	                {
	                    array<float> joinVerts = {};
	                    TessellateCircle(prevSX, prevSY, joinRadius, joinSegments, joinVerts);
	                    PolygonDrawCommand join = new PolygonDrawCommand();
	                    join.m_iColor = color;
	                    join.m_Vertices = joinVerts;
	                    m_aDrawCommands.Insert(join);
	                    units = units + 1;
	                }

	                if (units >= budgetRemaining)
	                {
	                    FlushRoadRun(run, color, stroke);
	                    return units;
	                }
	            }
	            else
	            {
	                // Break in visibility ends the run.
	                FlushRoadRun(run, color, stroke);
	                run = null;
	            }

	            prevSegmentDrawn = segVisible;
	        }

	        prevSX = sx;
	        prevSY = sy;
	        havePrev = true;
	        i += stride;
	    }

	    FlushRoadRun(run, color, stroke);
	    return units;
	}

	//------------------------------------------------------------------------------------------------
	//! Emit an accumulated run as one polyline command. No-op for null or a run shorter than a
	//! single segment, so callers can flush unconditionally.
	protected void FlushRoadRun(array<float> run, int color, float stroke)
	{
	    if (!run || run.Count() < 4)
	        return;

	    LineDrawCommand line = new LineDrawCommand();
	    line.m_iColor = color;
	    line.m_fWidth = stroke;
	    line.m_Vertices = run;
	    m_aDrawCommands.Insert(line);
	}

	//------------------------------------------------------------------------------------------------
	//! Draw building footprints sourced from /api/mod/terrain/structures (rect mode).
	//!
	//! Each AG0_TDLTerrainStructureRecord describes an oriented rectangle:
	//!   center (m_fCenterX, m_fCenterZ), size (m_fWidth, m_fDepth), rotation (m_fRotation, radians).
	//!
	//! Rotation convention: the API delivers radians using compass-CCW-from-north
	//! (same as Math.Atan2(forward.X, forward.Z)). Screen space here is Y-down,
	//! so a math-CCW rotation in screen coords visually reads as CW after the
	//! per-vertex Y flip below. We compensate by negating the world rotation when
	//! computing the rotation matrix — equivalent to rotating in the flipped
	//! coordinate space directly. This is the fix for the "rotations look right
	//! at certain headings, wrong at others" symptom in track mode that the
	//! legacy runtime building path also suffered from.
	//!
	//! Cull aggressively — Everon-scale datasets can be many thousands of buildings
	//! and we redraw every frame. World-space AABB reject before WorldToScreen
	//! avoids the sin/cos for any building outside the viewport.
	protected void DrawApiTerrainStructures()
	{
	    if (!m_aTerrainStructures || m_aTerrainStructures.IsEmpty())
	        return;

	    float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);
	    float mapRotRad = m_fRotation * Math.DEG2RAD;

	    // Sub-pixel rejection threshold, hoisted out of the loop and inverted into world units.
	    // The old test computed halfW/halfL in SCREEN pixels and therefore ran AFTER
	    // WorldToScreen — so every building inside the viewport paid a full projection before
	    // being thrown away for being too small to see. The test depends only on the building's
	    // size and the zoom, never on its position:
	    //     halfW < 1  <=>  m_fWidth * 0.5 * pixelsPerWorldUnit < 1
	    //                <=>  m_fWidth < 2 / pixelsPerWorldUnit
	    // so it is a per-frame constant and the reject becomes two float compares.
	    //
	    // This is the win that matters at zoom-out, which is where the map fell over: at low
	    // zoom nearly every building is sub-pixel, and they now cost two compares each instead
	    // of an AABB test plus a projection.
	    //
	    // A non-positive scale rejects EVERYTHING, matching the old behaviour: halfW would have
	    // been <= 0, hence < 1. Rejecting nothing there would emit a degenerate colinear quad
	    // per record and trip the triangulator spam the OR-test below exists to prevent.
	    float minWorldExtent = float.MAX;
	    if (pixelsPerWorldUnit > 0)
	        minWorldExtent = 2.0 / pixelsPerWorldUnit;

	    // Per-frame world-space viewport AABB for cheap per-building rejection
	    // (saves the WorldToScreen sin/cos for off-screen features). Diagonal-
	    // sized to be rotation-safe, same trick as DrawApiTerrainRoads and
	    // DrawMapTexture's UV sampling.
	    float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom;
	    float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
	    float diagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ);
	    const float STRUCT_CULL_MARGIN = 50.0;
	    float viewMinX = m_vCenterWorld[0] - diagonal * 0.5 - STRUCT_CULL_MARGIN;
	    float viewMaxX = m_vCenterWorld[0] + diagonal * 0.5 + STRUCT_CULL_MARGIN;
	    float viewMinZ = m_vCenterWorld[2] - diagonal * 0.5 - STRUCT_CULL_MARGIN;
	    float viewMaxZ = m_vCenterWorld[2] + diagonal * 0.5 + STRUCT_CULL_MARGIN;

	    foreach (AG0_TDLTerrainStructureRecord rec : m_aTerrainStructures)
	    {
	        if (!rec)
	            continue;

	        // Sub-pixel reject FIRST: two compares, no projection, and at zoom-out it rejects
	        // almost everything. Still OR (not AND): a building with one axis sub-pixel is a
	        // colinear strip after rotation, which trips the engine's polygon triangulator
	        // ("DrawPolygon triangulation failed" log spam every frame, FPS hit). Tradeoff is
	        // invisible-at-zoom-out buildings cull a frame earlier; they weren't readable at
	        // that scale anyway.
	        if (rec.m_fWidth < minWorldExtent || rec.m_fDepth < minWorldExtent)
	            continue;

	        // World-space cull. Half-extent bound = (w + d) * 0.5 is a tiny bit
	        // larger than the true half-diagonal sqrt(w² + d²)/2 (since
	        // (w+d)² >= w² + d²) and avoids a per-feature sqrt.
	        float halfBound = (rec.m_fWidth + rec.m_fDepth) * 0.5;
	        if (rec.m_fCenterX + halfBound < viewMinX ||
	            rec.m_fCenterX - halfBound > viewMaxX ||
	            rec.m_fCenterZ + halfBound < viewMinZ ||
	            rec.m_fCenterZ - halfBound > viewMaxZ)
	            continue;

	        // World center → screen (handles map rotation, zoom, and Y-flip for position)
	        vector worldCenter = Vector(rec.m_fCenterX, 0, rec.m_fCenterZ);
	        float centerX, centerY;
	        WorldToScreen(worldCenter, centerX, centerY);

	        // Half-extents in screen pixels (API delivers full width/depth).
	        float halfW = rec.m_fWidth * 0.5 * pixelsPerWorldUnit;
	        float halfL = rec.m_fDepth * 0.5 * pixelsPerWorldUnit;

	        // Total rotation: see comment above for the negation rationale.
	        // Building rotation is world-CCW; map rotation is also world-CCW (track
	        // mode rotates the world frame). Combine then negate to take both into
	        // the Y-flipped screen frame in one step.
	        //
	        // The +π/2 offset compensates for a 90° mismatch between the API's
	        // rotation reference and the renderer's local-rect axis convention.
	        // If the structures rotate the *wrong* direction after this fix,
	        // change `+ Math.PI * 0.5` to `- Math.PI * 0.5`.
	        float totalRot = -(rec.m_fRotation + Math.PI * 0.5 + mapRotRad);
	        float cosR = Math.Cos(totalRot);
	        float sinR = Math.Sin(totalRot);

	        // Local corners (unrotated, screen-space sized)
	        array<float> localX = {-halfW,  halfW, halfW, -halfW};
	        array<float> localY = {-halfL, -halfL, halfL,  halfL};

	        // Rotated fill verts. Y-flip at output to match screen coords.
	        array<float> verts = {};
	        for (int i = 0; i < 4; i = i + 1)
	        {
	            float rotX = localX[i] * cosR - localY[i] * sinR;
	            float rotY = localX[i] * sinR + localY[i] * cosR;
	            verts.Insert(centerX + rotX);
	            verts.Insert(centerY - rotY);
	        }

	        // Outline polygon (slightly expanded)
	        float outlineOffset = 1.5;
	        array<float> outLocalX = {-halfW - outlineOffset,  halfW + outlineOffset, halfW + outlineOffset, -halfW - outlineOffset};
	        array<float> outLocalY = {-halfL - outlineOffset, -halfL - outlineOffset, halfL + outlineOffset,  halfL + outlineOffset};

	        array<float> outlineVerts = {};
	        for (int j = 0; j < 4; j = j + 1)
	        {
	            float rotX = outLocalX[j] * cosR - outLocalY[j] * sinR;
	            float rotY = outLocalX[j] * sinR + outLocalY[j] * cosR;
	            outlineVerts.Insert(centerX + rotX);
	            outlineVerts.Insert(centerY - rotY);
	        }

	        PolygonDrawCommand outline = new PolygonDrawCommand();
	        outline.m_iColor = 0xFF000000;
	        outline.m_Vertices = outlineVerts;
	        m_aDrawCommands.Insert(outline);

	        PolygonDrawCommand fill = new PolygonDrawCommand();
	        fill.m_iColor = m_iBuildingColor;
	        fill.m_Vertices = verts;
	        m_aDrawCommands.Insert(fill);
	    }
	}

	// -----------------------------------------------------------------------
	// ADAPTIVE SEGMENT COUNT
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	//! Calculate optimal polygon segment count for a circle at a given screen radius.
	//! Uses the inscribed polygon error formula: N = π / arccos(1 - ε/r)
	//! where ε is the maximum pixel error tolerance (0.5px).
	//! Clamped to [8, 128] — 8 segments for tiny circles, up to 128 for huge ones.
	protected int GetAdaptiveSegments(float screenRadius)
	{
		if (screenRadius < 2)
			return 8;
		
		// Lookup-based approximation — avoids Math.Acos dependency.
		// These thresholds match the exact formula to within ±1 segment.
		if (screenRadius < 5)   return 8;
		if (screenRadius < 15)  return 12;
		if (screenRadius < 40)  return 20;
		if (screenRadius < 100) return 32;
		if (screenRadius < 250) return 48;
		if (screenRadius < 500) return 72;
		return 128;
	}
	
	// -----------------------------------------------------------------------
	// MAIN DISPATCH
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	protected void DrawShapes()
	{
		bool hasCommitted = m_aShapes && !m_aShapes.IsEmpty();
		if (!hasCommitted && !m_GhostShape)
			return;

		float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);

		if (hasCommitted)
		{
			foreach (AG0_TDLMapShape shape : m_aShapes)
			{
				DrawSingleShape(shape, pixelsPerWorldUnit);
			}
		}

		// Ghost goes last so the in-progress draw always reads on top of any
		// committed shapes that overlap the same region.
		if (m_GhostShape)
			DrawSingleShape(m_GhostShape, pixelsPerWorldUnit);
	}

	//------------------------------------------------------------------------------------------------
	//! Dispatch one shape to its type-specific renderer. Extracted from
	//! DrawShapes so the ghost-shape preview reuses the exact same code paths.
	protected void DrawSingleShape(AG0_TDLMapShape shape, float pixelsPerWorldUnit)
	{
		if (!shape)
			return;

		float screenX, screenY;
		WorldToScreen(shape.m_vCenter, screenX, screenY);

		float boundingScreenR = shape.GetBoundingRadius() * pixelsPerWorldUnit;
		float margin = boundingScreenR + 20;

		if (screenX + margin < 0 || screenX - margin > m_fCanvasWidth)
			return;
		if (screenY + margin < 0 || screenY - margin > m_fCanvasHeight)
			return;

		switch (shape.m_eShapeType)
		{
			case AG0_ETDLShapeType.CIRCLE:
				DrawShapeCircle(shape, screenX, screenY, pixelsPerWorldUnit);
				break;

			case AG0_ETDLShapeType.SECTOR:
				DrawShapeSector(shape, screenX, screenY, pixelsPerWorldUnit);
				break;

			case AG0_ETDLShapeType.RANGE_RINGS:
				DrawShapeRangeRings(shape, screenX, screenY, pixelsPerWorldUnit);
				break;

			case AG0_ETDLShapeType.RECTANGLE:
			case AG0_ETDLShapeType.POLYGON:
				DrawShapePolygon(shape, pixelsPerWorldUnit);
				break;

			case AG0_ETDLShapeType.FREEHAND:
				DrawShapeFreehand(shape, pixelsPerWorldUnit);
				break;

			case AG0_ETDLShapeType.ROUTE:
				DrawShapeRoute(shape, pixelsPerWorldUnit);
				break;
		}
	}
	
	// -----------------------------------------------------------------------
	// CIRCLE
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	protected void DrawShapeCircle(AG0_TDLMapShape shape, float cx, float cy, float ppu)
	{
		float screenRadius = shape.m_fRadius * ppu;
		if (screenRadius < 1)
			return;
		
		int segments = GetAdaptiveSegments(screenRadius);
		
		array<float> verts = {};
		TessellateCircle(cx, cy, screenRadius, segments, verts);
		
		// Fill
		if (shape.m_iFillColor != 0)
		{
			PolygonDrawCommand fill = new PolygonDrawCommand();
			fill.m_iColor = shape.m_iFillColor;
			fill.m_Vertices = verts;
			m_aDrawCommands.Insert(fill);
		}
		
		// Stroke
		DrawClosedStroke(verts, shape.m_iStrokeColor, shape.m_fStrokeWidth);
		
		// Label
		DrawShapeLabel(shape, cx, cy);
	}
	
	// -----------------------------------------------------------------------
	// SECTOR (fan/wedge — weapon engagement zones, sensor FOV)
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	protected void DrawShapeSector(AG0_TDLMapShape shape, float cx, float cy, float ppu)
	{
		float screenRadius = shape.m_fRadius * ppu;
		if (screenRadius < 1)
			return;
		
		int fullCircleSegs = GetAdaptiveSegments(screenRadius);
		
		// Convert bearings (0=North, CW) to screen angles.
		// Screen coords: +X=right, +Y=down.
		// Bearing 0 (N) → up on screen → angle -π/2 in standard math.
		// Bearing 90 (E) → right → angle 0.
		// Formula: screenAngle = (bearing - 90) degrees
		// Map rotation shifts all bearings on screen.
		float startDeg = shape.m_fStartAngle - 90.0 + m_fRotation;
		float endDeg   = shape.m_fEndAngle   - 90.0 + m_fRotation;
		
		float startRad = startDeg * Math.DEG2RAD;
		float endRad   = endDeg   * Math.DEG2RAD;
		
		// Sweep — always positive (CW in bearing = CW on screen with Y-down)
		float sweep = endRad - startRad;
		if (sweep <= 0)
			sweep += Math.PI * 2;
		
		// Arc segment count proportional to sweep fraction of full circle
		int arcSegs = Math.Ceil(sweep / (Math.PI * 2) * fullCircleSegs);
		if (arcSegs < 4)  arcSegs = 4;
		if (arcSegs > 128) arcSegs = 128;
		
		// Build vertex list: center → arc → (implicit close back to center)
		array<float> verts = {};
		verts.Insert(cx);
		verts.Insert(cy);
		
		for (int i = 0; i <= arcSegs; i++)
		{
			float angle = startRad + sweep * i / arcSegs;
			verts.Insert(cx + Math.Cos(angle) * screenRadius);
			verts.Insert(cy + Math.Sin(angle) * screenRadius);
		}
		
		// Fill (PolygonDrawCommand auto-closes last vertex to first)
		if (shape.m_iFillColor != 0)
		{
			PolygonDrawCommand fill = new PolygonDrawCommand();
			fill.m_iColor = shape.m_iFillColor;
			fill.m_Vertices = verts;
			m_aDrawCommands.Insert(fill);
		}
		
		// Stroke — radial line from center to arc start
		LineDrawCommand radial1 = new LineDrawCommand();
		radial1.m_iColor = shape.m_iStrokeColor;
		radial1.m_fWidth = shape.m_fStrokeWidth;
		radial1.m_Vertices = {cx, cy, verts[2], verts[3]};
		m_aDrawCommands.Insert(radial1);
		
		// Stroke — arc segments
		int vertCount = verts.Count();
		for (int i = 2; i < vertCount - 2; i += 2)
		{
			LineDrawCommand arcLine = new LineDrawCommand();
			arcLine.m_iColor = shape.m_iStrokeColor;
			arcLine.m_fWidth = shape.m_fStrokeWidth;
			arcLine.m_Vertices = {verts[i], verts[i + 1], verts[i + 2], verts[i + 3]};
			m_aDrawCommands.Insert(arcLine);
		}
		
		// Stroke — radial line from arc end back to center
		LineDrawCommand radial2 = new LineDrawCommand();
		radial2.m_iColor = shape.m_iStrokeColor;
		radial2.m_fWidth = shape.m_fStrokeWidth;
		radial2.m_Vertices = {verts[vertCount - 2], verts[vertCount - 1], cx, cy};
		m_aDrawCommands.Insert(radial2);
		
		// Label — positioned at visual center of the fan (half-radius along bisector)
		if (!shape.m_sLabel.IsEmpty())
		{
			float midAngle = startRad + sweep * 0.5;
			float labelDist = screenRadius * 0.5;
			float labelX = cx + Math.Cos(midAngle) * labelDist;
			float labelY = cy + Math.Sin(midAngle) * labelDist;
			DrawShapeLabel(shape, labelX, labelY);
		}
	}
	
	// -----------------------------------------------------------------------
	// RANGE RINGS (concentric circles + center crosshair)
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	protected void DrawShapeRangeRings(AG0_TDLMapShape shape, float cx, float cy, float ppu)
	{
		// Draw each ring as stroke-only
		foreach (float ringRadius : shape.m_aRings)
		{
			float screenR = ringRadius * ppu;
			if (screenR < 1)
				continue;
			
			int segments = GetAdaptiveSegments(screenR);
			
			array<float> verts = {};
			TessellateCircle(cx, cy, screenR, segments, verts);
			
			DrawClosedStroke(verts, shape.m_iStrokeColor, shape.m_fStrokeWidth);
		}
		
		// Center crosshair
		float crossSize = 6;
		
		LineDrawCommand hLine = new LineDrawCommand();
		hLine.m_iColor = shape.m_iStrokeColor;
		hLine.m_fWidth = 1;
		hLine.m_Vertices = {cx - crossSize, cy, cx + crossSize, cy};
		m_aDrawCommands.Insert(hLine);
		
		LineDrawCommand vLine = new LineDrawCommand();
		vLine.m_iColor = shape.m_iStrokeColor;
		vLine.m_fWidth = 1;
		vLine.m_Vertices = {cx, cy - crossSize, cx, cy + crossSize};
		m_aDrawCommands.Insert(vLine);
		
		// Distance labels at north cardinal of each ring
		foreach (float ringRadius : shape.m_aRings)
		{
			float screenR = ringRadius * ppu;
			if (screenR < 20)
				continue; // Too small for a label
			
			// Format distance: show meters or km
			string distLabel;
			if (ringRadius >= 1000)
				distLabel = string.Format("%1km", Math.Round(ringRadius / 100) * 0.1);
			else
				distLabel = string.Format("%1m", Math.Round(ringRadius));
			
			// North cardinal point (screen Y-up is negative)
			DrawTextLabel(distLabel, cx, cy - screenR, 9, SHAPE_LABEL_TEXT_COLOR);
		}
		
		// Main shape label at center
		DrawShapeLabel(shape, cx, cy);
	}
	
	// -----------------------------------------------------------------------
	// POLYGON / RECTANGLE / FREEHAND (vertex-based closed shapes)
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	protected void DrawShapePolygon(AG0_TDLMapShape shape, float ppu)
	{
		int rawCount = shape.m_aVertices.Count();
		if (rawCount < 4)
			return; // Need at least 2 vertex pairs

		array<float> screenVerts = {};
		float minX = float.MAX, minY = float.MAX;
		float maxX = -float.MAX, maxY = -float.MAX;
		for (int i = 0; i + 1 < rawCount; i += 2)
		{
			float sx, sy;
			WorldToScreen(Vector(shape.m_aVertices[i], 0, shape.m_aVertices[i + 1]), sx, sy);
			screenVerts.Insert(sx);
			screenVerts.Insert(sy);
			if (sx < minX) minX = sx;
			if (sy < minY) minY = sy;
			if (sx > maxX) maxX = sx;
			if (sy > maxY) maxY = sy;
		}

		// Fill (need 3+ vertices = 6+ floats for a meaningful polygon).
		// Also skip when the projected polygon collapses below ~1 pixel in either
		// axis at the current zoom — at that point the vertices are effectively
		// colinear in screen space and the engine's triangulator fails with
		// "DrawPolygon triangulation failed, is the polygon degenerated?" spam.
		// Threshold of 2 px gives a small safety margin; at this scale the fill
		// would be invisible anyway, so dropping it costs nothing visually but
		// stops the per-frame error log (which itself was costing FPS).
		float spanX = maxX - minX;
		float spanY = maxY - minY;
		if (shape.m_iFillColor != 0 && screenVerts.Count() >= 6 && spanX >= 2.0 && spanY >= 2.0)
		{
			PolygonDrawCommand fill = new PolygonDrawCommand();
			fill.m_iColor = shape.m_iFillColor;
			fill.m_Vertices = screenVerts;
			m_aDrawCommands.Insert(fill);
		}
		
		// Stroke — closed perimeter
		DrawClosedStroke(screenVerts, shape.m_iStrokeColor, shape.m_fStrokeWidth);
		
		// Label at centroid
		float centroidX, centroidY;
		WorldToScreen(shape.m_vCenter, centroidX, centroidY);
		DrawShapeLabel(shape, centroidX, centroidY);
	}
	
	// -----------------------------------------------------------------------
	// FREEHAND (open continuous stroke — pen-like drawing)
	// -----------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Render freehand as an open polyline along the sampled cursor path —
	//! no closing segment, no fill, no waypoint decoration. This is what
	//! distinguishes a "drawing" gesture from a polygon's closed region:
	//! the stroke ends where the pen lifted, the shape is the line itself
	//! rather than the area it encloses.
	protected void DrawShapeFreehand(AG0_TDLMapShape shape, float ppu)
	{
		int rawCount = shape.m_aVertices.Count();
		if (rawCount < 4)
			return;

		array<float> screenVerts = {};
		for (int i = 0; i + 1 < rawCount; i += 2)
		{
			float sx, sy;
			WorldToScreen(Vector(shape.m_aVertices[i], 0, shape.m_aVertices[i + 1]), sx, sy);
			screenVerts.Insert(sx);
			screenVerts.Insert(sy);
		}

		DrawOpenStroke(screenVerts, shape.m_iStrokeColor, shape.m_fStrokeWidth);

		if (!shape.m_sLabel.IsEmpty())
		{
			float centroidX, centroidY;
			WorldToScreen(shape.m_vCenter, centroidX, centroidY);
			DrawShapeLabel(shape, centroidX, centroidY);
		}
	}

	// -----------------------------------------------------------------------
	// ROUTE (open polyline with waypoint dots)
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	protected void DrawShapeRoute(AG0_TDLMapShape shape, float ppu)
	{
		int rawCount = shape.m_aVertices.Count();
		if (rawCount < 4)
			return;

		array<float> screenVerts = {};
		for (int i = 0; i + 1 < rawCount; i += 2)
		{
			float sx, sy;
			WorldToScreen(Vector(shape.m_aVertices[i], 0, shape.m_aVertices[i + 1]), sx, sy);
			screenVerts.Insert(sx);
			screenVerts.Insert(sy);
		}

		// Route line — open (not closed)
		DrawOpenStroke(screenVerts, shape.m_iStrokeColor, shape.m_fStrokeWidth);
		
		// Waypoint dots at each vertex
		int waypointIdx = 0;
		for (int i = 0; i + 1 < screenVerts.Count(); i += 2)
		{
			float wpX = screenVerts[i];
			float wpY = screenVerts[i + 1];
			
			// Outline circle
			array<float> outVerts = {};
			TessellateCircle(wpX, wpY, 5, 8, outVerts);
			
			PolygonDrawCommand wpOutline = new PolygonDrawCommand();
			wpOutline.m_iColor = m_iMarkerOutlineColor;
			wpOutline.m_Vertices = outVerts;
			m_aDrawCommands.Insert(wpOutline);
			
			// Filled circle
			array<float> wpVerts = {};
			TessellateCircle(wpX, wpY, 3.5, 8, wpVerts);
			
			PolygonDrawCommand wpFill = new PolygonDrawCommand();
			wpFill.m_iColor = shape.m_iStrokeColor;
			wpFill.m_Vertices = wpVerts;
			m_aDrawCommands.Insert(wpFill);
			
			// Per-waypoint label (from m_aWaypointLabels, offset above the dot)
			if (shape.m_aWaypointLabels && waypointIdx < shape.m_aWaypointLabels.Count())
			{
				string wpLabel = shape.m_aWaypointLabels[waypointIdx];
				if (!wpLabel.IsEmpty())
					DrawTextLabel(wpLabel, wpX, wpY - 10, 9, SHAPE_LABEL_TEXT_COLOR);
			}
			waypointIdx++;
		}
		
		// Direction arrows along route segments
		int pointCount = screenVerts.Count() / 2;
		for (int i = 0; i < pointCount - 1; i++)
		{
			float x0 = screenVerts[i * 2];
			float y0 = screenVerts[i * 2 + 1];
			float x1 = screenVerts[(i + 1) * 2];
			float y1 = screenVerts[(i + 1) * 2 + 1];
			
			// Midpoint
			float mx = (x0 + x1) * 0.5;
			float my = (y0 + y1) * 0.5;
			
			// Segment direction
			float dx = x1 - x0;
			float dy = y1 - y0;
			float len = Math.Sqrt(dx * dx + dy * dy);
			
			if (len < 20)
				continue; // Skip arrow on very short segments
			
			// Normalize
			dx = dx / len;
			dy = dy / len;
			
			// Arrow head — triangle pointing along the segment
			float arrowLen = 6;
			float arrowHalf = 3;
			
			// Tip at midpoint + half arrow length along direction
			float tipX = mx + dx * arrowLen;
			float tipY = my + dy * arrowLen;
			
			// Two base points perpendicular to direction
			float perpX = -dy;
			float perpY = dx;
			
			float baseX = mx - dx * arrowLen;
			float baseY = my - dy * arrowLen;
			
			array<float> arrowVerts = {
				tipX, tipY,
				baseX + perpX * arrowHalf, baseY + perpY * arrowHalf,
				baseX - perpX * arrowHalf, baseY - perpY * arrowHalf
			};
			
			PolygonDrawCommand arrow = new PolygonDrawCommand();
			arrow.m_iColor = shape.m_iStrokeColor;
			arrow.m_Vertices = arrowVerts;
			m_aDrawCommands.Insert(arrow);
		}
		
		// Main shape label at route midpoint
		if (!shape.m_sLabel.IsEmpty() && screenVerts.Count() >= 4)
		{
			int midIdx = (screenVerts.Count() / 2) & ~1; // Snap to even index (x,y pair)
			if (midIdx + 1 < screenVerts.Count())
				DrawShapeLabel(shape, screenVerts[midIdx], screenVerts[midIdx + 1]);
		}
	}
	
	// -----------------------------------------------------------------------
	// SHAPE LABEL HELPERS
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	//! Draw a text label centered over a shape's screen position.
	//! Renders a semi-transparent background pill behind the text for readability.
	//! Skips drawing if the shape has no label.
	protected void DrawShapeLabel(AG0_TDLMapShape shape, float screenX, float screenY)
	{
		if (shape.m_sLabel.IsEmpty())
			return;
		
		DrawTextLabel(shape.m_sLabel, screenX, screenY, SHAPE_LABEL_SIZE, SHAPE_LABEL_TEXT_COLOR);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Draw a text label at an arbitrary screen position with a background pill.
	//! Used for shape labels and route waypoint labels.
	protected void DrawTextLabel(string text, float screenX, float screenY, float fontSize, int textColor)
	{
		if (text.IsEmpty())
			return;
		
		float textWidth = text.Length() * SHAPE_LABEL_CHAR_WIDTH;
		float halfW = textWidth * 0.5;
		float halfH = SHAPE_LABEL_HEIGHT * 0.5;
		
		// Background pill centered on (screenX, screenY)
		PolygonDrawCommand bg = new PolygonDrawCommand();
		bg.m_iColor = SHAPE_LABEL_BG_COLOR;
		bg.m_Vertices = {
			screenX - halfW - SHAPE_LABEL_PAD, screenY - halfH - SHAPE_LABEL_PAD,
			screenX + halfW + SHAPE_LABEL_PAD, screenY - halfH - SHAPE_LABEL_PAD,
			screenX + halfW + SHAPE_LABEL_PAD, screenY + halfH + SHAPE_LABEL_PAD,
			screenX - halfW - SHAPE_LABEL_PAD, screenY + halfH + SHAPE_LABEL_PAD
		};
		m_aDrawCommands.Insert(bg);
		
		// Text — position at top-left corner of where text should sit
		// TextDrawCommand renders from its position as the top-left origin
		TextDrawCommand cmd = new TextDrawCommand();
		cmd.m_sText = text;
		cmd.m_Position = Vector(screenX - halfW, screenY - halfH, 0);
		cmd.m_Pivot = Vector(0, 0, 0);
		cmd.m_iColor = textColor;
		cmd.m_fSize = fontSize;
		m_aDrawCommands.Insert(cmd);
	}

	// -----------------------------------------------------------------------
	// SCALE BAR
	// -----------------------------------------------------------------------
	//
	// Renders a dynamic distance scale in the bottom-left of the canvas. The
	// bar length is chosen from a 1-2-5 sequence so the label is always a
	// round number (200m, 500m, 1km, 2km, 5km, ...) regardless of zoom.
	//
	// Style: classic military/ATAK alternating black/white 4-segment bar with
	// end ticks and a label below, on a semi-transparent dark backdrop so it
	// stays legible over both bright satellite imagery and dark terrain.
	//
	// Screen-space — does not rotate with track-up mode. The bar represents
	// pixels-on-screen-to-meters-on-the-ground, which is invariant under
	// rotation but changes with zoom; matches what every paper map / nav
	// system does and what operators expect.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	//! Zone designator + 100 km square, parked just above the scale bar.
	//!
	//! Constant for the whole map, so repeating it on every readout would be noise — but without
	//! it somewhere on screen the grid line digits are unqualified and a player reading a
	//! reference off the map has nothing to prefix it with.
	protected void DrawGridLegend()
	{
		if (m_fCanvasWidth <= 0 || m_fCanvasHeight <= 0)
			return;

		string legend = AG0_MGRSGridUtils.GetGridSquareLegend();
		if (legend.IsEmpty())
			return;

		// Sits directly above the scale bar's backdrop. Both are anchored to the same left
		// margin so they read as one block of view metadata rather than two stray labels.
		const float LEGEND_MARGIN_BOTTOM = 132.0;
		const float LEGEND_SIZE = 15.0;

		float legendX = 120.0 + (legend.Length() * SHAPE_LABEL_CHAR_WIDTH * 0.5);
		float legendY = m_fCanvasHeight - LEGEND_MARGIN_BOTTOM;

		DrawTextLabel(legend, legendX, legendY, LEGEND_SIZE, SHAPE_LABEL_TEXT_COLOR);
	}

	//------------------------------------------------------------------------------------------------
	protected void DrawScaleBar()
	{
		// Guard against the first frame before LoadMapData() populated the
		// map extent, or a canvas that hasn't been laid out yet.
		if (m_fMapSizeX <= 0 || m_fZoom <= 0 || m_fCanvasWidth <= 0 || m_fCanvasHeight <= 0)
			return;

		float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);
		if (pixelsPerWorldUnit <= 0)
			return;

		// Target on-screen length. ~225 px reads cleanly without crowding
		// the ZoomControls (anchored left-center) or the SelfPanel (anchored
		// bottom-right). The nice-number step will push the actual width up
		// or down from here, so this is just the seed.
		const float TARGET_PIXELS = 225.0;
		float targetMeters = TARGET_PIXELS / pixelsPerWorldUnit;

		float niceMeters = PickNiceScaleMeters(targetMeters);
		float barPixels = niceMeters * pixelsPerWorldUnit;

		// Position — anchored to bottom-left of the canvas. Left margin is
		// pushed well inside the canvas so the bar clears the edge and any
		// nearby UI cleanly; bottom margin sits 90px up for safe clearance
		// from the canvas bottom edge / toolbar. Bar/label dimensions are
		// 1.5x the original so it reads well from a normal seated distance.
		const float MARGIN_LEFT = 120.0;
		const float MARGIN_BOTTOM = 90.0;
		const float BAR_HEIGHT = 12.0;
		const float TICK_EXTRA = 6.0;     // end-tick extension above + below the bar
		const float LABEL_GAP = 9.0;
		const float LABEL_SIZE = 18.0;
		const float LABEL_CHAR_WIDTH = 10.5; // rough px/char at LABEL_SIZE for backdrop width

		float x0 = MARGIN_LEFT;
		float y0 = m_fCanvasHeight - MARGIN_BOTTOM;
		float x1 = x0 + barPixels;
		float y1 = y0 + BAR_HEIGHT;

		string label = FormatScaleLabel(niceMeters);
		float labelTextW = label.Length() * LABEL_CHAR_WIDTH;

		// Backdrop pill — wide enough to cover the bar AND the label, with a
		// little padding so neither runs to the edge of the dark fill. Uses
		// the same SHAPE_LABEL_BG_COLOR pattern as DrawTextLabel above.
		const float BG_PAD_X = 12.0;
		const float BG_PAD_TOP = 9.0;
		const float BG_PAD_BOTTOM = 9.0;
		float bgRight;
		float barRightWithTick = x1 + BG_PAD_X;
		float labelRightWithPad = x0 + labelTextW + BG_PAD_X;
		if (barRightWithTick > labelRightWithPad)
			bgRight = barRightWithTick;
		else
			bgRight = labelRightWithPad;

		PolygonDrawCommand bg = new PolygonDrawCommand();
		bg.m_iColor = SHAPE_LABEL_BG_COLOR;
		bg.m_Vertices = {
			x0 - BG_PAD_X, y0 - TICK_EXTRA - BG_PAD_TOP,
			bgRight,        y0 - TICK_EXTRA - BG_PAD_TOP,
			bgRight,        y1 + TICK_EXTRA + LABEL_GAP + LABEL_SIZE + BG_PAD_BOTTOM,
			x0 - BG_PAD_X, y1 + TICK_EXTRA + LABEL_GAP + LABEL_SIZE + BG_PAD_BOTTOM
		};
		m_aDrawCommands.Insert(bg);

		// 4 alternating black/white segments.
		const int SEGMENTS = 4;
		int black = 0xFF000000;
		int white = 0xFFFFFFFF;
		float segWidth = barPixels / SEGMENTS;
		for (int i = 0; i < SEGMENTS; i = i + 1)
		{
			int segColor;
			if (i % 2 == 0)
				segColor = black;
			else
				segColor = white;

			float sx0 = x0 + (i * segWidth);
			float sx1 = sx0 + segWidth;

			PolygonDrawCommand seg = new PolygonDrawCommand();
			seg.m_iColor = segColor;
			seg.m_Vertices = {
				sx0, y0,
				sx1, y0,
				sx1, y1,
				sx0, y1
			};
			m_aDrawCommands.Insert(seg);
		}

		// Bar outline (top + bottom) — gives the white segments a defined
		// border against the dark backdrop.
		LineDrawCommand topEdge = new LineDrawCommand();
		topEdge.m_iColor = white;
		topEdge.m_fWidth = 1;
		topEdge.m_Vertices = {x0, y0, x1, y0};
		m_aDrawCommands.Insert(topEdge);

		LineDrawCommand botEdge = new LineDrawCommand();
		botEdge.m_iColor = white;
		botEdge.m_fWidth = 1;
		botEdge.m_Vertices = {x0, y1, x1, y1};
		m_aDrawCommands.Insert(botEdge);

		// End ticks — vertical strokes at both ends of the bar.
		LineDrawCommand leftTick = new LineDrawCommand();
		leftTick.m_iColor = white;
		leftTick.m_fWidth = 1;
		leftTick.m_Vertices = {x0, y0 - TICK_EXTRA, x0, y1 + TICK_EXTRA};
		m_aDrawCommands.Insert(leftTick);

		LineDrawCommand rightTick = new LineDrawCommand();
		rightTick.m_iColor = white;
		rightTick.m_fWidth = 1;
		rightTick.m_Vertices = {x1, y0 - TICK_EXTRA, x1, y1 + TICK_EXTRA};
		m_aDrawCommands.Insert(rightTick);

		// Label — below the bar, left-aligned with the bar's left edge.
		TextDrawCommand text = new TextDrawCommand();
		text.m_sText = label;
		text.m_Position = Vector(x0, y1 + TICK_EXTRA + LABEL_GAP, 0);
		text.m_Pivot = Vector(0, 0, 0);
		text.m_iColor = 0xFFFFFFFF;
		text.m_fSize = LABEL_SIZE;
		m_aDrawCommands.Insert(text);
	}

	//------------------------------------------------------------------------------------------------
	//! Round `targetMeters` to the NEAREST value in the 1-2-5 * 10^n sequence.
	//! Returns a round number so the scale label is always something like
	//! "500m" or "2km", never "473m".
	//!
	//! Why nearest-rounding and not floor: with floor, a target of ~900m
	//! lands on "500m" even though km of terrain is plainly visible on the
	//! canvas — looks wrong. Nearest-rounding lets values past the 7.5×
	//! mantissa threshold roll into the next decade (so target 900m → 1km,
	//! target 8km → 10km), matching the standard cartographic convention
	//! used on paper military maps and in ATAK.
	//!
	//! Magnitude is found by walking powers of 10 rather than Math.Log10
	//! to sidestep version-to-version variance in the engine's math lib
	//! and avoid floating-point edge cases at decade boundaries.
	protected float PickNiceScaleMeters(float targetMeters)
	{
		if (targetMeters <= 1)
			return 1;

		// Largest power of 10 not exceeding the target. Capped at 1e6 m
		// (1000 km) — far beyond any realistic Arma map.
		float magnitude = 1.0;
		int safety = 0;
		while (magnitude * 10 <= targetMeters && safety < 7)
		{
			magnitude = magnitude * 10;
			safety = safety + 1;
		}

		// Mantissa in [1, 10). Standard 1-2-5-10 nearest-rounding bands —
		// each threshold is the geometric midpoint of adjacent nice values
		// (sqrt(1*2)≈1.41, sqrt(2*5)≈3.16, sqrt(5*10)≈7.07), rounded to
		// readable cut-offs (1.5 / 3.5 / 7.5) so the behavior is obvious
		// at a glance.
		float mantissa = targetMeters / magnitude;
		float nice;
		if (mantissa < 1.5)
			nice = 1.0;
		else if (mantissa < 3.5)
			nice = 2.0;
		else if (mantissa < 7.5)
			nice = 5.0;
		else
			nice = 10.0;

		return nice * magnitude;
	}

	//------------------------------------------------------------------------------------------------
	//! Format a scale value for display. Switches to km at 1000m. All values
	//! we emit come from PickNiceScaleMeters's 1-2-5 sequence, so both the
	//! meter and kilometre branches always print whole numbers — no decimals
	//! to worry about. Matches the no-space "500m"/"1km" convention used by
	//! the shape ring distance labels above.
	protected string FormatScaleLabel(float meters)
	{
		if (meters >= 1000)
		{
			int km = Math.Floor(meters / 1000);
			return string.Format("%1km", km);
		}
		int m = Math.Floor(meters);
		return string.Format("%1m", m);
	}

	// -----------------------------------------------------------------------
	// STROKE HELPERS
	// -----------------------------------------------------------------------
	
	//------------------------------------------------------------------------------------------------
	//! Draw line segments connecting consecutive vertex pairs, closing back to first vertex.
	//! Used for circle perimeters, polygon outlines, etc.
	protected void DrawClosedStroke(array<float> verts, int color, float width)
	{
		int count = verts.Count();
		if (count < 4)
			return;
		
		// Segments between consecutive vertices
		for (int i = 0; i < count - 2; i += 2)
		{
			LineDrawCommand line = new LineDrawCommand();
			line.m_iColor = color;
			line.m_fWidth = width;
			line.m_Vertices = {verts[i], verts[i + 1], verts[i + 2], verts[i + 3]};
			m_aDrawCommands.Insert(line);
		}
		
		// Closing segment: last vertex → first vertex
		LineDrawCommand close = new LineDrawCommand();
		close.m_iColor = color;
		close.m_fWidth = width;
		close.m_Vertices = {verts[count - 2], verts[count - 1], verts[0], verts[1]};
		m_aDrawCommands.Insert(close);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Draw line segments connecting consecutive vertex pairs, open-ended (no closing segment).
	//! Used for routes.
	protected void DrawOpenStroke(array<float> verts, int color, float width)
	{
		int count = verts.Count();
		if (count < 4)
			return;
		
		for (int i = 0; i < count - 2; i += 2)
		{
			LineDrawCommand line = new LineDrawCommand();
			line.m_iColor = color;
			line.m_fWidth = width;
			line.m_Vertices = {verts[i], verts[i + 1], verts[i + 2], verts[i + 3]};
			m_aDrawCommands.Insert(line);
		}
	}
	
    
    //------------------------------------------------------------------------------------------------
    protected void DrawMarkers()
    {
        foreach (AG0_TDLMapMarker marker : m_aMarkers)
        {
            float screenX, screenY;
            WorldToScreen(marker.m_vWorldPos, screenX, screenY);

            if (screenX < -20 || screenX > m_fCanvasWidth + 20 ||
                screenY < -20 || screenY > m_fCanvasHeight + 20)
                continue;

            DrawMarker(marker, screenX, screenY);
        }
    }

	//------------------------------------------------------------------------------------------------
	protected void DrawGrid()
	{
	    // Calculate visible world bounds (use diagonal for rotation coverage)
	    float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom;
	    float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
	    float diagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ) * 0.5;
	    
	    float minX = m_vCenterWorld[0] - diagonal;
	    float maxX = m_vCenterWorld[0] + diagonal;
	    float minZ = m_vCenterWorld[2] - diagonal;
	    float maxZ = m_vCenterWorld[2] + diagonal;
	    
	    // Minor lines (100m) - only draw when zoomed in enough to see them
	    if (m_fZoom < 0.3)
	    {
	        float minorSpacing = 100.0;
	        int minorColor = 0x40000000; // Very transparent black
	        
	        DrawGridLines(minX, maxX, minZ, maxZ, minorSpacing, minorColor, 1.0);
	    }
	    
	    // Major lines (1000m) - always visible
	    float majorSpacing = 1000.0;
	    int majorColor = 0x60000000; // Semi-transparent black

	    DrawGridLines(minX, maxX, minZ, maxZ, majorSpacing, majorColor, 2.0);
	    DrawGridLabels(minX, maxX, minZ, maxZ, majorSpacing);
	}

	//------------------------------------------------------------------------------------------------
	//! Label each major grid line with its 1 km digits, at the point the line enters the canvas.
	//!
	//! Unlabelled grid lines make a map you can navigate but not report from — the whole point of
	//! a grid is being able to read a reference off it and say it out loud. Anchoring to where the
	//! line crosses the canvas edge is what a printed map sheet does, and it keeps working under
	//! track-up: in north-up the clip degenerates to "top edge and left edge", which is exactly
	//! where these belong.
	protected void DrawGridLabels(float minX, float maxX, float minZ, float maxZ, float spacing)
	{
	    float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);

	    // Below this the labels collide with each other and turn the edges into noise. The grid
	    // lines themselves stay — they still read as a grid without digits on them.
	    if (spacing * pixelsPerWorldUnit < GRID_LABEL_MIN_SPACING_PX)
	        return;

	    float startX = Math.Floor(minX / spacing) * spacing;
	    float startZ = Math.Floor(minZ / spacing) * spacing;

	    // Eastings — constant world X. Anchored at whichever end sits higher on screen, so in
	    // north-up they all read along the top edge.
	    for (float x = startX; x <= maxX; x += spacing)
	    {
	        float x0, y0, x1, y1;
	        WorldToScreen(Vector(x, 0, minZ), x0, y0);
	        WorldToScreen(Vector(x, 0, maxZ), x1, y1);

	        float cx0, cy0, cx1, cy1;
	        if (!ClipSegmentToCanvas(x0, y0, x1, y1, cx0, cy0, cx1, cy1))
	            continue;

	        if (cy1 < cy0)
	            PlaceGridLabel(AG0_MGRSGridUtils.GetKilometreGridDigits(x), cx1, cy1, cx0, cy0);
	        else
	            PlaceGridLabel(AG0_MGRSGridUtils.GetKilometreGridDigits(x), cx0, cy0, cx1, cy1);
	    }

	    // Northings — constant world Z. Anchored at whichever end sits further left.
	    for (float z = startZ; z <= maxZ; z += spacing)
	    {
	        float x0, y0, x1, y1;
	        WorldToScreen(Vector(minX, 0, z), x0, y0);
	        WorldToScreen(Vector(maxX, 0, z), x1, y1);

	        float cx0, cy0, cx1, cy1;
	        if (!ClipSegmentToCanvas(x0, y0, x1, y1, cx0, cy0, cx1, cy1))
	            continue;

	        if (cx1 < cx0)
	            PlaceGridLabel(AG0_MGRSGridUtils.GetKilometreGridDigits(z), cx1, cy1, cx0, cy0);
	        else
	            PlaceGridLabel(AG0_MGRSGridUtils.GetKilometreGridDigits(z), cx0, cy0, cx1, cy1);
	    }
	}

	//------------------------------------------------------------------------------------------------
	//! Place one grid label a fixed distance in from where its line enters the canvas.
	//!
	//! Walks ALONG the line rather than offsetting in screen X or Y. Under track-up a constant-X
	//! line can enter through a side edge, and a fixed downward offset then slides the label
	//! along that edge instead of into the canvas — which is how these ended up clipped. Moving
	//! down the line itself works for whichever edge was crossed, and keeps the label visibly
	//! attached to the line it belongs to.
	//!
	//! @param anchorX/anchorY Clipped entry point — the end the label reads from
	//! @param towardX/towardY The line's other clipped end, giving the inward direction
	protected void PlaceGridLabel(string text, float anchorX, float anchorY, float towardX, float towardY)
	{
	    float dx = towardX - anchorX;
	    float dy = towardY - anchorY;
	    float chord = Math.Sqrt(dx * dx + dy * dy);

	    // A line that only nicks a corner has nowhere legible to put a label, and walking the
	    // inset would overshoot its far end.
	    if (chord < GRID_LABEL_MIN_CHORD_PX)
	        return;

	    float step = GRID_LABEL_INSET_PX / chord;
	    float labelX = anchorX + dx * step;
	    float labelY = anchorY + dy * step;

	    // The pill is centred on the anchor, so its own half-extents plus a margin define how
	    // close to an edge that centre may sit. A corner entry can still leave a walked-in
	    // anchor within half a pill of the edge, so clamp as the final guarantee — by this
	    // point the label is already on the interior side, so the clamp nudges rather than
	    // detaching it from its line.
	    float halfW = (text.Length() * SHAPE_LABEL_CHAR_WIDTH) * 0.5 + SHAPE_LABEL_PAD + GRID_LABEL_EDGE_MARGIN_PX;
	    float halfH = SHAPE_LABEL_HEIGHT * 0.5 + SHAPE_LABEL_PAD + GRID_LABEL_EDGE_MARGIN_PX;

	    if (labelX < halfW)
	        labelX = halfW;
	    if (labelX > m_fCanvasWidth - halfW)
	        labelX = m_fCanvasWidth - halfW;
	    if (labelY < halfH)
	        labelY = halfH;
	    if (labelY > m_fCanvasHeight - halfH)
	        labelY = m_fCanvasHeight - halfH;

	    DrawTextLabel(text, labelX, labelY, GRID_LABEL_SIZE, GRID_LABEL_TEXT_COLOR);
	}

	//------------------------------------------------------------------------------------------------
	//! Liang-Barsky clip of a screen-space segment against the canvas rect.
	//!
	//! Grid lines are generated across the view diagonal so they still span the canvas when the
	//! map is rotated, which means both endpoints are normally well outside it. Finding where a
	//! line actually enters is therefore a clip, not a clamp — clamping would slide every label
	//! into a corner instead of leaving it attached to its own line.
	//!
	//! Returns false when the segment misses the canvas entirely.
	protected bool ClipSegmentToCanvas(float x0, float y0, float x1, float y1,
	    out float cx0, out float cy0, out float cx1, out float cy1)
	{
	    cx0 = x0;
	    cy0 = y0;
	    cx1 = x1;
	    cy1 = y1;

	    float dx = x1 - x0;
	    float dy = y1 - y0;

	    float tEnter = 0;
	    float tExit = 1;

	    // Edges in order: left, right, top, bottom. Written as an indexed loop with the p/q
	    // pair selected inline rather than a helper taking them by reference — an `inout float`
	    // on a script-side method has no precedent anywhere in this codebase, and no local
	    // arrays because this runs once per grid line per frame.
	    for (int edge = 0; edge < 4; edge++)
	    {
	        float p = 0;
	        float q = 0;
	        if (edge == 0)
	        {
	            p = -dx;
	            q = x0;
	        }
	        else if (edge == 1)
	        {
	            p = dx;
	            q = m_fCanvasWidth - x0;
	        }
	        else if (edge == 2)
	        {
	            p = -dy;
	            q = y0;
	        }
	        else
	        {
	            p = dy;
	            q = m_fCanvasHeight - y0;
	        }

	        if (p == 0)
	        {
	            // Parallel to this edge: inside only if it starts on the correct side.
	            if (q < 0)
	                return false;

	            continue;
	        }

	        float t = q / p;
	        if (p < 0)
	        {
	            if (t > tExit)
	                return false;
	            if (t > tEnter)
	                tEnter = t;
	        }
	        else
	        {
	            if (t < tEnter)
	                return false;
	            if (t < tExit)
	                tExit = t;
	        }
	    }

	    cx0 = x0 + dx * tEnter;
	    cy0 = y0 + dy * tEnter;
	    cx1 = x0 + dx * tExit;
	    cy1 = y0 + dy * tExit;
	    return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void DrawGridLines(float minX, float maxX, float minZ, float maxZ, float spacing, int color, float width)
	{
	    float startX = Math.Floor(minX / spacing) * spacing;
	    float startZ = Math.Floor(minZ / spacing) * spacing;
	    
	    // Vertical lines (constant X, vary Z)
	    for (float x = startX; x <= maxX; x += spacing)
	    {
	        float x0, y0, x1, y1;
	        WorldToScreen(Vector(x, 0, minZ), x0, y0);
	        WorldToScreen(Vector(x, 0, maxZ), x1, y1);
	        
	        LineDrawCommand line = new LineDrawCommand();
	        line.m_iColor = color;
	        line.m_fWidth = width;
	        line.m_Vertices = {x0, y0, x1, y1};
	        m_aDrawCommands.Insert(line);
	    }
	    
	    // Horizontal lines (constant Z, vary X)
	    for (float z = startZ; z <= maxZ; z += spacing)
	    {
	        float x0, y0, x1, y1;
	        WorldToScreen(Vector(minX, 0, z), x0, y0);
	        WorldToScreen(Vector(maxX, 0, z), x1, y1);
	        
	        LineDrawCommand line = new LineDrawCommand();
	        line.m_iColor = color;
	        line.m_fWidth = width;
	        line.m_Vertices = {x0, y0, x1, y1};
	        m_aDrawCommands.Insert(line);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    protected void DrawMarker(AG0_TDLMapMarker marker, float screenX, float screenY)
    {
        float size = marker.m_fSize;

        PolygonDrawCommand outline = new PolygonDrawCommand();
        outline.m_iColor = m_iMarkerOutlineColor;
        
        array<float> outlineVerts = {};
        TessellateCircle(screenX, screenY, size + 2, 8, outlineVerts);
        outline.m_Vertices = outlineVerts;
        m_aDrawCommands.Insert(outline);

        PolygonDrawCommand fill = new PolygonDrawCommand();
        fill.m_iColor = marker.m_iColor;

        array<float> fillVerts = {};
        TessellateCircle(screenX, screenY, size, 8, fillVerts);
        fill.m_Vertices = fillVerts;
        m_aDrawCommands.Insert(fill);

        if (marker.m_bShowHeading)
        {
            DrawHeadingIndicator(screenX, screenY, GetMarkerScreenHeading(marker.m_vWorldPos, marker.m_fHeading), size, marker.m_iColor);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! `screenHeading` is already in screen space — resolved by GetMarkerScreenHeading so
    //! canvas ticks agree with the widget markers under a hosted 3D pane, where the correct
    //! angle depends on where the marker sits under perspective and not on view rotation alone.
    protected void DrawHeadingIndicator(float x, float y, float screenHeading, float size, int color)
    {
        float rotRad = screenHeading * Math.DEG2RAD;
        float length = size * 2;
        
        float tipX = x + Math.Sin(rotRad) * length;
        float tipY = y - Math.Cos(rotRad) * length;
        
        LineDrawCommand line = new LineDrawCommand();
        line.m_iColor = color;
        line.m_fWidth = 3;
        line.m_fOutlineWidth = 1;
        line.m_iOutlineColor = m_iMarkerOutlineColor;
        line.m_Vertices = {x, y, tipX, tipY};
        m_aDrawCommands.Insert(line);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void TessellateCircle(float centerX, float centerY, float radius, int segments, out array<float> verts)
    {
        verts = {};
        float angleStep = (Math.PI * 2) / segments;
        
        for (int i = 0; i < segments; i++)
        {
            float angle = i * angleStep;
            verts.Insert(centerX + Math.Cos(angle) * radius);
            verts.Insert(centerY + Math.Sin(angle) * radius);
        }
    }
    
	
	//------------------------------------------------------------------------------------------------
    //! Toggle an overlay layer on/off by index
    void SetOverlayEnabled(int index, bool enabled)
    {
        if (index >= 0 && index < m_aOverlayEnabled.Count())
            m_aOverlayEnabled[index] = enabled;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Toggle an overlay layer on/off by name (case-insensitive partial match)
    void SetOverlayEnabledByName(string name, bool enabled)
    {
        name.ToLower();
        for (int i = 0; i < m_aOverlayNames.Count(); i++)
        {
            string layerName = m_aOverlayNames[i];
            layerName.ToLower();
            if (layerName.Contains(name))
            {
                m_aOverlayEnabled[i] = enabled;
                return;
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get overlay count (for building UI toggles)
    int GetOverlayCount()
    {
        return m_aOverlayNames.Count();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get overlay name by index
    string GetOverlayName(int index)
    {
        if (index >= 0 && index < m_aOverlayNames.Count())
            return m_aOverlayNames[index];
        return "";
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get overlay enabled state by index
    bool IsOverlayEnabled(int index)
    {
        if (index >= 0 && index < m_aOverlayEnabled.Count())
            return m_aOverlayEnabled[index];
        return false;
    }
	
    //------------------------------------------------------------------------------------------------
    // ACCESSORS
    //------------------------------------------------------------------------------------------------
    vector GetCenter() { return m_vCenterWorld; }
    float GetZoom() { return m_fZoom; }
    bool IsTextureLoaded() { return m_bTextureLoaded; }

    //------------------------------------------------------------------------------------------------
    //! While the 3D pane is up this reports the camera's yaw (negated: a camera yawed
    //! right shows the world rotated left on screen), so rotation consumers — the
    //! compass heading indicator above all — track the orbit instead of a 2D rotation
    //! state that is not being rendered. m_fRotation itself is left untouched so the
    //! 2D view resumes exactly where it was when the 3D map closes.
    float GetRotation()
    {
        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (view)
            return -view.GetCameraYaw();

        return m_fRotation;
    }

    //------------------------------------------------------------------------------------------------
    //! Screen rotation for a marker at `worldPos` facing `worldHeading`. Split out from
    //! GetRotation because under perspective the correct answer depends on WHERE the
    //! marker is, not just on the view: the 3D map projects a point ahead of the marker
    //! and measures the on-screen angle, which a flat heading+rotation sum cannot match
    //! at shallow camera pitch.
    float GetMarkerScreenHeading(vector worldPos, float worldHeading)
    {
        AG0_TDLMap3DView view = GetHostedMap3DView();
        if (view)
        {
            float projectedDeg;
            if (view.ProjectHeadingToPane(worldPos, worldHeading, projectedDeg))
                return projectedDeg;
        }

        return worldHeading + GetRotation();
    }
}

//------------------------------------------------------------------------------------------------
// Marker data structure
//------------------------------------------------------------------------------------------------
class AG0_TDLMapMarker
{
    vector m_vWorldPos;
    int m_iColor;
    float m_fSize = 8;
    float m_fHeading;
    bool m_bShowHeading;
    string m_sLabel;
}

