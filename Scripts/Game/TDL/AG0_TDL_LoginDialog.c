//------------------------------------------------------------------------------------------------
//! Network Name Dialog - First dialog in the two-dialog sequence
//! Single EditBox for network name entry with proper gamepad/console support
//------------------------------------------------------------------------------------------------
class AG0_TDL_NetworkNameDialog : SCR_ConfigurableDialogUi
{
    protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";
    
    protected SCR_EditBoxComponent m_InputField;
    protected EditBoxWidget m_EditBox;
    
    //------------------------------------------------------------------------------------------------
    //! Factory method to create the dialog
    static AG0_TDL_NetworkNameDialog CreateDialog(string title = "TDL NETWORK")
    {
        AG0_TDL_NetworkNameDialog dialog = new AG0_TDL_NetworkNameDialog();
        SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_networkname", dialog);
        
        if (!title.IsEmpty())
            dialog.SetTitle(title);
        
        return dialog;
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
    {
        super.OnMenuOpen(preset);
        
        // Try SCR_EditBoxComponent first (preferred for gamepad)
        m_InputField = SCR_EditBoxComponent.GetEditBoxComponent("NetworkNameInput", m_wRoot);
        
        if (m_InputField)
        {
            // Focus the component's root widget and activate write mode
            Widget inputWidget = m_InputField.GetRootWidget();
            if (inputWidget)
            {
                GetGame().GetWorkspace().SetFocusedWidget(inputWidget);
                
                // Get the actual EditBox for write mode activation
                m_EditBox = EditBoxWidget.Cast(inputWidget.FindAnyWidget("EditBox"));
                if (!m_EditBox)
                    m_EditBox = EditBoxWidget.Cast(inputWidget);
                
                if (m_EditBox)
                {
                    // Delay activation slightly to ensure focus is set
                    GetGame().GetCallqueue().CallLater(ActivateEditMode, 50, false);
                }
            }
            
            Print("TDL_DIALOG: NetworkNameDialog opened with SCR_EditBoxComponent", LogLevel.DEBUG);
        }
        else
        {
            // Fallback to direct EditBoxWidget
            m_EditBox = EditBoxWidget.Cast(m_wRoot.FindAnyWidget("NetworkNameInput"));
            if (!m_EditBox)
            {
                // Try nested structure
                Widget wrapper = m_wRoot.FindAnyWidget("NetworkNameInput");
                if (wrapper)
                    m_EditBox = EditBoxWidget.Cast(wrapper.FindAnyWidget("EditBox"));
            }
            
            if (m_EditBox)
            {
                GetGame().GetWorkspace().SetFocusedWidget(m_EditBox);
                GetGame().GetCallqueue().CallLater(ActivateEditMode, 50, false);
            }
            
            Print("TDL_DIALOG: NetworkNameDialog opened with fallback EditBoxWidget", LogLevel.DEBUG);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ActivateEditMode()
    {
        if (m_EditBox)
            m_EditBox.ActivateWriteMode();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get the entered network name
    string GetNetworkName()
    {
        if (m_InputField)
            return m_InputField.GetValue().Trim();
        
        if (m_EditBox)
            return m_EditBox.GetText().Trim();
        
        return "";
    }
}

//------------------------------------------------------------------------------------------------
//! Network Password Dialog - Second dialog in the two-dialog sequence
//! Single EditBox for password entry with proper gamepad/console support
//! Password is optional (empty string allowed)
//------------------------------------------------------------------------------------------------
class AG0_TDL_NetworkPasswordDialog : SCR_ConfigurableDialogUi
{
    protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";
    
    protected SCR_EditBoxComponent m_InputField;
    protected EditBoxWidget m_EditBox;
    
    //------------------------------------------------------------------------------------------------
    //! Factory method to create the dialog
    static AG0_TDL_NetworkPasswordDialog CreateDialog(string title = "TDL NETWORK")
    {
        AG0_TDL_NetworkPasswordDialog dialog = new AG0_TDL_NetworkPasswordDialog();
        SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_networkpassword", dialog);
        
        if (!title.IsEmpty())
            dialog.SetTitle(title);
        
        return dialog;
    }
    
    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
    {
        super.OnMenuOpen(preset);
        
        // Try SCR_EditBoxComponent first (preferred for gamepad)
        m_InputField = SCR_EditBoxComponent.GetEditBoxComponent("NetworkPasswordInput", m_wRoot);
        
        if (m_InputField)
        {
            // Focus the component's root widget and activate write mode
            Widget inputWidget = m_InputField.GetRootWidget();
            if (inputWidget)
            {
                GetGame().GetWorkspace().SetFocusedWidget(inputWidget);
                
                // Get the actual EditBox for write mode activation
                m_EditBox = EditBoxWidget.Cast(inputWidget.FindAnyWidget("EditBox"));
                if (!m_EditBox)
                    m_EditBox = EditBoxWidget.Cast(inputWidget);
                
                if (m_EditBox)
                {
                    // Delay activation slightly to ensure focus is set
                    GetGame().GetCallqueue().CallLater(ActivateEditMode, 50, false);
                }
            }
            
            Print("TDL_DIALOG: NetworkPasswordDialog opened with SCR_EditBoxComponent", LogLevel.DEBUG);
        }
        else
        {
            // Fallback to direct EditBoxWidget
            m_EditBox = EditBoxWidget.Cast(m_wRoot.FindAnyWidget("NetworkPasswordInput"));
            if (!m_EditBox)
            {
                // Try nested structure
                Widget wrapper = m_wRoot.FindAnyWidget("NetworkPasswordInput");
                if (wrapper)
                    m_EditBox = EditBoxWidget.Cast(wrapper.FindAnyWidget("EditBox"));
            }
            
            if (m_EditBox)
            {
                GetGame().GetWorkspace().SetFocusedWidget(m_EditBox);
                GetGame().GetCallqueue().CallLater(ActivateEditMode, 50, false);
            }
            
            Print("TDL_DIALOG: NetworkPasswordDialog opened with fallback EditBoxWidget", LogLevel.DEBUG);
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected void ActivateEditMode()
    {
        if (m_EditBox)
            m_EditBox.ActivateWriteMode();
    }
    
    //------------------------------------------------------------------------------------------------
    //! Get the entered password (may be empty - password is optional)
    string GetPassword()
    {
        if (m_InputField)
            return m_InputField.GetValue().Trim();
        
        if (m_EditBox)
            return m_EditBox.GetText().Trim();
        
        return "";
    }
}