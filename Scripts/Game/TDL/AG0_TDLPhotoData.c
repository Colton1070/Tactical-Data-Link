//------------------------------------------------------------------------------------------------
// AG0_TDLPhotoData.c
// Canvas-based image rendering using draw commands
// Renders palette-indexed images as batched colored quads
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// Base64 decoder - lookup table for performance
//------------------------------------------------------------------------------------------------
class AG0_Base64
{
    protected static ref array<int> s_Lookup;
    
    static void InitLookup()
    {
        if (s_Lookup)
            return;
        
        s_Lookup = {};
        s_Lookup.Resize(128);
        
        string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++)
            s_Lookup[chars.ToAscii(i)] = i;
    }
    
    static array<int> Decode(string input)
    {
        InitLookup();
        
        array<int> output = {};
        int len = input.Length();
        output.Reserve((len * 3) / 4);
        
        for (int i = 0; i < len; i += 4)
        {
            int a = s_Lookup[input.ToAscii(i)];
            int b = s_Lookup[input.ToAscii(i + 1)];
            int c = s_Lookup[input.ToAscii(i + 2)];
            int d = s_Lookup[input.ToAscii(i + 3)];
            
            output.Insert((a << 2) | (b >> 4));
            if (input.ToAscii(i + 2) != 61)  // '=' is ASCII 61
                output.Insert(((b & 0xF) << 4) | (c >> 2));
            if (input.ToAscii(i + 3) != 61)
                output.Insert(((c & 0x3) << 6) | d);
        }
        return output;
    }
}

//------------------------------------------------------------------------------------------------
// REST callback for image API
//------------------------------------------------------------------------------------------------
//class AG0_TDLImageCallback : RestCallback
//{
//    protected AG0_TDLPhotoComponent m_Component;
//    
//    void AG0_TDLImageCallback(AG0_TDLPhotoComponent comp)
//    {
//        m_Component = comp;
//    }
//    
//    override void OnSuccess(string data, int dataSize)
//    {
//        Print(string.Format("[TDLImage] Received %1 bytes", dataSize), LogLevel.NORMAL);
//        
//        if (!m_Component)
//            return;
//        
//        m_Component.OnImageDataReceived(data);
//    }
//    
//    override void OnError(int errorCode)
//    {
//        Print(string.Format("[TDLImage] Request failed: %1", errorCode), LogLevel.ERROR);
//    }
//    
//    override void OnTimeout()
//    {
//        Print("[TDLImage] Request timed out", LogLevel.ERROR);
//    }
//}

//------------------------------------------------------------------------------------------------
// Serializable image data - designed for network transfer
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoData
{
    int m_iWidth;
    int m_iHeight;
    ref array<int> m_aPalette;      // ARGB colors
    ref array<int> m_aPixels;       // Indices into palette
    
    void AG0_TDLPhotoData()
    {
        m_aPalette = {};
        m_aPixels = {};
    }
    
    //------------------------------------------------------------------------------------------------
    int GetSerializedSize()
    {
        return 8 + (m_aPalette.Count() * 4) + (m_aPixels.Count() * 4);
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDLPhotoData CreateTestGradient(int size = 64)
    {
        AG0_TDLPhotoData data = new AG0_TDLPhotoData();
        data.m_iWidth = size;
        data.m_iHeight = size;
        
        // 16-color rainbow palette
        data.m_aPalette = {
            0xFFFF0000, 0xFFFF4000, 0xFFFF8000, 0xFFFFBF00,
            0xFFFFFF00, 0xFFBFFF00, 0xFF00FF00, 0xFF00FFBF,
            0xFF00FFFF, 0xFF00BFFF, 0xFF0080FF, 0xFF0000FF,
            0xFF8000FF, 0xFFBF00FF, 0xFFFF00FF, 0xFFFF0080
        };
        
        data.m_aPixels.Reserve(size * size);
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                int idx = ((x + y) * 16 / (size * 2)) % 16;
                data.m_aPixels.Insert(idx);
            }
        }
        
        return data;
    }
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDLPhotoData CreateTestCheckerboard(int size = 64, int cellSize = 8)
    {
        AG0_TDLPhotoData data = new AG0_TDLPhotoData();
        data.m_iWidth = size;
        data.m_iHeight = size;
        
        data.m_aPalette = { 0xFF202020, 0xFFE0E0E0 };
        
        data.m_aPixels.Reserve(size * size);
        for (int y = 0; y < size; y++)
        {
            for (int x = 0; x < size; x++)
            {
                int idx = ((x / cellSize) + (y / cellSize)) % 2;
                data.m_aPixels.Insert(idx);
            }
        }
        
        return data;
    }
}

//------------------------------------------------------------------------------------------------
// Renders AG0_TDLPhotoData to a CanvasWidget using batched draw commands
//------------------------------------------------------------------------------------------------
class AG0_TDLPhotoRenderer
{
    protected CanvasWidget m_wCanvas;
    protected ref AG0_TDLPhotoData m_PhotoData;
    protected ref array<ref CanvasWidgetCommand> m_aDrawCommands = {};
    
    protected float m_fCanvasWidth;
    protected float m_fCanvasHeight;
    protected float m_fPixelSize;
    protected float m_fOffsetX;
    protected float m_fOffsetY;
    
    protected int m_iCommandCount;
    protected int m_iVertexCount;
    
    //------------------------------------------------------------------------------------------------
    bool Init(CanvasWidget canvas)
    {
        if (!canvas)
            return false;
        
        m_wCanvas = canvas;
        vector size = m_wCanvas.GetSizeInUnits();
        m_fCanvasWidth = size[0];
        m_fCanvasHeight = size[1];
        
        Print(string.Format("[TDLPhotoRenderer] Canvas size: %1x%2", m_fCanvasWidth, m_fCanvasHeight), LogLevel.DEBUG);
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetPhotoData(AG0_TDLPhotoData data)
    {
        m_PhotoData = data;
        if (!data)
            return;
        
        float scaleX = m_fCanvasWidth / data.m_iWidth;
        float scaleY = m_fCanvasHeight / data.m_iHeight;
        m_fPixelSize = Math.Min(scaleX, scaleY);
        
        float imageWidth = data.m_iWidth * m_fPixelSize;
        float imageHeight = data.m_iHeight * m_fPixelSize;
        m_fOffsetX = (m_fCanvasWidth - imageWidth) * 0.5;
        m_fOffsetY = (m_fCanvasHeight - imageHeight) * 0.5;
        
        Print(string.Format("[TDLPhotoRenderer] Photo %1x%2, %3 colors, pixel size: %4", 
            data.m_iWidth, data.m_iHeight, data.m_aPalette.Count(), m_fPixelSize), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    void Draw()
    {
        if (!m_wCanvas || !m_PhotoData)
            return;
        
        m_aDrawCommands.Clear();
        m_iCommandCount = 0;
        m_iVertexCount = 0;
        
        vector size = m_wCanvas.GetSizeInUnits();
        m_fCanvasWidth = size[0];
        m_fCanvasHeight = size[1];
        
        DrawBatchedByColor();
        m_wCanvas.SetDrawCommands(m_aDrawCommands);
        
        Print(string.Format("[TDLPhotoRenderer] %1 commands, %2 vertices", m_iCommandCount, m_iVertexCount), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void DrawBatchedByColor()
    {
        int width = m_PhotoData.m_iWidth;
        int height = m_PhotoData.m_iHeight;
        int paletteSize = m_PhotoData.m_aPalette.Count();
        
        for (int colorIdx = 0; colorIdx < paletteSize; colorIdx++)
        {
            int color = m_PhotoData.m_aPalette[colorIdx];
            ref array<float> verts = {};
            ref array<int> indices = {};
            int quadCount = 0;
            
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    int pixelIdx = y * width + x;
                    if (m_PhotoData.m_aPixels[pixelIdx] != colorIdx)
                        continue;
                    
                    float px = m_fOffsetX + x * m_fPixelSize;
                    float py = m_fOffsetY + y * m_fPixelSize;
                    
                    int baseVert = quadCount * 4;
                    
                    // Quad verts: TL, TR, BR, BL
                    verts.Insert(px);
                    verts.Insert(py);
                    verts.Insert(px + m_fPixelSize);
                    verts.Insert(py);
                    verts.Insert(px + m_fPixelSize);
                    verts.Insert(py + m_fPixelSize);
                    verts.Insert(px);
                    verts.Insert(py + m_fPixelSize);
                    
                    // Two triangles: TL-TR-BR, TL-BR-BL
                    indices.Insert(baseVert + 0);
                    indices.Insert(baseVert + 1);
                    indices.Insert(baseVert + 2);
                    indices.Insert(baseVert + 0);
                    indices.Insert(baseVert + 2);
                    indices.Insert(baseVert + 3);
                    
                    quadCount++;
                    
                    // Split at 399 quads (2394 indices, under 2400 limit)
                    if (quadCount >= 399)
                    {
                        EmitTriMeshCommand(color, verts, indices);
                        verts = {};
                        indices = {};
                        quadCount = 0;
                    }
                }
            }
            
            if (verts.Count() > 0)
                EmitTriMeshCommand(color, verts, indices);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void EmitTriMeshCommand(int color, array<float> verts, array<int> indices)
    {
        if (verts.Count() < 8)
            return;
        
        TriMeshDrawCommand cmd = new TriMeshDrawCommand();
        cmd.m_iColor = color;
        cmd.m_Vertices = verts;
        cmd.m_Indices = indices;
        
        m_aDrawCommands.Insert(cmd);
        m_iCommandCount++;
        m_iVertexCount += verts.Count() / 2;
    }
    
    //------------------------------------------------------------------------------------------------
    int GetCommandCount() { return m_iCommandCount; }
    int GetVertexCount() { return m_iVertexCount; }
}

//------------------------------------------------------------------------------------------------
//class AG0_TDLPhotoComponentClass : ScriptComponentClass
//{
//}
//
//class AG0_TDLPhotoComponent : ScriptComponent
//{
//    [Attribute("{CA6955E6BE6A035B}UI/layouts/PhotoDisplay.layout", UIWidgets.ResourceNamePicker, "HUD layout", "layout")]
//    protected ResourceName m_PhotoHUDLayout;
//    
//    [Attribute("64", UIWidgets.Slider, "Test image size", "8 512 8")]
//    protected int m_iTestSize;
//    
//    // API config
//    protected static const string API_BASE = "https://tdl.blufor.info/api/image";
//    
//    protected ref Widget m_wHUDContainer;
//    protected CanvasWidget m_wCanvas;
//    protected ref AG0_TDLPhotoRenderer m_Renderer;
//    protected ref AG0_TDLPhotoData m_CurrentPhoto;
//    protected ref AG0_TDLImageCallback m_Callback;
//    protected bool m_bSetupComplete;
//    protected int m_iTestMode;
//    
//    //------------------------------------------------------------------------------------------------
//    override void OnPostInit(IEntity owner)
//    {
//        super.OnPostInit(owner);
//        GetGame().GetCallqueue().CallLater(SetupHUD, 500, false);
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    protected void SetupHUD()
//    {
//        WorkspaceWidget workspace = GetGame().GetWorkspace();
//        if (!workspace)
//            return;
//        
//        m_wHUDContainer = workspace.CreateWidgets(m_PhotoHUDLayout);
//        if (!m_wHUDContainer)
//            return;
//        
//        m_wCanvas = CanvasWidget.Cast(m_wHUDContainer.FindAnyWidget("PhotoImage"));
//        if (!m_wCanvas)
//        {
//            Print("[TDLPhotoComponent] PhotoImage not found or not CanvasWidget!", LogLevel.ERROR);
//            return;
//        }
//        
//        m_Renderer = new AG0_TDLPhotoRenderer();
//        if (!m_Renderer.Init(m_wCanvas))
//        {
//            Print("[TDLPhotoComponent] Renderer init failed!", LogLevel.ERROR);
//            return;
//        }
//        
//        m_Callback = new AG0_TDLImageCallback(this);
//        
//        m_bSetupComplete = true;
//        Print("[TDLPhotoComponent] Setup complete", LogLevel.NORMAL);
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    // Fetch image from API - test endpoint returns generated pattern
//    void FetchTestImage(int size = 64, int colors = 16)
//    {
//        if (!m_bSetupComplete)
//            return;
//        
//        string url = string.Format("%1/test?s=%2&c=%3", API_BASE, size, colors);
//        Print(string.Format("[TDLPhotoComponent] Fetching: %1", url), LogLevel.NORMAL);
//        
//        RestContext ctx = GetGame().GetRestApi().GetContext(url);
//        ctx.GET(m_Callback, "");
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    // Fetch and convert a real image URL
//    void FetchImage(string imageUrl, int size = 256, int colors = 64)
//    {
//        if (!m_bSetupComplete)
//            return;
//        
//        // URL encode the image URL for query param
//        string url = string.Format("%1?url=%2&s=%3&c=%4", API_BASE, imageUrl, size, colors);
//        Print(string.Format("[TDLPhotoComponent] Fetching: %1", url), LogLevel.NORMAL);
//        
//        RestContext ctx = GetGame().GetRestApi().GetContext(url);
//        ctx.GET(m_Callback, "");
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    // Called by REST callback when data arrives
//    void OnImageDataReceived(string jsonData)
//    {
//        Print(string.Format("[TDLPhotoComponent] Parsing JSON, length: %1", jsonData.Length()), LogLevel.NORMAL);
//        
//        // Parse JSON: {"w":64,"h":64,"p":[...],"d":"base64..."}
//        SCR_JsonLoadContext json = new SCR_JsonLoadContext();
//        if (!json.ImportFromString(jsonData))
//        {
//            Print("[TDLPhotoComponent] JSON parse failed!", LogLevel.ERROR);
//            return;
//        }
//        
//        AG0_TDLPhotoData photo = new AG0_TDLPhotoData();
//        
//        if (!json.ReadValue("w", photo.m_iWidth))
//        {
//            Print("[TDLPhotoComponent] Missing 'w' field", LogLevel.ERROR);
//            return;
//        }
//        
//        if (!json.ReadValue("h", photo.m_iHeight))
//        {
//            Print("[TDLPhotoComponent] Missing 'h' field", LogLevel.ERROR);
//            return;
//        }
//        
//        if (!json.ReadValue("p", photo.m_aPalette))
//        {
//            Print("[TDLPhotoComponent] Missing 'p' field", LogLevel.ERROR);
//            return;
//        }
//        
//        string pixelsB64;
//        if (!json.ReadValue("d", pixelsB64))
//        {
//            Print("[TDLPhotoComponent] Missing 'd' field", LogLevel.ERROR);
//            return;
//        }
//        
//        Print(string.Format("[TDLPhotoComponent] Decoding base64, length: %1", pixelsB64.Length()), LogLevel.NORMAL);
//        
//        photo.m_aPixels = AG0_Base64.Decode(pixelsB64);
//        
//        Print(string.Format("[TDLPhotoComponent] Decoded %1 pixels, expected %2", 
//            photo.m_aPixels.Count(), photo.m_iWidth * photo.m_iHeight), LogLevel.NORMAL);
//        
//        SetPhoto(photo);
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    // Test with a hardcoded JSON string (for offline testing)
//    void TestWithJSON(string jsonData)
//    {
//        if (!m_bSetupComplete)
//            return;
//        
//        OnImageDataReceived(jsonData);
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    // Local gradient test (no network)
//    void RunLocalTest()
//    {
//        if (!m_bSetupComplete)
//            return;
//        
//        if (m_iTestMode == 0)
//        {
//            Print(string.Format("[TDLPhotoComponent] Gradient test %1x%1", m_iTestSize), LogLevel.NORMAL);
//            m_CurrentPhoto = AG0_TDLPhotoData.CreateTestGradient(m_iTestSize);
//        }
//        else
//        {
//            Print(string.Format("[TDLPhotoComponent] Checkerboard test %1x%1", m_iTestSize), LogLevel.NORMAL);
//            m_CurrentPhoto = AG0_TDLPhotoData.CreateTestCheckerboard(m_iTestSize, 8);
//        }
//        
//        m_Renderer.SetPhotoData(m_CurrentPhoto);
//        m_Renderer.Draw();
//        
//        Print(string.Format("[TDLPhotoComponent] %1 commands, %2 verts, ~%3 bytes",
//            m_Renderer.GetCommandCount(), m_Renderer.GetVertexCount(), 
//            m_CurrentPhoto.GetSerializedSize()), LogLevel.NORMAL);
//        
//        m_iTestMode = (m_iTestMode + 1) % 2;
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    void SetPhoto(AG0_TDLPhotoData data)
//    {
//        if (!m_bSetupComplete)
//            return;
//        
//        m_CurrentPhoto = data;
//        m_Renderer.SetPhotoData(data);
//        m_Renderer.Draw();
//        
//        Print(string.Format("[TDLPhotoComponent] Rendered: %1 commands, %2 verts",
//            m_Renderer.GetCommandCount(), m_Renderer.GetVertexCount()), LogLevel.NORMAL);
//    }
//    
//    //------------------------------------------------------------------------------------------------
//    override void OnDelete(IEntity owner)
//    {
//        if (m_wHUDContainer)
//        {
//            m_wHUDContainer.RemoveFromHierarchy();
//            m_wHUDContainer = null;
//        }
//        super.OnDelete(owner);
//    }
//}

//------------------------------------------------------------------------------------------------
//class AG0_TDLPhotoAction : ScriptedUserAction
//{
//    protected int m_iMode = 0;
//    
//    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
//    {
//        AG0_TDLPhotoComponent photoComp = AG0_TDLPhotoComponent.Cast(
//            pOwnerEntity.FindComponent(AG0_TDLPhotoComponent));
//        
//        if (!photoComp)
//            return;
//        
//        // Cycle through test modes
//        switch (m_iMode)
//        {
//            case 0:
//                // Local generated test
//                photoComp.RunLocalTest();
//                break;
//            case 1:
//                // Fetch from test API endpoint
//                photoComp.FetchTestImage(64, 16);
//                break;
//            case 2:
//                // Fetch larger test
//                photoComp.FetchImage("https://www.insidehook.com/wp-content/uploads/2017/10/blade-runner-2049-1005-1.jpg", 256, 256);
//                break;
//        }
//        
//        m_iMode = (m_iMode + 1) % 3;
//    }
//    
//    override bool CanBePerformedScript(IEntity user)
//    {
//        return true;
//    }
//    
//    override bool GetActionNameScript(out string outName)
//    {
//        outName = "Test Photo Render";
//        return true;
//    }
//}