modded class SCR_CharacterControllerComponent : CharacterControllerComponent
{
    override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
    {
        super.OnPrepareControls(owner, am, dt, player);
        
        // Pump TDL context every frame (must be continuous, not one-shot)
        if (player)
            PumpTDLMenuContext();
    }
    
    protected void PumpTDLMenuContext()
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return;
        
        if (pc.ShouldTDLMenuContextBeActive())
        {
            InputManager im = GetGame().GetInputManager();
            if (im)
                im.ActivateContext("TDLMenuContext", 1);
        }
    }
}