//------------------------------------------------------------------------------------------------
// AG0_MapSatelliteConfig.c
// Map satellite texture configuration with overlay layer support
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
//! Individual overlay layer entry
//! Defines a single texture overlay with draw order and optional opacity
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sLayerName")]
class AG0_MapOverlayEntry
{
    [Attribute("", UIWidgets.EditBox, "Layer name (for UI toggle labels)")]
    string m_sLayerName;
    
    [Attribute("", UIWidgets.ResourceNamePicker, "Overlay texture (transparent PNG imported as .edds)", params: "edds")]
    ResourceName m_OverlayTexture;
    
    [Attribute("0", UIWidgets.Slider, "Draw order (lower = drawn first, behind higher layers)", params: "0 100 1")]
    int m_iDrawOrder;
    
    [Attribute("1.0", UIWidgets.Slider, "Opacity multiplier (0.0 = invisible, 1.0 = full)", params: "0 1 0.05")]
    float m_fOpacity;
    
    [Attribute("1", UIWidgets.CheckBox, "Enabled by default")]
    bool m_bEnabledByDefault;
}

//------------------------------------------------------------------------------------------------
//! Individual map satellite texture entry
//! Associates a world file identifier with its satellite imagery path and optional overlays
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sWorldIdentifier")]
class AG0_MapSatelliteEntry
{
    [Attribute("", UIWidgets.EditBox, "World file identifier (partial match against GetGame().GetWorldFile())")]
    string m_sWorldIdentifier;
    
    [Attribute("", UIWidgets.ResourceNamePicker, "Satellite background image texture", params: "edds")]
    ResourceName m_SatelliteTexture;
    
    [Attribute("", UIWidgets.Object, "Overlay layers (structures, roads, water, contours, etc.)")]
    ref array<ref AG0_MapOverlayEntry> m_aOverlays;
}

//------------------------------------------------------------------------------------------------
//! Master configuration for map satellite textures
//! Provides fallback texture lookup for maps that don't use prefab-based MapEntity configuration
[BaseContainerProps(configRoot: true)]
class AG0_MapSatelliteConfig
{
    [Attribute("", UIWidgets.Object, "Map satellite texture entries")]
    ref array<ref AG0_MapSatelliteEntry> m_aMapEntries;
    
    //------------------------------------------------------------------------------------------------
    //! Find the full map entry for current world
    //! @param worldFile - The world file path from GetGame().GetWorldFile()
    //! @return The matching entry, or null if not found
    AG0_MapSatelliteEntry GetMapEntry(string worldFile)
    {
        if (!m_aMapEntries)
            return null;
        
        foreach (AG0_MapSatelliteEntry entry : m_aMapEntries)
        {
            if (!entry || entry.m_sWorldIdentifier.IsEmpty())
                continue;
            
            if (worldFile.Contains(entry.m_sWorldIdentifier))
                return entry;
        }
        
        return null;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Find satellite texture for current world (backward compatible)
    //! @param worldFile - The world file path from GetGame().GetWorldFile()
    //! @return ResourceName of satellite texture, or empty if not found
    ResourceName GetSatelliteTexture(string worldFile)
    {
        AG0_MapSatelliteEntry entry = GetMapEntry(worldFile);
        if (entry)
            return entry.m_SatelliteTexture;
        
        return ResourceName.Empty;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Debug: Print all configured maps
    void DebugPrintEntries()
    {
        if (!m_aMapEntries)
        {
            Print("[MapSatelliteConfig] No entries configured", LogLevel.DEBUG);
            return;
        }
        
        Print(string.Format("[MapSatelliteConfig] %1 entries configured:", m_aMapEntries.Count()), LogLevel.DEBUG);
        foreach (int i, AG0_MapSatelliteEntry entry : m_aMapEntries)
        {
            if (!entry)
                continue;
            
            int overlayCount = 0;
            if (entry.m_aOverlays)
                overlayCount = entry.m_aOverlays.Count();
            
            Print(string.Format("  [%1] %2 -> %3 (%4 overlays)", 
                i, entry.m_sWorldIdentifier, entry.m_SatelliteTexture, overlayCount), LogLevel.DEBUG);
        }
    }
}

//------------------------------------------------------------------------------------------------
//! Static helper class for loading and caching the map satellite config
class AG0_MapSatelliteConfigHelper
{
    // Default config path - adjust to your mod's structure
    protected static const ResourceName DEFAULT_CONFIG_PATH = "{140BDA7A9C54113A}Configs/TDL/TDL_MapSatelliteConfig.conf";
    
    protected static ref AG0_MapSatelliteConfig s_CachedConfig;
    protected static bool s_bConfigLoaded;
    
    //------------------------------------------------------------------------------------------------
    //! Get the cached config, loading it if necessary
    static AG0_MapSatelliteConfig GetConfig(ResourceName configPath = "")
    {
        if (s_bConfigLoaded)
            return s_CachedConfig;
        
        ResourceName pathToLoad = configPath;
        if (pathToLoad.IsEmpty())
            pathToLoad = DEFAULT_CONFIG_PATH;
        
        Resource resource = BaseContainerTools.LoadContainer(pathToLoad);
        if (!resource || !resource.IsValid())
        {
            Print(string.Format("[MapSatelliteConfigHelper] Failed to load config: %1", pathToLoad), LogLevel.ERROR);
            s_bConfigLoaded = true;
            return null;
        }
        
        BaseContainer container = resource.GetResource().ToBaseContainer();
        if (!container)
        {
            Print("[MapSatelliteConfigHelper] Failed to get container from resource", LogLevel.ERROR);
            s_bConfigLoaded = true;
            return null;
        }
        
        s_CachedConfig = AG0_MapSatelliteConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(container));
        s_bConfigLoaded = true;
        
        if (s_CachedConfig && s_CachedConfig.m_aMapEntries)
            Print(string.Format("[MapSatelliteConfigHelper] Loaded config with %1 map entries", s_CachedConfig.m_aMapEntries.Count()), LogLevel.DEBUG);
        
        return s_CachedConfig;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Convenience method to get satellite texture for current world
    static ResourceName GetSatelliteTextureForCurrentWorld(ResourceName configPath = "")
    {
        AG0_MapSatelliteConfig config = GetConfig(configPath);
        if (!config)
            return ResourceName.Empty;
        
        string worldFile = GetGame().GetWorldFile();
        return config.GetSatelliteTexture(worldFile);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get the full map entry for current world (includes overlays)
    static AG0_MapSatelliteEntry GetMapEntryForCurrentWorld(ResourceName configPath = "")
    {
        AG0_MapSatelliteConfig config = GetConfig(configPath);
        if (!config)
            return null;
        
        string worldFile = GetGame().GetWorldFile();
        return config.GetMapEntry(worldFile);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Force reload of config
    static void ReloadConfig()
    {
        s_CachedConfig = null;
        s_bConfigLoaded = false;
    }
}