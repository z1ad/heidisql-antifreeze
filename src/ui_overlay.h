#pragma once
#ifndef UI_OVERLAY_H
#define UI_OVERLAY_H

#include <windows.h>
#include <functional>
#include <string>
#include <atomic>

#define WM_APP_LOCK_WARNING (WM_APP + 101)

HWND UIOverlay_FindMainWindow(DWORD threadId);
HWND UIOverlay_FindActiveParent(DWORD threadId);

struct UIOverlayParams {
    HWND hParent = NULL;
    std::wstring title;
    bool hasOrderBy = false;
    std::function<void()> onCancel;
    std::function<void()> onRerun;
    std::function<void(unsigned long)> onKillBlocker;
    std::atomic<HWND>* pOutHwnd = nullptr;
};

// Thread entry point for the dedicated background UI thread
void UIOverlay_ThreadProc(UIOverlayParams params);

#endif // UI_OVERLAY_H
