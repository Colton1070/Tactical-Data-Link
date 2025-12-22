class AG0_TDL_LoginDialog : SCR_ConfigurableDialogUi
{
	protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";
	
    protected EditBoxWidget m_NetworkNameWidget;
    protected EditBoxWidget m_NetworkPasswordWidget;
    
	static AG0_TDL_LoginDialog CreateLoginDialog(string message, string title = "")
    {
        AG0_TDL_LoginDialog dialog = new AG0_TDL_LoginDialog();
        SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_tdllogin", dialog);
        
        dialog.SetMessage(message);
        if (!title.IsEmpty())
            dialog.SetTitle(title);
            
        return dialog;
    }
	
    override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
	    super.OnMenuOpen(preset);
	    
	    m_NetworkNameWidget = EditBoxWidget.Cast(m_wRoot.FindAnyWidget("NetworkNameInput"));
	    m_NetworkPasswordWidget = EditBoxWidget.Cast(m_wRoot.FindAnyWidget("NetworkPasswordInput"));
	    
	    Print(string.Format("Name widget: %1", m_NetworkNameWidget), LogLevel.DEBUG);
	    Print(string.Format("Pass widget: %1", m_NetworkPasswordWidget), LogLevel.DEBUG);
	    
	    if (m_NetworkNameWidget)
	        GetGame().GetWorkspace().SetFocusedWidget(m_NetworkNameWidget);
	}
    
    string GetNetworkName()
    {
        if (!m_NetworkNameWidget) return "";
        return m_NetworkNameWidget.GetText().Trim();
    }
    
    string GetNetworkPassword()
    {
        if (!m_NetworkPasswordWidget) return "";
        return m_NetworkPasswordWidget.GetText().Trim();
    }
}