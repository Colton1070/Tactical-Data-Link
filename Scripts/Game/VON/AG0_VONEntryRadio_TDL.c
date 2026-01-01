//------------------------------------------------------------------------------------------------
//! TDL Modded VON Entry Radio
//! - Shows encryption status via color (green = encrypted, red = default)
//! - Shows "FH" prefix when frequency hopping is active
//! - Blocks manual frequency changes when FH is enabled
//------------------------------------------------------------------------------------------------

modded class SCR_VONEntryRadio : SCR_VONEntry
{
    //------------------------------------------------------------------------------------------------
    //! Override to block frequency changes when FH is active and show FH prefix
    override void AdjustEntryModif(int modifier)
    {
        if (!m_RadioTransceiver)
            return;
        
        // Check if FH is enabled for this transceiver
        BaseRadioComponent radio = m_RadioTransceiver.GetRadio();
        if (radio)
        {
            AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
                radio.GetOwner().FindComponent(AG0_TDLRadioComponent));
            
            if (tdlRadio)
            {
                int tsvIdx = m_iTransceiverNumber - 1; // VON uses 1-based index
                
                if (tdlRadio.IsFrequencyHopEnabled(tsvIdx))
                {
                    // Block frequency changes in FH mode
                    if (modifier != 0)
                    {
                        SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.SOUND_RADIO_CHANGEFREQUENCY_ERROR);
                        return;
                    }
                    
                    // Update display with FH prefix and current hop frequency
                    int hopFreq = tdlRadio.GetCurrentFrequency(tsvIdx);
                    float fFrequency = Math.Round(hopFreq * 0.1) * 0.01;
                    m_sText = "FH " + fFrequency.ToString(3, 1) + " " + LABEL_FREQUENCY_UNITS;
                    
                    return;
                }
            }
        }
        
        // Not FH mode - use normal behavior
        super.AdjustEntryModif(modifier);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Override to block channel preset cycling when FH is active
    override void AdjustEntry(int modifier)
    {
        if (!m_RadioTransceiver)
            return;
        
        // Check if FH is enabled - if so, block preset changes
        BaseRadioComponent radio = m_RadioTransceiver.GetRadio();
        if (radio)
        {
            AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
                radio.GetOwner().FindComponent(AG0_TDLRadioComponent));
            
            if (tdlRadio)
            {
                int tsvIdx = m_iTransceiverNumber - 1;
                
                if (tdlRadio.IsFrequencyHopEnabled(tsvIdx))
                {
                    // Block preset changes in FH mode
                    if (modifier != 0)
                    {
                        SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.SOUND_RADIO_CHANGEFREQUENCY_ERROR);
                    }
                    return;
                }
            }
        }
        
        // Not FH mode - use normal behavior
        super.AdjustEntry(modifier);
    }
    
    //------------------------------------------------------------------------------------------------
    //! Override Update to show encryption color and refresh FH display
    override void Update()
    {
        super.Update();
        
        if (!m_RadioTransceiver)
            return;
        
        SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);
        if (!entryComp)
            return;
        
        BaseRadioComponent radio = m_RadioTransceiver.GetRadio();
        if (!radio)
            return;
        
        // Only apply TDL features to TDL radios
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(
            radio.GetOwner().FindComponent(AG0_TDLRadioComponent));
        
        if (!tdlRadio)
            return;
        
        int tsvIdx = m_iTransceiverNumber - 1;
        
        // If FH is active, update the frequency text with current hop
        if (tdlRadio.IsFrequencyHopEnabled(tsvIdx))
        {
            int hopFreq = tdlRadio.GetCurrentFrequency(tsvIdx);
            float fFrequency = Math.Round(hopFreq * 0.1) * 0.01;
            string fhText = "FH " + fFrequency.ToString(3, 1) + " " + LABEL_FREQUENCY_UNITS;
            entryComp.SetFrequencyText(fhText);
        }
        
        // Set color based on encryption status
        // Green = custom key filled (encrypted)
        // Red = default key (not encrypted)
        bool encrypted = tdlRadio.GetCurrentCryptoKey() != tdlRadio.GetDefaultCryptoKey();
        
        Color encryptionColor;
        if (encrypted)
            encryptionColor = GUIColors.GREEN;
        else
            encryptionColor = GUIColors.RED;
        
        // Only override color if not currently selected (preserve selection highlight)
        if (!m_bIsSelected)
        {
            entryComp.SetFrequencyColor(Color.FromInt(encryptionColor.PackToInt()));
        }
    }
}