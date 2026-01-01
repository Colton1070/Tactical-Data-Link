//------------------------------------------------------------------------------------------------
// AG0_TDLToggleFrequencyHopAction.c
// User action to toggle frequency hopping on/off for a TDL radio
//------------------------------------------------------------------------------------------------

class AG0_TDLToggleFrequencyHopAction : ScriptedUserAction
{
    [Attribute("0", UIWidgets.Slider, "Which transceiver to toggle FH for", "0 7 1")]
    protected int m_iTransceiverIndex;
    
    //------------------------------------------------------------------------------------------------
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            pOwnerEntity.FindComponent(AG0_TDLRadioComponent));
        
        if (tdlRadio)
        {
            tdlRadio.ToggleFrequencyHop(m_iTransceiverIndex);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    override bool CanBePerformedScript(IEntity user)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return false;
        
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            owner.FindComponent(AG0_TDLRadioComponent));
        
        if (!tdlRadio)
            return false;
        
        // Always allow DISABLING
        if (tdlRadio.IsFrequencyHopEnabled(m_iTransceiverIndex))
            return true;
        
        // To ENABLE: require crypto key to be filled
        return tdlRadio.GetCurrentCryptoKey() != tdlRadio.GetDefaultCryptoKey();
    }
    
    //------------------------------------------------------------------------------------------------
    override bool GetActionNameScript(out string outName)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return false;
        
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            owner.FindComponent(AG0_TDLRadioComponent));
        
        if (!tdlRadio)
            return false;
        
        if (tdlRadio.IsFrequencyHopEnabled(m_iTransceiverIndex))
        {
            outName = "Disable Frequency Hop";
        }
        else
        {
            outName = "Enable Frequency Hop";
        }
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        IEntity owner = GetOwner();
        if (!owner)
            return false;
        
        // Only show for TDL radios
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            owner.FindComponent(AG0_TDLRadioComponent));
        
        if (!tdlRadio)
            return false;
        
        // Check transceiver exists
        BaseRadioComponent baseRadio = tdlRadio.GetRadioComponent();
        if (!baseRadio || m_iTransceiverIndex >= baseRadio.TransceiversCount())
            return false;
        
        return true;
    }
	
	override bool HasLocalEffectOnlyScript() { return false; }
}