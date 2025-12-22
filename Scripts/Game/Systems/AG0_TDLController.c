//class AG0_TDLController : WorldController
//{
//	[RplProp()]
//	protected int m_iPlayerId;
//    //------------------------------------------------------------------------------------------------
//    override static void InitInfo(WorldControllerInfo outInfo)
//    {
//        outInfo.SetPublic(true);
//    }
//	
//	void AG0_TDLController()
//	{
//	    //Nothing. These don't work well right now.
//	}
//	
////	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
////	private void RPC_RegisterWithPlayerID(int realPlayerId)
////	{
////	    AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
////	    if (system)
////	    {
////			//Since FindController(controller, playerId) is scuffed and doesn't work.
////	        system.RegisterController(this, realPlayerId);
////	        Print(string.Format("TDL_PLAYERCONTROLLER: Registered with player ID %1", realPlayerId), LogLevel.DEBUG);
////	    }
////		m_iPlayerId = realPlayerId;
////		Replication.BumpMe();
////	}
////	
////	void ClientRegisterPlayerId(int playerId)
////	{
////	    if (!playerId)
////	        return;
////	        
////	    Rpc(RPC_RegisterWithPlayerID, playerId);
////	    Print(string.Format("TDL_PLAYERCONTROLLER: Client registering with player ID %1", playerId), LogLevel.DEBUG);
////	}
//	
//	override protected void OnAuthorityReady()
//	{
////	    #ifdef WORKBENCH
////	    Rpc(RPC_RegisterWithPlayerID, 1);
////	    #endif
//	    // Server does nothing - client will handle registration
//	}
//	
//	void ~AG0_TDLController()
//    {
//        AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
//        if (system && m_iPlayerId)
//        {
//            system.UnregisterController(m_iPlayerId);
//        }
//    }
//}