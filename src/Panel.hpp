#pragma once
#include "Devices.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace rv
{
enum class Action
{
    Apply,
    Diagnose,
    Repair,
    ToggleAuto
};

struct Command
{
    Action action;
    Device device{};
};

// The UI owns its windows and device list. Only commands cross to the game thread.
class Panel
{
    std::thread thread_;
    std::atomic<HWND> window_{};
    std::mutex mutex_;
    std::vector<Command> commands_;
    std::wstring status_ = L"Join a session before applying a microphone. Automatic repair: ON.";
    std::vector<Device> devices_; // UI thread only
    HWND combo_{}, statusLabel_{}, owner_{};
    void run();
    void refresh();
    void command(int id);
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM);

public:
    ~Panel()
    {
        stop();
    }

    bool start(HWND owner);
    void stop();
    void toggle();

    bool focused() const
    {
        return window_.load() && GetAncestor(GetForegroundWindow(), GA_ROOT) == window_.load();
    }

    void status(std::wstring message);
    std::vector<Command> takeCommands();
};
} // namespace rv
