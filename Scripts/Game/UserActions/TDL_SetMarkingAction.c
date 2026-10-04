//! "Set patch text" on a TDL_MarkingComponent item. The action runs on the server and on
//! the performing client; only the performer's own client has a UI to open, and the
//! resulting change goes back through the player controller RPC, so the server side of
//! PerformAction is deliberately a no-op.
class TDL_SetMarkingAction : ScriptedUserAction
{
    //------------------------------------------------------------------------------------------------
    override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
    {
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc || pc.GetControlledEntity() != pUserEntity)
            return;

        TDL_MarkingComponent patch = TDL_MarkingComponent.Cast(pOwnerEntity.FindComponent(TDL_MarkingComponent));
        RplComponent rpl = RplComponent.Cast(pOwnerEntity.FindComponent(RplComponent));
        if (!patch || !rpl)
        {
            Print("[TDL_Marking] action owner lacks TDL_MarkingComponent or RplComponent", LogLevel.ERROR);
            return;
        }

        TDL_MarkingDialog.CreateDialog(rpl.Id(), patch.GetText());
    }

    //------------------------------------------------------------------------------------------------
    override bool CanBeShownScript(IEntity user)
    {
        return GetOwner().FindComponent(TDL_MarkingComponent) != null;
    }

    //------------------------------------------------------------------------------------------------
    override bool GetActionNameScript(out string outName)
    {
        outName = "Set Patch Text";
        return true;
    }

    //------------------------------------------------------------------------------------------------
    override bool HasLocalEffectOnlyScript()
    {
        return true;
    }
}
