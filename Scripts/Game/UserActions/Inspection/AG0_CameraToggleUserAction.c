class AG0_CameraToggleUserAction : SCR_InventoryAction
{
    protected AG0_TDLDeviceComponent m_TdlDeviceComp;

    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        if (!m_TdlDeviceComp)
            return false;

        // Must have VIDEO_SOURCE capability
        if (!m_TdlDeviceComp.HasCapability(AG0_ETDLDeviceCapability.VIDEO_SOURCE))
            return false;

        // Keep the inspect check
        CharacterControllerComponent charComp = CharacterControllerComponent.Cast(user.FindComponent(CharacterControllerComponent));
        if (charComp && !charComp.GetInspect())
            return false;

        // Device must be powered and in network
        if (!m_TdlDeviceComp.IsPowered() || !m_TdlDeviceComp.IsInNetwork())
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (!m_TdlDeviceComp)
        {
            Print("AG0_CameraToggleUserAction: Missing AG0_TDLDeviceComponent on PerformAction.", LogLevel.WARNING);
            return;
        }

        // Toggle camera broadcasting state
        bool currentState = m_TdlDeviceComp.IsCameraBroadcasting();
        m_TdlDeviceComp.SetCameraBroadcasting(!currentState);
        
        Print(string.Format("TDL_CAMERA_ACTION: Player %1 toggled camera to %2", 
            pUserEntity, !currentState), LogLevel.DEBUG);
    }

    //------------------------------------------------------------------------------------------------
    override bool HasLocalEffectOnlyScript()
    {
        return false;
    }

    //------------------------------------------------------------------------------------------------
    override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
    {
        m_TdlDeviceComp = AG0_TDLDeviceComponent.Cast(pOwnerEntity.FindComponent(AG0_TDLDeviceComponent));
        if (!m_TdlDeviceComp)
            Print(string.Format("AG0_CameraToggleUserAction: Owner entity '%1' does not have AG0_TDLDeviceComponent.", pOwnerEntity), LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    override bool GetActionNameScript(out string outName)
    {
        if (!m_TdlDeviceComp)
            return false;

        if (m_TdlDeviceComp.IsCameraBroadcasting())
            outName = "Stop Camera";
        else
            outName = "Start Camera";
        
        return true;
    }
}