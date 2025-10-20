//! @ingroup ManualCamera

//! Override GM camera to show player TDL broadcast feed
[BaseContainerProps(), SCR_BaseManualCameraComponentTitle()]
class AG0_PlayerCameraOverrideComponent : SCR_BaseManualCameraComponent
{
	//This is an attribute - probably shouldn't be. Leave it 0.
    [Attribute("0")]
    protected bool m_bOverrideActive;
    
    protected AG0_TDLDeviceComponent m_ViewedDevice;
    
    //------------------------------------------------------------------------------------------------
    // Low priority to run after all movement components - high priority is good if you need to apply your transform additively before other components. In this case we want the final say.
    override int GetPriority() 
    { 
        return -1; 
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnCameraFrame(SCR_ManualCameraParam param)
    {
        if (!m_bOverrideActive || !m_ViewedDevice) 
            return;
        
        // Get broadcaster transform
        IEntity broadcaster = m_ViewedDevice.GetOwner();
        if (!broadcaster || !m_ViewedDevice.m_CameraAttachment) {
            // Device went away, disable override
            SetViewedDevice(null);
            return;
        }
        
        // Calculate camera position from attachment point
        vector entityTransform[4], localOffset[4];
        broadcaster.GetWorldTransform(entityTransform);
        m_ViewedDevice.m_CameraAttachment.GetLocalTransform(localOffset);
        Math3D.MatrixMultiply4(entityTransform, localOffset, param.transform);
        
        // Force camera update
        param.isDirty = true;
        
        // Disable manual input while overriding
        param.isManualInput = false;
    }
    
    //------------------------------------------------------------------------------------------------
    void SetViewedDevice(AG0_TDLDeviceComponent device) 
    { 
        m_ViewedDevice = device;
        m_bOverrideActive = (device != null);
        
        if (m_bOverrideActive) {
            Print("AG0_PlayerCameraOverrideComponent: Activating player camera override", LogLevel.DEBUG);
        } else {
            Print("AG0_PlayerCameraOverrideComponent: Deactivating player camera override", LogLevel.DEBUG);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsOverrideActive()
    {
        return m_bOverrideActive;
    }
    
    //------------------------------------------------------------------------------------------------
    AG0_TDLDeviceComponent GetViewedDevice()
    {
        return m_ViewedDevice;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool EOnCameraInit()
    {
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnCameraExit()
    {
        // Clean up on camera destruction
        SetViewedDevice(null);
    }
}