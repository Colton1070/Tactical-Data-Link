//------------------------------------------------------------------------------------------------
//! TDL Modded VON Controller
//! Intercepts radio transmission activation to enforce half-duplex blocking
//------------------------------------------------------------------------------------------------
modded class SCR_VONController : ScriptComponent
{
	//------------------------------------------------------------------------------------------------
	//! Override to add half-duplex check before allowing transmission
	//! If a TDL radio has half-duplex enabled and we're receiving on the target frequency,
	//! block the transmission and fall back to direct speech
	override protected bool ActivateVON(notnull SCR_VONEntry entry, EVONTransmitType transmitType = EVONTransmitType.NONE)
	{
		// Only check radio entries, not direct speech
		SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
		if (radioEntry)
		{
			if (IsHalfDuplexBlocked(radioEntry))
			{
				// Blocked by half-duplex - fall back to direct speech like other blocking cases
				SetVONProximity(true);
				
				if (m_VONDisplay)
					m_VONDisplay.ShowSelectedVONDisabledHint();
				
				return false;
			}
		}
		
		return super.ActivateVON(entry, transmitType);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Check if transmission should be blocked due to half-duplex receiving
	//! \param radioEntry The radio entry attempting to transmit
	//! \return true if blocked (receiving on same frequency with half-duplex enabled)
	protected bool IsHalfDuplexBlocked(notnull SCR_VONEntryRadio radioEntry)
	{
		// Need display to check incoming transmissions
		if (!m_VONDisplay)
			return false;
		
		BaseTransceiver transceiver = radioEntry.GetTransceiver();
		if (!transceiver)
			return false;
		
		BaseRadioComponent radio = transceiver.GetRadio();
		if (!radio)
			return false;
		
		// Find TDL component on the radio's owner entity
		IEntity radioOwner = radio.GetOwner();
		if (!radioOwner)
			return false;
		
		AG0_TDLRadioComponent tdlRadio = AG0_TDLRadioComponent.Cast(radioOwner.FindComponent(AG0_TDLRadioComponent));
		if (!tdlRadio)
			return false;
		
		// Check if half-duplex is enabled on this radio
		if (!tdlRadio.ShouldBlockHalfDuplex())
		    return false;
		
		// Check if we're receiving on this transceiver's frequency
		int frequency = transceiver.GetFrequency();
		return m_VONDisplay.HasActiveIncomingOnFrequency(frequency);
	}
}