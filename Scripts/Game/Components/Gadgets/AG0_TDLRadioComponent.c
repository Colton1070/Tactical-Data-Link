[EntityEditorProps(category: "GameScripted/Gadgets", description: "TDL radio gadget", color: "0 0 255 255")]
class AG0_TDLRadioComponentClass : SCR_RadioComponentClass
{
}

class AG0_TDLRadioComponent : SCR_RadioComponent
{
	protected RplComponent rplComp;
	protected AG0_TDLDeviceComponent m_DeviceComp;

	
	// Dialog references
	protected ref AG0_TDL_KeyDialog m_inputDialog;
	protected EditBoxWidget m_editBox;
	
	protected ref AG0_TDL_KeyDialog m_networkDialog;
	protected EditBoxWidget m_networkNameEdit;
	protected EditBoxWidget m_networkPasswordEdit;
	protected bool m_bCreateNetworkMode = false;
	
	[RplProp(onRplName: "OnKeyReplicated")]
	protected string m_sCurrentCryptoKey;
	
	[RplProp()]
	protected int m_iCurrentNetworkID = -1;
	
	[RplProp()]
	protected ref array<RplId> m_aConnectedRadioIDs = {};
	
	protected string m_sDefaultCryptoKey;
	
	protected RplIdentity mfkerRplIdentity;
	protected RplIdentity serverRplIdentity;
	
	
	//Test
	[RplProp()]
	protected ref array<ref AG0_TDLNetworkMember> m_mArrayConnectedMembers = new array<ref AG0_TDLNetworkMember>();
	
	[RplProp()]
	protected ref array<RplId> m_mConnectedMembers = new array<RplId>();
	
	[Attribute(defvalue: "1", desc: "Block transmit while receiving on same frequency (half-duplex operation)", category: "TDL Radio")]
	protected bool m_bHalfDuplexEnabled;
	
	[Attribute(defvalue: "1", desc: "Allow full duplex when device is connected to TDL network", category: "TDL Radio")]
	protected bool m_bFullDuplexWhenNetworked;
	
	// ============================================================================
	// FREQUENCY HOPPING MEMBERS - Server-authoritative
	// ============================================================================
	
	[Attribute("0.25", UIWidgets.Slider, "Hop rate (hops per second)", "0.1 10 0.05", category: "TDL Radio")]
	protected float m_fHopRate;
	
	[RplProp(onRplName: "OnFHStateReplicated")]
	protected int m_iFHEnabledMask = 0;
	
	// Server-authoritative hop slot - replicated to all clients
	[RplProp(onRplName: "OnHopSlotReplicated")]
	protected int m_iCurrentHopSlot = 0;
	
	protected ref map<int, ref AG0_FrequencyHopPattern> m_mHopPatterns = new map<int, ref AG0_FrequencyHopPattern>();
	protected bool m_bFHUpdateActive = false;
	
	// 
	//
	// CRYPTO KEY FILL METHODS BELOW
	//
	//

	//------------------------------------------------------------------------------------------------
	// Public method to be called by the User Action
	//------------------------------------------------------------------------------------------------
	
	void DropFillKey()
	{
		if (!rplComp || !Replication.IsServer())
	    {
	        //Print("AG0_TDLRadioComponent: Cannot drop fill key - not the server.", LogLevel.WARNING);
	        return;
	    }
		if (m_BaseRadioComp)
		{
			//Print(string.Format("'%1' AG0_TDLRadioComponent::DropFillKey - Setting BaseRadioComp key to default: %2", GetOwner(), m_sDefaultCryptoKey), LogLevel.DEBUG);
	
			m_sCurrentCryptoKey = m_sDefaultCryptoKey;

			//OnKeyReplicated();
	
			// Ensure replication system knows the property changed
			Replication.BumpMe();
			
			GetGame().GetCallqueue().CallLater(WaitForOwnershipReset, 1000, false);
		}
		else
		{
			//Print(string.Format("'%1' AG0_TDLRadioComponent::DropFillKey m_BaseRadioComp is NULL!", GetOwner()), LogLevel.WARNING);
		}
	}
	
	void FillKey(IEntity userEntity)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (m_BaseRadioComp && playerManager)
		{
			//Print(string.Format("'%1' AG0_TDLRadioComponent::FillKey - attempting to fill key", GetOwner()), LogLevel.DEBUG);
			int userPlayerId = playerManager.GetPlayerIdFromControlledEntity(userEntity);
			SCR_PlayerController playerController = SCR_PlayerController.Cast(playerManager.GetPlayerController(userPlayerId));
			if(!playerController) {
				return;
			}
			mfkerRplIdentity = playerController.GetRplIdentity();
			
			//Print(mfkerRplIdentity);
			

			//PrintFormat("Giving ownership from %1 to %2", serverRplIdentity, mfkerRplIdentity);
			rplComp.GiveExt(mfkerRplIdentity, true);
			
			Replication.BumpMe();
			
			GetGame().GetCallqueue().CallLater(WaitForOwnershipChange, 1000, false);
		}
		else
		{
			//Print(string.Format("'%1' AG0_TDLRadioComponent::FillKey m_BaseRadioComp is NULL!", GetOwner()), LogLevel.WARNING);
		}
	}
	
	void WaitForOwnershipChange()
	{
		Rpc(RpcDo_OpenKeyEntryDialog);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_OpenKeyEntryDialog()
	{
		if(System.IsConsoleApp())
			return; //fuck you
		//Print("AG0_TDLRadioComponent: Activating crypto key input", LogLevel.DEBUG);
		
		ShowKeyDialog();

		//Print("AG0_TDLRadioComponent: Not the player, fuck em.", LogLevel.DEBUG);
	}
	
	protected void ShowKeyDialog()
	{
		//Print("AG0_TDLRadioComponent: Attempt show key dialog.", LogLevel.DEBUG);
		// Prevent opening multiple dialogs
		if (m_inputDialog)
		{
			//Print("AG0_TDLRadioComponent: Key input dialog already open.", LogLevel.WARNING);
			// Optionally bring existing dialog to focus if possible/needed
			// WorkspaceWidget workspace = GetGame().GetWorkspace();
			// if (workspace && m_inputDialog.GetRootWidget())
			//   workspace.SetFocusedWidget(m_inputDialog.GetRootWidget()); // Might not work as expected
			return;
		}

		// Create the dialog - using the same preset as the CDU example
		// Make sure "dialog_cypherkey" preset exists in TDL_Dialogs.conf and has an "InputField" EditBoxWidget.
		m_inputDialog = AG0_TDL_KeyDialog.CreateKeyDialog("ENTER CRYPTO KEY", "CRYPTO KEY INPUT");

		//Print("AG0_TDLRadioComponent: Dialog created.", LogLevel.DEBUG);
		// Check if dialog creation failed
		if (!m_inputDialog || !m_inputDialog.GetRootWidget())
		{
			//Print("AG0_TDLRadioComponent: Failed to create or initialize key input dialog.", LogLevel.ERROR);
			m_inputDialog = null; // Ensure reference is cleared
			return;
		}

		// Hook up callbacks
		
		//Print("AG0_TDLRadioComponent: Adding callbacks.", LogLevel.DEBUG);
		
		m_inputDialog.m_OnConfirm.Insert(OnDialogConfirm);
		m_inputDialog.m_OnCancel.Insert(OnDialogCancel);
		//m_inputDialog.m_OnClose.Insert(OnDialogCancel); // Treat close as cancel

		// Get the input field
		m_editBox = EditBoxWidget.Cast(m_inputDialog.GetRootWidget().FindAnyWidget("InputField"));

		if (!m_editBox)
		{
			//Print("AG0_TDLRadioComponent: Could not find 'InputField' in the dialog layout!", LogLevel.ERROR);
			// Close the invalid dialog
			m_inputDialog.Close();
			m_inputDialog = null;
			return;
		}

		// Set initial text if needed (e.g., show current key)
		// m_editBox.SetText(m_sCurrentCryptoKey);
		//Print("AG0_TDLRadioComponent: Trying to focus widget.", LogLevel.DEBUG);
		// Focus the input field - This makes the dialog immediately usable
		GetGame().GetWorkspace().SetFocusedWidget(m_editBox);
		// No need to activate write mode manually, focus usually handles this for EditBox
		m_editBox.ActivateWriteMode(); // Generally not needed if focused
		//Print("AG0_TDLRadioComponent: Activating write mode.", LogLevel.DEBUG);
	}
	
	protected void OnKeyReplicated()
	{
		//Print(string.Format("'%1' AG0_TDLRadioComponent::OnKeyReplicated - Received Key: '%2'", GetOwner(), m_sCurrentCryptoKey), LogLevel.DEBUG);
		// Update the actual BaseRadioComponent everywhere
		// This ensures consistency even if BaseRadioComponent itself doesn't replicate the key.
		if (m_BaseRadioComp)
		{

			//Print(string.Format("'%1' AG0_TDLRadioComponent::OnKeyReplicated - Setting BaseRadioComp key to '%2'", GetOwner(), m_sCurrentCryptoKey), LogLevel.DEBUG);

			m_BaseRadioComp.SetEncryptionKey(m_sCurrentCryptoKey);
		}
		// If key was cleared (back to default), disable FH on all transceivers
	    if (m_sCurrentCryptoKey == m_sDefaultCryptoKey)
	    {
	        if (m_iFHEnabledMask != 0)
	        {
	            Print("AG0_FH: Crypto key cleared, disabling FH on all transceivers", LogLevel.DEBUG);
	            
	            // Clear all FH state
	            m_iFHEnabledMask = 0;
	            m_mHopPatterns.Clear();
	            StopFHUpdateLoop();
	            
	            // Bump replication if we're server
	            if (Replication.IsServer())
	                Replication.BumpMe();
	        }
	        return;
	    }
	    
	    // Key changed but still valid - regenerate patterns for active FH transceivers
	    if (m_iFHEnabledMask != 0 && m_BaseRadioComp)
	    {
	        int transceiverCount = m_BaseRadioComp.TransceiversCount();
	        
	        for (int i = 0; i < transceiverCount; i++)
	        {
	            if (!IsFrequencyHopEnabled(i))
	                continue;
	            
	            BaseTransceiver tsv = m_BaseRadioComp.GetTransceiver(i);
	            if (!tsv)
	                continue;
	            
	            AG0_FrequencyHopPattern pattern = new AG0_FrequencyHopPattern();
	            pattern.GenerateFromKey(
	                m_sCurrentCryptoKey,
	                tsv.GetMinFrequency(),
	                tsv.GetMaxFrequency(),
	                tsv.GetFrequencyResolution()
	            );
	            
	            m_mHopPatterns.Set(i, pattern);
	            
	            Print(string.Format("AG0_FH: Regenerated pattern for transceiver %1 with new key", i), LogLevel.DEBUG);
	        }
	    }

	}
	
	//------------------------------------------------------------------------------------------------
	// RPC Method: Called BY a client, executed ON the SERVER
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetCryptoKey(string newKey)
	{
		//Print("AG0_TDLRadioComponent::RpcAsk_SetCryptoKey - Received request to set key (Server)");

		// --- SERVER-SIDE VALIDATION ---
		// Add any checks here (e.g., key length, allowed characters, player permissions)
		// if (!IsValidCryptoKey(newKey)) { Print("Invalid key format received.", LogLevel.WARNING); return; }
		// ---

		// Update the replicated property - this will trigger OnKeyReplicated on all clients & server
		m_sCurrentCryptoKey = newKey;
		
		if(serverRplIdentity)
			rplComp.GiveExt(serverRplIdentity, true);

		// Explicitly call the replication callback on the server immediately
		// because RplProp callbacks might not trigger instantly on the server itself.
		// Alternatively, just set the BaseRadioComponent key directly here too.
		//OnKeyReplicated();

		// Ensure replication system knows the property changed
		Replication.BumpMe();
		
		GetGame().GetCallqueue().CallLater(WaitForOwnershipReset, 1000, false);
	}
	
	void WaitForOwnershipReset()
	{
		if(m_BaseRadioComp) {
			//PrintFormat("Reset ownership to server, setting encryption key for Radio on server to %1.", m_sCurrentCryptoKey);
			m_BaseRadioComp.SetEncryptionKey(m_sCurrentCryptoKey);
		}
	}


	//------------------------------------------------------------------------------------------------
	// Callback when the dialog's Confirm button is pressed (CLIENT-SIDE ONLY)
	//------------------------------------------------------------------------------------------------
	protected void OnDialogConfirm(SCR_ConfigurableDialogUi dialog)
	{
		// Should already be on client due to OpenKeyEntryDialog check, but good practice:
		if (!rplComp || !rplComp.IsOwner()) return;

		if (!m_editBox)
		{
			//Print("AG0_TDLRadioComponent: OnDialogConfirm called but m_editBox is null!", LogLevel.ERROR);
			CleanupDialogRefs();
			return;
		}

		string enteredText = m_editBox.GetText();
		//Print(string.Format("AG0_TDLRadioComponent: Crypto key entered locally: '%1' (Client)", enteredText), LogLevel.DEBUG);

		// --- Send the key to the Server via RPC ---
		Rpc(RpcAsk_SetCryptoKey, enteredText);
		// ---

		// Optional: Update local display immediately for responsiveness,
		// but be aware it will be overwritten by replication shortly.
		// m_sCurrentCryptoKey = enteredText; // Not recommended if using RplProp
		// UpdateCypherKeyDisplay();          // If you have a local display

		CleanupDialogRefs();
	}

	//------------------------------------------------------------------------------------------------
	// Callback when the dialog's Cancel or Close button is pressed
	//------------------------------------------------------------------------------------------------
	protected void OnDialogCancel(SCR_ConfigurableDialogUi dialog)
	{
		// Should already be on client
		if (!GetGame().GetPlayerController()) return;

		//Print("AG0_TDLRadioComponent: Crypto key input cancelled (Client).", LogLevel.DEBUG);
		CleanupDialogRefs();
	}

	//------------------------------------------------------------------------------------------------
	// Helper to nullify dialog references
	//------------------------------------------------------------------------------------------------
	protected void CleanupDialogRefs()
	{
		// The dialog usually closes itself when a button is pressed,
		// but we need to clear our references to it.
		if (m_inputDialog)
		{
			// Optional: Explicitly clear button listeners if ClearButtons() is necessary,
			// but dialog closure often handles this.
			// AG0_TDL_KeyDialog castDialog = AG0_TDL_KeyDialog.Cast(m_inputDialog);
			// if (castDialog) castDialog.ClearButtons();

			m_inputDialog = null;
		}
		m_editBox = null;
	}
	//------------------------------------------------------------------------------------------------
	// Ensure dialog is closed if the component is deleted
	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		// Check if we are on a client before trying to interact with UI
		if (GetGame().GetPlayerController() && m_inputDialog)
		{
			m_inputDialog.Close();
			CleanupDialogRefs();
		}
		
		StopFHUpdateLoop();
	    m_mHopPatterns.Clear();
		
		super.OnDelete(owner);
	}
	
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		rplComp = RplComponent.Cast(owner.FindComponent(RplComponent));
		
		if(rplComp && Replication.IsServer()) {
			serverRplIdentity = RplIdentity.Local();
		}
		
		if (m_BaseRadioComp && m_BaseRadioComp.GetEncryptionKey() != m_sCurrentCryptoKey)
		{
			if(!m_sDefaultCryptoKey)
			{
				m_sDefaultCryptoKey = m_BaseRadioComp.GetEncryptionKey();
			}
			m_sCurrentCryptoKey = m_BaseRadioComp.GetEncryptionKey();
			//Print(string.Format("'%1' AG0_TDLRadioComponent::EOnInit - Syncing our default key to '%2'", owner, m_BaseRadioComp.GetEncryptionKey()), LogLevel.DEBUG);
		}
		m_DeviceComp = AG0_TDLDeviceComponent.Cast(owner.FindComponent(AG0_TDLDeviceComponent));
	}
	
	float GetNetworkRange() {
		if(!m_BaseRadioComp)
			return 0;
		if(m_BaseRadioComp.TransceiversCount() == 0)
			return 0;
		BaseTransceiver baseRadioTransceiver = m_BaseRadioComp.GetTransceiver(0);
		if(!baseRadioTransceiver)
			return 0;
		return baseRadioTransceiver.GetRange();
	}
	
	
	//------------------------------------------------------------------------------------------------
	// Setter for the current key if needed elsewhere
	//------------------------------------------------------------------------------------------------
	
	void SetCryptoKeyDirectly(string newKey)
	{
	    if (!rplComp || !Replication.IsServer())
	    {
	        //Print("AG0_TDLRadioComponent: Cannot set crypto key directly - not the server.", LogLevel.WARNING);
	        return;
	    }
	    
	    if (m_BaseRadioComp)
	    {
	        //Print(string.Format("'%1' AG0_TDLRadioComponent::SetCryptoKeyDirectly - Setting key to '%2'", GetOwner(), newKey), LogLevel.DEBUG);
	        
	        // Update the replicated property
	        m_sCurrentCryptoKey = newKey;
	        
	        // Update the actual radio component
	        m_BaseRadioComp.SetEncryptionKey(m_sCurrentCryptoKey);
	        
	        // Ensure replication system knows the property changed
	        Replication.BumpMe();
	    }
	    else
	    {
	        //Print(string.Format("'%1' AG0_TDLRadioComponent::SetCryptoKeyDirectly - m_BaseRadioComp is NULL!", GetOwner()), LogLevel.WARNING);
	    }
	}

	//------------------------------------------------------------------------------------------------
	// Getter for the current key if needed elsewhere
	//------------------------------------------------------------------------------------------------
	string GetCurrentCryptoKey()
	{
		return m_sCurrentCryptoKey;
	}
	
	string GetDefaultCryptoKey()
	{
		return m_sDefaultCryptoKey;
	}
	
	bool ShouldBlockHalfDuplex()
	{
	    if (!m_bHalfDuplexEnabled)
	        return false;
	    
	    // Full duplex allowed when networked
	    if (m_bFullDuplexWhenNetworked && m_DeviceComp && m_DeviceComp.IsInNetwork())
	        return false;
	    
	    return true; // Caller still needs to check if actually receiving
	}
	
	// ============================================================================
	// FREQUENCY HOPPING - Server-Authoritative Implementation
	// ============================================================================
	
	//------------------------------------------------------------------------------------------------
	// Toggle FH mode for a specific transceiver
	//------------------------------------------------------------------------------------------------
	void ToggleFrequencyHop(int transceiverIdx = 0)
	{
	    if (!Replication.IsServer())
	    {
	        Rpc(RpcAsk_ToggleFrequencyHop, transceiverIdx);
	        return;
	    }
	    
	    ServerToggleFrequencyHop(transceiverIdx);
	}
	
	//------------------------------------------------------------------------------------------------
	// Server-side toggle implementation
	//------------------------------------------------------------------------------------------------
	protected void ServerToggleFrequencyHop(int transceiverIdx)
	{
	    if (!m_BaseRadioComp || transceiverIdx >= m_BaseRadioComp.TransceiversCount())
	    {
	        Print(string.Format("AG0_FH: Invalid transceiver index %1", transceiverIdx), LogLevel.WARNING);
	        return;
	    }
	    
	    bool currentlyEnabled = IsFrequencyHopEnabled(transceiverIdx);
	    bool newState = !currentlyEnabled;
	    
	    // Require crypto key to ENABLE FH
	    if (newState && m_sCurrentCryptoKey == m_sDefaultCryptoKey)
	    {
	        Print("AG0_FH: Cannot enable FH without crypto key fill", LogLevel.WARNING);
	        return;
	    }
	    
	    if (newState)
	    {
	        // Generate pattern for this transceiver
	        BaseTransceiver tsv = m_BaseRadioComp.GetTransceiver(transceiverIdx);
	        if (!tsv)
	            return;
	        
	        AG0_FrequencyHopPattern pattern = new AG0_FrequencyHopPattern();
	        pattern.GenerateFromKey(
	            m_sCurrentCryptoKey,
	            tsv.GetMinFrequency(),
	            tsv.GetMaxFrequency(),
	            tsv.GetFrequencyResolution()
	        );
	        
	        if (!pattern.IsValid())
	        {
	            Print("AG0_FH: Failed to generate hop pattern", LogLevel.WARNING);
	            return;
	        }
	        
	        m_mHopPatterns.Set(transceiverIdx, pattern);
	        
	        // Set bit in mask
	        m_iFHEnabledMask = m_iFHEnabledMask | (1 << transceiverIdx);
	        
	        Print(string.Format("AG0_FH: ENABLED for transceiver %1 on %2", transceiverIdx, GetOwner()), LogLevel.DEBUG);
	        
	        // Start update loop on server
	        StartFHUpdateLoop();
	    }
	    else
	    {
	        // Clear bit in mask
	        m_iFHEnabledMask = m_iFHEnabledMask & ~(1 << transceiverIdx);
	        
	        // Clean up pattern
	        m_mHopPatterns.Remove(transceiverIdx);
	        
	        Print(string.Format("AG0_FH: DISABLED for transceiver %1 on %2", transceiverIdx, GetOwner()), LogLevel.DEBUG);
	        
	        // Stop update loop if no transceivers hopping
	        if (m_iFHEnabledMask == 0)
	            StopFHUpdateLoop();
	    }
	    
	    Replication.BumpMe();
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_ToggleFrequencyHop(int transceiverIdx)
	{
	    ServerToggleFrequencyHop(transceiverIdx);
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if FH is enabled for a specific transceiver
	//------------------------------------------------------------------------------------------------
	bool IsFrequencyHopEnabled(int transceiverIdx = 0)
	{
	    return (m_iFHEnabledMask & (1 << transceiverIdx)) != 0;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if ANY transceiver has FH enabled
	//------------------------------------------------------------------------------------------------
	bool HasAnyFrequencyHopEnabled()
	{
	    return m_iFHEnabledMask != 0;
	}
	
	//------------------------------------------------------------------------------------------------
	// Get current hop slot (returns server-authoritative replicated value)
	//------------------------------------------------------------------------------------------------
	int GetCurrentHopSlot()
	{
	    return m_iCurrentHopSlot;
	}
	
	//------------------------------------------------------------------------------------------------
	// Get current frequency for a transceiver (uses replicated hop slot)
	//------------------------------------------------------------------------------------------------
	int GetCurrentFrequency(int transceiverIdx = 0)
	{
	    if (!m_BaseRadioComp || transceiverIdx >= m_BaseRadioComp.TransceiversCount())
	        return 0;
	    
	    BaseTransceiver tsv = m_BaseRadioComp.GetTransceiver(transceiverIdx);
	    if (!tsv)
	        return 0;
	    
	    // If FH enabled and we have a valid pattern, calculate hopped frequency
	    if (IsFrequencyHopEnabled(transceiverIdx))
	    {
	        AG0_FrequencyHopPattern pattern = m_mHopPatterns.Get(transceiverIdx);
	        if (pattern && pattern.IsValid())
	        {
	            // Use replicated slot, not calculated from world time
	            return pattern.GetFrequencyForSlot(m_iCurrentHopSlot);
	        }
	    }
	    
	    // Otherwise return the fixed frequency
	    return tsv.GetFrequency();
	}
	
	//------------------------------------------------------------------------------------------------
	// Get hop rate
	//------------------------------------------------------------------------------------------------
	int GetHopRate()
	{
	    return m_fHopRate;
	}
	
	// ============================================================================
	// FH UPDATE LOOP - SERVER ONLY
	// ============================================================================
	
	//------------------------------------------------------------------------------------------------
	protected void StartFHUpdateLoop()
	{
	    // Only server runs the hop timer
	    if (!Replication.IsServer())
	        return;
	    
	    if (m_bFHUpdateActive)
	        return;
	    
	    m_bFHUpdateActive = true;
	    
	    // Calculate update interval in ms
	    float intervalMs = 1000.0 / m_fHopRate;
		GetGame().GetCallqueue().CallLater(UpdateFrequencyHopLoop, intervalMs, true);
	    
	    Print(string.Format("AG0_FH: Server started hop loop at %1 Hz (%2 ms interval)", m_fHopRate, intervalMs), LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void StopFHUpdateLoop()
	{
	    if (!m_bFHUpdateActive)
	        return;
	    
	    m_bFHUpdateActive = false;
	    GetGame().GetCallqueue().Remove(UpdateFrequencyHopLoop);
	    
	    Print("AG0_FH: Stopped hop loop", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	// SERVER ONLY - Increments hop slot and replicates
	//------------------------------------------------------------------------------------------------
	protected void UpdateFrequencyHopLoop()
	{
	    if (!Replication.IsServer())
	        return;
	    
	    if (m_iFHEnabledMask == 0)
	    {
	        StopFHUpdateLoop();
	        return;
	    }
	    
	    // Calculate slot from world time - NOT increment
	    World world = GetGame().GetWorld();
	    if (!world)
	        return;
	    
	    float worldTimeSec = world.GetWorldTime() / 1000.0;
	    float dwellTimeSec = 1.0 / m_fHopRate;
		int newSlot = Math.Floor(worldTimeSec / dwellTimeSec);
	    
	    // Only update if slot changed
	    if (newSlot == m_iCurrentHopSlot)
	        return;
	    
	    m_iCurrentHopSlot = newSlot;
	    
	    // Update frequencies on server
	    ApplyHopSlotToTransceivers();
	    
	    // Replicate the new slot to clients
	    Replication.BumpMe();
	}
	
	//------------------------------------------------------------------------------------------------
	// Apply current hop slot to all FH-enabled transceivers
	//------------------------------------------------------------------------------------------------
	protected void ApplyHopSlotToTransceivers()
	{
	    if (!m_BaseRadioComp)
	        return;
	    
	    int transceiverCount = m_BaseRadioComp.TransceiversCount();
	    
	    for (int i = 0; i < transceiverCount; i++)
	    {
	        if (!IsFrequencyHopEnabled(i))
	            continue;
	        
	        AG0_FrequencyHopPattern pattern = m_mHopPatterns.Get(i);
	        if (!pattern || !pattern.IsValid())
	            continue;
	        
	        int newFreq = pattern.GetFrequencyForSlot(m_iCurrentHopSlot);
	        
	        BaseTransceiver tsv = m_BaseRadioComp.GetTransceiver(i);
	        if (!tsv)
	            continue;
	        
	        if (tsv.GetFrequency() != newFreq)
	            tsv.SetFrequency(newFreq);
	    }
	}
	
	// ============================================================================
	// FH CALLBACKS
	// ============================================================================
	
	//------------------------------------------------------------------------------------------------
	// Called when hop slot replicates to clients
	//------------------------------------------------------------------------------------------------
	protected void OnHopSlotReplicated()
	{
	    if (Replication.IsServer())
	        return;
	    
	    // Calculate what we think the slot should be
	    int predictedSlot = CalculateHopSlotFromWorldTime();
	    
	    // If we're more than 1 slot off from server, snap to server
	    int drift = Math.AbsInt(predictedSlot - m_iCurrentHopSlot);
	    if (drift > 1)
	    {
	        Print(string.Format("AG0_FH: Client drift detected (%1 slots), resyncing", drift), LogLevel.DEBUG);
	        ApplyHopSlotToTransceivers();
	    }
	    // Otherwise trust our local prediction
	}
	
	//------------------------------------------------------------------------------------------------
	protected int CalculateHopSlotFromWorldTime()
	{
	    World world = GetGame().GetWorld();
	    if (!world)
	        return 0;
	    
	    float worldTimeSec = world.GetWorldTime() / 1000.0;
	    float dwellTimeSec = 1.0 / m_fHopRate;
	    return Math.Floor(worldTimeSec / dwellTimeSec);
	}
	
	//------------------------------------------------------------------------------------------------
	// Called when FH state replicates to clients
	//------------------------------------------------------------------------------------------------
	protected void OnFHStateReplicated()
	{
	    Print(string.Format("AG0_FH: State replicated, mask = %1", m_iFHEnabledMask), LogLevel.DEBUG);
	    
	    // Regenerate patterns on client when FH state changes
	    if (m_iFHEnabledMask != 0)
	    {
	        RegenerateClientPatterns();
	        // Don't start loop on client - client just reacts to OnHopSlotReplicated
	    }
	}
	
	//------------------------------------------------------------------------------------------------
	// Regenerate patterns on client for frequency calculation
	//------------------------------------------------------------------------------------------------
	protected void RegenerateClientPatterns()
	{
	    if (!m_BaseRadioComp)
	        return;
	    
	    int transceiverCount = m_BaseRadioComp.TransceiversCount();
	    
	    for (int i = 0; i < transceiverCount; i++)
	    {
	        if (!IsFrequencyHopEnabled(i))
	            continue;
	        
	        // Skip if already have valid pattern with same key
	        AG0_FrequencyHopPattern existing = m_mHopPatterns.Get(i);
	        if (existing && existing.IsValid() && existing.GetCryptoSeed() == m_sCurrentCryptoKey)
	            continue;
	        
	        BaseTransceiver tsv = m_BaseRadioComp.GetTransceiver(i);
	        if (!tsv)
	            continue;
	        
	        AG0_FrequencyHopPattern pattern = new AG0_FrequencyHopPattern();
	        pattern.GenerateFromKey(
	            m_sCurrentCryptoKey,
	            tsv.GetMinFrequency(),
	            tsv.GetMaxFrequency(),
	            tsv.GetFrequencyResolution()
	        );
	        
	        m_mHopPatterns.Set(i, pattern);
	        
	        Print(string.Format("AG0_FH: Client generated pattern for transceiver %1", i), LogLevel.DEBUG);
	    }
	}
	
}

//------------------------------------------------------------------------------------------------
// AG0_FrequencyHopPattern.c
// Generates deterministic frequency hop sequences from crypto key + transceiver specs
//------------------------------------------------------------------------------------------------

class AG0_FrequencyHopPattern
{
    protected ref array<int> m_aHopSequence = {};
    protected int m_iSequenceLength;
    protected string m_sCryptoSeed;
    
    // Transceiver specs this pattern was generated for
    protected int m_iMinFreq;
    protected int m_iMaxFreq;
    protected int m_iResolution;
    
    //------------------------------------------------------------------------------------------------
    // Generate hop sequence from crypto key and transceiver frequency bounds
    //------------------------------------------------------------------------------------------------
    void GenerateFromKey(string cryptoKey, int minFreq, int maxFreq, int resolution)
    {
        if (cryptoKey.IsEmpty() || resolution <= 0)
            return;
        
        m_sCryptoSeed = cryptoKey;
        m_iMinFreq = minFreq;
        m_iMaxFreq = maxFreq;
        m_iResolution = resolution;
        m_aHopSequence.Clear();
        
        // Calculate number of possible frequencies
        int numChannels = ((maxFreq - minFreq) / resolution) + 1;
        if (numChannels <= 0)
            return;
        
        // Build index array
        array<int> indices = {};
        for (int i = 0; i < numChannels; i++)
            indices.Insert(i);
        
        // Shuffle using seeded PRNG
        int seed = HashString(cryptoKey);
        ShuffleWithSeed(indices, seed);
        
        // Use up to 64 frequencies in hop set (or all if fewer)
        m_iSequenceLength = Math.Min(numChannels, 64);
        for (int i = 0; i < m_iSequenceLength; i++)
            m_aHopSequence.Insert(indices[i]);
        
        Print(string.Format("AG0_FH: Generated pattern - %1 hops from %2 channels (key: %3, range: %4-%5 kHz, res: %6)", 
            m_iSequenceLength, numChannels, cryptoKey, minFreq, maxFreq, resolution), LogLevel.DEBUG);
    }
    
    //------------------------------------------------------------------------------------------------
    // Get frequency for a given hop slot
    //------------------------------------------------------------------------------------------------
    int GetFrequencyForSlot(int hopSlot)
    {
        if (m_aHopSequence.IsEmpty() || m_iResolution <= 0)
            return m_iMinFreq;
        
        int index = m_aHopSequence[Math.AbsInt(hopSlot) % m_iSequenceLength];
        return m_iMinFreq + (index * m_iResolution);
    }
    
    //------------------------------------------------------------------------------------------------
    int GetSequenceLength()
    {
        return m_iSequenceLength;
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsValid()
    {
        return !m_aHopSequence.IsEmpty() && m_iSequenceLength > 0;
    }
    
    //------------------------------------------------------------------------------------------------
    // Check if another pattern is compatible (same specs = same frequencies)
    //------------------------------------------------------------------------------------------------
    bool IsCompatibleWith(AG0_FrequencyHopPattern other)
    {
        if (!other)
            return false;
        
        return m_iMinFreq == other.m_iMinFreq &&
               m_iMaxFreq == other.m_iMaxFreq &&
               m_iResolution == other.m_iResolution &&
               m_sCryptoSeed == other.m_sCryptoSeed;
    }
    
    //------------------------------------------------------------------------------------------------
    // Getters for specs
    //------------------------------------------------------------------------------------------------
    int GetMinFreq() { return m_iMinFreq; }
    int GetMaxFreq() { return m_iMaxFreq; }
    int GetResolution() { return m_iResolution; }
    string GetCryptoSeed() { return m_sCryptoSeed; }
    
    //------------------------------------------------------------------------------------------------
    // Simple string hash for deterministic seed
    //------------------------------------------------------------------------------------------------
    protected int HashString(string str)
    {
        int hash = 5381;
        for (int i = 0; i < str.Length(); i++)
        {
            hash = ((hash << 5) + hash) + str.ToAscii(i);
        }
        return Math.AbsInt(hash);
    }
    
    //------------------------------------------------------------------------------------------------
    // Fisher-Yates shuffle with seeded LCG random
    //------------------------------------------------------------------------------------------------
    protected void ShuffleWithSeed(array<int> arr, int seed)
    {
        int state = seed;
        
        for (int i = arr.Count() - 1; i > 0; i--)
        {
            // LCG: state = (a * state + c) mod m
            state = (1103515245 * state + 12345) & 0x7FFFFFFF;
            int j = state % (i + 1);
            
            // Swap
            int temp = arr[i];
            arr[i] = arr[j];
            arr[j] = temp;
        }
    }
}