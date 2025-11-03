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
	
	// Server-only method to set the device RplId
    void SetDeviceRplId(RplId deviceRplId)
    {
		PrintFormat("Setting deviceRplId for marker to %1", deviceRplId);
        m_DeviceRplId = deviceRplId;
        Replication.BumpMe(); // Trigger replication to clients
    }
	
	// Add callback that updates visibility
	void OnDeviceRplIdChanged()
	{
		PrintFormat("Setting deviceRplId locally for marker to %1", m_DeviceRplId);
	    SetLocalVisible(false); // Start hidden, let update cycle show if needed
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
	    PrintFormat("Device marker %1 changed position: ", m_DeviceRplId);
	    // Only check on clients with valid device ID
	    if (m_DeviceRplId == RplId.Invalid())
	        return;
	        
	    SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	    if (!pc) return;
	    
	    bool shouldShow = pc.CanSeeDevice(m_DeviceRplId);
	    SetLocalVisible(shouldShow);
	}

    // Method to get the device RplId (accessible on server and client)
    RplId GetDeviceRplId()
    {
        return m_DeviceRplId;
    }
	
	void SetVisibility(bool visible)
	{
	    SetLocalVisible(visible);
	}

}