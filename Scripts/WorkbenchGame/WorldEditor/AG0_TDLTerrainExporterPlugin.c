#ifdef WORKBENCH

// ============================================================================
// TDL TERRAIN DATA EXPORTER - Main Plugin
// ============================================================================
[WorkbenchPluginAttribute(
    name: "TDL Terrain Data Exporter",
    description: "Export comprehensive terrain data for TDL web application",
    shortcut: "Ctrl+Shift+T",
    wbModules: { "WorldEditor" },
    category: "TDL",
    awesomeFontCode: 0xf0ac)
]
class TDL_TerrainExporter : WorldEditorPlugin
{
    // ========== Export Toggles ==========
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export terrain metadata (bounds, size, etc.)")]
    bool m_bExportMetadata;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export heightmap grid data")]
    bool m_bExportHeightmap;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export elevation contour lines")]
    bool m_bExportContours;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export road network")]
    bool m_bExportRoads;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export water features (rivers, lakes, ocean)")]
    bool m_bExportWater;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export structures and buildings")]
    bool m_bExportStructures;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export points of interest")]
    bool m_bExportPOIs;
    
    [Attribute(defvalue: "True", category: "1. Exports", desc: "Export vegetation density map")]
    bool m_bExportVegetation;
    
    // ========== Heightmap Settings ==========
    [Attribute(defvalue: "5", category: "2. Heightmap", desc: "Sample interval in meters (smaller = more detail, slower)")]
    float m_fHeightmapCellSize;
    
    [Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, enums: { ParamEnum("ASC (ESRI Grid)", "0"), ParamEnum("JSON Array", "1"), ParamEnum("Both", "2") }, category: "2. Heightmap")]
    int m_iHeightmapFormat;
    
    // ========== Contour Settings ==========
    [Attribute(defvalue: "10", category: "3. Contours", desc: "Elevation interval between contour lines (meters)")]
    float m_fContourInterval;
    
    [Attribute(defvalue: "50", category: "3. Contours", desc: "Major contour interval (meters) - rendered thicker")]
    float m_fMajorContourInterval;
    
    [Attribute(defvalue: "2", category: "3. Contours", desc: "Simplification tolerance (higher = less points)")]
    float m_fContourSimplification;
    
    // ========== Vegetation Settings ==========
    [Attribute(defvalue: "25", category: "4. Vegetation", desc: "Sample cell size for vegetation density")]
    float m_fVegetationCellSize;
    
    [Attribute(defvalue: "50", category: "4. Vegetation", desc: "Query radius for tree detection")]
    float m_fVegetationQueryRadius;
    
    // ========== Output Settings ==========
    [Attribute(defvalue: "$profile:TDL_TerrainExport", category: "5. Output", desc: "Output directory path")]
    string m_sOutputDirectory;
    
    [Attribute(defvalue: "", category: "5. Output", desc: "File prefix (empty = use world name)")]
    string m_sFilePrefix;
    
    // ========== Internal State ==========
    protected ref TDL_ExportContext m_Context;
    
    //------------------------------------------------------------------------------------------------
    override void Run()
    {
        Workbench.ScriptDialog("TDL Terrain Data Exporter", "Configure export settings and click Export", this);
    }
    
    //------------------------------------------------------------------------------------------------
    [ButtonAttribute("Export Selected", true)]
    protected bool ButtonExport()
    {
        // Initialize context
        m_Context = new TDL_ExportContext();
        if (!m_Context.Initialize())
        {
            Workbench.Dialog("Export Failed", "Could not initialize export context. Is a world loaded?");
            return false;
        }
        
        // Set output paths
        string prefix = m_sFilePrefix;
        if (prefix.IsEmpty())
            prefix = m_Context.GetWorldName();
        
        m_Context.SetOutputPath(m_sOutputDirectory, prefix);
        
        // Create output directory
        FileIO.MakeDirectory(m_sOutputDirectory);
        
        int startTime = System.GetTickCount();
        int exportCount = 0;
        
        // Run exports
        if (m_bExportMetadata)
        {
            TDL_Export_Metadata.Export(m_Context);
            exportCount++;
        }
        
        if (m_bExportHeightmap)
        {
            TDL_Export_Heightmap.Export(m_Context, m_fHeightmapCellSize, m_iHeightmapFormat);
            exportCount++;
        }
        
        if (m_bExportContours)
        {
            TDL_Export_Contours.Export(m_Context, m_fHeightmapCellSize, m_fContourInterval, m_fMajorContourInterval, m_fContourSimplification);
            exportCount++;
        }
        
        if (m_bExportRoads)
        {
            TDL_Export_Roads.Export(m_Context);
            exportCount++;
        }
        
        if (m_bExportWater)
        {
            TDL_Export_Water.Export(m_Context);
            exportCount++;
        }
        
        if (m_bExportStructures)
        {
            TDL_Export_Structures.Export(m_Context);
            exportCount++;
        }
        
        if (m_bExportPOIs)
        {
            TDL_Export_POIs.Export(m_Context);
            exportCount++;
        }
        
        if (m_bExportVegetation)
        {
            TDL_Export_Vegetation.Export(m_Context, m_fVegetationCellSize, m_fVegetationQueryRadius);
            exportCount++;
        }
        
        int elapsed = System.GetTickCount() - startTime;
        
        string summary = string.Format("Exported %1 datasets in %2ms\nOutput: %3", exportCount, elapsed, m_sOutputDirectory);
        Print("[TDL Export] " + summary);
        Workbench.Dialog("TDL Export Complete", summary);
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    [ButtonAttribute("Open Output Folder")]
    protected bool ButtonOpenFolder()
    {
        Workbench.RunCmd("explorer \"" + m_sOutputDirectory + "\"");
        return false;
    }
}

// ============================================================================
// EXPORT CONTEXT - Shared state for all exporters
// ============================================================================
class TDL_ExportContext
{
    // Engine references
    protected WorldEditor m_WorldEditor;
    protected WorldEditorAPI m_API;
    protected BaseWorld m_World;
    
    // World bounds
    protected vector m_vMin;
    protected vector m_vMax;
    protected vector m_vSize;
    protected float m_fOceanHeight;
    
    // Output config
    protected string m_sOutputDir;
    protected string m_sPrefix;
    
    //------------------------------------------------------------------------------------------------
    bool Initialize()
    {
        m_WorldEditor = Workbench.GetModule(WorldEditor);
        if (!m_WorldEditor)
            return false;
        
        m_API = m_WorldEditor.GetApi();
        if (!m_API)
            return false;
        
        m_World = m_API.GetWorld();
        if (!m_World)
            return false;
        
        m_WorldEditor.GetTerrainBounds(m_vMin, m_vMax);
        m_vSize = m_vMax - m_vMin;
        m_fOceanHeight = m_World.GetOceanBaseHeight();
        
        Print(string.Format("[TDL Export] World: Min=%1 Max=%2 Size=%3 Ocean=%4", 
            m_vMin.ToString(), m_vMax.ToString(), m_vSize.ToString(), m_fOceanHeight));
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetOutputPath(string dir, string prefix)
    {
        m_sOutputDir = dir;
        m_sPrefix = prefix;
    }
    
    //------------------------------------------------------------------------------------------------
    string GetOutputFile(string suffix)
    {
        return string.Format("%1/%2_%3", m_sOutputDir, m_sPrefix, suffix);
    }
    
    //------------------------------------------------------------------------------------------------
    string GetWorldName()
    {
        ResourceName worldPath;
		m_API.GetWorldPath(worldPath);
        if (!worldPath)
            return "unknown";
        
        // Extract just the world name from path
        string name = worldPath;
        int lastSlash = name.LastIndexOf("/");
        if (lastSlash >= 0)
            name = name.Substring(lastSlash + 1, name.Length() - lastSlash - 1);
        int dotPos = name.IndexOf(".");
        if (dotPos >= 0)
            name = name.Substring(0, dotPos);
        
        return name;
    }
    
    // Accessors
    WorldEditorAPI GetAPI() { return m_API; }
    BaseWorld GetWorld() { return m_World; }
    vector GetMin() { return m_vMin; }
    vector GetMax() { return m_vMax; }
    vector GetSize() { return m_vSize; }
    float GetOceanHeight() { return m_fOceanHeight; }
}

// ============================================================================
// METADATA EXPORTER
// ============================================================================
class TDL_Export_Metadata
{
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx)
    {
        string path = ctx.GetOutputFile("metadata.json");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file)
        {
            Print("[TDL Export] ERROR: Could not create " + path, LogLevel.ERROR);
            return;
        }
        
        vector min = ctx.GetMin();
        vector max = ctx.GetMax();
        vector size = ctx.GetSize();
        
        file.WriteLine("{");
        file.WriteLine(string.Format("  \"name\": \"%1\",", ctx.GetWorldName()));
        file.WriteLine(string.Format("  \"exportVersion\": \"%1\",", "1.0.0"));
        file.WriteLine(string.Format("  \"exportTimestamp\": %1,", System.GetUnixTime()));
        file.WriteLine("  \"bounds\": {");
        file.WriteLine(string.Format("    \"min\": [%1, %2, %3],", min[0], min[1], min[2]));
        file.WriteLine(string.Format("    \"max\": [%1, %2, %3]", max[0], max[1], max[2]));
        file.WriteLine("  },");
        file.WriteLine("  \"size\": {");
        file.WriteLine(string.Format("    \"x\": %1,", size[0]));
        file.WriteLine(string.Format("    \"y\": %1,", size[1]));
        file.WriteLine(string.Format("    \"z\": %1", size[2]));
        file.WriteLine("  },");
        file.WriteLine(string.Format("  \"oceanHeight\": %1,", ctx.GetOceanHeight()));
        file.WriteLine("  \"coordinateSystem\": {");
        file.WriteLine("    \"type\": \"cartesian\",");
        file.WriteLine("    \"xAxis\": \"east\",");
        file.WriteLine("    \"yAxis\": \"up\",");
        file.WriteLine("    \"zAxis\": \"north\",");
        file.WriteLine("    \"units\": \"meters\"");
        file.WriteLine("  }");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Metadata -> " + path);
    }
}

// ============================================================================
// HEIGHTMAP EXPORTER
// ============================================================================
class TDL_Export_Heightmap
{
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx, float cellSize, int format)
    {
        vector size = ctx.GetSize();
        vector min = ctx.GetMin();
        BaseWorld world = ctx.GetWorld();
        
        int cols = Math.Ceil(size[0] / cellSize) + 1;
        int rows = Math.Ceil(size[2] / cellSize) + 1;
        int total = cols * rows;
        
        Print(string.Format("[TDL Export] Sampling heightmap: %1 x %2 = %3 samples", cols, rows, total));
        
        // Sample heights
        array<float> heights = {};
        heights.Reserve(total);
        
        float minH = float.MAX;
        float maxH = -float.MAX;
        
        int progressStep = rows / 10;
        if (progressStep < 1) progressStep = 1;
        
        for (int row = 0; row < rows; row++)
        {
            float worldZ = min[2] + (row * cellSize);
            
            for (int col = 0; col < cols; col++)
            {
                float worldX = min[0] + (col * cellSize);
                float h = world.GetSurfaceY(worldX, worldZ);
                
                heights.Insert(h);
                
                if (h < minH) minH = h;
                if (h > maxH) maxH = h;
            }
            
            if (row % progressStep == 0)
                Print(string.Format("[TDL Export] Heightmap: %1%%", (row * 100) / rows));
        }
        
        Print(string.Format("[TDL Export] Height range: %1 to %2 meters", minH, maxH));
        
        // Export formats
        if (format == 0 || format == 2)
            WriteASC(ctx, heights, cols, rows, cellSize, min);
        
        if (format == 1 || format == 2)
            WriteJSON(ctx, heights, cols, rows, cellSize, min, minH, maxH);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteASC(TDL_ExportContext ctx, array<float> heights, int cols, int rows, float cellSize, vector origin)
    {
        string path = ctx.GetOutputFile("heightmap.asc");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        // ESRI ASCII Grid header
        file.WriteLine(string.Format("ncols         %1", cols));
        file.WriteLine(string.Format("nrows         %1", rows));
        file.WriteLine(string.Format("xllcorner     %1", origin[0]));
        file.WriteLine(string.Format("yllcorner     %1", origin[2]));
        file.WriteLine(string.Format("cellsize      %1", cellSize));
        file.WriteLine("NODATA_value  -9999");
        
        // Data rows (ASC is north-to-south, so reverse row order)
        for (int row = rows - 1; row >= 0; row--)
        {
            string line = "";
            int rowOffset = row * cols;
            
            for (int col = 0; col < cols; col++)
            {
                if (col > 0) line += " ";
                line += heights[rowOffset + col].ToString();
            }
            file.WriteLine(line);
        }
        
        file.Close();
        Print("[TDL Export] Heightmap ASC -> " + path);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteJSON(TDL_ExportContext ctx, array<float> heights, int cols, int rows, float cellSize, vector origin, float minH, float maxH)
    {
        string path = ctx.GetOutputFile("heightmap.json");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine(string.Format("  \"cols\": %1,", cols));
        file.WriteLine(string.Format("  \"rows\": %1,", rows));
        file.WriteLine(string.Format("  \"cellSize\": %1,", cellSize));
        file.WriteLine(string.Format("  \"originX\": %1,", origin[0]));
        file.WriteLine(string.Format("  \"originZ\": %1,", origin[2]));
        file.WriteLine(string.Format("  \"minHeight\": %1,", minH));
        file.WriteLine(string.Format("  \"maxHeight\": %1,", maxH));
        file.WriteLine("  \"data\": [");
        
        for (int row = 0; row < rows; row++)
        {
            string line = "    [";
            int rowOffset = row * cols;
            
            for (int col = 0; col < cols; col++)
            {
                if (col > 0) line += ",";
                // Round to 2 decimal places for smaller file size
                float h = Math.Round(heights[rowOffset + col] * 100) / 100;
                line += h.ToString();
            }
            
            line += "]";
            if (row < rows - 1) line += ",";
            file.WriteLine(line);
        }
        
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Heightmap JSON -> " + path);
    }
}

// ============================================================================
// CONTOUR EXPORTER - Using marching squares algorithm
// ============================================================================
class TDL_Export_Contours
{
    protected static ref array<float> s_Heights;
    protected static int s_Cols;
    protected static int s_Rows;
    protected static float s_CellSize;
    protected static vector s_Origin;
    
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx, float cellSize, float interval, float majorInterval, float simplifyTolerance)
    {
        vector size = ctx.GetSize();
        vector min = ctx.GetMin();
        BaseWorld world = ctx.GetWorld();
        float oceanHeight = ctx.GetOceanHeight();
        
        s_CellSize = cellSize;
        s_Origin = min;
        s_Cols = Math.Ceil(size[0] / cellSize) + 1;
        s_Rows = Math.Ceil(size[2] / cellSize) + 1;
        
        Print(string.Format("[TDL Export] Building height grid for contours: %1 x %2", s_Cols, s_Rows));
        
        // Sample heights
        s_Heights = {};
        s_Heights.Reserve(s_Cols * s_Rows);
        
        float minH = float.MAX;
        float maxH = -float.MAX;
        
        for (int row = 0; row < s_Rows; row++)
        {
            float worldZ = min[2] + (row * cellSize);
            for (int col = 0; col < s_Cols; col++)
            {
                float worldX = min[0] + (col * cellSize);
                float h = world.GetSurfaceY(worldX, worldZ);
                s_Heights.Insert(h);
                if (h < minH) minH = h;
                if (h > maxH) maxH = h;
            }
        }
        
        // Calculate contour levels
        float startLevel = Math.Ceil(Math.Max(minH, oceanHeight) / interval) * interval;
        float endLevel = Math.Floor(maxH / interval) * interval;
        
        int levelCount = ((endLevel - startLevel) / interval) + 1;
        Print(string.Format("[TDL Export] Generating %1 contour levels from %2m to %3m", levelCount, startLevel, endLevel));
        
        // Generate contours
        array<ref TDL_ContourLine> contours = {};
        
        for (float level = startLevel; level <= endLevel; level += interval)
        {
            bool isMajor = Math.AbsFloat(Math.Repeat(level, majorInterval)) < 0.01;
            array<ref array<vector>> lines = TraceContourLevel(level);
            
            foreach (array<vector> linePoints : lines)
            {
                if (linePoints.Count() < 2)
                    continue;
                
                // Simplify line
                array<vector> simplified = SimplifyLine(linePoints, simplifyTolerance);
                
                if (simplified.Count() >= 2)
                {
                    TDL_ContourLine contour = new TDL_ContourLine();
                    contour.elevation = level;
                    contour.isMajor = isMajor;
                    contour.points = simplified;
                    contours.Insert(contour);
                }
            }
        }
        
        Print(string.Format("[TDL Export] Generated %1 contour lines", contours.Count()));
        
        // Write GeoJSON
        WriteGeoJSON(ctx, contours);
        
        // Cleanup
        s_Heights = null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static array<ref array<vector>> TraceContourLevel(float level)
    {
        array<ref array<vector>> lines = {};
        
        // Track which cell edges have been used
        set<string> usedEdges = new set<string>();
        
        // Scan each cell
        for (int row = 0; row < s_Rows - 1; row++)
        {
            for (int col = 0; col < s_Cols - 1; col++)
            {
                // Get corner heights
                float h00 = s_Heights[row * s_Cols + col];           // Bottom-left
                float h10 = s_Heights[row * s_Cols + col + 1];       // Bottom-right
                float h01 = s_Heights[(row + 1) * s_Cols + col];     // Top-left
                float h11 = s_Heights[(row + 1) * s_Cols + col + 1]; // Top-right
                
                // Calculate marching squares case
                int caseIndex = 0;
                if (h00 >= level) caseIndex |= 1;
                if (h10 >= level) caseIndex |= 2;
                if (h11 >= level) caseIndex |= 4;
                if (h01 >= level) caseIndex |= 8;
                
                // Skip empty or full cells
                if (caseIndex == 0 || caseIndex == 15)
                    continue;
                
                // Get cell world position
                float cellX = s_Origin[0] + col * s_CellSize;
                float cellZ = s_Origin[2] + row * s_CellSize;
                
                // Interpolate edge crossings
                array<vector> crossings = GetEdgeCrossings(caseIndex, level, h00, h10, h01, h11, cellX, cellZ);
                
                // Add line segment
                if (crossings.Count() >= 2)
                {
                    array<vector> segment = {};
                    segment.Insert(crossings[0]);
                    segment.Insert(crossings[1]);
                    lines.Insert(segment);
                }
            }
        }
        
        // Merge connected segments
        return MergeContourSegments(lines);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static array<vector> GetEdgeCrossings(int caseIndex, float level, float h00, float h10, float h01, float h11, float cellX, float cellZ)
    {
        array<vector> crossings = {};
        
        // Bottom edge (0-1)
        if (((caseIndex & 1) != 0) != ((caseIndex & 2) != 0))
        {
            float t = (level - h00) / (h10 - h00);
            crossings.Insert(Vector(cellX + t * s_CellSize, level, cellZ));
        }
        
        // Right edge (1-2)
        if (((caseIndex & 2) != 0) != ((caseIndex & 4) != 0))
        {
            float t = (level - h10) / (h11 - h10);
            crossings.Insert(Vector(cellX + s_CellSize, level, cellZ + t * s_CellSize));
        }
        
        // Top edge (2-3)
        if (((caseIndex & 4) != 0) != ((caseIndex & 8) != 0))
        {
            float t = (level - h11) / (h01 - h11);
            crossings.Insert(Vector(cellX + s_CellSize - t * s_CellSize, level, cellZ + s_CellSize));
        }
        
        // Left edge (3-0)
        if (((caseIndex & 8) != 0) != ((caseIndex & 1) != 0))
        {
            float t = (level - h01) / (h00 - h01);
            crossings.Insert(Vector(cellX, level, cellZ + s_CellSize - t * s_CellSize));
        }
        
        return crossings;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static array<ref array<vector>> MergeContourSegments(array<ref array<vector>> segments)
    {
        // Simple merge - connect segments that share endpoints
        array<ref array<vector>> merged = {};
        
        while (segments.Count() > 0)
        {
            array<vector> current = segments[0];
            segments.Remove(0);
            
            bool didMerge = true;
            while (didMerge)
            {
                didMerge = false;
                
                for (int i = segments.Count() - 1; i >= 0; i--)
                {
                    array<vector> other = segments[i];
                    float threshold = s_CellSize * 0.1;
                    
                    // Check if endpoints connect
                    if (vector.Distance(current[current.Count() - 1], other[0]) < threshold)
                    {
                        // Append other to current
                        for (int j = 1; j < other.Count(); j++)
                            current.Insert(other[j]);
                        segments.Remove(i);
                        didMerge = true;
                    }
                    else if (vector.Distance(current[current.Count() - 1], other[other.Count() - 1]) < threshold)
                    {
                        // Append reversed other to current
                        for (int j = other.Count() - 2; j >= 0; j--)
                            current.Insert(other[j]);
                        segments.Remove(i);
                        didMerge = true;
                    }
                    else if (vector.Distance(current[0], other[other.Count() - 1]) < threshold)
                    {
                        // Prepend other to current
                        for (int j = other.Count() - 2; j >= 0; j--)
                            current.InsertAt(other[j], 0);
                        segments.Remove(i);
                        didMerge = true;
                    }
                    else if (vector.Distance(current[0], other[0]) < threshold)
                    {
                        // Prepend reversed other to current
                        for (int j = 1; j < other.Count(); j++)
                            current.InsertAt(other[j], 0);
                        segments.Remove(i);
                        didMerge = true;
                    }
                }
            }
            
            merged.Insert(current);
        }
        
        return merged;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static array<vector> SimplifyLine(array<vector> points, float tolerance)
    {
        // Douglas-Peucker simplification
        if (points.Count() < 3)
            return points;
        
        // Find point with maximum distance
        float maxDist = 0;
        int maxIndex = 0;
        
        vector start = points[0];
        vector end = points[points.Count() - 1];
        
        for (int i = 1; i < points.Count() - 1; i++)
        {
            float dist = PerpendicularDistance(points[i], start, end);
            if (dist > maxDist)
            {
                maxDist = dist;
                maxIndex = i;
            }
        }
        
        // If max distance exceeds tolerance, recursively simplify
        if (maxDist > tolerance)
        {
            // Split and recurse
            array<vector> left = {};
            array<vector> right = {};
            
            for (int i = 0; i <= maxIndex; i++)
                left.Insert(points[i]);
            for (int i = maxIndex; i < points.Count(); i++)
                right.Insert(points[i]);
            
            array<vector> leftSimplified = SimplifyLine(left, tolerance);
            array<vector> rightSimplified = SimplifyLine(right, tolerance);
            
            // Combine (skip duplicate middle point)
            array<vector> result = {};
            for (int i = 0; i < leftSimplified.Count() - 1; i++)
                result.Insert(leftSimplified[i]);
            for (int i = 0; i < rightSimplified.Count(); i++)
                result.Insert(rightSimplified[i]);
            
            return result;
        }
        else
        {
            // Return just endpoints
            array<vector> result = {};
            result.Insert(start);
            result.Insert(end);
            return result;
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected static float PerpendicularDistance(vector point, vector lineStart, vector lineEnd)
    {
        vector line = lineEnd - lineStart;
        float lineLen = line.Length();
        
        if (lineLen < 0.0001)
            return vector.Distance(point, lineStart);
        
        vector toPoint = point - lineStart;
        float t = vector.Dot(toPoint, line) / (lineLen * lineLen);
        t = Math.Clamp(t, 0, 1);
        
        vector closest = lineStart + line * t;
        return vector.Distance(point, closest);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteGeoJSON(TDL_ExportContext ctx, array<ref TDL_ContourLine> contours)
    {
        string path = ctx.GetOutputFile("contours.geojson");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine("  \"type\": \"FeatureCollection\",");
        file.WriteLine("  \"features\": [");
        
        for (int i = 0; i < contours.Count(); i++)
		{
		    TDL_ContourLine c = contours[i];
		    
		    if (i > 0) file.WriteLine(",");
		    
		    file.WriteLine("    {");
		    file.WriteLine("      \"type\": \"Feature\",");
		    file.WriteLine("      \"geometry\": {");
		    file.WriteLine("        \"type\": \"LineString\",");
		    file.WriteLine("        \"coordinates\": [");
		    
		    for (int p = 0; p < c.points.Count(); p++)
		    {
		        vector pt = c.points[p];
		        string coord = string.Format("          [%1, %2]", pt[0], pt[2]);
		        if (p < c.points.Count() - 1) coord += ",";
		        file.WriteLine(coord);
		    }
		    
		    file.WriteLine("        ]");
		    file.WriteLine("      },");
		    file.WriteLine("      \"properties\": {");
		    file.WriteLine(string.Format("        \"elevation\": %1,", c.elevation));
		    
		    // No ternary in Enforce Script
		    string majorStr = "false";
		    if (c.isMajor)
		        majorStr = "true";
		    file.WriteLine(string.Format("        \"isMajor\": %1", majorStr));
		    
		    file.WriteLine("      }");
		    file.Write("    }");
		}
        
        file.WriteLine("");
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Contours -> " + path);
    }
}

class TDL_ContourLine
{
    float elevation;
    bool isMajor;
    ref array<vector> points;
}

// ============================================================================
// ROAD EXPORTER
// ============================================================================
class TDL_Export_Roads
{
    protected static ref array<ref TDL_RoadData> s_Roads;
    protected static WorldEditorAPI s_API;
    
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx)
    {
        s_Roads = {};
        s_API = ctx.GetAPI();
        
        ctx.GetWorld().QueryEntitiesByAABB(ctx.GetMin(), ctx.GetMax(), ProcessRoadEntity, FilterRoadEntity);
        
        Print(string.Format("[TDL Export] Found %1 road segments", s_Roads.Count()));
        
        WriteGeoJSON(ctx);
        
        s_Roads = null;
        s_API = null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool FilterRoadEntity(IEntity e)
    {
        return e.IsInherited(RoadEntity);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool ProcessRoadEntity(IEntity e)
    {
        BaseContainer container = s_API.EntityToSource(e);
        if (!container)
            return true;
        
        // Get spline points
        BaseContainerList splinePoints = container.GetObjectArray("SplinePoints");
        if (!splinePoints || splinePoints.Count() < 2)
            return true;
        
        vector origin = e.GetOrigin();
        
        // Get road properties
        float width = 6.0; // default
        container.Get("Width", width);
        
        ResourceName material;
        container.Get("Material", material);
        
        // Classify road type
        string roadType = "road";
        int priority = 2;
        
        if (material)
        {
            string matStr = material;
            if (matStr.Contains("Trail") || matStr.Contains("Dirt") || matStr.Contains("Gravel"))
            {
                roadType = "trail";
                priority = 1;
            }
            else if (matStr.Contains("Asphalt") || matStr.Contains("Highway"))
            {
                roadType = "highway";
                priority = 3;
            }
            else if (matStr.Contains("Concrete"))
            {
                roadType = "paved";
                priority = 2;
            }
        }
        
        // Extract points
        TDL_RoadData road = new TDL_RoadData();
        road.roadType = roadType;
        road.width = width;
        road.priority = priority;
        road.points = {};
        
        for (int i = 0; i < splinePoints.Count(); i++)
        {
            BaseContainer pointContainer = splinePoints.Get(i);
            vector position;
            pointContainer.Get("Position", position);
            road.points.Insert(origin + position);
        }
        
        s_Roads.Insert(road);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteGeoJSON(TDL_ExportContext ctx)
    {
        string path = ctx.GetOutputFile("roads.geojson");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine("  \"type\": \"FeatureCollection\",");
        file.WriteLine("  \"features\": [");
        
        for (int i = 0; i < s_Roads.Count(); i++)
        {
            TDL_RoadData road = s_Roads[i];
            
            if (i > 0) file.WriteLine(",");
            
            file.WriteLine("    {");
            file.WriteLine("      \"type\": \"Feature\",");
            file.WriteLine("      \"geometry\": {");
            file.WriteLine("        \"type\": \"LineString\",");
            file.WriteLine("        \"coordinates\": [");
            
            for (int p = 0; p < road.points.Count(); p++)
            {
                vector pt = road.points[p];
                string coord = string.Format("          [%1, %2, %3]", pt[0], pt[2], pt[1]);
                if (p < road.points.Count() - 1) coord += ",";
                file.WriteLine(coord);
            }
            
            file.WriteLine("        ]");
            file.WriteLine("      },");
            file.WriteLine("      \"properties\": {");
            file.WriteLine(string.Format("        \"type\": \"%1\",", road.roadType));
            file.WriteLine(string.Format("        \"width\": %1,", road.width));
            file.WriteLine(string.Format("        \"priority\": %1", road.priority));
            file.WriteLine("      }");
            file.Write("    }");
        }
        
        file.WriteLine("");
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Roads -> " + path);
    }
}

class TDL_RoadData
{
    string roadType;
    float width;
    int priority;
    ref array<vector> points;
}

// ============================================================================
// WATER EXPORTER
// ============================================================================
class TDL_Export_Water
{
    protected static ref array<ref TDL_WaterFeature> s_Features;
    protected static WorldEditorAPI s_API;
    protected static BaseWorld s_World;
    
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx)
    {
        s_Features = {};
        s_API = ctx.GetAPI();
        s_World = ctx.GetWorld();
        
        // Add ocean as a feature
        float oceanHeight = ctx.GetOceanHeight();
        if (oceanHeight > -1000) // Valid ocean height
        {
            TDL_WaterFeature ocean = new TDL_WaterFeature();
            ocean.featureType = "ocean";
            ocean.elevation = oceanHeight;
            ocean.points = {}; // Ocean covers everything below its height
            s_Features.Insert(ocean);
        }
        
        // Scan for rivers and lakes
        int entityCount = s_API.GetEditorEntityCount();
        
        for (int i = 0; i < entityCount; i++)
        {
            IEntitySource entitySource = s_API.GetEditorEntity(i);
            if (!entitySource)
                continue;
            
            string className = entitySource.GetClassName();
            
            // Check for lake
            if (className.Contains("Lake"))
            {
                ProcessLake(entitySource);
            }
            
            // Check for river (rivers are children of shape entities)
            IEntitySource childSource = entitySource.GetChild(0);
            if (childSource)
            {
                IEntity child = s_API.SourceToEntity(childSource);
                if (child && child.IsInherited(RiverEntity))
                {
                    ProcessRiver(entitySource, childSource);
                }
            }
        }
        
        Print(string.Format("[TDL Export] Found %1 water features", s_Features.Count()));
        
        WriteGeoJSON(ctx);
        
        s_Features = null;
        s_API = null;
        s_World = null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void ProcessLake(IEntitySource lakeSource)
    {
        IEntitySource splineSource = IEntitySource.Cast(lakeSource.GetParent());
        if (!splineSource)
            return;
        
        ShapeEntity spline = ShapeEntity.Cast(s_World.FindEntityByID(splineSource.GetID()));
        if (!spline)
            return;
        
        array<vector> outPoints = {};
        spline.GetPointsPositions(outPoints);
        
        if (outPoints.Count() < 3)
            return;
        
        vector origin = spline.GetOrigin();
        
        // Find water surface elevation
        float surfaceHeight = float.MAX;
        foreach (vector pt : outPoints)
        {
            vector worldPt = origin + pt;
            if (worldPt[1] < surfaceHeight)
                surfaceHeight = worldPt[1];
        }
        
        TDL_WaterFeature lake = new TDL_WaterFeature();
        lake.featureType = "lake";
        lake.elevation = surfaceHeight;
        lake.points = {};
        
        foreach (vector pt : outPoints)
        {
            lake.points.Insert(origin + pt);
        }
        
        // Close the polygon
        if (lake.points.Count() > 0)
            lake.points.Insert(lake.points[0]);
        
        s_Features.Insert(lake);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void ProcessRiver(IEntitySource shapeSource, IEntitySource riverSource)
    {
        ShapeEntity spline = ShapeEntity.Cast(s_API.SourceToEntity(shapeSource));
        if (!spline)
            return;
        
        BaseContainerList points = shapeSource.GetObjectArray("Points");
        if (!points || points.Count() < 2)
            return;
        
        vector origin = spline.GetOrigin();
        
        TDL_WaterFeature river = new TDL_WaterFeature();
        river.featureType = "river";
        river.elevation = 0;
        river.points = {};
        river.widths = {};
        
        for (int i = 0; i < points.Count(); i++)
        {
            BaseContainer pointContainer = points.Get(i);
            
            vector position;
            pointContainer.Get("Position", position);
            river.points.Insert(origin + position);
            
            // Get width from point data
            BaseContainerList dataList = pointContainer.GetObjectArray("Data");
            float width = 10.0; // default
            if (dataList && dataList.Count() > 0)
            {
                BaseContainer data = dataList.Get(0);
                data.Get("FixedWidth", width);
            }
            river.widths.Insert(width);
            
            // Track elevation
            if (position[1] > river.elevation)
                river.elevation = origin[1] + position[1];
        }
        
        s_Features.Insert(river);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteGeoJSON(TDL_ExportContext ctx)
    {
        string path = ctx.GetOutputFile("water.geojson");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine("  \"type\": \"FeatureCollection\",");
        file.WriteLine("  \"features\": [");
        
        int featureIndex = 0;
        
        for (int i = 0; i < s_Features.Count(); i++)
        {
            TDL_WaterFeature feat = s_Features[i];
            
            // Skip ocean in geometry (it's just metadata)
            if (feat.featureType == "ocean")
            {
                if (featureIndex > 0) file.WriteLine(",");
                
                file.WriteLine("    {");
                file.WriteLine("      \"type\": \"Feature\",");
                file.WriteLine("      \"geometry\": null,");
                file.WriteLine("      \"properties\": {");
                file.WriteLine("        \"type\": \"ocean\",");
                file.WriteLine(string.Format("        \"elevation\": %1", feat.elevation));
                file.WriteLine("      }");
                file.Write("    }");
                
                featureIndex++;
                continue;
            }
            
            if (feat.points.Count() < 2)
                continue;
            
            if (featureIndex > 0) file.WriteLine(",");
            
            file.WriteLine("    {");
            file.WriteLine("      \"type\": \"Feature\",");
            file.WriteLine("      \"geometry\": {");
            
            // Rivers are LineStrings, Lakes are Polygons
            if (feat.featureType == "river")
            {
                file.WriteLine("        \"type\": \"LineString\",");
                file.WriteLine("        \"coordinates\": [");
            }
            else
            {
                file.WriteLine("        \"type\": \"Polygon\",");
                file.WriteLine("        \"coordinates\": [[");
            }
            
            for (int p = 0; p < feat.points.Count(); p++)
            {
                vector pt = feat.points[p];
                string coord = string.Format("          [%1, %2, %3]", pt[0], pt[2], pt[1]);
                if (p < feat.points.Count() - 1) coord += ",";
                file.WriteLine(coord);
            }
            
            if (feat.featureType == "river")
                file.WriteLine("        ]");
            else
                file.WriteLine("        ]]");
            
            file.WriteLine("      },");
            file.WriteLine("      \"properties\": {");
            file.WriteLine(string.Format("        \"type\": \"%1\",", feat.featureType));
            file.WriteLine(string.Format("        \"elevation\": %1", feat.elevation));
            
            // Add widths array for rivers
            if (feat.featureType == "river" && feat.widths && feat.widths.Count() > 0)
            {
                file.WriteLine(",");
                file.Write("        \"widths\": [");
                for (int w = 0; w < feat.widths.Count(); w++)
                {
                    if (w > 0) file.Write(",");
                    file.Write(feat.widths[w].ToString());
                }
                file.WriteLine("]");
            }
            else
            {
                file.WriteLine("");
            }
            
            file.WriteLine("      }");
            file.Write("    }");
            
            featureIndex++;
        }
        
        file.WriteLine("");
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Water -> " + path);
    }
}

class TDL_WaterFeature
{
    string featureType;  // "ocean", "lake", "river"
    float elevation;
    ref array<vector> points;
    ref array<float> widths;  // For rivers - width at each point
}

// ============================================================================
// STRUCTURES EXPORTER
// ============================================================================
// ============================================================================
// STRUCTURES EXPORTER - Updated with polygon footprints
// ============================================================================
class TDL_Export_Structures
{
    protected static ref array<ref TDL_StructureData> s_Structures;
    protected static WorldEditorAPI s_API;
    
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx)
    {
        s_Structures = {};
        s_API = ctx.GetAPI();
        
        ctx.GetWorld().QueryEntitiesByAABB(ctx.GetMin(), ctx.GetMax(), ProcessEntity, FilterEntity);
        
        Print(string.Format("[TDL Export] Found %1 structures", s_Structures.Count()));
        
        WriteGeoJSON(ctx);
        
        s_Structures = null;
        s_API = null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool FilterEntity(IEntity e)
    {
        // Destructible buildings
        if (e.IsInherited(SCR_DestructibleBuildingEntity))
            return true;
        
        // Check prefab name for structure indicators
        EntityPrefabData prefabData = e.GetPrefabData();
        if (prefabData)
        {
            string name = prefabData.GetPrefabName();
            if (name.Contains("Building") || name.Contains("House") || 
                name.Contains("Barn") || name.Contains("Church") ||
                name.Contains("Tower") || name.Contains("Bunker") ||
                name.Contains("Hangar") || name.Contains("Warehouse") ||
                name.Contains("Wall") || name.Contains("Fence") ||
                name.Contains("Cover") || name.Contains("Shelter"))
            {
                return true;
            }
        }
        
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool ProcessEntity(IEntity e)
    {
        TDL_StructureData structure = new TDL_StructureData();
        
        // Get local bounding box
        vector mins, maxs;
        e.GetBounds(mins, maxs);
        
        // Create 4 corner points from bounds (in local space, Y=0 plane)
        vector p1 = Vector(mins[0], 0, mins[2]);  // Bottom-left
        vector p2 = Vector(mins[0], 0, maxs[2]);  // Top-left
        vector p3 = Vector(maxs[0], 0, maxs[2]);  // Top-right
        vector p4 = Vector(maxs[0], 0, mins[2]);  // Bottom-right
        
        // Rotate by entity yaw
        float yawRad = e.GetYawPitchRoll()[0] * -Math.DEG2RAD;
        p1 = RotatePointY(p1, yawRad);
        p2 = RotatePointY(p2, yawRad);
        p3 = RotatePointY(p3, yawRad);
        p4 = RotatePointY(p4, yawRad);
        
        // Transform to world position
        vector origin = e.GetOrigin();
        p1 = p1 + origin;
        p2 = p2 + origin;
        p3 = p3 + origin;
        p4 = p4 + origin;
        
        // Store footprint polygon
        structure.footprint = {};
        structure.footprint.Insert(p1);
        structure.footprint.Insert(p2);
        structure.footprint.Insert(p3);
        structure.footprint.Insert(p4);
        structure.footprint.Insert(p1);  // Close polygon
        
        // Store centroid and metadata
        structure.position = origin;
        structure.rotation = e.GetYawPitchRoll()[0];
        structure.height = maxs[1] - mins[1];
        
        // Get prefab info for classification
        EntityPrefabData prefabData = e.GetPrefabData();
        if (prefabData)
        {
            structure.prefabName = prefabData.GetPrefabName();
            structure.structureType = ClassifyStructure(structure.prefabName);
        }
        else
        {
            structure.prefabName = "Unknown";
            structure.structureType = "unknown";
        }
        
        s_Structures.Insert(structure);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static vector RotatePointY(vector point, float angleRad)
    {
        float cosA = Math.Cos(angleRad);
        float sinA = Math.Sin(angleRad);
        
        return Vector(
            point[0] * cosA - point[2] * sinA,
            point[1],
            point[0] * sinA + point[2] * cosA
        );
    }
    
    //------------------------------------------------------------------------------------------------
    protected static string ClassifyStructure(string prefabName)
    {
        if (prefabName.Contains("Church") || prefabName.Contains("Chapel"))
            return "religious";
        if (prefabName.Contains("Tower") || prefabName.Contains("Radar") || prefabName.Contains("Antenna"))
            return "tower";
        if (prefabName.Contains("Bunker") || prefabName.Contains("Military") || prefabName.Contains("Barracks"))
            return "military";
        if (prefabName.Contains("Industrial") || prefabName.Contains("Factory") || prefabName.Contains("Power"))
            return "industrial";
        if (prefabName.Contains("Barn") || prefabName.Contains("Farm") || prefabName.Contains("Silo"))
            return "agricultural";
        if (prefabName.Contains("Wall"))
            return "wall";
        if (prefabName.Contains("Fence"))
            return "fence";
        if (prefabName.Contains("Shelter") || prefabName.Contains("Cover"))
            return "cover";
        
        return "residential";
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteGeoJSON(TDL_ExportContext ctx)
    {
        string path = ctx.GetOutputFile("structures.geojson");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine("  \"type\": \"FeatureCollection\",");
        file.WriteLine("  \"features\": [");
        
        for (int i = 0; i < s_Structures.Count(); i++)
        {
            TDL_StructureData s = s_Structures[i];
            
            if (i > 0) file.WriteLine(",");
            
            file.WriteLine("    {");
            file.WriteLine("      \"type\": \"Feature\",");
            file.WriteLine("      \"geometry\": {");
            file.WriteLine("        \"type\": \"Polygon\",");
            file.WriteLine("        \"coordinates\": [[");
            
            // Output footprint polygon vertices
            for (int p = 0; p < s.footprint.Count(); p++)
            {
                vector pt = s.footprint[p];
                // GeoJSON: [x, z, elevation] - we use x, z (north), y (height)
                string coord = string.Format("          [%1, %2]", pt[0], pt[2]);
                if (p < s.footprint.Count() - 1) coord += ",";
                file.WriteLine(coord);
            }
            
            file.WriteLine("        ]]");
            file.WriteLine("      },");
            file.WriteLine("      \"properties\": {");
            file.WriteLine(string.Format("        \"type\": \"%1\",", s.structureType));
            file.WriteLine(string.Format("        \"height\": %1,", s.height));
            file.WriteLine(string.Format("        \"rotation\": %1,", s.rotation));
            file.WriteLine(string.Format("        \"centerX\": %1,", s.position[0]));
            file.WriteLine(string.Format("        \"centerZ\": %1,", s.position[2]));
            file.WriteLine(string.Format("        \"prefab\": \"%1\"", s.prefabName));
            file.WriteLine("      }");
            file.Write("    }");
        }
        
        file.WriteLine("");
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Structures -> " + path);
    }
}

class TDL_StructureData
{
    vector position;           // Centroid
    float rotation;            // Yaw in degrees
    float height;              // Building height
    string prefabName;
    string structureType;
    ref array<vector> footprint;  // Polygon vertices (closed)
}

// ============================================================================
// POI EXPORTER
// ============================================================================
class TDL_Export_POIs
{
    protected static ref array<ref TDL_POIData> s_POIs;
    
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx)
    {
        s_POIs = {};
        
        ctx.GetWorld().QueryEntitiesByAABB(ctx.GetMin(), ctx.GetMax(), ProcessEntity, FilterEntity);
        
        Print(string.Format("[TDL Export] Found %1 POIs", s_POIs.Count()));
        
        WriteGeoJSON(ctx);
        
        s_POIs = null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool FilterEntity(IEntity e)
    {
        return e.FindComponent(MapDescriptorComponent) != null;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool ProcessEntity(IEntity e)
    {
        MapDescriptorComponent mapDesc = MapDescriptorComponent.Cast(e.FindComponent(MapDescriptorComponent));
        if (!mapDesc)
            return true;
        
        TDL_POIData poi = new TDL_POIData();
        poi.position = e.GetOrigin();
        
        // Get map descriptor type
        EMapDescriptorType descType = mapDesc.GetBaseType();
        poi.descriptorType = typename.EnumToString(EMapDescriptorType, descType);
        
        // Try to get display name
        MapItem mapItem = mapDesc.Item();
        if (mapItem)
        {
            poi.displayName = mapItem.GetDisplayName();
        }
        
        if (poi.displayName.IsEmpty())
            poi.displayName = e.GetName();
        
        // Classify POI
        string typeStr = poi.descriptorType;
		typeStr.ToLower();
        if (typeStr.Contains("town") || typeStr.Contains("city") || typeStr.Contains("village"))
            poi.category = "settlement";
        else if (typeStr.Contains("military") || typeStr.Contains("base") || typeStr.Contains("camp"))
            poi.category = "military";
        else if (typeStr.Contains("airport") || typeStr.Contains("airfield"))
            poi.category = "airfield";
        else if (typeStr.Contains("port") || typeStr.Contains("harbor"))
            poi.category = "port";
        else if (typeStr.Contains("fuel") || typeStr.Contains("gas"))
            poi.category = "fuel";
        else if (typeStr.Contains("hospital") || typeStr.Contains("medical"))
            poi.category = "medical";
        else
            poi.category = "other";
        
        s_POIs.Insert(poi);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteGeoJSON(TDL_ExportContext ctx)
    {
        string path = ctx.GetOutputFile("pois.geojson");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine("  \"type\": \"FeatureCollection\",");
        file.WriteLine("  \"features\": [");
        
        for (int i = 0; i < s_POIs.Count(); i++)
        {
            TDL_POIData poi = s_POIs[i];
            
            if (i > 0) file.WriteLine(",");
            
            file.WriteLine("    {");
            file.WriteLine("      \"type\": \"Feature\",");
            file.WriteLine("      \"geometry\": {");
            file.WriteLine("        \"type\": \"Point\",");
            file.WriteLine(string.Format("        \"coordinates\": [%1, %2, %3]", poi.position[0], poi.position[2], poi.position[1]));
            file.WriteLine("      },");
            file.WriteLine("      \"properties\": {");
            file.WriteLine(string.Format("        \"name\": \"%1\",", poi.displayName));
            file.WriteLine(string.Format("        \"category\": \"%1\",", poi.category));
            file.WriteLine(string.Format("        \"type\": \"%1\"", poi.descriptorType));
            file.WriteLine("      }");
            file.Write("    }");
        }
        
        file.WriteLine("");
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] POIs -> " + path);
    }
}

class TDL_POIData
{
    vector position;
    string displayName;
    string descriptorType;
    string category;
}

// ============================================================================
// VEGETATION DENSITY EXPORTER
// ============================================================================
class TDL_Export_Vegetation
{
    protected static float s_QueryRadius;
    protected static vector s_QueryPos;
    protected static float s_TreeDensity;
    
    //------------------------------------------------------------------------------------------------
    static void Export(TDL_ExportContext ctx, float cellSize, float queryRadius)
    {
        s_QueryRadius = queryRadius;
        
        vector size = ctx.GetSize();
        vector min = ctx.GetMin();
        BaseWorld world = ctx.GetWorld();
        
        int cols = Math.Ceil(size[0] / cellSize);
        int rows = Math.Ceil(size[2] / cellSize);
        
        Print(string.Format("[TDL Export] Sampling vegetation: %1 x %2 cells", cols, rows));
        
        array<float> density = {};
        density.Reserve(cols * rows);
        
        float maxDensity = 0;
        
        int progressStep = rows / 10;
        if (progressStep < 1) progressStep = 1;
        
        for (int row = 0; row < rows; row++)
        {
            float worldZ = min[2] + (row + 0.5) * cellSize;
            
            for (int col = 0; col < cols; col++)
            {
                float worldX = min[0] + (col + 0.5) * cellSize;
                float height = world.GetSurfaceY(worldX, worldZ);
                
                // Query for trees
                s_TreeDensity = 0;
                s_QueryPos = Vector(worldX, height, worldZ);
                
                world.QueryEntitiesBySphere(s_QueryPos, queryRadius, CountVegetation);
                
                density.Insert(s_TreeDensity);
                if (s_TreeDensity > maxDensity)
                    maxDensity = s_TreeDensity;
            }
            
            if (row % progressStep == 0)
                Print(string.Format("[TDL Export] Vegetation: %1%%", (row * 100) / rows));
        }
        
        // Normalize and write
        WriteJSON(ctx, density, cols, rows, cellSize, min, maxDensity);
    }
    
    //------------------------------------------------------------------------------------------------
    protected static bool CountVegetation(IEntity e)
    {
        if (!e.IsInherited(Tree))
            return true;
        
        float dist = vector.Distance(s_QueryPos, e.GetOrigin());
        if (dist < s_QueryRadius)
        {
            // Weight by distance - closer trees contribute more
            float weight = 1.0 - (dist / s_QueryRadius);
            s_TreeDensity += weight;
        }
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected static void WriteJSON(TDL_ExportContext ctx, array<float> density, int cols, int rows, float cellSize, vector origin, float maxDensity)
    {
        string path = ctx.GetOutputFile("vegetation.json");
        FileHandle file = FileIO.OpenFile(path, FileMode.WRITE);
        if (!file) return;
        
        file.WriteLine("{");
        file.WriteLine(string.Format("  \"cols\": %1,", cols));
        file.WriteLine(string.Format("  \"rows\": %1,", rows));
        file.WriteLine(string.Format("  \"cellSize\": %1,", cellSize));
        file.WriteLine(string.Format("  \"originX\": %1,", origin[0]));
        file.WriteLine(string.Format("  \"originZ\": %1,", origin[2]));
        file.WriteLine(string.Format("  \"maxDensity\": %1,", maxDensity));
        file.WriteLine("  \"data\": [");
        
        for (int row = 0; row < rows; row++)
        {
            string line = "    [";
            int rowOffset = row * cols;
            
            for (int col = 0; col < cols; col++)
            {
                if (col > 0) line += ",";
                // Normalize to 0-1 range
                float normalized = 0;
                if (maxDensity > 0)
                    normalized = Math.Round(density[rowOffset + col] / maxDensity * 100) / 100;
                line += normalized.ToString();
            }
            
            line += "]";
            if (row < rows - 1) line += ",";
            file.WriteLine(line);
        }
        
        file.WriteLine("  ]");
        file.WriteLine("}");
        
        file.Close();
        Print("[TDL Export] Vegetation -> " + path);
    }
}

#endif