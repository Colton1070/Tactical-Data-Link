//------------------------------------------------------------------------------------------------
// AG0_TDLShapeDrawSession
//------------------------------------------------------------------------------------------------
//! Collects world-space points from the marker tool panel's click stream and
//! turns them into an AG0_TDLMapShape on commit. One instance per
//! AG0_TDLMarkerToolPanel (i.e. one per AG0_TDLMenuController instance).
//!
//! Input contract is ATAK-style click-only — every gesture is a fixed number
//! of "drop a point at the current pointer / canvas centre" actions, so the
//! same code path serves PC mouse, gamepad (canvas centre as the pointer),
//! fullscreen menu, and world-space frontend. The session itself does not
//! subscribe to inputs; the panel feeds it AddPoint / SetCursorWorld /
//! CommitVariadic / Cancel.
//!
//! Per-tool semantics (matches research_shape_draw_tool.md §1):
//!   CIRCLE       — 2 clicks (center, edge);              auto-commits on click 2
//!   RECTANGLE    — 2 clicks (corner, opposite corner);   auto-commits on click 2
//!   POLYGON      — variadic (vertex × N);                commit action closes (min 3)
//!   SECTOR       — 3 clicks (center, radial1, radial2);  auto-commits on click 3
//!   RANGE_RINGS  — variadic (center, ring1, ring2, …);   commit action closes (min 1 ring)
//!   ROUTE        — variadic (waypoint × N);              commit action closes (min 2)
//!   FREEHAND     — not supported in v1 (web API does not emit it either)
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Panel-local tool enum. Index order matches the ShapeToolSpinBox so the
//! panel can pass spinbox index through directly. UNKNOWN sits at 0 to match
//! the rest of the mod's enum convention.
//------------------------------------------------------------------------------------------------
enum AG0_ETDLShapeTool
{
	UNKNOWN = 0,
	CIRCLE,
	RECTANGLE,
	POLYGON,
	SECTOR,
	RANGE_RINGS,
	ROUTE,
	FREEHAND
}

enum AG0_ETDLShapeDrawPhase
{
	IDLE = 0,
	COLLECTING
}

//------------------------------------------------------------------------------------------------
class AG0_TDLShapeDrawSession
{
	// Lifecycle state
	protected AG0_ETDLShapeTool m_eActiveTool = AG0_ETDLShapeTool.UNKNOWN;
	protected AG0_ETDLShapeDrawPhase m_ePhase = AG0_ETDLShapeDrawPhase.IDLE;

	// Collected world-space points in click order. Vector Y is always 0 — the
	// map is flat in XZ; height is irrelevant for shape rendering.
	protected ref array<vector> m_aWorldPoints = {};

	// Latest cursor world position. Updated between clicks via SetCursorWorld
	// so the ghost rubber-bands to follow the pointer. Sentinel value handled
	// in BuildGhostShape so an unset cursor doesn't fake a point.
	protected vector m_vCursorWorld = vector.Zero;
	protected bool   m_bCursorWorldKnown;

	// Style state mirrored from the panel. Pushed via SetStyle whenever a
	// stroke / fill / label control changes so the ghost rebuild stays
	// constant-time without the session reaching back into the panel.
	protected int    m_iStrokeColor = 0xFFFF0000;
	protected float  m_fStrokeWidth = 2;
	protected int    m_iFillColor   = 0;
	protected string m_sLabel       = "";

	// World-distance thresholds for the close-the-loop commit gesture on
	// variadic tools. Picked per-tool by intent:
	//   POLYGON      — click near the first vertex to close the loop.
	//   ROUTE        — click near the last vertex (double-tap last waypoint).
	//   RANGE_RINGS  — click near the center to signal "done adding rings".
	// Values are deliberately small enough that adjacent intentional clicks
	// can sit close together (consecutive route waypoints, tight polygon
	// corners) without accidentally triggering a commit. At very high
	// zoom-out the threshold is sub-pixel — refine to a screen-space check
	// if that becomes a problem in practice.
	protected static const float CLOSE_LOOP_POLYGON_RADIUS     = 25.0;
	protected static const float CLOSE_LOOP_ROUTE_RADIUS       = 10.0;
	protected static const float CLOSE_LOOP_RANGE_RINGS_RADIUS = 15.0;

	// Freehand stream parameters. Sampling is driven by mouse-drag on the
	// map canvas; each frame the drag handler may emit a cursor sample that
	// AddFreehandSample either records or drops. The distance filter keeps
	// the vertex count down without losing perceptible fidelity — the API's
	// Douglas-Peucker simplification on commit further reduces server-side
	// storage, but client-side pre-filtering keeps the RPC payload under
	// the 8191-byte per-string-param ceiling at any drag duration. Hard cap
	// stops accepting after MAX_POINTS so a very long drag can't blow past
	// the ceiling even at low density.
	protected static const float FREEHAND_SAMPLE_MIN_DISTANCE  = 5.0;
	protected static const int   FREEHAND_MAX_POINTS           = 200;

	//! Fires (AG0_TDLMapShape draft) when the session produces a final shape.
	//! Panel subscribes and routes draft to the player-controller create RPC.
	ref ScriptInvoker m_OnCommitted = new ScriptInvoker();

	//! Fires () when the session resets (commit or cancel). Panel uses this
	//! to clear its parameter readout and the map view's ghost.
	ref ScriptInvoker m_OnReset = new ScriptInvoker();

	//------------------------------------------------------------------------------------------------
	bool IsArmed()
	{
		return m_ePhase == AG0_ETDLShapeDrawPhase.COLLECTING;
	}

	AG0_ETDLShapeTool GetActiveTool()
	{
		return m_eActiveTool;
	}

	int GetPointCount()
	{
		return m_aWorldPoints.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Cursor world position last pushed via SetCursorWorld. Exposed so
	//! sibling tools (delete-sweep) can reuse the same cursor-resolution
	//! pipeline (mouse-move / world-space EOnFrame / gamepad fallback) the
	//! shape ghost already consumes, without each tool re-implementing the
	//! per-frontend conversions.
	vector GetCursorWorld()
	{
		return m_vCursorWorld;
	}

	bool IsCursorWorldKnown()
	{
		return m_bCursorWorldKnown;
	}

	//------------------------------------------------------------------------------------------------
	//! True when an explicit commit gesture would succeed right now —
	//! variadic tool, in COLLECTING phase, with enough points placed. Used
	//! by the panel-level confirm button to gate its visibility: only
	//! variadic shapes (polygon, route, range_rings) need a button, and only
	//! once the user has placed enough geometry that the commit produces a
	//! valid shape. Fixed-point tools (circle, rectangle, sector) auto-commit
	//! through AddPoint and never report true here.
	bool CanCommit()
	{
		if (m_ePhase != AG0_ETDLShapeDrawPhase.COLLECTING)
			return false;
		if (GetMaxPoints(m_eActiveTool) > 0)
			return false;
		return m_aWorldPoints.Count() >= GetMinPointsToCommit(m_eActiveTool);
	}

	//------------------------------------------------------------------------------------------------
	//! Begin a draw with the given tool. Clears any prior state. UNKNOWN
	//! disarms — used by the panel when the user backs out of shape mode.
	void Arm(AG0_ETDLShapeTool tool)
	{
		m_aWorldPoints.Clear();
		m_bCursorWorldKnown = false;
		m_vCursorWorld = vector.Zero;
		m_eActiveTool = tool;

		if (tool == AG0_ETDLShapeTool.UNKNOWN)
		{
			m_ePhase = AG0_ETDLShapeDrawPhase.IDLE;
			m_OnReset.Invoke();
			return;
		}

		m_ePhase = AG0_ETDLShapeDrawPhase.COLLECTING;
	}

	//------------------------------------------------------------------------------------------------
	//! Push the panel's current style state. Cheap to call repeatedly — the
	//! session just stores the values for the next ghost rebuild.
	void SetStyle(int strokeColor, float strokeWidth, int fillColor, string label)
	{
		m_iStrokeColor = strokeColor;
		m_fStrokeWidth = strokeWidth;
		m_iFillColor   = fillColor;
		m_sLabel       = label;
	}

	//------------------------------------------------------------------------------------------------
	//! Live cursor tracking — only affects the ghost preview, never the
	//! committed shape. Y component is normalised to 0 to match every other
	//! coordinate the session and renderer work in.
	void SetCursorWorld(vector worldPos)
	{
		m_vCursorWorld = Vector(worldPos[0], 0, worldPos[2]);
		m_bCursorWorldKnown = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Append a world-space click. Returns true if the click triggered an
	//! auto-commit (fixed-point-count tools), false if the session is still
	//! collecting or rejected the input. Caller doesn't need to inspect the
	//! return — the commit is signalled via m_OnCommitted.
	bool AddPoint(vector worldPos)
	{
		if (m_ePhase != AG0_ETDLShapeDrawPhase.COLLECTING)
			return false;

		// Freehand uses a continuous sample stream rather than discrete
		// clicks. Route a stray click in that mode through the same
		// dedup/cap path the sample stream uses, so a stray click during
		// a freehand drag (or before a drag starts) is harmless.
		if (m_eActiveTool == AG0_ETDLShapeTool.FREEHAND)
			return AddFreehandSample(worldPos);

		// Variadic tools: a click near the closing vertex commits the
		// in-progress shape instead of adding another point. Replaces the
		// dedicated TDLCommitShape input action — same behaviour, no
		// keybinding required, works identically across all four input
		// contexts (KBM mouse, gamepad canvas-centre, fullscreen menu,
		// world-space frontend).
		if (TryCloseLoopCommit(worldPos))
			return true;

		vector point = Vector(worldPos[0], 0, worldPos[2]);
		m_aWorldPoints.Insert(point);

		// First click of a fresh draw — snap the cursor world to the click
		// position so the ghost's rubber-band leg starts at click-to-click
		// rather than stretching from the click to a stale cursor world.
		// Stale cursor happens when the mouse hasn't moved since session
		// arm (KBM) or the gamepad hasn't panned recently to refresh the
		// canvas-centre push (gamepad). The ghost-stretch artefact
		// otherwise reads as a line across the canvas on the first draw.
		if (m_aWorldPoints.Count() == 1)
		{
			m_vCursorWorld = point;
			m_bCursorWorldKnown = true;
		}

		int maxPoints = GetMaxPoints(m_eActiveTool);
		if (maxPoints > 0 && m_aWorldPoints.Count() >= maxPoints)
		{
			Commit();
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Per-frame freehand sampling hook. Reads the current cursor world
	//! position (pushed via SetCursorWorld) and feeds it into the freehand
	//! stream. Controller's Tick calls this each frame the TDLDraw input is
	//! held; same code path serves menu KBM (cursor = mouse), world-space
	//! normal mode, and world-space focus mode (cursor = m_fCursorX/Y of
	//! the device's content frame). Returns true when a sample was recorded.
	bool TickFreehandFromCursor()
	{
		if (m_ePhase != AG0_ETDLShapeDrawPhase.COLLECTING)
			return false;
		if (m_eActiveTool != AG0_ETDLShapeTool.FREEHAND)
			return false;
		if (!m_bCursorWorldKnown)
			return false;
		return AddFreehandSample(m_vCursorWorld);
	}

	//------------------------------------------------------------------------------------------------
	//! Append a freehand stream sample. Drops samples that are too close to
	//! the previous one (server-side Douglas-Peucker would discard them
	//! anyway, this just keeps the in-memory vertex list bounded) and stops
	//! accepting once the hard cap is reached. Returns true when the sample
	//! was recorded, false when dropped or rejected.
	bool AddFreehandSample(vector worldPos)
	{
		if (m_ePhase != AG0_ETDLShapeDrawPhase.COLLECTING)
			return false;
		if (m_eActiveTool != AG0_ETDLShapeTool.FREEHAND)
			return false;

		vector sample = Vector(worldPos[0], 0, worldPos[2]);

		int count = m_aWorldPoints.Count();
		if (count > 0 && vector.Distance(sample, m_aWorldPoints[count - 1]) < FREEHAND_SAMPLE_MIN_DISTANCE)
			return false;
		if (count >= FREEHAND_MAX_POINTS)
			return false;

		m_aWorldPoints.Insert(sample);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Detect a close-the-loop click for variadic tools. Returns true when
	//! the click was within the per-tool threshold of the closing vertex AND
	//! the session has already collected enough points to commit a valid
	//! shape. Caller treats true as "commit happened" and skips appending.
	protected bool TryCloseLoopCommit(vector worldPos)
	{
		int minPts = GetMinPointsToCommit(m_eActiveTool);
		if (m_aWorldPoints.Count() < minPts)
			return false;

		vector target;
		float threshold;
		switch (m_eActiveTool)
		{
			case AG0_ETDLShapeTool.POLYGON:
			{
				target = m_aWorldPoints[0];
				threshold = CLOSE_LOOP_POLYGON_RADIUS;
				break;
			}
			case AG0_ETDLShapeTool.ROUTE:
			{
				target = m_aWorldPoints[m_aWorldPoints.Count() - 1];
				threshold = CLOSE_LOOP_ROUTE_RADIUS;
				break;
			}
			case AG0_ETDLShapeTool.RANGE_RINGS:
			{
				target = m_aWorldPoints[0];
				threshold = CLOSE_LOOP_RANGE_RINGS_RADIUS;
				break;
			}
			default:
				return false;
		}

		vector click = Vector(worldPos[0], 0, worldPos[2]);
		if (vector.Distance(click, target) > threshold)
			return false;

		Commit();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Variadic-tool commit entry point. Returns true if the commit fired,
	//! false if the session doesn't have enough points yet (caller can show
	//! a UI hint). No-op for fixed-point tools — those auto-commit through
	//! AddPoint.
	//!
	//! For FREEHAND specifically, an insufficient-commit also clears the
	//! partial samples. A freehand "stroke" is the press-to-release window
	//! of TDLDraw, and releasing without enough samples means the stroke
	//! was a discarded gesture (typical when TDLDraw is co-bound with the
	//! screen-click action — a quick click holds TDLDraw briefly enough
	//! that cursor jitter produces 1–2 samples). Without the clear, those
	//! samples would persist into the next press, accumulating across
	//! clicks until the min threshold trips and an unwanted shape commits
	//! with its "start point" at the first stray sample. Other variadic
	//! tools (polygon, route, range_rings) legitimately accumulate
	//! vertices across multiple commit attempts (user clicks vertices,
	//! commits explicitly when ready), so they keep their partial points.
	bool CommitVariadic()
	{
		if (m_ePhase != AG0_ETDLShapeDrawPhase.COLLECTING)
			return false;

		int minPoints = GetMinPointsToCommit(m_eActiveTool);
		if (m_aWorldPoints.Count() < minPoints)
		{
			if (m_eActiveTool == AG0_ETDLShapeTool.FREEHAND)
				m_aWorldPoints.Clear();
			return false;
		}

		Commit();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	void Cancel()
	{
		m_aWorldPoints.Clear();
		m_bCursorWorldKnown = false;
		m_vCursorWorld = vector.Zero;
		m_ePhase = AG0_ETDLShapeDrawPhase.IDLE;
		m_eActiveTool = AG0_ETDLShapeTool.UNKNOWN;
		m_OnReset.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	//! Build the live ghost shape for the in-progress draw. Returns null when
	//! the session is idle or hasn't collected enough points to form a
	//! meaningful preview. The returned shape uses a translucent stroke
	//! (alpha halved) so users can distinguish in-progress geometry from
	//! committed shapes that share the same colour.
	AG0_TDLMapShape BuildGhostShape()
	{
		if (m_ePhase != AG0_ETDLShapeDrawPhase.COLLECTING)
			return null;
		if (m_aWorldPoints.IsEmpty())
			return null;

		AG0_TDLMapShape ghost = new AG0_TDLMapShape();
		ghost.m_iStrokeColor = BuildGhostStrokeColor(m_iStrokeColor);
		ghost.m_fStrokeWidth = m_fStrokeWidth;
		// Fill is intentionally omitted from the ghost — a filled preview
		// reads as "already committed" and obscures the underlying terrain
		// while the user is still placing.
		ghost.m_iFillColor   = 0;
		ghost.m_sLabel       = "";

		switch (m_eActiveTool)
		{
			case AG0_ETDLShapeTool.CIRCLE:        return BuildCircleGhost(ghost);
			case AG0_ETDLShapeTool.RECTANGLE:     return BuildRectangleGhost(ghost);
			case AG0_ETDLShapeTool.POLYGON:       return BuildPolygonGhost(ghost);
			case AG0_ETDLShapeTool.SECTOR:        return BuildSectorGhost(ghost);
			case AG0_ETDLShapeTool.RANGE_RINGS:   return BuildRangeRingsGhost(ghost);
			case AG0_ETDLShapeTool.ROUTE:         return BuildRouteGhost(ghost);
			case AG0_ETDLShapeTool.FREEHAND:      return BuildFreehandGhost(ghost);
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// COMMIT — build the final shape and reset
	//------------------------------------------------------------------------------------------------

	protected void Commit()
	{
		AG0_TDLMapShape final = BuildFinalShape();
		AG0_ETDLShapeTool tool = m_eActiveTool;

		// Reset before firing the callback so listeners that immediately
		// re-arm the session don't fight a stale phase.
		m_aWorldPoints.Clear();
		m_bCursorWorldKnown = false;
		m_vCursorWorld = vector.Zero;
		m_ePhase = AG0_ETDLShapeDrawPhase.IDLE;
		m_eActiveTool = AG0_ETDLShapeTool.UNKNOWN;
		m_OnReset.Invoke();

		if (final)
			m_OnCommitted.Invoke(final, tool);
	}

	//------------------------------------------------------------------------------------------------
	protected AG0_TDLMapShape BuildFinalShape()
	{
		AG0_TDLMapShape shape = new AG0_TDLMapShape();
		shape.m_iStrokeColor = m_iStrokeColor;
		shape.m_fStrokeWidth = m_fStrokeWidth;
		shape.m_iFillColor   = m_iFillColor;
		shape.m_sLabel       = m_sLabel;

		switch (m_eActiveTool)
		{
			case AG0_ETDLShapeTool.CIRCLE:        return PopulateCircle(shape, false);
			case AG0_ETDLShapeTool.RECTANGLE:     return PopulateRectangle(shape, false);
			case AG0_ETDLShapeTool.POLYGON:       return PopulatePolygon(shape, false);
			case AG0_ETDLShapeTool.SECTOR:        return PopulateSector(shape, false);
			case AG0_ETDLShapeTool.RANGE_RINGS:   return PopulateRangeRings(shape, false);
			case AG0_ETDLShapeTool.ROUTE:         return PopulateRoute(shape, false);
			case AG0_ETDLShapeTool.FREEHAND:      return PopulateFreehand(shape, false);
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// GHOST BUILDERS — each calls Populate* with includeCursor=true so the
	// rubber-band leg is appended where it makes sense for the tool.
	//------------------------------------------------------------------------------------------------

	protected AG0_TDLMapShape BuildCircleGhost(AG0_TDLMapShape ghost)
	{
		return PopulateCircle(ghost, true);
	}

	protected AG0_TDLMapShape BuildRectangleGhost(AG0_TDLMapShape ghost)
	{
		return PopulateRectangle(ghost, true);
	}

	protected AG0_TDLMapShape BuildPolygonGhost(AG0_TDLMapShape ghost)
	{
		return PopulatePolygon(ghost, true);
	}

	protected AG0_TDLMapShape BuildSectorGhost(AG0_TDLMapShape ghost)
	{
		return PopulateSector(ghost, true);
	}

	protected AG0_TDLMapShape BuildRangeRingsGhost(AG0_TDLMapShape ghost)
	{
		return PopulateRangeRings(ghost, true);
	}

	protected AG0_TDLMapShape BuildRouteGhost(AG0_TDLMapShape ghost)
	{
		return PopulateRoute(ghost, true);
	}

	protected AG0_TDLMapShape BuildFreehandGhost(AG0_TDLMapShape ghost)
	{
		// Freehand ghost intentionally does not append the live cursor —
		// the sample stream already captures cursor history, so adding the
		// cursor as a synthetic next vertex would duplicate the most-recent
		// sample and read as a flickering tail when the cursor is between
		// dedup windows. Pure samples produce the smoothest preview.
		return PopulateFreehand(ghost, false);
	}

	//------------------------------------------------------------------------------------------------
	// POPULATE — shared geometry generation. includeCursor controls whether
	// the live cursor is treated as a synthetic next-click. Ghost path passes
	// true, commit path passes false.
	//------------------------------------------------------------------------------------------------

	protected AG0_TDLMapShape PopulateCircle(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.CIRCLE;

		if (m_aWorldPoints.Count() == 0)
			return null;

		vector center = m_aWorldPoints[0];
		shape.m_vCenter = center;

		vector edge;
		if (m_aWorldPoints.Count() >= 2)
			edge = m_aWorldPoints[1];
		else if (includeCursor && m_bCursorWorldKnown)
			edge = m_vCursorWorld;
		else
			return null;

		shape.m_fRadius = vector.Distance(center, edge);
		if (shape.m_fRadius < 1.0)
			return null;

		return shape;
	}

	protected AG0_TDLMapShape PopulateRectangle(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.RECTANGLE;

		if (m_aWorldPoints.Count() == 0)
			return null;

		vector a = m_aWorldPoints[0];
		vector b;
		if (m_aWorldPoints.Count() >= 2)
			b = m_aWorldPoints[1];
		else if (includeCursor && m_bCursorWorldKnown)
			b = m_vCursorWorld;
		else
			return null;

		// Axis-aligned in world XZ; four vertices in CW order from upper-left.
		// Vertex storage layout matches AG0_TDLMapShape.m_aVertices contract:
		// flat [x0,z0, x1,z1, …] in world coordinates.
		float minX = Math.Min(a[0], b[0]);
		float maxX = Math.Max(a[0], b[0]);
		float minZ = Math.Min(a[2], b[2]);
		float maxZ = Math.Max(a[2], b[2]);

		array<float> verts = {};
		verts.Insert(minX); verts.Insert(maxZ);
		verts.Insert(maxX); verts.Insert(maxZ);
		verts.Insert(maxX); verts.Insert(minZ);
		verts.Insert(minX); verts.Insert(minZ);
		shape.m_aVertices = verts;

		// Centroid used by culling — keep it consistent with the rendered shape.
		shape.m_vCenter = Vector((minX + maxX) * 0.5, 0, (minZ + maxZ) * 0.5);

		return shape;
	}

	protected AG0_TDLMapShape PopulatePolygon(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.POLYGON;

		int n = m_aWorldPoints.Count();
		if (n == 0)
			return null;

		bool addCursor = includeCursor && m_bCursorWorldKnown;
		// Need at least three points (counting the cursor leg) to render a
		// polygon — two points draw as a line which DrawShapePolygon would
		// still close but would look like a flat triangle. Skip the ghost
		// until there's enough geometry to be meaningful.
		if (!addCursor && n < 3)
			return null;
		if (addCursor && n < 2)
			return null;

		array<float> verts = {};
		float sumX = 0, sumZ = 0;
		int total = 0;

		for (int i = 0; i < n; i++)
		{
			verts.Insert(m_aWorldPoints[i][0]);
			verts.Insert(m_aWorldPoints[i][2]);
			sumX = sumX + m_aWorldPoints[i][0];
			sumZ = sumZ + m_aWorldPoints[i][2];
			total = total + 1;
		}

		if (addCursor)
		{
			verts.Insert(m_vCursorWorld[0]);
			verts.Insert(m_vCursorWorld[2]);
			sumX = sumX + m_vCursorWorld[0];
			sumZ = sumZ + m_vCursorWorld[2];
			total = total + 1;
		}

		shape.m_aVertices = verts;
		shape.m_vCenter = Vector(sumX / total, 0, sumZ / total);
		return shape;
	}

	protected AG0_TDLMapShape PopulateSector(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.SECTOR;

		int n = m_aWorldPoints.Count();
		if (n == 0)
			return null;

		// Click 1 = center. Click 2 = first radial endpoint (sets radius and
		// startAngle). Click 3 = second radial endpoint (sets endAngle only —
		// sector has a uniform radius, second click's distance is ignored).
		vector center = m_aWorldPoints[0];
		shape.m_vCenter = center;

		bool haveRadial1 = n >= 2;
		bool haveRadial2 = n >= 3;

		vector radial1;
		if (haveRadial1)
			radial1 = m_aWorldPoints[1];
		else if (includeCursor && m_bCursorWorldKnown)
			radial1 = m_vCursorWorld;
		else
			return null;

		shape.m_fRadius = vector.Distance(center, radial1);
		if (shape.m_fRadius < 1.0)
			return null;
		shape.m_fStartAngle = BearingDegrees(center, radial1);

		vector radial2;
		if (haveRadial2)
			radial2 = m_aWorldPoints[2];
		else if (haveRadial1 && includeCursor && m_bCursorWorldKnown)
			radial2 = m_vCursorWorld;
		else
		{
			// One radial only — degenerate sector with zero arc length.
			// Render the single radial line as the preview by giving it a
			// trivial arc that the renderer will draw as a hairline.
			shape.m_fEndAngle = shape.m_fStartAngle;
			return shape;
		}

		shape.m_fEndAngle = BearingDegrees(center, radial2);
		return shape;
	}

	protected AG0_TDLMapShape PopulateRangeRings(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.RANGE_RINGS;

		int n = m_aWorldPoints.Count();
		if (n == 0)
			return null;

		vector center = m_aWorldPoints[0];
		shape.m_vCenter = center;

		array<float> rings = {};
		for (int i = 1; i < n; i++)
		{
			float r = vector.Distance(center, m_aWorldPoints[i]);
			if (r >= 1.0)
				rings.Insert(r);
		}

		if (includeCursor && m_bCursorWorldKnown)
		{
			float r = vector.Distance(center, m_vCursorWorld);
			if (r >= 1.0)
				rings.Insert(r);
		}

		if (rings.IsEmpty())
			return null;

		shape.m_aRings = rings;
		// Outer ring drives bounding-radius culling via GetBoundingRadius —
		// stamp m_fRadius too so existing culling reads a consistent value.
		float maxR = rings[0];
		foreach (float r : rings)
		{
			if (r > maxR) maxR = r;
		}
		shape.m_fRadius = maxR;
		return shape;
	}

	protected AG0_TDLMapShape PopulateRoute(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.ROUTE;

		int n = m_aWorldPoints.Count();
		if (n == 0)
			return null;

		array<float> verts = {};
		float sumX = 0, sumZ = 0;
		int total = 0;

		for (int i = 0; i < n; i++)
		{
			verts.Insert(m_aWorldPoints[i][0]);
			verts.Insert(m_aWorldPoints[i][2]);
			sumX = sumX + m_aWorldPoints[i][0];
			sumZ = sumZ + m_aWorldPoints[i][2];
			total = total + 1;
		}

		if (includeCursor && m_bCursorWorldKnown)
		{
			verts.Insert(m_vCursorWorld[0]);
			verts.Insert(m_vCursorWorld[2]);
			sumX = sumX + m_vCursorWorld[0];
			sumZ = sumZ + m_vCursorWorld[2];
			total = total + 1;
		}

		// Route needs at least two distinct points to render as a path.
		if (verts.Count() < 4)
			return null;

		shape.m_aVertices = verts;
		shape.m_vCenter = Vector(sumX / total, 0, sumZ / total);
		// Per-waypoint labels deferred to v2 — empty array satisfies the API
		// contract (length check only fires when labels are present).
		shape.m_aWaypointLabels = {};
		return shape;
	}

	//------------------------------------------------------------------------------------------------
	//! Build the FREEHAND shape from the accumulated drag samples. Renders
	//! as a closed polygon via DrawShapePolygon — same path the API-emitted
	//! freehand shapes go through (FREEHAND falls into the polygon switch
	//! arm in AG0_TDLMapView.DrawSingleShape). Centroid is the average of
	//! all sampled points, matching PopulatePolygon's bookkeeping.
	protected AG0_TDLMapShape PopulateFreehand(AG0_TDLMapShape shape, bool includeCursor)
	{
		shape.m_eShapeType = AG0_ETDLShapeType.FREEHAND;

		int n = m_aWorldPoints.Count();
		if (n < 2)
			return null;

		array<float> verts = {};
		float sumX = 0, sumZ = 0;
		int total = 0;

		for (int i = 0; i < n; i++)
		{
			verts.Insert(m_aWorldPoints[i][0]);
			verts.Insert(m_aWorldPoints[i][2]);
			sumX = sumX + m_aWorldPoints[i][0];
			sumZ = sumZ + m_aWorldPoints[i][2];
			total = total + 1;
		}

		if (includeCursor && m_bCursorWorldKnown)
		{
			verts.Insert(m_vCursorWorld[0]);
			verts.Insert(m_vCursorWorld[2]);
			sumX = sumX + m_vCursorWorld[0];
			sumZ = sumZ + m_vCursorWorld[2];
			total = total + 1;
		}

		shape.m_aVertices = verts;
		shape.m_vCenter = Vector(sumX / total, 0, sumZ / total);
		return shape;
	}

	//------------------------------------------------------------------------------------------------
	// HELPERS
	//------------------------------------------------------------------------------------------------

	//! Per-tool fixed click count, -1 for variadic. Auto-commit fires when
	//! AddPoint reaches this count.
	protected int GetMaxPoints(AG0_ETDLShapeTool tool)
	{
		switch (tool)
		{
			case AG0_ETDLShapeTool.CIRCLE:      return 2;
			case AG0_ETDLShapeTool.RECTANGLE:   return 2;
			case AG0_ETDLShapeTool.SECTOR:      return 3;
		}
		return -1;
	}

	//! Minimum points required before the variadic-commit action will fire.
	//! Below this the action is a no-op so the user can't commit a degenerate
	//! shape (e.g. polygon with two vertices).
	protected int GetMinPointsToCommit(AG0_ETDLShapeTool tool)
	{
		switch (tool)
		{
			case AG0_ETDLShapeTool.POLYGON:      return 3;
			case AG0_ETDLShapeTool.RANGE_RINGS:  return 2;
			case AG0_ETDLShapeTool.ROUTE:        return 2;
			case AG0_ETDLShapeTool.FREEHAND:     return 3;
		}
		// Fixed-point tools auto-commit so the variadic path is irrelevant.
		return 1000000;
	}

	//! Compute compass bearing from `from` toward `to` in degrees, 0=North,
	//! clockwise. Matches the sector startAngle/endAngle contract used by
	//! AG0_TDLMapView.DrawShapeSector (which then rotates by -90 and adds the
	//! view rotation).
	//!
	//! North in Reforger world coordinates is +Z (confirmed against
	//! WorldToScreen: screenY = canvasCenter - rotatedZ*ppu, so larger Z
	//! renders further up on screen, and up on the map is North). atan2(dx, dz)
	//! therefore maps a vector pointing north to 0 and rotates clockwise
	//! through east (+X) → south (-Z) → west (-X).
	protected float BearingDegrees(vector from, vector to)
	{
		float dx = to[0] - from[0];
		float dz = to[2] - from[2];
		float rad = Math.Atan2(dx, dz);
		float deg = rad * Math.RAD2DEG;
		if (deg < 0) deg = deg + 360;
		return deg;
	}

	//! Halve the alpha component of an ARGB strokeColor for the ghost render.
	//! Bit layout: 0xAARRGGBB. Reduces "in progress" geometry to a translucent
	//! preview while keeping hue and saturation intact so the user can tell
	//! which stroke colour will land on commit.
	protected int BuildGhostStrokeColor(int strokeColor)
	{
		int alpha = (strokeColor >> 24) & 0xFF;
		int halved = alpha / 2;
		if (halved < 0x20) halved = 0x20;
		return (halved << 24) | (strokeColor & 0x00FFFFFF);
	}
}
