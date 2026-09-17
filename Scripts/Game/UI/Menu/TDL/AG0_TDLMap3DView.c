//------------------------------------------------------------------------------------------------
// AG0_TDLMap3DView.c
//
// The TDL map's 3D mode. A miniature of the mission terrain is built inside a private
// script-created world and surfaced through a RenderTargetWidget authored into the menu
// layout, so 2D and 3D are two renderings of one map rather than two separate maps.
//
// The 2D view stays authoritative for everything that is not geometry. While this view is
// open AG0_TDLMapView skips its own canvas pass and instead drives Tick(), hands over the
// live shape set and bloodhound state, and takes the orbit focus back as the map centre.
// Marker projection and placement picking route through two seams on AG0_TDLMapView
// rather than being reimplemented here, which is what keeps markers, placement, shapes and
// tool cursors identical in both modes.
//
// Credit: the GRS Team, and Gaz in particular, for the original idea of rendering an
// Enfusion terrain diorama into a script-created world and for sharing their source as
// reference material. Several of the engine constraints documented through this file were
// established there first.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Abstract elevation source so the mesh builder is independent of where heights came
//! from. The local terrain is available immediately and costs no network round trip;
//! the streamed grid is higher fidelity but only exists once the API path has synced.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DHeightSource
{
    float SampleY(float worldX, float worldZ)
    {
        return 0;
    }

    //! Fills the world-space rect the source can describe. Returns false when unusable.
    bool GetExtent(out float minX, out float minZ, out float spanX, out float spanZ)
    {
        minX = 0;
        minZ = 0;
        spanX = 0;
        spanZ = 0;
        return false;
    }
}

//------------------------------------------------------------------------------------------------
//! Samples the live terrain the player is standing on.
//! GetSurfaceY is a heightfield lookup rather than an entity query, so it answers for
//! the whole map regardless of what is streamed in around the player.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DWorldHeightSource : AG0_TDLMap3DHeightSource
{
    //! Below this a sample is the engine's off-terrain sentinel rather than seabed.
    protected static const float SEA_FLOOR_SENTINEL_M = -200;

    protected BaseWorld m_World;

    void AG0_TDLMap3DWorldHeightSource(BaseWorld world)
    {
        m_World = world;
    }

    override float SampleY(float worldX, float worldZ)
    {
        if (!m_World)
            return 0;

        float y = m_World.GetSurfaceY(worldX, worldZ);

        // Only the off-terrain sentinel is rejected, not everything below sea level.
        // Clamping the whole negative band to zero laid a flat sheet exactly coplanar
        // with the rendered ocean; genuine sub-sea terrain now passes through and sits
        // under the water where it belongs. The threshold sits below any real Reforger
        // bathymetry but above the sentinel (observed near -256 on a large map).
        if (y < SEA_FLOOR_SENTINEL_M)
            return 0;

        return y;
    }

    override bool GetExtent(out float minX, out float minZ, out float spanX, out float spanZ)
    {
        minX = 0;
        minZ = 0;
        spanX = 0;
        spanZ = 0;
        if (!m_World)
            return false;

        // Playable terrain extent, not the entity bound box. GetBoundBox covers every
        // entity in the scene and runs far wider than the terrain, which put almost
        // every sample off the heightfield where GetSurfaceY returns its off-terrain
        // sentinel — the whole grid clamped to sea level and rendered as a dead flat
        // plane. This is the same extent the 2D map view resolves its own bounds from.
        SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
        if (mapEntity)
        {
            vector size = mapEntity.Size();
            vector offset = mapEntity.Offset();
            if (size[0] > 0 && size[2] > 0)
            {
                minX = offset[0];
                minZ = offset[2];
                spanX = size[0];
                spanZ = size[2];
                return true;
            }
        }

        vector mins;
        vector maxs;
        m_World.GetBoundBox(mins, maxs);

        spanX = maxs[0] - mins[0];
        spanZ = maxs[2] - mins[2];
        if (spanX <= 0 || spanZ <= 0)
            return false;

        minX = mins[0];
        minZ = mins[2];
        return true;
    }
}

//------------------------------------------------------------------------------------------------
//! Samples the elevation grid streamed from /api/mod/terrain/heightmap.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DGridHeightSource : AG0_TDLMap3DHeightSource
{
    protected AG0_TDLTerrainHeightmapManager m_Grid;

    void AG0_TDLMap3DGridHeightSource(AG0_TDLTerrainHeightmapManager grid)
    {
        m_Grid = grid;
    }

    override float SampleY(float worldX, float worldZ)
    {
        if (!m_Grid)
            return 0;
        return m_Grid.SampleWorld(worldX, worldZ);
    }

    override bool GetExtent(out float minX, out float minZ, out float spanX, out float spanZ)
    {
        minX = 0;
        minZ = 0;
        spanX = 0;
        spanZ = 0;
        if (!m_Grid || !m_Grid.IsReady())
            return false;

        minX = m_Grid.GetOriginX();
        minZ = m_Grid.GetOriginZ();
        spanX = m_Grid.GetSpanX();
        spanZ = m_Grid.GetSpanZ();
        return spanX > 0 && spanZ > 0;
    }
}

//------------------------------------------------------------------------------------------------
//! Scratch upload buffers for one terrain tile.
//! Enfusion caps a single class at 256 KB, and local arrays cap at 2048 elements, so
//! these live in a heap object allocated only for the duration of a tile build.
//! 64x64 cells sizes the three arrays to ~182 KB together, inside the class cap.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DTileBuffers
{
    vector m_aVerts[4228];
    float m_aUVs[8456];
    int m_aIndices[24579];
}

//------------------------------------------------------------------------------------------------
//! One rectangular patch of the diorama terrain, in private-world (miniaturised) space.
//!
//! The terrain is split into tiles rather than uploaded as a single MeshObject because
//! a whole-map lattice at any useful resolution overruns the 256 KB class cap on the
//! index buffer alone. Each tile is a single-geometry MeshObject (numMeshes = 1) — the
//! shape every engine caller uses; the multi-geometry variant is not exercised here.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DTileEntityClass : GenericEntityClass
{
}

class AG0_TDLMap3DTileEntity : GenericEntity
{
    static const int TILE_CELLS = 64;
    static const int TILE_VERTS = 65;

    //! Degenerate triangle placed far above the tile, widened horizontally to the
    //! camera's full orbit envelope. An axis-aligned bound box that contains the camera
    //! cannot fail a frustum test, which suppresses the view-dependent whole-mesh cull
    //! that otherwise blanks tiles on shallow-pitch rotation. A vertical-only sliver is
    //! not enough — the camera leaves the box sideways when orbiting near the horizon.
    //!
    //! Disabled: it is a workaround for a cull this build may not exhibit, and it inflates
    //! every tile's bounds to 30 km, which distorts anything that reasons about them.
    //! If tiles start vanishing on shallow-pitch orbit, this is the first thing to restore.
    static const bool CULL_GUARD_ENABLED = false;
    static const float CULL_GUARD_HEIGHT = 30000;

    //! Vertical scale multiplier applied on top of the uniform miniaturisation. The
    //! diorama shrinks height by the same factor as distance, so a 400 m range across a
    //! 12.8 km island compresses to ~60 diorama metres and reads nearly flat. Raise this
    //! to make relief legible; 1.0 is the geometrically honest value.
    static const float HEIGHT_EXAGGERATION = 1.0;

    //! Stored as DIORAMA-LOCAL Y, not world metres, because every consumer (camera floor
    //! clamp, anything seated on the surface) works in diorama space — keeping the two
    //! unit systems apart here is what stops a scale factor going missing at a call site.
    protected ref array<float> m_aHeights = {};
    protected float m_fMinSampleY;
    protected float m_fMaxSampleY;
    protected int m_iOriginCol;
    protected int m_iOriginRow;
    protected float m_fLocalCellX;
    protected float m_fLocalCellZ;
    protected float m_fLocalMinX;
    protected float m_fLocalMinZ;
    protected bool m_bBuilt;

    //------------------------------------------------------------------------------------------------
    //! @param source      Where heights come from, in real world metres.
    //! @param worldMinX   World X of this tile's first column.
    //! @param worldMinZ   World Z of this tile's first row.
    //! @param stepX       World metres between columns.
    //! @param stepZ       World metres between rows.
    //! @param centerX     World X mapped to diorama origin.
    //! @param centerZ     World Z mapped to diorama origin.
    //! @param scale       World metres per diorama metre.
    //! @param guardRadius Horizontal half-extent of the cull guard, diorama metres.
    //! @param material    Material applied to the generated geometry.
    bool Build(AG0_TDLMap3DHeightSource source, float worldMinX, float worldMinZ,
        float stepX, float stepZ, float centerX, float centerZ, float scale,
        float guardRadius, string material,
        float mapMinX, float mapMinZ, float mapSpanX, float mapSpanZ)
    {
        if (m_bBuilt || !source || scale <= 0)
            return false;

        AG0_TDLMap3DTileBuffers buffers = new AG0_TDLMap3DTileBuffers();

        m_aHeights.Clear();
        m_aHeights.Reserve(TILE_VERTS * TILE_VERTS);

        m_fLocalCellX = stepX / scale;
        m_fLocalCellZ = stepZ / scale;
        m_fLocalMinX = (worldMinX - centerX) / scale;
        m_fLocalMinZ = (worldMinZ - centerZ) / scale;

        int vertIndex = 0;
        int uvIndex = 0;

        for (int row = 0; row < TILE_VERTS; row = row + 1)
        {
            float worldZ = worldMinZ + row * stepZ;
            for (int col = 0; col < TILE_VERTS; col = col + 1)
            {
                float worldX = worldMinX + col * stepX;
                float height = source.SampleY(worldX, worldZ);

                float localY = height * HEIGHT_EXAGGERATION / scale;
                m_aHeights.Insert(localY);

                if (height < m_fMinSampleY || vertIndex == 0)
                    m_fMinSampleY = height;
                if (height > m_fMaxSampleY || vertIndex == 0)
                    m_fMaxSampleY = height;

                buffers.m_aVerts[vertIndex] = Vector(
                    (worldX - centerX) / scale,
                    localY,
                    (worldZ - centerZ) / scale);
                vertIndex = vertIndex + 1;

                // UVs address the WHOLE map, not this tile, so every tile samples its own
                // window of one shared drape texture. Per-tile 0..1 UVs would repeat the
                // entire map on each quarter.
                buffers.m_aUVs[uvIndex] = (worldX - mapMinX) / mapSpanX;
                uvIndex = uvIndex + 1;
                // V is flipped because texture space runs top-down while world Z runs
                // north-up; without this the drape lands mirrored about the equator.
                buffers.m_aUVs[uvIndex] = 1.0 - (worldZ - mapMinZ) / mapSpanZ;
                uvIndex = uvIndex + 1;
            }
        }

        int guardBase = vertIndex;
        if (CULL_GUARD_ENABLED)
        {
            buffers.m_aVerts[guardBase] = Vector(-guardRadius, CULL_GUARD_HEIGHT, -guardRadius);
            buffers.m_aVerts[guardBase + 1] = Vector(guardRadius, CULL_GUARD_HEIGHT, -guardRadius);
            buffers.m_aVerts[guardBase + 2] = Vector(guardRadius, CULL_GUARD_HEIGHT, guardRadius);
            buffers.m_aUVs[uvIndex] = 0;
            buffers.m_aUVs[uvIndex + 1] = 0;
            buffers.m_aUVs[uvIndex + 2] = 0;
            buffers.m_aUVs[uvIndex + 3] = 0;
            buffers.m_aUVs[uvIndex + 4] = 0;
            buffers.m_aUVs[uvIndex + 5] = 0;
        }

        int idx = 0;
        for (int cellRow = 0; cellRow < TILE_CELLS; cellRow = cellRow + 1)
        {
            for (int cellCol = 0; cellCol < TILE_CELLS; cellCol = cellCol + 1)
            {
                int v00 = cellRow * TILE_VERTS + cellCol;
                int v10 = v00 + 1;
                int v01 = v00 + TILE_VERTS;
                int v11 = v01 + 1;

                buffers.m_aIndices[idx] = v00;
                buffers.m_aIndices[idx + 1] = v01;
                buffers.m_aIndices[idx + 2] = v10;
                buffers.m_aIndices[idx + 3] = v10;
                buffers.m_aIndices[idx + 4] = v01;
                buffers.m_aIndices[idx + 5] = v11;
                idx = idx + 6;
            }
        }

        int totalVerts = guardBase;
        if (CULL_GUARD_ENABLED)
        {
            buffers.m_aIndices[idx] = guardBase;
            buffers.m_aIndices[idx + 1] = guardBase + 1;
            buffers.m_aIndices[idx + 2] = guardBase + 2;
            idx = idx + 3;
            totalVerts = guardBase + 3;
        }
        int vertexCounts[1];
        vertexCounts[0] = totalVerts;
        int indexCounts[1];
        indexCounts[0] = idx;
        string materials[1];
        materials[0] = material;

        Resource meshResource = MeshObject.Create(1, vertexCounts, indexCounts, materials,
            MeshObjectFlags.UPDATE_BOUNDBOX);
        if (!meshResource)
            return false;

        BaseResourceObject resourceObject = meshResource.GetResource();
        if (!resourceObject)
            return false;

        MeshObject meshObject = resourceObject.ToMeshObject();
        if (!meshObject)
            return false;

        meshObject.UpdateVerts(0, buffers.m_aVerts, buffers.m_aUVs);
        meshObject.UpdateIndices(0, buffers.m_aIndices);
        SetObject(meshObject, string.Empty);

        // No Physics.CreateStatic: placement picking marches the same bilinear sampler
        // this mesh was built from, so nothing ever traces against the tile and a runtime
        // collision mesh would only add native surface for no consumer.
        Update();

        m_bBuilt = true;
        return true;
    }

    //! World-metre height range this tile sampled. A range of 0..0 means the sampler
    //! returned nothing usable — an extent bug, not a rendering one.
    float GetMinSampledHeight() { return m_fMinSampleY; }
    float GetMaxSampledHeight() { return m_fMaxSampleY; }

    //------------------------------------------------------------------------------------------------
    //! Bilinear height at diorama-local XZ, using this tile's OWN sample grid so anything
    //! seated on the surface meets the rendered triangles rather than the true terrain.
    //! Returns false when the point falls outside the tile.
    bool SampleLocalY(float localX, float localZ, out float outY)
    {
        outY = 0;
        if (!m_bBuilt || m_fLocalCellX <= 0 || m_fLocalCellZ <= 0)
            return false;

        float fc = (localX - m_fLocalMinX) / m_fLocalCellX;
        float fr = (localZ - m_fLocalMinZ) / m_fLocalCellZ;
        if (fc < 0 || fr < 0 || fc > TILE_CELLS || fr > TILE_CELLS)
            return false;

        int c0 = Math.Floor(fc);
        int r0 = Math.Floor(fr);
        if (c0 > TILE_CELLS - 1)
            c0 = TILE_CELLS - 1;
        if (r0 > TILE_CELLS - 1)
            r0 = TILE_CELLS - 1;

        float tx = fc - c0;
        float tz = fr - r0;

        float h00 = m_aHeights[r0 * TILE_VERTS + c0];
        float h10 = m_aHeights[r0 * TILE_VERTS + c0 + 1];
        float h01 = m_aHeights[(r0 + 1) * TILE_VERTS + c0];
        float h11 = m_aHeights[(r0 + 1) * TILE_VERTS + c0 + 1];

        float top = h00 + (h10 - h00) * tx;
        float bottom = h01 + (h11 - h01) * tx;
        outY = top + (bottom - top) * tz;
        return true;
    }
}

//------------------------------------------------------------------------------------------------
//! The 3D map itself: owns the private world, the tiles, the camera and the pane.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DView
{
    //! Whether closing the view reaps the diorama or leaves it standing for the next open.
    //!
    //! False because the engine owns a script-created world's lifetime: CreateWorld hands
    //! back a SharedItemRef whose entire surface is GetRef() and IsValid(), and there is no
    //! destroy call to pair with it. Keeping the world alive across opens is therefore the
    //! supported shape, and it makes reopening free — the second open skips BuildDiorama
    //! entirely and only refreshes what tracks the outside world (see Open).
    //!
    //! DestroyWorld() exists for the case where a diorama must be rebuilt from scratch, and
    //! encodes the unbind ordering that makes that survivable. Both a RenderTargetWidget
    //! holding SetWorld(world, index) and an RTTextureWidget bound onto an entity's mesh
    //! object via SetRenderTarget(entity) are live render-thread references; deleting the
    //! entity or dropping the world underneath either one is a use-after-free that surfaces
    //! as a native crash with no script-side error. Turning this on has not been exercised.
    static const bool RELEASE_WORLD_ON_CLOSE = false;

    //! Lightweight UI world class, as used by the engine's own preview surfaces.
    //!
    //! "ChimeraWorld" was tried and reverted: it produced the same six of nine
    //! GenericWorldPP_Default effects and the same soft image, while standing up the
    //! entire game system stack in a second world — AI, replication, vehicles, navmesh,
    //! and a duplicate-AISmartActionSystem warning. The three missing effects (SSDO,
    //! HBAO, HeightmapAO) are all ambient occlusion and are gated by the user's AO
    //! graphics setting, not by the world class.
    protected static const string WORLD_TYPE = "Preview";

    //! Distinct instance name. Reusing "Preview" for the name as well as the type risks
    //! colliding with the engine's own preview world — BI advise against it, and one of
    //! the reported symptoms of a raw CreateWorld("Preview", "Preview") is a render
    //! target that stays blank.
    protected static const string WORLD_NAME = "TDL3D";

    //! Some world classes need their systems brought up explicitly after creation.
    protected static const bool WORLD_RELOAD_SYSTEMS = false;

    //! World environment: sky, planets, clouds, ocean. Authors no post-process entities,
    //! which is what lets this world drive PP on its own camera slot instead of inheriting
    //! a stack aimed at four slots it never renders.
    //!
    //! It also authors no GenericWorldLightEntity, which is why LIGHTING_PREFAB is spawned
    //! as a peer — the atmosphere and sun presets describe how lit surfaces look but cast
    //! nothing on their own.
    protected static const ResourceName WORLD_ENV_PREFAB =
        "{9739FC0958721521}Prefabs/World/DefaultWorld/GenericWorld_TDL.et";

    //! Spawned as a peer of the environment. The sky and planet presets describe how the
    //! atmosphere looks at a given moment but nothing advances or positions that moment —
    //! without this manager the sun has no place to be, which is what leaves the terrain
    //! lit by ambient alone.
    protected static const ResourceName TIME_WEATHER_PREFAB =
        "{A3BAF78F6F03315B}Prefabs/World/Game/TimeAndWeatherManager.et";

    //! Stock lighting rig. GenericWorld_TDL authors sky, planets and ocean but no
    //! GenericWorldLightEntity, so this supplies the directional light the atmosphere
    //! presets describe the appearance of but do not themselves cast.
    protected static const ResourceName LIGHTING_PREFAB =
        "{5B2B348D9520F7C7}Prefabs/World/DefaultWorld/Lighting_Default.et";

    //! Environment probe. PBR surfaces take their ambient and reflection input from one;
    //! with none in the world those terms are undefined, which is why an otherwise
    //! correctly-lit scene can still read wrong.
    protected static const ResourceName ENV_PROBE_PREFAB =
        "{B6B6A21399C5571B}Prefabs/World/DefaultWorld/EnvProbe_Default.et";

    //! Height above the terrain at the diorama's centre, in PRIVATE-WORLD metres — the
    //! probe lives in the miniature, so this is not a real-world distance.
    protected static const float ENV_PROBE_HEIGHT_M = 10;

    //! Stock post-process stack. Supplies SMAA as well as HDR — the antialiasing that
    //! went missing when the preview scene was dropped, which is most of why the pane
    //! reads jagged no matter what resolution it renders at. Its "Overlapping PP effects
    //! ... overwrites empty effect" lines are the cost, and they overwrite nothing.
    protected static const ResourceName WORLD_PP_PREFAB =
        "{3AFFB0B0EC055284}Prefabs/World/DefaultWorld/GenericWorldPP_Default.et";

    //! The PP prefab makes the effect framework available in this world but does not
    //! aim anything at our camera slot, so effects are assigned here explicitly.
    protected static const bool SCRIPT_DRIVEN_PP = true;

    //! Priorities mirror the ordering the stock preview scene uses — tonemap first,
    //! antialiasing last so it resolves after everything that could introduce edges.
    protected static const int PP_PRIORITY_HDR = 10;
    protected static const int PP_PRIORITY_AO = 14;
    protected static const int PP_PRIORITY_SSR = 16;
    protected static const int PP_PRIORITY_SMAA = 18;

    //! Leave any of these empty to skip that effect. Empty is skipped rather than passed
    //! through, because the API treats an empty material as "disable this priority slot".
    //!
    //! Ambient occlusion goes through HBAO rather than the SSDO_*.emat files sitting
    //! beside it: PostProcessEffectType has SSAO and HBAO but no SSDO member, and the
    //! enum names are required to match the effect material class names.
    //! Antialiasing is left to WORLD_PP_PREFAB, which already installs SMAA on camera 0
    //! at priority 17. Adding a second pass here does not sharpen anything — it runs
    //! morphological AA over an already-antialiased frame and smears the whole image,
    //! sky included. Set one of these only if the PP prefab is removed.
    //!
    //! If a single pass ever needs replacing rather than adding: SMAA wants depth and
    //! velocity inputs, which a lightweight "Preview" world may not produce, whereas
    //! FXAA works on final-image luminance alone.
    protected static const string PP_MAT_SMAA = "";
    protected static const string PP_MAT_FXAA = "";
    protected static const string PP_MAT_HBAO = "";
    protected static const string PP_MAT_SSR = "";

    //! Stock HDR post-process, applied to this world's camera slot only.
    //!
    //! Deliberately NOT TDL's Common/Postprocess/HDR_Camera.emat despite the inviting
    //! name — that one carries Saturation 0 and ColorizationColor 0.318 0.071 0.561,
    //! the monochrome-purple wash the AN/ARC-231 screen prefabs use. It tints the whole
    //! scene, sky included. The diorama wants a neutral tonemap.
    protected static const string PP_MATERIAL =
        "{9DEECCABE8357209}Common/Postprocess/HDR.emat";


    //! Untextured fallback. A real in-repo MatPBRBasic — the `defMat.emat` GUID quoted by
    //! the MeshObject.Create reference example does not resolve in this build.
    protected static const string TILE_MATERIAL_PLAIN =
        "{0892B099B5CEF158}Assets/Vehicles/Helicopters/TDL/Data/DefaultGeneratedMaterial.emat";

    //! Map-draped material, declaring BCRMap "$rendertarget" so the painted drape has
    //! somewhere to land. Everything else runs at MatPBRBasic defaults, which means the
    //! map takes sun and cloud shadows rather than reading as a flat unlit sheet.
    protected static const string TILE_MATERIAL_DRAPE =
        "{F6D3F333E55FE844}Assets/TDL/TDL_Map3DDrape.emat";

    protected static const bool MAP_DRAPE_ENABLED = true;

    //! Drape resolution in PHYSICAL pixels. This is the whole island's raster on one
    //! texture, so it divides down hard: 8192 over a 4 km map is ~0.5 m/texel, 4096 is 1.
    //!
    //! ~256 MB of VRAM at 8192, four times the 4096 cost — but painted once and then
    //! frozen, so it is a static allocation rather than per-frame work. The map image
    //! cannot exceed its source resolution no matter how large this gets; what does
    //! sharpen is everything drawn as vectors into the canvas, roads included.
    protected static const int DRAPE_SIZE_PX = 8192;

    //! Frames the drape renders before it is switched off. Disabling the widget freezes
    //! its last content on the mesh, so a painted drape costs nothing from then on.
    protected static const int DRAPE_FREEZE_TICKS = 3;

    //! Roads painted into the drape rather than built as geometry: baked into the texture
    //! they cannot z-fight the terrain, they cost nothing once the drape freezes, and
    //! they take the terrain's lighting for free.
    protected static const bool DRAPE_ROADS_ENABLED = true;

    //! Styling copied from the 2D map's DrawApiTerrainRoads so both surfaces agree.
    //!
    //! The minimum strokes are texel counts, so they describe a real-world width only at
    //! one drape resolution. Doubling the drape would otherwise halve every thin road —
    //! DrapeStrokeScale() rebases them so resolution changes sharpen the line rather than
    //! shrink it.
    protected static const int DRAPE_STROKE_REFERENCE_PX = 4096;
    protected static const float ROAD_STROKE_MIN_HIGHWAY = 2.5;
    protected static const float ROAD_STROKE_MIN_PAVED = 1.8;
    protected static const float ROAD_STROKE_MIN_TRAIL = 1.2;
    protected static const int ROAD_COLOR_HIGHWAY = 0xFFE8C57A;
    protected static const int ROAD_COLOR_PAVED = 0xFFC9B98A;
    protected static const int ROAD_COLOR_TRAIL = 0xFF9C8964;

    //! MGRS grid baked into the drape beside the roads: ground-locked content that
    //! occludes correctly behind hills and costs nothing once the drape freezes — the
    //! same reasoning as the roads. Spacing and colours mirror the 2D DrawGrid.
    protected static const bool DRAPE_GRID_ENABLED = true;
    protected static const float GRID_SPACING_MAJOR_M = 1000;
    protected static const float GRID_SPACING_MINOR_M = 100;
    protected static const int GRID_COLOR_MAJOR = 0x60000000;
    protected static const int GRID_COLOR_MINOR = 0x40000000;

    //! Texel strokes at DRAPE_STROKE_REFERENCE_PX, rebased by the same stroke scale as
    //! the roads so a drape resolution change sharpens the lines instead of fattening
    //! or starving them.
    protected static const float GRID_STROKE_MAJOR = 2.0;
    protected static const float GRID_STROKE_MINOR = 1.0;

    //! TDL shapes (published + the in-progress ghost) painted into the drape on their
    //! own canvas: ground-locked like the 2D map draws them, occluding correctly behind
    //! hills, and taking the terrain's lighting. The drape is frozen most of the time,
    //! so shape changes wake it for a few frames and let it re-freeze (see WakeDrape).
    protected static const bool DRAPE_SHAPES_ENABLED = true;

    //! 2D stroke widths are screen pixels that hold constant under zoom; on the drape a
    //! width is a fixed ground distance that thins as the camera pulls back. Painted at
    //! 1:1 a default 2 px stroke covers ~1 m of ground and vanishes at overview range,
    //! so shape strokes (and dots, arrows, crosshairs) are widened beyond the road/grid
    //! rebase to stay readable at tactical zoom.
    protected static const float DRAPE_SHAPE_STROKE_MUL = 3.0;

    //! Labels get their own multiplier: ground-painted text is minified by distance
    //! like everything else, and screen-sized glyphs would be unreadable smears. This
    //! puts a 10 pt shape label at roughly 30 ground-metres of text height on a 4 km
    //! map — the runway-number aesthetic, legible from operating altitude.
    protected static const float DRAPE_SHAPE_LABEL_MUL = 3.0;

    //! Pill/label geometry mirrored from the 2D map's shape-label consts so both
    //! surfaces render the same proportions.
    protected static const float DRAPE_LABEL_SIZE = 10;
    protected static const float DRAPE_LABEL_CHAR_WIDTH = 6.5;
    protected static const float DRAPE_LABEL_HEIGHT = 12;
    protected static const float DRAPE_LABEL_PAD = 3;
    protected static const int DRAPE_LABEL_BG_COLOR = 0xCC000000;
    protected static const int DRAPE_LABEL_TEXT_COLOR = 0xFFFFFFFF;

    //! Screen-space overlay canvas above the pane, rebuilt every tick like the 2D map's
    //! own command list. It carries the content the drape is wrong for: range-ring
    //! distance labels (must stay legible at any camera distance, where ground-baked
    //! text minifies away) and the bloodhound line (per-frame dynamic — baking it would
    //! keep the drape awake every frame — and its colour must not sink into the terrain
    //! palette). Everything else stays on the drape.
    protected static const bool OVERLAY_CANVAS_ENABLED = true;

    //! Bloodhound styling mirrored from the 2D map so the tool reads identically.
    protected static const int OVERLAY_BLOODHOUND_COLOR = 0xFFC0FF4D;
    protected static const float OVERLAY_BLOODHOUND_WIDTH = 2.0;
    protected static const float OVERLAY_BLOODHOUND_TICK = 12.0;

    //! Mirrors the 2D gate: rings projected smaller than this get no distance label,
    //! so tight ring sets do not stack unreadable text.
    protected static const float OVERLAY_RING_LABEL_MIN_PX = 20.0;
    protected static const float OVERLAY_RING_LABEL_FONT = 9.0;

    //! Authored drape layout. Preferred over building the tree in code, because a layout
    //! states the render target's pixel size declaratively and cannot have it silently
    //! dropped by a slot type the workspace root chose. Leave empty to use the code path.
    //! Expects a root frame containing an RTTextureWidget named "RTTexture".
    protected static const ResourceName DRAPE_LAYOUT = "";

    //! Zoom is polled rather than driven by an action listener. The wheel is an analog
    //! relative source, and a DOWN-trigger listener on it fires unreliably — polling the
    //! value each frame is how the sibling gamepad pan axes in this context already work.
    protected static const string ACTION_ZOOM_IN = "TDLMapZoomIn";
    protected static const string ACTION_ZOOM_OUT = "TDLMapZoomOut";

    //! Low because a mouse wheel notch reports a small value even amplified by the
    //! action's Multiplier, where a gamepad trigger reports most of its 0..1 range.
    //! One threshold serves both only if it clears wheel noise without demanding a
    //! hard scroll — set from the values ZOOM_DIAG reports, not by feel.
    protected static const float ZOOM_DEADZONE = 0.02;

    //! Prints the live zoom axis whenever it is nonzero. Turn on to read the actual
    //! per-notch value on this machine, then set ZOOM_DEADZONE below it. Off by default
    //! because it fires every frame the wheel moves.
    protected static const bool ZOOM_DIAG = false;

    //! Held triggers should keep zooming; a wheel click should step once. An edge latch
    //! plus a repeat delay serves both without needing to know which device fired.
    protected static const float ZOOM_REPEAT_S = 0.12;

    //! Camera slot inside the private world. Must match the index the pane's render
    //! target is bound to — an authored RenderTargetWidget carries its own camera index,
    //! and a world camera configured on a different slot is simply not the one being
    //! rendered.
    protected static const int CAMERA_INDEX = 10;

    //! Tiles per axis. This, not antialiasing, is what sets how smooth the terrain
    //! silhouette is: the grid is TILES_PER_AXIS * TILE_CELLS cells across the whole map,
    //! so on a 4 km map 2 tiles gives 32 m cells and a visibly stepped horizon, while 4
    //! gives 16 m and 8 gives 8 m. Cost is quadratic in both triangles and entity count
    //! (8 per axis is 64 entities and ~524k triangles), and the tile buffers are transient
    //! so the memory cost is per-build rather than resident.
    protected static const int TILES_PER_AXIS = 4;

    protected static const bool STRUCTURES_ENABLED = true;

    //! True scale: buildings shrink by exactly the factor the terrain does, so their
    //! height relative to the ground they stand on is the real one. Raise it only if
    //! built-up areas need to read as silhouettes rather than as surface detail.
    protected static const float STRUCTURE_HEIGHT_MUL = 1.0;

    //! Upper bound on footprints turned into geometry. A dense map can carry thousands,
    //! and every one is 20 verts submitted every frame with no LOD behind it yet.
    protected static const int STRUCTURE_CAP = 6144;

    //! Wall and roof materials. Identical means one mesh per batch; point them at two
    //! different materials and the builder splits automatically to give roofs their own
    //! tone, which is what makes dense built-up areas legible from directly above.
    protected static const string STRUCTURE_MATERIAL_WALLS =
        "{06ECA2A121AD66C8}Assets/TDL/TDL_3DBuildings.emat";
    protected static const string STRUCTURE_MATERIAL_ROOFS =
        "{06ECA2A121AD66C8}Assets/TDL/TDL_3DBuildings.emat";

    //! The diorama is compressed until the whole map fits inside this many private-world
    //! metres. Angular size is scale-invariant, but no vertex may sit beyond the player's
    //! view distance, because global view distance IS the camera far clip — a miniature
    //! larger than that gets its far side clipped away on lower graphics settings.
    protected static const float MAX_SPAN_M = 2000;
    protected static const float VIEWDIST_FRACTION = 0.42;

    protected static const float CAM_FOV = 48;
    protected static const float CAM_NEAR = 1;
    protected static const float CAM_FAR_FLOOR = 6000;
    //! Lifts the eye when the orbit position would sit inside terrain. Disabled: it
    //! silently overrides the pose the user asked for, which reads as the camera fighting
    //! back near hills, and it costs a height sample every frame.
    protected static const bool CAMERA_FLOOR_CLAMP_ENABLED = false;
    protected static const float CAM_CLEARANCE_M = 35;

    //! Bounds focus travel to the built slab. Disabled: panning past the edge is a
    //! legitimate thing to want, and the clamp cannot tell that from an accident.
    protected static const bool PAN_LIMIT_ENABLED = false;
    protected static const float CAM_SMOOTH_RATE = 14;
    protected static const float DEFAULT_DISTANCE = 1300;
    protected static const float MIN_DISTANCE = 60;
    protected static const float MIN_PITCH = -80;
    protected static const float MAX_PITCH = -8;

    //! Taste multiplier on top of the geometrically-derived pan rate. 1.0 means the
    //! ground tracks the cursor exactly; lower feels heavier.
    protected static const float PAN_SENSITIVITY = 1.0;

    //! Floor on sin(pitch) when converting screen-vertical drag to ground distance.
    //! The conversion divides by that sine, so near-horizontal pitch sends it toward
    //! infinity — the ground is nearly edge-on and a pixel really does cover an
    //! unbounded distance. Clamping trades exactness at shallow angles for a pan that
    //! stays controllable.
    protected static const float PAN_PITCH_SIN_FLOOR = 0.25;

    //! Indexed cameras run no eye adaptation, so exposure has to be pushed manually every
    //! frame. Copying the main camera's live adapted value self-calibrates across times of
    //! day; the floor stops a pitch-black frame from driving the diorama to pure white.
    protected static const float HDR_FLOOR = 0.002;

    //! Closed-loop exposure against this world's own measured brightness rather than the
    //! main camera's. Copying the player camera assumes both are looking at comparably
    //! lit scenes, and they are not — the diorama has its own sky, its own sun angle and
    //! no interiors, so the borrowed value ran consistently hot.
    protected static const bool AUTO_EXPOSURE = true;

    //! Middle-grey target. 0.18 is the standard neutral; the pane was metering around
    //! 0.35-0.44, which is roughly a stop over and reads as washed out.
    protected static const float EXPOSURE_TARGET_MID = 0.18;

    //! Night target. Metering a night scene to daytime middle grey is what blows the
    //! terrain out to white: most of the frame is black sky, so driving the average up to
    //! 0.18 can only be done by overexposing the few lit surfaces. A map raster makes it
    //! worse than natural terrain would — it is mostly pale greys and greens, so its
    //! albedo is far higher than the ground it represents. A night scene is supposed to
    //! read dark; this target lets it.
    protected static const float EXPOSURE_TARGET_MID_NIGHT = 0.05;

    //! Seconds to cross between the day and night targets. IsSunSet flips instantly, and
    //! stepping the target would visibly pump the whole image at dawn and dusk.
    protected static const float EXPOSURE_TARGET_BLEND_S = 4.0;

    //! Fraction of the error corrected per second. Low enough that a cloud crossing the
    //! sun does not pump the exposure, high enough to settle within a second or two.
    protected static const float EXPOSURE_ADAPT_RATE = 2.0;

    //! Hard bounds on the closed loop, so a degenerate brightness reading (the first
    //! frame reports 1.0 before anything has rendered) cannot drive exposure to a value
    //! the scene never recovers from.
    protected static const float EXPOSURE_MIN = 0.0005;
    protected static const float EXPOSURE_MAX = 0.05;

    //! Ceiling on exposure as a multiple of the darkest value this lighting has needed.
    //!
    //! The darkest exposure the loop has settled on is the one measured when terrain
    //! filled the frame — that is the reading actually describing the light. Pointing the
    //! camera at sky replaces lit pixels with dark ones, the average collapses, and the
    //! loop compensates by opening up until the terrain is white. Since the lighting does
    //! not change when the camera turns, exposure has no business changing either;
    //! capping against the minimum lets it track real light changes while refusing to
    //! chase framing.
    protected static const float EXPOSURE_MAX_OVER_MIN = 1.35;

    //! Manual trim in stops applied to every world, added to the per-map trim the API serves.
    //! Negative is darker. Both scale the metering target, not the computed exposure — see
    //! UpdateAutoExposure.
    //!
    //! A per-map value exists because metering the average to mid-grey cannot stop a pale
    //! raster's bright regions clipping, and how pale the raster is depends on the image: a
    //! desert map and a temperate island cannot share one number.
    protected static const float EXPOSURE_BIAS_STOPS = 0;

    //! Seconds between clamp-pinned warnings. The condition persists frame after frame once
    //! it starts, and one line per frame would be the only thing left in the log.
    protected static const float EXPOSURE_PINNED_WARN_S = 10.0;

    //! How far measured brightness must sit from target before a pinned clamp is worth
    //! reporting. Within this factor the loop has effectively converged and the clamp is
    //! just where it happened to land.
    protected static const float EXPOSURE_PINNED_FACTOR = 1.5;

    //! Periodic state dump while the pane is open. Off by default because a shipped
    //! session has no use for it; the interval exists so that turning it on while tuning
    //! camera or exposure behaviour does not bury the rest of the log.
    protected static const bool DIAG_ENABLED = false;
    protected static const int DIAG_INTERVAL_MS = 5000;

    protected static const int PANE_Z_ORDER = 20000;

    //! Diorama metres of lift applied to overlay anchors seated on the terrain, so the
    //! surface's own pixels never occlude a marker sitting exactly on it.
    protected static const float OVERLAY_ANCHOR_LIFT = 1.0;

    //! How far above the sampled ground, in diorama units, something has to be before its
    //! icon leaves the terrain. Sampled ground and real terrain disagree by a metre or two —
    //! the model is a grid, the world is not — and without a margin a standing player's icon
    //! would flicker between seated and airborne as they walked.
    protected static const float AIRBORNE_LIFT_MIN = 3.0;

    //! World metres ahead of a marker projected to derive its screen-space heading.
    //! Short enough that terrain curvature between the two samples is negligible, long
    //! enough that the projected pixel delta clears sub-pixel noise at full zoom-out.
    protected static const float HEADING_SAMPLE_AHEAD_M = 20.0;

    //! Name of an authored RenderTargetWidget, looked for in the live menu tree.
    //!
    //! An authored pane is REQUIRED for a sharp image, not a preference. The identical
    //! setup on a code-created RenderTargetWidget renders soft no matter what resolution
    //! scale, call ordering or world class it is given — a layout-authored one is crisp
    //! immediately. Something the layout declares (format being the likeliest) cannot be
    //! reproduced through CreateWidget. The code fallback below exists only so a missing
    //! widget degrades to a soft image instead of no image; it is not an equivalent path.
    protected static const string PANE_WIDGET_NAME = "RenderTarget0";
    protected static const ResourceName PANE_LAYOUT = "";
    protected static const int RT_FPS_ACTIVE = 60;
    protected static const int RT_FPS_ACTIVE_CONSOLE = 30;
    //! Native resolution on both platform classes. The scale only takes effect when it is
    //! applied after SetWorld, which is why it is re-applied there rather than at creation.
    //! Console has not been profiled at 1.0 and is the likelier of the two to need dropping
    //! back toward 0.35.
    protected static const float RT_RES_ACTIVE = 1.0;
    protected static const float RT_RES_ACTIVE_CONSOLE = 1.0;

    //! Host priority. LOWER outranks higher. A request is refused against a live holder unless
    //! it strictly outranks it, which preserves the original two-surface rule (the menu takes
    //! the pane from the device, the device does not take it from the menu, and two devices do
    //! not trade it every frame) and adds a tier below both for the HUD mirror. The mirror is
    //! the fallback surface by definition: hidden whenever the menu is up, outranked by a held
    //! device, so it inherits the pane only when neither is alive to hold it — and both can
    //! take it straight back, which matters because the mirror heartbeats continuously and
    //! would otherwise never look stale enough to displace.
    static const int HOST_RANK_MENU = 0;
    static const int HOST_RANK_WORLDSPACE = 1;
    static const int HOST_RANK_MIRROR = 2;

    //! Root widget name shared by every TDL surface layout — the boundary GetSurfaceRoot stops
    //! at so a pane lookup never escapes into a sibling surface's tree.
    protected static const string SURFACE_ROOT_NAME = "rootFrame";

    //! Render cost is per-surface because the mirror is always on and drawn at a fraction of
    //! the screen. Full scale on a glance widget buys resolution nobody can resolve.
    protected static const int RT_FPS_MIRROR = 30;
    protected static const float RT_RES_MIRROR = 0.35;

    protected static ref AG0_TDLMap3DView s_Instance;

    //! Shared terrain sampler cache — see ResolveSharedHeightSource. Lives outside the view
    //! instance because 2D surfaces sample elevation whether or not the 3D map was ever opened.
    protected static ref AG0_TDLMap3DHeightSource s_TerrainSource;
    protected static BaseWorld s_TerrainSourceWorld;
    protected static bool s_bTerrainSourceIsGrid;

    protected ref SharedItemRef m_WorldRef;
    protected BaseWorld m_World;
    protected BaseWorld m_SourceWorld;
    protected IEntity m_LightingEntity;
    protected IEntity m_TimeWeatherEntity;
    protected IEntity m_EnvProbeEntity;
    protected IEntity m_LightingRigEntity;
    protected IEntity m_PostProcessEntity;
    protected ref array<AG0_TDLMap3DTileEntity> m_aTiles = {};
    protected ref array<AG0_TDLMap3DStructureBatch> m_aStructureBatches = {};

    protected Widget m_wPaneRoot;
    protected RenderTargetWidget m_wPane;
    protected bool m_bPaneIsAuthored;
    protected Widget m_wDrapeRoot;
    protected RTTextureWidget m_wDrapeRT;
    protected int m_iDrapeFreezeTicks;

    //! Kept so a raster the server pushes after the slab was built can replace the one baked
    //! into the drape. Without it the two map surfaces disagree until the next 3D rebuild.
    protected ImageWidget m_wDrapeRaster;
    protected int m_iDrapeSatelliteRevision = -1;
    protected Widget m_wHost;

    protected float m_fScale = 1;
    protected float m_fSpanX;
    protected float m_fSpanZ;
    protected float m_fMapMinX;
    protected float m_fMapMinZ;
    protected ref array<ref CanvasWidgetCommand> m_aDrapeCommands;
    protected ref array<ref CanvasWidgetCommand> m_aDrapeGridCommands;
    protected ref array<ref CanvasWidgetCommand> m_aDrapeShapeCommands;
    protected CanvasWidget m_wShapesCanvas;

    //! Every drape painter surface, kept only so the freeze tick can measure them. A
    //! canvas that comes back smaller than the drape is silently clipping its painter,
    //! which is invisible until someone notices content stopping mid-map.
    protected ref array<CanvasWidget> m_aDrapeCanvases = {};
    protected ref array<string> m_aDrapeCanvasLabels = {};
    protected int m_iDrapeShapeSignature;
    protected bool m_bDrapeGhostWasActive;
    protected bool m_bDrapeMeasured;

    //! Milliseconds without a RequestHost from the hosting surface before that host is
    //! treated as gone. A live surface calls in every drawn frame, so anything past a
    //! few frames means its tree was destroyed or it stopped rendering — 500 ms sits
    //! comfortably past a frame hitch and comfortably before a human notices the wait.
    protected static const int HOST_STALE_MS = 500;

    protected int m_iHostRank;
    protected int m_iHostHeartbeatMs;

    protected CanvasWidget m_wOverlayCanvas;
    protected ref array<ref CanvasWidgetCommand> m_aOverlayCommands;
    protected ref array<ref AG0_TDLMapShape> m_aOverlayShapes;
    protected AG0_TDLMapShape m_OverlayGhost;
    protected bool m_bBloodhoundEnabled;
    protected vector m_vBloodhoundCursor;
    protected vector m_vBloodhoundDevice;
    protected vector m_vCameraTarget;
    protected vector m_vTargetFocus;
    protected float m_fPanLimitX;
    protected float m_fPanLimitZ;
    protected vector m_vCameraRotation;
    protected vector m_vTargetRotation;
    protected float m_fCameraDistance;
    protected float m_fTargetDistance;
    protected float m_fMaxDistance;
    protected bool m_bBuilt;
    protected bool m_bOpen;
    protected int m_iLastTickMs;
    protected bool m_bZoomLatched;
    protected float m_fZoomHeldS;
    protected int m_iDiagLastMs;
    protected float m_fLastExposure;
    protected float m_fLastSceneMid;
    protected float m_fExposure;
    protected float m_fExposureTarget;
    protected float m_fExposureMinSeen;
    protected float m_fExposurePinnedWarnTimer;
    protected float m_fLastWantedTarget;

    //------------------------------------------------------------------------------------------------
    //! Open the 3D map over `host`, or close it if already open. Returns the new open state.
    //! `focusWorld` seeds the orbit focus (pass the 2D view's centre so the mode switch
    //! reads as the same map standing up); vector.Zero leaves the previous focus alone.
    static bool Toggle(Widget host, int hostRank, vector focusWorld = vector.Zero)
    {
        if (!s_Instance)
            s_Instance = new AG0_TDLMap3DView();

        if (s_Instance.IsOpen())
        {
            s_Instance.Close();
            return false;
        }

        return s_Instance.Open(host, hostRank, focusWorld);
    }

    //------------------------------------------------------------------------------------------------
    static AG0_TDLMap3DView GetInstance()
    {
        DiscardStaleInstance();
        return s_Instance;
    }

    static bool IsViewOpen()
    {
        DiscardStaleInstance();
        return s_Instance && s_Instance.IsOpen();
    }

    //! Open AND currently being painted by a surface. m_bOpen deliberately outlives a frontend
    //! teardown so a reopened menu rehosts the same diorama — which means IsViewOpen stays true
    //! for a pane nobody is drawing, and the camera state behind it is frozen wherever the last
    //! host left it. A read-only consumer (the HUD mirror follows the orbit focus) has to gate
    //! on liveness or it latches to that frozen focus permanently. The heartbeat is the signal:
    //! a live host refreshes it every RequestHost, so a stale one means no host at all.
    static bool IsHostLive()
    {
        DiscardStaleInstance();
        return s_Instance && s_Instance.HasLiveHost();
    }

    //! Hand the pane back before a surface destroys the tree it lives in.
    //!
    //! Only the surface itself can know its tree is about to go. The heartbeat gets there
    //! eventually, but for up to half a second the view holds a RenderTargetWidget handle into
    //! freed widgets, and the next Rehost would unbind a render target that no longer exists.
    //! Deliberately does not Close(): the view stays open so the next live surface inherits it.
    static void ReleaseHostForSurface(Widget surfaceRoot)
    {
        if (!s_Instance || !surfaceRoot)
            return;

        s_Instance.ReleaseIfHostedUnder(surfaceRoot);
    }

    void ReleaseIfHostedUnder(Widget surfaceRoot)
    {
        if (!m_wHost)
            return;

        if (GetSurfaceRoot(m_wHost) != surfaceRoot)
            return;

        ReleasePane();
    }

    bool HasLiveHost()
    {
        if (!m_bOpen || !m_wPane)
            return false;

        return System.GetTickCount() - m_iHostHeartbeatMs <= HOST_STALE_MS;
    }

    //------------------------------------------------------------------------------------------------
    //! Drop the singleton when the world it was built against is gone.
    //!
    //! The instance is static so a diorama survives the menu closing, which is what makes
    //! reopening free. It does NOT survive a mission or world change: the private world, its
    //! tiles and the pane binding all died with the old level, while m_bOpen and m_bBuilt
    //! stayed true. The next menu open then found a view that believed it was already
    //! standing, showed a pane bound to nothing, and needed toggling twice to recover.
    //!
    //! Nothing is torn down here on purpose. Everything this instance owned belonged to the
    //! old world and went with it; reaching into those references now would be touching
    //! freed render resources. Dropping the handle is the whole operation.
    protected static void DiscardStaleInstance()
    {
        if (!s_Instance || !s_Instance.m_SourceWorld)
            return;

        if (s_Instance.m_SourceWorld == GetGame().GetWorld())
            return;

        Print("[TDL_MAP3D] Source world changed — discarding the stale 3D view",
            LogLevel.DEBUG);
        s_Instance = null;
    }

    bool IsOpen()
    {
        return m_bOpen;
    }

    //------------------------------------------------------------------------------------------------
    //! True while the pane still belongs to the widget tree it was created under.
    bool IsHostedBy(Widget host)
    {
        return m_wPane && m_wHost == host;
    }

    //------------------------------------------------------------------------------------------------
    //! Surface arbitration. Every view drawing while the 3D map is open calls this each
    //! frame; the call doubles as the host's liveness heartbeat. The fullscreen menu
    //! always outranks the world-space device — a menu on screen means the device
    //! display is literally behind it — and the device may claim the pane only once
    //! the current host has stopped drawing (menu closed, or device holstered).
    //! Without the rank rule, two live surfaces would Rehost() the pane back and forth
    //! every frame. Returns true when `host` owns the pane after the call.
    //! Last verdict printed, so a per-frame decision only reaches the log when it changes.
    protected static string s_sLastHostDecision;

    protected void ReportHostDecision(string decision)
    {
        if (decision == s_sLastHostDecision)
            return;

        s_sLastHostDecision = decision;
        Print("[TDL_MAP3D] host " + decision, LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    bool RequestHost(Widget host, int requestRank)
    {
        if (!host)
            return false;

        if (!m_bOpen)
        {
            ReportHostDecision("refused: view is not open");
            return false;
        }

        int now = System.GetTickCount();
        if (IsHostedBy(host))
        {
            // Visibility is re-asserted, not assumed. The invariant this class owns is
            // "the authored pane is visible exactly while this class holds it", and the
            // frontends hide RenderTarget0 whenever they (re)initialise a tree so a
            // surface that has NOT claimed the pane cannot cover its 2D map with an
            // unbound black rectangle. On the world-space device that initialise runs
            // again on every re-equip, against a tree that outlives the controller — so
            // without this the pane would be hidden underneath a 3D map that still
            // believes it is hosted here, and no rehost would ever fire to bring it back.
            if (m_wPane && !m_wPane.IsVisible())
                m_wPane.SetVisible(true);

            m_iHostHeartbeatMs = now;
            return true;
        }

        bool currentDead = !m_wPane || now - m_iHostHeartbeatMs > HOST_STALE_MS;

        // Strictly-better-or-equal is refused, not just lower: two held devices each run their
        // own display controller and both request at WORLDSPACE, and letting equal ranks tie
        // would restore the per-frame steal this arbitration exists to stop. Using the holder's
        // rank rather than a fixed tier is what lets a device take the pane back from the
        // mirror — the mirror heartbeats at ~18 Hz, so it never goes stale on its own and a
        // fixed rule would lock the device out for good once it had inherited.
        if (requestRank >= m_iHostRank && !currentDead)
        {
            ReportHostDecision("refused: request does not outrank the live host");
            return false;
        }

        // Set BEFORE the rehost, because CreatePane reads it to pick the render budget.
        // Restored on failure so a refused contender does not leave its rank behind as the
        // holder's — which would then decide the next surface's resolution.
        int previousRank = m_iHostRank;
        m_iHostRank = requestRank;

        Rehost(host);
        if (!IsHostedBy(host))
        {
            m_iHostRank = previousRank;
            ReportHostDecision("refused: rehost did not take");
            return false;
        }
        m_iHostHeartbeatMs = now;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Topmost widget of the SURFACE that owns `host` — the child of the parentless
    //! workspace root, not the workspace root itself. Searching from the absolute root
    //! stopped being safe the moment two surfaces can exist at once: both spawn the
    //! same layout, so an absolute-root FindAnyWidget can hand back the OTHER surface's
    //! RenderTarget0.
    //! Stops at the spawned layout's own root, which every TDL surface names "rootFrame".
    //!
    //! The climb-to-just-below-the-workspace-root fallback holds for a surface whose tree hangs
    //! directly off the workspace, which was true of both original surfaces. The HUD mirror is
    //! parented under the HUD manager's root instead, so the same walk would climb straight past
    //! the mirror and hand back a HUD-wide subtree — and the pane lookup would then be free to
    //! bind whatever RenderTarget0 it found first, on any surface.
    protected Widget GetSurfaceRoot(Widget host)
    {
        Widget node = host;
        while (node)
        {
            if (node.GetName() == SURFACE_ROOT_NAME)
                return node;

            Widget parent = node.GetParent();
            if (!parent)
                break;

            node = parent;
        }

        node = host;
        while (node.GetParent())
        {
            Widget parent = node.GetParent();
            if (!parent.GetParent())
                break;

            node = parent;
        }
        return node;
    }

    //------------------------------------------------------------------------------------------------
    //! Rebuild the pane under a new host.
    //!
    //! The pane is a child of the menu's widget tree, so closing the TDL menu destroys it
    //! while this class still believes it is open — the view then renders nothing and the
    //! 2D map stays suppressed behind a pane that no longer exists. Reopening the menu
    //! builds a fresh tree, and the view rehosts onto it rather than requiring the user to
    //! toggle 3D off and on to recover.
    void Rehost(Widget host)
    {
        if (!host)
            return;

        ReleasePane();
        if (!CreatePane(host))
        {
            Print("[TDL_MAP3D] Rehost failed — closing 3D map", LogLevel.WARNING);
            m_bOpen = false;
            return;
        }

        // ReleasePane clears the host along with the pane, and CreatePane does not set it —
        // only Open ever did. Without recording it here, IsHostedBy compares against null
        // forever, so the very next frame decides the pane needs rehosting again and the
        // view tears down and rebuilds itself every frame for as long as 3D is up.
        m_wHost = host;

        // Same reason Open zeroes it: the clock kept running while the menu was shut, so a
        // first tick measured against it would hand every glide a time slice the length of
        // however long the pane was away and snap the camera on the frame it comes back.
        m_iLastTickMs = 0;

        Print("[TDL_MAP3D] Rehosted pane onto a new menu tree", LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    bool Open(Widget host, int hostRank, vector focusWorld = vector.Zero)
    {
        if (m_bOpen || !host)
            return false;

        m_wHost = host;

        // Before CreatePane below, which reads it to pick the render budget. The opening
        // surface is the host by definition, so there is no earlier claim to preserve.
        m_iHostRank = hostRank;

        m_SourceWorld = GetGame().GetWorld();
        if (!m_SourceWorld)
            return false;

        int buildStart = System.GetTickCount();
        bool freshBuild = !m_bBuilt;

        if (freshBuild)
        {
            if (!BuildDiorama())
            {
                Print("[TDL_MAP3D] Diorama build failed", LogLevel.WARNING);
                return false;
            }
        }
        else
        {
            // Reusing a session-persistent world: nothing in BuildDiorama runs again, so
            // anything that tracks the outside world has to be refreshed here or it stays
            // frozen at whatever it was when the world was first created.
            SyncDioramaTime();
        }

        if (!CreatePane(host))
        {
            Print("[TDL_MAP3D] Pane creation failed", LogLevel.WARNING);
            return false;
        }

        m_bOpen = true;
        m_iLastTickMs = 0;
        m_iDiagLastMs = 0;

        // The opening surface is the host by definition — arbitration starts fresh.
        m_iHostHeartbeatMs = System.GetTickCount();

        // Open looking at what the 2D map was looking at, so switching modes reads as
        // the same map standing up rather than a jump to wherever 3D was last left.
        // Snapped, not glided: there is no previous 3D frame on screen to glide from.
        if (focusWorld != vector.Zero)
        {
            FocusOnWorld(focusWorld);
            m_vCameraTarget = m_vTargetFocus;
        }

        // The cap is re-earned each session: the world may have been rebuilt, and the
        // time of day almost certainly moved while the pane was closed.
        m_fExposureMinSeen = 0;
        m_fLastWantedTarget = 0;
        m_fExposurePinnedWarnTimer = 0;

        Print(string.Format(
            "[TDL_MAP3D] OPEN %1ms | freshBuild=%2 tiles=%3 scale=%4 span=%5x%6 m | drape=%7 frozen=%8",
            System.GetTickCount() - buildStart, freshBuild, m_aTiles.Count(), m_fScale,
            m_fSpanX, m_fSpanZ, MAP_DRAPE_ENABLED, m_iDrapeFreezeTicks == 0),
            LogLevel.DEBUG);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected bool BuildDiorama()
    {
        AG0_TDLMap3DHeightSource source = ResolveHeightSource();
        if (!source)
            return false;

        float minX;
        float minZ;
        float spanX;
        float spanZ;
        if (!source.GetExtent(minX, minZ, spanX, spanZ))
            return false;

        m_fSpanX = spanX;
        m_fSpanZ = spanZ;
        m_fMapMinX = minX;
        m_fMapMinZ = minZ;

        float viewDistance = GetGame().GetViewDistance();
        if (viewDistance < 1000)
            viewDistance = 1000;

        float spanCap = viewDistance * VIEWDIST_FRACTION;
        if (spanCap > MAX_SPAN_M)
            spanCap = MAX_SPAN_M;

        float largestSpan = spanX;
        if (spanZ > largestSpan)
            largestSpan = spanZ;

        m_fScale = largestSpan / spanCap;
        if (m_fScale < 1)
            m_fScale = 1;

        float centerX = minX + spanX * 0.5;
        float centerZ = minZ + spanZ * 0.5;

        m_WorldRef = BaseWorld.CreateWorld(WORLD_TYPE, WORLD_NAME);
        if (!m_WorldRef || !m_WorldRef.IsValid())
        {
            Print("[TDL_MAP3D] CreateWorld returned no usable ref", LogLevel.WARNING);
            return false;
        }

        // Direct assignment rather than a Cast: GetRef returns the shared item the engine
        // already typed as a world, which is how the engine's own preview-world example
        // reads it back.
        World sharedWorld = m_WorldRef.GetRef();
        if (!sharedWorld)
        {
            Print("[TDL_MAP3D] CreateWorld ref did not resolve to a world", LogLevel.WARNING);
            m_WorldRef = null;
            return false;
        }

        if (WORLD_RELOAD_SYSTEMS)
            sharedWorld.ReloadSystems();

        m_World = sharedWorld;
        Print(string.Format("[TDL_MAP3D] World class '%1' created (reloadSystems=%2)",
            WORLD_TYPE, WORLD_RELOAD_SYSTEMS), LogLevel.DEBUG);

        // A missing environment renders the diorama black, which is indistinguishable from
        // a broken render path. Both failure points are reported separately so a black pane
        // can be told apart from a black world.
        Resource environmentResource = Resource.Load(WORLD_ENV_PREFAB);
        if (!environmentResource || !environmentResource.IsValid())
        {
            Print("[TDL_MAP3D] World environment prefab failed to load — diorama will render unlit",
                LogLevel.WARNING);
        }
        else
        {
            m_LightingEntity = GetGame().SpawnEntityPrefab(environmentResource, m_World);
            if (!m_LightingEntity)
                Print("[TDL_MAP3D] World environment prefab spawned null", LogLevel.WARNING);
        }

        SpawnPeerPrefab(LIGHTING_PREFAB, "lighting", m_LightingRigEntity);
        SpawnPeerPrefab(WORLD_PP_PREFAB, "post-process", m_PostProcessEntity);
        SpawnTimeAndWeather();

        // Tiles are sized so their boundaries land on shared sample columns: each tile
        // spans TILE_CELLS steps and starts where the previous one ended, so adjacent
        // tiles resample the identical world position and no seam can open between them.
        int tilesX = TILES_PER_AXIS;
        int tilesZ = TILES_PER_AXIS;
        int cellsX = tilesX * AG0_TDLMap3DTileEntity.TILE_CELLS;
        int cellsZ = tilesZ * AG0_TDLMap3DTileEntity.TILE_CELLS;
        float stepX = spanX / cellsX;
        float stepZ = spanZ / cellsZ;

        m_fMaxDistance = largestSpan / m_fScale * 0.65;
        if (m_fMaxDistance < 400)
            m_fMaxDistance = 400;

        float guardRadius = m_fMaxDistance * 1.5;

        for (int tz = 0; tz < tilesZ; tz = tz + 1)
        {
            for (int tx = 0; tx < tilesX; tx = tx + 1)
            {
                EntitySpawnParams params = new EntitySpawnParams();
                Math3D.MatrixIdentity4(params.Transform);

                AG0_TDLMap3DTileEntity tile = AG0_TDLMap3DTileEntity.Cast(
                    GetGame().SpawnEntity(AG0_TDLMap3DTileEntity, m_World, params));
                if (!tile)
                    continue;

                float tileMinX = minX + tx * AG0_TDLMap3DTileEntity.TILE_CELLS * stepX;
                float tileMinZ = minZ + tz * AG0_TDLMap3DTileEntity.TILE_CELLS * stepZ;

                string tileMaterial = TILE_MATERIAL_PLAIN;
                if (MAP_DRAPE_ENABLED)
                    tileMaterial = TILE_MATERIAL_DRAPE;

                if (tile.Build(source, tileMinX, tileMinZ, stepX, stepZ,
                    centerX, centerZ, m_fScale, guardRadius, tileMaterial,
                    minX, minZ, spanX, spanZ))
                {
                    m_aTiles.Insert(tile);
                }
                else
                {
                    SCR_EntityHelper.DeleteEntityAndChildren(tile);
                }
            }
        }

        if (m_aTiles.IsEmpty())
        {
            Print("[TDL_MAP3D] No tiles built", LogLevel.WARNING);
            return false;
        }

        // A flat render has two very different causes — the sampler returned nothing, or
        // the miniaturisation squashed real relief below visibility. Reporting the raw
        // world-metre range next to its diorama-metre equivalent separates them on sight.
        float sampledMin = m_aTiles[0].GetMinSampledHeight();
        float sampledMax = m_aTiles[0].GetMaxSampledHeight();
        for (int t = 1, tileCount = m_aTiles.Count(); t < tileCount; t = t + 1)
        {
            if (m_aTiles[t].GetMinSampledHeight() < sampledMin)
                sampledMin = m_aTiles[t].GetMinSampledHeight();
            if (m_aTiles[t].GetMaxSampledHeight() > sampledMax)
                sampledMax = m_aTiles[t].GetMaxSampledHeight();
        }
        Print(string.Format(
            "[TDL_MAP3D] Extent %1 x %2 m | height %3..%4 m -> %5 diorama m relief | grid %6 cells @ %7 m",
            spanX, spanZ, sampledMin, sampledMax,
            (sampledMax - sampledMin) * AG0_TDLMap3DTileEntity.HEIGHT_EXAGGERATION / m_fScale,
            cellsX, stepX),
            LogLevel.DEBUG);

        // After the tiles, because both seat themselves against the built surface.
        SpawnEnvProbe();
        BuildStructures(centerX, centerZ);

        if (MAP_DRAPE_ENABLED)
            PaintMapDrape();

        ConfigureCamera();

        // Focus travel is bounded by the slab itself so panning can never leave the
        // built terrain and stare into empty space.
        m_fPanLimitX = spanX / m_fScale * 0.5;
        m_fPanLimitZ = spanZ / m_fScale * 0.5;

        // Start the pivot on the surface at map centre, matching where panning will keep
        // it — otherwise the first pan visibly jumps the view as the focus climbs out of
        // the ground.
        float centreY = 0;
        SampleDioramaY(0, 0, centreY);
        m_vCameraTarget = Vector(0, centreY, 0);
        m_vTargetFocus = m_vCameraTarget;
        m_vCameraRotation = Vector(-45, -32, 0);
        m_vTargetRotation = m_vCameraRotation;
        m_fCameraDistance = DEFAULT_DISTANCE;
        if (m_fCameraDistance > m_fMaxDistance)
            m_fCameraDistance = m_fMaxDistance;
        m_fTargetDistance = m_fCameraDistance;

        ApplyCamera();
        m_bBuilt = true;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Spawn the time/weather manager into the private world and match it to the real
    //! world's clock. Without the sync the diorama runs on whatever the prefab defaults
    //! to, so a night operation would be briefed over a sunlit map.
    //!
    //! The manager is reached through the spawn return value rather than through
    //! ChimeraWorld: the private world is the lightweight "Preview" class, so the
    //! ChimeraWorld accessors the main world offers are not available on it.
    //! Match the diorama clock to the real world's. Called on every open, not just at
    //! build: the world is session-persistent, so a diorama built at noon keeps noon
    //! lighting after the mission rolls into night. Exposure is copied from the main
    //! camera, which by then is adapted for a dark scene — that mismatch is what blew
    //! the pane out to white on reopen after a time change.
    protected void SyncDioramaTime()
    {
        TimeAndWeatherManagerEntity dioramaWeather =
            TimeAndWeatherManagerEntity.Cast(m_TimeWeatherEntity);
        if (!dioramaWeather)
        {
            Print("[TDL_MAP3D] No diorama time manager — lighting will not track the world",
                LogLevel.WARNING);
            return;
        }

        ChimeraWorld sourceChimera = ChimeraWorld.CastFrom(m_SourceWorld);
        if (!sourceChimera)
            return;

        TimeAndWeatherManagerEntity sourceWeather = sourceChimera.GetTimeAndWeatherManager();
        if (!sourceWeather)
            return;

        TimeContainer sourceTime = sourceWeather.GetTime();
        if (!sourceTime)
            return;

        float timeOfDay = sourceTime.ToTimeOfTheDay();
        bool applied = dioramaWeather.SetDateTimePreview(true, -1, -1, -1, timeOfDay);

        Print(string.Format("[TDL_MAP3D] Time sync %1:%2 (%3h, applied=%4, sunSet=%5)",
            sourceTime.m_iHours, sourceTime.m_iMinutes, timeOfDay, applied,
            sourceWeather.IsSunSet()), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    protected void SpawnTimeAndWeather()
    {
        Resource weatherResource = Resource.Load(TIME_WEATHER_PREFAB);
        if (!weatherResource || !weatherResource.IsValid())
        {
            Print("[TDL_MAP3D] Time/weather prefab failed to load — sun will have no position",
                LogLevel.WARNING);
            return;
        }

        IEntity spawned = GetGame().SpawnEntityPrefab(weatherResource, m_World);
        if (!spawned)
        {
            Print("[TDL_MAP3D] Time/weather prefab spawned null", LogLevel.WARNING);
            return;
        }

        m_TimeWeatherEntity = spawned;
        SyncDioramaTime();
    }

    //------------------------------------------------------------------------------------------------
    //! Draw the map raster into an RTTextureWidget and bind that onto the terrain meshes.
    //!
    //! The indirection is not optional. Binding a texture to a material at runtime is
    //! closed off (SetParam returns false, Material.Create crashes), and an authored
    //! material cannot reference a UI-imported .edds directly — it loads clean, samples
    //! nothing, and renders white. UI textures ARE legal inside a widget, so the working
    //! route is: draw the raster as an ImageWidget into an RTTextureWidget, then bind
    //! that widget onto the mesh through a material declaring BCRMap "$rendertarget".
    //!
    //! TILE_MATERIAL_DRAPE must therefore be an authored material whose body is:
    //!     MatPBRBasic {
    //!      Color 1 1 1 1
    //!      CastShadow 0
    //!      NoDecals 1
    //!      RoughnessScale 1
    //!      MetalnessScale 0
    //!      DielectricReflectance 0
    //!      BCRMap "$rendertarget"
    //!     }
    //! Add Emissive 1 1 1 1 / EmissiveLV 0.5 / ApplyAlbedoToEmissive 1 / ReceiveShadow 0
    //! for a flat unlit map; leave them off to let the sun and cloud shadows fall on it.
    //!
    //! SetRenderTarget takes one entity, so it is called once per tile: the binding lives
    //! on the mesh object, not on the widget, and every tile carries its own. That is also
    //! why teardown has to unbind per tile (see DestroyWorld) rather than nulling the
    //! widget's target once.
    protected void PaintMapDrape()
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
            return;

        // An authored layout states the target's pixel size declaratively, which is the
        // only way to be certain it applied — the code path below depends on slot types
        // chosen by the workspace root, and a rejected size there fails silently.
        if (DRAPE_LAYOUT != string.Empty)
        {
            m_wDrapeRoot = workspace.CreateWidgets(DRAPE_LAYOUT);
            if (m_wDrapeRoot)
                m_wDrapeRT = RTTextureWidget.Cast(m_wDrapeRoot.FindAnyWidget("RTTexture"));

            if (!m_wDrapeRT)
            {
                Print("[TDL_MAP3D] Drape layout missing an RTTexture widget", LogLevel.WARNING);
                return;
            }
        }

        // Code-built fallback. The RT needs a Frame-slotted PARENT or its size never
        // applies. IGNORE_CURSOR | NOFOCUS on every node: this is a workspace-rooted
        // 4096-unit frame that exists only to give the target a sized parent, and without
        // those flags it is a screen-sized hit target that swallows every drag.
        if (!m_wDrapeRT)
        {
            m_wDrapeRoot = workspace.CreateWidget(WidgetType.FrameWidgetTypeID,
                WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
                new Color(0, 0, 0, 0), 0, null);
            if (!m_wDrapeRoot)
            {
                Print("[TDL_MAP3D] Drape container creation failed", LogLevel.WARNING);
                return;
            }

            m_wDrapeRT = RTTextureWidget.Cast(workspace.CreateWidget(
                WidgetType.RTTextureWidgetTypeID,
                WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
                new Color(0, 0, 0, 1), 0, m_wDrapeRoot));
            if (!m_wDrapeRT)
            {
                Print("[TDL_MAP3D] Drape RT creation failed", LogLevel.WARNING);
                return;
            }
        }

        // Slot sizes are LOGICAL units that the engine multiplies by UI scale to reach
        // physical pixels, so passing the pixel count directly under a sub-1.0 UI scale
        // paints the raster into only part of the target and the rest is stretched — the
        // drape loses resolution before the mesh ever samples it. Convert so the physical
        // result is DRAPE_SIZE_PX whatever the user's interface scale.
        float drapeLogical = workspace.DPIUnscale(DRAPE_SIZE_PX);
        FrameSlot.SetSize(m_wDrapeRoot, drapeLogical, drapeLogical);
        FrameSlot.SetPos(m_wDrapeRT, 0, 0);
        FrameSlot.SetSize(m_wDrapeRT, drapeLogical, drapeLogical);

        // Resolved through the 2D view rather than named here, so a world configured for
        // the 2D map is configured for this one too and neither can drift onto a raster
        // the other does not know about. An unresolvable raster is not fatal: the terrain
        // still renders, and the roads and grid below still give it readable structure.
        ResourceName raster = AG0_TDLMapView.ResolveSatelliteTexture();
        m_iDrapeSatelliteRevision = AG0_TDLMapSatelliteOverride.GetRevision();
        if (raster.IsEmpty())
        {
            Print("[TDL_MAP3D] No satellite raster for this world — drape carries vectors only",
                LogLevel.WARNING);
        }
        else
        {
            ImageWidget rasterImage = ImageWidget.Cast(workspace.CreateWidget(
                WidgetType.ImageWidgetTypeID,
                WidgetFlags.VISIBLE | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR
                    | WidgetFlags.NOFOCUS,
                new Color(1, 1, 1, 1), 0, m_wDrapeRT));
            if (!rasterImage)
            {
                Print("[TDL_MAP3D] Drape image creation failed", LogLevel.WARNING);
                return;
            }

            FrameSlot.SetPos(rasterImage, 0, 0);
            FrameSlot.SetSize(rasterImage, drapeLogical, drapeLogical);
            rasterImage.LoadImageTexture(0, raster);
            m_wDrapeRaster = rasterImage;
        }

        // Painters are handed the PHYSICAL pixel extent, not the logical one used for the
        // slot calls above. Slot sizing is in logical units that the engine multiplies by
        // interface scale; canvas draw commands are in the widget's real pixels — the same
        // space GetScreenSize reports, which is what AG0_TDLMapView sizes its own commands
        // against. Passing the logical number to a painter divides every coordinate by the
        // user's interface scale while the target stays full size, which packs the whole
        // map's roads, grid and shapes into a fraction of the drape at the origin corner.
        PaintDrapeRoads(workspace, DRAPE_SIZE_PX);
        PaintDrapeGrid(workspace, DRAPE_SIZE_PX);
        CreateDrapeShapesCanvas(workspace);

        for (int i = 0, count = m_aTiles.Count(); i < count; i = i + 1)
        {
            if (m_aTiles[i])
                m_wDrapeRT.SetRenderTarget(m_aTiles[i]);
        }

        m_iDrapeFreezeTicks = DRAPE_FREEZE_TICKS;

        // Measurement is deferred to the freeze tick, not taken here: GetScreenSize
        // reports zero until the widget has been through a layout pass, so measuring on
        // the creation frame says nothing about whether the size applied.
        Print(string.Format("[TDL_MAP3D] Drape requested %1px over %2 tiles",
            DRAPE_SIZE_PX, m_aTiles.Count()), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    //! Spawn a world-scoped prefab at the origin and hold a reference for teardown.
    //! Load and spawn failures are reported separately because they mean different
    //! things: a bad GUID versus a prefab the private world class refused to accept.
    protected void SpawnPeerPrefab(ResourceName prefab, string label, out IEntity stored)
    {
        stored = null;

        Resource resource = Resource.Load(prefab);
        if (!resource || !resource.IsValid())
        {
            Print(string.Format("[TDL_MAP3D] %1 prefab failed to load", label), LogLevel.WARNING);
            return;
        }

        stored = GetGame().SpawnEntityPrefab(resource, m_World);
        if (!stored)
            Print(string.Format("[TDL_MAP3D] %1 prefab spawned null", label), LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    //! One painter surface covering the whole drape.
    //!
    //! Two unit systems meet on this widget and they are not interchangeable. The slot
    //! call below is in LOGICAL units, which the engine multiplies by the user's interface
    //! scale to reach real pixels — hence DPIUnscale. Everything the painters draw is in
    //! PHYSICAL pixels, the space GetScreenSize reports and the space AG0_TDLMapView and
    //! AG0_TDLPhotoRenderer both size their commands against. Conflating the two is what
    //! compressed the roads, grid and shapes into a fraction of the drape at the origin
    //! corner, scaled down by exactly the interface scale while the target stayed 8192.
    //! The conversion therefore lives here and nowhere else, so no painter is ever holding
    //! a logical number it could mistake for a drawing coordinate.
    //!
    //! The canvas's virtual unit space (SetSizeInUnits / SetZoom) is deliberately NOT
    //! touched: AG0_TDLPhotoRenderer records that it reads ~1000x1000 regardless of the
    //! widget's real dimensions, and both it and AG0_TDLMapView size their draw commands
    //! from GetScreenSize instead. Commands are pixels, not units.
    //!
    //! STRETCH is belt and braces rather than the fix — the freeze-tick measurement below
    //! confirms these canvases already come back at the full drape size — but it is what
    //! guarantees the fill if a slot call ever fails to land on a render-target child, the
    //! way TDL_WorldSpaceDisplayComponent records FrameSlot being unreliable against one.
    //!
    //! @param blend Set for any painter whose colours carry alpha. Without the flag the
    //!              engine composites those draws opaque.
    protected CanvasWidget CreateDrapeCanvas(WorkspaceWidget workspace, bool blend,
        string label)
    {
        if (!m_wDrapeRT)
            return null;

        WidgetFlags flags = WidgetFlags.VISIBLE | WidgetFlags.STRETCH
            | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS;
        if (blend)
            flags = WidgetFlags.VISIBLE | WidgetFlags.STRETCH | WidgetFlags.BLEND
                | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS;

        CanvasWidget canvas = CanvasWidget.Cast(workspace.CreateWidget(
            WidgetType.CanvasWidgetTypeID, flags,
            new Color(1, 1, 1, 1), 0, m_wDrapeRT));
        if (!canvas)
        {
            Print(string.Format("[TDL_MAP3D] Drape %1 canvas creation failed", label),
                LogLevel.WARNING);
            return null;
        }

        // Logical units here, physical pixels in the painters — see PaintMapDrape. The
        // conversion is done inside this factory so that number never reaches a caller
        // that could mistake it for a drawing coordinate.
        float drapeLogical = workspace.DPIUnscale(DRAPE_SIZE_PX);
        FrameSlot.SetPos(canvas, 0, 0);
        FrameSlot.SetSize(canvas, drapeLogical, drapeLogical);

        m_aDrapeCanvasLabels.Insert(label);
        m_aDrapeCanvases.Insert(canvas);

        return canvas;
    }

    //------------------------------------------------------------------------------------------------
    //! Swap the drape's map image for one the server pushed after the slab was built.
    //!
    //! Only replaces an image that already exists. A world that resolved no raster at build
    //! time has no image widget to update, and creating one now would put it above the road,
    //! grid and shape canvases already parented to the render target — the picture would
    //! cover the vectors instead of sitting under them. That case waits for the next rebuild.
    void RefreshDrapeRaster()
    {
        if (m_iDrapeSatelliteRevision == AG0_TDLMapSatelliteOverride.GetRevision())
            return;

        m_iDrapeSatelliteRevision = AG0_TDLMapSatelliteOverride.GetRevision();

        if (!m_wDrapeRaster)
            return;

        ResourceName raster = AG0_TDLMapView.ResolveSatelliteTexture();
        if (raster.IsEmpty())
            return;

        m_wDrapeRaster.LoadImageTexture(0, raster);
    }

    //------------------------------------------------------------------------------------------------
    //! Draw the road network into the drape, over the map image, on a CanvasWidget.
    //!
    //! Same source and same styling as the 2D map's DrawApiTerrainRoads — priority picks
    //! the colour and minimum stroke, and round joins fill the outside of bends — so the
    //! two surfaces cannot disagree about what the network looks like.
    //!
    //! The projection is the only real difference. The 2D map maps world to a panning,
    //! zooming, rotating viewport; here the drape is a fixed square covering the whole
    //! island, so world maps to drape pixels directly. That also means no culling: every
    //! road is inside the target by definition, and the whole thing is drawn once and
    //! then frozen rather than rebuilt per frame.
    protected void PaintDrapeRoads(WorkspaceWidget workspace, float drapePx)
    {
        if (!DRAPE_ROADS_ENABLED)
            return;

        AG0_TDLTerrainRoadManager roadManager = ResolveRoadManager();
        if (!roadManager || roadManager.GetCount() == 0)
        {
            Print("[TDL_MAP3D] No road dataset for the drape", LogLevel.DEBUG);
            return;
        }

        CanvasWidget canvas = CreateDrapeCanvas(workspace, false, "roads");
        if (!canvas)
            return;

        array<ref AG0_TDLTerrainRoadFeature> roads = roadManager.GetFeatures();
        array<ref CanvasWidgetCommand> commands = {};

        // World metres to drape pixels. The drape is authored in logical units, so the
        // canvas works in those too and the engine scales the whole thing at render time.
        float pxPerMetreX = drapePx / m_fSpanX;
        float pxPerMetreZ = drapePx / m_fSpanZ;

        // Keeps the minimum strokes at a constant world width across drape resolutions.
        // Both operands are ints, so the 1.0 forces a float divide — an integer divide
        // happens to be exact at 8192 and silently truncates to 1 at anything between.
        float strokeScale = DRAPE_SIZE_PX * 1.0 / DRAPE_STROKE_REFERENCE_PX;

        int segments = 0;
        for (int r = 0, roadCount = roads.Count(); r < roadCount; r = r + 1)
        {
            AG0_TDLTerrainRoadFeature road = roads[r];
            if (!road || !road.m_aPoints)
                continue;

            int rawCount = road.m_aPoints.Count();
            if (rawCount < 4)
                continue;

            float minStroke = ROAD_STROKE_MIN_TRAIL;
            int color = ROAD_COLOR_TRAIL;
            if (road.m_iPriority == 3)
            {
                minStroke = ROAD_STROKE_MIN_HIGHWAY;
                color = ROAD_COLOR_HIGHWAY;
            }
            else if (road.m_iPriority == 2)
            {
                minStroke = ROAD_STROKE_MIN_PAVED;
                color = ROAD_COLOR_PAVED;
            }

            float stroke = Math.Max(road.m_fWidth * pxPerMetreX, minStroke * strokeScale);
            float joinRadius = stroke * 0.5;
            bool drawJoins = joinRadius >= 1.0;
            int joinSegments = 6;
            if (joinRadius >= 4.0)
                joinSegments = 10;
            if (joinRadius >= 8.0)
                joinSegments = 14;

            float prevX;
            float prevY;
            bool havePrev = false;
            bool prevSegmentDrawn = false;

            for (int i = 0; i + 1 < rawCount; i = i + 2)
            {
                float px = (road.m_aPoints[i] - m_fMapMinX) * pxPerMetreX;
                // V is flipped for the same reason the terrain UVs are: texture space
                // runs top-down while world Z runs north-up.
                float py = drapePx - (road.m_aPoints[i + 1] - m_fMapMinZ) * pxPerMetreZ;

                if (havePrev)
                {
                    LineDrawCommand line = new LineDrawCommand();
                    line.m_iColor = color;
                    line.m_fWidth = stroke;
                    line.m_Vertices = {prevX, prevY, px, py};
                    commands.Insert(line);
                    segments = segments + 1;

                    if (drawJoins && prevSegmentDrawn)
                    {
                        array<float> joinVerts = {};
                        TessellateDrapeCircle(prevX, prevY, joinRadius, joinSegments, joinVerts);
                        PolygonDrawCommand join = new PolygonDrawCommand();
                        join.m_iColor = color;
                        join.m_Vertices = joinVerts;
                        commands.Insert(join);
                    }

                    prevSegmentDrawn = true;
                }

                prevX = px;
                prevY = py;
                havePrev = true;
            }
        }

        // The command array must outlive the call — the widget keeps a reference to it,
        // not a copy.
        m_aDrapeCommands = commands;
        canvas.SetDrawCommands(commands);

        Print(string.Format("[TDL_MAP3D] Drape roads: %1 features, %2 segments, %3 commands",
            roads.Count(), segments, commands.Count()), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    //! Paint the MGRS grid into the drape, over the roads.
    //!
    //! The 2D map redraws its grid per frame and gates the 100 m minors on zoom; the
    //! drape is painted once and seen at every distance, so both spacings are always
    //! drawn and distance does the gating optically — minors alias away as the camera
    //! pulls back, the way fine print does on a paper map.
    //!
    //! Its own canvas rather than the roads': a later sibling composites above the
    //! road strokes (grid over roads, matching the 2D draw order), and the grid stays
    //! up on maps with no road dataset at all. BLEND is required — without it the
    //! canvas ignores the commands' alpha and the translucent lines render opaque.
    protected void PaintDrapeGrid(WorkspaceWidget workspace, float drapePx)
    {
        if (!DRAPE_GRID_ENABLED || !m_wDrapeRT)
            return;

        CanvasWidget canvas = CreateDrapeCanvas(workspace, true, "grid");
        if (!canvas)
            return;

        array<ref CanvasWidgetCommand> commands = {};
        float strokeScale = DRAPE_SIZE_PX * 1.0 / DRAPE_STROKE_REFERENCE_PX;

        // Minors first so the heavier majors overpaint them where they coincide.
        AddDrapeGridLines(commands, drapePx, GRID_SPACING_MINOR_M,
            GRID_COLOR_MINOR, GRID_STROKE_MINOR * strokeScale);
        AddDrapeGridLines(commands, drapePx, GRID_SPACING_MAJOR_M,
            GRID_COLOR_MAJOR, GRID_STROKE_MAJOR * strokeScale);

        // The command array must outlive the call — the widget keeps a reference to
        // it, not a copy.
        m_aDrapeGridCommands = commands;
        canvas.SetDrawCommands(commands);

        Print(string.Format("[TDL_MAP3D] Drape grid: %1 commands", commands.Count()),
            LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    //! Grid lines anchored to world-origin multiples of `spacing`, matching the 2D
    //! DrawGridLines anchoring so both surfaces agree where a gridline falls.
    protected void AddDrapeGridLines(array<ref CanvasWidgetCommand> commands,
        float drapePx, float spacing, int color, float stroke)
    {
        float pxPerMetreX = drapePx / m_fSpanX;
        float pxPerMetreZ = drapePx / m_fSpanZ;

        float startX = Math.Floor(m_fMapMinX / spacing) * spacing;
        for (float worldX = startX; worldX <= m_fMapMinX + m_fSpanX; worldX = worldX + spacing)
        {
            if (worldX < m_fMapMinX)
                continue;

            float px = (worldX - m_fMapMinX) * pxPerMetreX;
            LineDrawCommand line = new LineDrawCommand();
            line.m_iColor = color;
            line.m_fWidth = stroke;
            line.m_Vertices = {px, 0, px, drapePx};
            commands.Insert(line);
        }

        float startZ = Math.Floor(m_fMapMinZ / spacing) * spacing;
        for (float worldZ = startZ; worldZ <= m_fMapMinZ + m_fSpanZ; worldZ = worldZ + spacing)
        {
            if (worldZ < m_fMapMinZ)
                continue;

            // V is flipped for the same reason the terrain UVs and roads are: texture
            // space runs top-down while world Z runs north-up.
            float py = drapePx - (worldZ - m_fMapMinZ) * pxPerMetreZ;
            LineDrawCommand line = new LineDrawCommand();
            line.m_iColor = color;
            line.m_fWidth = stroke;
            line.m_Vertices = {0, py, drapePx, py};
            commands.Insert(line);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! The shapes canvas is created empty and painted by UpdateDrapeShapes. Created
    //! after the grid canvas so it composites above roads and grid — the same stacking
    //! the 2D map uses (shapes over grid over roads).
    protected void CreateDrapeShapesCanvas(WorkspaceWidget workspace)
    {
        if (!DRAPE_SHAPES_ENABLED || !m_wDrapeRT)
            return;

        // BLEND for the same reason as the grid: stroke and fill colours carry alpha,
        // and without the flag they composite opaque.
        m_wShapesCanvas = CreateDrapeCanvas(workspace, true, "shapes");
    }

    //------------------------------------------------------------------------------------------------
    //! Re-enable the frozen drape so changed canvas content reaches the terrain, then
    //! let the Tick countdown freeze it again. Extending the countdown on every call
    //! means an active draw session keeps the drape live continuously and it re-freezes
    //! DRAPE_FREEZE_TICKS after the last change instead of mid-edit.
    protected void WakeDrape()
    {
        if (!m_wDrapeRT)
            return;

        if (!m_wDrapeRT.IsEnabled())
            m_wDrapeRT.SetEnabled(true);

        m_iDrapeFreezeTicks = DRAPE_FREEZE_TICKS;
    }

    //------------------------------------------------------------------------------------------------
    //! Feed the committed shapes and the in-progress ghost, every frame the pane draws.
    //! Repaints only when something actually changed, because a repaint forces the whole
    //! drape RT to re-render for a few frames — the ghost therefore repaints every frame
    //! while present (it is rubber-banded to the cursor), and committed shapes repaint
    //! on a cheap version/identity signature rather than a deep compare.
    void UpdateDrapeShapes(array<ref AG0_TDLMapShape> shapes, AG0_TDLMapShape ghost)
    {
        // Stashed ahead of every guard: the overlay canvas reads these each tick for
        // range-ring distance labels, and must keep working even if the drape's shape
        // canvas failed to build.
        m_aOverlayShapes = shapes;
        m_OverlayGhost = ghost;

        if (!DRAPE_SHAPES_ENABLED || !m_wShapesCanvas)
            return;

        bool ghostActive = ghost != null;
        int signature = ComputeShapeSignature(shapes);

        // The frame after the ghost disappears must still repaint, to erase it.
        bool changed = ghostActive
            || m_bDrapeGhostWasActive
            || signature != m_iDrapeShapeSignature;
        m_bDrapeGhostWasActive = ghostActive;
        m_iDrapeShapeSignature = signature;

        if (!changed)
            return;

        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace)
            return;
        float drapePx = DRAPE_SIZE_PX;

        array<ref CanvasWidgetCommand> commands = {};

        if (shapes)
        {
            foreach (AG0_TDLMapShape shape : shapes)
            {
                PaintDrapeShape(commands, shape, drapePx);
            }
        }

        // Ghost last so the in-progress draw reads on top of committed shapes.
        if (ghost)
            PaintDrapeShape(commands, ghost, drapePx);

        // The command array must outlive the call — the widget keeps a reference to
        // it, not a copy.
        m_aDrapeShapeCommands = commands;
        m_wShapesCanvas.SetDrawCommands(commands);
        WakeDrape();
    }

    //------------------------------------------------------------------------------------------------
    //! Cheap change signature over the committed shapes. Identity is carried by the
    //! version counter (bumped on every API/local edit) plus enough geometry and style
    //! terms that add/delete/replace at equal count still shifts the value. Deliberately
    //! not a deep hash — this runs every frame the pane draws.
    protected int ComputeShapeSignature(array<ref AG0_TDLMapShape> shapes)
    {
        if (!shapes)
            return 0;

        int hash = shapes.Count();
        foreach (AG0_TDLMapShape shape : shapes)
        {
            if (!shape)
                continue;

            int centerX10 = Math.Round(shape.m_vCenter[0] * 10);
            int centerZ10 = Math.Round(shape.m_vCenter[2] * 10);
            int radius10 = Math.Round(shape.m_fRadius * 10);

            hash = hash * 31 + shape.m_eShapeType;
            hash = hash * 31 + shape.m_iVersion;
            hash = hash * 31 + shape.m_iStrokeColor;
            hash = hash * 31 + shape.m_iFillColor;
            hash = hash * 31 + centerX10;
            hash = hash * 31 + centerZ10;
            hash = hash * 31 + radius10;
            if (shape.m_aVertices)
                hash = hash * 31 + shape.m_aVertices.Count();
        }
        return hash;
    }

    //------------------------------------------------------------------------------------------------
    //! Per-frame bloodhound state, mirroring AG0_TDLMapView.SetBloodhound — the 2D
    //! canvas emission is suppressed while the pane is up, so the line renders on the
    //! overlay canvas here instead.
    void SetBloodhound(bool enabled, vector cursorWorld, vector deviceWorld)
    {
        m_bBloodhoundEnabled = enabled;
        m_vBloodhoundCursor = cursorWorld;
        m_vBloodhoundDevice = deviceWorld;
    }

    //------------------------------------------------------------------------------------------------
    //! Rebuild the screen-space overlay: ring distance labels and the bloodhound line.
    //! Runs every tick after the camera has been applied, because everything here is a
    //! projection of the freshly-posed camera — there is nothing to cache.
    protected void UpdateOverlayCanvas()
    {
        if (!m_wOverlayCanvas || !m_wPane)
            return;

        array<ref CanvasWidgetCommand> commands = {};

        float paneW;
        float paneH;
        m_wPane.GetScreenSize(paneW, paneH);
        if (paneW >= 1 && paneH >= 1)
        {
            if (m_aOverlayShapes)
            {
                foreach (AG0_TDLMapShape shape : m_aOverlayShapes)
                {
                    AddOverlayRingLabels(commands, shape, paneW, paneH);
                }
            }
            AddOverlayRingLabels(commands, m_OverlayGhost, paneW, paneH);

            if (m_bBloodhoundEnabled)
                AddOverlayBloodhound(commands, paneW, paneH);
        }

        // The command array must outlive the call — the widget keeps a reference to
        // it, not a copy.
        m_aOverlayCommands = commands;
        m_wOverlayCanvas.SetDrawCommands(commands);
    }

    //------------------------------------------------------------------------------------------------
    //! Distance labels at the north cardinal of each ring, projected per frame. The
    //! label gate compares PROJECTED radius, so backing the camera off naturally sheds
    //! labels from the inner rings first — the same behaviour zooming out gives in 2D.
    protected void AddOverlayRingLabels(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float paneW, float paneH)
    {
        if (!shape || shape.m_eShapeType != AG0_ETDLShapeType.RANGE_RINGS)
            return;

        float centerX;
        float centerY;
        if (!ProjectWorldToPane(shape.m_vCenter, centerX, centerY))
            return;

        foreach (float ringRadius : shape.m_aRings)
        {
            vector northPoint = Vector(
                shape.m_vCenter[0], 0, shape.m_vCenter[2] + ringRadius);

            float labelX;
            float labelY;
            if (!ProjectWorldToPane(northPoint, labelX, labelY))
                continue;

            float dx = labelX - centerX;
            float dy = labelY - centerY;
            if (dx * dx + dy * dy < OVERLAY_RING_LABEL_MIN_PX * OVERLAY_RING_LABEL_MIN_PX)
                continue;

            if (labelX < 0 || labelX > paneW || labelY < 0 || labelY > paneH)
                continue;

            string distLabel;
            if (ringRadius >= 1000)
                distLabel = string.Format("%1km", Math.Round(ringRadius / 100) * 0.1);
            else
                distLabel = string.Format("%1m", Math.Round(ringRadius));

            // labelScale 1: this is screen-space text, the drape multipliers are for
            // ground-painted content.
            AddDrapeTextLabel(commands, distLabel, labelX, labelY,
                OVERLAY_RING_LABEL_FONT, DRAPE_LABEL_TEXT_COLOR, 1.0);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Port of the 2D DrawBloodhound onto projected endpoints: device -> cursor,
    //! extended through the cursor to the pane edge, with the perpendicular tick at the
    //! cursor. The screen-space extension survives the projection unchanged — "points
    //! off-pane toward the bearing" is a property of the view, not the terrain.
    protected void AddOverlayBloodhound(array<ref CanvasWidgetCommand> commands,
        float paneW, float paneH)
    {
        float cursorX;
        float cursorY;
        if (!ProjectWorldToPane(m_vBloodhoundCursor, cursorX, cursorY))
            return;

        float deviceX;
        float deviceY;
        if (!ProjectWorldToPane(m_vBloodhoundDevice, deviceX, deviceY))
        {
            // Device behind the camera leaves no direction to extend along; the tick
            // still marks the cursor so the tool does not silently vanish.
            AddOverlayBloodhoundTick(commands, cursorX, cursorY, 1, 0);
            return;
        }

        float dirX = cursorX - deviceX;
        float dirY = cursorY - deviceY;
        if (Math.AbsFloat(dirX) < 0.5 && Math.AbsFloat(dirY) < 0.5)
            return;

        float exitX;
        float exitY;
        ExtendLineToPaneEdge(cursorX, cursorY, dirX, dirY, paneW, paneH, exitX, exitY);

        // Device endpoint clipped into the pane the same way the 2D path clips it to
        // the canvas — a start point far off-pane costs stroke length nobody sees.
        float startX = deviceX;
        float startY = deviceY;
        if (deviceX < 0 || deviceX > paneW || deviceY < 0 || deviceY > paneH)
            ExtendLineToPaneEdge(cursorX, cursorY, -dirX, -dirY, paneW, paneH, startX, startY);

        LineDrawCommand mainLine = new LineDrawCommand();
        mainLine.m_iColor = OVERLAY_BLOODHOUND_COLOR;
        mainLine.m_fWidth = OVERLAY_BLOODHOUND_WIDTH;
        mainLine.m_Vertices = {startX, startY, exitX, exitY};
        commands.Insert(mainLine);

        float len = Math.Sqrt(dirX * dirX + dirY * dirY);
        if (len <= 0)
            return;
        AddOverlayBloodhoundTick(commands, cursorX, cursorY, -dirY / len, dirX / len);
    }

    //------------------------------------------------------------------------------------------------
    protected void AddOverlayBloodhoundTick(array<ref CanvasWidgetCommand> commands,
        float cursorX, float cursorY, float normalX, float normalY)
    {
        LineDrawCommand tick = new LineDrawCommand();
        tick.m_iColor = OVERLAY_BLOODHOUND_COLOR;
        tick.m_fWidth = OVERLAY_BLOODHOUND_WIDTH;
        tick.m_Vertices = {
            cursorX - normalX * OVERLAY_BLOODHOUND_TICK,
            cursorY - normalY * OVERLAY_BLOODHOUND_TICK,
            cursorX + normalX * OVERLAY_BLOODHOUND_TICK,
            cursorY + normalY * OVERLAY_BLOODHOUND_TICK};
        commands.Insert(tick);
    }

    //------------------------------------------------------------------------------------------------
    //! Smallest positive-t intersection of P(t) = (px,py) + t*(dirX,dirY) with the
    //! pane rect — the 2D map's ExtendLineToCanvasEdge against pane bounds.
    protected void ExtendLineToPaneEdge(float px, float py, float dirX, float dirY,
        float paneW, float paneH, out float outX, out float outY)
    {
        float bestT = 1000000000.0;
        if (dirX > 0.0001)
        {
            float tRight = (paneW - px) / dirX;
            if (tRight > 0 && tRight < bestT)
                bestT = tRight;
        }
        else if (dirX < -0.0001)
        {
            float tLeft = (0 - px) / dirX;
            if (tLeft > 0 && tLeft < bestT)
                bestT = tLeft;
        }
        if (dirY > 0.0001)
        {
            float tBottom = (paneH - py) / dirY;
            if (tBottom > 0 && tBottom < bestT)
                bestT = tBottom;
        }
        else if (dirY < -0.0001)
        {
            float tTop = (0 - py) / dirY;
            if (tTop > 0 && tTop < bestT)
                bestT = tTop;
        }
        if (bestT >= 1000000000.0)
            bestT = 2.0;
        outX = px + dirX * bestT;
        outY = py + dirY * bestT;
    }

    //------------------------------------------------------------------------------------------------
    //! World XZ -> drape pixels. Same transform as the road and grid painters; the
    //! drape is north-up and unrotated, so no map-rotation term exists here.
    protected void DrapeShapePoint(float worldX, float worldZ, float drapePx,
        out float px, out float py)
    {
        px = (worldX - m_fMapMinX) * (drapePx / m_fSpanX);
        py = drapePx - (worldZ - m_fMapMinZ) * (drapePx / m_fSpanZ);
    }

    //------------------------------------------------------------------------------------------------
    //! One shape onto the drape — the drape-space port of the 2D map's DrawSingleShape.
    //! Geometry logic matches the 2D renderers so both surfaces draw the same picture;
    //! the differences are the projection (fixed north-up drape px instead of a panning
    //! viewport), radii scaled per-axis (the drape square stretches over a possibly
    //! non-square map extent), and sizes widened by the drape multipliers. No visibility
    //! cull: everything is inside the drape by definition, and painting is change-gated.
    protected void PaintDrapeShape(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float drapePx)
    {
        if (!shape)
            return;

        float pxPerMetreX = drapePx / m_fSpanX;
        float pxPerMetreZ = drapePx / m_fSpanZ;
        float baseScale = DRAPE_SIZE_PX * 1.0 / DRAPE_STROKE_REFERENCE_PX;
        float strokeScale = baseScale * DRAPE_SHAPE_STROKE_MUL;
        float labelScale = baseScale * DRAPE_SHAPE_LABEL_MUL;
        float stroke = shape.m_fStrokeWidth * strokeScale;

        float cx;
        float cy;
        DrapeShapePoint(shape.m_vCenter[0], shape.m_vCenter[2], drapePx, cx, cy);

        switch (shape.m_eShapeType)
        {
            case AG0_ETDLShapeType.CIRCLE:
                PaintDrapeShapeCircle(commands, shape, cx, cy,
                    pxPerMetreX, pxPerMetreZ, stroke, labelScale);
                break;

            case AG0_ETDLShapeType.SECTOR:
                PaintDrapeShapeSector(commands, shape, cx, cy,
                    pxPerMetreX, pxPerMetreZ, stroke, labelScale);
                break;

            case AG0_ETDLShapeType.RANGE_RINGS:
                PaintDrapeShapeRangeRings(commands, shape, cx, cy,
                    pxPerMetreX, pxPerMetreZ, stroke, strokeScale, labelScale);
                break;

            case AG0_ETDLShapeType.RECTANGLE:
            case AG0_ETDLShapeType.POLYGON:
                PaintDrapeShapePolygon(commands, shape, cx, cy, drapePx,
                    stroke, labelScale);
                break;

            case AG0_ETDLShapeType.FREEHAND:
                PaintDrapeShapeFreehand(commands, shape, cx, cy, drapePx,
                    stroke, labelScale);
                break;

            case AG0_ETDLShapeType.ROUTE:
                PaintDrapeShapeRoute(commands, shape, drapePx,
                    stroke, strokeScale, labelScale);
                break;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Segment count lookup mirrored from the 2D map's GetAdaptiveSegments, fed drape
    //! pixels instead of screen pixels — drape radii run large, so this mostly lands in
    //! the high buckets, and the cost is paid only on repaint.
    protected int DrapeSegments(float drapeRadius)
    {
        if (drapeRadius < 5)
            return 8;
        if (drapeRadius < 15)
            return 12;
        if (drapeRadius < 40)
            return 20;
        if (drapeRadius < 100)
            return 32;
        if (drapeRadius < 250)
            return 48;
        if (drapeRadius < 500)
            return 72;
        return 128;
    }

    //------------------------------------------------------------------------------------------------
    //! Per-axis radii, because the drape square stretches across the map's true extent —
    //! an isotropic radius would render circles as ellipses on any non-square map.
    protected void TessellateDrapeEllipse(float cx, float cy, float rx, float ry,
        int segments, out array<float> verts)
    {
        verts = {};
        float step = Math.PI2 / segments;
        for (int i = 0; i < segments; i = i + 1)
        {
            float angle = step * i;
            verts.Insert(cx + Math.Cos(angle) * rx);
            verts.Insert(cy + Math.Sin(angle) * ry);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void AddDrapeClosedStroke(array<ref CanvasWidgetCommand> commands,
        array<float> verts, int color, float width)
    {
        int count = verts.Count();
        if (count < 4)
            return;

        for (int i = 0; i < count - 2; i = i + 2)
        {
            LineDrawCommand line = new LineDrawCommand();
            line.m_iColor = color;
            line.m_fWidth = width;
            line.m_Vertices = {verts[i], verts[i + 1], verts[i + 2], verts[i + 3]};
            commands.Insert(line);
        }

        LineDrawCommand close = new LineDrawCommand();
        close.m_iColor = color;
        close.m_fWidth = width;
        close.m_Vertices = {verts[count - 2], verts[count - 1], verts[0], verts[1]};
        commands.Insert(close);
    }

    //------------------------------------------------------------------------------------------------
    protected void AddDrapeOpenStroke(array<ref CanvasWidgetCommand> commands,
        array<float> verts, int color, float width)
    {
        int count = verts.Count();
        if (count < 4)
            return;

        for (int i = 0; i < count - 2; i = i + 2)
        {
            LineDrawCommand line = new LineDrawCommand();
            line.m_iColor = color;
            line.m_fWidth = width;
            line.m_Vertices = {verts[i], verts[i + 1], verts[i + 2], verts[i + 3]};
            commands.Insert(line);
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Pill + text, mirroring the 2D DrawTextLabel proportions scaled to ground size.
    protected void AddDrapeTextLabel(array<ref CanvasWidgetCommand> commands, string text,
        float px, float py, float fontSize, int textColor, float labelScale)
    {
        if (text.IsEmpty())
            return;

        float textWidth = text.Length() * DRAPE_LABEL_CHAR_WIDTH * labelScale;
        float halfW = textWidth * 0.5;
        float halfH = DRAPE_LABEL_HEIGHT * labelScale * 0.5;
        float pad = DRAPE_LABEL_PAD * labelScale;

        PolygonDrawCommand bg = new PolygonDrawCommand();
        bg.m_iColor = DRAPE_LABEL_BG_COLOR;
        bg.m_Vertices = {
            px - halfW - pad, py - halfH - pad,
            px + halfW + pad, py - halfH - pad,
            px + halfW + pad, py + halfH + pad,
            px - halfW - pad, py + halfH + pad
        };
        commands.Insert(bg);

        TextDrawCommand cmd = new TextDrawCommand();
        cmd.m_sText = text;
        cmd.m_Position = Vector(px - halfW, py - halfH, 0);
        cmd.m_Pivot = Vector(0, 0, 0);
        cmd.m_iColor = textColor;
        cmd.m_fSize = fontSize * labelScale;
        commands.Insert(cmd);
    }

    //------------------------------------------------------------------------------------------------
    protected void AddDrapeShapeLabel(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float px, float py, float labelScale)
    {
        if (shape.m_sLabel.IsEmpty())
            return;

        AddDrapeTextLabel(commands, shape.m_sLabel, px, py,
            DRAPE_LABEL_SIZE, DRAPE_LABEL_TEXT_COLOR, labelScale);
    }

    //------------------------------------------------------------------------------------------------
    protected void PaintDrapeShapeCircle(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float cx, float cy, float pxPerMetreX, float pxPerMetreZ,
        float stroke, float labelScale)
    {
        float rx = shape.m_fRadius * pxPerMetreX;
        float ry = shape.m_fRadius * pxPerMetreZ;
        if (rx < 1 || ry < 1)
            return;

        array<float> verts;
        TessellateDrapeEllipse(cx, cy, rx, ry, DrapeSegments(Math.Max(rx, ry)), verts);

        if (shape.m_iFillColor != 0)
        {
            PolygonDrawCommand fill = new PolygonDrawCommand();
            fill.m_iColor = shape.m_iFillColor;
            fill.m_Vertices = verts;
            commands.Insert(fill);
        }

        AddDrapeClosedStroke(commands, verts, shape.m_iStrokeColor, stroke);
        AddDrapeShapeLabel(commands, shape, cx, cy, labelScale);
    }

    //------------------------------------------------------------------------------------------------
    protected void PaintDrapeShapeSector(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float cx, float cy, float pxPerMetreX, float pxPerMetreZ,
        float stroke, float labelScale)
    {
        float rx = shape.m_fRadius * pxPerMetreX;
        float ry = shape.m_fRadius * pxPerMetreZ;
        if (rx < 1 || ry < 1)
            return;

        // Bearing (0 = north, clockwise) to drape angle: the drape is north-up with
        // y growing southward, so the 2D map's conversion applies with rotation zero.
        float startRad = (shape.m_fStartAngle - 90.0) * Math.DEG2RAD;
        float endRad = (shape.m_fEndAngle - 90.0) * Math.DEG2RAD;

        float sweep = endRad - startRad;
        if (sweep <= 0)
            sweep = sweep + Math.PI2;

        int arcSegs = Math.Ceil(sweep / Math.PI2 * DrapeSegments(Math.Max(rx, ry)));
        if (arcSegs < 4)
            arcSegs = 4;
        if (arcSegs > 128)
            arcSegs = 128;

        array<float> verts = {};
        verts.Insert(cx);
        verts.Insert(cy);
        for (int i = 0; i <= arcSegs; i = i + 1)
        {
            float angle = startRad + sweep * i / arcSegs;
            verts.Insert(cx + Math.Cos(angle) * rx);
            verts.Insert(cy + Math.Sin(angle) * ry);
        }

        if (shape.m_iFillColor != 0)
        {
            PolygonDrawCommand fill = new PolygonDrawCommand();
            fill.m_iColor = shape.m_iFillColor;
            fill.m_Vertices = verts;
            commands.Insert(fill);
        }

        // Radial edge to the arc start, the arc itself, then the closing radial —
        // stroke-only, same as the 2D sector (no closing chord).
        LineDrawCommand radialStart = new LineDrawCommand();
        radialStart.m_iColor = shape.m_iStrokeColor;
        radialStart.m_fWidth = stroke;
        radialStart.m_Vertices = {cx, cy, verts[2], verts[3]};
        commands.Insert(radialStart);

        int vertCount = verts.Count();
        for (int i = 2; i < vertCount - 2; i = i + 2)
        {
            LineDrawCommand arcLine = new LineDrawCommand();
            arcLine.m_iColor = shape.m_iStrokeColor;
            arcLine.m_fWidth = stroke;
            arcLine.m_Vertices = {verts[i], verts[i + 1], verts[i + 2], verts[i + 3]};
            commands.Insert(arcLine);
        }

        LineDrawCommand radialEnd = new LineDrawCommand();
        radialEnd.m_iColor = shape.m_iStrokeColor;
        radialEnd.m_fWidth = stroke;
        radialEnd.m_Vertices = {verts[vertCount - 2], verts[vertCount - 1], cx, cy};
        commands.Insert(radialEnd);

        if (!shape.m_sLabel.IsEmpty())
        {
            float midAngle = startRad + sweep * 0.5;
            AddDrapeShapeLabel(commands, shape,
                cx + Math.Cos(midAngle) * rx * 0.5,
                cy + Math.Sin(midAngle) * ry * 0.5, labelScale);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void PaintDrapeShapeRangeRings(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float cx, float cy, float pxPerMetreX, float pxPerMetreZ,
        float stroke, float strokeScale, float labelScale)
    {
        foreach (float ringRadius : shape.m_aRings)
        {
            float rx = ringRadius * pxPerMetreX;
            float ry = ringRadius * pxPerMetreZ;
            if (rx < 1 || ry < 1)
                continue;

            array<float> verts;
            TessellateDrapeEllipse(cx, cy, rx, ry, DrapeSegments(Math.Max(rx, ry)), verts);
            AddDrapeClosedStroke(commands, verts, shape.m_iStrokeColor, stroke);
        }

        float crossSize = 6 * strokeScale;

        LineDrawCommand hLine = new LineDrawCommand();
        hLine.m_iColor = shape.m_iStrokeColor;
        hLine.m_fWidth = strokeScale;
        hLine.m_Vertices = {cx - crossSize, cy, cx + crossSize, cy};
        commands.Insert(hLine);

        LineDrawCommand vLine = new LineDrawCommand();
        vLine.m_iColor = shape.m_iStrokeColor;
        vLine.m_fWidth = strokeScale;
        vLine.m_Vertices = {cx, cy - crossSize, cx, cy + crossSize};
        commands.Insert(vLine);

        // Ring distance labels deliberately NOT baked here — ground text minifies with
        // camera distance and the numbers are the point of the tool. They render on the
        // screen-space overlay canvas instead (AddOverlayRingLabels), where they stay
        // legible at any zoom.
        AddDrapeShapeLabel(commands, shape, cx, cy, labelScale);
    }

    //------------------------------------------------------------------------------------------------
    protected void PaintDrapeShapePolygon(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float cx, float cy, float drapePx,
        float stroke, float labelScale)
    {
        int rawCount = shape.m_aVertices.Count();
        if (rawCount < 4)
            return;

        array<float> drapeVerts = {};
        float minX = float.MAX;
        float minY = float.MAX;
        float maxX = -float.MAX;
        float maxY = -float.MAX;
        for (int i = 0; i + 1 < rawCount; i = i + 2)
        {
            float px;
            float py;
            DrapeShapePoint(shape.m_aVertices[i], shape.m_aVertices[i + 1],
                drapePx, px, py);
            drapeVerts.Insert(px);
            drapeVerts.Insert(py);
            if (px < minX)
                minX = px;
            if (py < minY)
                minY = py;
            if (px > maxX)
                maxX = px;
            if (py > maxY)
                maxY = py;
        }

        // Same degenerate-fill guard as the 2D renderer: a polygon collapsed below a
        // couple of pixels trips the engine triangulator into per-frame error spam.
        float spanX = maxX - minX;
        float spanY = maxY - minY;
        if (shape.m_iFillColor != 0 && drapeVerts.Count() >= 6 && spanX >= 2.0 && spanY >= 2.0)
        {
            PolygonDrawCommand fill = new PolygonDrawCommand();
            fill.m_iColor = shape.m_iFillColor;
            fill.m_Vertices = drapeVerts;
            commands.Insert(fill);
        }

        AddDrapeClosedStroke(commands, drapeVerts, shape.m_iStrokeColor, stroke);
        AddDrapeShapeLabel(commands, shape, cx, cy, labelScale);
    }

    //------------------------------------------------------------------------------------------------
    protected void PaintDrapeShapeFreehand(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float cx, float cy, float drapePx,
        float stroke, float labelScale)
    {
        int rawCount = shape.m_aVertices.Count();
        if (rawCount < 4)
            return;

        array<float> drapeVerts = {};
        for (int i = 0; i + 1 < rawCount; i = i + 2)
        {
            float px;
            float py;
            DrapeShapePoint(shape.m_aVertices[i], shape.m_aVertices[i + 1],
                drapePx, px, py);
            drapeVerts.Insert(px);
            drapeVerts.Insert(py);
        }

        AddDrapeOpenStroke(commands, drapeVerts, shape.m_iStrokeColor, stroke);

        if (!shape.m_sLabel.IsEmpty())
            AddDrapeShapeLabel(commands, shape, cx, cy, labelScale);
    }

    //------------------------------------------------------------------------------------------------
    protected void PaintDrapeShapeRoute(array<ref CanvasWidgetCommand> commands,
        AG0_TDLMapShape shape, float drapePx,
        float stroke, float strokeScale, float labelScale)
    {
        int rawCount = shape.m_aVertices.Count();
        if (rawCount < 4)
            return;

        array<float> drapeVerts = {};
        for (int i = 0; i + 1 < rawCount; i = i + 2)
        {
            float px;
            float py;
            DrapeShapePoint(shape.m_aVertices[i], shape.m_aVertices[i + 1],
                drapePx, px, py);
            drapeVerts.Insert(px);
            drapeVerts.Insert(py);
        }

        AddDrapeOpenStroke(commands, drapeVerts, shape.m_iStrokeColor, stroke);

        int waypointIdx = 0;
        for (int i = 0; i + 1 < drapeVerts.Count(); i = i + 2)
        {
            float wpX = drapeVerts[i];
            float wpY = drapeVerts[i + 1];

            array<float> outVerts = {};
            TessellateDrapeCircle(wpX, wpY, 5 * strokeScale, 8, outVerts);
            PolygonDrawCommand wpOutline = new PolygonDrawCommand();
            wpOutline.m_iColor = 0xFF000000;
            wpOutline.m_Vertices = outVerts;
            commands.Insert(wpOutline);

            array<float> wpVerts = {};
            TessellateDrapeCircle(wpX, wpY, 3.5 * strokeScale, 8, wpVerts);
            PolygonDrawCommand wpFill = new PolygonDrawCommand();
            wpFill.m_iColor = shape.m_iStrokeColor;
            wpFill.m_Vertices = wpVerts;
            commands.Insert(wpFill);

            if (shape.m_aWaypointLabels && waypointIdx < shape.m_aWaypointLabels.Count())
            {
                string wpLabel = shape.m_aWaypointLabels[waypointIdx];
                if (!wpLabel.IsEmpty())
                {
                    AddDrapeTextLabel(commands, wpLabel, wpX,
                        wpY - 10 * strokeScale, 9, DRAPE_LABEL_TEXT_COLOR, labelScale);
                }
            }
            waypointIdx = waypointIdx + 1;
        }

        // Direction arrows at segment midpoints, sized and gated by the same ratios
        // as the 2D route renderer.
        int pointCount = drapeVerts.Count() / 2;
        for (int i = 0; i < pointCount - 1; i = i + 1)
        {
            float x0 = drapeVerts[i * 2];
            float y0 = drapeVerts[i * 2 + 1];
            float x1 = drapeVerts[(i + 1) * 2];
            float y1 = drapeVerts[(i + 1) * 2 + 1];

            float mx = (x0 + x1) * 0.5;
            float my = (y0 + y1) * 0.5;

            float dx = x1 - x0;
            float dy = y1 - y0;
            float len = Math.Sqrt(dx * dx + dy * dy);
            if (len < 20 * strokeScale)
                continue;

            dx = dx / len;
            dy = dy / len;

            float arrowLen = 6 * strokeScale;
            float arrowHalf = 3 * strokeScale;

            float tipX = mx + dx * arrowLen;
            float tipY = my + dy * arrowLen;
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
            commands.Insert(arrow);
        }

        if (!shape.m_sLabel.IsEmpty() && drapeVerts.Count() >= 4)
        {
            int midIdx = (drapeVerts.Count() / 2) & ~1;
            if (midIdx + 1 < drapeVerts.Count())
            {
                AddDrapeShapeLabel(commands, shape,
                    drapeVerts[midIdx], drapeVerts[midIdx + 1], labelScale);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    protected void TessellateDrapeCircle(float cx, float cy, float radius, int segments,
        out array<float> verts)
    {
        float step = Math.PI2 / segments;
        for (int i = 0; i < segments; i = i + 1)
        {
            float angle = step * i;
            verts.Insert(cx + Math.Cos(angle) * radius);
            verts.Insert(cy + Math.Sin(angle) * radius);
        }
    }

    //------------------------------------------------------------------------------------------------
    protected AG0_TDLTerrainRoadManager ResolveRoadManager()
    {
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return null;

        SCR_PlayerController tdlController = SCR_PlayerController.Cast(pc);
        if (!tdlController)
            return null;

        return tdlController.GetTDLTerrainRoadManager();
    }

    //------------------------------------------------------------------------------------------------
    //! Extrude the streamed building footprints into the diorama.
    //!
    //! Reads the same dataset the 2D map draws, so the two surfaces cannot disagree and
    //! nothing extra crosses the network. Silent no-op when the dataset has not arrived —
    //! terrain is useful without buildings, and the view should not fail on their absence.
    protected void BuildStructures(float centerX, float centerZ)
    {
        if (!STRUCTURES_ENABLED)
            return;

        AG0_TDLTerrainStructureManager manager = ResolveStructureManager();
        if (!manager || manager.GetCount() == 0)
        {
            Print("[TDL_MAP3D] No structure dataset — terrain only", LogLevel.DEBUG);
            return;
        }

        array<ref AG0_TDLTerrainStructureRecord> records = manager.GetStructures();
        int total = records.Count();
        if (total > STRUCTURE_CAP)
            total = STRUCTURE_CAP;

        int built = 0;
        int cursor = 0;
        while (cursor < total)
        {
            int batchCount = AG0_TDLMap3DStructureBatch.BOXES_PER_BATCH;
            if (cursor + batchCount > total)
                batchCount = total - cursor;

            EntitySpawnParams params = new EntitySpawnParams();
            Math3D.MatrixIdentity4(params.Transform);

            // One mesh per batch while both tones share a material; two only when they
            // actually differ, since that is the only thing a second mesh buys.
            bool twoTone = STRUCTURE_MATERIAL_WALLS != STRUCTURE_MATERIAL_ROOFS;

            int wallPart = AG0_TDLMap3DStructureBatch.PART_ALL;
            if (twoTone)
                wallPart = AG0_TDLMap3DStructureBatch.PART_WALLS;

            AG0_TDLMap3DStructureBatch walls = AG0_TDLMap3DStructureBatch.Cast(
                GetGame().SpawnEntity(AG0_TDLMap3DStructureBatch, m_World, params));
            if (!walls)
                break;

            if (walls.Build(records, cursor, batchCount, this, centerX, centerZ,
                m_fScale, STRUCTURE_HEIGHT_MUL, STRUCTURE_MATERIAL_WALLS, wallPart))
            {
                m_aStructureBatches.Insert(walls);
                built = built + batchCount;
            }
            else
            {
                SCR_EntityHelper.DeleteEntityAndChildren(walls);
            }

            if (twoTone)
            {
                AG0_TDLMap3DStructureBatch roofs = AG0_TDLMap3DStructureBatch.Cast(
                    GetGame().SpawnEntity(AG0_TDLMap3DStructureBatch, m_World, params));
                if (roofs)
                {
                    if (roofs.Build(records, cursor, batchCount, this, centerX, centerZ,
                        m_fScale, STRUCTURE_HEIGHT_MUL, STRUCTURE_MATERIAL_ROOFS,
                        AG0_TDLMap3DStructureBatch.PART_ROOFS))
                    {
                        m_aStructureBatches.Insert(roofs);
                    }
                    else
                    {
                        SCR_EntityHelper.DeleteEntityAndChildren(roofs);
                    }
                }
            }

            cursor = cursor + batchCount;
        }

        Print(string.Format("[TDL_MAP3D] Structures %1 of %2 in %3 batches (heightMul %4)",
            built, records.Count(), m_aStructureBatches.Count(), STRUCTURE_HEIGHT_MUL),
            LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    protected AG0_TDLTerrainStructureManager ResolveStructureManager()
    {
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return null;

        SCR_PlayerController tdlController = SCR_PlayerController.Cast(pc);
        if (!tdlController)
            return null;

        return tdlController.GetTDLTerrainStructureManager();
    }

    //------------------------------------------------------------------------------------------------
    //! Seat an environment probe over the middle of the diorama.
    //!
    //! Centre rather than anywhere else because a single probe covers the whole miniature
    //! and the centre is the least-bad representative sample. Its height is taken from the
    //! built surface so the probe clears the terrain on a mountainous map instead of
    //! sitting inside it.
    protected void SpawnEnvProbe()
    {
        Resource envProbeResource = Resource.Load(ENV_PROBE_PREFAB);
        if (!envProbeResource || !envProbeResource.IsValid())
        {
            Print("[TDL_MAP3D] Env probe prefab failed to load — ambient will be undefined",
                LogLevel.WARNING);
            return;
        }

        float floorY = 0;
        SampleDioramaY(0, 0, floorY);

        EntitySpawnParams params = new EntitySpawnParams();
        Math3D.MatrixIdentity4(params.Transform);
        params.Transform[3] = Vector(0, floorY + ENV_PROBE_HEIGHT_M, 0);

        m_EnvProbeEntity = GetGame().SpawnEntityPrefab(envProbeResource, m_World, params);
        if (!m_EnvProbeEntity)
        {
            Print("[TDL_MAP3D] Env probe spawned null", LogLevel.WARNING);
            return;
        }

        Print(string.Format("[TDL_MAP3D] Env probe at diorama y=%1 (terrain %2 + %3)",
            floorY + ENV_PROBE_HEIGHT_M, floorY, ENV_PROBE_HEIGHT_M), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    protected AG0_TDLMap3DHeightSource ResolveHeightSource()
    {
        return ResolveSharedHeightSource();
    }

    //------------------------------------------------------------------------------------------------
    //! Terrain height at a world XZ for anything that needs one — the 2D map's readouts as
    //! much as this view's mesh builder. Exposed as a free function rather than living behind
    //! a view instance because a surface asking "how high is the ground here" should not have
    //! to care whether the 3D map happens to be open.
    static float SampleTerrainY(float worldX, float worldZ)
    {
        AG0_TDLMap3DHeightSource source = ResolveSharedHeightSource();
        if (!source)
            return 0;

        return source.SampleY(worldX, worldZ);
    }

    //------------------------------------------------------------------------------------------------
    //! Cached because callers sample per frame and resolution allocates a source object.
    //!
    //! Prefer the streamed grid once it has arrived: it is the higher-fidelity source and it is
    //! identical on every client, which the local heightfield is not guaranteed to be across
    //! graphics settings. Two clients reading different elevations for the same grid reference
    //! is exactly the kind of disagreement a shared tactical picture cannot have.
    //!
    //! The cache self-invalidates on world change for the same reason DiscardStaleInstance
    //! exists — a source built against a dead world samples freed terrain.
    protected static AG0_TDLMap3DHeightSource ResolveSharedHeightSource()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return null;

        bool cacheUsable = s_TerrainSource && s_TerrainSourceWorld == world;

        // Steady state once the grid has landed: there is nothing better to upgrade to, so
        // the common path costs a pointer compare and no PlayerController lookup.
        if (cacheUsable && s_bTerrainSourceIsGrid)
            return s_TerrainSource;

        AG0_TDLTerrainHeightmapManager grid = ResolveHeightmapManager();
        bool gridReady = grid && grid.IsReady();

        // The grid streams in asynchronously, so a cache built before it arrived has to be
        // allowed to upgrade to it later; until then the local heightfield keeps answering.
        if (cacheUsable && !gridReady)
            return s_TerrainSource;

        if (gridReady)
        {
            s_TerrainSource = new AG0_TDLMap3DGridHeightSource(grid);
            s_bTerrainSourceIsGrid = true;
        }
        else
        {
            s_TerrainSource = new AG0_TDLMap3DWorldHeightSource(world);
            s_bTerrainSourceIsGrid = false;
        }

        s_TerrainSourceWorld = world;
        return s_TerrainSource;
    }

    //------------------------------------------------------------------------------------------------
    protected static AG0_TDLTerrainHeightmapManager ResolveHeightmapManager()
    {
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return null;

        // The TDL controller is a `modded class SCR_PlayerController`, so there is no
        // distinct TDL type to cast to — the modded members hang off the base type.
        SCR_PlayerController tdlController = SCR_PlayerController.Cast(pc);
        if (!tdlController)
            return null;

        return tdlController.GetTDLTerrainHeightmapManager();
    }

    //------------------------------------------------------------------------------------------------
    protected void ConfigureCamera()
    {
        if (!m_World)
            return;

        float farPlane = m_fMaxDistance * 2.5;
        if (farPlane < CAM_FAR_FLOOR)
            farPlane = CAM_FAR_FLOOR;

        m_World.SetCameraType(CAMERA_INDEX, CameraType.PERSPECTIVE);
        m_World.SetCameraNearPlane(CAMERA_INDEX, CAM_NEAR);
        m_World.SetCameraFarPlane(CAMERA_INDEX, farPlane);
        m_World.SetCameraVerticalFOV(CAMERA_INDEX, CAM_FOV);

        if (!SCRIPT_DRIVEN_PP)
            return;

        m_World.SetCameraPostProcessEffect(CAMERA_INDEX, PP_PRIORITY_HDR,
            PostProcessEffectType.HDR, PP_MATERIAL);

        // SMAA is what the pane has been missing since the stock preview scene was
        // dropped; the terrain silhouette is one long high-contrast edge and reads as
        // low resolution without it.
        // FXAA takes the same priority slot when set, so the two can never stack.
        if (PP_MAT_FXAA != string.Empty)
        {
            m_World.SetCameraPostProcessEffect(CAMERA_INDEX, PP_PRIORITY_SMAA,
                PostProcessEffectType.FXAA, PP_MAT_FXAA);
        }
        else if (PP_MAT_SMAA != string.Empty)
        {
            m_World.SetCameraPostProcessEffect(CAMERA_INDEX, PP_PRIORITY_SMAA,
                PostProcessEffectType.SMAA, PP_MAT_SMAA);
        }

        if (PP_MAT_HBAO != string.Empty)
        {
            m_World.SetCameraPostProcessEffect(CAMERA_INDEX, PP_PRIORITY_AO,
                PostProcessEffectType.HBAO, PP_MAT_HBAO);
        }

        if (PP_MAT_SSR != string.Empty)
        {
            m_World.SetCameraPostProcessEffect(CAMERA_INDEX, PP_PRIORITY_SSR,
                PostProcessEffectType.SSR, PP_MAT_SSR);
        }

        Print(string.Format("[TDL_MAP3D] PP on cam %1 | hdr=1 smaa=%2 fxaa=%3 hbao=%4 ssr=%5",
            CAMERA_INDEX, PP_MAT_SMAA != string.Empty && PP_MAT_FXAA == string.Empty,
            PP_MAT_FXAA != string.Empty, PP_MAT_HBAO != string.Empty,
            PP_MAT_SSR != string.Empty), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    protected bool CreatePane(Widget host)
    {
        WorkspaceWidget workspace = GetGame().GetWorkspace();
        if (!workspace || !m_World)
            return false;

        // IGNORE_CURSOR | NOFOCUS is load-bearing, not tidiness: the pane sits above the
        // map canvas, and the pan/click handler is a widget component on the canvas
        // itself. Without these flags the pane consumes every mouse event before the
        // canvas sees it, so drag-to-orbit and click-through both die silently.
        // Authored widget already in the host's tree wins. Searching from the SURFACE
        // root rather than from the host means the widget can be placed anywhere in
        // that surface's layout — but no further: the menu and the world-space device
        // spawn the same layout, so a wider search could bind the other surface's pane.
        Widget searchRoot = GetSurfaceRoot(host);

        m_wPane = RenderTargetWidget.Cast(searchRoot.FindAnyWidget(PANE_WIDGET_NAME));
        if (m_wPane)
        {
            m_bPaneIsAuthored = true;
            Print(string.Format("[TDL_MAP3D] Using authored pane '%1' from the layout",
                PANE_WIDGET_NAME), LogLevel.DEBUG);
        }

        if (!m_wPane && PANE_LAYOUT != string.Empty)
        {
            m_wPaneRoot = workspace.CreateWidgets(PANE_LAYOUT, host);
            if (m_wPaneRoot)
                m_wPane = RenderTargetWidget.Cast(m_wPaneRoot.FindAnyWidget(PANE_WIDGET_NAME));

            if (!m_wPane)
            {
                Print(string.Format("[TDL_MAP3D] Pane layout has no RenderTargetWidget named '%1'",
                    PANE_WIDGET_NAME), LogLevel.WARNING);
                return false;
            }
        }

        if (!m_wPane)
        {
            m_wPane = RenderTargetWidget.Cast(workspace.CreateWidget(
                WidgetType.RenderTargetWidgetTypeID,
                WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
                new Color(0, 0, 0, 1), 0, host));
            if (!m_wPane)
                return false;

            // Slot geometry only applies to the code-created pane; an authored layout
            // states its own, and overwriting it would discard the declarative sizing
            // that is the reason the authored pane renders sharp.
            FrameSlot.SetAnchorMin(m_wPane, 0, 0);
            FrameSlot.SetAnchorMax(m_wPane, 1, 1);
            FrameSlot.SetOffsets(m_wPane, 0, 0, 0, 0);
        }

        if (m_bPaneIsAuthored)
        {
            // An authored pane states its own z-order, slot and flags. Overwriting them
            // would re-introduce the code path that renders soft; the only thing it needs
            // from here is to be shown. It ships hidden so a surface that has not claimed
            // the pane never covers its 2D canvas with an unbound black rectangle.
            m_wPane.SetVisible(true);
        }
        else
        {
            m_wPane.SetZOrder(PANE_Z_ORDER);
        }

        m_wPane.SetClearColor(true, ARGB(255, 0, 0, 0));

        // Update() BEFORE the resolution call, not after. The render target is sized from
        // the widget's extent, and a freshly created widget has none until a layout pass
        // gives it one — setting the resolution scale first allocates the target against
        // a zero-sized widget, and the result is then stretched across the real extent
        // for the rest of the session. Same trap that made GetScreenSize report 0x0 on
        // the drape. SetWorld comes last, after the target is correctly sized.
        m_wPane.Update();

        int fps = RT_FPS_ACTIVE;
        float res = RT_RES_ACTIVE;
        if (GetGame().IsPlatformGameConsole())
        {
            fps = RT_FPS_ACTIVE_CONSOLE;
            res = RT_RES_ACTIVE_CONSOLE;
        }

        // Checked after the console branch so it wins on both platform classes: the mirror's
        // budget is set by how small it is drawn, not by what the machine could manage.
        if (m_iHostRank == HOST_RANK_MIRROR)
        {
            fps = RT_FPS_MIRROR;
            res = RT_RES_MIRROR;
        }
        m_wPane.SetMaxFPS(fps);
        m_wPane.SetRefresh(1, 0);
        m_wPane.SetResolutionScale(res, res);

        m_wPane.SetWorld(m_World, CAMERA_INDEX);

        // Re-applied AFTER SetWorld, deliberately. Binding a world is what makes the widget
        // allocate its render target, so a scale set beforehand describes a target that does
        // not exist yet. Both calls are kept: the first covers the case where the widget
        // already had a target, the second is the one that lands on the target actually
        // being rendered into.
        m_wPane.SetMaxFPS(fps);
        m_wPane.SetResolutionScale(res, res);

        float paneW;
        float paneH;
        m_wPane.GetScreenSize(paneW, paneH);
        Print(string.Format("[TDL_MAP3D] Pane %1x%2 px at scale %3 -> %4x%5 rendered",
            paneW, paneH, res, paneW * res, paneH * res), LogLevel.DEBUG);

        CreateOverlayCanvas(workspace, host);
        DiagWidgetChain();

        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! The per-frame screen-space canvas over the pane. A code-created sibling appended
    //! after every authored child, so it composites above the pane and the marker
    //! overlay — matching the 2D order, where the bloodhound draws after markers.
    protected void CreateOverlayCanvas(WorkspaceWidget workspace, Widget host)
    {
        if (!OVERLAY_CANVAS_ENABLED)
            return;

        m_wOverlayCanvas = CanvasWidget.Cast(workspace.CreateWidget(
            WidgetType.CanvasWidgetTypeID,
            WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.IGNORE_CURSOR
                | WidgetFlags.NOFOCUS,
            new Color(1, 1, 1, 1), 0, host));
        if (!m_wOverlayCanvas)
        {
            Print("[TDL_MAP3D] Overlay canvas creation failed", LogLevel.WARNING);
            return;
        }

        FrameSlot.SetAnchorMin(m_wOverlayCanvas, 0, 0);
        FrameSlot.SetAnchorMax(m_wOverlayCanvas, 1, 1);
        FrameSlot.SetOffsets(m_wOverlayCanvas, 0, 0, 0, 0);
    }

    //------------------------------------------------------------------------------------------------
    //! Walk the pane's ancestry, reporting each widget's type and measured size.
    //!
    //! The render target is sized from the pane's extent, and the pane's extent comes
    //! from whatever its ancestors allow. A container that is smaller than it appears, or
    //! one whose slot type silently rejected the anchor calls, produces a correctly-
    //! reported pane over an undersized target — which looks exactly like a resolution
    //! problem while every number in the log reads correct. Measuring the chain is the
    //! only way to tell those apart.
    protected void DiagWidgetChain()
    {
        if (!DIAG_ENABLED || !m_wPane)
            return;

        Widget node = m_wPane;
        int depth = 0;

        while (node && depth < 12)
        {
            float nodeW;
            float nodeH;
            node.GetScreenSize(nodeW, nodeH);

            Print(string.Format("[TDL_MAP3D] CHAIN[%1] %2 '%3' %4x%5",
                depth, node.GetTypeName(), node.GetName(), nodeW, nodeH),
                LogLevel.DEBUG);

            node = node.GetParent();
            depth = depth + 1;
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Drive from the owning surface's per-frame update.
    //! Derives its own delta from the tick counter because the map view's draw path has
    //! no time slice to hand down, and this view should not force a signature change on it.
    void Tick()
    {
        if (!m_bOpen || !m_World)
            return;

        int now = System.GetTickCount();
        float timeSlice = 0;
        if (m_iLastTickMs != 0)
            timeSlice = (now - m_iLastTickMs) * 0.001;
        m_iLastTickMs = now;

        // A frame hitch or a paused menu must not let the camera jump; clamp before use.
        if (timeSlice > 0.1)
            timeSlice = 0.1;
        if (timeSlice <= 0)
            return;

        // Freeze the drape once it has had a few frames to render. Disabling the widget
        // leaves its last content on the mesh, so the painted map costs nothing after
        // this and the 2048px target stops re-rendering behind the pane every frame.
        if (m_iDrapeFreezeTicks > 0 && m_wDrapeRT)
        {
            m_iDrapeFreezeTicks = m_iDrapeFreezeTicks - 1;
            if (m_iDrapeFreezeTicks == 0)
            {
                // Measured once per drape build, not on every re-freeze — WakeDrape
                // reruns this countdown after each dynamic-content change, and the
                // size cannot have changed in between.
                if (!m_bDrapeMeasured)
                {
                    m_bDrapeMeasured = true;

                    // Now that the tree has been laid out, the measured size is
                    // meaningful. This is the number that says whether the drape is
                    // actually full size or whether the requested size was silently
                    // dropped.
                    float drapeW;
                    float drapeH;
                    m_wDrapeRT.GetScreenSize(drapeW, drapeH);

                    float texelM = 0;
                    if (drapeW > 0)
                        texelM = m_fSpanX / drapeW;

                    Print(string.Format("[TDL_MAP3D] Drape measured %1x%2 (asked %3) — %4 m/texel",
                        drapeW, drapeH, DRAPE_SIZE_PX, texelM), LogLevel.DEBUG);

                    // Each painter surface measured against the target it sits on. A
                    // canvas narrower than the drape clips its painter at that width and
                    // nothing reports it, so the mismatch is stated here rather than left
                    // to be inferred from content that stops partway across the map.
                    for (int c = 0, canvasCount = m_aDrapeCanvases.Count(); c < canvasCount; c = c + 1)
                    {
                        if (!m_aDrapeCanvases[c])
                            continue;

                        float canvasW;
                        float canvasH;
                        m_aDrapeCanvases[c].GetScreenSize(canvasW, canvasH);

                        Print(string.Format("[TDL_MAP3D] Drape %1 canvas %2x%3",
                            m_aDrapeCanvasLabels[c], canvasW, canvasH), LogLevel.DEBUG);

                        if (canvasW < drapeW || canvasH < drapeH)
                        {
                            Print(string.Format(
                                "[TDL_MAP3D] Drape %1 canvas is smaller than the drape (%2x%3 vs %4x%5) — its painter is being clipped",
                                m_aDrapeCanvasLabels[c], canvasW, canvasH, drapeW, drapeH),
                                LogLevel.WARNING);
                        }
                    }

                    if (drapeW < DRAPE_SIZE_PX)
                    {
                        Print("[TDL_MAP3D] Drape smaller than requested — code-created sizing did not apply",
                            LogLevel.WARNING);
                    }
                }

                m_wDrapeRT.SetEnabled(false);
            }
        }

        UpdateExposure(timeSlice);
        UpdateZoomInput(timeSlice);
        DiagTick();

        float blend = timeSlice * CAM_SMOOTH_RATE;
        if (blend > 1)
            blend = 1;

        m_vCameraRotation = m_vCameraRotation + (m_vTargetRotation - m_vCameraRotation) * blend;
        m_fCameraDistance = m_fCameraDistance + (m_fTargetDistance - m_fCameraDistance) * blend;
        m_vCameraTarget = m_vCameraTarget + (m_vTargetFocus - m_vCameraTarget) * blend;

        ApplyCamera();

        // After ApplyCamera, so the overlay projects against the pose actually being
        // rendered this frame rather than trailing it by one.
        UpdateOverlayCanvas();
    }

    //------------------------------------------------------------------------------------------------
    //! Poll the zoom axis. Both actions are read as values and subtracted into a signed
    //! axis so a device that reports both at once cancels out instead of fighting itself.
    protected void UpdateZoomInput(float timeSlice)
    {
        InputManager inputManager = GetGame().GetInputManager();
        if (!inputManager)
            return;

        float zoomIn = inputManager.GetActionValue(ACTION_ZOOM_IN);
        float zoomOut = inputManager.GetActionValue(ACTION_ZOOM_OUT);
        float axis = zoomIn - zoomOut;

        if (ZOOM_DIAG && (zoomIn != 0 || zoomOut != 0))
        {
            Print(string.Format("[TDL_MAP3D] zoom in=%1 out=%2 axis=%3 deadzone=%4",
                zoomIn, zoomOut, axis, ZOOM_DEADZONE), LogLevel.DEBUG);
        }

        if (Math.AbsFloat(axis) < ZOOM_DEADZONE)
        {
            m_bZoomLatched = false;
            m_fZoomHeldS = 0;
            return;
        }

        float direction = -1;
        if (axis > 0)
            direction = 1;

        // First frame of a new input always steps, so a single wheel click is never
        // swallowed. Continued input repeats on a timer, which is what makes a held
        // gamepad trigger behave like a zoom rather than a one-shot.
        bool shouldStep = false;
        if (!m_bZoomLatched)
        {
            shouldStep = true;
            m_bZoomLatched = true;
            m_fZoomHeldS = 0;
        }
        else
        {
            m_fZoomHeldS = m_fZoomHeldS + timeSlice;
            if (m_fZoomHeldS >= ZOOM_REPEAT_S)
            {
                shouldStep = true;
                m_fZoomHeldS = 0;
            }
        }

        if (shouldStep)
            ZoomInput(direction);
    }

    //------------------------------------------------------------------------------------------------
    protected void UpdateExposure(float timeSlice)
    {
        if (!m_SourceWorld || !m_World)
            return;

        if (AUTO_EXPOSURE)
        {
            UpdateAutoExposure(timeSlice);
            return;
        }

        CameraManager cameraManager = GetGame().GetCameraManager();
        if (!cameraManager)
            return;

        CameraBase current = cameraManager.CurrentCamera();
        if (!current)
            return;

        float exposure = m_SourceWorld.GetCameraHDRBrightness(current.GetCameraIndex());
        if (exposure < HDR_FLOOR)
            exposure = HDR_FLOOR;

        m_World.SetCameraHDRBrightness(CAMERA_INDEX, exposure);
        m_fLastExposure = exposure;
    }

    //------------------------------------------------------------------------------------------------
    //! Drive exposure from this camera's own metered brightness toward middle grey.
    //!
    //! An indexed camera runs no eye adaptation of its own, which is why exposure has to
    //! be pushed manually at all. Rather than borrow a number from a camera looking at a
    //! different scene, this closes the loop on GetCameraSceneMiddleBrightness for our
    //! own slot: too bright means divide the exposure down, too dark means raise it. It
    //! also makes the view self-correcting across time of day, which matters while the
    //! diorama clock is not reliably following the world's.
    protected void UpdateAutoExposure(float timeSlice)
    {
        float measured = m_World.GetCameraSceneMiddleBrightness(CAMERA_INDEX);

        float wantedTarget = EXPOSURE_TARGET_MID;
        TimeAndWeatherManagerEntity dioramaWeather =
            TimeAndWeatherManagerEntity.Cast(m_TimeWeatherEntity);
        if (dioramaWeather && dioramaWeather.IsSunSet())
            wantedTarget = EXPOSURE_TARGET_MID_NIGHT;

        // Trim moves the SETPOINT, not the exposure this loop computes for it. Scaling the
        // output would be undone within a second: the brightness this reads back is measured
        // off the frame that output produced, so the loop simply unwinds any trim applied
        // after the fact and settles on the same picture. Asking it to aim darker is the
        // only instruction it cannot compensate away.
        wantedTarget = wantedTarget
            * Math.Pow(2, EXPOSURE_BIAS_STOPS + AG0_TDLMapSatelliteOverride.GetExposureBias());

        // A day/night flip is a real lighting change, and the old minimum describes light
        // that no longer exists — a daytime floor would cap the night far too dark. A trim
        // change lands here too, and wants the same treatment for the same reason.
        // Camera movement must never reach this reset; only the light may.
        if (m_fLastWantedTarget > 0 && wantedTarget != m_fLastWantedTarget)
            m_fExposureMinSeen = 0;
        m_fLastWantedTarget = wantedTarget;

        if (m_fExposureTarget <= 0)
            m_fExposureTarget = wantedTarget;

        float targetBlend = timeSlice / EXPOSURE_TARGET_BLEND_S;
        if (targetBlend > 1)
            targetBlend = 1;
        m_fExposureTarget = m_fExposureTarget
            + (wantedTarget - m_fExposureTarget) * targetBlend;

        if (m_fExposure <= 0)
        {
            // Seed from the main camera so the first frame is in the right decade rather
            // than adapting up from black.
            CameraManager seedManager = GetGame().GetCameraManager();
            float seed = m_fExposureTarget;
            if (seedManager && seedManager.CurrentCamera())
                seed = m_SourceWorld.GetCameraHDRBrightness(seedManager.CurrentCamera().GetCameraIndex());

            if (seed <= 0)
                seed = HDR_FLOOR;
            m_fExposure = seed;
        }

        // A non-positive or unrendered reading carries no information; hold the last
        // value rather than treating it as "infinitely dark" and blowing the loop open.
        if (measured > 0)
        {
            float correction = m_fExposureTarget / measured;

            float blend = timeSlice * EXPOSURE_ADAPT_RATE;
            if (blend > 1)
                blend = 1;

            float targetExposure = m_fExposure * correction;
            m_fExposure = m_fExposure + (targetExposure - m_fExposure) * blend;
        }

        float clamped = Math.Clamp(m_fExposure, EXPOSURE_MIN, EXPOSURE_MAX);
        ReportExposureClampIfPinned(timeSlice, m_fExposure, clamped, measured);

        // Hold the integrator at the bound as well, for the same reason the ceiling below
        // does: a loop that keeps integrating past a clamp it cannot escape accumulates an
        // excursion it then has to unwind before the image responds to a real light change.
        m_fExposure = clamped;
        float applied = clamped;

        // The minimum is only meaningful once the loop has actually settled — the seed
        // and the first few frames are not measurements of anything.
        if (measured > 0)
        {
            if (m_fExposureMinSeen <= 0 || applied < m_fExposureMinSeen)
                m_fExposureMinSeen = applied;
        }

        if (m_fExposureMinSeen > 0)
        {
            float ceiling = m_fExposureMinSeen * EXPOSURE_MAX_OVER_MIN;
            if (applied > ceiling)
            {
                applied = ceiling;
                // Hold the internal value at the cap too, otherwise the loop keeps
                // integrating upward while the camera faces sky and then has to unwind
                // that whole excursion once terrain fills the frame again.
                m_fExposure = ceiling;
            }
        }

        m_World.SetCameraHDRBrightness(CAMERA_INDEX, applied);
        m_fLastExposure = applied;
        m_fLastSceneMid = measured;
    }

    //------------------------------------------------------------------------------------------------
    //! Warn when the hard exposure bounds are the reason the image looks wrong.
    //!
    //! A pinned clamp is invisible from the outside: the loop reports a sensible internal
    //! value while the scene stays blown out or black, and every knob above the clamp stops
    //! having any effect. That is a different failure from a mis-tuned target and wants a
    //! different fix — widen the bound, not trim the map — so it is worth naming in the log
    //! rather than leaving to a diagnostic build nobody is running when it happens.
    protected void ReportExposureClampIfPinned(float timeSlice, float wanted, float clamped,
        float measured)
    {
        if (m_fExposurePinnedWarnTimer > 0)
            m_fExposurePinnedWarnTimer -= timeSlice;

        if (wanted == clamped)
            return;
        if (measured <= 0)
            return;

        // Inside this band the loop has converged and the clamp is incidental.
        if (measured < m_fExposureTarget * EXPOSURE_PINNED_FACTOR
            && measured > m_fExposureTarget / EXPOSURE_PINNED_FACTOR)
            return;

        if (m_fExposurePinnedWarnTimer > 0)
            return;
        m_fExposurePinnedWarnTimer = EXPOSURE_PINNED_WARN_S;

        string bound = "EXPOSURE_MAX";
        if (wanted < clamped)
            bound = "EXPOSURE_MIN";

        Print(string.Format(
            "[TDL_MAP3D] Exposure pinned at %1 (wanted %2, clamped %3) — scene mid %4 vs target %5."
            + " This world needs an exposure the bounds do not allow; widen the bound rather than"
            + " trimming the map.",
            bound, wanted, clamped, measured, m_fExposureTarget), LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    //! Periodic state dump while open. A blown-out or blank frame in a video correlates
    //! to an exact camera pose and exposure this way, which a one-shot open line cannot
    //! do — most of the failures so far only showed up after the view had been sitting.
    protected void DiagTick()
    {
        if (!DIAG_ENABLED)
            return;

        int now = System.GetTickCount();
        if (m_iDiagLastMs != 0 && now - m_iDiagLastMs < DIAG_INTERVAL_MS)
            return;
        m_iDiagLastMs = now;

        float sceneBrightness = m_World.GetCameraSceneMiddleBrightness(CAMERA_INDEX);

        Print(string.Format(
            "[TDL_MAP3D] DIAG pose=%1 dist=%2 | exp=%3 (min %4, cap %5) mid=%6 target=%7 night=%8",
            m_vCameraRotation, m_fCameraDistance,
            m_fLastExposure, m_fExposureMinSeen,
            m_fExposureMinSeen * EXPOSURE_MAX_OVER_MIN, sceneBrightness,
            m_fExposureTarget, m_fExposureTarget < EXPOSURE_TARGET_MID * 0.5),
            LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    protected void ApplyCamera()
    {
        if (!m_World)
            return;

        vector cameraMatrix[4];
        Math3D.AnglesToMatrix(m_vCameraRotation, cameraMatrix);
        cameraMatrix[3] = m_vCameraTarget + m_fCameraDistance * cameraMatrix[2] * -1;

        if (CAMERA_FLOOR_CLAMP_ENABLED)
        {
            // Keep the eye above the rendered surface: at shallow pitch on a tall
            // miniature the orbit position otherwise ends up inside a hillside.
            // floorY is already diorama-local; only the clearance needs converting.
            float floorY;
            if (SampleDioramaY(cameraMatrix[3][0], cameraMatrix[3][2], floorY))
            {
                float minY = floorY + CAM_CLEARANCE_M / m_fScale;
                if (cameraMatrix[3][1] < minY)
                    cameraMatrix[3] = Vector(cameraMatrix[3][0], minY, cameraMatrix[3][2]);
            }
        }

        m_World.SetCameraEx(CAMERA_INDEX, cameraMatrix);
    }

    //------------------------------------------------------------------------------------------------
    //! Public surface query for geometry that has to sit on the terrain — see
    //! AG0_TDLMap3DStructures.c. Same sampler the camera pivot uses, so buildings and the
    //! focus agree about where the ground is.
    bool SampleSurfaceY(float localX, float localZ, out float outY)
    {
        return SampleDioramaY(localX, localZ, outY);
    }

    //------------------------------------------------------------------------------------------------
    protected bool SampleDioramaY(float localX, float localZ, out float outY)
    {
        outY = 0;
        for (int i = 0, count = m_aTiles.Count(); i < count; i = i + 1)
        {
            if (m_aTiles[i] && m_aTiles[i].SampleLocalY(localX, localZ, outY))
                return true;
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! World position -> pane pixels, for the screen-space overlay (markers and anything
    //! else positioned as a widget over the pane). Returns false when the point is behind
    //! the camera or the pane cannot be measured, so callers can hide rather than place a
    //! widget at a meaningless coordinate.
    //!
    //! The anchor is seated on the RENDERED surface, not at the caller's Y: 2D markers
    //! mark a place on the map, and most callers pass Y=0, which in the diorama is sea
    //! level — a hillside marker would float in front of the terrain or sink behind it.
    //! The caller's Y (miniaturised) is kept only where the point falls off the built
    //! slab and there is no surface to seat against.
    //! `useAltitude` says the caller's Y is a real height rather than a placeholder. Markers
    //! placed on the map carry no altitude at all — their world position is two ints and a
    //! zero — so seating everything on the terrain is right for them and wrong for anything
    //! that can leave the ground. An aircraft's icon stamped onto the hillside underneath it
    //! is not a position, it is a shadow.
    bool ProjectWorldToPane(vector worldPos, out float paneX, out float paneY, bool useAltitude = false)
    {
        paneX = 0;
        paneY = 0;
        if (!m_bOpen || !m_World || !m_wPane)
            return false;

        float paneW;
        float paneH;
        m_wPane.GetScreenSize(paneW, paneH);
        // Zero until the pane's first layout pass — same trap as every other widget
        // measurement in this file.
        if (paneW < 1 || paneH < 1)
            return false;

        float centerX = m_fMapMinX + m_fSpanX * 0.5;
        float centerZ = m_fMapMinZ + m_fSpanZ * 0.5;
        float localX = (worldPos[0] - centerX) / m_fScale;
        float localZ = (worldPos[2] - centerZ) / m_fScale;

        // Same transform the tile builder applies to terrain heights, so a real altitude
        // sits as far above the model's hills as it does above the real ones.
        float altitudeY = worldPos[1] * AG0_TDLMap3DTileEntity.HEIGHT_EXAGGERATION / m_fScale;

        float localY;
        float groundY;
        if (!SampleDioramaY(localX, localZ, groundY))
        {
            // Off the slab there is no ground to seat against, so the altitude is all there
            // is — true whether or not the caller vouched for it.
            localY = altitudeY;
        }
        else if (useAltitude && altitudeY > groundY + AIRBORNE_LIFT_MIN)
        {
            localY = altitudeY;
        }
        else
        {
            // Lifted slightly so hillside pixels never swallow the anchor on the
            // near side of a crest.
            localY = groundY + OVERLAY_ANCHOR_LIFT;
        }

        vector projected = m_World.ProjectWorldToViewport(
            Vector(localX, localY, localZ), CAMERA_INDEX, paneW, paneH);

        // The projection returns a point regardless of direction; only positive
        // camera-forward depth means the point is actually in front of the camera.
        if (projected[2] <= 0)
            return false;

        paneX = projected[0];
        paneY = projected[1];
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Diorama-local point -> game-world coordinates. Inverse of the transform the tile
    //! builder applies, exaggeration included, so a picked surface point round-trips to
    //! the height the terrain was sampled at.
    protected vector DioramaToWorld(vector local)
    {
        float centerX = m_fMapMinX + m_fSpanX * 0.5;
        float centerZ = m_fMapMinZ + m_fSpanZ * 0.5;
        return Vector(
            local[0] * m_fScale + centerX,
            local[1] * m_fScale / AG0_TDLMap3DTileEntity.HEIGHT_EXAGGERATION,
            local[2] * m_fScale + centerZ);
    }

    //------------------------------------------------------------------------------------------------
    //! Game-world position currently under the pane centre. The orbit focus is what the
    //! camera looks at, so it is the 3D answer to "where is the map centred" — crosshair
    //! placement and view-state sync both key off that question.
    vector GetFocusWorld()
    {
        return DioramaToWorld(m_vCameraTarget);
    }

    //------------------------------------------------------------------------------------------------
    //! Aim the orbit focus at a game-world position, seated on the rendered surface.
    //! Goes through m_vTargetFocus so the per-tick blend glides the view there — the
    //! same feel as pan, not a teleport.
    void FocusOnWorld(vector worldPos)
    {
        float centerX = m_fMapMinX + m_fSpanX * 0.5;
        float centerZ = m_fMapMinZ + m_fSpanZ * 0.5;
        float localX = (worldPos[0] - centerX) / m_fScale;
        float localZ = (worldPos[2] - centerZ) / m_fScale;

        // Off-mesh targets keep the previous focus height instead of snapping to sea
        // level — same rule the pan path uses.
        float localY;
        if (!SampleDioramaY(localX, localZ, localY))
            localY = m_vTargetFocus[1];

        m_vTargetFocus = Vector(localX, localY, localZ);
    }

    //------------------------------------------------------------------------------------------------
    //! Pane pixels -> GAME-world position on the rendered terrain. The inverse of
    //! ProjectWorldToPane, for click placement while the pane is up.
    //!
    //! Purely analytic: the tiles carry no physics (see the tile Build comment), so
    //! TraceMove has nothing to hit — the viewport ray is clipped to the slab and
    //! marched against the same bilinear sampler the tiles were built from, which also
    //! means a pick lands exactly on the surface markers are seated against. Rays that
    //! miss the slab fall through to the y=0 sea plane so ocean and shoreline clicks
    //! still resolve; only rays that never come down (above the horizon at shallow
    //! pitch) fail.
    bool PickWorldFromPane(float paneX, float paneY, out vector outWorldPos)
    {
        outWorldPos = vector.Zero;
        if (!m_bOpen || !m_World || !m_wPane)
            return false;

        float paneW;
        float paneH;
        m_wPane.GetScreenSize(paneW, paneH);
        if (paneW < 1 || paneH < 1)
            return false;

        vector rayDir;
        vector rayStart = m_World.ProjectViewportToWorld(paneX, paneY, CAMERA_INDEX,
            paneW, paneH, rayDir);
        if (rayDir.LengthSq() < 0.000001)
            return false;
        rayDir.Normalize();

        float farPlane = m_fMaxDistance * 2.5;
        if (farPlane < CAM_FAR_FLOOR)
            farPlane = CAM_FAR_FLOOR;

        vector localHit;
        if (!MarchDioramaSurface(rayStart, rayDir, farPlane, localHit))
        {
            if (rayDir[1] >= -0.000001)
                return false;

            float seaT = -rayStart[1] / rayDir[1];
            if (seaT < 0 || seaT > farPlane)
                return false;

            localHit = rayStart + rayDir * seaT;
        }

        outWorldPos = DioramaToWorld(localHit);
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! First crossing of a view ray with the tile heightfield, bisection-refined.
    //! Clipped to the slab's XZ bounds first so the march only spends samples where
    //! tiles exist, so the pick marches the tile sampler analytically instead.
    protected bool MarchDioramaSurface(vector rayStart, vector rayDir, float maxDistance,
        out vector outLocalHit)
    {
        outLocalHit = vector.Zero;

        float halfX = m_fSpanX * 0.5 / m_fScale;
        float halfZ = m_fSpanZ * 0.5 / m_fScale;
        float rayEnter = 0;
        float rayExit = maxDistance;

        if (Math.AbsFloat(rayDir[0]) < 0.000001)
        {
            if (rayStart[0] < -halfX || rayStart[0] > halfX)
                return false;
        }
        else
        {
            float enterX = (-halfX - rayStart[0]) / rayDir[0];
            float exitX = (halfX - rayStart[0]) / rayDir[0];
            if (enterX > exitX)
            {
                float swapX = enterX;
                enterX = exitX;
                exitX = swapX;
            }
            rayEnter = Math.Max(rayEnter, enterX);
            rayExit = Math.Min(rayExit, exitX);
        }

        if (Math.AbsFloat(rayDir[2]) < 0.000001)
        {
            if (rayStart[2] < -halfZ || rayStart[2] > halfZ)
                return false;
        }
        else
        {
            float enterZ = (-halfZ - rayStart[2]) / rayDir[2];
            float exitZ = (halfZ - rayStart[2]) / rayDir[2];
            if (enterZ > exitZ)
            {
                float swapZ = enterZ;
                enterZ = exitZ;
                exitZ = swapZ;
            }
            rayEnter = Math.Max(rayEnter, enterZ);
            rayExit = Math.Min(rayExit, exitZ);
        }

        if (rayExit < rayEnter || rayExit < 0)
            return false;
        rayEnter = Math.Max(rayEnter, 0);

        // Half a terrain cell per step: fine enough that a ridge one cell wide cannot
        // be stepped over, coarse enough that a full-slab march stays in the hundreds
        // of samples.
        float cellLocal = m_fSpanX
            / (TILES_PER_AXIS * AG0_TDLMap3DTileEntity.TILE_CELLS) / m_fScale;
        float marchStep = Math.Max(1, cellLocal * 0.5);

        float previousT = rayEnter;
        vector previousPoint = rayStart + rayDir * previousT;
        if (SurfaceDelta(previousPoint) <= 0)
        {
            // Grazing entry already at or under the surface — seat it and take it.
            float entryY;
            if (SampleDioramaY(previousPoint[0], previousPoint[2], entryY))
                previousPoint[1] = entryY;
            outLocalHit = previousPoint;
            return true;
        }

        for (int march = 0; march < 2048 && previousT < rayExit; march = march + 1)
        {
            float currentT = Math.Min(previousT + marchStep, rayExit);
            vector currentPoint = rayStart + rayDir * currentT;
            if (SurfaceDelta(currentPoint) <= 0)
            {
                float lowT = previousT;
                float highT = currentT;
                for (int refine = 0; refine < 10; refine = refine + 1)
                {
                    float midT = (lowT + highT) * 0.5;
                    if (SurfaceDelta(rayStart + rayDir * midT) > 0)
                        lowT = midT;
                    else
                        highT = midT;
                }

                vector hitPoint = rayStart + rayDir * highT;
                float hitY;
                if (SampleDioramaY(hitPoint[0], hitPoint[2], hitY))
                    hitPoint[1] = hitY;
                outLocalHit = hitPoint;
                return true;
            }

            previousT = currentT;
        }

        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Height of `point` above the tile surface; positive means airborne. Positions
    //! with no tile under them read as far above ground so the march keeps going
    //! instead of hitting a phantom surface at the slab edge.
    protected float SurfaceDelta(vector point)
    {
        float surfaceY;
        if (!SampleDioramaY(point[0], point[2], surfaceY))
            return 1000000;
        return point[1] - surfaceY;
    }

    //------------------------------------------------------------------------------------------------
    //! Screen-space rotation for a marker facing `worldHeading` at `worldPos`, derived by
    //! projecting a second point ahead of the first rather than by offsetting the heading
    //! with camera yaw — under perspective pitch the ground plane is foreshortened, so a
    //! yaw-offset arrow reads up to ~15-20 degrees wrong at shallow pitch. Returns false
    //! when either point fails to project; callers fall back to the yaw approximation.
    bool ProjectHeadingToPane(vector worldPos, float worldHeading, out float screenHeadingDeg)
    {
        screenHeadingDeg = worldHeading;

        float ax;
        float ay;
        if (!ProjectWorldToPane(worldPos, ax, ay))
            return false;

        float headingRad = worldHeading * Math.DEG2RAD;
        vector ahead = worldPos + Vector(
            Math.Sin(headingRad), 0, Math.Cos(headingRad)) * HEADING_SAMPLE_AHEAD_M;

        float bx;
        float by;
        if (!ProjectWorldToPane(ahead, bx, by))
            return false;

        float dx = bx - ax;
        float dy = by - ay;
        // Both points landing on the same pixel means the sampled direction is edge-on
        // to the camera and the angle would be noise.
        if (dx * dx + dy * dy < 0.01)
            return false;

        // Clockwise from screen-up, matching ImageWidget.SetRotation. Screen Y grows
        // downward, hence the negated dy.
        screenHeadingDeg = Math.Atan2(dx, -dy) * Math.RAD2DEG;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Aim camera yaw, leaving pitch, roll and distance alone.
    //!
    //! Written to the target so the per-tick blend glides into it — a running player's
    //! heading jitters, and driving the live rotation would pass that straight into the
    //! image. The target is first rebased onto the same revolution as the current yaw,
    //! because that blend is a plain lerp with no angular wrapping: orbit drag leaves
    //! yaw unbounded (it can sit at 725 degrees), so handing it a raw 0..360 heading
    //! would spin the diorama the long way round rather than taking the short arc.
    void SetCameraYaw(float yawDeg)
    {
        float delta = yawDeg - m_vCameraRotation[0];
        delta = delta - Math.Floor(delta / 360 + 0.5) * 360;

        m_vTargetRotation = Vector(m_vCameraRotation[0] + delta,
            m_vTargetRotation[1], m_vTargetRotation[2]);
    }

    //------------------------------------------------------------------------------------------------
    //! Smoothed (rendered) yaw, for consumers that need the 3D equivalent of the 2D
    //! map's rotation — negated by the caller, since a camera yawed right shows the
    //! world rotated left on screen.
    float GetCameraYaw()
    {
        return m_vCameraRotation[0];
    }

    //------------------------------------------------------------------------------------------------
    void OrbitInput(float deltaYaw, float deltaPitch)
    {
        m_vTargetRotation = Vector(
            m_vTargetRotation[0] + deltaYaw,
            Math.Clamp(m_vTargetRotation[1] + deltaPitch, MIN_PITCH, MAX_PITCH),
            0);
    }

    //------------------------------------------------------------------------------------------------
    //! Translate the orbit focus across the terrain so the surface follows the cursor.
    //!
    //! The rate is derived from the projection rather than tuned, because a hand-tuned
    //! constant is only correct at one pane size and one pitch. At the focus distance the
    //! visible height of the world is 2*d*tan(fov/2), so one pixel is that over the pane
    //! height in pixels. The forward axis additionally divides by sin(pitch): the ground
    //! plane is foreshortened when the camera looks along it, so a pixel of vertical drag
    //! covers more ground than a pixel of horizontal drag — using one rate for both is
    //! what makes pan feel slightly fast.
    void PanInput(float deltaX, float deltaZ)
    {
        if (!m_wPane)
            return;

        float paneW;
        float paneH;
        m_wPane.GetScreenSize(paneW, paneH);
        if (paneH < 1)
            return;

        vector cameraMatrix[4];
        Math3D.AnglesToMatrix(m_vCameraRotation, cameraMatrix);

        // Flatten the basis: at steep pitch the raw forward vector is mostly vertical,
        // and panning with it would drive the focus underground instead of across.
        vector right = Vector(cameraMatrix[0][0], 0, cameraMatrix[0][2]);
        vector forward = Vector(cameraMatrix[2][0], 0, cameraMatrix[2][2]);
        right.Normalize();
        forward.Normalize();

        float metresPerPixel = 2 * m_fCameraDistance
            * Math.Tan(CAM_FOV * 0.5 * Math.DEG2RAD) / paneH;

        float pitchSin = Math.AbsFloat(Math.Sin(m_vCameraRotation[1] * Math.DEG2RAD));
        if (pitchSin < PAN_PITCH_SIN_FLOOR)
            pitchSin = PAN_PITCH_SIN_FLOOR;

        float lateralStep = metresPerPixel * PAN_SENSITIVITY;
        float forwardStep = lateralStep / pitchSin;

        vector moved = m_vTargetFocus - right * deltaX * lateralStep
            - forward * deltaZ * forwardStep;

        float focusX = moved[0];
        float focusZ = moved[2];
        if (PAN_LIMIT_ENABLED)
        {
            focusX = Math.Clamp(focusX, -m_fPanLimitX, m_fPanLimitX);
            focusZ = Math.Clamp(focusZ, -m_fPanLimitZ, m_fPanLimitZ);
        }

        // Focus rides the terrain rather than staying on the y=0 plane. Orbiting a fixed
        // sea-level point means panning inland leaves the pivot buried under the ground,
        // so the view tilts away from the surface and the apparent camera height changes
        // with elevation. Following the surface keeps the pivot on what is being looked
        // at. Off-mesh positions keep the previous height instead of snapping to zero.
        float surfaceY = m_vTargetFocus[1];
        float sampledY;
        if (SampleDioramaY(focusX, focusZ, sampledY))
            surfaceY = sampledY;

        m_vTargetFocus = Vector(focusX, surfaceY, focusZ);
    }

    //------------------------------------------------------------------------------------------------
    void ZoomInput(float direction)
    {
        float factor = 1.22;
        if (direction > 0)
            factor = 0.82;

        m_fTargetDistance = Math.Clamp(m_fTargetDistance * factor,
            MIN_DISTANCE / m_fScale, m_fMaxDistance);
    }

    //------------------------------------------------------------------------------------------------
    //! Detach the pane. The world and its geometry are left standing unless
    //! RELEASE_WORLD_ON_CLOSE is set — see the file header for why those are separate.
    void Close()
    {
        if (!m_bOpen)
            return;

        m_bOpen = false;
        ReleasePane();

        Print(string.Format("[TDL_MAP3D] CLOSE | destroyWorld=%1", RELEASE_WORLD_ON_CLOSE),
            LogLevel.DEBUG);

        if (RELEASE_WORLD_ON_CLOSE)
            DestroyWorld();
    }

    //------------------------------------------------------------------------------------------------
    //! The pane must stop referencing the private world BEFORE the widget dies, and the
    //! widget must die before anything in that world is touched. Both directions of that
    //! ordering are render-thread constraints, not script ones.
    protected void ReleasePane()
    {
        // The overlay canvas is code-created into the host tree, so it dies with the
        // pane and comes back in CreatePane — leaving it would leak one canvas per
        // rehost into a menu tree that no longer drives it.
        if (m_wOverlayCanvas)
        {
            m_wOverlayCanvas.RemoveFromHierarchy();
            m_wOverlayCanvas = null;
        }

        if (!m_wPane)
            return;

        m_wPane.SetWorld(null, CAMERA_INDEX);

        if (m_bPaneIsAuthored)
        {
            // The layout owns this widget — hide it rather than destroying part of the
            // menu, which would not come back when the view reopens.
            m_wPane.SetVisible(false);
        }
        else if (m_wPaneRoot)
        {
            // A spawned layout brings a root above the pane; dropping only the pane
            // would leak that root on every reopen.
            m_wPaneRoot.RemoveFromHierarchy();
        }
        else
        {
            m_wPane.RemoveFromHierarchy();
        }

        m_bPaneIsAuthored = false;
        m_wPaneRoot = null;
        m_wPane = null;
        m_wHost = null;
    }

    //------------------------------------------------------------------------------------------------
    //! Forced teardown, gated behind RELEASE_WORLD_ON_CLOSE.
    //!
    //! Order is deliberate: the pane is already unbound and gone by the time this runs,
    //! so no render-thread reference survives into the entity deletes. Entity deletion
    //! goes through the engine's normal detach path. The world ref is released LAST and
    //! only by dropping the handle — there is no world destruction call to make, so this
    //! is a refcount drop and nothing more.
    //!
    //! If a native crash lands here, the useful next datapoint is whether it still occurs
    //! with the tile entities left alive (delete nothing, drop only the ref).
    protected void DestroyWorld()
    {
        // The drape binding lives ON the tile's mesh object, so it has to be removed from
        // each tile before that tile is deleted — otherwise the render thread still holds
        // a reference to geometry that no longer exists. SetRenderTarget(null) does not
        // do this; it never touches the entity.
        for (int r = 0, releaseCount = m_aTiles.Count(); r < releaseCount; r = r + 1)
        {
            if (m_wDrapeRT && m_aTiles[r])
                m_wDrapeRT.RemoveRenderTarget(m_aTiles[r]);
        }

        // Removing the container takes the RT and its painted children with it.
        if (m_wDrapeRoot)
            m_wDrapeRoot.RemoveFromHierarchy();
        m_wDrapeRoot = null;
        m_wDrapeRT = null;
        m_wDrapeRaster = null;
        m_wShapesCanvas = null;
        m_aDrapeCanvases.Clear();
        m_aDrapeCanvasLabels.Clear();
        m_iDrapeFreezeTicks = 0;
        m_bDrapeMeasured = false;
        // A rebuilt drape starts blank, so the first shape feed must repaint even if
        // the shape set is unchanged from before the teardown.
        m_iDrapeShapeSignature = 0;
        m_bDrapeGhostWasActive = false;

        for (int b = 0, batchCount = m_aStructureBatches.Count(); b < batchCount; b = b + 1)
        {
            if (m_aStructureBatches[b])
                SCR_EntityHelper.DeleteEntityAndChildren(m_aStructureBatches[b]);
        }
        m_aStructureBatches.Clear();

        for (int i = 0, count = m_aTiles.Count(); i < count; i = i + 1)
        {
            if (m_aTiles[i])
                SCR_EntityHelper.DeleteEntityAndChildren(m_aTiles[i]);
        }
        m_aTiles.Clear();

        if (m_EnvProbeEntity)
            SCR_EntityHelper.DeleteEntityAndChildren(m_EnvProbeEntity);
        m_EnvProbeEntity = null;

        if (m_PostProcessEntity)
            SCR_EntityHelper.DeleteEntityAndChildren(m_PostProcessEntity);
        m_PostProcessEntity = null;

        if (m_LightingRigEntity)
            SCR_EntityHelper.DeleteEntityAndChildren(m_LightingRigEntity);
        m_LightingRigEntity = null;

        if (m_TimeWeatherEntity)
            SCR_EntityHelper.DeleteEntityAndChildren(m_TimeWeatherEntity);
        m_TimeWeatherEntity = null;

        if (m_LightingEntity)
            SCR_EntityHelper.DeleteEntityAndChildren(m_LightingEntity);
        m_LightingEntity = null;

        m_World = null;
        m_WorldRef = null;
        m_bBuilt = false;
    }
}
