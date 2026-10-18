#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <oleauto.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include "resource.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr int FILTER_ID = 101;
constexpr int LIST_ID = 102;
constexpr UINT_PTR APPLY_TIMER = 1;
constexpr COLORREF BACKGROUND = RGB(18, 20, 24);
constexpr COLORREF SURFACE = RGB(28, 31, 37);
constexpr COLORREF FOREGROUND = RGB(230, 233, 239);
constexpr COLORREF MUTED = RGB(155, 164, 179);
constexpr COLORREF HIGHLIGHT = RGB(39, 59, 79);
constexpr wchar_t SETTINGS_KEY[] = L"Software\\bry-ful\\fdList";

struct Failure
{
    std::wstring message;
};

struct WindowSettings
{
    DWORD width = 850;
    DWORD height = 620;
    DWORD maximized = FALSE;
};

WindowSettings loadWindowSettings()
{
    WindowSettings settings;
    DWORD size = sizeof(settings);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"Window", RRF_RT_REG_BINARY,
        nullptr, &settings, &size);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) { return {}; }
    if (status != ERROR_SUCCESS || size != sizeof(settings) || settings.width == 0 || settings.width > 32768 ||
        settings.height == 0 || settings.height > 32768 || settings.maximized > TRUE)
    {
        throw Failure{L"ウィンドウ設定を読み込めません。\nHKEY_CURRENT_USER\\" + std::wstring(SETTINGS_KEY) +
            L"\\Window\nエラー: " + std::to_wstring(status)};
    }
    return settings;
}

RECT centeredWindow(const WindowSettings& settings, const RECT& work)
{
    const LONG width = std::min(std::max(600L, static_cast<LONG>(settings.width)), work.right - work.left);
    const LONG height = std::min(std::max(350L, static_cast<LONG>(settings.height)), work.bottom - work.top);
    const LONG left = work.left + (work.right - work.left - width) / 2;
    const LONG top = work.top + (work.bottom - work.top - height) / 2;
    return {left, top, left + width, top + height};
}

void check(HRESULT hr, const std::wstring& message)
{
    if (FAILED(hr))
    {
        wchar_t code[32]{};
        swprintf_s(code, L"\nHRESULT: 0x%08lX", static_cast<unsigned long>(hr));
        throw Failure{message + code};
    }
}

struct Value
{
    VARIANT data{};
    Value() { VariantInit(&data); }
    ~Value() { VariantClear(&data); }
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
};

DWORD runningAfx()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        throw Failure{L"あふｗの起動状態を確認できません。"};
    }
    PROCESSENTRY32W entry{sizeof(entry)};
    DWORD pid = 0;
    unsigned count = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, L"AFXW.EXE") == 0)
            {
                pid = entry.th32ProcessID;
                ++count;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    const DWORD error = GetLastError();
    CloseHandle(snapshot);
    if (error != ERROR_NO_MORE_FILES)
    {
        throw Failure{L"プロセスの一覧を取得できません。"};
    }
    if (count != 1)
    {
        throw Failure{count == 0 ? L"あふｗが起動していません。" :
            L"あふｗが複数起動しているため、操作対象を特定できません。"};
    }
    return pid;
}

struct Automation
{
    ComPtr<IDispatch> object;
    DWORD pid = 0;
    int side = -1;
    fs::path path;

    void invoke(const wchar_t* method, Value& result, const std::wstring* text = nullptr)
    {
        LPOLESTR name = const_cast<LPOLESTR>(method);
        DISPID id{};
        check(object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id),
            std::wstring(L"あふｗのメソッドを取得できません: ") + method);
        Value argument;
        DISPPARAMS params{};
        if (text)
        {
            argument.data.vt = VT_BSTR;
            argument.data.bstrVal = SysAllocStringLen(text->data(), static_cast<UINT>(text->size()));
            if (!argument.data.bstrVal) { throw std::bad_alloc(); }
            params.rgvarg = &argument.data;
            params.cArgs = 1;
        }
        EXCEPINFO info{};
        const HRESULT hr = object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT,
            DISPATCH_METHOD, &params, &result.data, &info, nullptr);
        if (info.pfnDeferredFillIn) { info.pfnDeferredFillIn(&info); }
        std::wstring detail = info.bstrDescription ? info.bstrDescription : L"";
        SysFreeString(info.bstrSource);
        SysFreeString(info.bstrDescription);
        SysFreeString(info.bstrHelpFile);
        check(hr, std::wstring(L"あふｗとの通信に失敗しました: ") + method + L"\n" + detail);
    }

    int currentSide()
    {
        Value value;
        invoke(L"SrcWin", value);
        check(VariantChangeType(&value.data, &value.data, 0, VT_I4), L"操作側を取得できません。分類: SrcWin");
        if (value.data.lVal != 0 && value.data.lVal != 1)
        {
            throw Failure{L"あふｗのアクティブなファイル窓を確認できません。"};
        }
        return value.data.lVal;
    }

    std::wstring extract(const std::wstring& macro)
    {
        Value value;
        invoke(L"Extract", value, &macro);
        check(VariantChangeType(&value.data, &value.data, 0, VT_BSTR), L"あふｗのパスを取得できません。");
        return std::wstring(value.data.bstrVal, SysStringLen(value.data.bstrVal));
    }

    void connect()
    {
        pid = runningAfx();
        CLSID clsid{};
        check(CLSIDFromProgID(L"afxw.obj", &clsid),
            L"あふｗのオートメーションサーバが未登録です。設定の about タブから登録してください。");
        check(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(object.GetAddressOf())),
            L"あふｗのオートメーションサーバに接続できません。");
        side = currentSide();
        const std::wstring source = extract(L"$P");
        if (source.empty()) { throw Failure{L"あふｗの現在のパスが空です。"}; }
        path = source;
        if (!path.is_absolute() || !fs::is_directory(path))
        {
            throw Failure{L"通常の実在フォルダを開いてください。\n" + source};
        }
        validate();
    }

    void validate()
    {
        if (runningAfx() != pid || currentSide() != side ||
            _wcsicmp(fs::path(extract(L"$P")).lexically_normal().c_str(), path.lexically_normal().c_str()) != 0)
        {
            throw Failure{L"あふｗの操作側またはパスが起動時から変化しています。\n" + path.wstring()};
        }
    }

    bool exec(const std::wstring& command)
    {
        Value value;
        invoke(L"Exec", value, &command);
        check(VariantChangeType(&value.data, &value.data, 0, VT_BOOL), L"あふｗの実行結果を取得できません。");
        return value.data.boolVal != VARIANT_FALSE;
    }
};

struct Entry
{
    std::wstring name;
};

std::wstring lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c)
    {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

std::wstring windowText(HWND window)
{
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    const int count = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    text.resize(count);
    return text;
}

// Exec 内の置換マクロとしてファイル名の $ が解釈されないようにする。
std::wstring commandLiteral(const std::wstring& text)
{
    std::wstring result;
    for (const wchar_t c : text)
    {
        result += c;
        if (c == L'$') { result += L'$'; }
    }
    return result;
}

struct App
{
    Automation afx;
    HWND window = nullptr;
    HWND filter = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    HBRUSH backgroundBrush = CreateSolidBrush(BACKGROUND);
    HBRUSH surfaceBrush = CreateSolidBrush(SURFACE);
    std::vector<Entry> entries;
    std::vector<size_t> visible;
    bool applying = false;
    bool commandSent = false;
    bool rememberWindow = false;
    bool maximized = false;
    fs::path target;
    std::wstring command;
    ULONGLONG deadline = 0;

    void saveWindowSettings()
    {
        if (!rememberWindow) { return; }
        WINDOWPLACEMENT placement{sizeof(placement)};
        LSTATUS status = ERROR_SUCCESS;
        if (!GetWindowPlacement(window, &placement))
        {
            status = static_cast<LSTATUS>(GetLastError());
        }
        else
        {
            const RECT& rect = placement.rcNormalPosition;
            const WindowSettings settings{static_cast<DWORD>(rect.right - rect.left),
                static_cast<DWORD>(rect.bottom - rect.top), maximized ? DWORD{1} : DWORD{0}};
            status = RegSetKeyValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"Window", REG_BINARY, &settings, sizeof(settings));
        }
        if (status != ERROR_SUCCESS)
        {
            MessageBoxW(window, (L"ウィンドウ設定を保存できません。\nHKEY_CURRENT_USER\\" + std::wstring(SETTINGS_KEY) +
                L"\\Window\nエラー: " + std::to_wstring(status)).c_str(), L"fdList — エラー", MB_OK | MB_ICONERROR);
        }
    }

    ~App()
    {
        if (font) { DeleteObject(font); }
        if (backgroundBrush) { DeleteObject(backgroundBrush); }
        if (surfaceBrush) { DeleteObject(surfaceBrush); }
    }

    void styleControls()
    {
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        SetWindowTheme(list, L"", L"");
        SetWindowTheme(ListView_GetHeader(list), L"", L"");
        ListView_SetBkColor(list, SURFACE);
        ListView_SetTextBkColor(list, SURFACE);
        ListView_SetTextColor(list, FOREGROUND);
    }

    void loadDirectories()
    {
        entries.clear();
        for (const auto& item : fs::directory_iterator(afx.path))
        {
            if (item.is_directory()) { entries.push_back({item.path().filename().wstring()}); }
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
        {
            return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
    }

    void load()
    {
        afx.connect();
        loadDirectories();
    }

    void rebuild()
    {
        size_t focused = entries.size();
        const int previous = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        if (previous >= 0 && static_cast<size_t>(previous) < visible.size()) { focused = visible[previous]; }
        std::wistringstream input(lower(windowText(filter)));
        std::vector<std::wstring> words;
        for (std::wstring word; input >> word;) { words.push_back(word); }
        visible.clear();
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        int focusRow = 0;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            auto& entry = entries[i];
            const auto name = lower(entry.name);
            if (!std::all_of(words.begin(), words.end(), [&](const auto& word) { return name.find(word) != std::wstring::npos; })) { continue; }
            const int row = static_cast<int>(visible.size());
            visible.push_back(i);
            if (i == focused) { focusRow = row; }
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = entry.name.data();
            ListView_InsertItem(list, &item);
        }
        if (!visible.empty())
        {
            ListView_SetItemState(list, focusRow, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
            ListView_EnsureVisible(list, focusRow, FALSE);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
        const auto text = std::to_wstring(visible.size()) + L" 件    Enter: 移動    Escape: 終了";
        SetWindowTextW(status, text.c_str());
    }

    void moveActive(int direction)
    {
        if (visible.empty()) { return; }
        const int current = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        const int next = current < 0 ? 0 : std::clamp(current + direction, 0, static_cast<int>(visible.size()) - 1);
        if (current == next) { return; }
        ListView_SetItemState(list, -1, 0, LVIS_FOCUSED | LVIS_SELECTED);
        ListView_SetItemState(list, next, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
        ListView_EnsureVisible(list, next, FALSE);
    }

    void beginMove()
    {
        if (applying) { return; }
        const int row = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        if (row < 0 || static_cast<size_t>(row) >= visible.size()) { return; }
        afx.validate();
        target = afx.path / entries[visible[row]].name;
        if (!fs::is_directory(target)) { throw Failure{L"ディレクトリが存在しません。\n" + target.wstring()}; }
        command = L"&EXCD -" + std::wstring(afx.side == 0 ? L"l" : L"r") + L"\"" + commandLiteral(target.wstring()) + L"\\\"";
        applying = true;
        commandSent = false;
        deadline = GetTickCount64() + 5000;
        EnableWindow(filter, FALSE);
        EnableWindow(list, FALSE);
        SetWindowTextW(status, L"あふｗを移動中…");
        if (!SetTimer(window, APPLY_TIMER, 100, nullptr)) { throw Failure{L"移動処理のタイマーを作成できません。"}; }
    }

    void activateAfx()
    {
        struct TargetWindow { DWORD pid; HWND handle; } found{afx.pid, nullptr};
        EnumWindows([](HWND candidate, LPARAM data) -> BOOL
        {
            auto& found = *reinterpret_cast<TargetWindow*>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            if (pid == found.pid && IsWindowVisible(candidate) && GetWindow(candidate, GW_OWNER) == nullptr)
            {
                found.handle = candidate;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&found));
        if (!found.handle) { throw Failure{L"あふｗのウィンドウを確認できません。"}; }
        if (IsIconic(found.handle)) { ShowWindow(found.handle, SW_RESTORE); }
        AllowSetForegroundWindow(afx.pid);
        if (!SetForegroundWindow(found.handle)) { throw Failure{L"移動は完了しましたが、あふｗをアクティブにできません。"}; }
    }

    void applyNext()
    {
        if (!commandSent)
        {
            afx.validate();
            if (afx.exec(command)) { commandSent = true; deadline = GetTickCount64() + 5000; }
        }
        else
        {
            if (runningAfx() != afx.pid || afx.currentSide() != afx.side)
            {
                throw Failure{L"あふｗの操作対象が変化しました。"};
            }
            const fs::path current = afx.extract(afx.side == 0 ? L"$L" : L"$R");
            std::error_code error;
            if (fs::equivalent(current, target, error) && !error)
            {
                KillTimer(window, APPLY_TIMER);
                activateAfx();
                DestroyWindow(window);
                return;
            }
        }
        if (GetTickCount64() >= deadline) { throw Failure{L"あふｗの移動完了を確認できません。\n" + target.wstring()}; }
    }

    void layout()
    {
        RECT rc{};
        GetClientRect(window, &rc);
        MoveWindow(filter, 20, 20, std::max(10L, rc.right - 40), 30, TRUE);
        MoveWindow(list, 20, 66, std::max(10L, rc.right - 40), std::max(10L, rc.bottom - 118), TRUE);
        MoveWindow(status, 20, rc.bottom - 36, std::max(10L, rc.right - 40), 28, TRUE);
        ListView_SetColumnWidth(list, 0, std::max(80L, rc.right - 64));
    }
};
LRESULT CALLBACK childProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR data)
{
    auto& app = *reinterpret_cast<App*>(data);
    if (message == WM_NOTIFY && window == app.list)
    {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == ListView_GetHeader(app.list) && header->code == NM_CUSTOMDRAW)
        {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT)
            {
                RECT bounds{};
                GetClientRect(header->hwndFrom, &bounds);
                FillRect(draw->hdc, &bounds, app.surfaceBrush);
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                FillRect(draw->hdc, &draw->rc, app.surfaceBrush);
                wchar_t text[64]{};
                HDITEMW item{};
                item.mask = HDI_TEXT;
                item.pszText = text;
                item.cchTextMax = 64;
                Header_GetItem(header->hwndFrom, static_cast<int>(draw->dwItemSpec), &item);
                SetTextColor(draw->hdc, MUTED);
                SetBkMode(draw->hdc, TRANSPARENT);
                RECT rect = draw->rc;
                rect.left += 10;
                DrawTextW(draw->hdc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    if (message == WM_GETDLGCODE) { return DLGC_WANTALLKEYS; }
    // Space は項目操作ではなく Filter の空白入力として扱う。
    if (window == app.list && message == WM_KEYDOWN && wParam == VK_SPACE) { return 0; }
    if (message == WM_CHAR && window == app.list)
    {
        if (wParam == L' ') { SendMessageW(app.filter, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" ")); return 0; }
        if (wParam >= 32 && wParam != 127)
        {
            SetFocus(app.filter);
            SendMessageW(app.filter, WM_CHAR, wParam, lParam);
            return 0;
        }
        if (wParam == L'\b') { SetFocus(app.filter); SendMessageW(app.filter, message, wParam, lParam); return 0; }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) { return DefWindowProcW(window, message, wParam, lParam); }
    try
    {
        switch (message)
        {
        case WM_CREATE:
        {
            app->font = CreateFontW(-MulDiv(10, static_cast<int>(GetDpiForWindow(window)), 72), 0, 0, 0,
                FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            app->filter = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(FILTER_ID)), nullptr, nullptr);
            SendMessageW(app->filter, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter — 空白区切りで AND 検索"));
            app->list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(LIST_ID)), nullptr, nullptr);
            app->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            for (HWND control : {app->filter, app->list, app->status})
            {
                if (!control) { throw Failure{L"ウィンドウ部品を作成できません。"}; }
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app->font), TRUE);
            }
            ListView_SetExtendedListViewStyle(app->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            app->styleControls();
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.cx = 500;
            column.pszText = const_cast<wchar_t*>(L"名前");
            ListView_InsertColumn(app->list, 0, &column);
            SetWindowSubclass(app->filter, childProc, 1, reinterpret_cast<DWORD_PTR>(app));
            SetWindowSubclass(app->list, childProc, 1, reinterpret_cast<DWORD_PTR>(app));
            app->layout();
            app->rebuild();
            return 0;
        }
        case WM_SIZE:
            if (wParam == SIZE_MAXIMIZED) { app->maximized = true; }
            else if (wParam == SIZE_RESTORED) { app->maximized = false; }
            if (app->list && wParam != SIZE_MINIMIZED) { app->layout(); }
            return 0;
        case WM_ERASEBKGND:
        {
            RECT rect{};
            GetClientRect(window, &rect);
            FillRect(reinterpret_cast<HDC>(wParam), &rect, app->backgroundBrush);
            return 1;
        }
        case WM_CTLCOLOREDIT:
            SetTextColor(reinterpret_cast<HDC>(wParam), FOREGROUND);
            SetBkColor(reinterpret_cast<HDC>(wParam), SURFACE);
            return reinterpret_cast<LRESULT>(app->surfaceBrush);
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(wParam), MUTED);
            SetBkColor(reinterpret_cast<HDC>(wParam), BACKGROUND);
            return reinterpret_cast<LRESULT>(app->backgroundBrush);
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {600, 350}; return 0;
        case WM_COMMAND:
            if (!app->applying && LOWORD(wParam) == FILTER_ID && HIWORD(wParam) == EN_CHANGE) { app->rebuild(); }
            return 0;
        case WM_NOTIFY:
        {
            const auto* header = reinterpret_cast<NMHDR*>(lParam);
            if (header->hwndFrom == app->list && header->code == NM_DBLCLK && !app->applying)
            {
                const auto* click = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (click->iItem >= 0 && static_cast<size_t>(click->iItem) < app->visible.size())
                {
                    ListView_SetItemState(app->list, -1, 0, LVIS_FOCUSED | LVIS_SELECTED);
                    ListView_SetItemState(app->list, click->iItem, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
                    app->beginMove();
                }
                return 0;
            }
            if (header->code == NM_CUSTOMDRAW && header->hwndFrom == ListView_GetHeader(app->list))
            {
                auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
                if (draw->dwDrawStage == CDDS_PREPAINT) { return CDRF_NOTIFYITEMDRAW; }
                if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    FillRect(draw->hdc, &draw->rc, app->surfaceBrush);
                    wchar_t text[64]{};
                    HDITEMW item{};
                    item.mask = HDI_TEXT;
                    item.pszText = text;
                    item.cchTextMax = 64;
                    Header_GetItem(header->hwndFrom, static_cast<int>(draw->dwItemSpec), &item);
                    SetTextColor(draw->hdc, MUTED);
                    SetBkMode(draw->hdc, TRANSPARENT);
                    RECT rect = draw->rc;
                    rect.left += 10;
                    DrawTextW(draw->hdc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    return CDRF_SKIPDEFAULT;
                }
            }
            if (header->code == NM_CUSTOMDRAW && header->hwndFrom == app->list)
            {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) { return CDRF_NOTIFYITEMDRAW; }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    const bool focused = (ListView_GetItemState(app->list, static_cast<int>(draw->nmcd.dwItemSpec), LVIS_SELECTED) != 0);
                    draw->clrText = FOREGROUND;
                    draw->clrTextBk = focused ? HIGHLIGHT : SURFACE;
                    draw->nmcd.uItemState &= ~CDIS_SELECTED;
                    return CDRF_NEWFONT;
                }
            }
            return 0;
        }
        case WM_TIMER: if (wParam == APPLY_TIMER) { app->applyNext(); } return 0;
        case WM_CLOSE: if (!app->applying) { DestroyWindow(window); } return 0;
        case WM_DESTROY: app->saveWindowSettings(); PostQuitMessage(0); return 0;
        }
    }
    catch (const Failure& error)
    {
        KillTimer(window, APPLY_TIMER);
        MessageBoxW(window, error.message.c_str(), L"fdList — エラー", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        return message == WM_CREATE ? -1 : 0;
    }
    catch (const std::exception& error)
    {
        const std::string what = error.what();
        const int count = MultiByteToWideChar(CP_UTF8, 0, what.c_str(), -1, nullptr, 0);
        std::wstring detail(count > 0 ? count : 1, L'\0');
        if (count > 0) { MultiByteToWideChar(CP_UTF8, 0, what.c_str(), -1, detail.data(), count); }
        MessageBoxW(window, (L"処理に失敗しました。\n" + app->afx.path.wstring() + L"\n" + detail).c_str(), L"fdList — エラー", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        return message == WM_CREATE ? -1 : 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool handleKey(App& app, const MSG& message)
{
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) { return false; }
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const auto key = message.wParam;
    if (key == VK_ESCAPE) { DestroyWindow(app.window); return true; }
    if (app.applying) { return true; }
    if (key == VK_RETURN && !control && !shift && !alt)
    {
        app.beginMove(); return true;
    }
    if (GetFocus() == app.filter && control && !alt && key == 'A')
    {
        SendMessageW(app.filter, EM_SETSEL, 0, -1); return true;
    }
    if ((message.hwnd == app.filter || message.hwnd == app.list) &&
        (key == VK_DOWN || key == VK_UP) && !alt && !control && !shift)
    {
        app.moveActive(key == VK_DOWN ? 1 : -1); return true;
    }
    return false;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
    {
        MessageBoxW(nullptr, L"COMを初期化できません。", L"fdList — エラー", MB_OK | MB_ICONERROR);
        return 1;
    }
    int result = 0;
    {
        App app;
        try
        {
            const WindowSettings settings = loadWindowSettings();
            MONITORINFO monitor{sizeof(monitor)};
            if (!GetMonitorInfoW(MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY), &monitor))
            {
                throw Failure{L"画面の作業領域を取得できません。"};
            }
            const RECT bounds = centeredWindow(settings, monitor.rcWork);
            app.load();
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
            if (!InitCommonControlsEx(&controls)) { throw Failure{L"共通コントロールを初期化できません。"}; }
            WNDCLASSW type{};
            type.lpfnWndProc = windowProc;
            type.hInstance = instance;
            type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            type.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
            if (!type.hIcon) { throw Failure{L"fdListのアプリアイコンを読み込めません。"}; }
            type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            type.lpszClassName = L"fdListWindow";
            if (!RegisterClassW(&type)) { throw Failure{L"ウィンドウクラスを登録できません。"}; }
            const auto title = L"fdList — " + app.afx.path.wstring();
            HWND window = CreateWindowExW(WS_EX_TOPMOST, type.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                nullptr, nullptr, instance, &app);
            if (!window) { throw Failure{L"fdListのウィンドウを作成できません。"}; }
            app.rememberWindow = true;
            ShowWindow(window, settings.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
            SetFocus(app.filter);
            MSG message{};
            BOOL status;
            while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0)
            {
                if (!handleKey(app, message) && !IsDialogMessageW(window, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            if (status == -1) { throw Failure{L"ウィンドウメッセージを取得できません。"}; }
        }
        catch (const Failure& error)
        {
            MessageBoxW(app.window, error.message.c_str(), L"fdList — エラー", MB_OK | MB_ICONERROR);
            result = 1;
        }
        catch (const std::exception&)
        {
            MessageBoxW(app.window, (L"フォルダの読み込みまたは処理に失敗しました。\n" + app.afx.path.wstring()).c_str(), L"fdList — エラー", MB_OK | MB_ICONERROR);
            result = 1;
        }
        if (IsWindow(app.window)) { DestroyWindow(app.window); }
    }
    CoUninitialize();
    return result;
}
