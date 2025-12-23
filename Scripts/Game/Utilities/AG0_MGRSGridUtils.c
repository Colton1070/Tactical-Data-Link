//------------------------------------------------------------------------------------------------
//! MGRS Grid Reference Utility
//! Provides full Military Grid Reference System coordinates including Grid Zone Designator
//! 
//! MGRS Format: [GZD] [100km Square] [Easting] [Northing]
//! Example: "33T WN 01234 05678"
//!   - 33T = Grid Zone Designator (UTM zone 33, latitude band T)
//!   - WN = 100,000m square identifier (calculated from map's lat/long)
//!   - 01234 05678 = Grid coordinates calculated from world position
//!
//! NOTE: Grid numbers are calculated directly from world position to match
//! what players see on the in-game map. GZD and 100km square are derived
//! from the map's configured latitude/longitude.
//------------------------------------------------------------------------------------------------

class AG0_MGRSGridUtils
{
    // Latitude band letters (C-X, omitting I and O)
    protected static const string LATITUDE_BANDS = "CDEFGHJKLMNPQRSTUVWX";
    
    // Column letters for 100km squares (A-Z minus I and O = 24 letters)
    protected static const string MGRS_COL_LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    
    // Row letters for 100km squares (A-V minus I and O = 20 letters)
    protected static const string MGRS_ROW_LETTERS = "ABCDEFGHJKLMNPQRSTUV";
    
    // Cached GZD
    protected static string s_CachedGZD;
    protected static float s_CachedLatitude;
    protected static float s_CachedLongitude;
    protected static bool s_bGZDValid = false;
    
    // Cached 100km square identifier (static for entire map)
    protected static string s_Cached100kmSquare;
    protected static bool s_b100kmSquareValid = false;
    
    // Cached UTM data
    protected static int s_CachedUTMZone;
    protected static float s_CachedBaseEasting;
    protected static float s_CachedBaseNorthing;
    
    //------------------------------------------------------------------------------------------------
    //! Get the full MGRS coordinate string for a world position
    //! @param worldPos World position vector
    //! @param numDigits Number of digits for easting/northing (4 = 10m, 5 = 1m)
    //! @return Full MGRS string like "33T WN 0107 0285"
    static string GetFullMGRS(vector worldPos, int numDigits = 4)
    {
        string gzd = GetGridZoneDesignator();
        string squareId = Get100kmSquareId();
        string gridRef = GetGridReference(worldPos, numDigits);
        
        if (gzd.IsEmpty() && squareId.IsEmpty())
            return gridRef;
        
        if (gzd.IsEmpty())
            return string.Format("%1 %2", squareId, gridRef);
        
        if (squareId.IsEmpty())
            return string.Format("%1 %2", gzd, gridRef);
        
        return string.Format("%1 %2 %3", gzd, squareId, gridRef);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get grid reference calculated directly from world position
    //! This matches exactly what the game map shows
    //! @param worldPos World position vector
    //! @param numDigits Number of digits (4 = 10m precision, 5 = 1m precision)
    static string GetGridReference(vector worldPos, int numDigits = 4)
    {
        int gridEasting;
        int gridNorthing;
        
        // Calculate based on precision requested
        if (numDigits == 5)
        {
            // 1m precision - 5 digits (00000-99999)
            gridEasting = Math.AbsInt(worldPos[0]) % 100000;
            gridNorthing = Math.AbsInt(worldPos[2]) % 100000;
        }
        else if (numDigits == 4)
        {
            // 10m precision - 4 digits (0000-9999)
            gridEasting = (Math.AbsInt(worldPos[0]) / 10) % 10000;
            gridNorthing = (Math.AbsInt(worldPos[2]) / 10) % 10000;
        }
        else if (numDigits == 3)
        {
            // 100m precision - 3 digits (000-999)
            gridEasting = (Math.AbsInt(worldPos[0]) / 100) % 1000;
            gridNorthing = (Math.AbsInt(worldPos[2]) / 100) % 1000;
        }
        else if (numDigits == 2)
        {
            // 1km precision - 2 digits (00-99)
            gridEasting = (Math.AbsInt(worldPos[0]) / 1000) % 100;
            gridNorthing = (Math.AbsInt(worldPos[2]) / 1000) % 100;
        }
        else
        {
            // Default to 4 digit
            gridEasting = (Math.AbsInt(worldPos[0]) / 10) % 10000;
            gridNorthing = (Math.AbsInt(worldPos[2]) / 10) % 10000;
            numDigits = 4;
        }
        
        // Format with leading zeros
        string eastingStr = gridEasting.ToString();
        string northingStr = gridNorthing.ToString();
        
        while (eastingStr.Length() < numDigits)
            eastingStr = "0" + eastingStr;
        while (northingStr.Length() < numDigits)
            northingStr = "0" + northingStr;
        
        return string.Format("%1 %2", eastingStr, northingStr);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get the 100km square identifier for the current map
    //! Calculated once from lat/long, static for entire map
    static string Get100kmSquareId()
    {
        if (s_b100kmSquareValid)
            return s_Cached100kmSquare;
        
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return "";
        
        TimeAndWeatherManagerEntity weatherManager = world.GetTimeAndWeatherManager();
        if (!weatherManager)
            return "";
        
        float latitude = weatherManager.GetCurrentLatitude();
        float longitude = weatherManager.GetCurrentLongitude();
        
        // Convert map's lat/long to UTM
        LatLongToUTM(latitude, longitude, s_CachedUTMZone, s_CachedBaseEasting, s_CachedBaseNorthing);
        
        // Calculate 100km square from the map origin's UTM position
        s_Cached100kmSquare = Calculate100kmSquare(s_CachedUTMZone, s_CachedBaseEasting, s_CachedBaseNorthing);
        s_b100kmSquareValid = true;
        
        Print(string.Format("[MGRS] 100km Square: %1 (Zone=%2, E=%3, N=%4)", 
            s_Cached100kmSquare, s_CachedUTMZone, s_CachedBaseEasting, s_CachedBaseNorthing), LogLevel.DEBUG);
        
        return s_Cached100kmSquare;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Calculate 100km square identifier from UTM zone and coordinates
    protected static string Calculate100kmSquare(int utmZone, float easting, float northing)
    {
        int col100k = Math.Floor(easting / 100000.0);
        
        // Column letter pattern repeats every 3 zones
        int setIndex = (utmZone - 1) % 3;
        int colLetterIndex = (setIndex * 8 + col100k - 1) % 24;
        if (colLetterIndex < 0) 
            colLetterIndex += 24;
        
        // Row index cycles every 2,000,000m
        int row100k = Math.Floor(northing / 100000.0);
        row100k = row100k % 20;
        if (row100k < 0) row100k += 20;
        
        // Row letters offset by 5 for even zones
        if (utmZone % 2 == 0)
            row100k = (row100k + 5) % 20;
        
        colLetterIndex = Math.ClampInt(colLetterIndex, 0, 23);
        row100k = Math.ClampInt(row100k, 0, 19);
        
        string colLetter = MGRS_COL_LETTERS.Substring(colLetterIndex, 1);
        string rowLetter = MGRS_ROW_LETTERS.Substring(row100k, 1);
        
        return colLetter + rowLetter;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Convert latitude/longitude to UTM
    protected static void LatLongToUTM(float latitude, float longitude, out int utmZone, out float easting, out float northing)
    {
        const float a = 6378137.0;
        const float f = 1.0 / 298.257223563;
        const float k0 = 0.9996;
        
        float e2 = 2.0 * f - f * f;
        float e_prime2 = e2 / (1.0 - e2);
        
        utmZone = Math.Floor((longitude + 180.0) / 6.0) + 1;
        utmZone = Math.ClampInt(utmZone, 1, 60);
        
        // Norway/Svalbard exceptions
        if (latitude >= 56.0 && latitude < 64.0 && longitude >= 3.0 && longitude < 12.0)
            utmZone = 32;
        else if (latitude >= 72.0 && latitude < 84.0)
        {
            if (longitude >= 0.0 && longitude < 9.0)
                utmZone = 31;
            else if (longitude >= 9.0 && longitude < 21.0)
                utmZone = 33;
            else if (longitude >= 21.0 && longitude < 33.0)
                utmZone = 35;
            else if (longitude >= 33.0 && longitude < 42.0)
                utmZone = 37;
        }
        
        float lon0Deg = (utmZone - 1) * 6.0 - 180.0 + 3.0;
        float lon0 = lon0Deg * Math.DEG2RAD;
        float latRad = latitude * Math.DEG2RAD;
        float lonRad = longitude * Math.DEG2RAD;
        
        float sinLat = Math.Sin(latRad);
        float cosLat = Math.Cos(latRad);
        float tanLat = Math.Tan(latRad);
        
        float N = a / Math.Sqrt(1.0 - e2 * sinLat * sinLat);
        float T = tanLat * tanLat;
        float C = e_prime2 * cosLat * cosLat;
        float A = cosLat * (lonRad - lon0);
        float A2 = A * A;
        float A3 = A2 * A;
        float A4 = A3 * A;
        float A5 = A4 * A;
        float A6 = A5 * A;
        
        float M = a * ((1.0 - e2/4.0 - 3.0*e2*e2/64.0 - 5.0*e2*e2*e2/256.0) * latRad
                     - (3.0*e2/8.0 + 3.0*e2*e2/32.0 + 45.0*e2*e2*e2/1024.0) * Math.Sin(2.0*latRad)
                     + (15.0*e2*e2/256.0 + 45.0*e2*e2*e2/1024.0) * Math.Sin(4.0*latRad)
                     - (35.0*e2*e2*e2/3072.0) * Math.Sin(6.0*latRad));
        
        easting = k0 * N * (A + (1.0 - T + C) * A3 / 6.0
                          + (5.0 - 18.0*T + T*T + 72.0*C - 58.0*e_prime2) * A5 / 120.0)
                  + 500000.0;
        
        northing = k0 * (M + N * tanLat * (A2 / 2.0
                        + (5.0 - T + 9.0*C + 4.0*C*C) * A4 / 24.0
                        + (61.0 - 58.0*T + T*T + 600.0*C - 330.0*e_prime2) * A6 / 720.0));
        
        if (latitude < 0.0)
            northing += 10000000.0;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get Grid Zone Designator (e.g., "33T")
    static string GetGridZoneDesignator()
    {
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return "";
        
        TimeAndWeatherManagerEntity weatherManager = world.GetTimeAndWeatherManager();
        if (!weatherManager)
            return "";
        
        float latitude = weatherManager.GetCurrentLatitude();
        float longitude = weatherManager.GetCurrentLongitude();
        
        if (s_bGZDValid && 
            Math.AbsFloat(latitude - s_CachedLatitude) < 0.001 && 
            Math.AbsFloat(longitude - s_CachedLongitude) < 0.001)
        {
            return s_CachedGZD;
        }
        
        int utmZone = Math.Floor((longitude + 180.0) / 6.0) + 1;
        utmZone = Math.ClampInt(utmZone, 1, 60);
        
        // Norway/Svalbard exceptions
        if (latitude >= 56.0 && latitude < 64.0 && longitude >= 3.0 && longitude < 12.0)
            utmZone = 32;
        else if (latitude >= 72.0 && latitude < 84.0)
        {
            if (longitude >= 0.0 && longitude < 9.0)
                utmZone = 31;
            else if (longitude >= 9.0 && longitude < 21.0)
                utmZone = 33;
            else if (longitude >= 21.0 && longitude < 33.0)
                utmZone = 35;
            else if (longitude >= 33.0 && longitude < 42.0)
                utmZone = 37;
        }
        
        string bandLetter = GetLatitudeBandLetter(latitude);
        
        s_CachedGZD = string.Format("%1%2", utmZone, bandLetter);
        s_CachedLatitude = latitude;
        s_CachedLongitude = longitude;
        s_bGZDValid = true;
        
        return s_CachedGZD;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Convert latitude to MGRS latitude band letter
    protected static string GetLatitudeBandLetter(float latitude)
    {
        if (latitude < -80.0)
            return "A";
        if (latitude >= 84.0)
            return "Z";
        
        int bandIndex;
        if (latitude >= 72.0)
            bandIndex = 19;
        else
        {
            bandIndex = Math.Floor((latitude + 80.0) / 8.0);
            bandIndex = Math.ClampInt(bandIndex, 0, 18);
        }
        
        return LATITUDE_BANDS.Substring(bandIndex, 1);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get compact MGRS (GZD + grid, no 100km square)
    static string GetCompactMGRS(vector worldPos, int numDigits = 4)
    {
        string gzd = GetGridZoneDesignator();
        string gridRef = GetGridReference(worldPos, numDigits);
        
        if (gzd.IsEmpty())
            return gridRef;
        
        return string.Format("%1 %2", gzd, gridRef);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get raw grid coordinates only (no letters)
    static string GetRawGridCoordinates(vector worldPos, int numDigits = 4)
    {
        return GetGridReference(worldPos, numDigits);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Clear cached data
    static void InvalidateCache()
    {
        s_bGZDValid = false;
        s_b100kmSquareValid = false;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Debug print
    static void DebugPrintMapLocation()
    {
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
        {
            Print("[MGRS] No ChimeraWorld", LogLevel.DEBUG);
            return;
        }
        
        TimeAndWeatherManagerEntity weatherManager = world.GetTimeAndWeatherManager();
        if (!weatherManager)
        {
            Print("[MGRS] No TimeAndWeatherManager", LogLevel.DEBUG);
            return;
        }
        
        float lat = weatherManager.GetCurrentLatitude();
        float lon = weatherManager.GetCurrentLongitude();
        
        Print(string.Format("[MGRS] Map: Lat=%1, Lon=%2, GZD=%3, 100km=%4", 
            lat, lon, GetGridZoneDesignator(), Get100kmSquareId()), LogLevel.DEBUG);
    }
}