//------------------------------------------------------------------------------------------------
//! Routes one web-originated mirror command to whatever live AG0_TDLMenuController
//! frontend is willing to take it. Static-only — there's no per-player state on
//! the dispatcher itself; it picks the first live controller and calls the
//! matching ApplyMirror* method, which already handles cross-frontend sync via
//! the existing static-state + ScriptInvoker plumbing.
//!
//! When no frontend is alive (player not holding device, menu not open) the
//! command is dropped here. The server-side queue handler then emits a
//! mirror_command_rejected event so the web tab can surface the "device not
//! held" state. We don't queue locally — replaying commands in unexpected order
//! after a long held/unheld gap is worse than rejecting and surfacing it.
//------------------------------------------------------------------------------------------------
class AG0_TDLMirrorCommandDispatcher
{
    //! Entry point — parses the command JSON and routes by type. Called from
    //! the owner-side RPC handler on AG0_PlayerController_TDL, so we are
    //! already on the correct client and the local player is guaranteed to be
    //! the mirror target.
    static void Dispatch(string commandJson)
    {
        if (commandJson.IsEmpty())
        {
            Print("[TDL_MIRROR_DISPATCH_C] empty commandJson", LogLevel.WARNING);
            return;
        }

        SCR_JsonLoadContext j = new SCR_JsonLoadContext();
        if (!j.ImportFromString(commandJson))
        {
            Print(string.Format("[TDL_MIRROR_DISPATCH_C] failed to parse JSON: %1", commandJson), LogLevel.WARNING);
            return;
        }

        string cmdType;
        if (!j.ReadValue("type", cmdType))
        {
            Print("[TDL_MIRROR_DISPATCH_C] command missing 'type'", LogLevel.WARNING);
            return;
        }

        switch (cmdType)
        {
            case "mirror_set_panel":
                ApplySetPanel(j);
                break;
            case "mirror_set_chat_contact":
                ApplySetChatContact(j);
                break;
            case "mirror_set_brightness":
                ApplySetBrightness(j);
                break;
            case "mirror_set_map_view":
                ApplySetMapView(j);
                break;
            case "mirror_toggle_bloodhound":
                ApplyToggleBloodhound(j);
                break;
            case "mirror_set_callsign":
                ApplySetCallsign(j);
                break;
            case "mirror_toggle_camera_broadcast":
                ApplyToggleCameraBroadcast(j);
                break;
        }
    }

    //! Return the first live controller, or null if no frontend is up. Skips
    //! dangling/null entries that can appear when a frontend is freed without
    //! Cleanup pulling its registry entry — strong-ref registry should prevent
    //! that, but this is a defensive backstop. Caller handles the null path.
    protected static AG0_TDLMenuController GetTargetController()
    {
        return AG0_TDLMenuController.FindPrimaryLiveController();
    }

    protected static void ApplySetPanel(SCR_JsonLoadContext j)
    {
        int panel;
        if (!j.ReadValue("panel", panel))
        {
            Print("[TDL_MIRROR_DISPATCH_C] ApplySetPanel: 'panel' missing or non-int", LogLevel.WARNING);
            return;
        }
        string pluginId;
        j.ReadValue("panelPluginID", pluginId);
        // Optional — the web mirror sends this whenever it transitions into
        // MEMBER_DETAIL so the in-game controller's selected member follows
        // the website's click. Absent for every other panel; default 0 means
        // "leave selection alone".
        int selectedDeviceRplIdInt = 0;
        j.ReadValue("selectedDeviceRplId", selectedDeviceRplIdInt);
        AG0_TDLMenuController c = GetTargetController();
        if (!c)
        {
            Print(string.Format("[TDL_MIRROR_DISPATCH_C] ApplySetPanel: no live controller (panel=%1)", panel), LogLevel.WARNING);
            return;
        }
        c.ApplyMirrorSetPanel(panel, pluginId, selectedDeviceRplIdInt);
    }

    protected static void ApplySetChatContact(SCR_JsonLoadContext j)
    {
        int rplIdInt;
        if (!j.ReadValue("chatContactRplId", rplIdInt))
            return;
        string name;
        j.ReadValue("chatContactName", name);
        AG0_TDLMenuController c = GetTargetController();
        if (!c)
            return;
        c.ApplyMirrorSetChatContact(rplIdInt, name);
    }

    protected static void ApplySetBrightness(SCR_JsonLoadContext j)
    {
        float value;
        if (!j.ReadValue("brightness", value))
            return;
        AG0_TDLMenuController c = GetTargetController();
        if (!c)
            return;
        c.ApplyMirrorSetBrightness(value);
    }

    protected static void ApplySetMapView(SCR_JsonLoadContext j)
    {
        float cx, cz, zoom;
        bool tracking, trackUp;
        // Each field individually optional so a partial update (just a pan, no
        // zoom change) only carries what it needs. Defaults are the current
        // persisted statics so an unspecified field keeps its prior value.
        vector savedCenter = AG0_TDLDisplayController.GetSavedCenter();
        if (!j.ReadValue("mapCenterX", cx))
            cx = savedCenter[0];
        if (!j.ReadValue("mapCenterZ", cz))
            cz = savedCenter[2];
        if (!j.ReadValue("mapZoom", zoom))
            zoom = AG0_TDLDisplayController.GetSavedZoom();
        if (!j.ReadValue("playerTracking", tracking))
            tracking = AG0_TDLDisplayController.GetPlayerTracking();
        if (!j.ReadValue("trackUp", trackUp))
            trackUp = AG0_TDLDisplayController.GetTrackUp();

        // Single-target call — ApplyMirrorSetMapView routes through
        // AG0_TDLDisplayController.PropagateMapState which fans out to every
        // live frontend's map view. Looping the dispatcher would just re-fire
        // the same fan-out per controller.
        AG0_TDLMenuController c = GetTargetController();
        if (!c)
        {
            Print(string.Format("[TDL_MIRROR_DISPATCH_C] ApplySetMapView: no live controller (cx=%1 cz=%2 zoom=%3 tracking=%4)",
                cx, cz, zoom, tracking), LogLevel.WARNING);
            return;
        }
        c.ApplyMirrorSetMapView(cx, cz, zoom, tracking, trackUp);
    }

    protected static void ApplyToggleBloodhound(SCR_JsonLoadContext j)
    {
        bool enabled;
        if (!j.ReadValue("enabled", enabled))
            return;
        AG0_TDLMenuController c = GetTargetController();
        if (!c)
            return;
        c.ApplyMirrorToggleBloodhound(enabled);
    }

    //! Callsign mutates the device's [RplProp] m_sCustomCallsign and rides the
    //! existing replication path. Routed through device.SetCustomCallsign so we
    //! take the EXACT same code path the in-game settings panel uses
    //! (m_NetworkDevice.SetCustomCallsign(text) in AG0_TDLMenuController). The
    //! earlier route through SCR_PlayerController.RequestSetDeviceCallsign used
    //! a different server RPC (RpcAsk_SetDeviceCallsign on the player controller
    //! vs RpcAsk_SetCustomCallsign on the device) — that path silently failed to
    //! reach the server in some configurations. Going through the device's own
    //! Rpc keeps the contract identical to in-game.
    protected static void ApplySetCallsign(SCR_JsonLoadContext j)
    {
        int deviceRplIdInt;
        if (!j.ReadValue("deviceRplId", deviceRplIdInt))
        {
            Print("[TDL_MIRROR_DISPATCH_C] ApplySetCallsign: 'deviceRplId' missing", LogLevel.WARNING);
            return;
        }
        string callsign;
        if (!j.ReadValue("callsign", callsign))
        {
            Print("[TDL_MIRROR_DISPATCH_C] ApplySetCallsign: 'callsign' missing", LogLevel.WARNING);
            return;
        }
        RplId deviceRplId = deviceRplIdInt;

        // Look up the device locally on the client. Replication.FindItem works
        // on both client and server as long as the device entity is replicated
        // into this peer's view — which it always is for the player's own
        // devices (held + inventory). If lookup fails, the snapshot's
        // deviceRplId was stale.
        RplComponent rpl = RplComponent.Cast(Replication.FindItem(deviceRplId));
        if (!rpl)
        {
            Print(string.Format("[TDL_MIRROR_DISPATCH_C] ApplySetCallsign: Replication.FindItem returned null for deviceRplId=%1 — stale snapshot?",
                deviceRplId), LogLevel.WARNING);
            return;
        }
        IEntity entity = rpl.GetEntity();
        if (!entity)
        {
            Print(string.Format("[TDL_MIRROR_DISPATCH_C] ApplySetCallsign: RplComponent has no entity for deviceRplId=%1",
                deviceRplId), LogLevel.WARNING);
            return;
        }
        AG0_TDLDeviceComponent device = AG0_TDLDeviceComponent.Cast(
            entity.FindComponent(AG0_TDLDeviceComponent));
        if (!device)
        {
            Print(string.Format("[TDL_MIRROR_DISPATCH_C] ApplySetCallsign: entity %1 has no AG0_TDLDeviceComponent",
                entity), LogLevel.WARNING);
            return;
        }

        // SetCustomCallsign auto-routes: if called on a client, fires its own
        // RpcAsk_SetCustomCallsign to the server. The server-side branch then
        // validates, trims, applies, BumpMe()'s, and notifies the system.
        device.SetCustomCallsign(callsign);
    }

    //! Camera broadcasting must route through the PlayerController's
    //! RpcAsk_SetCameraBroadcasting — AG0_TDLDeviceComponent.SetCameraBroadcasting
    //! has `if (!Replication.IsServer()) return;` at the top, so calling it
    //! from a client peer is a no-op. The PC's helper is the only client-side
    //! entry that fires a server RPC for this state, and it's the same path
    //! the in-game camera button uses.
    protected static void ApplyToggleCameraBroadcast(SCR_JsonLoadContext j)
    {
        int deviceRplIdInt;
        if (!j.ReadValue("deviceRplId", deviceRplIdInt))
        {
            Print("[TDL_MIRROR_DISPATCH_C] ApplyToggleCameraBroadcast: 'deviceRplId' missing", LogLevel.WARNING);
            return;
        }
        bool broadcasting;
        if (!j.ReadValue("broadcasting", broadcasting))
        {
            Print("[TDL_MIRROR_DISPATCH_C] ApplyToggleCameraBroadcast: 'broadcasting' missing", LogLevel.WARNING);
            return;
        }
        RplId deviceRplId = deviceRplIdInt;

        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
        {
            Print("[TDL_MIRROR_DISPATCH_C] ApplyToggleCameraBroadcast: no local PlayerController", LogLevel.WARNING);
            return;
        }
        pc.RequestSetCameraBroadcasting(deviceRplId, broadcasting);
    }
}
