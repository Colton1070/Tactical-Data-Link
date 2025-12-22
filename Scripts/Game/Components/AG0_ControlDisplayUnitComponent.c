class AG0_ControlDisplayUnitComponentClass : ScriptComponentClass
{
}

class AG0_ControlDisplayUnitComponent : ScriptComponent
{
    // Component references
    protected IEntity m_CDUEntity;
    protected Widget m_wRoot;
    protected RTTextureWidget m_wRTTexture;
	
	// Replication
	protected RplComponent m_RplComp;
	protected RplIdentity m_ServerIdentity;
	protected RplIdentity m_OriginalOwner; // Store the original owner for reverting
	protected RplIdentity m_UserIdentity;
	protected IEntity m_TargetUserEntity;
	
	// VoN capture handling - moved to server side
	protected SCR_VoNComponent m_VonComponent;
	protected ref ScriptInvoker m_OnVonCapture;
	protected bool m_bListeningForRadio = false;
	protected float m_fListenTimeout;
	protected const float LISTEN_TIMEOUT = 15000; // 15 seconds to key a radio
    
    // Display state
    protected bool m_IsDisplayOn = false;
    protected ResourceName m_CDULayoutPath = "{B1619EA92D9DC98B}UI/layouts/CDU_Base.layout";
	
	[RplProp(onRplName: "OnStatusMessageReplicated")]
	protected string m_sStatusMessage = "";
    
    // Dialog references
    protected ref AG0_TDL_KeyDialog m_inputDialog;
    protected EditBoxWidget m_editBox;
    protected string m_CurrentCypherKey = "";
    
    //------------------------------------------------------------------------------------------------
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        
        if (SCR_Global.IsEditMode())
            return;
        
        // Initialize with display off
        m_IsDisplayOn = false;
		
		m_CDUEntity = owner;
		
		// Get replication component
		m_RplComp = RplComponent.Cast(m_CDUEntity.FindComponent(RplComponent)); // Note: Using CDU entity's RplComp now
		
		if (!m_RplComp)
		{
		    Print("AG0_ControlDisplayUnitComponent: No RplComponent found on CDU entity", LogLevel.ERROR);
		}
		if(Replication.IsServer())
			m_ServerIdentity = RplIdentity.Local();
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsEntitySet()
    {
        return m_CDUEntity != null;
    }
    
    //------------------------------------------------------------------------------------------------
    void TurnOnDisplay()
    {
        if (m_IsDisplayOn)
        {
            Print("AG0_ControlDisplayUnitComponent: Display is already on", LogLevel.WARNING);
            return;
        }
		
		// Refresh entity reference
		if (!m_CDUEntity)
		{
		    m_CDUEntity = GetOwner();
		}
        
        if (!m_CDUEntity)
        {
            Print("AG0_ControlDisplayUnitComponent: Cannot turn on display, entity not set", LogLevel.ERROR);
            return;
        }
        
        // Initialize layout
        InitializeLayout();
        
        if (!m_wRoot || !m_wRTTexture)
        {
            Print("AG0_ControlDisplayUnitComponent: Failed to initialize display components", LogLevel.ERROR);
            return;
        }
        
        // Set the render target
        m_wRTTexture.SetRenderTarget(m_CDUEntity);
        SetDisplayVisibility(true);
        
        m_IsDisplayOn = true;
        Print("AG0_ControlDisplayUnitComponent: Display turned on successfully", LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    void TurnOffDisplay()
    {
        if (!m_IsDisplayOn)
            return;
        
        // Properly remove the render target
        if (m_wRTTexture && m_CDUEntity)
        {
            m_wRTTexture.RemoveRenderTarget(m_CDUEntity);
        }
        
        if (m_wRoot)
        {
            m_wRoot.SetVisible(false);
            m_wRoot.SetEnabled(false);
        }
        
        Cleanup();
        
        m_IsDisplayOn = false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void InitializeLayout()
    {
        Print("AG0_ControlDisplayUnitComponent: Starting InitializeLayout", LogLevel.DEBUG);
        
        if (!GetGame().GetWorkspace())
        {
            Print("AG0_ControlDisplayUnitComponent: Workspace not available", LogLevel.ERROR);
            return;
        }
        
        m_wRoot = GetGame().GetWorkspace().CreateWidgets(m_CDULayoutPath);
        if (!m_wRoot)
        {
            Print("AG0_ControlDisplayUnitComponent: Failed to create root widget", LogLevel.ERROR);
            return;
        }
        
        m_wRTTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("RTTexture0"));
        
        if (!m_wRTTexture)
        {
            Print("AG0_ControlDisplayUnitComponent: Failed to find required widgets", LogLevel.ERROR);
            return;
        }
        
        Print("AG0_ControlDisplayUnitComponent: InitializeLayout completed", LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    protected void SetDisplayVisibility(bool isVisible)
    {
        if (!m_wRoot)
        {
            Print("AG0_ControlDisplayUnitComponent: m_wRoot is null", LogLevel.ERROR);
            return;
        }
        
        // Set visibility for root widget
        m_wRoot.SetVisible(isVisible);
        
        // Set visibility for RTTexture widget
        if (m_wRTTexture)
            m_wRTTexture.SetVisible(isVisible);
        
        // Enable/disable root widget
        m_wRoot.SetEnabled(isVisible);
        
        // Set opacity
        if (isVisible)
        {
            m_wRoot.SetOpacity(1);
        }
        else
        {
            m_wRoot.SetOpacity(0);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    void UpdateDisplay()
    {
        if (!m_IsDisplayOn)
            return;
        
        // Update display content if needed
        // This would be where we update any text widgets with current state
        
        // Example: Update cypher key display
        UpdateCypherKeyDisplay();
    }
    
    //------------------------------------------------------------------------------------------------
    void UpdateCypherKeyDisplay()
    {
        if (!m_wRoot)
            return;
        
        TextWidget cypherKeyDisplay = TextWidget.Cast(m_wRoot.FindAnyWidget("CypherKeyDisplay"));
        if (cypherKeyDisplay)
        {
            cypherKeyDisplay.SetText(m_CurrentCypherKey);
        }
    }
	
	protected void OnStatusMessageReplicated()
	{
	    Print(string.Format("AG0_ControlDisplayUnitComponent::OnStatusMessageReplicated - Received message: '%1'", m_sStatusMessage), LogLevel.DEBUG);
	    
	    // Update UI if display is on
	    if (m_IsDisplayOn && m_wRoot)
	    {
	        TextWidget statusWidget = TextWidget.Cast(m_wRoot.FindAnyWidget("StatusMessage"));
	        if (statusWidget)
	        {
	            statusWidget.SetText(m_sStatusMessage);
	            statusWidget.SetVisible(!m_sStatusMessage.IsEmpty());
	        }
	    }
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_UpdateStatusMessage(string message)
	{
	    // Running on server
	    if (Replication.IsServer())
	    {
	        // Update the replicated property
	        m_sStatusMessage = message;
	        // Ensure replication
	        Replication.BumpMe();
	    }
	}
	
	// Client-side helper to request status update
	protected void RequestStatusUpdate(string message)
	{
	    Rpc(RpcAsk_UpdateStatusMessage, message);
	}
	
	
	//------------------------------------------------------------------------------------------------
	void InitiateCryptoFill(IEntity userEntity)
	{
	    if (!m_RplComp)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Cannot initiate crypto fill - no RplComponent.", LogLevel.WARNING);
	        return;
	    }
	    
	    if (!Replication.IsServer()) 
	    {
	        Print("AG0_ControlDisplayUnitComponent: Cannot initiate crypto fill - not the server.", LogLevel.WARNING);
	        return;
	    }
	    
	    // Store user entity for later use
	    m_TargetUserEntity = userEntity;
	    
	    // Find player controller to get RplIdentity
	    PlayerManager playerManager = GetGame().GetPlayerManager();
	    if (!playerManager)
	    {
	        Print("AG0_ControlDisplayUnitComponent: No player manager available", LogLevel.ERROR);
	        return;
	    }
	    
	    int userPlayerId = playerManager.GetPlayerIdFromControlledEntity(userEntity);
	    SCR_PlayerController playerController = SCR_PlayerController.Cast(playerManager.GetPlayerController(userPlayerId));
	    
	    if (!playerController)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Could not find player controller", LogLevel.ERROR);
	        return;
	    }
	    
	    // Store user's RplIdentity
	    m_UserIdentity = playerController.GetRplIdentity();
	    
	    // Store the current owner before transferring
	    m_OriginalOwner = Replication.FindOwner(m_RplComp.Id());
	    Print(string.Format("AG0_ControlDisplayUnitComponent: Current owner is %1", m_OriginalOwner), LogLevel.DEBUG);
	    
	    // Check if user is already the owner
	    if (m_OriginalOwner == m_UserIdentity)
	    {
	        Print("AG0_ControlDisplayUnitComponent: User is already the owner, opening dialog directly", LogLevel.DEBUG);
	        Rpc(RpcDo_OpenCryptoKeyDialog);
	        return;
	    }
	    
	    Print(string.Format("AG0_ControlDisplayUnitComponent: Giving ownership from %1 to %2", m_OriginalOwner, m_UserIdentity), LogLevel.DEBUG);
	    
	    // Give ownership to the user so they can interact with the dialog
	    m_RplComp.GiveExt(m_UserIdentity, true);
	    Replication.BumpMe();
	    
	    // Wait for ownership change then show dialog
	    GetGame().GetCallqueue().CallLater(WaitForOwnershipChange, 1000, false);
	}
	
	void ClearCryptoFill(IEntity userEntity)
	{
	     if (!m_RplComp)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Cannot drop crypto fill - no RplComponent.", LogLevel.WARNING);
	        return;
	    }
	    
	    if (!Replication.IsServer()) 
	    {
	        Print("AG0_ControlDisplayUnitComponent: Cannot drop crypto fill - not the server.", LogLevel.WARNING);
	        return;
	    }
	    
	    // Find the radio directly using our helper methods
	    IEntity radioEntity = FindActiveRadioViaVON(userEntity);
	    
	    // Fall back to inventory search if needed
	    if (!radioEntity)
	        radioEntity = FindRadioInInventory(userEntity);
	    
	    if (radioEntity)
	    {
	        // Get the TDL component and call DropFillKey
	        AG0_TDLRadioComponent tdlRadioComp = AG0_TDLRadioComponent.Cast(radioEntity.FindComponent(AG0_TDLRadioComponent));
	        if (tdlRadioComp)
	        {
	            tdlRadioComp.DropFillKey();
	            // Maybe update status
	            RequestStatusUpdate("KEY CLEARED");
	        }
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	void WaitForOwnershipChange()
	{
	    // Now on user's machine, show dialog
	    Rpc(RpcDo_OpenCryptoKeyDialog);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_OpenCryptoKeyDialog()
	{
	    if (System.IsConsoleApp())
	        return; // Skip on dedicated server
	        
	    Print("AG0_ControlDisplayUnitComponent: Opening crypto key dialog", LogLevel.DEBUG);
	    
	    // Ensure display is on
	    if (!m_IsDisplayOn)
	    {
	        TurnOnDisplay();
	    }
	    
	    // Create dialog similar to TDL radio component
	    m_inputDialog = AG0_TDL_KeyDialog.CreateKeyDialog("ENTER CRYPTO KEY", "CRYPTO KEY INPUT");
	    
	    // Check if dialog creation failed
	    if (!m_inputDialog || !m_inputDialog.GetRootWidget())
	    {
	        Print("AG0_ControlDisplayUnitComponent: Failed to create key input dialog", LogLevel.ERROR);
	        m_inputDialog = null;
	        return;
	    }
	    
	    // Hook up callbacks
	    m_inputDialog.m_OnConfirm.Insert(OnCryptoDialogConfirm);
	    m_inputDialog.m_OnCancel.Insert(OnCryptoDialogCancel);
	    
	    // Get the input field
	    m_editBox = EditBoxWidget.Cast(m_inputDialog.GetRootWidget().FindAnyWidget("InputField"));
	    
	    if (!m_editBox)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Could not find input field", LogLevel.ERROR);
	        m_inputDialog.Close();
	        m_inputDialog = null;
	        return;
	    }
	    
	    // Focus on the input
	    GetGame().GetWorkspace().SetFocusedWidget(m_editBox);
	    m_editBox.ActivateWriteMode();
	}
    
    //------------------------------------------------------------------------------------------------
   	protected void OnCryptoDialogConfirm(SCR_ConfigurableDialogUi dialog)
	{
	    if (!m_editBox)
	    {
	        Print("AG0_ControlDisplayUnitComponent: OnCryptoDialogConfirm called but m_editBox is null", LogLevel.ERROR);
	        CleanupDialog();
	        return;
	    }
	    
	    // Get entered text
	    string enteredKey = m_editBox.GetText();
	    
	    // Store locally for display
	    m_CurrentCypherKey = enteredKey;
	    UpdateCypherKeyDisplay();
	    
	    // Clean up dialog but KEEP ownership
	    CleanupDialog();
	    
	    // Update status via server
	    RequestStatusUpdate("PTT TO FILL");
	    
	    // Start listening for radio locally
	    StartListeningForRadio();
	}
	
	//------------------------------------------------------------------------------------------------
	// Callback when crypto key dialog is cancelled
	protected void OnCryptoDialogCancel(SCR_ConfigurableDialogUi dialog)
	{
	    CleanupDialog();
	    Rpc(RpcInform_FillCancel);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcInform_FillCancel()
	{
	    // Return ownership to server
	    if (m_RplComp && m_OriginalOwner)
	    {
	        Print(string.Format("AG0_ControlDisplayUnitComponent: Returning ownership to owner %1", m_OriginalOwner), LogLevel.DEBUG);
	        m_RplComp.GiveExt(m_OriginalOwner, true);
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetCryptoKey(string newKey)
	{
	    Print(string.Format("AG0_ControlDisplayUnitComponent: RpcAsk_SetCryptoKey - Received key: '%1' - Finding active radio", newKey), LogLevel.DEBUG);
	    
	    // Store the key on server
	    m_CurrentCypherKey = newKey;
	    
	    // Get the player entity that initiated this call
	    if (!m_TargetUserEntity)
	    {
	        Print("AG0_ControlDisplayUnitComponent: No target user entity available", LogLevel.ERROR);
	        return;
	    }
	    
	    // APPROACH 1: Try to find active radio via VON system
	    IEntity radioEntity = FindActiveRadioViaVON(m_TargetUserEntity);
	    
	    // APPROACH 2: If VON approach fails, search inventory as fallback
	    if (!radioEntity)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Could not find active radio via VON, falling back to inventory search", LogLevel.WARNING);
	        radioEntity = FindRadioInInventory(m_TargetUserEntity);
	    }
	    
	    // Apply key if we found a radio
	    if (radioEntity)
	    {
	        Print(string.Format("AG0_ControlDisplayUnitComponent: Found radio entity, applying key: %1", radioEntity), LogLevel.DEBUG);
	        ApplyKeyToRadio(radioEntity);
	        
	        // Return ownership to original owner
	        if (m_RplComp && m_OriginalOwner)
	        {
	            Print(string.Format("AG0_ControlDisplayUnitComponent: Returning ownership to owner %1", m_OriginalOwner), LogLevel.DEBUG);
	            m_RplComp.GiveExt(m_OriginalOwner, true);
	        }
	        Replication.BumpMe();
	    }
	    else
	    {
	        Print("AG0_ControlDisplayUnitComponent: Could not find any TDL radio to apply key to", LogLevel.ERROR);
	        // Return ownership anyway
	        if (m_RplComp && m_OriginalOwner)
	        {
	            m_RplComp.GiveExt(m_OriginalOwner, true);
	        }
	        Replication.BumpMe();
	    }
	}
	
	// New method to find the active radio via VON system
	protected IEntity FindActiveRadioViaVON(IEntity userEntity)
	{
	    if (!userEntity)
	        return null;
	    
	    // Get the player ID from entity
	    PlayerManager playerManager = GetGame().GetPlayerManager();
	    if (!playerManager)
	        return null;
	    
	    int userPlayerId = playerManager.GetPlayerIdFromControlledEntity(userEntity);
	    
	    // Get the player controller from ID
	    SCR_PlayerController playerController = SCR_PlayerController.Cast(playerManager.GetPlayerController(userPlayerId));
	    if (!playerController)
	        return null;
	    
	    // Get VON controller from player controller
	    SCR_VONController vonController = SCR_VONController.Cast(playerController.FindComponent(SCR_VONController));
	    if (!vonController)
	        return null;
	    
	    // Get active VON entry
	    SCR_VONEntry activeEntry = vonController.GetActiveEntry();
	    if (!activeEntry)
	        return null;
	    
	    // Cast to radio entry
	    SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(activeEntry);
	    if (!radioEntry)
	        return null;
	    
	    // Get transceiver
	    BaseTransceiver transceiver = radioEntry.GetTransceiver();
	    if (!transceiver)
	        return null;
	    
	    // Get radio component
	    BaseRadioComponent radioComp = transceiver.GetRadio();
	    if (!radioComp)
	        return null;
	    
	    // Get radio entity
	    return radioComp.GetOwner();
	}
	
	// Fallback method to find any TDL radio in inventory
	protected IEntity FindRadioInInventory(IEntity userEntity)
	{
	    if (!userEntity)
	        return null;
	    
	    // Create predicate for finding TDL radio
	    AG0_TDLRadioPredicate predicate = new AG0_TDLRadioPredicate();
	    
	    // Get inventory component
	    InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(userEntity.FindComponent(InventoryStorageManagerComponent));
	    if (!inventory)
	        return null;
	    
	    // Search for TDL radio in inventory
	    array<IEntity> foundItems = {};
	    inventory.FindItems(foundItems, predicate);
	    
	    // Return first matching radio if found
	    if (foundItems.Count() > 0)
	    {
	        Print(string.Format("AG0_ControlDisplayUnitComponent: Found %1 TDL radios in inventory, using first one", foundItems.Count()), LogLevel.DEBUG);
	        return foundItems[0];
	    }
	    
	    return null;
	}
	
	//------------------------------------------------------------------------------------------------
	// Cleanup dialog references
	protected void CleanupDialog()
	{
	    if (m_inputDialog)
	    {
	        // Optional: Explicitly clear button listeners
	        m_inputDialog = null;
	    }
	    m_editBox = null;
	}
	
	//------------------------------------------------------------------------------------------------
	protected void StartListeningForRadio()
	{
	    if (System.IsConsoleApp())
	        return; // Skip on dedicated server
	        
	    // Already listening?
	    if (m_bListeningForRadio)
	        return;
	        
	    Print("AG0_ControlDisplayUnitComponent: Starting to listen for radio transmissions (CLIENT)", LogLevel.DEBUG);
	    
	    // Get LOCAL player's VoN component
	    IEntity playerEntity = GetGame().GetPlayerController().GetControlledEntity();
	    if (!playerEntity)
	    {
	        Print("AG0_ControlDisplayUnitComponent: No local player entity for VoN listening", LogLevel.ERROR);
	        return;
	    }
	    
	    // Get VoN component from local player
	    m_VonComponent = SCR_VoNComponent.Cast(playerEntity.FindComponent(SCR_VoNComponent));
	    if (!m_VonComponent)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Could not find SCR_VoNComponent", LogLevel.ERROR);
	        return;
	    }
	    
	    // Subscribe to the OnCapture event
	    m_OnVonCapture = m_VonComponent.m_OnCaptureVON;
	    if (m_OnVonCapture)
	    {
	        m_OnVonCapture.Insert(OnRadioTransmission);
	        m_bListeningForRadio = true;
	        m_fListenTimeout = GetGame().GetWorld().GetWorldTime() + LISTEN_TIMEOUT;
	        
	        Print("AG0_ControlDisplayUnitComponent: Successfully hooked into VoN capture events", LogLevel.DEBUG);
	        
	        // Set a timer to stop listening after timeout
	        GetGame().GetCallqueue().CallLater(CheckListenTimeout, 1000, true);
	    }
	    else
	    {
	        Print("AG0_ControlDisplayUnitComponent: Failed to get OnCaptureVON invoker", LogLevel.ERROR);
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	// Stop listening for radio transmissions
	protected void StopListeningForRadio()
	{
	    if (!m_bListeningForRadio)
	        return;
	        
	    Print("AG0_ControlDisplayUnitComponent: Stopping radio transmission listening", LogLevel.DEBUG);
	    
	    // Unsubscribe from VoN capture events
	    if (m_OnVonCapture)
	    {
	        m_OnVonCapture.Remove(OnRadioTransmission);
	        m_OnVonCapture = null;
	    }
	    
	    m_VonComponent = null;
	    m_bListeningForRadio = false;
	    
	    // Cancel the timeout check
	    GetGame().GetCallqueue().Remove(CheckListenTimeout);
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if listening timeout has been reached
	protected void CheckListenTimeout()
	{
	    if (!m_bListeningForRadio)
	        return;
	        
	    if (GetGame().GetWorld().GetWorldTime() > m_fListenTimeout)
	    {
	        StopListeningForRadio();
	        RequestStatusUpdate("FILL FAILED");
			Rpc(RpcInform_FillCancel);
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	// Called when a radio transmission is captured via VoN
	protected void OnRadioTransmission(BaseTransceiver transmitter)
	{
	    if (!transmitter || !m_bListeningForRadio)
	        return;
	        
	    Print("AG0_ControlDisplayUnitComponent: Captured radio transmission via VoN (CLIENT)", LogLevel.DEBUG);
	    
	    // Get radio component
	    BaseRadioComponent radioComp = transmitter.GetRadio();
	    if (!radioComp)
	    {
	        Print("AG0_ControlDisplayUnitComponent: No radio component found", LogLevel.WARNING);
	        return;
	    }
	    
	    // Get radio entity
	    IEntity radioEntity = radioComp.GetOwner();
	    if (!radioEntity)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Could not get radio entity", LogLevel.WARNING);
	        return;
	    }
	    
	    // Check for TDL component
	    AG0_TDLRadioComponent tdlRadioComp = AG0_TDLRadioComponent.Cast(radioEntity.FindComponent(AG0_TDLRadioComponent));
	    if (!tdlRadioComp)
	    {
	        Print("AG0_ControlDisplayUnitComponent: Radio does not have TDL component", LogLevel.WARNING);
	        return;
	    }
	    RequestStatusUpdate("FILL OK");
	    
	    // Send the key and radio ID to the server
	    Rpc(RpcAsk_SetCryptoKey, m_CurrentCypherKey);
	    
	    // Stop listening
	    StopListeningForRadio();
	}
	
	//------------------------------------------------------------------------------------------------
	// Apply key to radio - directly on server
	protected void ApplyKeyToRadio(IEntity radioEntity)
	{
	    if (!radioEntity || !Replication.IsServer())
	    {
	        Print("AG0_ControlDisplayUnitComponent: ApplyKeyToRadio - Invalid radio entity or not server", LogLevel.ERROR);
	        return;
	    }
	    
	    AG0_TDLRadioComponent tdlRadioComp = AG0_TDLRadioComponent.Cast(radioEntity.FindComponent(AG0_TDLRadioComponent));
	    if (!tdlRadioComp)
	    {
	        Print("AG0_ControlDisplayUnitComponent: ApplyKeyToRadio - No TDL component", LogLevel.ERROR);
	        return;
	    }
	    
	    // Apply the key
	    Print(string.Format("AG0_ControlDisplayUnitComponent: Applying key '%1' to radio", m_CurrentCypherKey), LogLevel.DEBUG);
	    tdlRadioComp.SetCryptoKeyDirectly(m_CurrentCypherKey);
	    
	    // Notify original user - if applicable
	    if (m_TargetUserEntity)
	    {
	        // Could send notification RPC to user
	        // This would be for client-side UI feedback
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	// Cleanup when component is deleted
	void Cleanup()
	{
	    // Stop listening for radio if active
	    if (m_bListeningForRadio)
	    {
	        StopListeningForRadio();
	    }
	    
	    // Remove any pending callbacks
	    GetGame().GetCallqueue().Remove(CheckListenTimeout);
	    
	    // Cleanup dialog
	    CleanupDialog();
	    
	    // Destroy widgets
	    if (m_wRoot)
	    {
	        m_wRoot.RemoveFromHierarchy();
	        m_wRoot = null;
	    }
	    
	    m_wRTTexture = null;
	}
    
    //------------------------------------------------------------------------------------------------
    override void OnDelete(IEntity owner)
    {
        super.OnDelete(owner);
        Cleanup();
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsDisplayOn()
    {
        return m_IsDisplayOn;
    }
}