#include "statistics_internal.h"
#include "dialog/dialog_modern.h"
#include "language.h"
#include "log.h"
#include <commctrl.h>
#include <string.h>

#define CATEGORY_CLASS L"CatimeCategoryManager"
#define ID_CATEGORY_LIST 6101
#define ID_CATEGORY_NAME 6102
#define ID_CATEGORY_COLOR 6103
#define ID_CATEGORY_ADD 6104
#define ID_CATEGORY_RENAME 6105
#define ID_CATEGORY_DELETE 6106
#define ID_CATEGORY_UP 6107
#define ID_CATEGORY_DOWN 6108

static HWND s_window;
static HFONT s_font;
static UINT s_dpi = 96;

static int Scale(int value) {
    return DialogModern_Scale(s_dpi, value);
}

static void ToWide(const char* input, wchar_t* output, int count) {
    if (MultiByteToWideChar(CP_UTF8, 0, input, -1, output, count) <= 0) {
        output[0] = L'\0';
    }
}

static BOOL ReadUtf8(HWND edit, char* output, int count) {
    wchar_t wide[STATISTICS_CATEGORY_NAME_MAX] = {0};
    GetWindowTextW(edit, wide, _countof(wide));
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, output, count,
                               NULL, NULL) > 0;
}

static int SelectedIndex(HWND hwnd) {
    return (int)SendDlgItemMessageW(hwnd, ID_CATEGORY_LIST,
                                   LB_GETCURSEL, 0, 0);
}

static void RefreshList(HWND hwnd, int selected) {
    HWND list = GetDlgItem(hwnd, ID_CATEGORY_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    StatisticsCategory categories[STATISTICS_MAX_CATEGORIES];
    int count = Statistics_GetCategories(categories, _countof(categories));
    for (int i = 0; i < count; ++i) {
        wchar_t name[STATISTICS_CATEGORY_NAME_MAX];
        ToWide(categories[i].name, name, _countof(name));
        if (strcmp(categories[i].id, "general") == 0) {
            wcscpy_s(name, _countof(name),
                     GetLocalizedString(L"通用", L"General"));
        }
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name);
    }
    if (count > 0) {
        if (selected < 0 || selected >= count) selected = 0;
        SendMessageW(list, LB_SETCURSEL, selected, 0);
    }
    SendMessageW(hwnd, WM_COMMAND,
                 MAKEWPARAM(ID_CATEGORY_LIST, LBN_SELCHANGE), (LPARAM)list);
}

static void LoadSelected(HWND hwnd) {
    int selected = SelectedIndex(hwnd);
    StatisticsCategory categories[STATISTICS_MAX_CATEGORIES];
    int count = Statistics_GetCategories(categories, _countof(categories));
    if (selected < 0 || selected >= count) return;
    wchar_t name[STATISTICS_CATEGORY_NAME_MAX];
    wchar_t color[STATISTICS_COLOR_MAX];
    ToWide(categories[selected].name, name, _countof(name));
    ToWide(categories[selected].color, color, _countof(color));
    SetDlgItemTextW(hwnd, ID_CATEGORY_NAME, name);
    SetDlgItemTextW(hwnd, ID_CATEGORY_COLOR, color);
    EnableWindow(GetDlgItem(hwnd, ID_CATEGORY_DELETE), selected > 0);
    EnableWindow(GetDlgItem(hwnd, ID_CATEGORY_UP), selected > 1);
    EnableWindow(GetDlgItem(hwnd, ID_CATEGORY_DOWN),
                 selected > 0 && selected + 1 < count);
}

static void ApplyOperation(HWND hwnd, UINT command) {
    int selected = SelectedIndex(hwnd);
    StatisticsCategory categories[STATISTICS_MAX_CATEGORIES];
    int count = Statistics_GetCategories(categories, _countof(categories));
    char name[STATISTICS_CATEGORY_NAME_MAX] = {0};
    char color[STATISTICS_COLOR_MAX] = {0};
    ReadUtf8(GetDlgItem(hwnd, ID_CATEGORY_NAME), name, sizeof(name));
    ReadUtf8(GetDlgItem(hwnd, ID_CATEGORY_COLOR), color, sizeof(color));
    BOOL changed = FALSE;
    if (command == ID_CATEGORY_ADD) {
        changed = Statistics_AddCategory(name, color);
        if (changed) selected = count;
    } else if (selected >= 0 && selected < count) {
        const char* id = categories[selected].id;
        if (command == ID_CATEGORY_RENAME) {
            changed = Statistics_RenameCategory(id, name);
            if (changed && color[0]) Statistics_SetCategoryColor(id, color);
        } else if (command == ID_CATEGORY_DELETE) {
            changed = Statistics_DeleteCategory(id);
            if (selected >= count - 1) selected--;
        } else if (command == ID_CATEGORY_UP) {
            changed = Statistics_MoveCategory(id, -1);
            if (changed) selected--;
        } else if (command == ID_CATEGORY_DOWN) {
            changed = Statistics_MoveCategory(id, 1);
            if (changed) selected++;
        }
    }
    if (!changed) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    RefreshList(hwnd, selected);
    Statistics_RefreshOpenWindow();
}

static HWND AddControl(HWND hwnd, const wchar_t* className,
                       const wchar_t* text, DWORD style, int id) {
    HWND control = CreateWindowExW(0, className, text,
        WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd,
        (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, (WPARAM)s_font, TRUE);
    return control;
}

static void Layout(HWND hwnd) {
    RECT client;
    GetClientRect(hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    MoveWindow(GetDlgItem(hwnd, ID_CATEGORY_LIST), Scale(16), Scale(16),
               width / 2 - Scale(24), height - Scale(32), TRUE);
    int x = width / 2 + Scale(8);
    int fieldWidth = width - x - Scale(16);
    MoveWindow(GetDlgItem(hwnd, ID_CATEGORY_NAME), x, Scale(16),
               fieldWidth, Scale(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_CATEGORY_COLOR), x, Scale(56),
               fieldWidth, Scale(30), TRUE);
    int buttonWidth = (fieldWidth - 8) / 2;
    const int ids[] = {ID_CATEGORY_ADD, ID_CATEGORY_RENAME,
                       ID_CATEGORY_DELETE, ID_CATEGORY_UP, ID_CATEGORY_DOWN};
    for (int i = 0; i < (int)_countof(ids); ++i) {
        MoveWindow(GetDlgItem(hwnd, ids[i]),
                   x + (i % 2) * (buttonWidth + Scale(8)),
                   Scale(104 + (i / 2) * 40),
                   buttonWidth, Scale(30), TRUE);
    }
}

static LRESULT CALLBACK CategoryProc(HWND hwnd, UINT message,
                                     WPARAM wp, LPARAM lp) {
    (void)lp;
    switch (message) {
        case WM_CREATE: {
            s_window = hwnd;
            s_dpi = DialogModern_GetDpi(hwnd);
            s_font = DialogModern_CreateFont(s_dpi, 14, FW_NORMAL);
            AddControl(hwnd, WC_LISTBOXW, L"", LBS_NOTIFY | WS_BORDER | WS_VSCROLL,
                       ID_CATEGORY_LIST);
            AddControl(hwnd, WC_EDITW, L"", WS_BORDER | ES_AUTOHSCROLL, ID_CATEGORY_NAME);
            AddControl(hwnd, WC_EDITW, L"#3A96DD", WS_BORDER | ES_AUTOHSCROLL,
                       ID_CATEGORY_COLOR);
            AddControl(hwnd, WC_BUTTONW, GetLocalizedString(L"新增", L"Add"),
                       BS_PUSHBUTTON, ID_CATEGORY_ADD);
            AddControl(hwnd, WC_BUTTONW, GetLocalizedString(L"保存", L"Save"),
                       BS_PUSHBUTTON, ID_CATEGORY_RENAME);
            AddControl(hwnd, WC_BUTTONW, GetLocalizedString(L"删除", L"Delete"),
                       BS_PUSHBUTTON, ID_CATEGORY_DELETE);
            AddControl(hwnd, WC_BUTTONW, GetLocalizedString(L"上移", L"Move Up"),
                       BS_PUSHBUTTON, ID_CATEGORY_UP);
            AddControl(hwnd, WC_BUTTONW, GetLocalizedString(L"下移", L"Move Down"),
                       BS_PUSHBUTTON, ID_CATEGORY_DOWN);
            DialogModernPalette palette;
            DialogModern_ResolvePalette(&palette);
            DialogModern_ApplyTheme(hwnd, palette.darkMode);
            Layout(hwnd);
            RefreshList(hwnd, 0);
            return 0;
        }
        case WM_SIZE:
            Layout(hwnd);
            return 0;
        case WM_DPICHANGED: {
            s_dpi = HIWORD(wp);
            RECT* suggested = (RECT*)lp;
            SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            DeleteObject(s_font);
            s_font = DialogModern_CreateFont(s_dpi, 14, FW_NORMAL);
            for (int id = ID_CATEGORY_LIST; id <= ID_CATEGORY_DOWN; ++id) {
                SendDlgItemMessageW(hwnd, id, WM_SETFONT, (WPARAM)s_font, TRUE);
            }
            Layout(hwnd);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_CATEGORY_LIST && HIWORD(wp) == LBN_SELCHANGE) {
                LoadSelected(hwnd);
            } else if (LOWORD(wp) >= ID_CATEGORY_ADD &&
                       LOWORD(wp) <= ID_CATEGORY_DOWN) {
                ApplyOperation(hwnd, LOWORD(wp));
            }
            return 0;
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
            DeleteObject(s_font);
            s_font = NULL;
            s_window = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

void Statistics_ShowCategoryManager(HWND owner) {
    if (s_window) { SetForegroundWindow(s_window); return; }
    static BOOL registered;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpfnWndProc = CategoryProc;
        wc.lpszClassName = CATEGORY_CLASS;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        registered = RegisterClassW(&wc) != 0;
    }
    if (!registered) return;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, CATEGORY_CLASS,
        GetLocalizedString(L"管理专注分类", L"Manage Categories"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DialogModern_Scale(DialogModern_GetDpi(owner), 620),
        DialogModern_Scale(DialogModern_GetDpi(owner), 430), owner, NULL,
        GetModuleHandleW(NULL), NULL);
    if (window) ShowWindow(window, SW_SHOW);
}
