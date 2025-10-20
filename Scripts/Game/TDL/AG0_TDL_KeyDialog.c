class AG0_TDL_KeyDialog : SCR_ConfigurableDialogUi
{
	ResourceName m_dialogsConfig = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";
	
	//------------------------------------------------------------------------------------------------
	void AG0_TDL_KeyDialog(string dialogMessage, string dialogPreset, string dialogTitle = "")
    {
        // CreateFromPreset will actually initialize 'this' object
        SCR_ConfigurableDialogUi.CreateFromPreset(m_dialogsConfig, dialogPreset, this);
        
        SetMessage(dialogMessage);
        
        if (!dialogTitle.IsEmpty())
            SetTitle(dialogTitle);
    }
    
    void ClearButtons()
    {
        m_aButtonComponents.Clear();
    }
}