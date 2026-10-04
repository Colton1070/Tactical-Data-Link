//------------------------------------------------------------------------------------------------
//! Bound zero-arg to a row's SCR_ModularButtonComponent.m_OnClicked, which
//! cannot say which row fired. One relay per row carries its position.
class AG0_MissionRowClickRelay
{
	protected AG0_ATAKPlugin_Mission m_Plugin;
	protected int m_iRow;

	//------------------------------------------------------------------------------------------------
	void AG0_MissionRowClickRelay(AG0_ATAKPlugin_Mission plugin, int row)
	{
		m_Plugin = plugin;
		m_iRow = row;
	}

	//------------------------------------------------------------------------------------------------
	void OnClick()
	{
		if (m_Plugin)
			m_Plugin.OnRowClicked(m_iRow);
	}
}

//------------------------------------------------------------------------------------------------
//! One line of text asked of the player: a mission, step or group name,
//! a key call, a time. Reuses the network-name dialog preset and content
//! layout, with the title and prompt set per use, so text entry works on
//! console the same way it does there.
class AG0_TDLMissionTextDialog : SCR_ConfigurableDialogUi
{
	protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";

	protected SCR_EditBoxComponent m_InputField;
	protected EditBoxWidget m_EditBox;
	protected string m_sInitialText;

	//------------------------------------------------------------------------------------------------
	static AG0_TDLMissionTextDialog CreateDialog(string title, string message, string initialText)
	{
		AG0_TDLMissionTextDialog dialog = new AG0_TDLMissionTextDialog();
		dialog.m_sInitialText = initialText;
		SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_networkname", dialog);
		dialog.SetTitle(title);
		dialog.SetMessage(message);
		return dialog;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		m_InputField = SCR_EditBoxComponent.GetEditBoxComponent("NetworkNameInput", m_wRoot);
		if (!m_InputField)
			return;

		m_InputField.SetValue(m_sInitialText);

		Widget inputWidget = m_InputField.GetRootWidget();
		if (!inputWidget)
			return;

		GetGame().GetWorkspace().SetFocusedWidget(inputWidget);
		m_EditBox = EditBoxWidget.Cast(inputWidget.FindAnyWidget("EditBox"));
		if (!m_EditBox)
			m_EditBox = EditBoxWidget.Cast(inputWidget);
		if (m_EditBox)
			GetGame().GetCallqueue().CallLater(ActivateEditMode, 50, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void ActivateEditMode()
	{
		if (m_EditBox)
			m_EditBox.ActivateWriteMode();
	}

	//------------------------------------------------------------------------------------------------
	string GetEnteredText()
	{
		if (m_InputField)
			return m_InputField.GetValue().Trim();
		if (m_EditBox)
			return m_EditBox.GetText().Trim();
		return string.Empty;
	}
}

//------------------------------------------------------------------------------------------------
//! Which part of the mission the panel is showing.
enum AG0_EMissionPanelView
{
	RUN,     // The steps of the branch being executed, called from here
	PLAN,    // The branches and their steps, to change them
	STEP,    // One step of the plan
	POINTS,  // The control points on the map
	POINT,   // One control point
	TEAM,    // The groups
	GROUP    // One group
}

//------------------------------------------------------------------------------------------------
//! One row of the panel's list. The whole panel is a list of these: a row
//! is either a heading or a button, and a button names the action a press
//! performs. Building the panel as data first means a push from the server
//! can be compared with what is on screen and applied without tearing the
//! rows down when only their text moved.
class AG0_MissionPanelRow
{
	//! What a press does. Empty for a heading, which is not a button.
	string m_sAction;
	//! What the action applies to: a step, point, group or branch id, or a number as text.
	string m_sArg;
	//! Small line above the main text.
	string m_sTop;
	string m_sText;
	//! Line under the main text; a step's key call.
	string m_sCode;
	//! Right-hand text of the top line.
	string m_sState;
	//! Acts on the second press, so a stray press cannot do it.
	bool m_bConfirm;
	//! For a step in the run view, its index in the brief's step arrays:
	//! such a row's time and state are drawn live against the clock.
	int m_iLiveStep = -1;

	//------------------------------------------------------------------------------------------------
	//! Same key, same row: lets a rebuilt list keep its widgets and focus.
	string GetKey()
	{
		return m_sAction + "|" + m_sArg;
	}
}

//------------------------------------------------------------------------------------------------
//! The mission published to the player's network, run and written from
//! ATAK: its steps against H-hour with their key calls, the branch being
//! executed, the control points on the map and the groups. Anyone on the
//! mission's network can change it from here; the web planner and every
//! other player see the change.
//!
//! Not a device plugin. Missions reach a player through network
//! membership, not through a held radio, so AG0_TDLMenuController adds
//! this one itself instead of reading it from a device's plugin list.
class AG0_ATAKPlugin_Mission : AG0_ATAKPluginBase
{
	protected const ResourceName PANEL_LAYOUT = "{58BAF555957A1FEB}UI/layouts/Menus/TDL/Plugins/Mission/MissionPanel.layout";
	protected const ResourceName ROW_LAYOUT = "{B253FA07960A4565}UI/layouts/Menus/TDL/Plugins/Mission/MissionLineEntry.layout";
	//! The file is named for the comm card it first drew. It is now the
	//! heading row: a label and a value, with nothing to press.
	protected const ResourceName HEADING_LAYOUT = "{E0C494F96EA88A3F}UI/layouts/Menus/TDL/Plugins/Mission/MissionNetEntry.layout";
	protected const ResourceName TOOL_ICON = "{9912A9FC2F06E031}UI/Textures/Icons/tdl_mission.edds";

	//! A pending step this far past its time reads as late. Reporting a
	//! step takes a moment; a few seconds over is not late.
	protected const int LATE_AFTER_SECONDS = 60;
	//! How long a first press stays armed. Calling a step, or anything
	//! that cannot be taken back, takes two presses so that scrolling the
	//! list with a gamepad cannot do it by accident.
	protected const float ARM_SECONDS = 4.0;
	protected const float UPDATE_INTERVAL = 1.0;
	protected const string DIGITS = "0123456789";
	protected const float CREATE_WAIT_SECONDS = 15.0;

	protected Widget m_wPanel;
	protected TextWidget m_wMissionName;
	protected TextWidget m_wMissionClock;
	protected TextWidget m_wHHourText;
	protected Widget m_wSwitchButton;
	protected TextWidget m_wSwitchText;
	protected TextWidget m_wStatementText;
	protected Widget m_wRowList;
	protected Widget m_wTabRun;
	protected TextWidget m_wTabRunText;
	protected TextWidget m_wTabPlanText;
	protected TextWidget m_wTabPointsText;
	protected TextWidget m_wTabTeamText;

	//! The list as it should be shown, and the widgets drawn for it. Parallel.
	protected ref array<ref AG0_MissionPanelRow> m_aRows = {};
	protected ref array<Widget> m_aRowWidgets = {};
	protected ref array<ref AG0_MissionRowClickRelay> m_aRowRelays = {};

	// What the panel is showing. Static because the menu builds a new
	// controller, and with it a new plugin, every time it opens: a player
	// who closes ATAK to look around must come back to the same step.
	protected static AG0_EMissionPanelView s_eView = AG0_EMissionPanelView.RUN;
	protected static string s_sSelectedMissionId;
	//! Branch on show in the plan view. Empty follows the branch being executed.
	protected static string s_sPlanBranchId;
	//! Step open in the step view.
	protected static string s_sStepId;
	//! Step that new control points are placed for. Empty = no step.
	protected static string s_sPointStepId;
	protected static int s_iPointType = 1;
	//! Point open in the point view.
	protected static string s_sPointId;
	//! Group open in the group view.
	protected static string s_sGroupId;

	//! Key of the row waiting for its second press.
	protected string m_sArmedKey;
	//! State the armed step was in at the first press. If someone else
	//! calls it before the second, that press would undo their call.
	protected int m_iArmedStepState;
	protected float m_fArmedTimer;
	protected float m_fUpdateTimer;

	//! What the open text dialog is asking for, about what, and on which mission.
	protected ref AG0_TDLMissionTextDialog m_TextDialog;
	protected string m_sDialogPurpose;
	protected string m_sDialogArg;
	protected string m_sDialogMissionId;
	//! Row to put gamepad focus back on once the dialog has closed, and
	//! how many updates to wait for it to have done so.
	protected string m_sRefocusKey;
	protected int m_iRefocusDelay;

	//! Mission ids held when a new mission was asked for. The one that
	//! then arrives and is not among them is the new one, and is opened.
	protected ref array<string> m_aIdsBeforeCreate;
	//! How much longer to wait for it. A create the API refused never arrives.
	protected float m_fCreateWait;

	protected bool m_bSubscribed;
	//! The list no longer matches what should be shown: the server sent
	//! new missions, or a press changed the view.
	protected bool m_bViewDirty;
	//! Whether the toolbar was last built with this plugin's button in it.
	protected bool m_bToolbarShown;

	//! Control point types as the API names them, and what each is called
	//! on screen. Parallel.
	protected ref array<string> m_aPointTypes = {};
	protected ref array<string> m_aPointTypeNames = {};

	protected ref Color m_cStateNext = new Color(0.2, 0.8, 0.8, 1);
	protected ref Color m_cStateCalled = new Color(0.35, 0.85, 0.45, 1);
	protected ref Color m_cStateWarn = new Color(0.95, 0.7, 0.2, 1);
	protected ref Color m_cStateMuted = new Color(0.6, 0.6, 0.6, 1);
	protected ref Color m_cTimeLive = new Color(0.75, 0.75, 0.75, 1);
	protected ref Color m_cTimeDone = new Color(0.45, 0.45, 0.45, 1);

	//------------------------------------------------------------------------------------------------
	void AG0_ATAKPlugin_Mission()
	{
		m_sPluginID = "mission";
		m_sDisplayName = "Mission";
		m_sToolIcon = TOOL_ICON;

		AddPointType("sp", "SP  START POINT");
		AddPointType("cp", "CP  CHECKPOINT");
		AddPointType("rp", "RP  RELEASE POINT");
		AddPointType("point", "POINT");
		AddPointType("aa", "AA  ASSEMBLY AREA");
		AddPointType("orp", "ORP  OBJECTIVE RALLY POINT");
		AddPointType("rly", "RLY  RALLY POINT");
		AddPointType("sbf", "SBF  SUPPORT BY FIRE");
		AddPointType("ccp", "CCP  CASUALTY COLLECTION");
		AddPointType("acp", "ACP  AIR CONTROL POINT");
		AddPointType("irp", "IRP  INITIAL RALLY POINT");
		AddPointType("tdp", "TDP  TOUCHDOWN POINT");
		AddPointType("lagger", "LAGGER  LAAGER SITE");
	}

	//------------------------------------------------------------------------------------------------
	protected void AddPointType(string type, string name)
	{
		m_aPointTypes.Insert(type);
		m_aPointTypeNames.Insert(name);
	}

	//------------------------------------------------------------------------------------------------
	protected AG0_TDLMissionManager GetMissionManager()
	{
		SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!controller)
			return null;
		return controller.GetTDLMissionManager();
	}

	//------------------------------------------------------------------------------------------------
	//! Whether there is anything this tool could do: a mission to show,
	//! or a server that will take a new one from this player.
	protected bool HasUse()
	{
		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr)
			return false;
		return missionMgr.GetMissionCount() > 0 || missionMgr.IsAvailable();
	}

	//------------------------------------------------------------------------------------------------
	// Toolbar
	//------------------------------------------------------------------------------------------------

	//! No button on a server without the web API, or while the player is
	//! on no network: a tool that can only ever open an empty panel is
	//! clutter there.
	override bool ProvidesToolbarTool()
	{
		return HasUse();
	}

	//------------------------------------------------------------------------------------------------
	override void OnToolActivated(Widget menuRoot)
	{
		if (m_Controller)
			m_Controller.RequestPluginPanel(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Points are placed and moved at the map crosshair.
	override bool WantsMapCrosshair()
	{
		if (!m_wPanel)
			return false;
		return s_eView == AG0_EMissionPanelView.POINTS || s_eView == AG0_EMissionPanelView.POINT;
	}

	//------------------------------------------------------------------------------------------------
	override void FocusPanel()
	{
		if (!m_wPanel)
			return;
		if (FocusFirstButtonRow())
			return;
		if (m_wTabRun)
			GetGame().GetWorkspace().SetFocusedWidget(m_wTabRun);
	}

	//------------------------------------------------------------------------------------------------
	// Menu lifecycle
	//------------------------------------------------------------------------------------------------

	//! RefreshPlugins disables every plugin before it re-enables the ones
	//! that still apply, and can stop before it reaches this one. The
	//! subscription must not outlive that.
	override void OnDisabled()
	{
		Unsubscribe();
	}

	//------------------------------------------------------------------------------------------------
	protected void Unsubscribe()
	{
		if (!m_bSubscribed)
			return;

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (missionMgr)
			missionMgr.GetOnMissionsChanged().Remove(OnMissionsChanged);
		m_bSubscribed = false;
		m_bViewDirty = false;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpened(Widget menuRoot)
	{
		m_bToolbarShown = HasUse();

		if (m_bSubscribed)
			return;

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr)
			return;

		missionMgr.GetOnMissionsChanged().Insert(OnMissionsChanged);
		m_bSubscribed = true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClosed()
	{
		if (m_wPanel)
			OnPanelHidden();

		Unsubscribe();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		if (m_bViewDirty)
		{
			m_bViewDirty = false;
			ApplyChanges();
		}

		if (m_aIdsBeforeCreate)
		{
			m_fCreateWait -= tDelta;
			if (m_fCreateWait <= 0)
				m_aIdsBeforeCreate = null;
		}

		if (!m_wPanel)
			return;

		if (m_iRefocusDelay > 0)
		{
			m_iRefocusDelay = m_iRefocusDelay - 1;
			if (m_iRefocusDelay == 0)
				RefocusAfterDialog();
		}

		if (!m_sArmedKey.IsEmpty())
		{
			m_fArmedTimer -= tDelta;
			if (m_fArmedTimer <= 0)
			{
				m_sArmedKey = string.Empty;
				RefreshLive();
			}
		}

		m_fUpdateTimer += tDelta;
		if (m_fUpdateTimer < UPDATE_INTERVAL)
			return;
		m_fUpdateTimer = 0;

		RefreshLive();
	}

	//------------------------------------------------------------------------------------------------
	//! The server sent a new set of missions. Only noted here and applied
	//! on the next update: on a hosted game this can arrive in the middle
	//! of the press that caused it, and rebuilding the rows there would
	//! remove the row whose click handler is still running.
	protected void OnMissionsChanged()
	{
		m_bViewDirty = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyChanges()
	{
		bool hasUse = HasUse();
		if (hasUse != m_bToolbarShown && m_Controller)
		{
			m_bToolbarShown = hasUse;
			m_Controller.ClearPluginToolbarButtons();
			m_Controller.BuildPluginToolbarButtons();
			m_Controller.OnPluginPanelRebuilt();
		}

		SelectCreatedMission();

		if (m_wPanel)
			RefreshPanel();
	}

	//------------------------------------------------------------------------------------------------
	//! After the player asked for a new mission, open it when it arrives.
	protected void SelectCreatedMission()
	{
		if (!m_aIdsBeforeCreate)
			return;

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr)
			return;

		for (int i = 0; i < missionMgr.GetMissionCount(); i++)
		{
			string id = missionMgr.GetMission(i).m_sId;
			if (m_aIdsBeforeCreate.Contains(id))
				continue;

			s_sSelectedMissionId = id;
			m_aIdsBeforeCreate = null;
			s_sPlanBranchId = string.Empty;
			s_eView = AG0_EMissionPanelView.PLAN;
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	// Side panel
	//------------------------------------------------------------------------------------------------
	override void OnPanelShown(Widget panelRoot)
	{
		if (!panelRoot)
			return;

		// The controller can show a panel that is already up, when the web
		// mirror re-selects the plugin that owns the slot.
		if (m_wPanel)
			OnPanelHidden();

		m_wPanel = GetGame().GetWorkspace().CreateWidgets(PANEL_LAYOUT, panelRoot);
		if (!m_wPanel)
		{
			Print("[TDL_MISSIONS] Mission panel layout failed to load", LogLevel.WARNING);
			return;
		}

		m_wMissionName = TextWidget.Cast(m_wPanel.FindAnyWidget("MissionName"));
		m_wMissionClock = TextWidget.Cast(m_wPanel.FindAnyWidget("MissionClock"));
		m_wHHourText = TextWidget.Cast(m_wPanel.FindAnyWidget("HHourText"));
		m_wSwitchButton = m_wPanel.FindAnyWidget("SwitchButton");
		m_wSwitchText = TextWidget.Cast(m_wPanel.FindAnyWidget("SwitchText"));
		m_wStatementText = TextWidget.Cast(m_wPanel.FindAnyWidget("StatementText"));
		m_wRowList = m_wPanel.FindAnyWidget("RowList");
		m_wTabRun = m_wPanel.FindAnyWidget("TabRun");
		m_wTabRunText = TextWidget.Cast(m_wPanel.FindAnyWidget("TabRunText"));
		m_wTabPlanText = TextWidget.Cast(m_wPanel.FindAnyWidget("TabPlanText"));
		m_wTabPointsText = TextWidget.Cast(m_wPanel.FindAnyWidget("TabPointsText"));
		m_wTabTeamText = TextWidget.Cast(m_wPanel.FindAnyWidget("TabTeamText"));

		SCR_ModularButtonComponent closeComp = FindButton("CloseButton");
		if (closeComp)
			closeComp.m_OnClicked.Insert(OnCloseClicked);

		SCR_ModularButtonComponent switchComp = FindButton("SwitchButton");
		if (switchComp)
			switchComp.m_OnClicked.Insert(OnSwitchClicked);

		SCR_ModularButtonComponent runComp = FindButton("TabRun");
		if (runComp)
			runComp.m_OnClicked.Insert(OnTabRunClicked);

		SCR_ModularButtonComponent planComp = FindButton("TabPlan");
		if (planComp)
			planComp.m_OnClicked.Insert(OnTabPlanClicked);

		SCR_ModularButtonComponent pointsComp = FindButton("TabPoints");
		if (pointsComp)
			pointsComp.m_OnClicked.Insert(OnTabPointsClicked);

		SCR_ModularButtonComponent teamComp = FindButton("TabTeam");
		if (teamComp)
			teamComp.m_OnClicked.Insert(OnTabTeamClicked);

		m_sArmedKey = string.Empty;
		m_fUpdateTimer = 0;
		RefreshPanel();
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_ModularButtonComponent FindButton(string widgetName)
	{
		Widget button = m_wPanel.FindAnyWidget(widgetName);
		if (!button)
			return null;
		return SCR_ModularButtonComponent.FindComponent(button);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPanelHidden()
	{
		ClearRowWidgets();
		m_aRows.Clear();

		if (m_wPanel)
			m_wPanel.RemoveFromHierarchy();

		m_wPanel = null;
		m_wMissionName = null;
		m_wMissionClock = null;
		m_wHHourText = null;
		m_wSwitchButton = null;
		m_wSwitchText = null;
		m_wStatementText = null;
		m_wRowList = null;
		m_wTabRun = null;
		m_wTabRunText = null;
		m_wTabPlanText = null;
		m_wTabPointsText = null;
		m_wTabTeamText = null;
		m_sArmedKey = string.Empty;
		m_sRefocusKey = string.Empty;
		m_iRefocusDelay = 0;

		if (m_Controller)
			m_Controller.UpdateMarkerCrosshairVisibility();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnCloseClicked()
	{
		if (m_Controller)
			m_Controller.RequestPluginPanel(this);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTabRunClicked()
	{
		SetView(AG0_EMissionPanelView.RUN);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTabPlanClicked()
	{
		SetView(AG0_EMissionPanelView.PLAN);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTabPointsClicked()
	{
		SetView(AG0_EMissionPanelView.POINTS);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTabTeamClicked()
	{
		SetView(AG0_EMissionPanelView.TEAM);
	}

	//------------------------------------------------------------------------------------------------
	//! Applied on the next update rather than here: this is usually called
	//! from inside the press of a row that the new view will remove.
	protected void SetView(AG0_EMissionPanelView view)
	{
		s_eView = view;
		m_sArmedKey = string.Empty;
		m_bViewDirty = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Step to the next mission when the player's networks carry several.
	protected void OnSwitchClicked()
	{
		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr || missionMgr.GetMissionCount() < 2)
			return;

		int next = GetSelectedIndex(missionMgr) + 1;
		if (next >= missionMgr.GetMissionCount())
			next = 0;

		s_sSelectedMissionId = missionMgr.GetMission(next).m_sId;
		s_sPlanBranchId = string.Empty;
		s_sPointStepId = string.Empty;
		SetView(AG0_EMissionPanelView.RUN);
	}

	//------------------------------------------------------------------------------------------------
	//! Index of the mission on show. Falls back to the first when the
	//! selected one has been retracted or the player left its network.
	protected int GetSelectedIndex(AG0_TDLMissionManager missionMgr)
	{
		for (int i = 0; i < missionMgr.GetMissionCount(); i++)
		{
			if (missionMgr.GetMission(i).m_sId == s_sSelectedMissionId)
				return i;
		}
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected AG0_TDLMissionBrief GetSelectedMission()
	{
		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr || missionMgr.GetMissionCount() == 0)
			return null;

		AG0_TDLMissionBrief brief = missionMgr.GetMission(GetSelectedIndex(missionMgr));
		if (brief)
			s_sSelectedMissionId = brief.m_sId;
		return brief;
	}

	//------------------------------------------------------------------------------------------------
	//! Branch shown in the plan view: the one picked there while it still
	//! exists, otherwise the one being executed, otherwise the first.
	protected int GetPlanBranch(AG0_TDLMissionBrief brief)
	{
		int picked = brief.m_aBranchIds.Find(s_sPlanBranchId);
		if (picked >= 0)
			return picked;
		if (brief.m_iActiveBranch >= 0)
			return brief.m_iActiveBranch;
		if (brief.GetBranchCount() > 0)
			return 0;
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	// Panel contents
	//------------------------------------------------------------------------------------------------
	protected void RefreshPanel()
	{
		if (!m_wPanel)
			return;

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		AG0_TDLMissionBrief brief = GetSelectedMission();

		array<ref AG0_MissionPanelRow> rows = {};

		if (!brief)
		{
			SetText(m_wMissionName, "No mission");
			SetText(m_wMissionClock, string.Empty);
			SetText(m_wHHourText, "Nothing is published to your network");
			SetShown(m_wSwitchButton, false);
			SetShown(m_wStatementText, false);
			s_eView = AG0_EMissionPanelView.PLAN;

			if (missionMgr && missionMgr.IsAvailable())
				AddButton(rows, "mission_new", string.Empty, "+ NEW MISSION");
			else
				AddHeading(rows, "JOIN A NETWORK TO START ONE", string.Empty);
		}
		else
		{
			SetText(m_wMissionName, brief.m_sName);

			int missionCount = missionMgr.GetMissionCount();
			SetShown(m_wSwitchButton, missionCount > 1);
			if (missionCount > 1)
				SetText(m_wSwitchText, string.Format("%1 / %2  >", GetSelectedIndex(missionMgr) + 1, missionCount));

			bool showStatement = s_eView == AG0_EMissionPanelView.RUN && !brief.m_sStatement.IsEmpty();
			SetShown(m_wStatementText, showStatement);
			SetText(m_wStatementText, brief.m_sStatement);

			BuildRows(brief, missionMgr, rows);
		}

		RefreshTabs();
		ApplyRows(rows);
		RefreshLive();

		if (m_Controller)
			m_Controller.UpdateMarkerCrosshairVisibility();
	}

	//------------------------------------------------------------------------------------------------
	//! The tab of the view on show is bracketed. Its colour is not used
	//! for this: the button's hover effect owns the text colour.
	protected void RefreshTabs()
	{
		bool run = s_eView == AG0_EMissionPanelView.RUN;
		bool plan = s_eView == AG0_EMissionPanelView.PLAN || s_eView == AG0_EMissionPanelView.STEP;
		bool points = s_eView == AG0_EMissionPanelView.POINTS || s_eView == AG0_EMissionPanelView.POINT;
		bool team = s_eView == AG0_EMissionPanelView.TEAM || s_eView == AG0_EMissionPanelView.GROUP;

		SetText(m_wTabRunText, TabLabel("RUN", run));
		SetText(m_wTabPlanText, TabLabel("PLAN", plan));
		SetText(m_wTabPointsText, TabLabel("POINTS", points));
		SetText(m_wTabTeamText, TabLabel("TEAM", team));
	}

	//------------------------------------------------------------------------------------------------
	protected string TabLabel(string label, bool active)
	{
		if (active)
			return "[ " + label + " ]";
		return label;
	}

	//------------------------------------------------------------------------------------------------
	protected AG0_MissionPanelRow AddButton(array<ref AG0_MissionPanelRow> rows, string action, string arg, string text)
	{
		AG0_MissionPanelRow row = new AG0_MissionPanelRow();
		row.m_sAction = action;
		row.m_sArg = arg;
		row.m_sText = text;
		rows.Insert(row);
		return row;
	}

	//------------------------------------------------------------------------------------------------
	//! A button that acts on its second press.
	protected AG0_MissionPanelRow AddConfirmButton(array<ref AG0_MissionPanelRow> rows, string action, string arg, string text)
	{
		AG0_MissionPanelRow row = AddButton(rows, action, arg, text);
		row.m_bConfirm = true;
		return row;
	}

	//------------------------------------------------------------------------------------------------
	protected void AddHeading(array<ref AG0_MissionPanelRow> rows, string label, string value)
	{
		AG0_MissionPanelRow row = new AG0_MissionPanelRow();
		row.m_sArg = rows.Count().ToString();
		row.m_sText = label;
		row.m_sState = value;
		rows.Insert(row);
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildRows(AG0_TDLMissionBrief brief, AG0_TDLMissionManager missionMgr, array<ref AG0_MissionPanelRow> rows)
	{
		// A view of something that has since been deleted falls back to its list.
		if (s_eView == AG0_EMissionPanelView.STEP && brief.FindStep(s_sStepId) < 0)
			s_eView = AG0_EMissionPanelView.PLAN;
		if (s_eView == AG0_EMissionPanelView.POINT && brief.m_aPointIds.Find(s_sPointId) < 0)
			s_eView = AG0_EMissionPanelView.POINTS;
		if (s_eView == AG0_EMissionPanelView.GROUP && brief.m_aGroupIds.Find(s_sGroupId) < 0)
			s_eView = AG0_EMissionPanelView.TEAM;

		switch (s_eView)
		{
			case AG0_EMissionPanelView.RUN:
				BuildRunRows(brief, rows);
				break;
			case AG0_EMissionPanelView.PLAN:
				BuildPlanRows(brief, rows);
				break;
			case AG0_EMissionPanelView.STEP:
				BuildStepRows(brief, rows);
				break;
			case AG0_EMissionPanelView.POINTS:
				BuildPointsRows(brief, rows);
				break;
			case AG0_EMissionPanelView.POINT:
				BuildPointRows(brief, rows);
				break;
			case AG0_EMissionPanelView.TEAM:
				BuildTeamRows(brief, missionMgr, rows);
				break;
			case AG0_EMissionPanelView.GROUP:
				BuildGroupRows(brief, missionMgr, rows);
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! "Echo Team - SBF 1 occupied", or just the step when it is everyone's.
	protected string StepText(AG0_TDLMissionBrief brief, int step)
	{
		string label = brief.m_aStepLabels[step];
		if (label.IsEmpty())
			label = "Unnamed step";

		string group = brief.GetGroupName(brief.m_aStepGroup[step]);
		if (group.IsEmpty())
			return label;
		return group + " - " + label;
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildRunRows(AG0_TDLMissionBrief brief, array<ref AG0_MissionPanelRow> rows)
	{
		int branch = brief.m_iActiveBranch;
		array<int> steps = {};
		brief.GetBranchSteps(branch, steps);

		string heading = "STEPS";
		if (brief.GetBranchCount() > 1 && branch >= 0)
			heading = "STEPS  " + brief.m_aBranchNames[branch];

		// Said outright when empty. A panel that just stops after the
		// mission statement reads as broken rather than as a plan with
		// nothing written yet.
		if (steps.IsEmpty())
		{
			AddHeading(rows, heading, "NONE WRITTEN");
			AddButton(rows, "view_plan", string.Empty, "WRITE THE PLAN");
		}
		else
		{
			int done = 0;
			foreach (int doneStep : steps)
			{
				if (brief.m_aStepStates[doneStep] != AG0_ETDLMissionStepState.PENDING)
					done = done + 1;
			}
			AddHeading(rows, heading, string.Format("%1 / %2", done, steps.Count()));

			foreach (int step : steps)
			{
				AG0_MissionPanelRow row = AddButton(rows, "call", brief.m_aStepIds[step], StepText(brief, step));
				row.m_sCode = brief.m_aStepKeyCalls[step];
				row.m_iLiveStep = step;
			}
		}

		AddHeading(rows, "CONTROL", string.Empty);
		AddConfirmButton(rows, "hhour_now", string.Empty, "H-HOUR IS NOW");
		AddConfirmButton(rows, "hhour_in", "300", "H-HOUR IN 5 MIN");
		AddConfirmButton(rows, "hhour_in", "900", "H-HOUR IN 15 MIN");
		if (brief.m_iHHour > 0)
		{
			AddConfirmButton(rows, "hhour_slip", "300", "SLIP H-HOUR 5 MIN LATER");
			AddConfirmButton(rows, "hhour_slip", "-300", "BRING H-HOUR 5 MIN FORWARD");
			AddConfirmButton(rows, "hhour_clear", string.Empty, "BACK TO ON CALL");
		}

		for (int i = 0; i < brief.GetBranchCount(); i++)
		{
			if (i == branch)
				continue;
			AddConfirmButton(rows, "branch_exec", brief.m_aBranchIds[i], "EXECUTE BRANCH  " + brief.m_aBranchNames[i]);
		}

		if (!steps.IsEmpty())
			AddConfirmButton(rows, "reset", string.Empty, "RESET ALL CALLS");
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildPlanRows(AG0_TDLMissionBrief brief, array<ref AG0_MissionPanelRow> rows)
	{
		bool locked = brief.IsLocked();
		if (locked)
			AddHeading(rows, "PLAN LOCKED", "VIEW ONLY");

		int branch = GetPlanBranch(brief);
		if (branch >= 0)
		{
			string branchText = brief.m_aBranchNames[branch];
			if (branch == brief.m_iActiveBranch && brief.GetBranchCount() > 1)
				branchText = branchText + "  (BEING EXECUTED)";

			if (brief.GetBranchCount() > 1)
			{
				AG0_MissionPanelRow branchRow = AddButton(rows, "branch_next", string.Empty, branchText);
				branchRow.m_sTop = string.Format("BRANCH %1 / %2", branch + 1, brief.GetBranchCount());
				branchRow.m_sState = "NEXT >";
			}
			else
			{
				AddHeading(rows, "BRANCH", branchText);
			}
		}

		array<int> steps = {};
		brief.GetBranchSteps(branch, steps);
		AddHeading(rows, "STEPS", steps.Count().ToString());

		int number = 1;
		foreach (int step : steps)
		{
			AG0_MissionPanelRow row = AddButton(rows, "step_open", brief.m_aStepIds[step], StepText(brief, step));
			row.m_sTop = Pad2(number) + "  " + FormatOffset(brief.m_aStepOffsets[step], brief.m_aStepTimed[step] != 0);
			row.m_sCode = brief.m_aStepKeyCalls[step];
			row.m_sState = "EDIT >";
			number = number + 1;
		}

		if (!locked)
		{
			AddButton(rows, "step_add", string.Empty, "+ ADD STEP");
			AddButton(rows, "branch_add", string.Empty, "+ ADD BRANCH");
			AddButton(rows, "mission_rename", string.Empty, "RENAME MISSION");
		}
		AddButton(rows, "mission_new", string.Empty, "+ NEW MISSION");
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildStepRows(AG0_TDLMissionBrief brief, array<ref AG0_MissionPanelRow> rows)
	{
		int step = brief.FindStep(s_sStepId);
		bool locked = brief.IsLocked();

		AddButton(rows, "view_plan", string.Empty, "< BACK TO THE PLAN");
		AddHeading(rows, "STEP", string.Empty);

		string label = brief.m_aStepLabels[step];
		if (label.IsEmpty())
			label = "Unnamed step";
		AG0_MissionPanelRow nameRow = AddButton(rows, "step_name", string.Empty, label);
		nameRow.m_sTop = "WHAT HAPPENS";

		string keyCall = brief.m_aStepKeyCalls[step];
		if (keyCall.IsEmpty())
			keyCall = "None";
		AG0_MissionPanelRow keyRow = AddButton(rows, "step_keycall", string.Empty, keyCall);
		keyRow.m_sTop = "KEY CALL";

		bool timed = brief.m_aStepTimed[step] != 0;
		AG0_MissionPanelRow timeRow = AddButton(rows, "step_time", string.Empty, FormatOffset(brief.m_aStepOffsets[step], timed));
		timeRow.m_sTop = "TIME AGAINST H-HOUR";

		string group = brief.GetGroupName(brief.m_aStepGroup[step]);
		if (group.IsEmpty())
			group = "Everyone";
		AG0_MissionPanelRow groupRow = AddButton(rows, "step_group", string.Empty, group);
		groupRow.m_sTop = "GROUP";
		groupRow.m_sState = "NEXT >";

		if (locked)
		{
			nameRow.m_sAction = "noop";
			nameRow.m_sArg = "name";
			keyRow.m_sAction = "noop";
			keyRow.m_sArg = "keycall";
			timeRow.m_sAction = "noop";
			timeRow.m_sArg = "time";
			groupRow.m_sAction = "noop";
			groupRow.m_sArg = "group";
			groupRow.m_sState = string.Empty;
		}
		else
		{
			if (timed)
				AddButton(rows, "step_onorder", string.Empty, "MAKE IT ON ORDER");
			AddButton(rows, "step_move", "-1", "MOVE EARLIER");
			AddButton(rows, "step_move", "1", "MOVE LATER");
		}

		AddButton(rows, "step_points", string.Empty, "POINTS OF THIS STEP");

		if (!locked)
			AddConfirmButton(rows, "step_delete", string.Empty, "DELETE STEP");
	}

	//------------------------------------------------------------------------------------------------
	//! Label of the step new points are placed for.
	protected string PointStepText(AG0_TDLMissionBrief brief)
	{
		int step = brief.FindStep(s_sPointStepId);
		if (step < 0 || brief.m_aStepBranch[step] != brief.m_iActiveBranch)
		{
			s_sPointStepId = string.Empty;
			return "No step";
		}

		string label = brief.m_aStepLabels[step];
		if (label.IsEmpty())
			label = "Unnamed step";
		return label;
	}

	//------------------------------------------------------------------------------------------------
	protected string PointGrid(AG0_TDLMissionBrief brief, int point)
	{
		return AG0_MGRSGridUtils.GetFullMGRS(Vector(brief.m_aPointX[point], 0, brief.m_aPointZ[point]), 4);
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildPointsRows(AG0_TDLMissionBrief brief, array<ref AG0_MissionPanelRow> rows)
	{
		if (s_iPointType < 0 || s_iPointType >= m_aPointTypes.Count())
			s_iPointType = 0;

		if (brief.IsLocked())
		{
			AddHeading(rows, "PLAN LOCKED", "VIEW ONLY");
		}
		else
		{
			AddHeading(rows, "PLACE AT THE CROSSHAIR", string.Empty);

			AG0_MissionPanelRow stepRow = AddButton(rows, "point_step", string.Empty, PointStepText(brief));
			stepRow.m_sTop = "FOR STEP";
			stepRow.m_sState = "NEXT >";

			AG0_MissionPanelRow typeRow = AddButton(rows, "point_type", string.Empty, m_aPointTypeNames[s_iPointType]);
			typeRow.m_sTop = "TYPE";
			typeRow.m_sState = "NEXT >";

			AddButton(rows, "point_place", string.Empty, "PLACE IT HERE");
		}

		AddHeading(rows, "POINTS ON THE MAP", brief.m_aPointIds.Count().ToString());
		for (int i = 0; i < brief.m_aPointIds.Count(); i++)
		{
			AG0_MissionPanelRow row = AddButton(rows, "point_open", brief.m_aPointIds[i], brief.m_aPointLabels[i]);
			row.m_sTop = PointGrid(brief, i);
			int step = brief.m_aPointStep[i];
			if (step >= 0 && step < brief.m_aStepLabels.Count())
				row.m_sCode = brief.m_aStepLabels[step];
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildPointRows(AG0_TDLMissionBrief brief, array<ref AG0_MissionPanelRow> rows)
	{
		int point = brief.m_aPointIds.Find(s_sPointId);

		AddButton(rows, "view_points", string.Empty, "< BACK TO THE POINTS");
		AddHeading(rows, brief.m_aPointLabels[point], PointGrid(brief, point));
		AddButton(rows, "point_goto", string.Empty, "CENTRE THE MAP ON IT");

		if (brief.IsLocked())
			return;

		AddConfirmButton(rows, "point_move", string.Empty, "MOVE IT TO THE CROSSHAIR");
		AddConfirmButton(rows, "point_delete", string.Empty, "DELETE POINT");
	}

	//------------------------------------------------------------------------------------------------
	protected string GroupKindText(int kind)
	{
		if (kind == 1)
			return "AVIATION";
		return "GROUND";
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildTeamRows(AG0_TDLMissionBrief brief, AG0_TDLMissionManager missionMgr, array<ref AG0_MissionPanelRow> rows)
	{
		string myGroup = missionMgr.GetMyGroupId(brief.m_sId);

		if (brief.m_aGroupIds.IsEmpty())
			AddHeading(rows, "GROUPS", "NONE YET");
		else
			AddHeading(rows, "GROUPS", brief.m_aGroupIds.Count().ToString());

		for (int i = 0; i < brief.m_aGroupIds.Count(); i++)
		{
			AG0_MissionPanelRow row = AddButton(rows, "group_open", brief.m_aGroupIds[i], brief.m_aGroupNames[i]);
			row.m_sTop = GroupKindText(brief.m_aGroupKinds[i]);
			row.m_sCode = brief.m_aGroupMembers[i];
			if (brief.m_aGroupIds[i] == myGroup)
				row.m_sState = "YOUR GROUP";
		}

		if (brief.IsLocked())
			return;

		AddButton(rows, "group_add", "0", "+ ADD GROUND GROUP");
		AddButton(rows, "group_add", "1", "+ ADD AVIATION GROUP");
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildGroupRows(AG0_TDLMissionBrief brief, AG0_TDLMissionManager missionMgr, array<ref AG0_MissionPanelRow> rows)
	{
		int group = brief.m_aGroupIds.Find(s_sGroupId);
		bool mine = missionMgr.GetMyGroupId(brief.m_sId) == s_sGroupId;

		AddButton(rows, "view_team", string.Empty, "< BACK TO THE GROUPS");
		AddHeading(rows, brief.m_aGroupNames[group], GroupKindText(brief.m_aGroupKinds[group]));

		string members = brief.m_aGroupMembers[group];
		if (members.IsEmpty())
			members = "Nobody yet";
		AG0_MissionPanelRow membersRow = AddButton(rows, "noop", "members", members);
		membersRow.m_sTop = "IN THIS GROUP";

		if (mine)
			AddButton(rows, "group_leave", string.Empty, "LEAVE THIS GROUP");
		else
			AddButton(rows, "group_join", string.Empty, "JOIN THIS GROUP");

		if (!brief.IsLocked())
			AddConfirmButton(rows, "group_delete", string.Empty, "DELETE GROUP");
	}

	//------------------------------------------------------------------------------------------------
	// Rows on screen
	//------------------------------------------------------------------------------------------------

	//! Put a new list on screen. The widgets are only torn down when the
	//! list itself changed shape; a push that only moves text (someone
	//! called a step, a name was edited) rewrites the rows where they
	//! stand, which keeps gamepad focus and the scroll position.
	protected void ApplyRows(array<ref AG0_MissionPanelRow> rows)
	{
		bool same = rows.Count() == m_aRows.Count() && rows.Count() == m_aRowWidgets.Count();
		if (same)
		{
			for (int i = 0; i < rows.Count(); i++)
			{
				if (rows[i].GetKey() != m_aRows[i].GetKey())
				{
					same = false;
					break;
				}
			}
		}

		if (same)
		{
			m_aRows = rows;
			RefreshRowTexts();
			return;
		}

		// Removing the focused row hands focus to whatever the engine
		// picks next, usually something outside the panel.
		int focusedRow = GetFocusedRow();
		string focusedKey;
		if (focusedRow >= 0 && focusedRow < m_aRows.Count())
			focusedKey = m_aRows[focusedRow].GetKey();

		ClearRowWidgets();
		m_aRows = rows;

		if (!m_wRowList)
			return;

		for (int r = 0; r < m_aRows.Count(); r++)
		{
			AG0_MissionPanelRow row = m_aRows[r];
			ResourceName layout = ROW_LAYOUT;
			if (row.m_sAction.IsEmpty())
				layout = HEADING_LAYOUT;

			Widget widget = GetGame().GetWorkspace().CreateWidgets(layout, m_wRowList);
			if (!widget)
			{
				Print("[TDL_MISSIONS] Mission row layout failed to load", LogLevel.WARNING);
				ClearRowWidgets();
				m_aRows.Clear();
				return;
			}

			// Assigned every pass: a local declared in a loop keeps the
			// value the previous pass left in it.
			AG0_MissionRowClickRelay relay = null;
			if (!row.m_sAction.IsEmpty())
			{
				SCR_ModularButtonComponent comp = SCR_ModularButtonComponent.FindComponent(widget);
				if (comp)
				{
					relay = new AG0_MissionRowClickRelay(this, r);
					comp.m_OnClicked.Insert(relay.OnClick);
				}
			}

			m_aRowWidgets.Insert(widget);
			m_aRowRelays.Insert(relay);
		}

		RefreshRowTexts();

		if (m_Controller)
			m_Controller.OnPluginPanelRebuilt();

		// Focus goes back into the list only if it was there: a push can
		// rebuild the list while the player is working the map.
		if (focusedRow < 0)
			return;

		for (int k = 0; k < m_aRows.Count(); k++)
		{
			if (!m_aRows[k].m_sAction.IsEmpty() && m_aRows[k].GetKey() == focusedKey)
			{
				GetGame().GetWorkspace().SetFocusedWidget(m_aRowWidgets[k]);
				return;
			}
		}
		FocusFirstButtonRow();
	}

	//------------------------------------------------------------------------------------------------
	protected bool FocusFirstButtonRow()
	{
		for (int i = 0; i < m_aRows.Count() && i < m_aRowWidgets.Count(); i++)
		{
			if (m_aRows[i].m_sAction.IsEmpty())
				continue;
			GetGame().GetWorkspace().SetFocusedWidget(m_aRowWidgets[i]);
			return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearRowWidgets()
	{
		foreach (Widget widget : m_aRowWidgets)
		{
			if (widget)
				widget.RemoveFromHierarchy();
		}
		m_aRowWidgets.Clear();
		m_aRowRelays.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Index of the row gamepad focus is on or inside, -1 when focus is elsewhere.
	protected int GetFocusedRow()
	{
		Widget current = GetGame().GetWorkspace().GetFocusedWidget();
		while (current)
		{
			int index = m_aRowWidgets.Find(current);
			if (index != -1)
				return index;
			current = current.GetParent();
		}
		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Everything a row says that does not move with the clock.
	protected void RefreshRowTexts()
	{
		for (int i = 0; i < m_aRows.Count() && i < m_aRowWidgets.Count(); i++)
		{
			AG0_MissionPanelRow row = m_aRows[i];
			Widget widget = m_aRowWidgets[i];

			if (row.m_sAction.IsEmpty())
			{
				SetText(TextWidget.Cast(widget.FindAnyWidget("InfoLabel")), row.m_sText);
				SetText(TextWidget.Cast(widget.FindAnyWidget("InfoValue")), row.m_sState);
				continue;
			}

			SetText(TextWidget.Cast(widget.FindAnyWidget("LineText")), row.m_sText);

			TextWidget code = TextWidget.Cast(widget.FindAnyWidget("LineCode"));
			SetShown(code, !row.m_sCode.IsEmpty());
			SetText(code, row.m_sCode);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Everything that moves with the clock, with a step being called, or
	//! with a row being armed.
	protected void RefreshLive()
	{
		if (!m_wPanel)
			return;

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		AG0_TDLMissionBrief brief = GetSelectedMission();

		int now = 0;
		if (missionMgr)
			now = missionMgr.GetNow();

		if (brief)
		{
			if (brief.m_iHHour > 0)
			{
				SetText(m_wHHourText, "H-HOUR " + FormatZulu(brief.m_iHHour));
				SetText(m_wMissionClock, FormatClock(now - brief.m_iHHour));
			}
			else
			{
				SetText(m_wHHourText, "H-HOUR ON CALL");
				SetText(m_wMissionClock, string.Empty);
			}
		}

		string myGroup;
		int nextStep = -1;
		if (brief && missionMgr)
		{
			myGroup = missionMgr.GetMyGroupId(brief.m_sId);
			nextStep = brief.GetNextPendingStep(brief.m_iActiveBranch);
		}

		int number = 0;
		for (int i = 0; i < m_aRows.Count() && i < m_aRowWidgets.Count(); i++)
		{
			AG0_MissionPanelRow row = m_aRows[i];
			if (row.m_sAction.IsEmpty())
				continue;

			Widget widget = m_aRowWidgets[i];
			TextWidget timeWidget = TextWidget.Cast(widget.FindAnyWidget("LineTime"));
			TextWidget stateWidget = TextWidget.Cast(widget.FindAnyWidget("LineState"));
			bool armed = m_sArmedKey == row.GetKey();

			int step = row.m_iLiveStep;
			if (brief && step >= 0 && step < brief.m_aStepIds.Count())
			{
				number = number + 1;
				RefreshLiveStep(brief, step, number, now, armed, step == nextStep, myGroup, timeWidget, stateWidget);
				continue;
			}

			string stateText = row.m_sState;
			Color stateColor = m_cStateNext;
			if (armed)
			{
				stateText = "PRESS AGAIN";
				stateColor = m_cStateWarn;
			}

			SetText(timeWidget, row.m_sTop);
			if (timeWidget)
				timeWidget.SetColor(m_cStateMuted);
			SetText(stateWidget, stateText);
			if (stateWidget)
				stateWidget.SetColor(stateColor);

			// A plain action row has nothing above its text; without this
			// the empty line still takes its height.
			SetShown(widget.FindAnyWidget("LineTop"), !row.m_sTop.IsEmpty() || !stateText.IsEmpty());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Time and state of one step in the run view.
	protected void RefreshLiveStep(AG0_TDLMissionBrief brief, int step, int number, int now, bool armed, bool isNext, string myGroup, TextWidget timeWidget, TextWidget stateWidget)
	{
		int state = brief.m_aStepStates[step];
		bool timed = brief.m_aStepTimed[step] != 0;
		bool hasClockTime = timed && brief.m_iHHour > 0;
		int dueAt = brief.m_iHHour + brief.m_aStepOffsets[step];

		string timeText = string.Format("%1  %2", Pad2(number), FormatOffset(brief.m_aStepOffsets[step], timed));
		if (hasClockTime)
			timeText = timeText + "  " + FormatZulu(dueAt);

		int group = brief.m_aStepGroup[step];
		bool mine = !myGroup.IsEmpty() && group >= 0 && group < brief.m_aGroupIds.Count() && brief.m_aGroupIds[group] == myGroup;
		if (mine)
			timeText = timeText + "  YOUR GROUP";

		SetText(timeWidget, timeText);
		if (timeWidget)
		{
			if (state != AG0_ETDLMissionStepState.PENDING)
				timeWidget.SetColor(m_cTimeDone);
			else if (mine)
				timeWidget.SetColor(m_cStateNext);
			else
				timeWidget.SetColor(m_cTimeLive);
		}

		string stateText;
		Color stateColor = m_cStateMuted;

		if (armed)
		{
			stateColor = m_cStateWarn;
			if (state == AG0_ETDLMissionStepState.PENDING)
				stateText = "PRESS AGAIN TO CALL";
			else
				stateText = "PRESS AGAIN TO UNDO";
		}
		else if (state == AG0_ETDLMissionStepState.CALLED)
		{
			stateColor = m_cStateCalled;
			stateText = "CALLED";
			int calledAt = brief.m_aStepCalledAt[step];
			if (calledAt > 0)
			{
				stateText = stateText + " " + FormatZulu(calledAt);
				if (hasClockTime && Math.AbsInt(calledAt - dueAt) >= LATE_AFTER_SECONDS)
					stateText = stateText + " (" + FormatDelta(calledAt - dueAt) + ")";
			}
		}
		else if (state == AG0_ETDLMissionStepState.SKIPPED)
		{
			stateText = "SKIPPED";
		}
		else if (hasClockTime && now - dueAt >= LATE_AFTER_SECONDS)
		{
			stateColor = m_cStateWarn;
			stateText = "LATE " + FormatDelta(now - dueAt);
		}
		else if (isNext)
		{
			stateColor = m_cStateNext;
			stateText = "NEXT";
		}

		SetText(stateWidget, stateText);
		if (stateWidget)
			stateWidget.SetColor(stateColor);
	}

	//------------------------------------------------------------------------------------------------
	// Presses
	//------------------------------------------------------------------------------------------------

	//! A row was pressed. Rows that call a step or cannot be undone arm on
	//! the first press and act on the second within ARM_SECONDS.
	void OnRowClicked(int index)
	{
		if (index < 0 || index >= m_aRows.Count())
			return;

		AG0_MissionPanelRow row = m_aRows[index];
		if (row.m_sAction.IsEmpty() || row.m_sAction == "noop")
			return;

		if (row.m_bConfirm || row.m_sAction == "call")
		{
			int stepState = GetRowStepState(row);
			bool armedForThis = m_sArmedKey == row.GetKey() && m_iArmedStepState == stepState;
			if (!armedForThis)
			{
				m_sArmedKey = row.GetKey();
				m_iArmedStepState = stepState;
				m_fArmedTimer = ARM_SECONDS;
				RefreshLive();
				return;
			}
		}
		m_sArmedKey = string.Empty;

		if (row.m_sAction == "mission_new")
		{
			AskText("mission_new", string.Empty, "NEW MISSION", "Name the mission", string.Empty);
			return;
		}

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		AG0_TDLMissionBrief brief = GetSelectedMission();
		if (!missionMgr || !brief)
			return;

		DoAction(missionMgr, brief, row.m_sAction, row.m_sArg);
	}

	//------------------------------------------------------------------------------------------------
	//! State of the step a "call" row stands for, -1 for any other row.
	protected int GetRowStepState(AG0_MissionPanelRow row)
	{
		if (row.m_sAction != "call")
			return -1;

		AG0_TDLMissionBrief brief = GetSelectedMission();
		if (!brief)
			return -1;

		int step = brief.FindStep(row.m_sArg);
		if (step < 0)
			return -1;
		return brief.m_aStepStates[step];
	}

	//------------------------------------------------------------------------------------------------
	protected void DoAction(AG0_TDLMissionManager missionMgr, AG0_TDLMissionBrief brief, string action, string arg)
	{
		switch (action)
		{
			// ---- moving between views ----
			case "view_plan":
				SetView(AG0_EMissionPanelView.PLAN);
				break;
			case "view_points":
				SetView(AG0_EMissionPanelView.POINTS);
				break;
			case "view_team":
				SetView(AG0_EMissionPanelView.TEAM);
				break;
			case "step_open":
				s_sStepId = arg;
				SetView(AG0_EMissionPanelView.STEP);
				break;
			case "point_open":
				s_sPointId = arg;
				SetView(AG0_EMissionPanelView.POINT);
				break;
			case "group_open":
				s_sGroupId = arg;
				SetView(AG0_EMissionPanelView.GROUP);
				break;
			case "step_points":
				s_sPointStepId = s_sStepId;
				SetView(AG0_EMissionPanelView.POINTS);
				break;
			case "branch_next":
				ShowNextBranch(brief);
				break;

			// ---- running the mission ----
			case "call":
				CallStep(missionMgr, brief, arg);
				break;
			case "hhour_now":
				SendHHour(brief, missionMgr.GetNow());
				break;
			case "hhour_in":
				SendHHour(brief, missionMgr.GetNow() + arg.ToInt());
				break;
			case "hhour_slip":
				SendHHour(brief, brief.m_iHHour + arg.ToInt());
				break;
			case "hhour_clear":
				SendHHour(brief, 0);
				break;
			case "branch_exec":
				SendWithString(brief, "branch", "branchId", arg);
				break;
			case "reset":
				SendEdit(brief.m_sId, "reset", null);
				break;

			// ---- the plan ----
			case "step_add":
				AskText("step_add", string.Empty, "NEW STEP", "What happens in this step", string.Empty);
				break;
			case "branch_add":
				AskText("branch_add", string.Empty, "NEW BRANCH", "Name the branch, e.g. Alternate", string.Empty);
				break;
			case "mission_rename":
				AskText("mission_rename", string.Empty, "MISSION", "Name the mission", brief.m_sName);
				break;
			case "step_name":
				AskStepText(brief, "step_name", "STEP", "What happens in this step");
				break;
			case "step_keycall":
				AskStepText(brief, "step_keycall", "KEY CALL", "What is said on the net when this step happens. Empty for none");
				break;
			case "step_time":
				AskStepText(brief, "step_time", "TIME", "Against H-hour: -20 or +5 (minutes), -1:30 (h:mm), H. Empty for on order");
				break;
			case "step_onorder":
				SendStepOnOrder(brief);
				break;
			case "step_group":
				SendStepNextGroup(brief);
				break;
			case "step_move":
				SendStepMove(brief, arg.ToInt());
				break;
			case "step_delete":
				SendWithString(brief, "step_del", "stepId", s_sStepId);
				SetView(AG0_EMissionPanelView.PLAN);
				break;

			// ---- points ----
			case "point_step":
				PickNextPointStep(brief);
				break;
			case "point_type":
				s_iPointType = s_iPointType + 1;
				if (s_iPointType >= m_aPointTypes.Count())
					s_iPointType = 0;
				m_bViewDirty = true;
				break;
			case "point_place":
				SendPointPlace(brief);
				break;
			case "point_goto":
				CentreMapOnPoint(brief);
				break;
			case "point_move":
				SendPointMove(brief);
				break;
			case "point_delete":
				SendWithString(brief, "point_del", "pointId", s_sPointId);
				SetView(AG0_EMissionPanelView.POINTS);
				break;

			// ---- groups ----
			case "group_add":
				AskText("group_add", arg, "NEW GROUP", "Name the group, e.g. Echo Team", string.Empty);
				break;
			case "group_join":
				SendWithString(brief, "group_join", "groupId", s_sGroupId);
				break;
			case "group_leave":
				SendWithString(brief, "group_join", "groupId", string.Empty);
				break;
			case "group_delete":
				SendWithString(brief, "group_del", "groupId", s_sGroupId);
				SetView(AG0_EMissionPanelView.TEAM);
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowNextBranch(AG0_TDLMissionBrief brief)
	{
		if (brief.GetBranchCount() == 0)
			return;

		int next = GetPlanBranch(brief) + 1;
		if (next >= brief.GetBranchCount())
			next = 0;
		s_sPlanBranchId = brief.m_aBranchIds[next];
		m_bViewDirty = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Second press on a step: call it, or take the call back if it was already done.
	protected void CallStep(AG0_TDLMissionManager missionMgr, AG0_TDLMissionBrief brief, string stepId)
	{
		int step = brief.FindStep(stepId);
		if (step < 0)
			return;

		int newState = AG0_ETDLMissionStepState.CALLED;
		if (brief.m_aStepStates[step] != AG0_ETDLMissionStepState.PENDING)
			newState = AG0_ETDLMissionStepState.PENDING;

		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("stepId", stepId);
		args.WriteValue("state", newState);

		// Shown at once; the server's next push confirms or corrects it.
		missionMgr.SetStepStateLocal(brief.m_sId, stepId, newState);
		SendEdit(brief.m_sId, "call", args);
		RefreshLive();
	}

	//------------------------------------------------------------------------------------------------
	//! @param hHour Unix seconds, 0 to go back to on call.
	protected void SendHHour(AG0_TDLMissionBrief brief, int hHour)
	{
		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("hHour", hHour);
		SendEdit(brief.m_sId, "hhour", args);
	}

	//------------------------------------------------------------------------------------------------
	//! An edit whose only argument is one string.
	protected void SendWithString(AG0_TDLMissionBrief brief, string op, string key, string value)
	{
		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue(key, value);
		SendEdit(brief.m_sId, op, args);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendStepOnOrder(AG0_TDLMissionBrief brief)
	{
		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("stepId", s_sStepId);
		args.WriteValue("onOrder", 1);
		SendEdit(brief.m_sId, "step_set", args);
	}

	//------------------------------------------------------------------------------------------------
	//! Give the step to the next group in the list, then to everyone, and round again.
	protected void SendStepNextGroup(AG0_TDLMissionBrief brief)
	{
		int step = brief.FindStep(s_sStepId);
		if (step < 0)
			return;

		int next = brief.m_aStepGroup[step] + 1;

		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("stepId", s_sStepId);
		if (next >= 0 && next < brief.m_aGroupIds.Count())
			args.WriteValue("groupId", brief.m_aGroupIds[next]);
		else
			args.WriteValue("everyone", 1);
		SendEdit(brief.m_sId, "step_set", args);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendStepMove(AG0_TDLMissionBrief brief, int by)
	{
		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("stepId", s_sStepId);
		args.WriteValue("by", by);
		SendEdit(brief.m_sId, "step_move", args);
	}

	//------------------------------------------------------------------------------------------------
	//! New points go to the next step of the branch being executed, then
	//! to no step, and round again.
	protected void PickNextPointStep(AG0_TDLMissionBrief brief)
	{
		array<int> steps = {};
		brief.GetBranchSteps(brief.m_iActiveBranch, steps);

		int current = steps.Find(brief.FindStep(s_sPointStepId));
		int next = current + 1;
		if (next < steps.Count())
			s_sPointStepId = brief.m_aStepIds[steps[next]];
		else
			s_sPointStepId = string.Empty;
		m_bViewDirty = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void SendPointPlace(AG0_TDLMissionBrief brief)
	{
		if (!m_Controller)
			return;

		vector world = m_Controller.GetCrosshairWorld();

		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("type", m_aPointTypes[s_iPointType]);
		args.WriteValue("x", Math.Round(world[0]));
		args.WriteValue("z", Math.Round(world[2]));
		args.WriteValue("stepId", s_sPointStepId);
		SendEdit(brief.m_sId, "point_add", args);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendPointMove(AG0_TDLMissionBrief brief)
	{
		if (!m_Controller)
			return;

		vector world = m_Controller.GetCrosshairWorld();

		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("pointId", s_sPointId);
		args.WriteValue("x", Math.Round(world[0]));
		args.WriteValue("z", Math.Round(world[2]));
		SendEdit(brief.m_sId, "point_move", args);
	}

	//------------------------------------------------------------------------------------------------
	protected void CentreMapOnPoint(AG0_TDLMissionBrief brief)
	{
		int point = brief.m_aPointIds.Find(s_sPointId);
		if (point < 0 || !m_Controller)
			return;

		AG0_TDLDisplayController display = m_Controller.GetDisplayController();
		if (!display)
			return;

		AG0_TDLMapView mapView = display.GetMapView();
		if (!mapView)
			return;

		// Player tracking would pull the map straight back.
		mapView.CenterOnWorld(Vector(brief.m_aPointX[point], 0, brief.m_aPointZ[point]));
		AG0_TDLDisplayController.SetPlayerTracking(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Hand a change to the server, which passes it to the web API. Nothing
	//! changes here until the server's next push brings the result back.
	//! @param args The operation's arguments, or null when it takes none.
	protected void SendEdit(string missionId, string op, JsonSaveContext args)
	{
		SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!controller)
			return;

		string argsJson = "{}";
		if (args)
			argsJson = args.SaveToString();

		controller.AskMissionEdit(missionId, op, argsJson);
	}

	//------------------------------------------------------------------------------------------------
	// Text entry
	//------------------------------------------------------------------------------------------------
	protected void AskText(string purpose, string arg, string title, string message, string initialText)
	{
		m_sDialogPurpose = purpose;
		m_sDialogArg = arg;
		// The selection can move while the dialog is up: a new mission
		// arrives, the one on show is retracted. The answer belongs to
		// the mission the question was asked about.
		m_sDialogMissionId = s_sSelectedMissionId;

		m_sRefocusKey = string.Empty;
		int focusedRow = GetFocusedRow();
		if (focusedRow >= 0 && focusedRow < m_aRows.Count())
			m_sRefocusKey = m_aRows[focusedRow].GetKey();

		m_TextDialog = AG0_TDLMissionTextDialog.CreateDialog(title, message, initialText);
		if (!m_TextDialog)
			return;

		m_TextDialog.m_OnConfirm.Insert(OnTextConfirmed);
		m_TextDialog.m_OnCancel.Insert(OnTextCancelled);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTextCancelled(SCR_ConfigurableDialogUi dialog)
	{
		m_sDialogPurpose = string.Empty;
		m_sDialogArg = string.Empty;
		m_TextDialog = null;
		m_iRefocusDelay = 2;
	}

	//------------------------------------------------------------------------------------------------
	//! The dialog took gamepad focus and gives it to nothing when it
	//! closes. Put it back on the row that opened it, or failing that on
	//! the list, but only for a player who had it in the list to begin with.
	protected void RefocusAfterDialog()
	{
		if (m_sRefocusKey.IsEmpty())
			return;

		string key = m_sRefocusKey;
		m_sRefocusKey = string.Empty;

		for (int i = 0; i < m_aRows.Count() && i < m_aRowWidgets.Count(); i++)
		{
			if (m_aRows[i].m_sAction.IsEmpty() || m_aRows[i].GetKey() != key)
				continue;
			GetGame().GetWorkspace().SetFocusedWidget(m_aRowWidgets[i]);
			return;
		}
		FocusFirstButtonRow();
	}

	//------------------------------------------------------------------------------------------------
	//! Ask for one field of the step in the step view, starting from what it holds now.
	protected void AskStepText(AG0_TDLMissionBrief brief, string purpose, string title, string message)
	{
		int step = brief.FindStep(s_sStepId);
		if (step < 0)
			return;

		string current;
		if (purpose == "step_name")
			current = brief.m_aStepLabels[step];
		else if (purpose == "step_keycall")
			current = brief.m_aStepKeyCalls[step];
		else if (brief.m_aStepTimed[step] != 0)
			current = FormatOffsetInput(brief.m_aStepOffsets[step]);

		AskText(purpose, s_sStepId, title, message, current);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTextConfirmed(SCR_ConfigurableDialogUi dialog)
	{
		AG0_TDLMissionTextDialog textDialog = AG0_TDLMissionTextDialog.Cast(dialog);
		if (!textDialog)
			return;

		string text = textDialog.GetEnteredText();
		string purpose = m_sDialogPurpose;
		string arg = m_sDialogArg;
		m_sDialogPurpose = string.Empty;
		m_sDialogArg = string.Empty;
		m_TextDialog = null;
		m_iRefocusDelay = 2;

		if (purpose == "mission_new")
		{
			CreateMission(text);
			return;
		}

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr)
			return;

		AG0_TDLMissionBrief brief = missionMgr.FindMission(m_sDialogMissionId);
		if (!brief)
			return;

		JsonSaveContext args = new JsonSaveContext();

		switch (purpose)
		{
			case "mission_rename":
				if (text.IsEmpty())
					return;
				args.WriteValue("name", text);
				SendEdit(brief.m_sId, "name", args);
				break;

			case "branch_add":
				args.WriteValue("name", text);
				SendEdit(brief.m_sId, "branch_add", args);
				break;

			case "step_add":
				if (text.IsEmpty())
					return;
				args.WriteValue("branchId", PlanBranchId(brief));
				args.WriteValue("label", text);
				SendEdit(brief.m_sId, "step_add", args);
				break;

			case "step_name":
				if (text.IsEmpty())
					return;
				args.WriteValue("stepId", arg);
				args.WriteValue("label", text);
				SendEdit(brief.m_sId, "step_set", args);
				break;

			case "step_keycall":
				text.ToUpper();
				args.WriteValue("stepId", arg);
				args.WriteValue("keyCall", text);
				SendEdit(brief.m_sId, "step_set", args);
				break;

			case "step_time":
				SendStepTime(brief, arg, text);
				break;

			case "group_add":
				if (text.IsEmpty())
					return;
				args.WriteValue("name", text);
				args.WriteValue("kind", arg.ToInt());
				SendEdit(brief.m_sId, "group_add", args);
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected string PlanBranchId(AG0_TDLMissionBrief brief)
	{
		int branch = GetPlanBranch(brief);
		if (branch < 0)
			return string.Empty;
		return brief.m_aBranchIds[branch];
	}

	//------------------------------------------------------------------------------------------------
	protected void CreateMission(string name)
	{
		if (name.IsEmpty())
			return;

		AG0_TDLMissionManager missionMgr = GetMissionManager();
		if (!missionMgr)
			return;

		m_fCreateWait = CREATE_WAIT_SECONDS;
		m_aIdsBeforeCreate = {};
		for (int i = 0; i < missionMgr.GetMissionCount(); i++)
		{
			m_aIdsBeforeCreate.Insert(missionMgr.GetMission(i).m_sId);
		}

		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("name", name);
		SendEdit(string.Empty, "create", args);
	}

	//------------------------------------------------------------------------------------------------
	protected void SendStepTime(AG0_TDLMissionBrief brief, string stepId, string text)
	{
		bool timed;
		int offsetSec;
		if (!ParseOffset(text, timed, offsetSec))
			return;

		JsonSaveContext args = new JsonSaveContext();
		args.WriteValue("stepId", stepId);
		if (timed)
		{
			args.WriteValue("timed", 1);
			args.WriteValue("offsetSec", offsetSec);
		}
		else
		{
			args.WriteValue("onOrder", 1);
		}
		SendEdit(brief.m_sId, "step_set", args);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsDigits(string text)
	{
		if (text.IsEmpty())
			return false;
		for (int i = 0; i < text.Length(); i++)
		{
			if (!DIGITS.Contains(text.Substring(i, 1)))
				return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Read a time typed by the player: "-20" and "+5" are minutes, "-1:30"
	//! is hours and minutes, "H" is H-hour itself, a leading "H" is
	//! allowed ("H-0:20"), and nothing at all means on order.
	//! @return false when the text is not a time.
	protected bool ParseOffset(string input, out bool timed, out int offsetSec)
	{
		timed = false;
		offsetSec = 0;

		string text = input.Trim();
		text.ToUpper();
		if (text.IsEmpty())
			return true;

		if (text.Substring(0, 1) == "H")
		{
			if (text.Length() == 1)
			{
				timed = true;
				return true;
			}
			text = text.Substring(1, text.Length() - 1);
		}

		int sign = 1;
		string first = text.Substring(0, 1);
		if (first == "-" || first == "+")
		{
			if (text.Length() == 1)
				return false;
			if (first == "-")
				sign = -1;
			text = text.Substring(1, text.Length() - 1);
		}

		array<string> parts = {};
		text.Split(":", parts, false);

		int seconds;
		if (parts.Count() == 1 && IsDigits(parts[0]))
		{
			seconds = parts[0].ToInt() * 60;
		}
		else if (parts.Count() == 2 && IsDigits(parts[0]) && IsDigits(parts[1]) && parts[1].ToInt() < 60)
		{
			seconds = parts[0].ToInt() * 3600 + parts[1].ToInt() * 60;
		}
		else
		{
			return false;
		}

		timed = true;
		offsetSec = sign * seconds;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Formatting
	//------------------------------------------------------------------------------------------------
	protected void SetText(TextWidget widget, string text)
	{
		if (widget)
			widget.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetShown(Widget widget, bool shown)
	{
		if (widget)
			widget.SetVisible(shown);
	}

	//------------------------------------------------------------------------------------------------
	protected string Pad2(int value)
	{
		if (value < 10)
			return "0" + value.ToString();
		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	//! "H-0:20", "H+1:05", "H+0:00:30", "H-HOUR", or "ON ORDER".
	protected string FormatOffset(int offsetSec, bool timed)
	{
		if (!timed)
			return "ON ORDER";
		if (offsetSec == 0)
			return "H-HOUR";

		string sign = "+";
		int magnitude = offsetSec;
		if (offsetSec < 0)
		{
			sign = "-";
			magnitude = -offsetSec;
		}

		string text = string.Format("H%1%2:%3", sign, magnitude / 3600, Pad2((magnitude % 3600) / 60));
		int seconds = magnitude % 60;
		if (seconds != 0)
			text = text + ":" + Pad2(seconds);
		return text;
	}

	//------------------------------------------------------------------------------------------------
	//! A step's time the way ParseOffset reads it back: "H", "-0:20", "+1:05".
	protected string FormatOffsetInput(int offsetSec)
	{
		if (offsetSec == 0)
			return "H";

		string sign = "+";
		int magnitude = offsetSec;
		if (offsetSec < 0)
		{
			sign = "-";
			magnitude = -offsetSec;
		}
		return string.Format("%1%2:%3", sign, magnitude / 3600, Pad2((magnitude % 3600) / 60));
	}

	//------------------------------------------------------------------------------------------------
	//! Time of day in Zulu, "1930Z".
	protected string FormatZulu(int unixTime)
	{
		int ofDay = unixTime % 86400;
		return Pad2(ofDay / 3600) + Pad2((ofDay % 3600) / 60) + "Z";
	}

	//------------------------------------------------------------------------------------------------
	//! Where the mission clock stands: "H-0:12:30" before H-hour, "H+0:03:10" after.
	protected string FormatClock(int sinceHHour)
	{
		string sign = "+";
		int magnitude = sinceHHour;
		if (sinceHHour < 0)
		{
			sign = "-";
			magnitude = -sinceHHour;
		}
		return string.Format("H%1%2:%3:%4", sign, magnitude / 3600, Pad2((magnitude % 3600) / 60), Pad2(magnitude % 60));
	}

	//------------------------------------------------------------------------------------------------
	//! How far off its planned time something is: "+2:10", "-0:45", "+1:02:05".
	protected string FormatDelta(int deltaSec)
	{
		string sign = "+";
		int magnitude = deltaSec;
		if (deltaSec < 0)
		{
			sign = "-";
			magnitude = -deltaSec;
		}

		int hours = magnitude / 3600;
		int minutes = (magnitude % 3600) / 60;
		if (hours > 0)
			return string.Format("%1%2:%3:%4", sign, hours, Pad2(minutes), Pad2(magnitude % 60));
		return string.Format("%1%2:%3", sign, minutes, Pad2(magnitude % 60));
	}
}
