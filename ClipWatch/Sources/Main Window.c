#include <ClipWatch.h>

static void CWMonitorBoundsCheck(HMONITOR Monitor, RECT* WindowRect) {
	MONITORINFO MonitorInfo;
	MonitorInfo.cbSize = sizeof(MonitorInfo);
	GetMonitorInfoW(Monitor, &MonitorInfo);

	if(WindowRect->left < MonitorInfo.rcMonitor.left) {
		WindowRect->right += MonitorInfo.rcMonitor.left - WindowRect->left;
		WindowRect->left = MonitorInfo.rcMonitor.left;
	}
	if(WindowRect->right > MonitorInfo.rcMonitor.right) {
		WindowRect->left -= WindowRect->right - MonitorInfo.rcMonitor.right;
		WindowRect->right = MonitorInfo.rcMonitor.right;
	}
	if(WindowRect->top < MonitorInfo.rcMonitor.top) {
		WindowRect->bottom += MonitorInfo.rcMonitor.top - WindowRect->top;
		WindowRect->top = MonitorInfo.rcMonitor.top;
	}
	if(WindowRect->bottom > MonitorInfo.rcMonitor.bottom) {
		WindowRect->top -= WindowRect->bottom - MonitorInfo.rcMonitor.bottom;
		WindowRect->bottom = MonitorInfo.rcMonitor.bottom;
	}

	return;
}

static FORCEINLINE void CWPositionPopup(void) {
	RECT Rect = {0};

	HDC DC = CreateCompatibleDC(NULL);
	SelectObject(DC, CW.UI.Font);
	DrawTextW(DC, CW.Config.Text, (int)wcslen(CW.Config.Text), &Rect, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
	DeleteDC(DC);

	Rect.right += 10;
	Rect.bottom += 16;

	if(CW.Config.Flags & CW_CFG_POS_TO_CARET) {
		POINT Position;
		if(!GetCaretPos(&Position)) goto FailedGetCaretPos;
		ClientToScreen(GetActiveWindow(), &Position);
		Rect.left = Position.x + 10;
		Rect.top = Position.y + 10;
		CWMonitorBoundsCheck(MonitorFromPoint(Position, MONITOR_DEFAULTTONEAREST), &Rect);
	}

FailedGetCaretPos:
	if(CW.Config.Flags & CW_CFG_POS_TO_CURSOR) {
		POINT Position;
		GetPhysicalCursorPos(&Position);
		Rect.left = Position.x + 10;
		Rect.top = Position.y + 10;
		CWMonitorBoundsCheck(MonitorFromPoint(Position, MONITOR_DEFAULTTONEAREST), &Rect);
	}
	else if(CW.Config.Flags & CW_CFG_POS_TO_NEAREST) {
		HWND ActiveWindow = GetActiveWindow();
		HMONITOR Monitor = MonitorFromWindow(ActiveWindow, MONITOR_DEFAULTTONEAREST);
		MONITORINFO MonitorInfo;
		MonitorInfo.cbSize = sizeof(MonitorInfo);
		GetMonitorInfoW(Monitor, &MonitorInfo);

		Rect.left = (MonitorInfo.rcWork.right / 2) - (Rect.right / 2);
		Rect.top = MonitorInfo.rcWork.bottom - 35 - Rect.bottom;
	}
	else if(CW.Config.Flags & CW_CFG_POS_TO_PRIMARY) {
		HMONITOR Monitor = MonitorFromPoint((POINT){0}, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO MonitorInfo;
		MonitorInfo.cbSize = sizeof(MonitorInfo);
		GetMonitorInfoW(Monitor, &MonitorInfo);

		Rect.left = (MonitorInfo.rcWork.right / 2) - (Rect.right / 2);
		Rect.top = MonitorInfo.rcWork.bottom - 35 - Rect.bottom;
	}

	SetWindowPos(CW.Windows.Main, HWND_TOPMOST, Rect.left, Rect.top, Rect.right, Rect.bottom, SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOSENDCHANGING);
	return;
}

/* Popup animator thread. Also fixes positioning. */
#pragma warning(suppress: 4100)
static dword WINAPI CWPopupAnimator(void* Unused) {
	CWPositionPopup();
	AnimateWindow(CW.Windows.Main, CW.Config.FadeInDuration, AW_BLEND);
	Sleep(CW.Config.MidFadeDelay);
	AnimateWindow(CW.Windows.Main, CW.Config.FadeOutDuration, AW_BLEND | AW_HIDE);

	if(CW.UI.UpdateBGBrush) {
		DeleteObject(CW.UI.BackgroundColourBrush);
		CW.UI.BackgroundColourBrush = CW.UI.UpdateBGBrush;
		CW.UI.UpdateBGBrush = NULL;
		SetClassLongPtrW(CW.Windows.Main, GCLP_HBRBACKGROUND, (intptr)CW.UI.BackgroundColourBrush);
	}
	if(CW.UI.UpdateFont) {
		DeleteObject(CW.UI.Font);
		CW.UI.Font = CW.UI.UpdateFont;
		CW.UI.UpdateFont = NULL;
	}
	if(CW.Config.Flags & CW_CFG_RESET_TEXT) {
		CW.Config.Flags &= ~CW_CFG_RESET_TEXT;
		wcscpy_s(CW.Config.Text, ARRAYSIZE(CW.Config.Text), L"Clipboard Updated!");
	}

	ExitThread(0);
}

LRESULT CALLBACK CWWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam) {
	switch(Message) {
	case WM_CLIPBOARDUPDATE:
		if(!CW.PresentPopup) CW.PresentPopup = CreateThread(NULL, 1, CWPopupAnimator, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);

	case WM_CLOSE:
		return 0;

	case WM_SHLICON:
		switch(LOWORD(LParam)) {
		default:
			return 0;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:;
		}

		POINT CursorPos;
		GetPhysicalCursorPos(&CursorPos);
		SetForegroundWindow(CW.Windows.Main);
		switch(TrackPopupMenu(CW.NotifyIcon.Menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY | TPM_RETURNCMD | TPM_HORPOSANIMATION | TPM_VERPOSANIMATION, CursorPos.x, CursorPos.y, 0, Window, NULL)) {
		case CW_TIM_CLEAR:
			OpenClipboard(Window);
			EmptyClipboard();
			CloseClipboard();
			return 0;

		case CW_TIM_SETTINGS:
			if(!CW.Windows.Settings) CW.Windows.Settings = CreateDialogParamW(CW.ProcessModule, MAKEINTRESOURCEW(CW_IDD_SETTINGS), NULL, CWSettingsDialog, 0);
			else SetForegroundWindow(CW.Windows.Settings);
			return 0;

		case CW_TIM_ABOUT:
			if(!CW.Windows.About) CW.Windows.About = CreateDialogParamW(CW.ProcessModule, MAKEINTRESOURCEW(CW_IDD_ABOUT), NULL, CWAboutDialog, 0);
			else SetForegroundWindow(CW.Windows.About);
			return 0;

		case CW_TIM_EXIT:
			PostQuitMessage(0);
		}
		return 0;

	case WM_PAINT:
		PAINTSTRUCT PaintStruct;
		WParam = (WPARAM)BeginPaint(Window, &PaintStruct);

	case WM_PRINTCLIENT:
		RECT ClientRect;
		GetClientRect(Window, &ClientRect);

		HDC const restrict DC = (HDC)WParam;
		SetBkColor(DC, CW.Config.BackgroundColour);
		SetTextColor(DC, CW.Config.TextColour);
		SelectObject(DC, CW.UI.Font);
		DrawTextW(DC, CW.Config.Text, (int)wcslen(CW.Config.Text), &ClientRect, DT_CENTER | DT_NOCLIP | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);
		if(Message == WM_PAINT) EndPaint(Window, &PaintStruct);
		return 0;

	default:
		return DefWindowProcW(Window, Message, WParam, LParam);
	}
}