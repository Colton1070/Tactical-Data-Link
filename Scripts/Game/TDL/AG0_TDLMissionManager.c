//------------------------------------------------------------------------------------------------
// Missions published from the web planner (GET /api/mod/missions).
//
// One JSON object per mission. Branches, steps, groups and points arrive
// as parallel arrays (stepIds[i], stepLabels[i], ... describe step i)
// because the JSON reader takes arrays of scalars but not arrays of
// objects. Cross-references are indexes into the other arrays, -1 for
// none.
//------------------------------------------------------------------------------------------------

enum AG0_ETDLMissionStepState
{
	PENDING = 0,
	CALLED = 1,
	SKIPPED = 2
}

//------------------------------------------------------------------------------------------------
class AG0_TDLMissionBrief
{
	string m_sId;
	string m_sName;
	int m_iNetworkId = -1;
	//! 1 while the plan is locked: the mission can be run but not changed.
	int m_iLocked;
	//! Unix seconds. 0 while the mission is on call.
	int m_iHHour;
	string m_sStatement;
	//! Index into m_aBranchIds of the branch being executed, -1 when the plan has none.
	int m_iActiveBranch = -1;

	ref array<string> m_aBranchIds = {};
	ref array<string> m_aBranchNames = {};

	// Steps of every branch, each branch's in running order.
	ref array<string> m_aStepIds = {};
	ref array<int> m_aStepBranch = {};
	ref array<string> m_aStepLabels = {};
	//! Seconds from H-hour. Only meaningful where m_aStepTimed is 1.
	ref array<int> m_aStepOffsets = {};
	//! 1 = the step has a time, 0 = on order.
	ref array<int> m_aStepTimed = {};
	ref array<string> m_aStepKeyCalls = {};
	//! Index into m_aGroupIds, -1 when the step is everyone's.
	ref array<int> m_aStepGroup = {};
	ref array<int> m_aStepStates = {};
	//! Unix seconds the step was called. 0 if it has not been.
	ref array<int> m_aStepCalledAt = {};
	ref array<string> m_aStepCalledBy = {};

	ref array<string> m_aGroupIds = {};
	ref array<string> m_aGroupNames = {};
	//! 0 ground, 1 aviation.
	ref array<int> m_aGroupKinds = {};
	//! Member names of each group, comma-separated.
	ref array<string> m_aGroupMembers = {};

	// Control points of the branch being executed.
	ref array<string> m_aPointIds = {};
	ref array<string> m_aPointTypes = {};
	ref array<string> m_aPointLabels = {};
	ref array<float> m_aPointX = {};
	ref array<float> m_aPointZ = {};
	//! Index into m_aStepIds, -1 when the point belongs to no step.
	ref array<int> m_aPointStep = {};

	//------------------------------------------------------------------------------------------------
	bool IsLocked()
	{
		return m_iLocked != 0;
	}

	//------------------------------------------------------------------------------------------------
	int GetBranchCount()
	{
		return m_aBranchIds.Count();
	}

	//------------------------------------------------------------------------------------------------
	int FindStep(string stepId)
	{
		return m_aStepIds.Find(stepId);
	}

	//------------------------------------------------------------------------------------------------
	//! Indexes of a branch's steps, in running order.
	void GetBranchSteps(int branch, notnull array<int> outSteps)
	{
		outSteps.Clear();
		for (int i = 0; i < m_aStepIds.Count(); i++)
		{
			if (m_aStepBranch[i] == branch)
				outSteps.Insert(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Step the branch is waiting on, as an index into the step arrays.
	//! -1 when every step of the branch is done.
	int GetNextPendingStep(int branch)
	{
		for (int i = 0; i < m_aStepIds.Count(); i++)
		{
			if (m_aStepBranch[i] == branch && m_aStepStates[i] == AG0_ETDLMissionStepState.PENDING)
				return i;
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	string GetGroupName(int groupIndex)
	{
		if (groupIndex < 0 || groupIndex >= m_aGroupNames.Count())
			return string.Empty;
		return m_aGroupNames[groupIndex];
	}

	//------------------------------------------------------------------------------------------------
	//! A payload cut short in transit would leave the parallel arrays at
	//! different lengths, and every reader indexes them together.
	bool IsConsistent()
	{
		if (m_aBranchNames.Count() != m_aBranchIds.Count())
			return false;

		int steps = m_aStepIds.Count();
		if (m_aStepBranch.Count() != steps || m_aStepLabels.Count() != steps)
			return false;
		if (m_aStepOffsets.Count() != steps || m_aStepTimed.Count() != steps)
			return false;
		if (m_aStepKeyCalls.Count() != steps || m_aStepGroup.Count() != steps)
			return false;
		if (m_aStepStates.Count() != steps || m_aStepCalledAt.Count() != steps)
			return false;
		if (m_aStepCalledBy.Count() != steps)
			return false;

		int groups = m_aGroupIds.Count();
		if (m_aGroupNames.Count() != groups || m_aGroupKinds.Count() != groups)
			return false;
		if (m_aGroupMembers.Count() != groups)
			return false;

		int points = m_aPointIds.Count();
		if (m_aPointTypes.Count() != points || m_aPointLabels.Count() != points)
			return false;
		if (m_aPointX.Count() != points || m_aPointZ.Count() != points)
			return false;
		return m_aPointStep.Count() == points;
	}
}

//------------------------------------------------------------------------------------------------
//! Holds the missions. The server keeps every mission published to it and
//! hands each player the ones on their networks, with the group that
//! player is in; a client keeps whatever the server last sent it.
class AG0_TDLMissionManager
{
	//! First character of the line of a push that is not a mission: it
	//! tells the receiving player which group they are in on each one.
	protected static const string GROUPS_LINE_PREFIX = "@";

	protected ref array<ref AG0_TDLMissionBrief> m_aMissions = {};
	//! Parallel to m_aMissions. Kept so the server relays exactly what the API sent.
	protected ref array<string> m_aRawJsons = {};
	protected string m_sLastSyncHash;

	//! Server: "missionId|groupId|identityId" for every group member the
	//! API could put a game identity to. Never sent to clients as is.
	protected ref array<string> m_aMembers = {};

	//! Client: missionId → id of the group this player is in.
	protected ref map<string, string> m_mMyGroups = new map<string, string>();
	//! Client: the server can take mission edits from this player. False
	//! on a server without the web API, or while the player is on no network.
	protected bool m_bAvailable;

	protected ref ScriptInvoker m_OnMissionsChanged = new ScriptInvoker();

	// Client-side chunk reassembly
	protected string m_sAppliedPushHash;
	protected string m_sBufferedPushHash;
	protected int m_iExpectedChunks;
	protected int m_iReceivedChunks;
	protected ref array<string> m_aChunkBuffer;
	protected ref array<bool> m_aChunkReceived;

	//! Server clock minus this machine's clock, in seconds. The mission
	//! clock has to read the same for everyone, and a player's PC clock
	//! can be minutes out.
	protected int m_iClockOffset;

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnMissionsChanged()
	{
		return m_OnMissionsChanged;
	}

	//------------------------------------------------------------------------------------------------
	string GetLastSyncHash()
	{
		return m_sLastSyncHash;
	}

	//------------------------------------------------------------------------------------------------
	int GetMissionCount()
	{
		return m_aMissions.Count();
	}

	//------------------------------------------------------------------------------------------------
	AG0_TDLMissionBrief GetMission(int index)
	{
		if (index < 0 || index >= m_aMissions.Count())
			return null;
		return m_aMissions[index];
	}

	//------------------------------------------------------------------------------------------------
	AG0_TDLMissionBrief FindMission(string missionId)
	{
		foreach (AG0_TDLMissionBrief brief : m_aMissions)
		{
			if (brief.m_sId == missionId)
				return brief;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Client: whether missions can be made and changed from here.
	bool IsAvailable()
	{
		return m_bAvailable;
	}

	//------------------------------------------------------------------------------------------------
	//! Client: id of the group this player is in on a mission, empty when in none.
	string GetMyGroupId(string missionId)
	{
		string groupId;
		m_mMyGroups.Find(missionId, groupId);
		return groupId;
	}

	//------------------------------------------------------------------------------------------------
	//! Current time on the server's clock, unix seconds.
	int GetNow()
	{
		return System.GetUnixTime() + m_iClockOffset;
	}

	//------------------------------------------------------------------------------------------------
	//! Never returns null: a missing key reads as an empty array, which
	//! the length check in IsConsistent then judges with the rest.
	protected array<string> ReadStrings(JsonLoadContext json, string key)
	{
		array<string> values = {};
		if (json.ReadValue(key, values) && values)
			return values;
		return new array<string>();
	}

	//------------------------------------------------------------------------------------------------
	protected array<int> ReadInts(JsonLoadContext json, string key)
	{
		array<int> values = {};
		if (json.ReadValue(key, values) && values)
			return values;
		return new array<int>();
	}

	//------------------------------------------------------------------------------------------------
	protected array<float> ReadFloats(JsonLoadContext json, string key)
	{
		array<float> values = {};
		if (json.ReadValue(key, values) && values)
			return values;
		return new array<float>();
	}

	//------------------------------------------------------------------------------------------------
	AG0_TDLMissionBrief ParseBrief(string briefJson)
	{
		JsonLoadContext json = new JsonLoadContext();
		if (!json.LoadFromString(briefJson))
			return null;

		AG0_TDLMissionBrief brief = new AG0_TDLMissionBrief();
		if (!json.ReadValue("id", brief.m_sId) || brief.m_sId.IsEmpty())
			return null;

		json.ReadValue("name", brief.m_sName);
		json.ReadValue("networkId", brief.m_iNetworkId);
		json.ReadValue("locked", brief.m_iLocked);
		json.ReadValue("hHour", brief.m_iHHour);
		json.ReadValue("statement", brief.m_sStatement);
		json.ReadValue("activeBranch", brief.m_iActiveBranch);

		brief.m_aBranchIds = ReadStrings(json, "branchIds");
		brief.m_aBranchNames = ReadStrings(json, "branchNames");

		brief.m_aStepIds = ReadStrings(json, "stepIds");
		brief.m_aStepBranch = ReadInts(json, "stepBranch");
		brief.m_aStepLabels = ReadStrings(json, "stepLabels");
		brief.m_aStepOffsets = ReadInts(json, "stepOffsets");
		brief.m_aStepTimed = ReadInts(json, "stepTimed");
		brief.m_aStepKeyCalls = ReadStrings(json, "stepKeyCalls");
		brief.m_aStepGroup = ReadInts(json, "stepGroup");
		brief.m_aStepStates = ReadInts(json, "stepStates");
		brief.m_aStepCalledAt = ReadInts(json, "stepCalledAt");
		brief.m_aStepCalledBy = ReadStrings(json, "stepCalledBy");

		brief.m_aGroupIds = ReadStrings(json, "groupIds");
		brief.m_aGroupNames = ReadStrings(json, "groupNames");
		brief.m_aGroupKinds = ReadInts(json, "groupKinds");
		brief.m_aGroupMembers = ReadStrings(json, "groupMembers");

		brief.m_aPointIds = ReadStrings(json, "pointIds");
		brief.m_aPointTypes = ReadStrings(json, "pointTypes");
		brief.m_aPointLabels = ReadStrings(json, "pointLabels");
		brief.m_aPointX = ReadFloats(json, "pointX");
		brief.m_aPointZ = ReadFloats(json, "pointZ");
		brief.m_aPointStep = ReadInts(json, "pointStep");

		if (!brief.IsConsistent())
		{
			Print(string.Format("[TDL_MISSIONS] Mission %1 dropped: its arrays differ in length", brief.m_sId), LogLevel.WARNING);
			return null;
		}
		if (brief.m_iActiveBranch >= brief.GetBranchCount())
			brief.m_iActiveBranch = -1;
		return brief;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReplaceAll(array<string> briefJsons)
	{
		m_aMissions.Clear();
		m_aRawJsons.Clear();
		foreach (string briefJson : briefJsons)
		{
			if (briefJson.IsEmpty())
				continue;
			AG0_TDLMissionBrief brief = ParseBrief(briefJson);
			if (!brief)
				continue;
			m_aMissions.Insert(brief);
			m_aRawJsons.Insert(briefJson);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server: take the body of GET /api/mod/missions.
	//! @return true when the stored missions changed.
	bool ParseMissionsResponse(string jsonData)
	{
		JsonLoadContext json = new JsonLoadContext();
		if (!json.LoadFromString(jsonData))
		{
			Print("[TDL_MISSIONS] Missions response is not valid JSON", LogLevel.WARNING);
			return false;
		}

		string syncHash;
		if (!json.ReadValue("syncHash", syncHash))
			return false;
		if (syncHash == m_sLastSyncHash)
			return false;

		array<string> briefJsons = {};
		json.ReadValue("missions", briefJsons);

		ReplaceAll(briefJsons);
		m_aMembers = ReadStrings(json, "members");
		m_sLastSyncHash = syncHash;
		m_OnMissionsChanged.Invoke();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: which group this player is in on each mission, as the
	//! line of a push that says so. The identity never leaves the server.
	protected string BuildGroupsLine(string identityId)
	{
		string line = GROUPS_LINE_PREFIX;
		if (identityId.IsEmpty())
			return line;

		string wanted = identityId;
		wanted.ToLower();

		bool first = true;
		foreach (string member : m_aMembers)
		{
			array<string> parts = {};
			member.Split("|", parts, false);
			if (parts.Count() != 3 || parts[2] != wanted)
				continue;

			if (!first)
				line = line + ",";
			line = line + parts[0] + "|" + parts[1];
			first = false;
		}
		return line;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: everything one player is sent. The first line says which
	//! groups they are in; each line after it is a mission on one of
	//! their networks.
	string GetPackedMissionsForPlayer(set<int> networkIds, string identityId)
	{
		string packed = BuildGroupsLine(identityId);
		if (!networkIds)
			return packed;

		for (int i = 0; i < m_aMissions.Count(); i++)
		{
			if (!networkIds.Contains(m_aMissions[i].m_iNetworkId))
				continue;
			packed = packed + "\n" + m_aRawJsons[i];
		}
		return packed;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: names what GetPackedMissionsForPlayer would send without
	//! building it, so an unchanged player costs no RPC. The stored hash
	//! alone cannot stand in for this: it covers every mission on the
	//! server, and a player who changes network gets a different subset
	//! under the same hash. Empty when the player's networks carry none.
	string GetPushKeyForNetworks(set<int> networkIds)
	{
		string key;
		if (!networkIds || networkIds.IsEmpty())
			return key;

		foreach (AG0_TDLMissionBrief brief : m_aMissions)
		{
			if (!networkIds.Contains(brief.m_iNetworkId))
				continue;
			key = key + "|" + brief.m_sId;
		}
		if (key.IsEmpty())
			return key;
		return m_sLastSyncHash + key;
	}

	//------------------------------------------------------------------------------------------------
	//! Client: read the line of a push that says which groups this player is in.
	protected void ReadGroupsLine(string line)
	{
		m_mMyGroups.Clear();
		if (line.Length() <= 1)
			return;

		string body = line.Substring(1, line.Length() - 1);
		array<string> entries = {};
		body.Split(",", entries, true);
		foreach (string entry : entries)
		{
			array<string> parts = {};
			entry.Split("|", parts, false);
			if (parts.Count() == 2)
				m_mMyGroups.Set(parts[0], parts[1]);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Client: one chunk of a push from the server. Chunks of one push
	//! share a hash; a different hash abandons a transfer in progress,
	//! since the server re-sends from the first chunk.
	//! An empty assembled payload means the player is on no network, or
	//! the server has no web API: it clears the local missions.
	void ReceiveChunk(string pushHash, int serverTime, int totalChunks, int chunkIndex, string chunkData)
	{
		if (!pushHash.IsEmpty() && pushHash == m_sAppliedPushHash)
			return;
		if (totalChunks <= 0)
			return;

		if (m_iExpectedChunks == 0 || pushHash != m_sBufferedPushHash)
		{
			m_sBufferedPushHash = pushHash;
			m_iExpectedChunks = totalChunks;
			m_iReceivedChunks = 0;
			m_aChunkBuffer = new array<string>();
			m_aChunkBuffer.Resize(totalChunks);
			m_aChunkReceived = new array<bool>();
			m_aChunkReceived.Resize(totalChunks);
		}

		if (chunkIndex < 0 || chunkIndex >= m_iExpectedChunks)
			return;

		if (!m_aChunkReceived[chunkIndex])
		{
			m_aChunkReceived[chunkIndex] = true;
			m_iReceivedChunks = m_iReceivedChunks + 1;
		}
		m_aChunkBuffer[chunkIndex] = chunkData;

		if (m_iReceivedChunks < m_iExpectedChunks)
			return;

		string payload;
		for (int i = 0; i < m_iExpectedChunks; i++)
		{
			payload = payload + m_aChunkBuffer[i];
		}

		m_aChunkBuffer = null;
		m_aChunkReceived = null;
		m_sBufferedPushHash = string.Empty;
		m_iExpectedChunks = 0;
		m_iReceivedChunks = 0;

		m_sAppliedPushHash = pushHash;
		if (serverTime > 0)
			m_iClockOffset = serverTime - System.GetUnixTime();

		array<string> lines = {};
		if (!payload.IsEmpty())
			payload.Split("\n", lines, true);

		m_bAvailable = !payload.IsEmpty();
		m_mMyGroups.Clear();

		array<string> briefJsons = {};
		foreach (string line : lines)
		{
			if (line.Substring(0, 1) == GROUPS_LINE_PREFIX)
				ReadGroupsLine(line);
			else
				briefJsons.Insert(line);
		}
		ReplaceAll(briefJsons);

		Print(string.Format("[TDL_MISSIONS_CLIENT] %1 missions (hash: %2)", m_aMissions.Count(), pushHash), LogLevel.DEBUG);
		m_OnMissionsChanged.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	//! Client: show a step's new state before the server confirms it. The
	//! next push replaces it with the real state either way. Does not fire
	//! the change invoker — the caller is the only one who needs to know,
	//! and it is usually inside the click that caused this.
	bool SetStepStateLocal(string missionId, string stepId, int state)
	{
		AG0_TDLMissionBrief brief = FindMission(missionId);
		if (!brief)
			return false;
		int index = brief.FindStep(stepId);
		if (index < 0)
			return false;

		brief.m_aStepStates[index] = state;
		if (state == AG0_ETDLMissionStepState.PENDING)
			brief.m_aStepCalledAt[index] = 0;
		else
			brief.m_aStepCalledAt[index] = GetNow();
		return true;
	}
}
