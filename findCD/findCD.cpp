#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "resource.h"

namespace fs = std::filesystem;
constexpr wchar_t SETTINGS_KEY[] = L"Software\\bry-ful\\findCD";
constexpr COLORREF BACKGROUND = RGB(12, 14, 18);
constexpr COLORREF SURFACE = RGB(25, 28, 34);
constexpr COLORREF BORDER = RGB(49, 55, 65);
constexpr COLORREF FOREGROUND = RGB(233, 237, 243);
constexpr COLORREF ACCENT = RGB(115, 202, 237);
constexpr COLORREF SELECTION = RGB(31, 58, 72);

struct App
{
    fs::path root;
    fs::path afxw;
    wchar_t side = L'l';
    std::vector<fs::path> found;
    bool results = false;
    bool activeSideAcquired = false;
    int inputWidth = 0;
    int inputHeight = 0;
    HBRUSH backgroundBrush = CreateSolidBrush(BACKGROUND);
    HBRUSH surfaceBrush = CreateSolidBrush(SURFACE);

    ~App()
    {
        if (backgroundBrush) DeleteObject(backgroundBrush);
        if (surfaceBrush) DeleteObject(surfaceBrush);
    }
};

void drawControl(const DRAWITEMSTRUCT& item, const App& app)
{
    const bool isList = item.CtlID == IDC_RESULTS;
    const bool selected = isList ? (item.itemState & ODS_SELECTED) != 0 :
        (item.CtlID == IDC_LEFT ? app.side == L'l' : app.side == L'r');
    RECT rect = item.rcItem;
    SetDCBrushColor(item.hDC, selected ? SELECTION : (isList ? BACKGROUND : SURFACE));
    FillRect(item.hDC, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, selected ? ACCENT : FOREGROUND);
    const HGDIOBJ oldFont = SelectObject(item.hDC,
        reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
    if (isList && item.itemID != static_cast<UINT>(-1) && item.itemID < app.found.size())
    {
        const int padding = MulDiv(10, GetDpiForWindow(item.hwndItem), 96);
        if (selected)
        {
            RECT marker = rect;
            marker.right = marker.left + MulDiv(3, GetDpiForWindow(item.hwndItem), 96);
            SetDCBrushColor(item.hDC, ACCENT);
            FillRect(item.hDC, &marker, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        rect.left += padding - GetScrollPos(item.hwndItem, SB_HORZ);
        rect.right = std::max(rect.right, rect.left + static_cast<int>(SendMessageW(item.hwndItem, LB_GETHORIZONTALEXTENT, 0, 0)));
        const std::wstring text = std::to_wstring((item.itemID + 1) % 10) + L"   " +
            app.found[item.itemID].filename().wstring();
        DrawTextW(item.hDC, text.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    else if (!isList)
    {
        DrawTextW(item.hDC, item.CtlID == IDC_LEFT ? L"L" : L"R", -1, &rect,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        if (item.itemState & ODS_FOCUS)
        {
            InflateRect(&rect, -3, -3);
            DrawFocusRect(item.hDC, &rect);
        }
    }
    SelectObject(item.hDC, oldFont);
}

std::wstring lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c)
    {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

void centerWindow(HWND window)
{
    MONITORINFO monitor{ sizeof(MONITORINFO) };
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    RECT rect{};
    GetWindowRect(window, &rect);
    const RECT& screen = monitor.rcMonitor;
    SetWindowPos(window, nullptr,
        screen.left + (screen.right - screen.left - (rect.right - rect.left)) / 2,
        screen.top + (screen.bottom - screen.top - (rect.bottom - rect.top)) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK centerHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_ACTIVATE)
    {
        centerWindow(reinterpret_cast<HWND>(wParam));
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void showError(HWND owner, const std::wstring& text)
{
    MessageBoxW(owner, text.c_str(), L"findCD", MB_OK | MB_ICONERROR);
}

std::wstring loadSetting(const wchar_t* name)
{
    std::wstring value(32768, L'\0');
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, name, RRF_RT_REG_SZ,
        nullptr, value.data(), &bytes) != ERROR_SUCCESS)
    {
        return {};
    }
    value.resize(wcsnlen_s(value.data(), value.size()));
    return value;
}

bool saveSetting(HWND owner, const wchar_t* name, const std::wstring& value)
{
    const LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, SETTINGS_KEY, name, REG_SZ,
        value.c_str(), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS)
    {
        showError(owner, L"設定を保存できません。\nHKEY_CURRENT_USER\\" +
            std::wstring(SETTINGS_KEY) + L"\nError = " + std::to_wstring(status));
        return false;
    }
    return true;
}

bool isFile(const fs::path& path)
{
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

HRESULT tryGetActiveSide(wchar_t& side)
{
    // あふが終了している場合、COM接続のためだけに新規起動しない。
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, L"AFXW.EXE") == 0)
            {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!running)
    {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
    {
        return initialized;
    }
    const HRESULT status = [&]() -> HRESULT
    {
        CLSID classId{};
        HRESULT hr = CLSIDFromProgID(L"afxw.obj", &classId);
        if (FAILED(hr))
        {
            return hr;
        }
        Microsoft::WRL::ComPtr<IDispatch> object;
        hr = CoCreateInstance(classId, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(object.GetAddressOf()));
        if (FAILED(hr))
        {
            return hr;
        }
        wchar_t methodName[] = L"SrcWin";
        LPOLESTR name = methodName;
        DISPID methodId = DISPID_UNKNOWN;
        hr = object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &methodId);
        if (FAILED(hr))
        {
            return hr;
        }
        DISPPARAMS arguments{};
        VARIANT value;
        VariantInit(&value);
        hr = object->Invoke(methodId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
            &arguments, &value, nullptr, nullptr);
        if (SUCCEEDED(hr))
        {
            hr = VariantChangeType(&value, &value, 0, VT_I4);
            if (SUCCEEDED(hr))
            {
                if (value.lVal == 0 || value.lVal == 1)
                {
                    side = value.lVal == 1 ? L'r' : L'l';
                }
                else
                {
                    hr = E_UNEXPECTED;
                }
            }
        }
        VariantClear(&value);
        return hr;
    }();
    CoUninitialize();
    return status;
}

bool resolveAfxw(HWND owner, App& app)
{
    if (!app.afxw.empty())
    {
        if (!isFile(app.afxw))
        {
            showError(owner, L"指定されたAFXW.EXEが見つかりません。\n" + app.afxw.wstring());
            return false;
        }
        return saveSetting(owner, L"AFXW", app.afxw.wstring());
    }
    app.afxw = loadSetting(L"AFXW");
    if (isFile(app.afxw))
    {
        return true;
    }
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size())
    {
        showError(owner, L"findCD.exeのパスを取得できません。");
        return false;
    }
    module.resize(length);
    app.afxw = fs::path(module).parent_path() / L"AFXW.EXE";
    if (!isFile(app.afxw))
    {
        std::wstring selected(32768, L'\0');
        OPENFILENAMEW dialog{ sizeof(OPENFILENAMEW) };
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"AFXW.EXE\0AFXW.EXE\0実行ファイル (*.exe)\0*.exe\0\0";
        dialog.lpstrFile = selected.data();
        dialog.nMaxFile = static_cast<DWORD>(selected.size());
        dialog.lpstrTitle = L"AFXW.EXEを選択してください";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
        if (!GetOpenFileNameW(&dialog))
        {
            const DWORD error = CommDlgExtendedError();
            if (error != 0)
            {
                showError(owner, L"ファイル選択に失敗しました。Error = " + std::to_wstring(error));
            }
            return false;
        }
        app.afxw = selected.c_str();
    }
    return saveSetting(owner, L"AFXW", app.afxw.wstring());
}

bool setSide(HWND window, App& app, wchar_t side)
{
    if (!saveSetting(window, L"Side", std::wstring(1, side)))
    {
        return false;
    }
    app.side = side;
    CheckRadioButton(window, IDC_LEFT, IDC_RIGHT, app.side == L'r' ? IDC_RIGHT : IDC_LEFT);
    InvalidateRect(GetDlgItem(window, IDC_LEFT), nullptr, FALSE);
    InvalidateRect(GetDlgItem(window, IDC_RIGHT), nullptr, FALSE);
    return true;
}

void selectResult(HWND window, App& app, size_t index)
{
    if (index >= app.found.size())
    {
        return;
    }
    // 引用符の直前にある末尾のバックスラッシュは二重化する。
    auto quote = [](const std::wstring& text)
    {
        const auto last = text.find_last_not_of(L'\\');
        const size_t trailing = last == std::wstring::npos ? text.size() : text.size() - last - 1;
        return L"\"" + text + std::wstring(trailing, L'\\') + L"\"";
    };
    const fs::path& target = app.found[index];
    std::wstring command = quote(app.afxw.wstring()) + L" -s -" + app.side + quote(target.wstring());
    STARTUPINFOW startup{ sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.afxw.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, target.c_str(), &startup, &process))
    {
        showError(window, L"あふを起動できません。\n" + app.afxw.wstring() + L"\n移動先: " +
            target.wstring() + L"\nError = " + std::to_wstring(GetLastError()));
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    EndDialog(window, 0);
}

void search(HWND window, App& app)
{
    HWND edit = GetDlgItem(window, IDC_SEARCH);
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit)) + 1, L'\0');
    GetWindowTextW(edit, text.data(), static_cast<int>(text.size()));
    text.resize(wcslen(text.c_str()));
    std::wistringstream input(lower(text));
    std::vector<std::wstring> words;
    for (std::wstring word; input >> word;)
    {
        words.push_back(std::move(word));
    }
    if (words.empty())
    {
        return;
    }
    app.found.clear();
    std::error_code ec;
    fs::directory_iterator it(app.root, ec), end;
    while (!ec && it != end)
    {
        std::error_code entryError;
        if (it->is_directory(entryError))
        {
            const std::wstring name = lower(it->path().filename().wstring());
            if (std::all_of(words.begin(), words.end(), [&](const auto& word)
                { return name.find(word) != std::wstring::npos; }))
            {
                app.found.push_back(it->path());
                if (app.found.size() > 10)
                {
                    MessageBoxW(window, L"多すぎ", L"findCD", MB_OK | MB_ICONINFORMATION);
                    SetFocus(edit);
                    SendMessageW(edit, EM_SETSEL, 0, -1);
                    return;
                }
            }
        }
        else if (entryError && entryError != std::errc::permission_denied)
        {
            showError(window, L"フォルダを確認できません。\n" + it->path().wstring() + L"\n" +
                L"Error = " + std::to_wstring(entryError.value()));
            return;
        }
        it.increment(ec);
    }
    if (ec)
    {
        showError(window, L"検索先を読み取れません。\n" + app.root.wstring() + L"\nError = " +
            std::to_wstring(ec.value()));
        return;
    }
    if (app.found.empty())
    {
        MessageBoxW(window, L"フォルダが見つかりません。", L"findCD", MB_OK | MB_ICONINFORMATION);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return;
    }
    std::sort(app.found.begin(), app.found.end(), [](const auto& a, const auto& b)
    {
        return lower(a.filename().wstring()) < lower(b.filename().wstring());
    });
    HWND list = GetDlgItem(window, IDC_RESULTS);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int extent = 0;
    HDC dc = GetDC(list);
    HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)));
    for (size_t i = 0; i < app.found.size(); ++i)
    {
        const std::wstring label = std::to_wstring((i + 1) % 10) + L"   " + app.found[i].filename().wstring();
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SIZE size{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
        extent = std::max(extent, static_cast<int>(size.cx) + MulDiv(24, GetDpiForWindow(list), 96));
    }
    SelectObject(dc, previous);
    ReleaseDC(list, dc);
    SendMessageW(list, LB_SETHORIZONTALEXTENT, extent, 0);
    ShowWindow(GetDlgItem(window, IDC_SEARCH), SW_HIDE);
    const int rowHeight = static_cast<int>(SendMessageW(list, LB_GETITEMHEIGHT, 0, 0));
    RECT client{}, listRect{};
    GetClientRect(window, &client);
    GetWindowRect(list, &listRect);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&listRect), 2);
    const int height = rowHeight * static_cast<int>(app.found.size()) + GetSystemMetrics(SM_CYHSCROLL) + 4;
    SetWindowPos(list, nullptr, 0, 0, listRect.right - listRect.left, height, SWP_NOMOVE | SWP_NOZORDER);
    RECT spacing{ 0, 0, 6, 6 };
    MapDialogRect(window, &spacing);
    int buttonHeight = 0;
    const int buttonTop = listRect.top + height + spacing.bottom;
    if (!app.activeSideAcquired)
    {
        for (int id : { IDC_LEFT, IDC_RIGHT })
        {
            HWND button = GetDlgItem(window, id);
            RECT rect{};
            GetWindowRect(button, &rect);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect), 2);
            buttonHeight = std::max(buttonHeight, static_cast<int>(rect.bottom - rect.top));
            SetWindowPos(button, nullptr, rect.left, buttonTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
    const int contentHeight = app.activeSideAcquired ? listRect.top + height + spacing.bottom :
        buttonTop + buttonHeight + spacing.bottom;
    SetWindowPos(window, nullptr, 0, 0, app.inputWidth,
        app.inputHeight - client.bottom + contentHeight, SWP_NOMOVE | SWP_NOZORDER);
    app.results = true;
    InvalidateRect(window, nullptr, TRUE);
    SetWindowTextW(window, L"findCD — 選択 (1–9, 0 / Escで終了)");
    ShowWindow(list, SW_SHOW);
    ShowWindow(GetDlgItem(window, IDC_LEFT), app.activeSideAcquired ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(window, IDC_RIGHT), app.activeSideAcquired ? SW_HIDE : SW_SHOW);
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    centerWindow(window);
    SetFocus(list);
}

INT_PTR CALLBACK dialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, DWLP_USER));
    try
    {
        if (message == WM_INITDIALOG)
        {
            app = reinterpret_cast<App*>(lParam);
            SetWindowLongPtrW(window, DWLP_USER, lParam);
            SetPropW(window, L"findCD.App", app);
            HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
            HICON smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
            HICON largeIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
            if (smallIcon && largeIcon)
            {
                SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
                SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
            }
            else
            {
                showError(window, L"アプリアイコンを読み込めません。Error = " + std::to_wstring(GetLastError()));
            }
            if (!app->backgroundBrush || !app->surfaceBrush)
            {
                throw std::runtime_error("CreateSolidBrush");
            }
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &BACKGROUND, sizeof(BACKGROUND));
            DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &FOREGROUND, sizeof(FOREGROUND));
            HWND list = GetDlgItem(window, IDC_RESULTS);
            HDC dc = GetDC(list);
            HGDIOBJ font = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)));
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc, &metrics);
            SelectObject(dc, font);
            ReleaseDC(list, dc);
            SendMessageW(list, LB_SETITEMHEIGHT, 0, metrics.tmHeight + MulDiv(14, GetDpiForWindow(window), 96));
            SetWindowTextW(GetDlgItem(window, IDC_LEFT), L"左へ移動 (L)");
            SetWindowTextW(GetDlgItem(window, IDC_RIGHT), L"右へ移動 (R)");
            RECT rect{};
            GetWindowRect(window, &rect);
            app->inputWidth = rect.right - rect.left;
            app->inputHeight = rect.bottom - rect.top;
            ShowWindow(GetDlgItem(window, IDC_RESULTS), SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_LEFT), SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_RIGHT), SW_HIDE);
            CheckRadioButton(window, IDC_LEFT, IDC_RIGHT, app->side == L'r' ? IDC_RIGHT : IDC_LEFT);
            centerWindow(window);
            if (!resolveAfxw(window, *app))
            {
                EndDialog(window, 1);
                return TRUE;
            }
            SetFocus(GetDlgItem(window, IDC_SEARCH));
            return FALSE;
        }
        if (!app)
        {
            return FALSE;
        }
        if (message == WM_DESTROY)
        {
            RemovePropW(window, L"findCD.App");
        }
        if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC ||
            message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX)
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            const bool edit = message == WM_CTLCOLOREDIT;
            SetTextColor(dc, FOREGROUND);
            SetBkColor(dc, edit ? SURFACE : BACKGROUND);
            return reinterpret_cast<INT_PTR>(edit ? app->surfaceBrush : app->backgroundBrush);
        }
        if (message == WM_DRAWITEM)
        {
            drawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam), *app);
            return TRUE;
        }
        if (message == WM_MEASUREITEM)
        {
            reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight = MulDiv(32, GetDpiForWindow(window), 96);
            return TRUE;
        }
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            if (!app->results)
            {
                RECT rect{ 6, 6, 294, 23 };
                MapDialogRect(window, &rect);
                SetDCPenColor(dc, BORDER);
                HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
                HGDIOBJ brush = SelectObject(dc, app->surfaceBrush);
                const int radius = MulDiv(10, GetDpiForWindow(window), 96);
                RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
                SelectObject(dc, brush);
                SelectObject(dc, pen);
            }
            EndPaint(window, &paint);
            return TRUE;
        }
        if (message == WM_COMMAND)
        {
            const int id = LOWORD(wParam);
            if (id == IDCANCEL)
            {
                EndDialog(window, 0);
            }
            else if (id == IDC_LEFT || id == IDC_RIGHT)
            {
                if (app->results && HIWORD(wParam) == BN_CLICKED &&
                    setSide(window, *app, id == IDC_LEFT ? L'l' : L'r'))
                {
                    selectResult(window, *app, static_cast<size_t>(
                        SendDlgItemMessageW(window, IDC_RESULTS, LB_GETCURSEL, 0, 0)));
                }
            }
            else if (id == IDOK)
            {
                if (app->results)
                {
                    selectResult(window, *app, static_cast<size_t>(SendDlgItemMessageW(window, IDC_RESULTS, LB_GETCURSEL, 0, 0)));
                }
                else
                {
                    search(window, *app);
                }
            }
            return TRUE;
        }
        if (message == WM_CLOSE)
        {
            EndDialog(window, 0);
            return TRUE;
        }
    }
    catch (const std::exception&)
    {
        showError(window, L"処理に失敗しました。\n検索先: " + (app ? app->root.wstring() : L""));
    }
    return FALSE;
}

// ダイアログに届く前に、キーとリストのクリックを処理する。
LRESULT CALLBACK messageHook(int code, WPARAM wParam, LPARAM lParam)
{
    try
    {
    if (code == HC_ACTION && wParam == PM_REMOVE)
    {
        MSG& message = *reinterpret_cast<MSG*>(lParam);
        HWND dialog = GetAncestor(message.hwnd, GA_ROOT);
        App* app = reinterpret_cast<App*>(GetPropW(dialog, L"findCD.App"));
        if (app && message.message == WM_KEYDOWN)
        {
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (message.wParam == 'L' || message.wParam == 'R'))
            {
                setSide(dialog, *app, message.wParam == 'R' ? L'r' : L'l');
                message.message = WM_NULL;
            }
            else if (app->results && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000))
            {
                WPARAM key = message.wParam;
                if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
                {
                    key = '0' + key - VK_NUMPAD0;
                }
                if (key == VK_LEFT || key == VK_RIGHT)
                {
                    setSide(dialog, *app, key == VK_RIGHT ? L'r' : L'l');
                    message.message = WM_NULL;
                }
                else if (key >= '0' && key <= '9')
                {
                    selectResult(dialog, *app, key == '0' ? 9 : static_cast<size_t>(key - '1'));
                    message.message = WM_NULL;
                }
            }
        }
        else if (app && app->results && message.hwnd == GetDlgItem(dialog, IDC_RESULTS) &&
            message.message == WM_LBUTTONUP)
        {
            const LRESULT item = SendMessageW(message.hwnd, LB_ITEMFROMPOINT, 0, message.lParam);
            if (HIWORD(item) == 0)
            {
                selectResult(dialog, *app, LOWORD(item));
                message.message = WM_NULL;
            }
        }
    }
    }
    catch (const std::exception&)
    {
        showError(nullptr, L"選択処理に失敗しました。");
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetProcessDPIAware();
    HHOOK centering = SetWindowsHookExW(WH_CBT, centerHook, nullptr, GetCurrentThreadId());
    HHOOK keyboard = SetWindowsHookExW(WH_GETMESSAGE, messageHook, nullptr, GetCurrentThreadId());
    if (!centering || !keyboard)
    {
        showError(nullptr, L"ウィンドウ操作を初期化できません。Error = " + std::to_wstring(GetLastError()));
        if (keyboard) UnhookWindowsHookEx(keyboard);
        if (centering) UnhookWindowsHookEx(centering);
        return 1;
    }
    int result = 1;
    try
    {
        App app;
        app.root = fs::current_path();
        app.side = loadSetting(L"Side") == L"r" ? L'r' : L'l';
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
        {
            throw std::runtime_error("CommandLineToArgvW");
        }
        bool hasPath = false;
        bool valid = true;
        for (int i = 1; i < argc; ++i)
        {
            if (lower(argv[i]) == L"-afxw" && i + 1 < argc)
            {
                app.afxw = fs::absolute(argv[++i]);
            }
            else if (argv[i][0] == L'-' || hasPath)
            {
                valid = false;
                break;
            }
            else
            {
                app.root = fs::absolute(argv[i]);
                hasPath = true;
            }
        }
        LocalFree(argv);
        std::error_code ec;
        if (!valid)
        {
            showError(nullptr, L"使い方: findCD.exe [パス] [-afxw AFXW.EXEのパス]");
        }
        else if (!fs::is_directory(app.root, ec))
        {
            showError(nullptr, L"検索先のフォルダを開けません。\n" + app.root.wstring() +
                L"\nError = " + std::to_wstring(ec.value()));
        }
        else
        {
            // 取得できない場合は、読み込み済みの設定をそのまま使う。
            app.activeSideAcquired = SUCCEEDED(tryGetActiveSide(app.side));
            const INT_PTR dialogResult = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr,
                dialogProc, reinterpret_cast<LPARAM>(&app));
            if (dialogResult == -1)
            {
                showError(nullptr, L"ウィンドウを作成できません。Error = " + std::to_wstring(GetLastError()));
            }
            else
            {
                result = static_cast<int>(dialogResult);
            }
        }
    }
    catch (const std::exception&)
    {
        showError(nullptr, L"起動処理に失敗しました。指定したパスとアクセス権を確認してください。");
    }
    if (keyboard) UnhookWindowsHookEx(keyboard);
    if (centering) UnhookWindowsHookEx(centering);
    return result;
}
