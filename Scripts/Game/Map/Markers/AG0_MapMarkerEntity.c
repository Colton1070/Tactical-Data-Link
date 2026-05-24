//------------------------------------------------------------------------------------------------
[EntityEditorProps(category: "GameScripted/Markers")]
class AG0_MapMarkerTDLClass : SCR_MapMarkerEntityClass
{}

//------------------------------------------------------------------------------------------------
//! Dynamic map marker -> squad leader
class AG0_MapMarkerTDL : SCR_MapMarkerEntity
{
	[RplProp(onRplName: "OnDeviceRplIdChanged")]
    protected RplId m_DeviceRplId;
	
    void SetDeviceRplId(RplId deviceRplId)
    {
        m_DeviceRplId = deviceRplId;
        Replication.BumpMe(); // Trigger replication to clients
    }

	void OnDeviceRplIdChanged()
	{
		// Start hidden — the next OnUpdatePosition / RefreshAllMarkerVisibility
		// tick will flip visibility on if the local player can see the device.
		SetLocalVisible(false);
	}

	override void EOnInit(IEntity owner)
	{
	    super.EOnInit(owner);
	    
	    // Always start hidden on clients
	    if (GetGame().GetPlayerController()) {
	        SetLocalVisible(false);
	    }
	}
	
	override void OnUpdateType()
	{
	    super.OnUpdateType();
	    
	    // Ensure we start hidden even if RplProp fires
	    if (GetGame().GetPlayerController()) {
	        SetLocalVisible(false);
	    }
	}
		
	protected override void OnUpdatePosition()
	{
	    super.OnUpdatePosition();

	    // Templates with no bound RplId are filtered at registration; if we
	    // still see an invalid id here it's a marker mid-replication and we
	    // simply hold the last-known visibility until the next tick.
	    if (m_DeviceRplId == RplId.Invalid())
	        return;

	    SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!pc) return;

	    bool shouldShow = pc.CanSeeDevice(m_DeviceRplId);
	    SetLocalVisible(shouldShow);
	}

    RplId GetDeviceRplId()
    {
        return m_DeviceRplId;
    }
	
	void SetVisibility(bool visible)
	{
	    SetLocalVisible(visible);
	}

}