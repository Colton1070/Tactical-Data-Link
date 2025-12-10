//------------------------------------------------------------------------------------------------
//! TDL Modded VON Display
//! Adds frequency-specific incoming transmission detection for half-duplex radio support
//------------------------------------------------------------------------------------------------
modded class SCR_VonDisplay : SCR_InfoDisplayExtended
{
	//------------------------------------------------------------------------------------------------
	//! Check if there's an active incoming transmission on a specific frequency
	//! Used by half-duplex radio logic to block transmit while receiving
	//! \param frequency The frequency to check (raw int from transceiver)
	//! \return true if actively receiving on the specified frequency
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
	//! Get count of truly active incoming transmissions
	//! Unlike base GetActiveTransmissionsCount() which just returns array size,
	//! this checks the actual m_bIsActive state
	//! \return count of transmissions with m_bIsActive == true
	int GetTrueActiveTransmissionsCount()
	{
		int count = 0;
		foreach (TransmissionData transmission : m_aTransmissions)
		{
			if (transmission.m_bIsActive)
				count++;
		}
		return count;
	}
}