// TDL EUD Entity - owns its own bone manipulation with MP replication
class TDL_EUDEntityClass : GenericEntityClass {}

class TDL_EUDEntity : GenericEntity
{
    [Attribute("v_pivot", UIWidgets.EditBox, "Bone name to rotate")]
    protected string m_sBoneName;
    
    [Attribute("-90", UIWidgets.EditBox, "Min rotation angle (degrees) - stowed")]
    protected float m_fMinAngle;
    
    [Attribute("0", UIWidgets.EditBox, "Max rotation angle (degrees) - deployed")]
    protected float m_fMaxAngle;
    
    [Attribute("0", UIWidgets.ComboBox, "Rotation axis", "", ParamEnumArray.FromEnum(ETDL_EUDAxis))]
    protected ETDL_EUDAxis m_eRotationAxis;
    
    [Attribute("0.5", UIWidgets.Slider, "Initial position (0-1)", "0 1 0.01"), RplProp(onRplName: "OnPositionChanged")]
    protected float m_fPosition;
    
    protected int m_iBoneIdx = -1;
    protected bool m_bInitialized;
    
    //------------------------------------------------------------------------------------------------
    void TDL_EUDEntity(IEntitySource src, IEntity parent)
    {
        SetFlags(EntityFlags.ACTIVE, true);
        SetEventMask(EntityEvent.FRAME);
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnFrame(IEntity owner, float timeSlice)
    {
        if (!m_bInitialized)
        {
            InitBone();
            if (!m_bInitialized)
                return;
        }
        
        UpdateBone();
    }
    
    //------------------------------------------------------------------------------------------------
    protected void InitBone()
    {
        if (m_bInitialized)
            return;
        
        Animation anim = GetAnimation();
        if (!anim)
            return;
        
        m_iBoneIdx = anim.GetBoneIndex(m_sBoneName);
        
        if (m_iBoneIdx == -1)
            return;
        
        m_bInitialized = true;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void UpdateBone()
    {
        if (m_iBoneIdx == -1)
            return;
        
        Animation anim = GetAnimation();
        if (!anim)
            return;
        
        float angle = Math.Lerp(m_fMinAngle, m_fMaxAngle, m_fPosition);
        
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
        
        anim.SetBoneMatrix(this, m_iBoneIdx, mat);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Called when replicated position changes on clients
    protected void OnPositionChanged()
    {
        // EOnFrame will pick up the new value and update bone
    }
    
    //------------------------------------------------------------------------------------------------
    //! Request adjustment - called by action, runs on server
    void RequestAdjustment(float delta)
    {
        m_fPosition = Math.Clamp(m_fPosition + delta, 0.0, 1.0);
        Replication.BumpMe();
    }
    
    //------------------------------------------------------------------------------------------------
    void SetPosition(float pos)
    {
        m_fPosition = Math.Clamp(pos, 0.0, 1.0);
        Replication.BumpMe();
    }
    
    //------------------------------------------------------------------------------------------------
    float GetPosition()
    {
        return m_fPosition;
    }
    
    //------------------------------------------------------------------------------------------------
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