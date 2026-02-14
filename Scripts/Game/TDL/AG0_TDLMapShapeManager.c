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
	string m_sCreatedBy;                     // Creator identifier
	int m_iCreatedAt;                        // Unix timestamp
	int m_iStaleAt;                          // Unix timestamp — shape auto-removes after this
	
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
			m_sLastSyncHash = syncHash;
		
		// Parse shapes array
		array<string> shapeStrings = {};
		if (!json.ReadValue("shapes", shapeStrings))
		{
			Print("[TDL_SHAPES] No 'shapes' array in response", LogLevel.DEBUG);
			return 0;
		}
		
		// Store raw JSON strings for network redistribution
		m_mRawShapeJsons.Clear();
		
		// Track which shape IDs we received
		ref set<string> receivedIds = new set<string>();
		int parsed = 0;
		
		foreach (string shapeJson : shapeStrings)
		{
			AG0_TDLMapShape shape = ParseSingleShape(shapeJson);
			if (shape)
			{
				receivedIds.Insert(shape.m_sId);
				m_mRawShapeJsons.Set(shape.m_sId, shapeJson);
				
				AG0_TDLMapShape existing = m_mShapes.Get(shape.m_sId);
				if (existing && existing.m_iVersion >= shape.m_iVersion)
					continue;
				
				m_mShapes.Set(shape.m_sId, shape);
				m_bListDirty = true;
				parsed++;
			}
		}
		
		// Remove shapes not present in the response
		array<string> toRemove = {};
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			string key = m_mShapes.GetKey(i);
			if (!receivedIds.Contains(key))
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
		json.ReadValue("createdAt", shape.m_iCreatedAt);
		json.ReadValue("staleAt", shape.m_iStaleAt);
		
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
	string GetLastSyncHash()
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
		
		// Remove shapes not in the new data
		array<string> toRemove = {};
		for (int i = 0; i < m_mShapes.Count(); i++)
		{
			string key = m_mShapes.GetKey(i);
			if (!receivedIds.Contains(key))
				toRemove.Insert(key);
		}
		
		foreach (string removeId : toRemove)
		{
			m_mShapes.Remove(removeId);
			m_bListDirty = true;
		}
		
		return parsed;
	}
	
}