//------------------------------------------------------------------------------------------------
//! Per-viewer view of one network member, ferried in the atak_mirror_sync
//! event so the web mirror can render fields the server-wide
//! AG0_TDLDeviceState payload can't carry (signalStrength, networkIp,
//! GPS-active, bridged flags — all of which depend on which device is
//! looking). The same shape is used for the snapshot's contacts[] field and
//! for the per-entry mirror envelope.
//!
//! Sourced from the network's currently-stored AG0_TDLNetworkMember entries
//! via AG0_TDLNetwork.GetDeviceData(); not the per-device pair-wise signal
//! reading (which only lives in transient connectivity ticks). Close enough
//! to the in-game contact-detail view for the web mirror's purposes and
//! avoids a second cache layer server-side.
//------------------------------------------------------------------------------------------------
class AG0_TDLMirrorContact
{
    //! Replication id of the contact's device — primary key the web mirror
    //! joins against the server-wide `/api/servers/{id}/tdl` device list.
    int   rplId;
    //! Raw dBm from the member's perspective; typically negative (e.g. -75).
    //! Web rounds on display. Reflects the network's last connectivity-tick
    //! computation; not strictly the local player's pair-wise reading, but
    //! close enough to match what PopulateDetailView shows in-game.
    float signalStrength;
    //! Host octet for the in-game 192.168.0.X presentation. 1-254. The
    //! "192.168.0." prefix is added on the web side, not shipped.
    int   networkIp;
    //! Replication id of the broadcasting device this member can see, or 0
    //! when no broadcast is visible. Web mirror uses non-zero as the "this
    //! contact has a feed" gate for the broadcasting badge.
    int   videoSourceRplId;
    //! True when the contact's device is currently providing a GPS fix.
    //! Web renders a "GPS: no fix" amber badge when this is false.
    bool  isGpsActive;
    //! True when the contact reaches the viewer's network via a bridge
    //! rather than directly. Pairs with sourceNetworkId.
    bool  isBridged;
    //! Originating network id when isBridged is true; -1 when the contact
    //! is native to the viewer's own network.
    int   sourceNetworkId;
}

//------------------------------------------------------------------------------------------------
//! Per-identity envelope the server submits to the API in atak_mirror_sync.
//! All snapshot fields flattened onto the entry itself (no nested ref) so
//! SCR_JsonSaveContext introspection matches the AG0_TDLDeviceState /
//! AG0_TDLNetworkState pattern in this codebase, which is the only ref-array
//! serialization shape with proven coverage. Field names match the per-class
//! snapshot below so the API parser doesn't see two different naming schemes.
//------------------------------------------------------------------------------------------------
class AG0_TDLMirrorEntry
{
    string identityId;
    int    playerId;
    string playerName;

    int    panel;
    string panelPluginID;
    int    chatContactRplId;
    string chatContactName;
    int    selectedDeviceRplId;

    float  mapCenterX;
    float  mapCenterZ;
    float  mapZoom;
    bool   playerTracking;
    bool   trackUp;

    float  brightness;
    bool   bloodhoundEnabled;
    bool   bloodhoundPinned;
    float  bloodhoundPinX;
    float  bloodhoundPinZ;

    string deviceCallsign;
    int    networkId;
    string networkStableId;
    int    deviceRplId;
    bool   cameraBroadcasting;

    float  playerWorldX;
    float  playerWorldY;
    float  playerWorldZ;
    float  playerHeadingDeg;
    string mgrs;

    // Per-viewer contacts — populated server-side from the network's stored
    // member data after FromJson. Public field so SCR_JsonSaveContext picks it
    // up via introspection when the envelope is emitted to the API.
    ref array<ref AG0_TDLMirrorContact> contacts;

    void AG0_TDLMirrorEntry()
    {
        contacts = new array<ref AG0_TDLMirrorContact>();
    }

    //! Copy in fields from a snapshot. The entry exists only so the per-identity
    //! envelope can carry identityId/playerId/playerName alongside the snapshot
    //! values; this helper keeps the field mapping in one place.
    void CopyFromSnapshot(AG0_TDLMirrorSnapshot s)
    {
        if (!s) return;
        panel = s.panel;
        panelPluginID = s.panelPluginID;
        chatContactRplId = s.chatContactRplId;
        chatContactName = s.chatContactName;
        selectedDeviceRplId = s.selectedDeviceRplId;
        mapCenterX = s.mapCenterX;
        mapCenterZ = s.mapCenterZ;
        mapZoom = s.mapZoom;
        playerTracking = s.playerTracking;
        trackUp = s.trackUp;
        brightness = s.brightness;
        bloodhoundEnabled = s.bloodhoundEnabled;
        bloodhoundPinned = s.bloodhoundPinned;
        bloodhoundPinX = s.bloodhoundPinX;
        bloodhoundPinZ = s.bloodhoundPinZ;
        deviceCallsign = s.deviceCallsign;
        networkId = s.networkId;
        networkStableId = s.networkStableId;
        deviceRplId = s.deviceRplId;
        cameraBroadcasting = s.cameraBroadcasting;
        playerWorldX = s.playerWorldX;
        playerWorldY = s.playerWorldY;
        playerWorldZ = s.playerWorldZ;
        playerHeadingDeg = s.playerHeadingDeg;
        mgrs = s.mgrs;
        // Reuse the snapshot's array — the snapshot owns it for one tick and
        // is discarded after submission, so aliasing here is cheaper than a
        // per-contact deep copy and the envelope never outlives the source
        // snapshot.
        contacts = s.contacts;
    }
}

//------------------------------------------------------------------------------------------------
//! Web-mirror snapshot of one player's ATAK UI state.
//!
//! Carried client->server as a single JSON string param on RpcAsk_PushATAKPanelState
//! and server->API as a nested object in the atak_mirror_sync event. JSON instead of
//! SSnapSerializer because the same payload re-emerges on the HTTP hop unchanged.
//!
//! Wire size budget (per snapshot):
//!   * Client -> server (this.ToJson): every primitive field is small; contacts[]
//!     is intentionally NOT written here because the holding client never owns
//!     per-viewer data — the server fills it after FromJson. Stays ~1 KB even
//!     with maximal plugin/panel state, well under the 8191-byte RPC cap.
//!   * Server -> API (AG0_TDLMirrorEntry introspection): adds the contacts[]
//!     array populated by AugmentSnapshotWithServerFields. At ~45 bytes of JSON
//!     per contact, a saturated 32-member network adds ~1.4-2 KB; total
//!     envelope sits at 5-6 KB worst case. Still comfortably under the cap, but
//!     the margin is no longer "vast" — if a future field doubles contact size,
//!     re-check before adding.
//!
//! Map-view fields are the only ones the holding client samples at frame rate; the
//! controller rate-limits their delta to 10 Hz before assembling a fresh snapshot.
//------------------------------------------------------------------------------------------------
class AG0_TDLMirrorSnapshot
{
    int    panel;                       // ETDLPanelContent
    string panelPluginID;
    int    chatContactRplId;
    string chatContactName;
    int    selectedDeviceRplId;

    float  mapCenterX;
    float  mapCenterZ;
    float  mapZoom;
    bool   playerTracking;
    bool   trackUp;

    float  brightness;
    bool   bloodhoundEnabled;
    bool   bloodhoundPinned;
    float  bloodhoundPinX;
    float  bloodhoundPinZ;

    string deviceCallsign;
    int    networkId;
    string networkStableId;
    int    deviceRplId;
    bool   cameraBroadcasting;

    // Server-only fields — the holding client never populates these. They appear in
    // the JSON the server forwards to the API so the web mirror can render player
    // position / heading / MGRS without a second roundtrip.
    float  playerWorldX;
    float  playerWorldY;
    float  playerWorldZ;
    float  playerHeadingDeg;
    string mgrs;

    // Per-viewer network member view, populated by AugmentSnapshotWithServerFields
    // from network.GetDeviceData(). Holding client never touches this field — ToJson
    // and FromJson below intentionally skip it so the client RPC body stays small,
    // and the server rebuilds it each mirror tick.
    ref array<ref AG0_TDLMirrorContact> contacts;

    void AG0_TDLMirrorSnapshot()
    {
        contacts = new array<ref AG0_TDLMirrorContact>();
    }

    //! Wire snapshot to JSON for both RPC param and API submission. Field ordering
    //! mirrors the class layout so diffs against the last sent JSON are stable.
    string ToJson()
    {
        SCR_JsonSaveContext j = new SCR_JsonSaveContext();
        j.WriteValue("panel", panel);
        j.WriteValue("panelPluginID", panelPluginID);
        j.WriteValue("chatContactRplId", chatContactRplId);
        j.WriteValue("chatContactName", chatContactName);
        j.WriteValue("selectedDeviceRplId", selectedDeviceRplId);

        j.WriteValue("mapCenterX", mapCenterX);
        j.WriteValue("mapCenterZ", mapCenterZ);
        j.WriteValue("mapZoom", mapZoom);
        j.WriteValue("playerTracking", playerTracking);
        j.WriteValue("trackUp", trackUp);

        j.WriteValue("brightness", brightness);
        j.WriteValue("bloodhoundEnabled", bloodhoundEnabled);
        j.WriteValue("bloodhoundPinned", bloodhoundPinned);
        j.WriteValue("bloodhoundPinX", bloodhoundPinX);
        j.WriteValue("bloodhoundPinZ", bloodhoundPinZ);

        j.WriteValue("deviceCallsign", deviceCallsign);
        j.WriteValue("networkId", networkId);
        j.WriteValue("networkStableId", networkStableId);
        j.WriteValue("deviceRplId", deviceRplId);
        j.WriteValue("cameraBroadcasting", cameraBroadcasting);

        j.WriteValue("playerWorldX", playerWorldX);
        j.WriteValue("playerWorldY", playerWorldY);
        j.WriteValue("playerWorldZ", playerWorldZ);
        j.WriteValue("playerHeadingDeg", playerHeadingDeg);
        j.WriteValue("mgrs", mgrs);

        return j.ExportToString();
    }

    //! Inflate a snapshot from JSON. Used server-side when an RPC payload arrives so
    //! the held panel/map fields can be merged into the server's augmented copy
    //! before forwarding to the API.
    bool FromJson(string jsonStr)
    {
        SCR_JsonLoadContext j = new SCR_JsonLoadContext();
        if (!j.ImportFromString(jsonStr))
            return false;

        j.ReadValue("panel", panel);
        j.ReadValue("panelPluginID", panelPluginID);
        j.ReadValue("chatContactRplId", chatContactRplId);
        j.ReadValue("chatContactName", chatContactName);
        j.ReadValue("selectedDeviceRplId", selectedDeviceRplId);

        j.ReadValue("mapCenterX", mapCenterX);
        j.ReadValue("mapCenterZ", mapCenterZ);
        j.ReadValue("mapZoom", mapZoom);
        j.ReadValue("playerTracking", playerTracking);
        j.ReadValue("trackUp", trackUp);

        j.ReadValue("brightness", brightness);
        j.ReadValue("bloodhoundEnabled", bloodhoundEnabled);
        j.ReadValue("bloodhoundPinned", bloodhoundPinned);
        j.ReadValue("bloodhoundPinX", bloodhoundPinX);
        j.ReadValue("bloodhoundPinZ", bloodhoundPinZ);

        j.ReadValue("deviceCallsign", deviceCallsign);
        j.ReadValue("networkId", networkId);
        j.ReadValue("networkStableId", networkStableId);
        j.ReadValue("deviceRplId", deviceRplId);
        j.ReadValue("cameraBroadcasting", cameraBroadcasting);

        return true;
    }

    //! Cheap "did the client-visible state change" check used by the controller's
    //! send loop. Map-view floats are quantised so micro-jitter from continuous
    //! mouse-wheel zoom or floating-point drift doesn't flood the uplink. The 1 m
    //! / 0.001 zoom granularity matches the perceptible-change threshold on the
    //! web mirror at expected viewport sizes.
    bool EqualsForClientUplink(AG0_TDLMirrorSnapshot other)
    {
        if (!other)
            return false;
        if (panel != other.panel) return false;
        if (panelPluginID != other.panelPluginID) return false;
        if (chatContactRplId != other.chatContactRplId) return false;
        if (chatContactName != other.chatContactName) return false;
        if (selectedDeviceRplId != other.selectedDeviceRplId) return false;
        if (playerTracking != other.playerTracking) return false;
        if (trackUp != other.trackUp) return false;
        if (bloodhoundEnabled != other.bloodhoundEnabled) return false;
        if (bloodhoundPinned != other.bloodhoundPinned) return false;
        if (cameraBroadcasting != other.cameraBroadcasting) return false;
        if (networkId != other.networkId) return false;
        if (networkStableId != other.networkStableId) return false;
        if (deviceRplId != other.deviceRplId) return false;
        if (deviceCallsign != other.deviceCallsign) return false;
        if (Math.AbsFloat(brightness - other.brightness) > 0.5) return false;
        if (Math.AbsFloat(mapZoom - other.mapZoom) > 0.001) return false;
        if (Math.AbsFloat(mapCenterX - other.mapCenterX) > 1.0) return false;
        if (Math.AbsFloat(mapCenterZ - other.mapCenterZ) > 1.0) return false;
        if (Math.AbsFloat(bloodhoundPinX - other.bloodhoundPinX) > 1.0) return false;
        if (Math.AbsFloat(bloodhoundPinZ - other.bloodhoundPinZ) > 1.0) return false;
        return true;
    }

    //! Copy receiver fields from `src`. Used by the controller to stash the
    //! just-sent snapshot as the new baseline without aliasing the live snapshot
    //! the next sample will overwrite.
    void CopyFrom(AG0_TDLMirrorSnapshot src)
    {
        if (!src) return;
        panel = src.panel;
        panelPluginID = src.panelPluginID;
        chatContactRplId = src.chatContactRplId;
        chatContactName = src.chatContactName;
        selectedDeviceRplId = src.selectedDeviceRplId;
        mapCenterX = src.mapCenterX;
        mapCenterZ = src.mapCenterZ;
        mapZoom = src.mapZoom;
        playerTracking = src.playerTracking;
        trackUp = src.trackUp;
        brightness = src.brightness;
        bloodhoundEnabled = src.bloodhoundEnabled;
        bloodhoundPinned = src.bloodhoundPinned;
        bloodhoundPinX = src.bloodhoundPinX;
        bloodhoundPinZ = src.bloodhoundPinZ;
        deviceCallsign = src.deviceCallsign;
        networkId = src.networkId;
        networkStableId = src.networkStableId;
        deviceRplId = src.deviceRplId;
        cameraBroadcasting = src.cameraBroadcasting;
        playerWorldX = src.playerWorldX;
        playerWorldY = src.playerWorldY;
        playerWorldZ = src.playerWorldZ;
        playerHeadingDeg = src.playerHeadingDeg;
        mgrs = src.mgrs;
    }
}
