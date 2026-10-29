#include "Panel.hpp"
#include <future>

namespace rv
{
constexpr UINT ToggleMessage = WM_APP + 7, StopMessage = WM_APP + 8;
constexpr int Microphone = 101, Apply = 102, Refresh = 103, Diagnose = 104, Repair = 105,
              Auto = 106, Status = 107;
static constexpr wchar_t PanelClass[] = L"RVVoiceFix.Panel.020";

bool Panel::start(HWND owner)
{
    if (thread_.joinable())
    {
        return window_.load() != nullptr;
    }
    owner_ = owner;
    std::promise<bool> ready;
    auto result = ready.get_future();
    thread_ = std::thread(
        [this, ready = std::move(ready)]() mutable
        {
            // Force queue creation before start returns; the window is hidden initially.
            WNDCLASSEXW cls{sizeof(cls)};
            cls.lpfnWndProc = procedure;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            cls.lpszClassName = PanelClass;
            if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                ready.set_value(false);
                return;
            }
            RECT bounds{0, 0, 640, 274};
            AdjustWindowRectEx(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                               FALSE, WS_EX_TOOLWINDOW);
            auto window =
                CreateWindowExW(WS_EX_TOOLWINDOW, PanelClass, L"RV Voice Fix - test controls (F7)",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                bounds.bottom - bounds.top, owner_, nullptr, cls.hInstance, this);
            ready.set_value(window != nullptr);
            if (window)
            {
                run();
            }
            UnregisterClassW(PanelClass, cls.hInstance);
        });
    if (!result.get())
    {
        thread_.join();
        return false;
    }
    return true;
}

void Panel::stop()
{
    if (!thread_.joinable())
    {
        return;
    }
    if (auto window = window_.load())
    {
        PostMessageW(window, StopMessage, 0, 0);
    }
    thread_.join();
}

void Panel::toggle()
{
    if (auto window = window_.load())
    {
        PostMessageW(window, ToggleMessage, 0, 0);
    }
}

void Panel::status(std::wstring message)
{
    std::lock_guard lock(mutex_);
    status_ = std::move(message);
}

std::vector<Command> Panel::takeCommands()
{
    std::lock_guard lock(mutex_);
    std::vector<Command> result;
    result.swap(commands_);
    return result;
}

void Panel::refresh()
{
    GUID previous{};
    auto index = SendMessageW(combo_, CB_GETCURSEL, 0, 0);
    if (index >= 0 && size_t(index) < devices_.size())
    {
        previous = devices_[index].id;
    }
    devices_ = enumerateDevices();
    devices_.insert(devices_.begin(),
                    Device{GUID{}, L"Windows default (resolved when you click Apply)"});
    SendMessageW(combo_, CB_RESETCONTENT, 0, 0);
    size_t selected = 0;
    for (size_t i = 0; i < devices_.size(); ++i)
    {
        SendMessageW(combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(devices_[i].name.c_str()));
        if (IsEqualGUID(previous, devices_[i].id))
        {
            selected = i;
        }
    }
    SendMessageW(combo_, CB_SETCURSEL, selected, 0);
    SendMessageW(combo_, CB_SETDROPPEDWIDTH, 600, 0);
}

void Panel::command(int id)
{
    if (id == Refresh)
    {
        refresh();
        status(L"Device list refreshed. Select a microphone, then click Apply.");
        return;
    }
    Command request{};
    switch (id)
    {
        case Apply:
        {
            auto index = SendMessageW(combo_, CB_GETCURSEL, 0, 0);
            if (index < 0 || size_t(index) >= devices_.size())
            {
                return;
            }
            request = {Action::Apply, devices_[index]};
            break;
        }
        case Diagnose:
            request.action = Action::Diagnose;
            break;
        case Repair:
            request.action = Action::Repair;
            break;
        case Auto:
            request.action = Action::ToggleAuto;
            break;
        default:
            return;
    }
    std::lock_guard lock(mutex_);
    // Bound pending input when the game thread is paused or loading.
    if (commands_.size() < 16)
    {
        commands_.push_back(std::move(request));
        status_ = L"Queued; waiting for the game thread.";
    }
}

LRESULT CALLBACK Panel::procedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto self = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_.store(window);
    }
    if (!self)
    {
        return DefWindowProcW(window, message, w, l);
    }
    switch (message)
    {
        case WM_CREATE:
        {
            auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y,
                               int width, int height, int id)
            {
                auto child = CreateWindowExW(
                    0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window,
                    reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
                SendMessageW(child, WM_SETFONT,
                             reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
                return child;
            };
            control(L"STATIC", L"Microphone input", 0, 16, 16, 600, 20, 0);
            self->combo_ = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16,
                                   42, 608, 240, Microphone);
            control(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, 16, 80, 96, 30, Apply);
            control(L"BUTTON", L"Refresh", WS_TABSTOP, 122, 80, 96, 30, Refresh);
            control(L"BUTTON", L"Diagnose (F8)", WS_TABSTOP, 16, 126, 136, 30, Diagnose);
            control(L"BUTTON", L"Repair (F9)", WS_TABSTOP, 162, 126, 136, 30, Repair);
            control(L"BUTTON", L"Toggle auto (F10)", WS_TABSTOP, 308, 126, 150, 30, Auto);
            self->statusLabel_ = control(L"STATIC", L"", 0, 16, 174, 608, 52, Status);
            control(L"STATIC",
                    L"F7 or Esc closes this window. Use windowed or borderless display mode.", 0,
                    16, 240, 608, 20, 0);
            self->refresh();
            SetTimer(window, 1, 200, nullptr);
            return 0;
        }
        case WM_TIMER:
        {
            std::wstring status;
            {
                std::lock_guard lock(self->mutex_);
                status = self->status_;
            }
            SetWindowTextW(self->statusLabel_, status.c_str());
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(w) == BN_CLICKED)
            {
                self->command(LOWORD(w));
            }
            return 0;
        case ToggleMessage:
            if (!IsWindowVisible(window))
            {
                ShowWindow(window, SW_SHOWNORMAL);
                SetForegroundWindow(window);
                SetFocus(self->combo_);
                return 0;
            }
            [[fallthrough]];
        case WM_CLOSE:
            ShowWindow(window, SW_HIDE);
            if (self->owner_)
            {
                SetForegroundWindow(self->owner_);
            }
            return 0;
        case StopMessage:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            self->window_.store(nullptr);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

void Panel::run()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE || msg.wParam == VK_F7))
        {
            PostMessageW(window_.load(), WM_CLOSE, 0, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam >= VK_F8 && msg.wParam <= VK_F10)
        {
            command(msg.wParam == VK_F8 ? Diagnose : msg.wParam == VK_F9 ? Repair : Auto);
            continue;
        }
        if (!IsDialogMessageW(window_.load(), &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
} // namespace rv
