#define wWinMain fdListApplicationMain
#include "../fdList.cpp"
#undef wWinMain
#include <iostream>
#include <fstream>

void require(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

int main()
{
    const fs::path fixture = fs::temp_directory_path() / (L"fdList-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try
    {
        const RECT work{-1920, 0, 0, 1040};
        const RECT centered = centeredWindow(WindowSettings{1000, 600, FALSE}, work);
        require(centered.left == -1460 && centered.top == 220 && centered.right == -460 && centered.bottom == 820,
            "saved size centered on negative-coordinate monitor");
        const RECT clamped = centeredWindow(WindowSettings{4000, 3000, TRUE}, work);
        require(EqualRect(&clamped, &work) != FALSE, "oversized saved window fits work area");
        fs::create_directories(fixture / L"Alpha report");
        fs::create_directories(fixture / L"Alpha Folder");
        fs::create_directories(fixture / L"日本語 フォルダ");
        std::ofstream(fixture / L"excluded.txt") << "file";
        App app;
        app.afx.path = fixture;
        app.loadDirectories();
        require(app.entries.size() == 3, "directories only");
        for (const auto& entry : app.entries) { require(entry.name != L"excluded.txt", "files excluded"); }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        require(InitCommonControlsEx(&controls) != FALSE, "common controls");
        WNDCLASSW type{};
        type.lpfnWndProc = windowProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"fdListTests";
        require(RegisterClassW(&type) != 0, "register class");
        HWND window = CreateWindowExW(0, type.lpszClassName, L"fdList tests", WS_OVERLAPPEDWINDOW,
            0, 0, 850, 620, nullptr, nullptr, type.hInstance, &app);
        require(window != nullptr, "create controls");
        require(app.visible.size() == 3, "directory list");
        require(!(ListView_GetExtendedListViewStyle(app.list) & LVS_EX_CHECKBOXES), "no checkboxes");
        require(GetDlgItem(window, 200) == nullptr, "no display switches");
        const HWND navigationFocus = GetFocus();
        const auto navigationText = windowText(app.filter);
        MSG navigation{};
        navigation.message = WM_KEYDOWN;
        navigation.hwnd = app.filter;
        navigation.wParam = VK_DOWN;
        require(handleKey(app, navigation), "Down handled from Filter");
        require(ListView_GetNextItem(app.list, -1, LVNI_FOCUSED) == 1, "Down moves active row immediately");
        require(GetFocus() == navigationFocus && windowText(app.filter) == navigationText, "navigation preserves focus and Filter text");
        navigation.wParam = VK_UP;
        require(handleKey(app, navigation), "Up handled from Filter");
        require(ListView_GetNextItem(app.list, -1, LVNI_FOCUSED) == 0, "Up moves active row");
        handleKey(app, navigation);
        require(ListView_GetNextItem(app.list, -1, LVNI_FOCUSED) == 0, "Up clamps to first row");
        app.moveActive(100);
        require(ListView_GetNextItem(app.list, -1, LVNI_FOCUSED) == 2, "Down clamps to last row");
        app.moveActive(-100);
        SetWindowTextW(app.filter, L"ALPHA report");
        require(app.visible.size() == 1 && app.entries[app.visible[0]].name == L"Alpha report", "case insensitive AND");
        SetWindowTextW(app.filter, L"日本語 フォルダ");
        require(app.visible.size() == 1 && app.entries[app.visible[0]].name == L"日本語 フォルダ", "Unicode AND");
        SetWindowTextW(app.filter, L"no match");
        require(app.visible.empty(), "empty results");
        app.moveActive(1);
        require(ListView_GetNextItem(app.list, -1, LVNI_FOCUSED) == -1, "navigation on empty list");
        app.beginMove();
        require(!app.applying, "Enter on empty results does nothing");
        SetWindowTextW(app.filter, L"Alpha");
        SendMessageW(app.filter, EM_SETSEL, 5, 5);
        const HWND focusBefore = GetFocus();
        SendMessageW(app.list, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(app.list, WM_CHAR, L' ', 0);
        require(windowText(app.filter) == L"Alpha " && GetFocus() == focusBefore, "Space input preserves focus");
        require(commandLiteral(L"$P folder") == L"$$P folder", "macro escape");
        BYTE originalKeys[256]{};
        GetKeyboardState(originalKeys);
        BYTE keys[256]{};
        keys[VK_MENU] = keys[VK_LMENU] = 0x80;
        SetKeyboardState(keys);
        MSG shortcut{};
        shortcut.message = WM_SYSKEYDOWN;
        shortcut.hwnd = app.filter;
        for (const WPARAM key : {WPARAM('G'), WPARAM('F'), WPARAM('D'), WPARAM('A'), WPARAM('X'), WPARAM(VK_DELETE), WPARAM(VK_RETURN)})
        {
            shortcut.wParam = key;
            require(!handleKey(app, shortcut), "old Alt shortcuts removed");
        }
        keys[VK_MENU] = keys[VK_LMENU] = 0;
        keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
        SetKeyboardState(keys);
        shortcut.message = WM_KEYDOWN;
        for (const WPARAM key : {WPARAM('A'), WPARAM('M'), WPARAM(VK_DELETE)})
        {
            shortcut.wParam = key;
            require(!handleKey(app, shortcut), "Shift input preserved");
        }
        SetKeyboardState(originalKeys);
        shortcut.wParam = VK_ESCAPE;
        require(handleKey(app, shortcut) && !IsWindow(window), "Escape exits");
        fs::remove_all(fixture);
        std::cout << "PASS: directories only, filtering, no checks or switches, removed shortcuts, Space, Escape\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::error_code cleanup;
        fs::remove_all(fixture, cleanup);
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
