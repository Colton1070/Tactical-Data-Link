modded class SCR_VONEntryRadio : SCR_VONEntry
{
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
        
        // Only apply to TDL radios
        AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(radio.GetOwner().FindComponent(AG0_TDLRadioComponent));
        if (!tdlRadio)
            return;
        
        // Green = custom key filled, Red = default key (not encrypted)
        bool encrypted = tdlRadio.GetCurrentCryptoKey() != tdlRadio.GetDefaultCryptoKey();
        
        Color encryptionColor;
        if (encrypted)
            encryptionColor = GUIColors.GREEN;
        else
            encryptionColor = GUIColors.RED;
        
        entryComp.SetFrequencyColor(Color.FromInt(encryptionColor.PackToInt()));
    }
}