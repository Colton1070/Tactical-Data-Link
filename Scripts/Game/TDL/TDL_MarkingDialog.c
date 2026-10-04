//! Single console-safe text field; "/" splits the entry into two rows on the patch.
//! Reuses the network-name content layout so no new layout resource is needed.
class TDL_MarkingDialog : SCR_ConfigurableDialogUi
{
    protected const ResourceName DIALOG_CONFIG = "{B5657EE8DF7F856D}Configs/UI/AG0_TDL_Dialogs.conf";

    protected SCR_EditBoxComponent m_InputField;
    protected EditBoxWidget m_EditBox;
    protected RplId m_PatchId;
    protected string m_sInitialText;

    //------------------------------------------------------------------------------------------------
    static TDL_MarkingDialog CreateDialog(RplId patchId, string currentText)
    {
        TDL_MarkingDialog dialog = new TDL_MarkingDialog();
        dialog.m_PatchId = patchId;
        dialog.m_sInitialText = currentText;
        SCR_ConfigurableDialogUi.CreateFromPreset(DIALOG_CONFIG, "dialog_marking", dialog);
        dialog.m_OnConfirm.Insert(dialog.OnConfirmed);
        return dialog;
    }

    //------------------------------------------------------------------------------------------------
    override void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
    {
        super.OnMenuOpen(preset);

        m_InputField = SCR_EditBoxComponent.GetEditBoxComponent("NetworkNameInput", m_wRoot);
        if (!m_InputField)
            return;

        m_InputField.SetValue(m_sInitialText);

        Widget inputWidget = m_InputField.GetRootWidget();
        if (!inputWidget)
            return;

        GetGame().GetWorkspace().SetFocusedWidget(inputWidget);
        m_EditBox = EditBoxWidget.Cast(inputWidget.FindAnyWidget("EditBox"));
        if (!m_EditBox)
            m_EditBox = EditBoxWidget.Cast(inputWidget);
        if (m_EditBox)
            GetGame().GetCallqueue().CallLater(ActivateEditMode, 50, false);
    }

    //------------------------------------------------------------------------------------------------
    protected void ActivateEditMode()
    {
        if (m_EditBox)
            m_EditBox.ActivateWriteMode();
    }

    //------------------------------------------------------------------------------------------------
    protected void OnConfirmed(SCR_ConfigurableDialogUi dialog)
    {
        string text = "";
        if (m_InputField)
            text = m_InputField.GetValue().Trim();
        else if (m_EditBox)
            text = m_EditBox.GetText().Trim();

        SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
        if (pc)
            pc.RequestSetMarking(m_PatchId, text);
    }
}
