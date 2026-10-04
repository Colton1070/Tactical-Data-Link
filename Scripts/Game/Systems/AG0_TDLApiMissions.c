//------------------------------------------------------------------------------------------------
// REST callbacks for the mission endpoints. The request logic lives on
// AG0_TDLApiManager (PollMissions / SubmitMissionEdit).
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// GET /api/mod/missions
//------------------------------------------------------------------------------------------------
class AG0_TDLApiMissionsCallback : RestCallback
{
	protected AG0_TDLApiManager m_Manager;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLApiMissionsCallback(AG0_TDLApiManager manager)
	{
		m_Manager = manager;
		SetOnSuccess(OnSuccessHandler);
		SetOnError(OnErrorHandler);
	}

	//------------------------------------------------------------------------------------------------
	void OnSuccessHandler(RestCallback cb)
	{
		if (m_Manager)
			m_Manager.OnMissionsPollSuccess(cb.GetData());
	}

	//------------------------------------------------------------------------------------------------
	void OnErrorHandler(RestCallback cb)
	{
		if (!m_Manager)
			return;

		if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
		{
			m_Manager.OnMissionsPollFailed(0);
			return;
		}
		int errorCode = cb.GetHttpCode();
		m_Manager.OnMissionsPollFailed(errorCode);
	}
}

//------------------------------------------------------------------------------------------------
// POST /api/mod/missions/edit — a player changed a mission in game.
// Carries the player so a rejected change can be put right on their
// screen, which may already show it.
//------------------------------------------------------------------------------------------------
class AG0_TDLApiMissionEditCallback : RestCallback
{
	protected AG0_TDLApiManager m_Manager;
	protected int m_iPlayerId;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLApiMissionEditCallback(AG0_TDLApiManager manager, int playerId)
	{
		m_Manager = manager;
		m_iPlayerId = playerId;
		SetOnSuccess(OnSuccessHandler);
		SetOnError(OnErrorHandler);
	}

	//------------------------------------------------------------------------------------------------
	void OnSuccessHandler(RestCallback cb)
	{
		if (m_Manager)
			m_Manager.OnMissionEditSubmitted(m_iPlayerId, true, 200);
	}

	//------------------------------------------------------------------------------------------------
	void OnErrorHandler(RestCallback cb)
	{
		int errorCode = cb.GetHttpCode();
		if (m_Manager)
			m_Manager.OnMissionEditSubmitted(m_iPlayerId, false, errorCode);
	}
}
