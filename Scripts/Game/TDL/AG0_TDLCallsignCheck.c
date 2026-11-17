//! One callsign check in flight. Handler-registration shape, same as the other REST
//! callbacks: the override-OnSuccess variant logs "Function was not set".
class AG0_TDLCallsignCheckCallback : RestCallback
{
	protected int m_iKey;
	//! Plain pointer on purpose: it reads null if the device is deleted before the answer.
	protected AG0_TDLDeviceComponent m_Device;
	protected string m_sAttempted;
	protected string m_sPrevious;

	//------------------------------------------------------------------------------------------------
	void AG0_TDLCallsignCheckCallback(int key, AG0_TDLDeviceComponent device, string attempted, string previous)
	{
		m_iKey = key;
		m_Device = device;
		m_sAttempted = attempted;
		m_sPrevious = previous;
		SetOnSuccess(OnAccepted);
		SetOnError(OnFailed);
	}

	//------------------------------------------------------------------------------------------------
	void OnAccepted(RestCallback cb)
	{
		AG0_TDLCallsignCheck.Release(m_iKey);
	}

	//------------------------------------------------------------------------------------------------
	//! Only a 422 is a verdict. Any other failure leaves the callsign alone: an unreachable
	//! API must not undo what a server without the API would have allowed.
	void OnFailed(RestCallback cb)
	{
		// The player may have typed something else while the answer was in flight; that
		// newer callsign has its own check and is not this one to revert.
		if (cb.GetHttpCode() == 422 && m_Device && m_Device.GetCustomCallsign() == m_sAttempted)
		{
			Print(string.Format("[TDL_CALLSIGN] API rejected callsign on %1; previous restored", m_Device.GetOwner()), LogLevel.WARNING);
			m_Device.RestoreCallsign(m_sPrevious);
		}
		AG0_TDLCallsignCheck.Release(m_iKey);
	}
}

//! Server-only. Asks tdl-api whether a callsign that was just applied may stay.
class AG0_TDLCallsignCheck
{
	protected static const string API_BASE_URL = "https://tdl.blufor.info/api/mod";

	//! The engine holds no reference to a callback, so each one lives here until it answers.
	protected static ref map<int, ref AG0_TDLCallsignCheckCallback> s_Pending = new map<int, ref AG0_TDLCallsignCheckCallback>();
	protected static int s_iNextKey;

	//------------------------------------------------------------------------------------------------
	static void Submit(AG0_TDLDeviceComponent device, string attempted, string previous)
	{
		if (!Replication.IsServer() || !device || attempted.IsEmpty())
			return;
		AG0_TDLSystem system = AG0_TDLSystem.GetInstance();
		if (!system)
			return;
		AG0_TDLApiManager api = system.GetApiManager();
		if (!api || !api.CanCommunicate())
			return;
		RestContext ctx = GetGame().GetRestApi().GetContext(API_BASE_URL);
		if (!ctx)
			return;
		ctx.SetHeaders(string.Format("Authorization,Bearer %1,Content-Type,application/json", api.GetApiKey()));

		JsonSaveContext json = new JsonSaveContext();
		json.WriteValue("callsign", attempted);

		s_iNextKey++;
		AG0_TDLCallsignCheckCallback cb = new AG0_TDLCallsignCheckCallback(s_iNextKey, device, attempted, previous);
		s_Pending.Set(s_iNextKey, cb);
		ctx.POST(cb, "/callsign/check", json.SaveToString());
	}

	//------------------------------------------------------------------------------------------------
	static void Release(int key)
	{
		s_Pending.Remove(key);
	}
}
