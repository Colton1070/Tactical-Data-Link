// TDL_EUDBoneComponent.c
class TDL_EUDBoneComponentClass : ScriptComponentClass {}

class TDL_EUDBoneComponent : ScriptComponent
{
    [Attribute("v_pivot", UIWidgets.EditBox, "Bone name to rotate")]
    protected string m_sBoneName;
    
    [Attribute("-90", UIWidgets.EditBox, "Min rotation angle (degrees) - stowed")]
    protected float m_fMinAngle;
    
    [Attribute("0", UIWidgets.EditBox, "Max rotation angle (degrees) - deployed")]
    protected float m_fMaxAngle;
    
    [Attribute("0", UIWidgets.ComboBox, "Rotation axis", "", ParamEnumArray.FromEnum(ETDL_EUDAxis))]
    protected ETDL_EUDAxis m_eRotationAxis;
    
    [Attribute("0.5", UIWidgets.Slider, "Initial position (0-1)", "0 1 0.01")]
    protected float m_fTargetPosition;
    
    [Attribute("5.0", UIWidgets.Slider, "Lerp speed (higher = faster)", "0.5 20 0.5")]
    protected float m_fLerpSpeed;
    
    protected float m_fCurrentPosition;
    protected const float POSITION_EPSILON = 0.001;
    protected int m_iBoneIdx = -1;
    protected bool m_bInitialized;
    
    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        
        // Skip for preview entities
        if (owner.GetWorld() != GetGame().GetWorld())
            return;
        
        SetEventMask(owner, EntityEvent.FRAME);
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnFrame(IEntity owner, float timeSlice)
    {
        // Double-check world (in case of race)
        if (owner.GetWorld() != GetGame().GetWorld())
            return;
        
        if (!m_bInitialized)
        {
            InitBone(owner);
            if (!m_bInitialized)
                return;
        }
        
        UpdateLerp(timeSlice);
        UpdateBone(owner);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void InitBone(IEntity owner)
    {
        if (m_bInitialized)
            return;
        
        Animation anim = owner.GetAnimation();
        if (!anim)
            return;
        
        m_iBoneIdx = anim.GetBoneIndex(m_sBoneName);
        if (m_iBoneIdx == -1)
            return;
        
        m_fCurrentPosition = m_fTargetPosition;
        m_bInitialized = true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateLerp(float timeSlice)
    {
        float delta = m_fTargetPosition - m_fCurrentPosition;
        if (Math.AbsFloat(delta) < POSITION_EPSILON)
        {
            m_fCurrentPosition = m_fTargetPosition;
            return;
        }
        
        float t = Math.Clamp(m_fLerpSpeed * timeSlice, 0.0, 1.0);
        m_fCurrentPosition = Math.Lerp(m_fCurrentPosition, m_fTargetPosition, t);
        
        if (Math.AbsFloat(m_fTargetPosition - m_fCurrentPosition) < POSITION_EPSILON)
            m_fCurrentPosition = m_fTargetPosition;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateBone(IEntity owner)
    {
        if (m_iBoneIdx == -1)
            return;
        
        Animation anim = owner.GetAnimation();
        if (!anim)
            return;
        
        float angle = Math.Lerp(m_fMinAngle, m_fMaxAngle, m_fCurrentPosition);
        
        vector angles = vector.Zero;
        switch (m_eRotationAxis)
        {
            case ETDL_EUDAxis.X: angles[0] = angle; break;
            case ETDL_EUDAxis.Y: angles[1] = angle; break;
            case ETDL_EUDAxis.Z: angles[2] = angle; break;
        }
        
        vector mat[4];
        Math3D.AnglesToMatrix(angles, mat);
        mat[3] = vector.Zero;
        
        anim.SetBoneMatrix(owner, m_iBoneIdx, mat);
    }
    
    //------------------------------------------------------------------------------------------------
    void RequestAdjustment(float delta)
    {
        m_fTargetPosition = Math.Clamp(m_fTargetPosition + delta, 0.0, 1.0);
        Replication.BumpMe();
    }
    
    //------------------------------------------------------------------------------------------------
    void SetPosition(float pos)
    {
        m_fTargetPosition = Math.Clamp(pos, 0.0, 1.0);
        Replication.BumpMe();
    }
    
    //------------------------------------------------------------------------------------------------
    void SetPositionImmediate(float pos)
    {
        m_fTargetPosition = Math.Clamp(pos, 0.0, 1.0);
        m_fCurrentPosition = m_fTargetPosition;
        Replication.BumpMe();
    }
    
    //------------------------------------------------------------------------------------------------
    float GetPosition() { return m_fTargetPosition; }
    float GetCurrentPosition() { return m_fCurrentPosition; }
    bool IsAnimating() { return Math.AbsFloat(m_fTargetPosition - m_fCurrentPosition) >= POSITION_EPSILON; }
    float GetMinAngle() { return m_fMinAngle; }
    float GetMaxAngle() { return m_fMaxAngle; }
}

//------------------------------------------------------------------------------------------------
enum ETDL_EUDAxis
{
    X,
    Y,
    Z
}