//------------------------------------------------------------------------------------------------
// AG0_TDLLinkCommand.c
// Server command for TDL account linking: #tdl link <code>
//------------------------------------------------------------------------------------------------

// TDL Subcommand Args
enum AG0_ETDLSubcommandArg
{
	ETSA_MISSING,
	ETSA_HELP,
	ETSA_LINK,
	ETSA_STATUS,
	ETSA_IDENTITY,
	ETSA_MARKING,
	ETSA_MARKINGS,
	ETSA_MARKING_IMAGE,
	ETSA_MARKING_COLOR
}

// TDL Link Args
enum AG0_ETDLLinkArgs
{
	ETLA_COMMAND = 0,
	ETLA_SUBCOMMAND,
	ETLA_CODE,
	ETLA_END
}

// Callback state enum
enum AG0_ETDLLinkState
{
	PENDING,
	SUCCESS,
	ERROR,
	TIMEOUT
}

//------------------------------------------------------------------------------------------------
// REST Callback for account linking
//------------------------------------------------------------------------------------------------
class AG0_TDLLinkCallback : RestCallback
{
	AG0_ETDLLinkState m_eState = AG0_ETDLLinkState.PENDING;
	int m_iHttpCode = 0;
	string m_sResponseData;
	
	//------------------------------------------------------------------------------------------------
	void AG0_TDLLinkCallback()
	{
		SetOnSuccess(OnSuccessHandler);
		SetOnError(OnErrorHandler);
	}
	
	//------------------------------------------------------------------------------------------------
	void OnSuccessHandler(RestCallback cb)
	{
		m_sResponseData = cb.GetData();
		m_iHttpCode = cb.GetHttpCode();
		m_eState = AG0_ETDLLinkState.SUCCESS;
		Print(string.Format("[TDL_LINK] Success - HTTP %1", m_iHttpCode), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	void OnErrorHandler(RestCallback cb)
	{
		m_iHttpCode = cb.GetHttpCode();
		
		if (cb.GetRestResult() == ERestResult.EREST_ERROR_TIMEOUT)
		{
			m_eState = AG0_ETDLLinkState.TIMEOUT;
			Print("[TDL_LINK] Request timed out", LogLevel.WARNING);
		}
		else
		{
			m_eState = AG0_ETDLLinkState.ERROR;
			Print(string.Format("[TDL_LINK] Error - HTTP %1", m_iHttpCode), LogLevel.WARNING);
		}
	}
}

//------------------------------------------------------------------------------------------------
// TDL Server Command
//------------------------------------------------------------------------------------------------
class AG0_TDLLinkCommand : ScrServerCommand
{
	protected ref AG0_TDLLinkCallback m_Callback;
	protected AG0_ETDLSubcommandArg m_eSubcommandArg;
	
	protected string m_sLinkCode;
	protected string m_sPlayerName;
	protected string m_sIdentityId;
	protected int m_iPlayerId;
	
	//------------------------------------------------------------------------------------------------
	override string GetKeyword()
	{
		return "tdl";
	}
	
	//------------------------------------------------------------------------------------------------
	override bool IsServerSide()
	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	override int RequiredRCONPermission()
	{
		return 0;
	}
	
	//------------------------------------------------------------------------------------------------
	override int RequiredChatPermission()
	{
		return 0;
	}
	
	//------------------------------------------------------------------------------------------------
	override ref ScrServerCmdResult OnChatServerExecution(array<string> argv, int playerId)
	{
		return HandleCommand(argv, playerId);
	}
	
	//------------------------------------------------------------------------------------------------
	override ref ScrServerCmdResult OnChatClientExecution(array<string> argv, int playerId)
	{
		return ScrServerCmdResult(string.Empty, EServerCmdResultType.OK);
	}
	
	//------------------------------------------------------------------------------------------------
	override ref ScrServerCmdResult OnRCONExecution(array<string> argv)
	{
		return ScrServerCmdResult("Account linking requires in-game execution", EServerCmdResultType.ERR);
	}
	
	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleCommand(array<string> argv, int playerId)
	{
		Print(string.Format("[TDL_LINK] Command received from player %1, args: %2", playerId, argv.Count()), LogLevel.DEBUG);
		
		m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_MISSING;
		m_iPlayerId = playerId;
		
		if (argv.Count() > 1)
		{
			string sub = argv[1];
			sub.ToLower();
			
			if (sub == "link")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_LINK;
			else if (sub == "status")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_STATUS;
			else if (sub == "identity" || sub == "id")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_IDENTITY;
			else if (sub == "marking" || sub == "patch" || sub == "tag")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_MARKING;
			else if (sub == "markings")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_MARKINGS;
			else if (sub == "mimage")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_MARKING_IMAGE;
			else if (sub == "mcolor" || sub == "mcolour")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_MARKING_COLOR;
			else if (sub == "help")
				m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_HELP;
		}
		
		if (m_eSubcommandArg == AG0_ETDLSubcommandArg.ETSA_MISSING)
			m_eSubcommandArg = AG0_ETDLSubcommandArg.ETSA_HELP;
		
		switch (m_eSubcommandArg)
		{
			case AG0_ETDLSubcommandArg.ETSA_HELP:
				return ScrServerCmdResult("TDL Commands:\n#tdl link <code> - Link account\n#tdl status - API status\n#tdl identity - Show your ID\n#tdl patch <text> - Set your patch (AG0 or AG0/TEST)\n#tdl marking <slot> <text|clear> - Set any marking slot\n#tdl markings - Show your markings\n#tdl mimage <slot> <test|clear> - Image marking test / clear\n#tdl mcolor <slot> <bg|text> <#RRGGBB|clear> - Marking colours", EServerCmdResultType.OK);
			
			case AG0_ETDLSubcommandArg.ETSA_LINK:
				return HandleLink(argv, playerId);
			
			case AG0_ETDLSubcommandArg.ETSA_STATUS:
				return HandleStatus();
			
			case AG0_ETDLSubcommandArg.ETSA_IDENTITY:
				return HandleIdentity(playerId);

			case AG0_ETDLSubcommandArg.ETSA_MARKING:
				return HandleMarking(argv, playerId);

			case AG0_ETDLSubcommandArg.ETSA_MARKINGS:
				return HandleMarkings(playerId);

			case AG0_ETDLSubcommandArg.ETSA_MARKING_IMAGE:
				return HandleMarkingImage(argv, playerId);

			case AG0_ETDLSubcommandArg.ETSA_MARKING_COLOR:
				return HandleMarkingColor(argv, playerId);
		}
		
		return ScrServerCmdResult(string.Empty, EServerCmdResultType.ERR);
	}
	
	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleLink(array<string> argv, int playerId)
	{
		if (argv.Count() < AG0_ETDLLinkArgs.ETLA_END)
			return ScrServerCmdResult("Usage: #tdl link <code>", EServerCmdResultType.PARAMETERS);
		
		m_sLinkCode = argv[AG0_ETDLLinkArgs.ETLA_CODE];
		m_sLinkCode.ToUpper();
		
		// Validate code format
		if (m_sLinkCode.Length() < 4 || m_sLinkCode.Length() > 16)
			return ScrServerCmdResult("Invalid code length (4-16 chars)", EServerCmdResultType.ERR);
		
		for (int i = 0; i < m_sLinkCode.Length(); i++)
		{
			int c = m_sLinkCode.ToAscii(i);
			bool valid = (c >= 48 && c <= 57) || (c >= 65 && c <= 90);
			if (!valid)
				return ScrServerCmdResult("Invalid code format (A-Z, 0-9 only)", EServerCmdResultType.ERR);
		}
		
		// Get TDL System
		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		if (!tdlSystem)
		{
			Print("[TDL_LINK] TDL System not available", LogLevel.ERROR);
			return ScrServerCmdResult("TDL system unavailable", EServerCmdResultType.ERR);
		}
		
		// Get API Manager
		AG0_TDLApiManager apiManager = tdlSystem.GetApiManager();
		if (!apiManager)
		{
			Print("[TDL_LINK] API Manager not available", LogLevel.ERROR);
			return ScrServerCmdResult("API not configured", EServerCmdResultType.ERR);
		}
		
		if (!apiManager.CanCommunicate())
		{
			Print("[TDL_LINK] API cannot communicate", LogLevel.WARNING);
			return ScrServerCmdResult("API not connected", EServerCmdResultType.ERR);
		}
		
		// Get player identity using TDL System helper methods
		m_sIdentityId = tdlSystem.GetPlayerIdentityId(playerId);
		if (m_sIdentityId.IsEmpty())
		{
			Print(string.Format("[TDL_LINK] Could not get identity for player %1", playerId), LogLevel.WARNING);
			return ScrServerCmdResult("Could not retrieve identity", EServerCmdResultType.ERR);
		}
		
		m_sPlayerName = GetGame().GetPlayerManager().GetPlayerName(playerId);
		PlatformKind platform = tdlSystem.GetPlayerPlatform(playerId);
		
		Print(string.Format("[TDL_LINK] Player %1 (%2) requesting link with code %3", 
			m_sPlayerName, m_sIdentityId.Substring(0, 8), m_sLinkCode), LogLevel.NORMAL);
		
		// Create callback and submit
		m_Callback = new AG0_TDLLinkCallback();
		
		bool success = apiManager.SubmitAccountLink(m_Callback, m_sLinkCode, m_sIdentityId, m_sPlayerName, playerId, platform);
		if (!success)
		{
			Print("[TDL_LINK] Failed to submit link request", LogLevel.ERROR);
			return ScrServerCmdResult("Failed to submit request", EServerCmdResultType.ERR);
		}
		
		return ScrServerCmdResult("Linking account...", EServerCmdResultType.PENDING);
	}
	
	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleStatus()
	{
		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		if (!tdlSystem)
			return ScrServerCmdResult("TDL: Not initialized", EServerCmdResultType.OK);
		
		AG0_TDLApiManager apiManager = tdlSystem.GetApiManager();
		if (!apiManager)
			return ScrServerCmdResult("TDL API: Not configured", EServerCmdResultType.OK);
		
		string status = "TDL API: ";
		if (apiManager.CanCommunicate())
			status += "Connected (" + apiManager.GetServerName() + ")";
		else if (!apiManager.IsApiKeyValid())
			status += "Invalid API key";
		else if (!apiManager.IsEnabled())
			status += "Disabled";
		else
			status += "Not ready";
		
		return ScrServerCmdResult(status, EServerCmdResultType.OK);
	}
	
	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleIdentity(int playerId)
	{
		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		if (!tdlSystem)
			return ScrServerCmdResult("TDL system unavailable", EServerCmdResultType.ERR);
		
		string identityId = tdlSystem.GetPlayerIdentityId(playerId);
		if (identityId.IsEmpty())
			return ScrServerCmdResult("Could not retrieve identity", EServerCmdResultType.ERR);
		
		string shortId = identityId.Substring(0, 8) + "...";
		
		PlatformKind platform = tdlSystem.GetPlayerPlatform(playerId);
		string platformName = "Unknown";
		switch (platform)
		{
			case PlatformKind.STEAM: platformName = "Steam"; break;
			case PlatformKind.XBOX: platformName = "Xbox"; break;
			case PlatformKind.PSN: platformName = "PlayStation"; break;
		}
		
		return ScrServerCmdResult(string.Format("Identity: %1 (%2)", shortId, platformName), EServerCmdResultType.OK);
	}
	
	//------------------------------------------------------------------------------------------------
	//! #tdl patch <text>            — slot "patch"
	//! #tdl marking <slot> <text>   — any slot; "clear" empties it
	//! Runs on the server, so it goes straight into the registry: stamped on the player's
	//! items now, kept for the session, and written back to the API — which persists it only
	//! when the identity is linked to an account.
	protected ScrServerCmdResult HandleMarking(array<string> argv, int playerId)
	{
		string sub = argv[1];
		sub.ToLower();

		string slotKey = "patch";
		int textStart = 2;
		if (sub == "marking")
		{
			if (argv.Count() < 4)
				return ScrServerCmdResult("Usage: #tdl marking <slot> <text|clear>", EServerCmdResultType.PARAMETERS);
			slotKey = argv[2];
			slotKey.ToLower();
			textStart = 3;
		}
		else if (argv.Count() < 3)
		{
			return ScrServerCmdResult("Usage: #tdl patch <text|clear>  (use / for a second row)", EServerCmdResultType.PARAMETERS);
		}

		TDL_EMarkingSlot slot;
		if (!TDL_MarkingSlots.FromKey(slotKey, slot))
			return ScrServerCmdResult("Unknown slot. Try: patch, nametape, helmet_band, blood_type, device_plate, vehicle_tail", EServerCmdResultType.ERR);

		string raw = "";
		for (int i = textStart; i < argv.Count(); i++)
		{
			if (i > textStart)
				raw += " ";
			raw += argv[i];
		}
		string lowered = raw;
		lowered.ToLower();
		lowered = lowered.Trim();

		string text = "";
		if (lowered != "clear" && lowered != "none" && lowered != "off")
		{
			text = TDL_MarkingComponent.Sanitize(raw, slot);
			if (text.IsEmpty())
				return ScrServerCmdResult("Nothing usable in that text (A-Z 0-9 space - . and / between rows)", EServerCmdResultType.ERR);
		}

		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
		if (!tdlSystem || !reg)
			return ScrServerCmdResult("TDL system unavailable", EServerCmdResultType.ERR);

		string identityId = tdlSystem.GetPlayerIdentityId(playerId);
		if (identityId.IsEmpty())
			return ScrServerCmdResult("Could not retrieve identity", EServerCmdResultType.ERR);

		reg.EnsureSpawnHook();
		reg.OnLocalSet(identityId, TDL_MarkingSlots.ToKey(slot), text, true);

		if (text.IsEmpty())
			return ScrServerCmdResult(string.Format("%1 cleared", TDL_MarkingSlots.ToKey(slot)), EServerCmdResultType.OK);
		return ScrServerCmdResult(string.Format("%1 set to %2 (saved to your account if linked, else until you leave)", TDL_MarkingSlots.ToKey(slot), text), EServerCmdResultType.OK);
	}

	//------------------------------------------------------------------------------------------------
	//! #tdl mimage <slot> <test|clear> — session-only image marking. "test" draws a local
	//! checkerboard on every client with no API traffic. Real images are set on the site
	//! (ids are content hashes of other players' uploads, so they are not accepted here).
	protected ScrServerCmdResult HandleMarkingImage(array<string> argv, int playerId)
	{
		if (argv.Count() < 4)
			return ScrServerCmdResult("Usage: #tdl mimage <slot> <test|clear>", EServerCmdResultType.PARAMETERS);
		string slotKey = argv[2];
		slotKey.ToLower();
		TDL_EMarkingSlot slot;
		if (!TDL_MarkingSlots.FromKey(slotKey, slot))
			return ScrServerCmdResult("Unknown slot. Try: patch, nametape, helmet_band, blood_type, device_plate, vehicle_tail", EServerCmdResultType.ERR);

		string imageId = argv[3];
		string lowered = imageId;
		lowered.ToLower();
		if (lowered == "clear" || lowered == "none" || lowered == "off")
			imageId = "";
		else if (lowered == "test")
			imageId = TDL_MarkingComponent.TEST_IMAGE_ID;
		else
			return ScrServerCmdResult("Images are uploaded at tdl.blufor.info; here only test or clear", EServerCmdResultType.ERR);

		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
		if (!tdlSystem || !reg)
			return ScrServerCmdResult("TDL system unavailable", EServerCmdResultType.ERR);
		string identityId = tdlSystem.GetPlayerIdentityId(playerId);
		if (identityId.IsEmpty())
			return ScrServerCmdResult("Could not retrieve identity", EServerCmdResultType.ERR);

		reg.EnsureSpawnHook();
		reg.OnLocalSetImage(identityId, TDL_MarkingSlots.ToKey(slot), imageId);
		if (imageId.IsEmpty())
			return ScrServerCmdResult(string.Format("%1 image cleared", slotKey), EServerCmdResultType.OK);
		return ScrServerCmdResult(string.Format("%1 image set to %2 (this session)", slotKey, imageId), EServerCmdResultType.OK);
	}

	//------------------------------------------------------------------------------------------------
	//! #tdl mcolor <slot> <bg|text> <#RRGGBB|clear> — one colour key on one slot. Hex only
	//! here; the site has the picker. Written back so a linked player keeps it.
	protected ScrServerCmdResult HandleMarkingColor(array<string> argv, int playerId)
	{
		if (argv.Count() < 5)
			return ScrServerCmdResult("Usage: #tdl mcolor <slot> <bg|text> <#RRGGBB|clear>", EServerCmdResultType.PARAMETERS);
		string slotKey = argv[2];
		slotKey.ToLower();
		TDL_EMarkingSlot slot;
		if (!TDL_MarkingSlots.FromKey(slotKey, slot))
			return ScrServerCmdResult("Unknown slot. Try: patch, nametape, helmet_band, blood_type, device_plate, vehicle_tail", EServerCmdResultType.ERR);

		string key = argv[3];
		key.ToLower();
		if (key == "text" || key == "thread")
			key = "fg";
		else if (key == "bg" || key == "background" || key == "fabric")
			key = "bg";
		else
			return ScrServerCmdResult("Second argument is bg or text", EServerCmdResultType.ERR);

		string value = argv[4];
		value.ToLower();
		if (value == "clear" || value == "none" || value == "off" || value == "default")
		{
			value = "";
		}
		else if (value != TDL_MarkingColors.RAINBOW)
		{
			if (value.Length() == 6)
				value = "#" + value;
			int argb;
			if (!TDL_MarkingColors.FromHex(value, argb))
				return ScrServerCmdResult("Colour must be #RRGGBB", EServerCmdResultType.ERR);
		}

		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
		if (!tdlSystem || !reg)
			return ScrServerCmdResult("TDL system unavailable", EServerCmdResultType.ERR);
		string identityId = tdlSystem.GetPlayerIdentityId(playerId);
		if (identityId.IsEmpty())
			return ScrServerCmdResult("Could not retrieve identity", EServerCmdResultType.ERR);

		reg.EnsureSpawnHook();
		reg.OnLocalSetStyle(identityId, TDL_MarkingSlots.ToKey(slot), key, value);
		if (value.IsEmpty())
			return ScrServerCmdResult(string.Format("%1 %2 colour reset", slotKey, key), EServerCmdResultType.OK);
		return ScrServerCmdResult(string.Format("%1 %2 colour set to %3", slotKey, key, value), EServerCmdResultType.OK);
	}

	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleMarkings(int playerId)
	{
		AG0_TDLSystem tdlSystem = AG0_TDLSystem.GetInstance();
		TDL_MarkingRegistry reg = TDL_MarkingRegistry.GetInstance();
		if (!tdlSystem || !reg)
			return ScrServerCmdResult("TDL system unavailable", EServerCmdResultType.ERR);

		string identityId = tdlSystem.GetPlayerIdentityId(playerId);
		array<string> slots;
		reg.GetSlotsInUse(identityId, slots);

		string out_text = "Your markings:";
		int shown = 0;
		foreach (string slotKey : slots)
		{
			string line = reg.DescribeSlot(identityId, slotKey);
			if (line.IsEmpty())
				continue;
			out_text += string.Format("\n%1: %2", slotKey, line);
			shown++;
		}
		if (shown == 0)
			return ScrServerCmdResult("No markings set. #tdl patch <text> or tdl.blufor.info", EServerCmdResultType.OK);
		return ScrServerCmdResult(out_text, EServerCmdResultType.OK);
	}

	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleSuccessfulResult()
	{
		Print(string.Format("[TDL_LINK] Account linked for %1", m_sPlayerName), LogLevel.NORMAL);
		return ScrServerCmdResult("Account linked! Visit tdl.blufor.info", EServerCmdResultType.OK);
	}
	
	//------------------------------------------------------------------------------------------------
	protected ScrServerCmdResult HandleFailure()
	{
		string msg = "Link failed";
		
		switch (m_Callback.m_iHttpCode)
		{
			case 400: msg = "Invalid or expired code"; break;
			case 404: msg = "Code not found"; break;
			case 409: msg = "Already linked to another account"; break;
			case 401: msg = "Server not authorized"; break;
			case 500: msg = "Server error, try again"; break;
		}
		
		Print(string.Format("[TDL_LINK] Link failed for %1: %2 (HTTP %3)", m_sPlayerName, msg, m_Callback.m_iHttpCode), LogLevel.WARNING);
		return ScrServerCmdResult(msg, EServerCmdResultType.ERR);
	}
	
	//------------------------------------------------------------------------------------------------
	override ref ScrServerCmdResult OnUpdate()
	{
		if (!m_Callback)
			return ScrServerCmdResult(string.Empty, EServerCmdResultType.ERR);
		
		switch (m_Callback.m_eState)
		{
			case AG0_ETDLLinkState.SUCCESS: return HandleSuccessfulResult();
			case AG0_ETDLLinkState.PENDING: return ScrServerCmdResult(string.Empty, EServerCmdResultType.PENDING);
			case AG0_ETDLLinkState.TIMEOUT: return ScrServerCmdResult("Request timed out", EServerCmdResultType.ERR);
			case AG0_ETDLLinkState.ERROR:   return HandleFailure();
		}
		
		return ScrServerCmdResult(string.Empty, EServerCmdResultType.ERR);
	}
}