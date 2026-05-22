//------------------------------------------------------------------------------------------------
// AG0_TDLMapShapeManager.c
// Tactical Map Shape Manager
// Stores shapes received from the web API and provides them to the map renderer.
// Server-side: polls the /shapes endpoint and broadcasts shape data to clients.
// Client-side: receives shapes via RPC and stores them for rendering.
//
// Shape types supported:
//   circle, rectangle, polygon, freehand, route, range_rings, sector
//
// Shapes use parametric definitions (center + radius, etc.) — vertex tessellation
// happens at render time in AG0_TDLMapView, not here.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// Shape type enum — matches the "shapeType" field from the API
//------------------------------------------------------------------------------------------------
enum AG0_ETDLShapeType
{
	UNKNOWN = 0,
	CIRCLE,
	RECTANGLE,
	POLYGON,
	FREEHAND,
	ROUTE,
	RANGE_RINGS,
	SECTOR
}

//------------------------------------------------------------------------------------------------
// Shape origin — distinguishes shapes received from the API from shapes drawn
// in-game on this server. The full-replace pass during ParseShapesResponse /
// ParsePackedShapeData removes shapes whose ID is missing from the latest
// response. LOCAL-origin shapes must survive that pass: they live in mod
// memory until either an explicit local delete, a stale-time expiry, or a
// successful API mirror that lets us reconcile them to API-origin.
//------------------------------------------------------------------------------------------------
enum AG0_ETDLShapeOrigin
{
	API = 0,
	LOCAL
}

// Sentinel networkId for shapes drawn while the creator has no active network.
// Distribution filter sends these only to the creator (matched by identity ID).
// On the creator's first network join, the server promotes the shape's
// networkId so it propagates to other members.
const int AG0_TDL_SHAPE_NETWORK_ORPHAN = -1;

//------------------------------------------------------------------------------------------------
// Shape data class — one instance per shape received from the API.
// This is the canonical in-memory representation. Rendering happens in the map view.
//------------------------------------------------------------------------------------------------
class AG0_TDLMapShape
{
	// Identity
	string m_sId;                            // Unique shape ID from the API (e.g. "shape_a1b2c3d4")
	int m_iVersion;                          // Version number for concurrency control
	int m_iNetworkId;                        // TDL network this shape belongs to (0 = unscoped)
	AG0_ETDLShapeType m_eShapeType;          // Parsed shape type enum
	
	// Geometry — depends on shape type
	// Point-based shapes (circle, sector, range_rings): center position
	// Polygon/freehand/rectangle: first vertex or centroid for quick culling
	vector m_vCenter;                        // World position (X, 0, Z) — used by all types
	
	// Circle / sector / range_rings parameters
	float m_fRadius;                         // Radius in world meters (circle, sector, outer ring)
	float m_fStartAngle;                     // Start bearing in degrees, 0=North CW (sector only)
	float m_fEndAngle;                       // End bearing in degrees (sector only)
	ref array<float> m_aRings;               // Ring radii in meters (range_rings only)
	
	// Polygon / freehand / rectangle / route vertices
	// Stored as flat array: [x0, z0, x1, z1, ...] in world coordinates
	ref array<float> m_aVertices;
	
	// Route waypoint labels (parallel to vertices — one label per vertex pair)
	ref array<string> m_aWaypointLabels;
	
	// Style
	int m_iStrokeColor;                      // ARGB packed int (e.g. 0xFFFF0000 = red)
	float m_fStrokeWidth;                    // Stroke width in pixels
	int m_iFillColor;                        // ARGB packed int (0x00000000 = no fill)
	string m_sLabel;                         // Display label
	
	// Metadata
	string m_sCreatedBy;                     // Creator identifier (human-readable name)
	string m_sCreatedByPlayerIdentityId;     // Bohemia player identity UUID — orphan-distribution filter keys off this
	int m_iCreatedAt;                        // Unix timestamp
	int m_iStaleAt;                          // Unix timestamp — shape auto-removes after this

	// Origin marker. Default is API so the parse path doesn't have to set it
	// per-shape — InsertLocalShape flips it to LOCAL for mod-originated draws.
	AG0_ETDLShapeOrigin m_eOrigin = AG0_ETDLShapeOrigin.API;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLMapShape()
	{
		m_aVertices = {};
		m_aRings = {};
		m_aWaypointLabels = {};
		m_iStrokeColor = 0xFFFF0000;    // Default: red
		m_fStrokeWidth = 2;
		m_iFillColor = 0x00000000;       // Default: no fill
	}
	
	//------------------------------------------------------------------------------------------------
	//! Check if shape has expired based on stale time
	bool IsStale()
	{
		if (m_iStaleAt <= 0)
			return false; // No stale time = never expires
		
		return System.GetUnixTime() > m_iStaleAt;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Serialize this shape to the JSON shape the API and ParseSingleShape
	//! both understand. Used for client → server draft transmission via RPC
	//! and for re-emitting raw JSON after server-side mutation (orphan
	//! promotion, ID reconciliation). Fields with sentinel/empty values are
	//! still emitted; the parser handles missing fields gracefully but
	//! consistent payload shape makes diffing logs easier.
	//!
	//! includeCreatorIdentity defaults to true (the API-roundtrip caller).
	//! Per-client broadcast scrubs identity for shapes the viewing player
	//! doesn't own by passing false — keeps Bohemia identity UUIDs from
	//! leaking across clients. The createdBy display name stays either
	//! way because names are already public in-game.
	string ToJsonString(bool includeCreatorIdentity = true)
	{
		SCR_JsonSaveContext json = new SCR_JsonSaveContext();

		json.WriteValue("id", m_sId);
		json.WriteValue("shapeType", ShapeTypeToString(m_eShapeType));
		json.WriteValue("version", m_iVersion);
		json.WriteValue("networkId", m_iNetworkId);

		json.WriteValue("centerX", m_vCenter[0]);
		json.WriteValue("centerZ", m_vCenter[2]);

		json.WriteValue("radius", m_fRadius);
		json.WriteValue("startAngle", m_fStartAngle);
		json.WriteValue("endAngle", m_fEndAngle);

		if (m_aRings && !m_aRings.IsEmpty())
			json.WriteValue("rings", m_aRings);
		if (m_aVertices && !m_aVertices.IsEmpty())
			json.WriteValue("vertices", m_aVertices);
		if (m_aWaypointLabels && !m_aWaypointLabels.IsEmpty())
			json.WriteValue("waypointLabels", m_aWaypointLabels);

		json.WriteValue("strokeColor", m_iStrokeColor);
		json.WriteValue("strokeWidth", m_fStrokeWidth);
		json.WriteValue("fillColor", m_iFillColor);
		json.WriteValue("label", m_sLabel);

		json.WriteValue("createdBy", m_sCreatedBy);
		if (includeCreatorIdentity)
			json.WriteValue("createdByPlayerIdentityId", m_sCreatedByPlayerIdentityId);
		json.WriteValue("createdAt", m_iCreatedAt);
		json.WriteValue("staleAt", m_iStaleAt);

		return json.ExportToString();
	}

	//------------------------------------------------------------------------------------------------
	//! Inverse of AG0_TDLMapShapeManager.ParseShapeType. Kept on the shape so
	//! ToJsonString can write a canonical string without reaching into the
	//! manager. FREEHAND stays in the mapping for round-tripping API-origin
	//! shapes even though the mod doesn't emit freehand draws in v1.
	protected string ShapeTypeToString(AG0_ETDLShapeType t)
	{
		switch (t)
		{
			case AG0_ETDLShapeType.CIRCLE:       return "circle";
			case AG0_ETDLShapeType.RECTANGLE:    return "rectangle";
			case AG0_ETDLShapeType.POLYGON:      return "polygon";
			case AG0_ETDLShapeType.FREEHAND:     return "freehand";
			case AG0_ETDLShapeType.ROUTE:        return "route";
			case AG0_ETDLShapeType.RANGE_RINGS:  return "range_rings";
			case AG0_ETDLShapeType.SECTOR:       return "sector";
		}
		return "";
	}

	//------------------------------------------------------------------------------------------------
	//! Get bounding radius for frustum/viewport culling (world meters)
	float GetBoundingRadius()
	{
		switch (m_eShapeType)
		{
			case AG0_ETDLShapeType.CIRCLE:
			case AG0_ETDLShapeType.SECTOR:
				return m_fRadius;
			
			case AG0_ETDLShapeType.RANGE_RINGS:
			{
				float maxR = m_fRadius;
				foreach (float r : m_aRings)
				{
					if (r > maxR) maxR = r;
				}
				return maxR;
			}
			
			default:
			{
				// For vertex-based shapes, compute max distance from center
				float maxDist = 0;
				for (int i = 0; i + 1 < m_aVertices.Count(); i += 2)
				{
					float dx = m_aVertices[i] - m_vCenter[0];
					float dz = m_aVertices[i + 1] - m_vCenter[2];
					float dist = Math.Sqrt(dx * dx + dz * dz);
					if (dist > maxDist) maxDist = dist;
				}
				return maxDist;
			}
		}
		
		return 0;
	}
}

//------------------------------------------------------------------------------------------------
// Shape manager — holds all shapes, handles parsing API responses
//------------------------------------------------------------------------------------------------
class AG0_TDLMapShapeManager
{
	// All currently active shapes, keyed by shape ID for fast lookup
	protected ref map<string, ref AG0_TDLMapShape> m_mShapes = new map<string, ref AG0_TDLMapShape>();
	
	// Raw JSON strings from last API response — keyed by shape ID for filtered redistribution
	protected ref map<string, string> m_mRawShapeJsons = new map<string, string>();
	
	// Flat array for iteration during rendering (rebuilt on change)
	protected ref array<ref AG0_TDLMapShape> m_aShapeList = {};
	protected bool m_bListDirty = true;
	
	// Version tracking for delta polling
	protected string m_sLastSyncHash;

	// Counter bumped on every server-side local mutation (insert / remove /
	// network re-scope / raw-json refresh). Appended to m_sLastSyncHash via
	// GetLastSyncHash so client-side dedup in RpcDo_ReceiveTDLShapes sees a
	// different hash after a local draw, even when the API's view of the
	// world hasn't changed. Reset to 0 whenever the API parse paths assign
	// a fresh m_sLastSyncHash — the API hash becomes the new epoch.
	protected int m_iLocalMutationCounter;
	
	//------------------------------------------------------------------------------------------------
	//! Parse the full shapes response from GET /api/mod/shapes
	//! Stores raw JSON strings for network redistribution to clients.
	//! @return Number of shapes parsed
	int ParseShapesResponse(string jsonData)
	{
		SCR_JsonLoadContext json = new SCR_JsonLoadContext();
		if (!json.ImportFromString(jsonData))
		{
			Print("[TDL_SHAPES] Failed to parse shapes response JSON", LogLevel.WARNING);
			return 0;
		}
		
		// Read sync hash for future delta requests
		string syncHash;
		if (json.ReadValue("syncHash", syncHash))
		{
			m_sLastSyncHash = syncHash;
			// API hash is the new epoch for client-side dedup; any prior
			// local-mutation increments have already been broadcast under
			// the previous hash.
			m_iLocalMutationCounter = 0;
		}
		
		// Parse shapes array
		array<string> shapeStrings = {};
		if (!json.ReadValue("shapes", shapeStrings))
		{
			Print("[TDL_SHAPES] No 'shapes' array in response", LogLevel.DEBUG);
			return 0;
		}

		// Do NOT wholesale-clear m_mRawShapeJsons here. The foreach below
		// overwrites the raw JSON for any API-origin shape that appears in
		// the new response, and the removal pass at the bottom drops the
		// raw JSON for API-origin shapes that have gone away. A wholesale
		// clear would also wipe the raw JSON for LOCAL-origin shapes, even
		// though they survive in m_mShapes — leaving them in memory but
		// invisible to subsequent GetPackedShapeData* calls (the raw-JSON
		// empty guard would silently skip them), which manifested as
		// previously-drawn local shapes disappearing for the creator
		// whenever an API poll arrived between draws.

		ref set<string> receivedIds = new set<string>();
		int parsed = 0;
		
		foreach (string shapeJson : shapeStrings)
		{
			AG0_TDLMapShape shape = ParseSingleShape(shapeJson);
			if (!shape)
				continue;

			receivedIds.Insert(shape.m_sId);

			AG0_TDLMapShape existing = m_mShapes.Get(shape.m_sId);
			if (existing && existing.m_iVersion >= shape.m_iVersion)
			{
				// API has confirmed an existing LOCAL-origin entry — the
				// mod-drawn shape's POST round-trip already landed and the
				// canonical id is now present in the API's own listing.
				// Promote to API origin so the removal pass below treats
				// subsequent disappearances (web-delete) as authoritative.
				// Without this promotion the LOCAL stamp stays sticky
				// forever (version never bumps), and web-deletes of
				// mod-drawn shapes silently fail to propagate back in-game.
				// The raw JSON cache stays untouched — the POST response is
				// the richer payload, and the owner-bound GET feed carries
				// the same identity per the mod-feed contract.
				if (existing.m_eOrigin != AG0_ETDLShapeOrigin.API)
					existing.m_eOrigin = AG0_ETDLShapeOrigin.API;
				continue;
			}

			// Only update the cached raw JSON when we actually take the
			// new shape — otherwise an unchanged-poll round-trip would
			// replace a richer locally-cached payload (POST response,
			// includes createdByPlayerIdentityId) with the GET feed's
			// stripped variant, breaking owner-only delete checks on
			// subsequent broadcasts.
			m_mShapes.Set(shape.m_sId, shape);
			m_mRawShapeJsons.Set(shape.m_sId, shapeJson);
			m_bListDirty = true;
			parsed++;
		}
		
		// Remove shapes not present in the response — but only when they
		// originated from a previous API response. LOCAL-origin shapes live
		// independently of the API feed and would be wrongly wiped here.
		array<string> toRemove = {};
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			string key = m_mShapes.GetKey(i);
			if (receivedIds.Contains(key))
				continue;

			AG0_TDLMapShape existing = m_mShapes.GetElement(i);
			if (existing && existing.m_eOrigin != AG0_ETDLShapeOrigin.API)
				continue;

			toRemove.Insert(key);
		}

		foreach (string removeId : toRemove)
		{
			m_mShapes.Remove(removeId);
			m_mRawShapeJsons.Remove(removeId);
			m_bListDirty = true;
		}

		return parsed;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Parse a single shape from its JSON string
	//! @return Parsed shape or null on failure
	AG0_TDLMapShape ParseSingleShape(string shapeJson)
	{
		SCR_JsonLoadContext json = new SCR_JsonLoadContext();
		if (!json.ImportFromString(shapeJson))
		{
			Print("[TDL_SHAPES] Failed to parse individual shape JSON", LogLevel.DEBUG);
			return null;
		}
		
		AG0_TDLMapShape shape = new AG0_TDLMapShape();
		
		// Required fields
		if (!json.ReadValue("id", shape.m_sId))
		{
			Print("[TDL_SHAPES] Shape missing 'id'", LogLevel.DEBUG);
			return null;
		}
		
		string shapeType;
		if (!json.ReadValue("shapeType", shapeType))
		{
			Print("[TDL_SHAPES] Shape missing 'shapeType'", LogLevel.DEBUG);
			return null;
		}
		shape.m_eShapeType = ParseShapeType(shapeType);
		
		json.ReadValue("version", shape.m_iVersion);
		json.ReadValue("networkId", shape.m_iNetworkId);
		
		// Center position — all shapes have this
		float centerX, centerZ;
		if (json.ReadValue("centerX", centerX) && json.ReadValue("centerZ", centerZ))
		{
			shape.m_vCenter = Vector(centerX, 0, centerZ);
		}
		
		// Type-specific parameters
		json.ReadValue("radius", shape.m_fRadius);
		json.ReadValue("startAngle", shape.m_fStartAngle);
		json.ReadValue("endAngle", shape.m_fEndAngle);
		
		// Rings array (range_rings)
		array<float> rings = {};
		if (json.ReadValue("rings", rings))
			shape.m_aRings = rings;
		
		// Vertices (polygon, freehand, rectangle, route)
		array<float> vertices = {};
		if (json.ReadValue("vertices", vertices))
			shape.m_aVertices = vertices;
		
		// Waypoint labels (route)
		array<string> waypointLabels = {};
		if (json.ReadValue("waypointLabels", waypointLabels))
			shape.m_aWaypointLabels = waypointLabels;
		
		// Style
		int strokeColor;
		if (json.ReadValue("strokeColor", strokeColor))
			shape.m_iStrokeColor = strokeColor;
		
		float strokeWidth;
		if (json.ReadValue("strokeWidth", strokeWidth))
			shape.m_fStrokeWidth = strokeWidth;
		
		int fillColor;
		if (json.ReadValue("fillColor", fillColor))
			shape.m_iFillColor = fillColor;
		
		json.ReadValue("label", shape.m_sLabel);
		
		// Metadata
		json.ReadValue("createdBy", shape.m_sCreatedBy);
		json.ReadValue("createdByPlayerIdentityId", shape.m_sCreatedByPlayerIdentityId);
		json.ReadValue("createdAt", shape.m_iCreatedAt);
		json.ReadValue("staleAt", shape.m_iStaleAt);

		// Any path through Parse* came from the API response; explicit assignment
		// keeps the marker correct even if the constructor default ever changes.
		shape.m_eOrigin = AG0_ETDLShapeOrigin.API;

		return shape;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Convert shape type string to enum
	protected AG0_ETDLShapeType ParseShapeType(string typeStr)
	{
		typeStr.ToLower();
		
		if (typeStr == "circle")       return AG0_ETDLShapeType.CIRCLE;
		if (typeStr == "rectangle")    return AG0_ETDLShapeType.RECTANGLE;
		if (typeStr == "polygon")      return AG0_ETDLShapeType.POLYGON;
		if (typeStr == "freehand")     return AG0_ETDLShapeType.FREEHAND;
		if (typeStr == "route")        return AG0_ETDLShapeType.ROUTE;
		if (typeStr == "range_rings")  return AG0_ETDLShapeType.RANGE_RINGS;
		if (typeStr == "sector")       return AG0_ETDLShapeType.SECTOR;
		
		Print(string.Format("[TDL_SHAPES] Unknown shape type: '%1'", typeStr), LogLevel.WARNING);
		return AG0_ETDLShapeType.UNKNOWN;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Prune stale shapes
	//! @return Number of shapes removed
	int PruneStale()
	{
		array<string> staleIds = {};
		
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			AG0_TDLMapShape shape = m_mShapes.GetElement(i);
			if (shape.IsStale())
				staleIds.Insert(m_mShapes.GetKey(i));
		}
		
		foreach (string id : staleIds)
		{
			m_mShapes.Remove(id);
			m_mRawShapeJsons.Remove(id);
			m_bListDirty = true;
		}
		
		if (staleIds.Count() > 0)
			Print(string.Format("[TDL_SHAPES] Pruned %1 stale shapes", staleIds.Count()), LogLevel.DEBUG);
		
		return staleIds.Count();
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get all shapes as a flat array for rendering iteration
	array<ref AG0_TDLMapShape> GetShapes()
	{
		if (m_bListDirty)
		{
			m_aShapeList.Clear();
			for (int i = 0; i < m_mShapes.Count(); i++)
			{
				m_aShapeList.Insert(m_mShapes.GetElement(i));
			}
			m_bListDirty = false;
		}
		return m_aShapeList;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get a specific shape by ID
	AG0_TDLMapShape GetShape(string id)
	{
		return m_mShapes.Get(id);
	}
	
	//------------------------------------------------------------------------------------------------
	int GetShapeCount()
	{
		return m_mShapes.Count();
	}
	
	//------------------------------------------------------------------------------------------------
	//! Hash for client broadcast — combines the API epoch hash with the
	//! local-mutation counter so RpcDo_ReceiveTDLShapes's dedup check sees a
	//! fresh value after every server-side mutation. Suffix uses `_L<n>`
	//! because the API hash is hex-only (`[0-9a-f]+`) and never produces
	//! that pattern, so the separator stays unambiguous on the client.
	string GetLastSyncHash()
	{
		if (m_iLocalMutationCounter <= 0)
			return m_sLastSyncHash;
		return string.Format("%1_L%2", m_sLastSyncHash, m_iLocalMutationCounter);
	}

	//------------------------------------------------------------------------------------------------
	//! Hash for the GET /api/mod/shapes `since` query — emit the raw API
	//! epoch without the local-mutation suffix, otherwise the API would see
	//! a non-matching hash and return a full payload after every local
	//! draw (unnecessary network round-trip; the local mutations don't
	//! affect what the API knows).
	string GetApiPollSyncHash()
	{
		return m_sLastSyncHash;
	}
	
	//------------------------------------------------------------------------------------------------
	void Clear()
	{
		m_mShapes.Clear();
		m_mRawShapeJsons.Clear();
		m_aShapeList.Clear();
		m_bListDirty = false;
		m_sLastSyncHash = string.Empty;
		m_iLocalMutationCounter = 0;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Get raw JSON strings for network distribution, filtered to a specific network.
	//! Returns a newline-delimited packed string of matching shape JSONs.
	//! Empty string if no shapes match the network.
	string GetPackedShapeData(int networkId)
	{
		string packed;
		bool first = true;
		
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			AG0_TDLMapShape shape = m_mShapes.GetElement(i);
			if (shape.m_iNetworkId != networkId)
				continue;
			
			string rawJson = m_mRawShapeJsons.Get(shape.m_sId);
			if (rawJson.IsEmpty())
				continue;
			
			if (!first)
				packed = packed + "\n";
			packed = packed + rawJson;
			first = false;
		}
		
		return packed;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Player-centric variant of GetPackedShapeDataForNetworks. In addition
	//! to shapes whose networkId is in the supplied set, this also emits
	//! orphan shapes (AG0_TDL_SHAPE_NETWORK_ORPHAN) whose creator matches
	//! the supplied playerIdentityId. Orphans are shapes the player drew
	//! while they had no network — visible only to them until they join one
	//! and the server promotes the shape to that network.
	string GetPackedShapeDataForPlayer(set<int> networkIds, string playerIdentityId)
	{
		string packed;
		bool first = true;

		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			AG0_TDLMapShape shape = m_mShapes.GetElement(i);
			if (!shape)
				continue;

			bool include = false;
			if (networkIds && networkIds.Contains(shape.m_iNetworkId))
				include = true;
			else if (shape.m_iNetworkId == AG0_TDL_SHAPE_NETWORK_ORPHAN
				&& !playerIdentityId.IsEmpty()
				&& shape.m_sCreatedByPlayerIdentityId == playerIdentityId)
				include = true;

			if (!include)
				continue;

			// Per-client identity scrub. The cached raw JSON carries the
			// creator's Bohemia identity UUID — fine to expose to the
			// shape's own creator (their own UUID, used client-side for
			// owner-only delete checks) but a privacy leak when sent to
			// any other client. Owner gets cached JSON as-is; non-owners
			// get a freshly-serialised copy with createdByPlayerIdentityId
			// dropped. Display name (createdBy) stays for both — names
			// are already public in the game session.
			string shapeJson;
			bool isOwner = (!playerIdentityId.IsEmpty()
				&& shape.m_sCreatedByPlayerIdentityId == playerIdentityId);
			if (isOwner)
			{
				shapeJson = m_mRawShapeJsons.Get(shape.m_sId);
				if (shapeJson.IsEmpty())
					continue;
			}
			else
			{
				shapeJson = shape.ToJsonString(false);
				if (shapeJson.IsEmpty())
					continue;
			}

			if (!first)
				packed = packed + "\n";
			packed = packed + shapeJson;
			first = false;
		}

		return packed;
	}

	//------------------------------------------------------------------------------------------------
	//! Get raw JSON strings for shapes belonging to any of the given networks.
	//! Used for player-centric aggregation — a player in networks 3 and 5 gets both.
	string GetPackedShapeDataForNetworks(set<int> networkIds)
	{
		if (!networkIds || networkIds.Count() == 0)
			return string.Empty;
		
		string packed;
		bool first = true;
		
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			AG0_TDLMapShape shape = m_mShapes.GetElement(i);
			if (!networkIds.Contains(shape.m_iNetworkId))
				continue;
			
			string rawJson = m_mRawShapeJsons.Get(shape.m_sId);
			if (rawJson.IsEmpty())
				continue;
			
			if (!first)
				packed = packed + "\n";
			packed = packed + rawJson;
			first = false;
		}
		
		return packed;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Parse shapes from a packed newline-delimited string (received via RPC on clients).
	//! This replaces all current shapes with the received data.
	//! @return Number of shapes parsed
	int ParsePackedShapeData(string packedData, string syncHash)
	{
		m_sLastSyncHash = syncHash;
		// Client side: incoming hash from the server is authoritative,
		// reset the local counter so the next short-circuit check compares
		// against just the server's hash.
		m_iLocalMutationCounter = 0;
		
		if (packedData.IsEmpty())
		{
			// Empty = server has no shapes, clear local
			m_mShapes.Clear();
			m_bListDirty = true;
			return 0;
		}
		
		// Split packed string into individual JSON strings
		array<string> shapeStrings = {};
		packedData.Split("\n", shapeStrings, false);
		
		// Store raw strings keyed by shape ID (keeps parity with server)
		m_mRawShapeJsons.Clear();
		
		// Full replace — server is authoritative
		ref set<string> receivedIds = new set<string>();
		int parsed = 0;
		
		foreach (string shapeJson : shapeStrings)
		{
			AG0_TDLMapShape shape = ParseSingleShape(shapeJson);
			if (shape)
			{
				receivedIds.Insert(shape.m_sId);
				m_mShapes.Set(shape.m_sId, shape);
				m_mRawShapeJsons.Set(shape.m_sId, shapeJson);
				m_bListDirty = true;
				parsed++;
			}
		}
		
		// Remove shapes not in the new data — but spare LOCAL-origin shapes.
		// Same reasoning as the server-side Parse pass: the client's local
		// store must keep mod-originated shapes that have not yet reconciled
		// to API-origin (e.g. while the API mirror POST is in flight).
		array<string> toRemove = {};
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			string key = m_mShapes.GetKey(i);
			if (receivedIds.Contains(key))
				continue;

			AG0_TDLMapShape existing = m_mShapes.GetElement(i);
			if (existing && existing.m_eOrigin != AG0_ETDLShapeOrigin.API)
				continue;

			toRemove.Insert(key);
		}

		foreach (string removeId : toRemove)
		{
			m_mShapes.Remove(removeId);
			m_bListDirty = true;
		}

		return parsed;
	}

	//------------------------------------------------------------------------------------------------
	//! Insert a mod-originated shape with a server-assigned `local_<hex>` id.
	//! Caller is expected to have set m_sId, m_iNetworkId, m_sCreatedBy,
	//! m_sCreatedByPlayerIdentityId, geometry, and style fields. This helper
	//! stamps origin + version + createdAt and stores both the shape and its
	//! serialized JSON form so it can be redistributed via the existing
	//! GetPackedShapeData paths without a separate per-origin code path.
	void InsertLocalShape(AG0_TDLMapShape shape, string serializedJson)
	{
		if (!shape || shape.m_sId.IsEmpty())
			return;

		shape.m_eOrigin = AG0_ETDLShapeOrigin.LOCAL;
		if (shape.m_iVersion <= 0)
			shape.m_iVersion = 1;
		if (shape.m_iCreatedAt <= 0)
			shape.m_iCreatedAt = System.GetUnixTime();

		m_mShapes.Set(shape.m_sId, shape);
		if (!serializedJson.IsEmpty())
			m_mRawShapeJsons.Set(shape.m_sId, serializedJson);
		m_bListDirty = true;
		m_iLocalMutationCounter = m_iLocalMutationCounter + 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Replace the raw JSON cached for a shape. Used after orphan promotion
	//! mutates a shape's networkId so the next GetPackedShapeData* call emits
	//! the updated payload to clients.
	void UpdateRawShapeJson(string id, string serializedJson)
	{
		if (id.IsEmpty())
			return;
		m_mRawShapeJsons.Set(id, serializedJson);
		m_iLocalMutationCounter = m_iLocalMutationCounter + 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Remove a shape by ID regardless of origin. Used by the API mirror to
	//! drop the temporary local_<hex> entry once the API has assigned a
	//! canonical shape_<hex> id, and by future client-initiated deletes.
	bool RemoveShapeById(string id)
	{
		if (!m_mShapes.Contains(id))
			return false;
		m_mShapes.Remove(id);
		m_mRawShapeJsons.Remove(id);
		m_bListDirty = true;
		m_iLocalMutationCounter = m_iLocalMutationCounter + 1;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Mutate the networkId on an existing local shape. Used for orphan
	//! promotion: a shape drawn while the creator had no network gets
	//! re-scoped to the creator's network once they join one. Caller is
	//! responsible for re-emitting the raw JSON via UpdateRawShapeJson so the
	//! packed distribution reflects the new networkId.
	bool SetShapeNetworkId(string id, int networkId)
	{
		AG0_TDLMapShape shape = m_mShapes.Get(id);
		if (!shape)
			return false;
		if (shape.m_iNetworkId == networkId)
			return false;
		shape.m_iNetworkId = networkId;
		m_bListDirty = true;
		m_iLocalMutationCounter = m_iLocalMutationCounter + 1;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Walk all LOCAL-origin shapes belonging to a given player identity.
	//! Server uses this to find orphan shapes whose creator just joined a
	//! network. Returns shape IDs to keep the iteration safe across mutation.
	array<string> CollectLocalShapeIdsByCreator(string playerIdentityId)
	{
		array<string> ids = {};
		if (playerIdentityId.IsEmpty())
			return ids;

		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			AG0_TDLMapShape s = m_mShapes.GetElement(i);
			if (!s)
				continue;
			if (s.m_eOrigin != AG0_ETDLShapeOrigin.LOCAL)
				continue;
			if (s.m_sCreatedByPlayerIdentityId == playerIdentityId)
				ids.Insert(s.m_sId);
		}

		return ids;
	}

}