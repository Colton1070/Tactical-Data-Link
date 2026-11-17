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
    
    [Attribute("0.5", UIWidgets.Slider, "Initial position (0-1)", "0 1 0.01"), RplProp(onRplName: "OnTargetReplicated")]
    protected float m_fTargetPosition;

    [Attribute("1", UIWidgets.Slider, "Position (0-1) at which the docked screen is folded fully down for reading. Focus mode holds the hinge here", "0 1 1")]
    protected float m_fReadPosition;

    [Attribute("5.0", UIWidgets.Slider, "Lerp speed (higher = faster)", "0.5 20 0.5")]
    protected float m_fLerpSpeed;

    // Set while the wearer is reading the docked screen up close, so every machine keeps
    // that character's hand on the device.
    [RplProp(onRplName: "OnTargetReplicated")]
    protected bool m_bReadHold;

    protected float m_fCurrentPosition;

    // The hand reach runs here, on every machine that draws the wearer, not on the player
    // controller: its inputs are a graph variable and an IK target, which the engine does not
    // carry to other machines. Only the hinge target and the hold flag go over the network.
    protected ref TDL_FoldHandReach m_HandReach;
    protected float m_fSeenTarget;
    protected float m_fAgeS;

    // A proxy gets its first target shortly after it appears; that is not a fold.
    protected const float REACH_QUIET_S = 1.0;

	protected const float POSITION_EPSILON = 0.001;
    protected int m_iBoneIdx = -1;
    protected bool m_bInitialized;
    
    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        
		InitBone(owner);
        UpdateBone(owner);
        
        SetEventMask(owner, EntityEvent.FRAME);
        owner.SetFlags(EntityFlags.ACTIVE, true);
    }

    //------------------------------------------------------------------------------------------------
    // Inventory switches a stored item off, and a mount riding on gear is a stored item:
    // without this it stops ticking and the hinge never moves.
    protected void OnTargetReplicated()
    {
        GetOwner().SetFlags(EntityFlags.ACTIVE, true);
    }
    
    //------------------------------------------------------------------------------------------------
    override void EOnFrame(IEntity owner, float timeSlice)
    {
        // Double-check world (in case of race)
        if (owner.GetWorld() != GetGame().GetWorld())
            return;

        // Target is the replicated value; every client lerps its own local m_fCurrentPosition
        // toward it. This avoids the manipulator stuttering from per-frame replicated snaps
        // of an authoritative current value.
        UpdateLerp(timeSlice);
        UpdateBone(owner);
        DropStaleReadHold(owner);
        UpdateHandReach(owner, timeSlice);
    }

    //------------------------------------------------------------------------------------------------
    // The hold is let go by the client that asked for it. If that client can no longer do so,
    // because its character died or the mount came off, the server lets go for it.
    protected void DropStaleReadHold(IEntity owner)
    {
        if (!m_bReadHold || !Replication.IsServer())
            return;

        ChimeraCharacter wearer = ChimeraCharacter.Cast(owner.GetRootParent());
        if (wearer)
        {
            CharacterControllerComponent controller = wearer.GetCharacterController();
            if (controller != null && controller.GetLifeState() == ECharacterLifeState.ALIVE)
                return;
        }

        SetReadHold(false);
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
        m_fSeenTarget = m_fTargetPosition;
        m_bInitialized = true;
    }

    //------------------------------------------------------------------------------------------------
    protected void UpdateHandReach(IEntity owner, float timeSlice)
    {
        if (RplSession.Mode() == RplMode.Dedicated)
            return;

        if (!m_HandReach)
            m_HandReach = new TDL_FoldHandReach();

        m_fAgeS = m_fAgeS + timeSlice;
        if (m_fTargetPosition != m_fSeenTarget)
        {
            m_fSeenTarget = m_fTargetPosition;
            if (m_fAgeS > REACH_QUIET_S)
                m_HandReach.Trigger();
        }

        IEntity wearer = owner.GetRootParent();
        if (!ChimeraCharacter.Cast(wearer))
            wearer = null;

        if (m_bReadHold && wearer != null)
            m_HandReach.Keep();

        m_HandReach.Update(wearer, this, timeSlice);
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
        GetOwner().SetFlags(EntityFlags.ACTIVE, true);
        Replication.BumpMe();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Holding folds the screen down to where it reads and keeps the wearer's hand on it.
    void SetReadHold(bool hold)
    {
        m_bReadHold = hold;
        if (hold)
            m_fTargetPosition = Math.Clamp(m_fReadPosition, 0.0, 1.0);
        GetOwner().SetFlags(EntityFlags.ACTIVE, true);
        Replication.BumpMe();
    }

    //------------------------------------------------------------------------------------------------
    bool IsReadHeld() { return m_bReadHold; }

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
    float GetReadPosition() { return m_fReadPosition; }
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