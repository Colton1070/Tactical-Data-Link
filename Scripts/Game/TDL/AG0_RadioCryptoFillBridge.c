/**
 * AG0_RadioCryptoFillBridge
 *
 * Static bridge allowing the TDL PlayerController to deliver a confirmed
 * crypto key to whichever external mod component owns the target entity,
 * without creating a compile-time dependency in either direction.
 *
 * Usage:
 *   Subscriber (e.g. SCR_VehicleRadioComponent in Vehicle Intercom mod):
 *     AG0_RadioCryptoFillBridge.s_OnKeyReceived.Insert(OnBridgeKeyReceived);
 *
 *   Invoker (AG0_PlayerController_TDL):
 *     AG0_RadioCryptoFillBridge.s_OnKeyReceived.Invoke(entityRplId, key);
 *
 *   Subscriber handler filters by RplId:
 *     protected void OnBridgeKeyReceived(RplId entityRplId, string key)
 *     {
 *         if (Replication.FindId(GetOwner()) != entityRplId)
 *             return;
 *         // apply key
 *     }
 */
class AG0_RadioCryptoFillBridge
{
	//! Fires server-side when a player confirms a crypto key entry for a radio entity.
	//! Signature: void Func(RplId entityRplId, string key)
	static ref ScriptInvoker s_OnKeyReceived = new ScriptInvoker();
}