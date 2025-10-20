class AG0_DeviceTDLNetworkAction : SCR_InventoryAction
{
    protected AG0_TDLDeviceComponent m_TdlDeviceComp;
	
	[Attribute("0", UIWidgets.CheckBox, "Does this action create a network? If unchecked, this action will join network.")]
    protected bool m_bCreateNetwork;

    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        if (!m_TdlDeviceComp)
            return false;

        // Keep the inspect check
        CharacterControllerComponent charComp = CharacterControllerComponent.Cast(user.FindComponent(CharacterControllerComponent));
        if (charComp && !charComp.GetInspect())
            return false;

        // Only show if device is powered and has network capability
        if (!m_TdlDeviceComp.IsPowered() || !m_TdlDeviceComp.CanAccessNetwork())
            return false;
		
		// Don't show create if already in network
		if (m_bCreateNetwork && m_TdlDeviceComp.IsInNetwork())
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (!m_TdlDeviceComp)
        {
            Print("AG0_DeviceTDLNetworkAction: Missing AG0_TDLDeviceComponent on PerformAction.", LogLevel.WARNING);
            return;
        }

        if (m_bCreateNetwork) {
            m_TdlDeviceComp.CreateNetworkDialog(pUserEntity);
        } else {
            if (m_TdlDeviceComp.IsInNetwork()) {
                m_TdlDeviceComp.LeaveNetworkTDL();
            } else {
                m_TdlDeviceComp.JoinNetworkDialog(pUserEntity);
            }
        }
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
            Print(string.Format("AG0_DeviceTDLNetworkAction: Owner entity '%1' does not have AG0_TDLDeviceComponent.", pOwnerEntity), LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    override bool GetActionNameScript(out string outName)
    {
        if (!m_TdlDeviceComp)
            return false;

        if (m_bCreateNetwork) {
            outName = "Create TDL Network";
        } else {
            if (m_TdlDeviceComp.IsInNetwork()) {
                outName = "Leave TDL Network";
            } else {
                outName = "Join TDL Network";
            }
        }
        
        return true;
    }
    
    void SetCreateNetwork(bool value)
    {
        m_bCreateNetwork = value;
    }
}