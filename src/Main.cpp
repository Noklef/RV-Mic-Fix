#include "Panel.hpp"
#include "Proximity.hpp"
#include <array>

namespace rv
{
// The legacy UE4SS tick hook cannot unregister. Pin this DLL and keep callback
// state alive until process exit. No mod-instance pointer enters the callback.
struct Runtime
{
    std::mutex mutex;
    bool active = false, registered = false;
    CaptureControl capture;
    Panel panel;
    Settler settler;
    uint64_t nextPoll = 0;
    std::array<bool, 4> keys{};
};

static Runtime& runtime()
{
    static auto state = new Runtime;
    return *state;
}

static BOOL CALLBACK findWindow(HWND window, LPARAM result)
{
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER))
    {
        *reinterpret_cast<HWND*>(result) = window;
        return FALSE;
    }
    return TRUE;
}

static void report(Runtime& state, std::wstring message)
{
    log(message);
    state.panel.status(std::move(message));
}

static void perform(Runtime& state, RC::Unreal::UEngine* engine, const Command& command,
                    uint64_t now)
{
    switch (command.action)
    {
        case Action::Apply:
            report(state, state.capture.apply(command.device).message);
            break;
        case Action::Diagnose:
            report(state, repairProximity(engine, true));
            break;
        case Action::Repair:
            state.settler.schedule(now);
            report(state, L"Three manual repair attempts queued (3, 6.5 and 10 seconds).");
            break;
        case Action::ToggleAuto:
            state.settler.toggle();
            report(state, state.settler.automatic
                              ? L"Automatic repair: ON. Watching for world/player changes."
                              : L"Automatic repair: OFF. Pending repair attempts cancelled.");
            break;
    }
}

static void tick(RC::Unreal::UEngine* engine, float)
{
    auto& state = runtime();
    std::lock_guard lock(state.mutex);
    if (!state.active)
    {
        return;
    }
    try
    {
        auto now = GetTickCount64();
        if (now >= state.nextPoll)
        {
            state.nextPoll = now + 1000;
            state.settler.observe(rosterSignature(engine), now);
        }
        DWORD foregroundProcess{};
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        for (size_t i = 0; i < state.keys.size(); ++i)
        {
            bool down = (GetAsyncKeyState(VK_F7 + int(i)) & 0x8000) != 0;
            if (down && !state.keys[i] && foregroundProcess == GetCurrentProcessId() &&
                !state.panel.focused())
            {
                if (i == 0)
                {
                    HWND owner{};
                    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&owner));
                    if (state.panel.start(owner))
                    {
                        state.panel.toggle();
                    }
                    else
                    {
                        log(L"F7 window creation failed.");
                    }
                }
                else
                {
                    perform(state, engine,
                            {i == 1   ? Action::Diagnose
                             : i == 2 ? Action::Repair
                                      : Action::ToggleAuto},
                            now);
                }
            }
            state.keys[i] = down;
        }
        for (auto& command : state.panel.takeCommands())
        {
            perform(state, engine, command, now);
        }
        if (state.settler.ready(now))
        {
            report(state, repairProximity(engine, false));
        }
    }
    catch (const std::exception&)
    {
        state.active = false;
        report(state, L"C++ exception in game-thread work; mod disabled until the game restarts.");
    }
}

class VoiceFix final : public RC::CppUserModBase
{
public:
    VoiceFix()
    {
        ModName = L"RV Voice Fix";
        ModVersion = L"0.2.0-test";
        ModDescription = L"Checkpoint proximity repair and runtime microphone selection";
        ModAuthors = L"RV-Mod-Fix contributors";
        ModIntendedSDKVersion = L"UE4SS cdbc0b66";
    }

    void on_unreal_init() override
    {
        auto& state = runtime();
        std::lock_guard lock(state.mutex);
        if (!state.registered)
        {
            RC::Unreal::Hook::RegisterEngineTickPostCallback(tick);
            state.registered = true;
        }
        state.active = true;
        log(L"Loaded v0.2.0-test. Game-thread callback registered. F7=panel F8=diagnose F9=repair "
            L"F10=auto.");
    }

    ~VoiceFix() override
    {
        auto& state = runtime();
        std::lock_guard lock(state.mutex);
        state.active = false;
        state.panel.stop();
        log(L"Stopped; DLL retained for the legacy tick callback.");
    }
};
} // namespace rv

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    using namespace rv;
    try
    {
        HMODULE self{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&start_mod), &self))
        {
            return nullptr;
        }
        setLogPath(modulePath(self).parent_path().parent_path() / L"RVVoiceFix.log");
        auto loader = GetModuleHandleW(L"UE4SS.dll");
        if (!loader || sha256(modulePath(loader)) != LoaderHash ||
            sha256(modulePath(nullptr)) != GameHash)
        {
            log(L"DISABLED: executable or UE4SS SHA-256 does not match the pinned build. No native "
                L"access performed.");
            return nullptr;
        }
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&start_mod), &self))
        {
            return nullptr;
        }
        auto& state = runtime();
        std::lock_guard lock(state.mutex);
        if (state.active)
        {
            return nullptr;
        }
        state.capture = CaptureControl(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
        state.settler = {};
        state.nextPoll = 0;
        state.keys = {};
        log(L"Exact executable and loader hashes accepted.");
        return new VoiceFix;
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
