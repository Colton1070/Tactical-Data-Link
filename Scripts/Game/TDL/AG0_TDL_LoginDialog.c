class AG0_TDL_LoginDialog : SCR_ConfigurableDialogUi
{
    protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";
    
    protected const string USERNAME_WIDGET = "NetworkNameInput";
    protected const string PASSWORD_WIDGET = "NetworkPasswordInput";
    
    protected SCR_EditBoxComponent m_NetworkName;
    protected SCR_EditBoxComponent m_NetworkPassword;
    
    //------------------------------------------------------------------------------------------------
    static AG0_TDL_LoginDialog CreateLoginDialog(string message, string title = "")
    {
        AG0_TDL_LoginDialog dialog = new AG0_TDL_LoginDialog();
        SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_tdllogin", dialog);
        
        dialog.SetMessage(message);
        if (!title.IsEmpty())
            dialog.SetTitle(title);
            
        return dialog;
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
    {
        super.OnMenuOpen(preset);
        
        m_NetworkName = SCR_EditBoxComponent.GetEditBoxComponent(USERNAME_WIDGET, m_wRoot);
        if (m_NetworkName)
            GetGame().GetWorkspace().SetFocusedWidget(m_NetworkName.GetRootWidget());
        
        m_NetworkPassword = SCR_EditBoxComponent.GetEditBoxComponent(PASSWORD_WIDGET, m_wRoot);
    }
    
    //------------------------------------------------------------------------------------------------
    string GetNetworkName()
    {
        if (!m_NetworkName) return "";
        return m_NetworkName.GetValue().Trim();
    }
    
    //------------------------------------------------------------------------------------------------
    string GetNetworkPassword()
    {
        if (!m_NetworkPassword) return "";
        return m_NetworkPassword.GetValue().Trim();
    }
}