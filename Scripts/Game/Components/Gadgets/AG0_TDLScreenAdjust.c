//------------------------------------------------------------------------------------------------
// Action - adjusts screen angle on TDL_EUDEntity with MP support
//------------------------------------------------------------------------------------------------
class TDL_ScreenAdjustAction : ScriptedUserAction
{
    [Attribute("0.05", UIWidgets.Slider, "Adjustment step (0-1 range)", "0.01 0.2 0.01")]
    protected float m_fAdjustmentStep;
    
    [Attribute("SelectAction", UIWidgets.EditBox, "Input action for increase")]
    protected string m_sActionIncrease;
    
    [Attribute("", UIWidgets.EditBox, "Input action for decrease")]
    protected string m_sActionDecrease;
    
    protected TDL_EUDEntity m_Device;
    protected bool m_bIsLocalPlayer;
    protected float m_fTargetValue;
    
    //------------------------------------------------------------------------------------------------
    override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
    {
        m_Device = TDL_EUDEntity.Cast(pOwnerEntity);
        
        if (m_Device)
            m_fTargetValue = m_Device.GetPosition();
    }
    
    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        return m_Device != null;
    }
    
    //------------------------------------------------------------------------------------------------
    override bool CanBePerformedScript(IEntity user)
    {
        return m_Device != null;
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnActionStart(IEntity pUserEntity)
    {
        m_bIsLocalPlayer = SCR_PlayerController.GetLocalControlledEntity() == pUserEntity;
        
        if (!m_bIsLocalPlayer)
            return;
        
        // Sync target with current device state
        if (m_Device)
            m_fTargetValue = m_Device.GetPosition();
        
        InputManager im = GetGame().GetInputManager();
        if (im)
        {
            if (!m_sActionIncrease.IsEmpty())
                im.AddActionListener(m_sActionIncrease, EActionTrigger.VALUE, OnAdjust);
            if (!m_sActionDecrease.IsEmpty())
                im.AddActionListener(m_sActionDecrease, EActionTrigger.VALUE, OnAdjustDecrease);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnActionCanceled(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        if (!m_bIsLocalPlayer)
            return;
        
        m_bIsLocalPlayer = false;
        
        InputManager im = GetGame().GetInputManager();
        if (im)
        {
            if (!m_sActionIncrease.IsEmpty())
                im.RemoveActionListener(m_sActionIncrease, EActionTrigger.VALUE, OnAdjust);
            if (!m_sActionDecrease.IsEmpty())
                im.RemoveActionListener(m_sActionDecrease, EActionTrigger.VALUE, OnAdjustDecrease);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnAdjust(float value)
    {
        if (value == 0 || !m_Device)
            return;
        
        float delta = (value / Math.AbsFloat(value)) * m_fAdjustmentStep;
        m_fTargetValue = Math.Clamp(m_fTargetValue + delta, 0.0, 1.0);
        
        // Trigger network sync
        SetSendActionDataFlag();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void OnAdjustDecrease(float value)
    {
        if (value == 0 || !m_Device)
            return;
        
        m_fTargetValue = Math.Clamp(m_fTargetValue - m_fAdjustmentStep, 0.0, 1.0);
        SetSendActionDataFlag();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Not local only - needs to sync to server
    override bool HasLocalEffectOnlyScript()
    {
        return false;
    }
    
}