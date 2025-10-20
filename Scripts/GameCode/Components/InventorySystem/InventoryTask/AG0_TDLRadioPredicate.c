class AG0_TDLRadioPredicate: InventorySearchPredicate
{
	//Just a simple class to search for our radio in someone's inventory.
    void AG0_TDLRadioPredicate()
    {
        QueryComponentTypes.Insert(AG0_TDLRadioComponent);
    }
}