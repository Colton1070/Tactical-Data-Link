class AG0_TDL_KeyDialog : SCR_ConfigurableDialogUi
{
    protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";
    
    protected SCR_EditBoxComponent m_InputField;
    
    // Static factory - this is how you create instances
    static AG0_TDL_KeyDialog CreateKeyDialog(string message, string title = "")
    {
        AG0_TDL_KeyDialog dialog = new AG0_TDL_KeyDialog();
        SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_cypherkey", dialog);
        
        dialog.SetMessage(message);
        if (!title.IsEmpty())
            dialog.SetTitle(title);
            
        return dialog;
    }
    
    override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
    {
        super.OnMenuOpen(preset);
        
        m_InputField = SCR_EditBoxComponent.GetEditBoxComponent("InputField", m_wRoot);
        if (m_InputField)
        {
            GetGame().GetWorkspace().SetFocusedWidget(m_InputField.GetRootWidget());
        }
    }
    
    string GetInputValue()
    {
        if (!m_InputField) return "";
        return m_InputField.GetValue().Trim();
    }
}