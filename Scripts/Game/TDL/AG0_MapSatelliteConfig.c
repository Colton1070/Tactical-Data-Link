//------------------------------------------------------------------------------------------------
//! Individual map satellite texture entry
//! Associates a world file identifier with its satellite imagery path
[BaseContainerProps(), SCR_BaseContainerCustomTitleField("m_sWorldIdentifier")]
class AG0_MapSatelliteEntry
{
    [Attribute("", UIWidgets.EditBox, "World file identifier (partial match against GetGame().GetWorldFile())")]
    string m_sWorldIdentifier;
    
    [Attribute("", UIWidgets.ResourceNamePicker, "Satellite background image texture", params: "edds")]
    ResourceName m_SatelliteTexture;
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
    //! Find satellite texture for current world
    //! @param worldFile - The world file path from GetGame().GetWorldFile()
    //! @return ResourceName of satellite texture, or empty if not found
    ResourceName GetSatelliteTexture(string worldFile)
    {
        if (!m_aMapEntries)
            return ResourceName.Empty;
        
        foreach (AG0_MapSatelliteEntry entry : m_aMapEntries)
        {
            if (!entry || entry.m_sWorldIdentifier.IsEmpty())
                continue;
            
            // Partial match - allows "Cain" to match "worlds/Cain/Cain.ent"
            if (worldFile.Contains(entry.m_sWorldIdentifier))
                return entry.m_SatelliteTexture;
        }
        
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
            if (entry)
                Print(string.Format("  [%1] %2 -> %3", i, entry.m_sWorldIdentifier, entry.m_SatelliteTexture), LogLevel.DEBUG);
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
    //! @param configPath - Optional custom config path, uses default if empty
    //! @return The map satellite config, or null if loading failed
    static AG0_MapSatelliteConfig GetConfig(ResourceName configPath = "")
    {
        // Return cached if already loaded
        if (s_bConfigLoaded)
            return s_CachedConfig;
        
        ResourceName pathToLoad = configPath;
        if (pathToLoad.IsEmpty())
            pathToLoad = DEFAULT_CONFIG_PATH;
        
        // Load config
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
    //! @param configPath - Optional custom config path
    //! @return ResourceName of satellite texture, or empty if not found
    static ResourceName GetSatelliteTextureForCurrentWorld(ResourceName configPath = "")
    {
        AG0_MapSatelliteConfig config = GetConfig(configPath);
        if (!config)
            return ResourceName.Empty;
        
        string worldFile = GetGame().GetWorldFile();
        return config.GetSatelliteTexture(worldFile);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Force reload of config (useful for development/debugging)
    static void ReloadConfig()
    {
        s_CachedConfig = null;
        s_bConfigLoaded = false;
    }
}