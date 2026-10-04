//! Pickup hook: a marking item entering any inventory re-resolves its holder so the
//! registry can stamp it. The spawn-time path is the item's own deferred ResolveHolder;
//! this covers everything after that (ground pickup, loot, arsenal, trades).
modded class SCR_InventoryStorageManagerComponent
{
    override protected void OnItemAdded(BaseInventoryStorageComponent storageOwner, IEntity item)
    {
        super.OnItemAdded(storageOwner, item);

        if (!Replication.IsServer() || !item)
            return;
        TDL_MarkingComponent marking = TDL_MarkingComponent.Cast(item.FindComponent(TDL_MarkingComponent));
        if (!marking)
            return;
        // Parent link is settled by the next frame; resolving synchronously can still see
        // the old root.
        GetGame().GetCallqueue().CallLater(marking.ResolveHolder, 100, false);
    }
}
