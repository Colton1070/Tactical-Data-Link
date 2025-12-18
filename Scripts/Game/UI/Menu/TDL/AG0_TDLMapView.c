//------------------------------------------------------------------------------------------------
// AG0_TDLMapView.c
// Canvas-based map rendering for TDL menu - bypasses MapWidget limitations
//------------------------------------------------------------------------------------------------

class AG0_TDLMapView
{
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
    
    // View state
    protected vector m_vCenterWorld;      // World position we're centered on
    protected float m_fZoom = 1.0;        // Zoom level (1.0 = full map visible)
    protected float m_fRotation = 0;      // Rotation in degrees (0 = north up)
    protected float m_fMinZoom = 0.1;     // Most zoomed in
    protected float m_fMaxZoom = 1.0;     // Full map view
    
    // Canvas dimensions (cached)
    protected float m_fCanvasWidth;
    protected float m_fCanvasHeight;
    
    // Member markers
    protected ref array<ref AG0_TDLMapMarker> m_aMarkers = {};
    
    // Building cache
    protected ref array<ref AG0_TDLBuildingData> m_aCachedBuildings = {};
    protected vector m_vCacheCenterWorld;
    protected float m_fCacheZoom;
    protected float m_fCacheRadius;
    protected bool m_bBuildingCacheDirty = true;
    protected const float CACHE_BUFFER_MULT = 1.5;      // Query 1.5x viewport size
    protected const float CACHE_INVALIDATE_DIST = 100;  // Re-query if moved 100m from cache center
    
    // Colors
    protected int m_iSelfMarkerColor = 0xFF00FF00;      // Green for self
    protected int m_iMemberMarkerColor = 0xFF00BFFF;    // Blue for network members
    protected int m_iMarkerOutlineColor = 0xFF000000;   // Black outline
    protected int m_iBuildingColor = 0xFF4A4A4A;        // Dark gray for buildings
    
    //------------------------------------------------------------------------------------------------
    void AG0_TDLMapView()
    {
    }
    
    //------------------------------------------------------------------------------------------------
    void ~AG0_TDLMapView()
    {
        m_pMapTexture = null;
        m_aDrawCommands = null;
        m_aMarkers = null;
    }
    
    //------------------------------------------------------------------------------------------------
    // INITIALIZATION
    //------------------------------------------------------------------------------------------------
    bool Init(CanvasWidget canvas)
    {
        if (!canvas)
        {
            Print("[TDLMapView] Init failed - null canvas", LogLevel.ERROR);
            return false;
        }
        
        m_wCanvas = canvas;
        
        // Get map dimensions from MapEntity
        if (!LoadMapData())
        {
            Print("[TDLMapView] Failed to load map data from MapEntity", LogLevel.ERROR);
            return false;
        }
        
        // Load satellite texture
        if (!LoadMapTexture())
        {
            Print("[TDLMapView] Failed to load map texture", LogLevel.WARNING);
            // Continue anyway - we can still show markers without background
        }
        
        // Cache canvas size
        m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);
        
        // Default center to map center
        m_vCenterWorld = Vector(
            m_fMapOffsetX + m_fMapSizeX * 0.5,
            0,
            m_fMapOffsetY + m_fMapSizeY * 0.5
        );
        
        Print(string.Format("[TDLMapView] Initialized - Map size: %1x%2, Offset: %3,%4", 
            m_fMapSizeX, m_fMapSizeY, m_fMapOffsetX, m_fMapOffsetY), LogLevel.NORMAL);
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool LoadMapData()
    {
        SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
        if (!mapEntity)
        {
            Print("[TDLMapView] No MapEntity instance found", LogLevel.ERROR);
            return false;
        }
        
        vector size = mapEntity.Size();
        vector offset = mapEntity.Offset();
        
        m_fMapSizeX = size[0];
        m_fMapSizeY = size[2];
        m_fMapOffsetX = offset[0];
        m_fMapOffsetY = offset[2];
        
        if (m_fMapSizeX <= 0 || m_fMapSizeY <= 0)
        {
            Print("[TDLMapView] Invalid map dimensions", LogLevel.ERROR);
            return false;
        }
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected bool LoadMapTexture()
    {
        SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
        if (!mapEntity)
            return false;
        
        // Get texture path from MapEntity prefab data
        EntityPrefabData prefabData = mapEntity.GetPrefabData();
        if (!prefabData)
        {
            Print("[TDLMapView] No prefab data on MapEntity", LogLevel.WARNING);
            return false;
        }
        
        BaseContainer container = prefabData.GetPrefab();
        if (!container)
        {
            Print("[TDLMapView] Failed to get BaseContainer from prefab", LogLevel.WARNING);
            return false;
        }
        
        ResourceName texturePath;
        if (!container.Get("Satellite background image", texturePath))
        {
            Print("[TDLMapView] No 'Satellite background image' property in MapEntity prefab", LogLevel.WARNING);
            return false;
        }
        
        if (texturePath.IsEmpty())
        {
            Print("[TDLMapView] Satellite texture path is empty", LogLevel.WARNING);
            return false;
        }
        
        Print(string.Format("[TDLMapView] Loading texture: %1", texturePath), LogLevel.NORMAL);
        
        m_pMapTexture = CanvasWidget.LoadTexture(texturePath);
        if (!m_pMapTexture)
        {
            Print("[TDLMapView] Failed to load texture", LogLevel.WARNING);
            return false;
        }
        
        m_bTextureLoaded = true;
        return true;
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
        if (player)
            m_vCenterWorld = player.GetOrigin();
    }
    
    //------------------------------------------------------------------------------------------------
    void SetZoom(float zoom)
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
    void SetRotation(float degrees)
    {
        m_fRotation = degrees;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetTrackUp(float playerHeading)
    {
        // Rotate map so player heading points up
        m_fRotation = -playerHeading;
    }
    
    //------------------------------------------------------------------------------------------------
    void Pan(float screenDeltaX, float screenDeltaY)
    {
        // Convert screen delta to world delta based on current zoom
        float worldUnitsPerPixel = GetWorldUnitsPerPixel();
        
        // Account for rotation
        float rotRad = m_fRotation * Math.DEG2RAD;
        float cosR = Math.Cos(rotRad);
        float sinR = Math.Sin(rotRad);
        
        float worldDeltaX = (screenDeltaX * cosR - screenDeltaY * sinR) * worldUnitsPerPixel;
        float worldDeltaZ = (screenDeltaX * sinR + screenDeltaY * cosR) * worldUnitsPerPixel;
        
        m_vCenterWorld[0] = m_vCenterWorld[0] - worldDeltaX;
        m_vCenterWorld[2] = m_vCenterWorld[2] - worldDeltaZ;
        
        // Clamp to map bounds
        ClampCenterToBounds();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ClampCenterToBounds()
    {
        float halfViewX = (m_fMapSizeX * m_fZoom) * 0.5;
        float halfViewZ = (m_fMapSizeY * m_fZoom) * 0.5;
        
        m_vCenterWorld[0] = Math.Clamp(m_vCenterWorld[0], 
            m_fMapOffsetX + halfViewX, 
            m_fMapOffsetX + m_fMapSizeX - halfViewX);
        m_vCenterWorld[2] = Math.Clamp(m_vCenterWorld[2], 
            m_fMapOffsetY + halfViewZ, 
            m_fMapOffsetY + m_fMapSizeY - halfViewZ);
    }
    
    //------------------------------------------------------------------------------------------------
    protected float GetWorldUnitsPerPixel()
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
    // World position to screen position (canvas pixel coordinates)
    void WorldToScreen(vector worldPos, out float screenX, out float screenY)
    {
        // Offset from view center
        float offsetX = worldPos[0] - m_vCenterWorld[0];
        float offsetZ = worldPos[2] - m_vCenterWorld[2];
        
        // Apply rotation
        float rotRad = m_fRotation * Math.DEG2RAD;
        float cosR = Math.Cos(rotRad);
        float sinR = Math.Sin(rotRad);
        
        float rotatedX = offsetX * cosR - offsetZ * sinR;
        float rotatedZ = offsetX * sinR + offsetZ * cosR;
        
        // Use aspect-corrected view size (same as DrawMapTexture)
        float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
        float viewWorldSizeX = m_fMapSizeX * m_fZoom;
        
        // Scale to screen based on zoom and view width
        float pixelsPerWorldUnit = m_fCanvasWidth / viewWorldSizeX;
        
        // Convert to screen coords (center of canvas is center of view)
        screenX = (m_fCanvasWidth * 0.5) + (rotatedX * pixelsPerWorldUnit);
        // Flip Y for screen coordinates
        screenY = (m_fCanvasHeight * 0.5) - (rotatedZ * pixelsPerWorldUnit);
    }
    
    //------------------------------------------------------------------------------------------------
    // Screen position to world position
    void ScreenToWorld(float screenX, float screenY, out vector worldPos)
    {
        // Convert from screen center
        float canvasCenterX = m_fCanvasWidth * 0.5;
        float canvasCenterY = m_fCanvasHeight * 0.5;
        
        float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);
        
        float offsetX = (screenX - canvasCenterX) / pixelsPerWorldUnit;
        float offsetZ = -(screenY - canvasCenterY) / pixelsPerWorldUnit; // Flip Y
        
        // Reverse rotation
        float rotRad = -m_fRotation * Math.DEG2RAD;
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
    // BUILDING CACHE
    //------------------------------------------------------------------------------------------------
    protected void UpdateBuildingCache()
	{
	    // Check if cache is still valid
	    float distFromCache = vector.Distance(m_vCenterWorld, m_vCacheCenterWorld);
	    bool zoomChanged = Math.AbsFloat(m_fZoom - m_fCacheZoom) > 0.05;
	    
	    if (!m_bBuildingCacheDirty && distFromCache < CACHE_INVALIDATE_DIST && !zoomChanged)
	        return;
	    
	    m_aCachedBuildings.Clear();
	    
	    // Calculate query bounds - use larger area for better coverage
	    float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom * CACHE_BUFFER_MULT * 2.0; // Doubled buffer
	    float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
	    
	    vector queryMin = Vector(
	        m_vCenterWorld[0] - viewWorldSizeX * 0.5,
	        -1000,
	        m_vCenterWorld[2] - viewWorldSizeZ * 0.5
	    );
	    vector queryMax = Vector(
	        m_vCenterWorld[0] + viewWorldSizeX * 0.5,
	        1000,
	        m_vCenterWorld[2] + viewWorldSizeZ * 0.5
	    );
	    
	    // Use BaseWorld spatial query to find building entities directly
	    BaseWorld world = GetGame().GetWorld();
	    if (!world)
	        return;
	    
	    world.QueryEntitiesByAABB(queryMin, queryMax, QueryBuildingCallback, null, EQueryEntitiesFlags.STATIC);
	    
	    // Update cache state
	    m_vCacheCenterWorld = m_vCenterWorld;
	    m_fCacheZoom = m_fZoom;
	    m_bBuildingCacheDirty = false;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool QueryBuildingCallback(IEntity entity)
	{
	    // Check if entity has a map descriptor indicating it's a building
	    MapDescriptorComponent descriptor = MapDescriptorComponent.Cast(entity.FindComponent(MapDescriptorComponent));
	    if (!descriptor)
	        return true; // Continue query
	    
	    int descType = descriptor.GetBaseType();
	    if (!IsBuildingType(descType))
	        return true;
	    
	    // Get entity transform and bounds
	    vector transform[4];
	    entity.GetTransform(transform);
	    
	    vector mins, maxs;
	    entity.GetBounds(mins, maxs);
	    
	    // Calculate OBB data
	    AG0_TDLBuildingData data = new AG0_TDLBuildingData();
	    
	    // Center in local space, then transform to world
	    vector localCenter = (mins + maxs) * 0.5;
	    data.m_vCenter = entity.CoordToParent(localCenter);
	    
	    // Half extents
	    data.m_fHalfWidth = (maxs[0] - mins[0]) * 0.5;
	    data.m_fHalfLength = (maxs[2] - mins[2]) * 0.5;
	    
	    // Extract yaw from transform matrix
	    data.m_fYaw = Math.Atan2(transform[2][0], transform[2][2]) * Math.RAD2DEG;
	    
	    m_aCachedBuildings.Insert(data);
	    
	    return true; // Continue query
	}
    
    //------------------------------------------------------------------------------------------------
    protected bool IsBuildingType(int descriptorType)
    {
        switch (descriptorType)
        {
            case EMapDescriptorType.MDT_BUILDING:
            case EMapDescriptorType.MDT_HOUSE:
            case EMapDescriptorType.MDT_FORTRESS:
            case EMapDescriptorType.MDT_BUNKER:
            case EMapDescriptorType.MDT_FUELSTATION:
            case EMapDescriptorType.MDT_HOSPITAL:
            case EMapDescriptorType.MDT_CHURCH:
            case EMapDescriptorType.MDT_TRANSMITTER:
            case EMapDescriptorType.MDT_STACK:
            case EMapDescriptorType.MDT_RUIN:
            case EMapDescriptorType.MDT_WATERTOWER:
            case EMapDescriptorType.MDT_LIGHTHOUSE:
                return true;
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    void InvalidateBuildingCache()
    {
        m_bBuildingCacheDirty = true;
    }
    
    //------------------------------------------------------------------------------------------------
    // RENDERING
    //------------------------------------------------------------------------------------------------
    void Draw()
    {
        if (!m_wCanvas)
            return;
        
        // Update canvas size in case of resize
	    m_wCanvas.GetScreenSize(m_fCanvasWidth, m_fCanvasHeight);
	    
	    // Guard against zero dimensions (widget not yet laid out)
	    if (m_fCanvasHeight <= 0 || m_fCanvasWidth <= 0)
	        return;
        
        // Update building cache if needed
        UpdateBuildingCache();
        
        // Clear previous commands
        m_aDrawCommands.Clear();
        
        // Draw map background
        if (m_bTextureLoaded && m_pMapTexture)
            DrawMapTexture();
        else
            DrawFallbackBackground();
        
        // Draw buildings (under markers)
        DrawBuildings();
        
        // Draw markers (on top)
        DrawMarkers();
        
        // Submit draw commands
        m_wCanvas.SetDrawCommands(m_aDrawCommands);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawMapTexture()
	{
	    ImageDrawCommand cmd = new ImageDrawCommand();
	    cmd.m_pTexture = m_pMapTexture;
	    
	    float canvasAspect = m_fCanvasWidth / m_fCanvasHeight;
	    float viewWorldSizeX = m_fMapSizeX * m_fZoom;
	    float viewWorldSizeZ = viewWorldSizeX / canvasAspect;
	    
	    // Diagonal of the view rectangle (covers any rotation)
	    float diagonal = Math.Sqrt(viewWorldSizeX * viewWorldSizeX + viewWorldSizeZ * viewWorldSizeZ);
	    
	    // Sample square region sized to diagonal
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
	    
	    // Draw size must use same scale factor as UV sampling
	    float pixelsPerWorldUnit = m_fCanvasWidth / viewWorldSizeX;
	    float drawSize = diagonal * pixelsPerWorldUnit;
	    
	    float offsetX = (m_fCanvasWidth - drawSize) * 0.5;
	    float offsetY = (m_fCanvasHeight - drawSize) * 0.5;
	    
	    cmd.m_Position = Vector(offsetX, offsetY, 0);
	    cmd.m_Size = Vector(drawSize, drawSize, 0);
	    cmd.m_Pivot = Vector(drawSize * 0.5, drawSize * 0.5, 0);
	    cmd.m_fRotation = m_fRotation;
	    cmd.m_iColor = 0xFFFFFFFF;
	    cmd.m_iFlags = WidgetFlags.STRETCH;
	    
	    m_aDrawCommands.Insert(cmd);
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
	protected void DrawBuildings()
	{
	    if (m_aCachedBuildings.IsEmpty())
	        return;
	    
	    float pixelsPerWorldUnit = m_fCanvasWidth / (m_fMapSizeX * m_fZoom);
	    
	    foreach (AG0_TDLBuildingData bldg : m_aCachedBuildings)
	    {
	        // Get screen center
	        float centerX, centerY;
	        WorldToScreen(bldg.m_vCenter, centerX, centerY);
	        
	        // Calculate screen-space half-dimensions
	        float halfW = bldg.m_fHalfWidth * pixelsPerWorldUnit;
	        float halfL = bldg.m_fHalfLength * pixelsPerWorldUnit;
	        
	        // Skip tiny buildings (less than 2 pixels)
	        if (halfW < 1 && halfL < 1)
	            continue;
	        
	        // Skip if center is way off screen
	        float margin = Math.Max(halfW, halfL) + 20;
	        if (centerX < -margin || centerX > m_fCanvasWidth + margin ||
	            centerY < -margin || centerY > m_fCanvasHeight + margin)
	            continue;
	        
	        // Calculate rotated corners
	        // Total rotation = building yaw + map rotation
	        float totalRot = (bldg.m_fYaw + m_fRotation) * Math.DEG2RAD;
	        float cosR = Math.Cos(totalRot);
	        float sinR = Math.Sin(totalRot);
	        
	        // Local corners (unrotated)
	        // Note: on map, X is east-west, Z is north-south
	        // In screen space, we flip Z for Y
	        array<float> localX = {-halfW, halfW, halfW, -halfW};
	        array<float> localY = {-halfL, -halfL, halfL, halfL};
	        
	        // Build vertex array for polygon (rotated corners)
	        array<float> verts = {};
	        for (int i = 0; i < 4; i++)
	        {
	            float rotX = localX[i] * cosR - localY[i] * sinR;
	            float rotY = localX[i] * sinR + localY[i] * cosR;
	            verts.Insert(centerX + rotX);
	            verts.Insert(centerY - rotY); // Flip Y for screen coords
	        }
	        
	        // Draw outline - expand corners outward
			PolygonDrawCommand outline = new PolygonDrawCommand();
			outline.m_iColor = 0xFF000000;
			
			float outlineOffset = 1.5;
			array<float> outlineVerts = {};
			array<float> outLocalX = {-halfW - outlineOffset, halfW + outlineOffset, halfW + outlineOffset, -halfW - outlineOffset};
			array<float> outLocalY = {-halfL - outlineOffset, -halfL - outlineOffset, halfL + outlineOffset, halfL + outlineOffset};
			
			for (int j = 0; j < 4; j++)
			{
			    float rotX = outLocalX[j] * cosR - outLocalY[j] * sinR;
			    float rotY = outLocalX[j] * sinR + outLocalY[j] * cosR;
			    outlineVerts.Insert(centerX + rotX);
			    outlineVerts.Insert(centerY - rotY);
			}
			outline.m_Vertices = outlineVerts;
			m_aDrawCommands.Insert(outline);
	        
	        // Draw filled building
	        PolygonDrawCommand fill = new PolygonDrawCommand();
	        fill.m_iColor = m_iBuildingColor;
	        fill.m_Vertices = verts;
	        m_aDrawCommands.Insert(fill);
	    }
	}
    
    //------------------------------------------------------------------------------------------------
    protected void DrawMarkers()
    {
        foreach (AG0_TDLMapMarker marker : m_aMarkers)
        {
            float screenX, screenY;
            WorldToScreen(marker.m_vWorldPos, screenX, screenY);
            
            // Skip if outside canvas
            if (screenX < -20 || screenX > m_fCanvasWidth + 20 ||
                screenY < -20 || screenY > m_fCanvasHeight + 20)
                continue;
            
            DrawMarker(marker, screenX, screenY);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawMarker(AG0_TDLMapMarker marker, float screenX, float screenY)
    {
        float size = marker.m_fSize;
        
        // Draw outline
        PolygonDrawCommand outline = new PolygonDrawCommand();
        outline.m_iColor = m_iMarkerOutlineColor;
        
        array<float> outlineVerts = {};
        TessellateCircle(screenX, screenY, size + 2, 8, outlineVerts);
        outline.m_Vertices = outlineVerts;
        m_aDrawCommands.Insert(outline);
        
        // Draw filled circle
        PolygonDrawCommand fill = new PolygonDrawCommand();
        fill.m_iColor = marker.m_iColor;
        
        array<float> fillVerts = {};
        TessellateCircle(screenX, screenY, size, 8, fillVerts);
        fill.m_Vertices = fillVerts;
        m_aDrawCommands.Insert(fill);
        
        // Draw heading indicator if applicable
        if (marker.m_bShowHeading)
        {
            DrawHeadingIndicator(screenX, screenY, marker.m_fHeading, size, marker.m_iColor);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawHeadingIndicator(float x, float y, float heading, float size, int color)
    {
        // Arrow pointing in heading direction
        float rotRad = (heading + m_fRotation) * Math.DEG2RAD;
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
    // ACCESSORS
    //------------------------------------------------------------------------------------------------
    vector GetCenter() { return m_vCenterWorld; }
    float GetZoom() { return m_fZoom; }
    float GetRotation() { return m_fRotation; }
    bool IsTextureLoaded() { return m_bTextureLoaded; }
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

//------------------------------------------------------------------------------------------------
// Building data structure - stores OBB info
//------------------------------------------------------------------------------------------------
class AG0_TDLBuildingData
{
    vector m_vCenter;           // World center position
    float m_fHalfWidth;         // Half-width (local X)
    float m_fHalfLength;        // Half-length (local Z)
    float m_fYaw;               // Rotation in degrees
}