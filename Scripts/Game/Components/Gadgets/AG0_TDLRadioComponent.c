[EntityEditorProps(category: "GameScripted/Gadgets", description: "TDL radio gadget", color: "0 0 255 255")]
class AG0_TDLRadioComponentClass : SCR_RadioComponentClass
{
}

//IF you are reading this sadness. There are a couple things I would like to write in my pity diary.
//1. Players holding a radio do not own the radio.
//2. Dialogs must be rendered on client ONLY, otherwise will hang players/crash server.
//3. User Actions are done on client and server, but client can't tell the server to do shit.
//In light of the above, we have to execute user action on server, give ownership to calling user entity (RplIdentity NOT RplId)
//Then when they have ownership, they can RPC input back to server.
//Server can regain ownership and replicate radio props.
//This is so much pain for a simple feature.


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
	
	[Attribute(defvalue: "1", desc: "Allow full duplex when device is connected to TDL network")]
	protected bool m_bFullDuplexWhenNetworked;
	
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
			//Print(string.Format("'%1' AG0_TDLRadioComponent::DropFillKey - Setting BaseRadioComp key to default: %2", GetOwner(), m_sDefaultCryptoKey), LogLevel.NORMAL);
	
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
			//Print(string.Format("'%1' AG0_TDLRadioComponent::FillKey - attempting to fill key", GetOwner()), LogLevel.NORMAL);
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
		//Print("AG0_TDLRadioComponent: Activating crypto key input", LogLevel.NORMAL);
		
		ShowKeyDialog();

		//Print("AG0_TDLRadioComponent: Not the player, fuck em.", LogLevel.NORMAL);
	}
	
	protected void ShowKeyDialog()
	{
		//Print("AG0_TDLRadioComponent: Attempt show key dialog.", LogLevel.NORMAL);
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

		//Print("AG0_TDLRadioComponent: Dialog created.", LogLevel.NORMAL);
		// Check if dialog creation failed
		if (!m_inputDialog || !m_inputDialog.GetRootWidget())
		{
			//Print("AG0_TDLRadioComponent: Failed to create or initialize key input dialog.", LogLevel.ERROR);
			m_inputDialog = null; // Ensure reference is cleared
			return;
		}

		// Hook up callbacks
		
		//Print("AG0_TDLRadioComponent: Adding callbacks.", LogLevel.NORMAL);
		
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
		//Print("AG0_TDLRadioComponent: Trying to focus widget.", LogLevel.NORMAL);
		// Focus the input field - This makes the dialog immediately usable
		GetGame().GetWorkspace().SetFocusedWidget(m_editBox);
		// No need to activate write mode manually, focus usually handles this for EditBox
		m_editBox.ActivateWriteMode(); // Generally not needed if focused
		//Print("AG0_TDLRadioComponent: Activating write mode.", LogLevel.NORMAL);
	}
	
	protected void OnKeyReplicated()
	{
		//Print(string.Format("'%1' AG0_TDLRadioComponent::OnKeyReplicated - Received Key: '%2'", GetOwner(), m_sCurrentCryptoKey), LogLevel.NORMAL);
		// Update the actual BaseRadioComponent everywhere
		// This ensures consistency even if BaseRadioComponent itself doesn't replicate the key.
		if (m_BaseRadioComp)
		{

			//Print(string.Format("'%1' AG0_TDLRadioComponent::OnKeyReplicated - Setting BaseRadioComp key to '%2'", GetOwner(), m_sCurrentCryptoKey), LogLevel.NORMAL);

			m_BaseRadioComp.SetEncryptionKey(m_sCurrentCryptoKey);
		}
		else
		{
			//Print(string.Format("'%1' AG0_TDLRadioComponent::OnKeyReplicated - m_BaseRadioComp is NULL!", GetOwner()), LogLevel.WARNING);
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
		//Print(string.Format("AG0_TDLRadioComponent: Crypto key entered locally: '%1' (Client)", enteredText), LogLevel.NORMAL);

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

		//Print("AG0_TDLRadioComponent: Crypto key input cancelled (Client).", LogLevel.NORMAL);
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
			//Print(string.Format("'%1' AG0_TDLRadioComponent::EOnInit - Syncing our default key to '%2'", owner, m_BaseRadioComp.GetEncryptionKey()), LogLevel.NORMAL);
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
	        //Print(string.Format("'%1' AG0_TDLRadioComponent::SetCryptoKeyDirectly - Setting key to '%2'", GetOwner(), newKey), LogLevel.NORMAL);
	        
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
}