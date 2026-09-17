//------------------------------------------------------------------------------------------------
//! What the pointer is over on the 2D map.
//!
//! Order matters: the resolver walks these from most to least specific, so a contact standing
//! inside a drawn shape resolves as the contact. Anything that reads a pick and branches on it
//! should preserve that priority rather than testing in its own order.
//------------------------------------------------------------------------------------------------
enum AG0_ETDLPickKind
{
    NONE,           // Bare map
    SELF,           // The local player's own position
    MEMBER,         // Another TDL network member
    OWN_MARKER,     // A placed marker this player created — deletable, movable
    OTHER_MARKER,   // Someone else's marker — inspectable, not deletable
    SHAPE           // A drawn shape
}

//------------------------------------------------------------------------------------------------
//! One resolved pick.
//!
//! Carries IDs alongside the object handles because a consumer may outlive the frame the pick
//! was made in — the radial holds its open-position state across every frame the wheel is up.
//! `m_Marker` and `m_Shape` are plain (non-ref) members, so they are weak handles valid only
//! for the frame that produced them; re-resolve from the ID after that.
//------------------------------------------------------------------------------------------------
class AG0_TDLMapPickResult
{
    AG0_ETDLPickKind m_eKind;

    //! Human-readable name for whatever was hit — feeds the position readout's first line.
    string m_sLabel;

    //! Where the hit thing actually is, which is not the same as where the pointer was. Snapping
    //! to it is what lets "range and bearing to this" mean the contact rather than near it.
    vector m_vWorldPos;

    //! Same-frame handles.
    SCR_MapMarkerBase m_Marker;
    AG0_TDLMapShape m_Shape;

    //! Stable identity, safe to hold.
    int m_iMarkerId;
    string m_sShapeId;
    RplId m_MemberRplId;

    void AG0_TDLMapPickResult()
    {
        m_eKind = AG0_ETDLPickKind.NONE;
        m_iMarkerId = -1;
        m_sShapeId = "";
        m_MemberRplId = RplId.Invalid();
        m_vWorldPos = vector.Zero;
    }

    bool IsEmpty()
    {
        return m_eKind == AG0_ETDLPickKind.NONE;
    }

    bool IsMarker()
    {
        return m_eKind == AG0_ETDLPickKind.OWN_MARKER || m_eKind == AG0_ETDLPickKind.OTHER_MARKER;
    }

    //! Deletion is owner-gated the same way the sweep is: the server is authoritative, but
    //! offering the action on something it will refuse teaches the operator to distrust the menu.
    bool IsDeletable()
    {
        return m_eKind == AG0_ETDLPickKind.OWN_MARKER || m_eKind == AG0_ETDLPickKind.SHAPE;
    }
}
