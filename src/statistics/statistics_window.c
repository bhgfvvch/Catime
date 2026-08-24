#include "statistics_window_internal.h"
#include "../../resource/resource_app_ids.h"
#include "language.h"
#include "log.h"
#include "statistics/statistics_navigation.h"
#include <windowsx.h>
#include <string.h>

#define STATISTICS_WINDOW_CLASS L"CatimeStatisticsWindow"

StatisticsWindowState g_statisticsWindow = {0};

static void SetToday(SYSTEMTIME* date) {
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    SystemTimeToTzSpecificLocalTime(NULL, &utc, date);
    date->wHour = date->wMinute = date->wSecond = date->wMilliseconds = 0;
}

static void RefreshQuery(void) {
    Statistics_Query(g_statisticsWindow.range, &g_statisticsWindow.anchor,
                     &g_statisticsWindow.summary);
}

void StatisticsWindow_ChangeRange(StatisticsRangeKind range) {
    g_statisticsWindow.range = range;
    g_statisticsWindow.category_offset = 0;
    SetToday(&g_statisticsWindow.anchor);
    RefreshQuery();
    if (g_statisticsWindow.hwnd) InvalidateRect(g_statisticsWindow.hwnd, NULL, FALSE);
}

void StatisticsWindow_Navigate(int direction) {
    if (direction > 0 && !StatisticsWindow_CanNavigateNext()) return;
    StatisticsNavigation_Move(g_statisticsWindow.range,
                              &g_statisticsWindow.anchor, direction);
    RefreshQuery();
    InvalidateRect(g_statisticsWindow.hwnd, NULL, FALSE);
}

BOOL StatisticsWindow_CanNavigateNext(void) {
    if (g_statisticsWindow.range == STATS_RANGE_ALL) return FALSE;
    SYSTEMTIME today;
    SetToday(&today);
    return StatisticsNavigation_CanMoveNext(g_statisticsWindow.range,
        &g_statisticsWindow.anchor, &today);
}

static void HandleClick(int x, int y) {
    RECT client;
    GetClientRect(g_statisticsWindow.hwnd, &client);
    int width = client.right - client.left;
    int tabWidth = width / 5;
    int tabTop = DialogModern_Scale(g_statisticsWindow.dpi, 48);
    int tabBottom = DialogModern_Scale(g_statisticsWindow.dpi, 88);
    if (y >= tabTop && y < tabBottom) {
        int tab = x / (tabWidth > 0 ? tabWidth : 1);
        if (tab >= 0 && tab < 5) StatisticsWindow_ChangeRange((StatisticsRangeKind)tab);
        return;
    }
    if (y >= DialogModern_Scale(g_statisticsWindow.dpi, 92) &&
        y < DialogModern_Scale(g_statisticsWindow.dpi, 132)) {
        int edge = DialogModern_Scale(g_statisticsWindow.dpi, 90);
        if (x < edge) StatisticsWindow_Navigate(-1);
        if (x > width - edge) StatisticsWindow_Navigate(1);
    }
}

static LRESULT CALLBACK StatisticsWindowProc(HWND hwnd, UINT message,
                                             WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_CREATE: {
            g_statisticsWindow.hwnd = hwnd;
            g_statisticsWindow.dpi = DialogModern_GetDpi(hwnd);
            DialogModernPalette palette;
            DialogModern_ResolvePalette(&palette);
            DialogModern_ApplyTheme(hwnd, palette.darkMode);
            DialogModern_ApplyWindowShape(hwnd, g_statisticsWindow.dpi, 8);
            return 0;
        }
        case WM_LBUTTONUP:
            HandleClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEWHEEL: {
            int maximum = g_statisticsWindow.summary.category_count - 4;
            if (maximum < 0) maximum = 0;
            g_statisticsWindow.category_offset +=
                GET_WHEEL_DELTA_WPARAM(wp) < 0 ? 1 : -1;
            if (g_statisticsWindow.category_offset < 0) {
                g_statisticsWindow.category_offset = 0;
            }
            if (g_statisticsWindow.category_offset > maximum) {
                g_statisticsWindow.category_offset = maximum;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint;
            RECT client;
            HDC dc = BeginPaint(hwnd, &paint);
            GetClientRect(hwnd, &client);
            StatisticsWindow_Paint(hwnd, dc, &client);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lp;
            info->ptMinTrackSize.x = DialogModern_Scale(g_statisticsWindow.dpi, 680);
            info->ptMinTrackSize.y = DialogModern_Scale(g_statisticsWindow.dpi, 560);
            return 0;
        }
        case WM_DPICHANGED: {
            g_statisticsWindow.dpi = HIWORD(wp);
            RECT* suggested = (RECT*)lp;
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DialogModern_ApplyWindowShape(hwnd, g_statisticsWindow.dpi, 8);
            return 0;
        }
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE: {
            DialogModernPalette palette;
            DialogModern_ResolvePalette(&palette);
            DialogModern_ApplyTheme(hwnd, palette.darkMode);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_statisticsWindow.hwnd = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

static BOOL RegisterStatisticsClass(void) {
    static BOOL registered = FALSE;
    if (registered) return TRUE;
    WNDCLASSEXW windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = GetModuleHandleW(NULL);
    windowClass.lpfnWndProc = StatisticsWindowProc;
    windowClass.lpszClassName = STATISTICS_WINDOW_CLASS;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIconW(windowClass.hInstance,
                                  MAKEINTRESOURCEW(IDI_CATIME));
    registered = RegisterClassExW(&windowClass) != 0;
    return registered;
}

void Statistics_ShowWindow(HWND owner) {
    if (g_statisticsWindow.hwnd) {
        ShowWindow(g_statisticsWindow.hwnd, SW_RESTORE);
        SetForegroundWindow(g_statisticsWindow.hwnd);
        return;
    }
    if (!RegisterStatisticsClass()) {
        LOG_WINDOWS_ERROR("Statistics window class registration failed");
        return;
    }
    memset(&g_statisticsWindow, 0, sizeof(g_statisticsWindow));
    g_statisticsWindow.owner = owner;
    g_statisticsWindow.range = STATS_RANGE_TODAY;
    SetToday(&g_statisticsWindow.anchor);
    RefreshQuery();
    const wchar_t* title = GetLocalizedString(L"统计", L"Statistics");
    UINT dpi = DialogModern_GetDpi(owner);
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, STATISTICS_WINDOW_CLASS,
        title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, DialogModern_Scale(dpi, 900),
        DialogModern_Scale(dpi, 680), owner, NULL,
        GetModuleHandleW(NULL), NULL);
    if (window) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
}

void Statistics_RefreshOpenWindow(void) {
    if (!g_statisticsWindow.hwnd) return;
    RefreshQuery();
    InvalidateRect(g_statisticsWindow.hwnd, NULL, FALSE);
}
