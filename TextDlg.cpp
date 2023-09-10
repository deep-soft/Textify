#include "stdafx.h"
#include "resource.h"

#include "TextDlg.h"
#include "WebAppLaunch.h"
#include "Functions.h"

namespace
{
	void AppendStringToTextifyResults(CString& string, CString append, std::vector<int>& indexes);
	void GetAccessibleInfoFromPointMSAA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes);
	bool GetAccessibleInfoFromPointUIA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes);
	void GetAccessibleInfoFromPoint(TextRetrievalMethod method, POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes);
	CSize GetEditControlTextSize(CEdit window, LPCTSTR lpszString, int nMaxWidth = INT_MAX);
	CSize TextSizeToEditClientSize(HWND hWnd, CEdit editWnd, CSize textSize);
	CSize EditClientSizeToTextSize(HWND hWnd, CEdit editWnd, CSize editClientSize);
}

BOOL CTextDlg::OnInitDialog(CWindow wndFocus, LPARAM lInitParam)
{
	CPoint& ptEvent = *reinterpret_cast<CPoint*>(lInitParam);

	CWindow wndAcc;
	CString strText;
	CRect rcAccObject;
	GetAccessibleInfoFromPoint(m_config.m_textRetrievalMethod, ptEvent, wndAcc, strText, rcAccObject, m_editIndexes);

	// Check whether the target window is another TextDlg.
	if(wndAcc)
	{
		CWindow wndAccRoot{ ::GetAncestor(wndAcc, GA_ROOT) };
		if(wndAccRoot)
		{
			WCHAR szBuffer[32];
			if(::GetClassName(wndAccRoot, szBuffer, ARRAYSIZE(szBuffer)) &&
				wcscmp(szBuffer, L"TextifyEditDlg") == 0)
			{
				wndAccRoot.SendMessage(WM_CLOSE);
				EndDialog(0);
				return FALSE;
			}
		}
	}

	// Allows to steal focus.
	INPUT input{};
	SendInput(1, &input, sizeof(INPUT));

	if(!::SetForegroundWindow(m_hWnd))
	{
		//EndDialog(0);
		//return FALSE;
	}

	CEdit editWnd = GetDlgItem(IDC_EDIT);

	// Init font.
	if(!m_config.m_fontName.IsEmpty() || m_config.m_fontSize)
	{
		CFontHandle font(editWnd.GetFont());
		CLogFont fontAttributes(font);

		if(!m_config.m_fontName.IsEmpty())
		{
			wcscpy_s(fontAttributes.lfFaceName, m_config.m_fontName);
		}

		if(m_config.m_fontSize)
		{
			fontAttributes.lfHeight = -m_config.m_fontSize;
		}

		m_editFont = fontAttributes.CreateFontIndirect();
		editWnd.SetFont(m_editFont);
	}

	if(m_config.m_textBoxNonResiable)
	{
		ModifyStyle(WS_THICKFRAME, WS_BORDER);
	}

	InitWebAppButtons();

	CString strDefaultText;
	strDefaultText.LoadString(IDS_DEFAULT_TEXT);

	if(m_config.m_unicodeSpacesToAscii)
	{
		UnicodeSpacesToAscii(strText);
	}

	if(strText.IsEmpty())
	{
		strText = strDefaultText;
	}

	editWnd.SetLimitText(0);
	editWnd.SetWindowText(strText);

	AdjustWindowLocationAndSize(ptEvent, rcAccObject, strText, strDefaultText);

	m_lastSelStart = 0;
	m_lastSelEnd = strText.GetLength();

	if(m_config.m_autoCopySelection)
	{
		SetClipboardText(strText);
	}

	m_wndEdit.SubclassWindow(editWnd);

	return TRUE;
}

HBRUSH CTextDlg::OnCtlColorStatic(CDCHandle dc, CStatic wndStatic)
{
	if(wndStatic.GetDlgCtrlID() == IDC_EDIT)
	{
		return GetSysColorBrush(COLOR_WINDOW);
	}

	SetMsgHandled(FALSE);
	return NULL;
}

void CTextDlg::OnActivate(UINT nState, BOOL bMinimized, CWindow wndOther)
{
	if(nState == WA_INACTIVE && !m_pinned && !m_showingModalBrowserHost)
		EndDialog(0);
}

void CTextDlg::OnCancel(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	EndDialog(nID);
}

void CTextDlg::OnTextChange(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	MakePinned();
}

void CTextDlg::OnExitSizeMove()
{
	MakePinned();
}

void CTextDlg::OnCommand(UINT uNotifyCode, int nID, CWindow wndCtl)
{
	int buttonIndex = nID - IDC_WEB_BUTTON_1;
	if(buttonIndex >= 0 && buttonIndex < static_cast<int>(m_config.m_webButtonInfos.size()))
	{
		CString selectedText;
		m_wndEdit.GetWindowText(selectedText);

		int start, end;
		m_wndEdit.GetSel(start, end);
		if(start < end)
		{
			selectedText = selectedText.Mid(start, end - start);
		}

		m_showingModalBrowserHost = true;
		ShowWindow(SW_HIDE);

		const auto& buttonInfo = m_config.m_webButtonInfos[buttonIndex];
		bool succeeded = CommandLaunch(buttonInfo.command, selectedText,
			buttonInfo.width, buttonInfo.height);

		if(!succeeded)
		{
			CString title;
			title.LoadString(IDS_ERROR);

			CString text;
			text.LoadString(IDS_ERROR_EXECUTE);
			text += L"\n";
			text += buttonInfo.command;

			MessageBox(text, title, MB_ICONERROR);
		}

		EndDialog(0);
	}
}

void CTextDlg::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	lpMMI->ptMinTrackSize.x = m_minSize.cx;
	lpMMI->ptMinTrackSize.y = m_minSize.cy;
}

void CTextDlg::OnSize(UINT nType, CSize size)
{
	if(nType != SIZE_RESTORED && nType != SIZE_MAXIMIZED)
		return;

	CRect clientRect;
	GetClientRect(&clientRect);

	int numberOfWebAppButtons = static_cast<int>(m_config.m_webButtonInfos.size());

	CButton webAppButtonLast;
	if(numberOfWebAppButtons > 0)
	{
		CButton button = GetDlgItem(IDC_WEB_BUTTON_1 - 1 + numberOfWebAppButtons);
		if(button && button.IsWindowVisible())
		{
			webAppButtonLast = button;
		}
		else
		{
			numberOfWebAppButtons = 0;
		}
	}

	HDWP hDwp = ::BeginDeferWindowPos(numberOfWebAppButtons + 1);
	if(!hDwp)
		return;

	int firstButtonTop = clientRect.Height();
	if(webAppButtonLast)
	{
		CRect webAppButtonLastRect;
		webAppButtonLast.GetWindowRect(&webAppButtonLastRect);
		::MapWindowPoints(nullptr, m_hWnd, &webAppButtonLastRect.TopLeft(), 2);

		int newTop = clientRect.Height() - webAppButtonLastRect.Height();
		int heightDelta = newTop - webAppButtonLastRect.top;

		webAppButtonLast.DeferWindowPos(hDwp, nullptr,
			webAppButtonLastRect.left, webAppButtonLastRect.top + heightDelta,
			webAppButtonLastRect.Width(), webAppButtonLastRect.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

		for(int i = numberOfWebAppButtons - 1; i >= 1; i--)
		{
			CButton button = GetDlgItem(IDC_WEB_BUTTON_1 - 1 + i);

			CRect buttonRect;
			button.GetWindowRect(&buttonRect);
			::MapWindowPoints(nullptr, m_hWnd, &buttonRect.TopLeft(), 2);

			if(i == 1)
			{
				firstButtonTop = buttonRect.top + heightDelta;
			}

			button.DeferWindowPos(hDwp, nullptr,
				buttonRect.left, buttonRect.top + heightDelta,
				buttonRect.Width(), buttonRect.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
		}
	}

	CEdit editWnd = GetDlgItem(IDC_EDIT);
	editWnd.DeferWindowPos(hDwp, nullptr, 0, 0, clientRect.Width(), firstButtonTop,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

	::EndDeferWindowPos(hDwp);
}

LRESULT CTextDlg::OnNcHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = ::DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	if(result == HTCLIENT)
		result = HTCAPTION;

	return result;
}

void CTextDlg::OnKeyDown(UINT vk, UINT nRepCnt, UINT nFlags)
{
	if(vk == VK_TAB)
	{
		int start, end;
		m_wndEdit.GetSel(start, end);
		int length = m_wndEdit.SendMessage(WM_GETTEXTLENGTH);

		int newStart, newEnd;

		const int newlineSize = sizeof("\r\n") - 1;

		if(start == 0 && end == length) // all text is selected
		{
			newStart = 0;
			newEnd = m_editIndexes.empty() ? length : (m_editIndexes.front() - newlineSize);
		}
		else
		{
			newStart = 0;
			newEnd = length;

			for(size_t i = 0; i < m_editIndexes.size(); i++)
			{
				int from = (i == 0) ? 0 : m_editIndexes[i - 1];
				int to = m_editIndexes[i] - newlineSize;

				if(from == start && to == end)
				{
					newStart = m_editIndexes[i];
					newEnd = (i + 1 < m_editIndexes.size()) ? (m_editIndexes[i + 1] - newlineSize) : length;

					break;
				}
			}
		}

		m_wndEdit.SetSel(newStart, newEnd, TRUE);
	}
	else
	{
		SetMsgHandled(FALSE);
	}
}

void CTextDlg::OnKeyUp(UINT vk, UINT nRepCnt, UINT nFlags)
{
	OnSelectionMaybeChanged();
	SetMsgHandled(FALSE);
}

void CTextDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	OnSelectionMaybeChanged();
	SetMsgHandled(FALSE);
}

void CTextDlg::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if(nChar == 1) // Ctrl+A
	{
		m_wndEdit.SetSelAll(TRUE);
	}
	else
	{
		SetMsgHandled(FALSE);
	}
}

void CTextDlg::InitWebAppButtons()
{
	// This id shouldn't be in use yet, we're going to use it for a newly created button.
	ATLASSERT(!GetDlgItem(IDC_WEB_BUTTON_1));

	int numberOfButtons = static_cast<int>(m_config.m_webButtonInfos.size());
	if(numberOfButtons == 0)
	{
		return;
	}

	m_webButtonIcons.resize(numberOfButtons);

	int buttonSize = ScaleForWindow(m_hWnd, m_config.m_webButtonsIconSize);

	CRect buttonRect{
		0,
		0,
		GetSystemMetricsForWindow(m_hWnd, SM_CXEDGE) * 4 + buttonSize,
		GetSystemMetricsForWindow(m_hWnd, SM_CYEDGE) * 4 + buttonSize
	};

	for(int i = 0; i < numberOfButtons; i++)
	{
		CString buttonText;
		if(m_config.m_webButtonInfos[i].acceleratorKey)
			buttonText.Format(L"&%c", m_config.m_webButtonInfos[i].acceleratorKey);
		else if(i + 1 < 10)
			buttonText.Format(L"&%d", i + 1);
		else if(i + 1 == 10)
			buttonText.Format(L"1&0");
		else
			buttonText.Format(L"%d", i + 1);

		CButton button;
		button.Create(m_hWnd, buttonRect, buttonText,
			WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, IDC_WEB_BUTTON_1 + i);
		button.SetFont(GetFont());

		if(!m_config.m_webButtonInfos[i].name.IsEmpty())
		{
			if(!m_webButtonTooltip)
			{
				m_webButtonTooltip.Create(m_hWnd, NULL, NULL, WS_POPUP | TTS_NOPREFIX, WS_EX_TOPMOST);
			}

			m_webButtonTooltip.AddTool(
				CToolInfo(TTF_SUBCLASS, button, 0, NULL, (PWSTR)m_config.m_webButtonInfos[i].name.GetString()));
		}

		if(!m_config.m_webButtonInfos[i].iconPath.m_strPath.IsEmpty())
		{
			HICON icon;
			if(::SHDefExtractIcon(
				m_config.m_webButtonInfos[i].iconPath.m_strPath,
				m_config.m_webButtonInfos[i].iconIndex,
				0, &icon, nullptr, buttonSize) == S_OK)
			{
				button.ModifyStyle(0, BS_ICON);
				button.SetIcon(icon);
				m_webButtonIcons[i] = icon;
			}
		}
	}
}

void CTextDlg::AdjustWindowLocationAndSize(CPoint ptEvent, CRect rcAccObject, CString strText, CString strDefaultText)
{
	CEdit editWnd = GetDlgItem(IDC_EDIT);

	// A dirty, partial workaround.
	// http://stackoverflow.com/questions/35673347
	strText.Replace(L"!", L"! ");
	strText.Replace(L"|", L"| ");
	strText.Replace(L"?", L"? ");
	strText.Replace(L"-", L"- ");
	strText.Replace(L"}", L"} ");
	strText.Replace(L"{", L" {");
	strText.Replace(L"[", L" [");
	strText.Replace(L"(", L" (");
	strText.Replace(L"+", L" +");
	strText.Replace(L"%", L" %");
	strText.Replace(L"$", L" $");
	strText.Replace(L"\\", L" \\");

	CSize defTextSize = GetEditControlTextSize(editWnd, strDefaultText);
	CSize defTextSizeClient = TextSizeToEditClientSize(m_hWnd, editWnd, defTextSize);

	m_minSize.cy = defTextSizeClient.cy;

	int nMaxClientWidth = defTextSizeClient.cx > rcAccObject.Width() ? defTextSizeClient.cx : rcAccObject.Width();

	CSize textSize = GetEditControlTextSize(editWnd, strText, nMaxClientWidth);
	CSize textSizeClient = TextSizeToEditClientSize(m_hWnd, editWnd, textSize);

	if(textSizeClient.cx < rcAccObject.Width())
	{
		// Perhaps it will look better if we won't shrink the control,
		// as it will fit perfectly above the control.
		// Let's see if the shrinking is small.

		int nMinClientWidth = ScaleForWindow(m_hWnd, 200);

		if(rcAccObject.Width() <= nMinClientWidth || textSizeClient.cx * 1.5 >= rcAccObject.Width())
		{
			int delta = rcAccObject.Width() - textSizeClient.cx;
			textSizeClient.cx = rcAccObject.Width();
			textSize.cx += delta;

			// Recalculate the height, which might be smaller now.
			//CSize newTextSize = GetEditControlTextSize(editWnd, strText, textSize.cx);
			//textSize.cy = newTextSize.cy;
		}
	}

	CRect rcClient{ rcAccObject.TopLeft(), textSizeClient };
	if(rcAccObject.IsRectEmpty() || !rcClient.PtInRect(ptEvent))
	{
		CPoint ptWindowLocation{ ptEvent };
		ptWindowLocation.Offset(-rcClient.Width() / 2, -rcClient.Height() / 2);
		rcClient.MoveToXY(ptWindowLocation);
	}

	int numberOfWebAppButtons = static_cast<int>(m_config.m_webButtonInfos.size());
	int numberOfWebAppButtonsX = numberOfWebAppButtons;
	int numberOfWebAppButtonsY = 1;
	CSize webAppButtonSize;
	if(numberOfWebAppButtons > 0)
	{
		int numberOfWebAppButtonsPerRow = m_config.m_webButtonsPerRow;
		if(numberOfWebAppButtonsPerRow > 0)
		{
			numberOfWebAppButtonsX = numberOfWebAppButtons > numberOfWebAppButtonsPerRow ? numberOfWebAppButtonsPerRow : numberOfWebAppButtons;
			numberOfWebAppButtonsY = (numberOfWebAppButtons + numberOfWebAppButtonsPerRow - 1) / numberOfWebAppButtonsPerRow;
		}

		CButton webAppButton = GetDlgItem(IDC_WEB_BUTTON_1);
		CRect webAppButtonRect;
		webAppButton.GetWindowRect(webAppButtonRect);
		webAppButtonSize = webAppButtonRect.Size();

		if(rcClient.Width() < webAppButtonSize.cx * numberOfWebAppButtonsX)
			rcClient.right = rcClient.left + webAppButtonSize.cx * numberOfWebAppButtonsX;

		rcClient.bottom += webAppButtonSize.cy * numberOfWebAppButtonsY;

		m_minSize.cx = webAppButtonSize.cx * numberOfWebAppButtonsX;
		m_minSize.cy += webAppButtonSize.cy * numberOfWebAppButtonsY;
	}
	else
	{
		m_minSize.cx = GetSystemMetricsForWindow(m_hWnd, SM_CXICON) +
			GetSystemMetricsForWindow(m_hWnd, SM_CXVSCROLL);
	}

	CRect rcWindow{ rcClient };
	WndAdjustWindowRect(m_hWnd, &rcWindow);

	HMONITOR hMonitor = MonitorFromPoint(ptEvent, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorinfo = { sizeof(MONITORINFO) };
	if(GetMonitorInfo(hMonitor, &monitorinfo))
	{
		CRect rcMonitor{ monitorinfo.rcMonitor };
		CRect rcWindowPrev{ rcWindow };

		if(rcWindow.Width() > rcMonitor.Width() ||
			rcWindow.Height() > rcMonitor.Height())
		{
			if(rcWindow.Height() > rcMonitor.Height())
			{
				rcWindow.top = 0;
				rcWindow.bottom = rcMonitor.Height();
			}

			editWnd.ShowScrollBar(SB_VERT);
			rcWindow.right += GetSystemMetricsForWindow(m_hWnd, SM_CXVSCROLL);
			if(rcWindow.Width() > rcMonitor.Width())
			{
				rcWindow.left = 0;
				rcWindow.right = rcMonitor.Width();
			}
		}

		if(rcWindow.left < rcMonitor.left)
		{
			rcWindow.MoveToX(rcMonitor.left);
		}
		else if(rcWindow.right > rcMonitor.right)
		{
			rcWindow.MoveToX(rcMonitor.right - rcWindow.Width());
		}

		if(rcWindow.top < rcMonitor.top)
		{
			rcWindow.MoveToY(rcMonitor.top);
		}
		else if(rcWindow.bottom > rcMonitor.bottom)
		{
			rcWindow.MoveToY(rcMonitor.bottom - rcWindow.Height());
		}

		if(rcWindowPrev != rcWindow)
		{
			rcClient = rcWindow;
			WndUnadjustWindowRect(m_hWnd, &rcClient);
		}
	}

	if(numberOfWebAppButtons > 0)
	{
		if(rcClient.bottom - webAppButtonSize.cy * numberOfWebAppButtonsY > rcClient.top)
		{
			rcClient.bottom -= webAppButtonSize.cy * numberOfWebAppButtonsY;

			CPoint ptButton{ 0, rcClient.Height() };

			for(int i = 0; i < numberOfWebAppButtons; i++)
			{
				if(i > 0 && i % numberOfWebAppButtonsX == 0)
				{
					ptButton.x = 0;
					ptButton.y += webAppButtonSize.cy;
				}

				CButton webAppButton = GetDlgItem(IDC_WEB_BUTTON_1 + i);
				webAppButton.SetWindowPos(NULL, ptButton.x, ptButton.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

				ptButton.x += webAppButtonSize.cx;
			}
		}
		else
		{
			// Hide WebApp buttons.
			for(int i = 0; i < numberOfWebAppButtons; i++)
			{
				CButton webAppButton = GetDlgItem(IDC_WEB_BUTTON_1 + i);
				webAppButton.ShowWindow(SW_HIDE);
			}
		}
	}

	SetWindowPos(NULL, &rcWindow, SWP_NOZORDER | SWP_NOACTIVATE);
	editWnd.SetWindowPos(NULL, 0, 0, rcClient.Width(), rcClient.Height(), SWP_NOZORDER | SWP_NOACTIVATE);

	CRect minSizeRect{ CPoint{}, m_minSize };
	WndAdjustWindowRect(m_hWnd, &minSizeRect);
	m_minSize = minSizeRect.Size();
}

void CTextDlg::OnSelectionMaybeChanged()
{
	if(m_config.m_autoCopySelection)
	{
		int start, end;
		m_wndEdit.GetSel(start, end);
		if(start < end && (start != m_lastSelStart || end != m_lastSelEnd))
		{
			CString str;
			m_wndEdit.GetWindowText(str);

			SetClipboardText(str.Mid(start, end - start));

			m_lastSelStart = start;
			m_lastSelEnd = end;
		}
	}
}

void CTextDlg::MakePinned()
{
	if(m_pinned)
		return;

	CRect rc;
	GetWindowRect(&rc);
	WndUnadjustWindowRect(m_hWnd, &rc);

	CRect minSizeRect{ CPoint{}, m_minSize };
	WndUnadjustWindowRect(m_hWnd, &minSizeRect);

	// Add caption with an exit button.
	SetWindowText(L"Textify");

	ModifyStyle(WS_BORDER, WS_THICKFRAME | DS_MODALFRAME | WS_CAPTION | WS_SYSMENU);

	// Note: this causes WM_SIZE to be sent
	WndAdjustWindowRect(m_hWnd, &rc);
	SetWindowPos(NULL, rc, SWP_NOZORDER | SWP_FRAMECHANGED);

	WndAdjustWindowRect(m_hWnd, &minSizeRect);
	m_minSize = minSizeRect.Size();

	m_editIndexes.clear();

	CEdit editWnd = GetDlgItem(IDC_EDIT);
	editWnd.ShowScrollBar(SB_VERT);

	m_pinned = true;
}

namespace
{
	void AppendStringToTextifyResults(CString& string, CString append, std::vector<int>& indexes)
	{
		// Convert all newlines to CRLF and trim trailing newlines.
		append.Replace(L"\r\n", L"\n");
		append.Replace(L"\r", L"\n");
		append.TrimRight(L"\n");
		append.Replace(L"\n", L"\r\n");

		if(append.IsEmpty())
			return;

		if(!string.IsEmpty())
		{
			string += L"\r\n";
			indexes.push_back(string.GetLength());
		}

		string += append;
	}

	void GetAccessibleInfoFromPointMSAA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes)
	{
		outString.Empty();
		outRc = CRect{ pt, CSize{ 0, 0 } };
		outIndexes = std::vector<int>();

		HRESULT hr;

		CComPtr<IAccessible> pAcc;
		CComVariant vtChild;
		hr = AccessibleObjectFromPoint(pt, &pAcc, &vtChild);
		if(FAILED(hr) || !pAcc)
			return;

		// Chromium has a bug in which the correct element is sometimes
		// returned only after a second query. Do it.
		pAcc.Release();
		vtChild.Clear();
		hr = AccessibleObjectFromPoint(pt, &pAcc, &vtChild);
		if(FAILED(hr) || !pAcc)
			return;

		hr = WindowFromAccessibleObject(pAcc, &outWindow.m_hWnd);
		if(FAILED(hr))
			return;

		DWORD processId = 0;
		GetWindowThreadProcessId(outWindow.m_hWnd, &processId);

		while(true)
		{
			CString string;
			std::vector<int> indexes;

			CComBSTR bsName;
			hr = pAcc->get_accName(vtChild, &bsName);
			if(SUCCEEDED(hr) && bsName)
			{
				AppendStringToTextifyResults(string, bsName.m_str, indexes);
			}

			CComBSTR bsValue;
			hr = pAcc->get_accValue(vtChild, &bsValue);
			if(SUCCEEDED(hr) && bsValue && bsValue != bsName)
			{
				AppendStringToTextifyResults(string, bsValue.m_str, indexes);
			}

			CComVariant vtRole;
			hr = pAcc->get_accRole(CComVariant(CHILDID_SELF), &vtRole);
			if(FAILED(hr) || vtRole.lVal != ROLE_SYSTEM_TITLEBAR) // ignore description for the system title bar
			{
				CComBSTR bsDescription;
				hr = pAcc->get_accDescription(vtChild, &bsDescription);
				if(SUCCEEDED(hr) && bsDescription && bsDescription != bsName && bsDescription != bsValue)
				{
					AppendStringToTextifyResults(string, bsDescription.m_str, indexes);
				}
			}

			if(!string.IsEmpty())
			{
				outString = string;
				outIndexes = indexes;
				break;
			}

			if(vtChild.lVal == CHILDID_SELF)
			{
				CComPtr<IDispatch> pDispParent;
				hr = pAcc->get_accParent(&pDispParent);
				if(FAILED(hr) || !pDispParent)
					break;

				CComQIPtr<IAccessible> pAccParent(pDispParent);

				HWND hWnd;
				hr = WindowFromAccessibleObject(pAccParent, &hWnd);
				if(FAILED(hr))
					break;

				DWORD compareProcessId = 0;
				GetWindowThreadProcessId(hWnd, &compareProcessId);
				if(compareProcessId != processId)
					break;

				pAcc.Attach(pAccParent.Detach());
			}
			else
			{
				vtChild.lVal = CHILDID_SELF;
			}
		}

		long pxLeft, pyTop, pcxWidth, pcyHeight;
		hr = pAcc->accLocation(&pxLeft, &pyTop, &pcxWidth, &pcyHeight, vtChild);
		if(SUCCEEDED(hr))
		{
			outRc = CRect{ CPoint{ pxLeft, pyTop }, CSize{ pcxWidth, pcyHeight } };
		}
	}

	bool GetAccessibleInfoFromPointUIA(POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes)
	{
		outString.Empty();
		outRc = CRect{ pt, CSize{ 0, 0 } };
		outIndexes = std::vector<int>();

		HRESULT hr;

		CComPtr<IUIAutomation> uia;
		hr = uia.CoCreateInstance(CLSID_CUIAutomation);
		if(FAILED(hr) || !uia)
			return false;

		CComPtr<IUIAutomationElement> element;
		hr = uia->ElementFromPoint(pt, &element);
		if(FAILED(hr) || !element)
			return true;

		// Chromium has a bug in which the correct element is sometimes
		// returned only after a second query. Do it.
		element.Release();
		hr = uia->ElementFromPoint(pt, &element);
		if(FAILED(hr) || !element)
			return true;

		CComPtr<IUIAutomationCondition> trueCondition;
		hr = uia->CreateTrueCondition(&trueCondition);
		if(FAILED(hr) || !trueCondition)
			return true;

		CComPtr<IUIAutomationTreeWalker> treeWalker;
		hr = uia->CreateTreeWalker(trueCondition, &treeWalker);
		if(FAILED(hr) || !treeWalker)
			return true;

		int processId = 0;
		hr = element->get_CurrentProcessId(&processId);
		if(FAILED(hr))
			return true;

		while(true)
		{
			CString string;
			std::vector<int> indexes;

			CComBSTR bsName;
			hr = element->get_CurrentName(&bsName);
			if(SUCCEEDED(hr) && bsName)
			{
				AppendStringToTextifyResults(string, bsName.m_str, indexes);
			}

			CComVariant varValue;
			hr = element->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &varValue);
			if(SUCCEEDED(hr) && varValue.vt == VT_BSTR && bsName != varValue.bstrVal)
			{
				AppendStringToTextifyResults(string, varValue.bstrVal, indexes);
			}

			if(!string.IsEmpty())
			{
				outString = string;
				outIndexes = indexes;
				break;
			}

			CComPtr<IUIAutomationElement> parentElement;
			hr = treeWalker->GetParentElement(element, &parentElement);
			if(FAILED(hr) || !parentElement)
				break;

			int compareProcessId = 0;
			hr = parentElement->get_CurrentProcessId(&compareProcessId);
			if(FAILED(hr) || compareProcessId != processId)
				break;

			element.Attach(parentElement.Detach());
		}

		hr = element->get_CurrentNativeWindowHandle((UIA_HWND*)&outWindow.m_hWnd);
		if(SUCCEEDED(hr) && !outWindow)
		{
			CComPtr<IUIAutomationElement> parentElement;
			hr = treeWalker->GetParentElement(element, &parentElement);
			if(SUCCEEDED(hr) && parentElement)
			{
				while(true)
				{
					hr = parentElement->get_CurrentNativeWindowHandle((UIA_HWND*)&outWindow.m_hWnd);
					if(FAILED(hr) || outWindow)
						break;

					CComPtr<IUIAutomationElement> nextParentElement;
					hr = treeWalker->GetParentElement(parentElement, &nextParentElement);
					if(FAILED(hr) || !parentElement)
						break;

					parentElement.Attach(nextParentElement.Detach());
				}
			}
		}

		CRect boundingRc;
		hr = element->get_CurrentBoundingRectangle(&boundingRc);
		if(SUCCEEDED(hr))
		{
			outRc = boundingRc;
		}

		return true;
	}

	void GetAccessibleInfoFromPoint(TextRetrievalMethod method, POINT pt, CWindow& outWindow, CString& outString, CRect& outRc, std::vector<int>& outIndexes)
	{
		if(method == TextRetrievalMethod::msaa)
		{
			GetAccessibleInfoFromPointMSAA(pt, outWindow, outString, outRc, outIndexes);
			return;
		}

		if(method == TextRetrievalMethod::uia)
		{
			GetAccessibleInfoFromPointUIA(pt, outWindow, outString, outRc, outIndexes);
			return;
		}

		// Both MSAA and UIA have downsides specific to them:
		// MSAA: Isn't supported by new programs, e.g. the taskbar in Windows 11.
		// UIA: The returned element is not the most nested, e.g. with links in
		// Chromium or with the Textify text above the "Homepage" link. Also,
		// Firefox on Windows 7 returns an empty result:
		// https://bugzilla.mozilla.org/show_bug.cgi?id=1406295
		// As an attempt to choose the best result, both are used, and the MSAA
		// result is returned if either UIA returns an empty result, or if
		// MSAA's rectangle is smaller, in the assumption that it's a more
		// nested element and a better result.

		if(!GetAccessibleInfoFromPointUIA(pt, outWindow, outString, outRc, outIndexes) ||
			(outString.IsEmpty() && outRc.IsRectNull()))
		{
			GetAccessibleInfoFromPointMSAA(pt, outWindow, outString, outRc, outIndexes);
			return;
		}

		CWindow outWindowMsaa;
		CString outStringMsaa;
		CRect outRcMsaa;
		GetAccessibleInfoFromPointMSAA(pt, outWindowMsaa, outStringMsaa, outRcMsaa, outIndexes);

		if(
			outRcMsaa.left >= outRc.left &&
			outRcMsaa.top >= outRc.top &&
			outRcMsaa.right <= outRc.right &&
			outRcMsaa.bottom <= outRc.bottom && (
				outRcMsaa.left > outRc.left ||
				outRcMsaa.top > outRc.top ||
				outRcMsaa.right < outRc.right ||
				outRcMsaa.bottom < outRc.bottom
				)
			)
		{
			outWindow = outWindowMsaa;
			outString = outStringMsaa;
			outRc = outRcMsaa;
		}
	}

	CSize GetEditControlTextSize(CEdit window, LPCTSTR lpszString, int nMaxWidth /*= INT_MAX*/)
	{
		CFontHandle pEdtFont = window.GetFont();
		if(!pEdtFont)
			return CSize{};

		CClientDC oDC{ window };
		CFontHandle pOldFont = oDC.SelectFont(pEdtFont);

		CRect rc{ 0, 0, nMaxWidth, 0 };
		oDC.DrawTextEx((LPTSTR)lpszString, -1, &rc, DT_CALCRECT | DT_EDITCONTROL | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS | DT_TABSTOP);

		oDC.SelectFont(pOldFont);

		return rc.Size();
	}

	CSize TextSizeToEditClientSize(HWND hWnd, CEdit editWnd, CSize textSize)
	{
		CRect rc{ CPoint{ 0, 0 }, textSize };
		WndAdjustWindowRect(editWnd, &rc);

		UINT nLeftMargin, nRightMargin;
		editWnd.GetMargins(nLeftMargin, nRightMargin);

		CSize editClientSize;

		// Experiments show that this works kinda ok.
		editClientSize.cx = rc.Width() +
			nLeftMargin + nRightMargin +
			2 * GetSystemMetricsForWindow(hWnd, SM_CXBORDER) + 2 * GetSystemMetricsForWindow(hWnd, SM_CXDLGFRAME);

		editClientSize.cy = rc.Height() +
			2 * GetSystemMetricsForWindow(hWnd, SM_CYBORDER)/* +
			2 * GetSystemMetricsForWindow(hWnd, SM_CYDLGFRAME)*/;

		return editClientSize;
	}

	CSize EditClientSizeToTextSize(HWND hWnd, CEdit editWnd, CSize editClientSize)
	{
		UINT nLeftMargin, nRightMargin;
		editWnd.GetMargins(nLeftMargin, nRightMargin);

		editClientSize.cx -=
			nLeftMargin + nRightMargin +
			2 * GetSystemMetricsForWindow(hWnd, SM_CXBORDER) + 2 * GetSystemMetricsForWindow(hWnd, SM_CXDLGFRAME);

		editClientSize.cy -=
			2 * GetSystemMetricsForWindow(hWnd, SM_CYBORDER)/* +
			2 * GetSystemMetricsForWindow(hWnd, SM_CYDLGFRAME)*/;

		CRect rc{ CPoint{ 0, 0 }, editClientSize };
		WndUnadjustWindowRect(editWnd, &rc);

		return rc.Size();
	}
}
