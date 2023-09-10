#pragma once

#include "MouseGlobalHook.h"
#include "UserConfig.h"

class CMainDlg : public CDialogImpl<CMainDlg>
{
public:
	enum { IDD = IDD_MAINDLG };

	enum
	{
		UWM_MOUSEHOOKCLICKED = WM_APP,
		UWM_NOTIFYICON,
		UWM_BRING_TO_FRONT,
		UWM_UPDATE_CHECKED,
		UWM_EXIT,
	};

	enum
	{
		RCMENU_SHOW = 101,
		RCMENU_EXIT,
	};

	enum
	{
		TIMER_UPDATE_CHECK = 1,
	};

	BEGIN_MSG_MAP_EX(CMainDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_WINDOWPOSCHANGING(OnWindowPosChanging)
		MSG_WM_NOTIFY(OnNotify)
		MSG_WM_HOTKEY(OnHotKey)
		MSG_WM_TIMER(OnTimer)
		MESSAGE_HANDLER_EX(0x02E0, OnDpiChanged) // WM_DPICHANGED
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER_EX(IDC_SHOW_INI, OnShowIni)
		COMMAND_ID_HANDLER_EX(IDC_EXIT, OnExitButton)
		COMMAND_HANDLER_EX(IDC_CHECK_CTRL, BN_CLICKED, OnConfigChanged)
		COMMAND_HANDLER_EX(IDC_CHECK_ALT, BN_CLICKED, OnConfigChanged)
		COMMAND_HANDLER_EX(IDC_CHECK_SHIFT, BN_CLICKED, OnConfigChanged)
		COMMAND_HANDLER_EX(IDC_COMBO_KEYS, CBN_SELCHANGE, OnConfigChanged)
		MESSAGE_HANDLER_EX(UWM_MOUSEHOOKCLICKED, OnMouseHookClicked)
		MESSAGE_HANDLER_EX(m_uTaskbarCreatedMsg, OnTaskbarCreated)
		MESSAGE_HANDLER_EX(m_uTextifyMsg, OnCustomTextifyMsg)
		MESSAGE_HANDLER_EX(UWM_NOTIFYICON, OnNotifyIcon)
		MESSAGE_HANDLER_EX(UWM_BRING_TO_FRONT, OnBringToFront)
		MESSAGE_HANDLER_EX(UWM_UPDATE_CHECKED, OnUpdateChecked)
		MESSAGE_HANDLER_EX(UWM_EXIT, OnExit)
	END_MSG_MAP()

	BOOL OnInitDialog(CWindow wndFocus, LPARAM lInitParam);
	void OnDestroy();
	void OnWindowPosChanging(LPWINDOWPOS lpWndPos);
	LRESULT OnNotify(int idCtrl, LPNMHDR pnmh);
	void OnHotKey(int nHotKeyID, UINT uModifiers, UINT uVirtKey);
	void OnTimer(UINT_PTR nIDEvent);
	LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam);
	void OnOK(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnShowIni(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnExitButton(UINT uNotifyCode, int nID, CWindow wndCtl);
	void OnConfigChanged(UINT uNotifyCode, int nID, CWindow wndCtl);
	LRESULT OnMouseHookClicked(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnTaskbarCreated(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnCustomTextifyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnNotifyIcon(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnBringToFront(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnUpdateChecked(UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnExit(UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
	void ReloadMainIcon();
	void ApplyUiLanguage();
	void ApplyMouseAndKeyboardHotKeys();
	void UninitMouseAndKeyboardHotKeys();
	bool RegisterConfiguredKeybdHotKey(const HotKey& keybdHotKey);
	void ConfigToGui();
	void InitNotifyIconData();
	HICON LoadTrayIcon();
	void NotifyIconRightClickMenu();
	void Exit();

	CIcon m_trayIcon;
	std::optional<UserConfig> m_config;
	std::optional<MouseGlobalHook> m_mouseGlobalHook;
	UINT m_uTaskbarCreatedMsg = RegisterWindowMessage(L"TaskbarCreated");
	UINT m_uTextifyMsg = RegisterWindowMessage(L"Textify");
	NOTIFYICONDATA m_notifyIconData = {};
	bool m_hideDialog = false;
	bool m_registeredHotKey = false;
	bool m_checkingForUpdates = false;
	bool m_closeWhenUpdateCheckDone = false;
};
