//------------------------------------------------------------------------------------------------
//! TDL Modded VON Display
//! - Adds network member indicator (blue background, "NETWORK" label)
//! - Shows callsign instead of player name for network members
//! - Shows FH indicator for frequency hopping transmissions
//! - Half-duplex support helper methods
//------------------------------------------------------------------------------------------------

modded class SCR_VonDisplay : SCR_InfoDisplayExtended
{
    //------------------------------------------------------------------------------------------------
    //! Override DisplayUpdate to continuously update FH frequency during transmission
    override void DisplayUpdate(IEntity owner, float timeSlice)
    {
        super.DisplayUpdate(owner, timeSlice);
        
        // Update outgoing transmission FH display if active
        if (m_OutTransmission && m_OutTransmission.m_bIsActive && m_OutTransmission.m_RadioTransceiver)
        {
            UpdateFHFrequencyDisplay(m_OutTransmission, m_OutTransmission.m_RadioTransceiver);
        }
        
        // Update incoming transmission FH displays if active
        foreach (TransmissionData transmission : m_aTransmissions)
        {
            if (transmission && transmission.m_bIsActive && transmission.m_RadioTransceiver)
            {
                UpdateFHFrequencyDisplay(transmission, transmission.m_RadioTransceiver);
            }
        }
    }
    
    //------------------------------------------------------------------------------------------------
    //! Check if there's an active incoming transmission on a specific frequency
    //! Used for half-duplex blocking logic
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
    //! Check if there's any active incoming transmission on a TDL radio with FH enabled
    //! For FH radios, we check by crypto key match rather than exact frequency
    bool HasActiveIncomingOnFHChannel(AG0_TDLRadioComponent localRadio, int transceiverIdx)
    {
        if (!localRadio || !localRadio.IsFrequencyHopEnabled(transceiverIdx))
            return false;
        
        string localKey = localRadio.GetCurrentCryptoKey();
        
        foreach (TransmissionData transmission : m_aTransmissions)
        {
            if (!transmission.m_bIsActive || !transmission.m_RadioTransceiver)
                continue;
            
            // Check if sender is on same FH network (same crypto key)
            BaseRadioComponent senderRadio = transmission.m_RadioTransceiver.GetRadio();
            if (!senderRadio)
                continue;
            
            AG0_TDLRadioComponent senderTDL = AG0_TDLRadioComponent.Cast(
                senderRadio.GetOwner().FindComponent(AG0_TDLRadioComponent));
            
            if (senderTDL && senderTDL.GetCurrentCryptoKey() == localKey)
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
    
    //------------------------------------------------------------------------------------------------
    //! Helper to update frequency widget with FH info
    //! Returns true if FH is active and display was updated
    protected bool UpdateFHFrequencyDisplay(TransmissionData data, BaseTransceiver radioTransceiver)
    {
        if (!radioTransceiver || !data || !data.m_Widgets || !data.m_Widgets.m_wFrequency)
            return false;
        
        BaseRadioComponent radio = radioTransceiver.GetRadio();
        if (!radio)
            return false;
        
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            radio.GetOwner().FindComponent(AG0_TDLRadioComponent));
        
        if (!tdlRadio)
            return false;
        
        // Find which transceiver index this is
        int tsvIdx = -1;
        int tsvCount = radio.TransceiversCount();
        for (int i = 0; i < tsvCount; i++)
        {
            if (radio.GetTransceiver(i) == radioTransceiver)
            {
                tsvIdx = i;
                break;
            }
        }
        
        if (tsvIdx < 0)
            return false;
        
        // Check if FH is enabled
        if (!tdlRadio.IsFrequencyHopEnabled(tsvIdx))
            return false;
        
        // FH is active - get current hop frequency directly from TDL component
        int hopFreq = tdlRadio.GetCurrentFrequency(tsvIdx);
        float adjustedFreq = Math.Round(hopFreq * 0.1) / 100;
        
        data.m_Widgets.m_wFrequency.SetText("FH " + adjustedFreq.ToString() + " " + LABEL_FREQUENCY_UNITS);
        data.m_Widgets.m_wFrequency.SetVisible(true);
        
        return true;
    }
    
    //------------------------------------------------------------------------------------------------
    //! Override to show network status, callsign, and FH indicator
    override protected bool UpdateTransmission(TransmissionData data, BaseTransceiver radioTransceiver, int frequency, bool IsReceiving)
    {
        bool result = super.UpdateTransmission(data, radioTransceiver, frequency, IsReceiving);
        
        if (!result)
            return result;
        
        // Update FH display for both incoming and outgoing
        if (radioTransceiver)
        {
            UpdateFHFrequencyDisplay(data, radioTransceiver);
        }
        
        // Network member indicator - only for incoming
        if (!IsReceiving)
            return result;
        
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
            
            // Show network indicator with blue background
            data.m_Widgets.m_wChannelBackground.SetColor(Color.FromInt(GUIColors.BLUE.PackToInt()));
            data.m_Widgets.m_wChannelText.SetText("NETWORK");
            data.m_Widgets.m_wChannelFrame.SetVisible(true);
        }
        
        return result;
    }
}