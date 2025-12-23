//------------------------------------------------------------------------------------------------
//! TDL Modded VON Display
//! Adds network member indicator and half-duplex support
//------------------------------------------------------------------------------------------------
modded class SCR_VonDisplay : SCR_InfoDisplayExtended
{
    //------------------------------------------------------------------------------------------------
    //! Check if there's an active incoming transmission on a specific frequency
    bool HasActiveIncomingOnFrequency(int frequency)
    {
        foreach (TransmissionData transmission : m_aTransmissions)
        {
            if (!transmission.m_bIsActive || !transmission.m_RadioTransceiver)
                continue;
            
            if (transmission.m_RadioTransceiver.GetFrequency() == frequency)
                return true;
        }
        return false;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Check if transmission is from a TDL network member on encrypted channel
    protected bool IsNetworkMemberTransmission(int senderId, BaseTransceiver receiver)
    {
        if (!receiver)
            return false;
        
        BaseRadioComponent radio = receiver.GetRadio();
        if (!radio)
            return false;
        
        // Must be TDL radio
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            radio.GetOwner().FindComponent(AG0_TDLRadioComponent));
        if (!tdlRadio)
            return false;
        
        // Must be encrypted (not default key)
        if (tdlRadio.GetCurrentCryptoKey() == tdlRadio.GetDefaultCryptoKey())
            return false;
        
        // Sender must be in our connected players
        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (!pc)
            return false;
        
        return pc.IsConnectedTDLPlayer(senderId);
    }
    
	//Override the UI to show network status, and network callsign if set.
	override protected bool UpdateTransmission(TransmissionData data, BaseTransceiver radioTransceiver, int frequency, bool IsReceiving)
	{
	    bool result = super.UpdateTransmission(data, radioTransceiver, frequency, IsReceiving);
	    
	    // Only process if transmission wasn't filtered and is incoming
	    if (!result || !IsReceiving)
	        return result;
	    
	    // Network member indicator
	    bool isNetworkMember = IsNetworkMemberTransmission(data.m_iPlayerID, radioTransceiver);
	    if (isNetworkMember)
	    {
	        // Get the network ID from our receiving radio's device component
	        int networkId = 0;
	        if (radioTransceiver)
	        {
	            BaseRadioComponent radio = radioTransceiver.GetRadio();
	            if (radio)
	            {
	                AG0_TDLDeviceComponent tdlDevice = AG0_TDLDeviceComponent.Cast(
	                    radio.GetOwner().FindComponent(AG0_TDLDeviceComponent));
	                if (tdlDevice)
	                    networkId = tdlDevice.GetCurrentNetworkID();
	            }
	        }
	        
	        // Override name with callsign if available
	        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	        if (pc && networkId > 0)
	        {
	            string callsign = pc.GetCallsignForPlayerInNetwork(data.m_iPlayerID, networkId);
	            if (!callsign.IsEmpty())
	                data.m_Widgets.m_wName.SetText(callsign);
	        }
	        
	        // Show network indicator
	        data.m_Widgets.m_wChannelBackground.SetColor(Color.FromInt(GUIColors.BLUE.PackToInt()));
	        data.m_Widgets.m_wChannelText.SetText("NETWORK");
	        data.m_Widgets.m_wChannelFrame.SetVisible(true);
	    }
	    
	    return result;
	}
}