//------------------------------------------------------------------------------------------------
// AG0_TDLMap3DStructures.c
//
// Extruded building footprints for the 3D view, built from the same streamed structure
// dataset the 2D map draws — AG0_TDLTerrainStructureManager, delivered over the chunked
// RPC path. No second source of truth and no extra network cost.
//
// Roads deliberately have no equivalent here: the map raster draped over the terrain
// already contains them, drawn at the cartographer's own weights and colours. Re-drawing
// them as geometry would double every line and disagree with the 2D map.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Transient upload buffers for one batch of buildings.
//! Sized for BOXES_PER_BATCH boxes at 20 verts / 30 indices each — 384 keeps the three
//! arrays to ~199 KB together, inside the 256 KB per-class ceiling. Allocated only for
//! the duration of Build().
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DStructureBuffers
{
    vector m_aVerts[7680];
    float m_aUVs[15360];
    int m_aIndices[11520];
}

class AG0_TDLMap3DStructureBatchClass : GenericEntityClass
{
}

//------------------------------------------------------------------------------------------------
//! One mesh of up to BOXES_PER_BATCH extruded building footprints, in diorama space.
//!
//! Batched rather than one entity per building because a populated map runs to thousands
//! of footprints, and an entity each would cost more in submission overhead than the
//! geometry itself. Single-geometry MeshObject (numMeshes = 1), matching the terrain tiles.
//------------------------------------------------------------------------------------------------
class AG0_TDLMap3DStructureBatch : GenericEntity
{
    static const int BOXES_PER_BATCH = 384;

    protected static const int VERTS_PER_BOX = 20;
    protected static const int INDICES_PER_BOX = 30;

    //! Walls taper by this fraction toward the centre as they rise, so neighbouring
    //! buildings read as separate blocks instead of fusing where their walls touch.
    //!
    //! Applied to the top ring of the WALLS as well as the roof. Insetting only the roof
    //! leaves a ring of missing geometry between the wall tops and the roof edge — the
    //! buildings look open at the top, because they are.
    protected static const float ROOF_INSET = 0.06;

    protected bool m_bBuilt;

    //------------------------------------------------------------------------------------------------
    //! @param records    Source rows, in API-native units (metres, radians).
    //! @param first      Index of the first record for this batch.
    //! @param count      How many records to consume.
    //! @param sampler    Supplies diorama-local ground height at a diorama XZ.
    //! @param centerX/Z  World position mapped to diorama origin.
    //! @param scale      World metres per diorama metre.
    //! @param heightMul  Vertical exaggeration applied on top of the scale.
    //! @param material   Material for the generated geometry.
    //! @param part  PART_ALL, PART_WALLS or PART_ROOFS.
    //!
    //! Split exists only because MeshObject.Create takes one material per mesh, so giving
    //! roofs a different tone from walls requires a second mesh. When both share a
    //! material there is nothing to gain from that, and PART_ALL puts the whole box in
    //! one mesh instead of doubling the entity count for an identical result.
    static const int PART_ALL = 0;
    static const int PART_WALLS = 1;
    static const int PART_ROOFS = 2;

    bool Build(notnull array<ref AG0_TDLTerrainStructureRecord> records, int first, int count,
        AG0_TDLMap3DView sampler, float centerX, float centerZ, float scale,
        float heightMul, string material, int part)
    {
        if (m_bBuilt || count <= 0 || scale <= 0)
            return false;

        AG0_TDLMap3DStructureBuffers buffers = new AG0_TDLMap3DStructureBuffers();

        int vertIndex = 0;
        int uvIndex = 0;
        int idx = 0;
        int emitted = 0;

        for (int i = 0; i < count; i = i + 1)
        {
            int recordIndex = first + i;
            if (recordIndex >= records.Count())
                break;

            AG0_TDLTerrainStructureRecord record = records[recordIndex];
            if (!record)
                continue;

            float localX = (record.m_fCenterX - centerX) / scale;
            float localZ = (record.m_fCenterZ - centerZ) / scale;

            // Seat on the DIORAMA's own surface, not the true terrain. The mesh is a
            // coarse resample, so a building placed at its real elevation floats or sinks
            // wherever the two disagree — which is everywhere on a slope.
            float groundY;
            if (!sampler.SampleSurfaceY(localX, localZ, groundY))
                continue;

            float halfW = record.m_fWidth * 0.5 / scale;
            float halfD = record.m_fDepth * 0.5 / scale;
            if (halfW <= 0 || halfD <= 0)
                continue;

            float height = record.m_fHeight * heightMul / scale;
            if (height <= 0)
                continue;

            float cosR = Math.Cos(record.m_fRotation);
            float sinR = Math.Sin(record.m_fRotation);

            // Footprint corners, rotated about the centre.
            float cx[4];
            float cz[4];
            cx[0] = -halfW; cz[0] = -halfD;
            cx[1] = halfW;  cz[1] = -halfD;
            cx[2] = halfW;  cz[2] = halfD;
            cx[3] = -halfW; cz[3] = halfD;

            float wx[4];
            float wz[4];
            float tx[4];
            float tz[4];
            for (int c = 0; c < 4; c = c + 1)
            {
                wx[c] = localX + cx[c] * cosR - cz[c] * sinR;
                wz[c] = localZ + cx[c] * sinR + cz[c] * cosR;

                // The single inset ring the wall tops and the roof both use.
                tx[c] = wx[c] + (localX - wx[c]) * ROOF_INSET;
                tz[c] = wz[c] + (localZ - wz[c]) * ROOF_INSET;
            }

            // Four side quads: base ring at the footprint, top ring at the inset.
            for (int s = 0; s < 4 && part != PART_ROOFS; s = s + 1)
            {
                int next = s + 1;
                if (next > 3)
                    next = 0;

                buffers.m_aVerts[vertIndex] = Vector(wx[s], groundY, wz[s]);
                buffers.m_aVerts[vertIndex + 1] = Vector(wx[next], groundY, wz[next]);
                buffers.m_aVerts[vertIndex + 2] = Vector(tx[next], groundY + height, tz[next]);
                buffers.m_aVerts[vertIndex + 3] = Vector(tx[s], groundY + height, tz[s]);

                buffers.m_aUVs[uvIndex] = 0;     buffers.m_aUVs[uvIndex + 1] = 1;
                buffers.m_aUVs[uvIndex + 2] = 1; buffers.m_aUVs[uvIndex + 3] = 1;
                buffers.m_aUVs[uvIndex + 4] = 1; buffers.m_aUVs[uvIndex + 5] = 0;
                buffers.m_aUVs[uvIndex + 6] = 0; buffers.m_aUVs[uvIndex + 7] = 0;
                uvIndex = uvIndex + 8;

                buffers.m_aIndices[idx] = vertIndex;
                buffers.m_aIndices[idx + 1] = vertIndex + 2;
                buffers.m_aIndices[idx + 2] = vertIndex + 1;
                buffers.m_aIndices[idx + 3] = vertIndex;
                buffers.m_aIndices[idx + 4] = vertIndex + 3;
                buffers.m_aIndices[idx + 5] = vertIndex + 2;
                idx = idx + 6;

                vertIndex = vertIndex + 4;
            }

            if (part != PART_WALLS)
            {
                // Captured here rather than at the top of the box: in PART_ALL the roof
                // follows sixteen wall verts, in PART_ROOFS it is the first thing emitted.
                int roofBase = vertIndex;

                // Roof quad, sharing the wall tops' inset ring exactly so the two meet.
                float roofY = groundY + height;
                for (int r = 0; r < 4; r = r + 1)
                {
                    buffers.m_aVerts[vertIndex + r] = Vector(tx[r], roofY, tz[r]);
                    buffers.m_aUVs[uvIndex] = 0;
                    buffers.m_aUVs[uvIndex + 1] = 0;
                    uvIndex = uvIndex + 2;
                }

                buffers.m_aIndices[idx] = roofBase;
                buffers.m_aIndices[idx + 1] = roofBase + 2;
                buffers.m_aIndices[idx + 2] = roofBase + 1;
                buffers.m_aIndices[idx + 3] = roofBase;
                buffers.m_aIndices[idx + 4] = roofBase + 3;
                buffers.m_aIndices[idx + 5] = roofBase + 2;
                idx = idx + 6;

                vertIndex = vertIndex + 4;
            }

            emitted = emitted + 1;
        }

        if (emitted == 0)
            return false;

        int vertexCounts[1];
        vertexCounts[0] = vertIndex;
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
        Update();

        m_bBuilt = true;
        return true;
    }
}
