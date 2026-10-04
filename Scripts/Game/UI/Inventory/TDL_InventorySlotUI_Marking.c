//! Routes the inventory's USE button to the patch text dialog for TDL_MarkingComponent items.
//! CanUseItem/UseItem are the per-slot hooks the menu consults for that button (the supply
//! slot overrides the same pair), so this needs no navigation-bar or layout changes.
modded class SCR_InventorySlotUI
{
    //------------------------------------------------------------------------------------------------
    protected TDL_MarkingComponent GetPatchComponent()
    {
        InventoryItemComponent item = GetInventoryItemComponent();
        if (!item || !item.GetOwner())
            return null;
        return TDL_MarkingComponent.Cast(item.GetOwner().FindComponent(TDL_MarkingComponent));
    }

    //------------------------------------------------------------------------------------------------
    override bool CanUseItem(IEntity player)
    {
        if (GetPatchComponent())
            return true;
        return super.CanUseItem(player);
    }

    //------------------------------------------------------------------------------------------------
    override void UseItem(IEntity player, SCR_EUseContext context)
    {
        TDL_MarkingComponent patch = GetPatchComponent();
        if (!patch)
        {
            super.UseItem(player, context);
            return;
        }

        RplComponent rpl = RplComponent.Cast(patch.GetOwner().FindComponent(RplComponent));
        if (!rpl)
            return;

        TDL_MarkingDialog.CreateDialog(rpl.Id(), patch.GetText());
    }
}
