#include "../src/Panel.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

static unsigned checks = 0;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}
template<class Predicate> static bool eventually(Predicate predicate) {
    auto deadline = GetTickCount64() + 2000;
    while (!predicate() && GetTickCount64() < deadline) Sleep(5);
    return predicate();
}
int main(int argc, char**) {
    try {
        rv::Panel panel;
        check(panel.start(nullptr), "Panel creation failed");
        auto window = FindWindowW(L"RVVoiceFix.Panel.020", nullptr);
        check(window && !IsWindowVisible(window), "Panel must start hidden");
        if (argc > 1 || rv::modulePath(nullptr).stem() == L"panel-preview") {
            panel.toggle();
            std::cout << "Panel preview active for 45 seconds. No game or microphone capture is accessed.\n";
            Sleep(45000); return 0;
        }
        auto combo = GetDlgItem(window, 101);
        auto count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
        check(count == LRESULT(rv::enumerateDevices().size() + 1), "Dropdown does not match real Windows device enumeration");
        SendMessageW(GetDlgItem(window, 102), BM_CLICK, 0, 0);
        auto commands = panel.takeCommands();
        check(commands.size() == 1 && commands[0].action == rv::Action::Apply
            && IsEqualGUID(commands[0].device.id, GUID{}), "Default Apply did not queue the default-device request");
        if (count > 1) {
            SendMessageW(combo, CB_SETCURSEL, 1, 0);
            SendMessageW(GetDlgItem(window, 102), BM_CLICK, 0, 0);
            commands = panel.takeCommands();
            check(commands.size() == 1 && !IsEqualGUID(commands[0].device.id, GUID{}), "Named device was lost in the UI queue");
            auto selected = commands[0].device.id;
            SendMessageW(GetDlgItem(window, 103), BM_CLICK, 0, 0);
            SendMessageW(GetDlgItem(window, 102), BM_CLICK, 0, 0);
            commands = panel.takeCommands();
            check(commands.size() == 1 && IsEqualGUID(commands[0].device.id, selected), "Refresh lost the selected endpoint");
        }
        for (int id : {104, 105, 106}) SendMessageW(GetDlgItem(window, id), BM_CLICK, 0, 0);
        commands = panel.takeCommands();
        check(commands.size() == 3 && commands[0].action == rv::Action::Diagnose
            && commands[1].action == rv::Action::Repair && commands[2].action == rv::Action::ToggleAuto, "Control action order changed");
        for (int i = 0; i < 20; ++i) SendMessageW(GetDlgItem(window, 105), BM_CLICK, 0, 0);
        check(panel.takeCommands().size() == 16 && panel.takeCommands().empty(), "Paused-game queue must be bounded and drained once");
        panel.toggle();
        check(eventually([&] { return IsWindowVisible(window); }), "F7 show failed");
        SendMessageW(window, WM_CLOSE, 0, 0);
        check(!IsWindowVisible(window), "Close must hide the panel");
        panel.stop(); panel.stop();
        check(!IsWindow(window), "Panel stop left a window alive");
        check(panel.start(nullptr), "Panel could not restart"); panel.stop();
        rv::Settler scheduler;
        scheduler.observe({1, 2, 3}, 100);
        check(!scheduler.ready(3099) && scheduler.ready(3100), "Repair must wait for players to settle");
        check(scheduler.ready(6600) && scheduler.ready(10100) && !scheduler.ready(20000), "Repair burst must stop after three attempts");
        scheduler.observe({1, 2, 4}, 21000); scheduler.toggle();
        check(!scheduler.ready(30000), "Disabling automatic repair must cancel pending attempts");
        scheduler.schedule(30000);
        check(scheduler.ready(33000), "Manual repair must work with automatic repair disabled");
        scheduler.observe({}, 34000);
        check(!scheduler.ready(40000), "Leaving the world must cancel obsolete repairs");
        check(!rv::CaptureControl{}.apply({}).ok, "Microphone operation bypassed the executable gate");
        {
            std::ofstream file("hash-fixture.bin", std::ios::binary); file << "abc";
        }
        check(rv::sha256(L"hash-fixture.bin") == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 compatibility gate produced an incorrect digest");
        std::filesystem::remove(L"hash-fixture.bin");
        std::cout << "PASS: " << checks << " native checks; " << count - 1 << " real capture devices enumerated.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
