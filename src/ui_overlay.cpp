#include "ui_overlay.h"
#include <commctrl.h>
#include <string>
#include <vector>
#include <chrono>
#include <cstdio>
#include <thread>
#include "async_engine.h"

#define IDC_OVERLAY_CANCEL       1001
#define IDC_OVERLAY_RERUN        1002
#define IDC_OVERLAY_KILL_BLOCKER 1003

extern HMODULE g_hProxyModule;

struct OverlayState {
    std::function<void()> onCancel;
    std::function<void()> onRerun;
    std::function<void(unsigned long)> onKillBlocker;
    bool hasOrderBy;
    unsigned long blockingThreadId = 0;
    HWND hTitleLabel = NULL;
    HWND hTimerLabel = NULL;
    HWND hCancelBtn = NULL;
    HWND hRerunBtn = NULL;
    HWND hKillBlockerBtn = NULL;
    HFONT hFontBold = NULL;
    HFONT hFontRegular = NULL;
    HBRUSH hBgBrush = NULL;
    COLORREF textColor = RGB(33, 37, 41);
    COLORREF bgColor = RGB(245, 246, 248);
    std::chrono::steady_clock::time_point startTime;
    bool isWarning = false;
};

static const wchar_t* OVERLAY_CLASS = L"HeidiSQL_Proxy_Overlay_Class";

struct EnumThreadParam {
    DWORD targetThreadId;
    HWND hWndFound;
};

static BOOL CALLBACK EnumThreadWndProc(HWND hWnd, LPARAM lParam) {
    EnumThreadParam* param = (EnumThreadParam*)lParam;
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hWnd, &pid);
    if (tid == param->targetThreadId && IsWindowVisible(hWnd)) {
        LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
        if ((style & WS_CHILD) == 0 && (style & WS_CAPTION)) {
            param->hWndFound = hWnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND UIOverlay_FindMainWindow(DWORD threadId) {
    EnumThreadParam param = { threadId, NULL };
    EnumThreadWindows(threadId, EnumThreadWndProc, (LPARAM)&param);
    return param.hWndFound;
}

HWND UIOverlay_FindActiveParent(DWORD threadId) {
    HWND hActive = GetActiveWindow();
    if (hActive && IsWindowVisible(hActive)) {
        DWORD pid = 0;
        if (GetWindowThreadProcessId(hActive, &pid) == threadId) {
            return hActive;
        }
    }
    HWND hFg = GetForegroundWindow();
    if (hFg && IsWindowVisible(hFg)) {
        DWORD pid = 0;
        if (GetWindowThreadProcessId(hFg, &pid) == threadId) {
            return hFg;
        }
    }
    return UIOverlay_FindMainWindow(threadId);
}

static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    OverlayState* state = (OverlayState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    switch (uMsg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        state = (OverlayState*)cs->lpCreateParams;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)state);

        HINSTANCE hInst = GetModuleHandleW(NULL);

        state->hFontBold = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        state->hFontRegular = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        state->bgColor = RGB(245, 246, 248);
        state->textColor = RGB(33, 37, 41);
        state->hBgBrush = CreateSolidBrush(state->bgColor);

        state->hTitleLabel = CreateWindowExW(0, L"STATIC", L"Executing Database Query...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 18, 390, 24, hWnd, NULL, hInst, NULL);
        SendMessageW(state->hTitleLabel, WM_SETFONT, (WPARAM)state->hFontBold, TRUE);

        state->hTimerLabel = CreateWindowExW(0, L"STATIC", L"Elapsed: 0.1s",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 48, 390, 24, hWnd, NULL, hInst, NULL);
        SendMessageW(state->hTimerLabel, WM_SETFONT, (WPARAM)state->hFontRegular, TRUE);

        if (state->hasOrderBy) {
            state->hRerunBtn = CreateWindowExW(0, L"BUTTON", L"Rerun Without ORDER BY",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                20, 88, 210, 32, hWnd, (HMENU)IDC_OVERLAY_RERUN, hInst, NULL);
            SendMessageW(state->hRerunBtn, WM_SETFONT, (WPARAM)state->hFontRegular, TRUE);

            state->hCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel Query",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                245, 88, 120, 32, hWnd, (HMENU)IDC_OVERLAY_CANCEL, hInst, NULL);
            SendMessageW(state->hCancelBtn, WM_SETFONT, (WPARAM)state->hFontRegular, TRUE);
        } else {
            state->hRerunBtn = NULL;
            state->hCancelBtn = CreateWindowExW(0, L"BUTTON", L"Cancel Query",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                155, 88, 130, 32, hWnd, (HMENU)IDC_OVERLAY_CANCEL, hInst, NULL);
            SendMessageW(state->hCancelBtn, WM_SETFONT, (WPARAM)state->hFontRegular, TRUE);
        }

        state->hKillBlockerBtn = NULL;
        SetTimer(hWnd, 1, 100, NULL);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == 1 && state && !state->isWarning) {
            double elapsedSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - state->startTime).count();
            wchar_t buf[64];
            swprintf_s(buf, L"Elapsed: %.1fs", elapsedSec);
            if (state->hTimerLabel) {
                SetWindowTextW(state->hTimerLabel, buf);
            }
        }
        return 0;
    }

    case WM_SETCURSOR: {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, state ? state->textColor : RGB(33, 37, 41));
        SetBkColor(hdcStatic, state ? state->bgColor : RGB(245, 246, 248));
        return (LRESULT)(state ? state->hBgBrush : GetStockObject(WHITE_BRUSH));
    }

    case WM_COMMAND: {
        WORD cmdId = LOWORD(wParam);
        LogDebug("[OverlayWndProc] WM_COMMAND cmdId: %d", cmdId);
        if (cmdId == IDC_OVERLAY_CANCEL) {
            if (state && state->onCancel) {
                state->isWarning = true;
                SetWindowTextW(state->hTimerLabel, L"Cancelling query on server...");
                if (state->hRerunBtn) EnableWindow(state->hRerunBtn, FALSE);
                if (state->hCancelBtn) EnableWindow(state->hCancelBtn, FALSE);
                if (state->hKillBlockerBtn) EnableWindow(state->hKillBlockerBtn, FALSE);
                std::thread([onCancel = state->onCancel]() {
                    onCancel();
                }).detach();
            }
            return 0;
        } else if (cmdId == IDC_OVERLAY_RERUN) {
            if (state && state->onRerun) {
                state->isWarning = true;
                SetWindowTextW(state->hTimerLabel, L"Clearing saved sort & rerunning without ORDER BY...");
                if (state->hRerunBtn) EnableWindow(state->hRerunBtn, FALSE);
                if (state->hCancelBtn) EnableWindow(state->hCancelBtn, FALSE);
                if (state->hKillBlockerBtn) EnableWindow(state->hKillBlockerBtn, FALSE);
                std::thread([onRerun = state->onRerun]() {
                    onRerun();
                }).detach();
            }
            return 0;
        } else if (cmdId == IDC_OVERLAY_KILL_BLOCKER) {
            if (state && state->onKillBlocker) {
                SetWindowTextW(state->hTimerLabel, L"Killing blocking connection on server...");
                if (state->hKillBlockerBtn) EnableWindow(state->hKillBlockerBtn, FALSE);
                unsigned long targetId = state->blockingThreadId;
                std::thread([onKill = state->onKillBlocker, targetId]() {
                    onKill(targetId);
                }).detach();
            }
            return 0;
        }
        break;
    }

    case WM_APP_LOCK_WARNING: {
        unsigned long blockingId = (unsigned long)wParam;
        std::wstring* pText = (std::wstring*)lParam;

        if (state && pText) {
            state->isWarning = true;
            state->blockingThreadId = blockingId;
            state->bgColor = RGB(255, 243, 205);
            state->textColor = RGB(102, 77, 3);

            if (state->hBgBrush) {
                DeleteObject(state->hBgBrush);
            }
            state->hBgBrush = CreateSolidBrush(state->bgColor);

            if (state->hTitleLabel) {
                SetWindowTextW(state->hTitleLabel, L"⚠️ Metadata Lock Contention Detected");
            }
            if (state->hTimerLabel) {
                SetWindowTextW(state->hTimerLabel, pText->c_str());
            }

            if (blockingId > 0 && !state->hKillBlockerBtn) {
                HINSTANCE hInst = GetModuleHandleW(NULL);
                wchar_t btnText[64];
                swprintf_s(btnText, L"Kill Blocker #%lu", blockingId);

                RECT rc;
                GetWindowRect(hWnd, &rc);
                SetWindowPos(hWnd, HWND_TOPMOST, rc.left, rc.top, 460, 215, SWP_NOMOVE | SWP_NOACTIVATE);

                state->hKillBlockerBtn = CreateWindowExW(0, L"BUTTON", btnText,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    20, 125, 200, 32, hWnd, (HMENU)IDC_OVERLAY_KILL_BLOCKER, hInst, NULL);
                SendMessageW(state->hKillBlockerBtn, WM_SETFONT, (WPARAM)state->hFontRegular, TRUE);
            }

            delete pText;
            InvalidateRect(hWnd, NULL, TRUE);
            UpdateWindow(hWnd);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(IDC_OVERLAY_CANCEL, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_CLOSE: {
        DestroyWindow(hWnd);
        return 0;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, 1);
        if (state) {
            if (state->hFontBold) DeleteObject(state->hFontBold);
            if (state->hFontRegular) DeleteObject(state->hFontRegular);
            if (state->hBgBrush) DeleteObject(state->hBgBrush);
            delete state;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

void UIOverlay_ThreadProc(UIOverlayParams params) {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    HINSTANCE hModuleToUse = g_hProxyModule ? g_hProxyModule : hInst;

    WNDCLASSEXW existingWc = { 0 };
    if (!GetClassInfoExW(hModuleToUse, OVERLAY_CLASS, &existingWc)) {
        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hModuleToUse;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(245, 246, 248));
        wc.lpszClassName = OVERLAY_CLASS;
        RegisterClassExW(&wc);
    }

    int width = 440;
    int height = 165;
    int posX = 100;
    int posY = 100;

    if (params.hParent && IsWindow(params.hParent)) {
        RECT rc;
        GetWindowRect(params.hParent, &rc);
        posX = rc.left + (rc.right - rc.left - width) / 2;
        posY = rc.top + (rc.bottom - rc.top - height) / 2;
    } else {
        posX = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        posY = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    }

    HMONITOR hMon = MonitorFromWindow(params.hParent ? params.hParent : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        posX = max((int)mi.rcWork.left + 40, min(posX, (int)mi.rcWork.right - width - 40));
        posY = max((int)mi.rcWork.top + 40, min(posY, (int)mi.rcWork.bottom - height - 40));
    } else {
        posX = max(40, posX);
        posY = max(40, posY);
    }

    OverlayState* state = new OverlayState();
    state->hasOrderBy = params.hasOrderBy;
    state->onCancel = params.onCancel;
    state->onRerun = params.onRerun;
    state->onKillBlocker = params.onKillBlocker;
    state->startTime = std::chrono::steady_clock::now();

    HWND hOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        OVERLAY_CLASS,
        params.title.c_str(),
        WS_POPUP | WS_CAPTION | WS_VISIBLE | WS_SYSMENU,
        posX, posY, width, height,
        NULL, NULL, hModuleToUse, state);

    if (params.pOutHwnd) {
        params.pOutHwnd->store(hOverlay);
    }

    if (hOverlay) {
        ShowWindow(hOverlay, SW_SHOW);
        UpdateWindow(hOverlay);
        SetForegroundWindow(hOverlay);
        SetActiveWindow(hOverlay);
        SetFocus(hOverlay);
    }

    LogDebug("[UIOverlay_ThreadProc] Overlay window created: %p on thread %lu, pos: (%d, %d)",
        hOverlay, GetCurrentThreadId(), posX, posY);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (params.pOutHwnd) {
        params.pOutHwnd->store(NULL);
    }

    LogDebug("[UIOverlay_ThreadProc] Overlay thread exiting.");
}
