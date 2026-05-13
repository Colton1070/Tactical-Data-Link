//------------------------------------------------------------------------------------------------
//! Per-button click relay for plugin toolbar buttons.
//!
//! Background: SCR_ButtonBaseComponent.m_OnClicked is declared as untyped
//! `ScriptInvoker` (not `ScriptInvoker<T>`), which means it fires zero-arg.
//! A handler with a parameter receives null. To disambiguate which plugin's
//! button was clicked when N plugin buttons share one host, each button gets
//! its own relay instance that captures the plugin + menu-root reference and
//! exposes a zero-arg OnClick() method bound to the invoker.
//!
//! Lifetime: the menu UI holds a `ref array<ref AG0_PluginButtonClickRelay>`
//! so relays survive as long as the buttons they serve. Clearing that array
//! drops the relay along with the button it was bound to.
//------------------------------------------------------------------------------------------------
class AG0_PluginButtonClickRelay
{
    protected AG0_ATAKPluginBase m_Plugin;
    protected Widget m_MenuRoot;

    //------------------------------------------------------------------------------------------------
    void AG0_PluginButtonClickRelay(AG0_ATAKPluginBase plugin, Widget menuRoot)
    {
        m_Plugin = plugin;
        m_MenuRoot = menuRoot;
    }

    //------------------------------------------------------------------------------------------------
    //! Bound zero-arg to SCR_ModularButtonComponent.m_OnClicked. Dispatches
    //! to the captured plugin with the captured menu root.
    void OnClick()
    {
        if (m_Plugin && m_MenuRoot)
            m_Plugin.OnToolActivated(m_MenuRoot);
    }
}
