//! How the item's face gets its look.
//!   AUTHORED   — the face wears an authored BCR/NMO; the render target carries text only, on
//!                a transparent background, and only the floating overlay quad samples it.
//!   PROCEDURAL — the face material is the render target; swatch (image or flat colour) and
//!                text are drawn together in the one face zone.
enum TDL_EMarkingMode
{
    AUTHORED,
    PROCEDURAL
}

//! Surfaces a marking can be rendered on. Values are editor picks only; the string keys in
//! TDL_MarkingSlots are the contract with the API and must never change once shipped.
enum TDL_EMarkingSlot
{
    PATCH,
    NAMETAPE,
    HELMET_BAND,
    BLOOD_TYPE,
    DEVICE_PLATE,
    VEHICLE_TAIL
}

class TDL_MarkingSlots
{
    static string ToKey(TDL_EMarkingSlot slot)
    {
        switch (slot)
        {
            case TDL_EMarkingSlot.PATCH:        return "patch";
            case TDL_EMarkingSlot.NAMETAPE:     return "nametape";
            case TDL_EMarkingSlot.HELMET_BAND:  return "helmet_band";
            case TDL_EMarkingSlot.BLOOD_TYPE:   return "blood_type";
            case TDL_EMarkingSlot.DEVICE_PLATE: return "device_plate";
            case TDL_EMarkingSlot.VEHICLE_TAIL: return "vehicle_tail";
        }
        return "patch";
    }

    //! Inverse of ToKey for chat commands and API payloads. False on an unknown key.
    static bool FromKey(string key, out TDL_EMarkingSlot slot)
    {
        key.ToLower();
        switch (key)
        {
            case "patch":        slot = TDL_EMarkingSlot.PATCH;        return true;
            case "nametape":     slot = TDL_EMarkingSlot.NAMETAPE;     return true;
            case "helmet_band":  slot = TDL_EMarkingSlot.HELMET_BAND;  return true;
            case "blood_type":   slot = TDL_EMarkingSlot.BLOOD_TYPE;   return true;
            case "device_plate": slot = TDL_EMarkingSlot.DEVICE_PLATE; return true;
            case "vehicle_tail": slot = TDL_EMarkingSlot.VEHICLE_TAIL; return true;
        }
        return false;
    }

    //! Row/column limits per slot. Mirror of the API's SlotProfile table.
    static void GetProfile(TDL_EMarkingSlot slot, out int rows, out int cols)
    {
        rows = 1;
        cols = 12;
        switch (slot)
        {
            case TDL_EMarkingSlot.PATCH:        rows = 2; cols = 12; break;
            case TDL_EMarkingSlot.NAMETAPE:     rows = 1; cols = 14; break;
            case TDL_EMarkingSlot.HELMET_BAND:  rows = 1; cols = 10; break;
            case TDL_EMarkingSlot.BLOOD_TYPE:   rows = 1; cols = 4;  break;
            case TDL_EMarkingSlot.DEVICE_PLATE: rows = 1; cols = 12; break;
            case TDL_EMarkingSlot.VEHICLE_TAIL: rows = 1; cols = 6;  break;
        }
    }
}

//! Colour values a marking style can carry: "" (item default), "#rrggbb", or "rainbow".
class TDL_MarkingColors
{
    static const string RAINBOW = "rainbow";
    //! 0xFF000000 as a signed 32-bit int (what TriMeshDrawCommand.m_iColor and the API palette carry).
    static const int ALPHA_OPAQUE = -16777216;

    //------------------------------------------------------------------------------------------------
    //! "#rrggbb" (case-insensitive) -> 0xAARRGGBB with full alpha. False on anything else.
    static bool FromHex(string hex, out int argb)
    {
        argb = 0;
        if (hex.Length() != 7 || hex.Get(0) != "#")
            return false;
        int value = 0;
        for (int i = 1; i < 7; i++)
        {
            int code = hex.Get(i).ToAscii();
            int digit;
            if (code >= 48 && code <= 57)
                digit = code - 48;
            else if (code >= 97 && code <= 102)
                digit = code - 87;
            else if (code >= 65 && code <= 70)
                digit = code - 55;
            else
                return false;
            value = (value << 4) | digit;
        }
        argb = ALPHA_OPAQUE | value;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    static Color FromArgb(int argb)
    {
        // No >> in Enforce; mask first so the sign bit from the alpha byte can't leak in.
        int ri = (argb & 16711680) / 65536;
        int gi = (argb & 65280) / 256;
        int bi = argb & 255;
        float r = ri / 255.0;
        float g = gi / 255.0;
        float b = bi / 255.0;
        return new Color(r, g, b, 1.0);
    }

    //------------------------------------------------------------------------------------------------
    //! sRGB -> linear on each channel, keeping alpha. Canvas vertex colours go into the
    //! render target untouched and the material then treats the RT as sRGB, so a colour
    //! given as sRGB comes out lifted; feeding it linear cancels that.
    static int ToLinear(int argb)
    {
        int a = argb & ALPHA_OPAQUE;
        int ri = (argb & 16711680) / 65536;
        int gi = (argb & 65280) / 256;
        int bi = argb & 255;
        int rl = Math.Pow(ri / 255.0, 2.2) * 255 + 0.5;
        int gl = Math.Pow(gi / 255.0, 2.2) * 255 + 0.5;
        int bl = Math.Pow(bi / 255.0, 2.2) * 255 + 0.5;
        return a | (rl * 65536) | (gl * 256) | bl;
    }

    //------------------------------------------------------------------------------------------------
    //! Hue in degrees, full saturation and value -> 0xAARRGGBB.
    static int FromHue(float hue)
    {
        while (hue >= 360)
            hue -= 360;
        while (hue < 0)
            hue += 360;
        float h = hue / 60.0;
        int sector = h;
        float f = h - sector;
        float q = 1.0 - f;
        float r, g, b;
        switch (sector)
        {
            case 0: r = 1; g = f; b = 0; break;
            case 1: r = q; g = 1; b = 0; break;
            case 2: r = 0; g = 1; b = f; break;
            case 3: r = 0; g = q; b = 1; break;
            case 4: r = f; g = 0; b = 1; break;
            default: r = 1; g = 0; b = q; break;
        }
        int ri = r * 255;
        int gi = g * 255;
        int bi = b * 255;
        return ALPHA_OPAQUE | (ri * 65536) | (gi * 256) | bi;
    }
}

//! One slot's style as the registry keeps it: colour values per key, "" = item default.
class TDL_MarkingStyle
{
    string m_sBg;
    string m_sFg;
}

