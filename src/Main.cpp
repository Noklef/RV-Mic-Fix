#include "Hud.hpp"
#include "Voice.hpp"
#include <array>
#include <atomic>
#include <exception>
#include <mutex>

namespace rv
{
using RC::Unreal::UEngine;
using RC::Unreal::UFunction;
using RC::Unreal::UObject;

constexpr wchar_t Version[] = L"0.3.0-test";

// ReceiveDrawHUD of the current local HUD, non-null only while the overlay or
// a notice is visible. The ProcessEvent callback compares against it and
// returns; that is its entire cost for every other call.
static std::atomic<UFunction*> drawFunction{};

// The legacy UE4SS hooks cannot unregister. Pin this DLL and keep callback state
// alive until process exit. No mod-instance pointer enters the callbacks.
struct Runtime
{
    std::recursive_mutex mutex;
    bool busy = false; // tick is running; a draw re-entered from it returns
    bool active = false;
    bool faulted = false; // an exception occurred; only restore input, then stop
    bool registered = false;
    bool firstTick = true;
    CaptureControl capture;
    Voice voice;
    VoiceStatus voiceStatus;
    Prefs prefs;
    std::filesystem::path prefsPath;
    bool prefsDirty = false;
    uint64_t prefsDue = 0;
    Overlay overlay;
    InputControl input;
    bool open = false;
    bool details = false;
    std::vector<Device> devices;
    CaptureInfo captureInfo;
    std::wstring micStatus = L"Click a microphone to switch to it. The choice is saved.";
    std::wstring status;
    GUID savedId{};
    bool savedResolved = false;
    uint64_t nextSavedLookup = 0;
    Reapply reapply;
    std::vector<UiAction> pending;
    std::wstring notice;
    uint64_t noticeUntil = 0;
    uintptr_t hudIdentity = 0;
    int sizeXOffset = -1;
    int sizeYOffset = -1;
    uint64_t nextVoice = 0;
    uint64_t nextMic = 0;
    uint64_t nextHud = 0;
    uint64_t openedAt = 0;
    uint64_t lastDraw = 0;
    bool drawWarned = false;
    std::array<bool, 5> keys{}; // F7, F8, F9, F10, Esc
    HWND window{};
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
    wchar_t name[32]{};
    // The engine's main window class; excludes the UE4SS console in this process.
    if (process == GetCurrentProcessId() && IsWindowVisible(window) &&
        GetClassNameW(window, name, 32) && std::wstring_view(name) == L"UnrealWindow")
    {
        *reinterpret_cast<HWND*>(result) = window;
        return FALSE;
    }
    return TRUE;
}

static std::vector<Device> deviceList()
{
    auto devices = enumerateDevices();
    devices.insert(devices.begin(), Device{GUID{}, L"Windows default"});
    return devices;
}

static void showNotice(Runtime& state, std::wstring message, uint64_t now)
{
    state.notice = L"RV Voice Fix: " + std::move(message);
    state.noticeUntil = now + 6000;
}

static void say(Runtime& state, std::wstring message)
{
    log(message);
    state.status = std::move(message);
}

static void markDirty(Runtime& state, uint64_t now)
{
    state.prefsDirty = true;
    state.prefsDue = now + 1000;
}

static void resolveHud(Runtime& state, UEngine* engine)
{
    auto hud = InputControl::hud(engine);
    ue::Method draw(hud, L"ReceiveDrawHUD");
    auto x = draw ? draw.param(L"SizeX") : nullptr;
    auto y = draw ? draw.param(L"SizeY") : nullptr;
    if (!x || !y || x->size != 4 || y->size != 4)
    {
        state.hudIdentity = 0;
        drawFunction.store(nullptr);
        return;
    }
    state.sizeXOffset = x->offset;
    state.sizeYOffset = y->offset;
    state.hudIdentity = reinterpret_cast<uintptr_t>(hud);
    drawFunction.store(draw.function());
}

static void runPass(Runtime& state, UEngine* engine, bool repair, bool force, uint64_t now)
{
    std::vector<std::wstring> events;
    state.voiceStatus = state.voice.pass(engine, state.prefs, repair, force, now, events);
    for (auto& e : events)
    {
        log(e);
    }
    if (!events.empty())
    {
        showNotice(state, events.back(), now);
    }
}

static void openOverlay(Runtime& state, UEngine* engine, uint64_t now)
{
    if (state.open)
    {
        return;
    }
    state.window = nullptr;
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&state.window));
    log(L"F7 received; opening in-game overlay.");
    log(state.input.open(engine));
    state.devices = deviceList();
    state.savedResolved = false;
    state.open = true;
    state.openedAt = now;
    state.drawWarned = false;
    state.nextVoice = now;
    resolveHud(state, engine);
    if (!state.hudIdentity)
    {
        log(L"No HUD with ReceiveDrawHUD for the local player; the overlay cannot draw.");
    }
}

static void closeOverlay(Runtime& state, UEngine* engine)
{
    if (!state.open)
    {
        return;
    }
    state.open = false;
    log(state.input.close(engine));
}

static void perform(Runtime& state, UEngine* engine, const UiAction& action, uint64_t now)
{
    switch (action.kind)
    {
        case UiKind::Close:
            closeOverlay(state, engine);
            break;
        case UiKind::SelectDevice:
        {
            auto result = state.capture.apply(action.device);
            state.micStatus = result.message;
            log(result.message);
            if (result.ok)
            {
                bool isDefault = IsEqualGUID(action.device.id, GUID{});
                state.prefs.microphone = isDefault ? L"" : action.device.name;
                state.savedId = action.device.id;
                state.savedResolved = !isDefault;
                state.reapply.reset();
                markDirty(state, now);
            }
            break;
        }
        case UiKind::RefreshDevices:
            state.devices = deviceList();
            state.savedResolved = false;
            state.micStatus =
                L"Device list refreshed (" + std::to_wstring(state.devices.size() - 1) + L").";
            break;
        case UiKind::SetVolume:
            state.prefs.setPercent(action.player, action.value);
            state.voice.applyVolumes(state.prefs);
            markDirty(state, now);
            break;
        case UiKind::ToggleMute:
            state.prefs.toggleMute(action.player);
            state.voice.applyVolumes(state.prefs);
            markDirty(state, now);
            say(state, action.player +
                           (state.prefs.player(action.player).muted ? L" muted." : L" unmuted."));
            break;
        case UiKind::ToggleAuto:
            state.prefs.autoFix = !state.prefs.autoFix;
            markDirty(state, now);
            say(state, state.prefs.autoFix
                           ? L"Auto-fix ON: talkers are re-registered every 2 seconds."
                           : L"Auto-fix OFF: no talker or voice stream is changed.");
            break;
        case UiKind::RepairNow:
            runPass(state, engine, true, true, now);
            say(state,
                !state.voiceStatus.inSession ? std::wstring(L"Fix now: not in a session.")
                : !state.voiceStatus.host
                    ? std::wstring(L"Fix now runs only on the host; nothing was changed.")
                    : L"Fix now: re-registered " + std::to_wstring(state.voiceStatus.registered) +
                          L" talker(s), restarted " + std::to_wstring(state.voiceStatus.restarted) +
                          L" voice stream(s).");
            break;
        case UiKind::Diagnose:
            runPass(state, engine, false, false, now);
            Voice::logStatus(state.voiceStatus);
            say(state, L"Snapshot written to RVVoiceFix.log.");
            break;
        case UiKind::ToggleDetails:
            state.details = !state.details;
            break;
    }
}

// Re-applies the saved microphone when the game's capture uses another device,
// for example after the game reopens capture. "Windows default" is never forced.
static void pollMicrophone(Runtime& state, uint64_t now)
{
    state.captureInfo = state.capture.current();
    if (state.prefs.microphone.empty() || !state.captureInfo.ok ||
        !switchable(state.captureInfo.state))
    {
        return;
    }
    if (!state.savedResolved && now >= state.nextSavedLookup)
    {
        state.nextSavedLookup = now + 10000;
        for (auto& d : enumerateDevices())
        {
            if (d.name == state.prefs.microphone)
            {
                state.savedId = d.id;
                state.savedResolved = true;
            }
        }
    }
    if (!state.savedResolved ||
        !state.reapply.due(IsEqualGUID(state.captureInfo.id, state.savedId), now))
    {
        return;
    }
    auto result = state.capture.apply({state.savedId, state.prefs.microphone});
    state.reapply.result(result, now);
    log(L"Saved microphone re-apply: " + result.message);
    state.micStatus = result.message;
    if (result.ok)
    {
        showNotice(state, L"microphone restored to " + state.prefs.microphone, now);
    }
    else if (state.reapply.failures >= Reapply::MaxFailures)
    {
        state.micStatus = L"Saved microphone could not be re-applied; pick it again.";
        showNotice(state, state.micStatus, now);
    }
}

static Pointer pointer(Runtime& state, float width, float height)
{
    Pointer p;
    if (!state.window || GetAncestor(GetForegroundWindow(), GA_ROOT) != state.window)
    {
        return p;
    }
    POINT at{};
    RECT client{};
    if (!GetCursorPos(&at) || !ScreenToClient(state.window, &at) ||
        !GetClientRect(state.window, &client) || client.right <= 0 || client.bottom <= 0)
    {
        return p;
    }
    p.x = float(at.x) * width / float(client.right);
    p.y = float(at.y) * height / float(client.bottom);
    p.valid = at.x >= 0 && at.y >= 0 && at.x < client.right && at.y < client.bottom;
    int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    p.down = (GetAsyncKeyState(button) & 0x8000) != 0;
    return p;
}

static void draw(UObject* hud, void* params)
{
    auto& state = runtime();
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock() || !state.active || state.faulted || state.busy || !params ||
        reinterpret_cast<uintptr_t>(hud) != state.hudIdentity)
    {
        return;
    }
    try
    {
        HudPainter painter(hud);
        if (!painter.ready())
        {
            return;
        }
        auto now = GetTickCount64();
        state.lastDraw = now;
        int32_t sizeX{};
        int32_t sizeY{};
        std::memcpy(&sizeX, static_cast<char*>(params) + state.sizeXOffset, 4);
        std::memcpy(&sizeY, static_cast<char*>(params) + state.sizeYOffset, 4);
        if (sizeX < 64 || sizeY < 64 || sizeX > 16384 || sizeY > 16384)
        {
            return;
        }
        float w = float(sizeX);
        float h = float(sizeY);
        if (state.open)
        {
            OverlayModel model;
            model.devices = &state.devices;
            model.activeDevice = state.captureInfo.id;
            model.activeKnown = state.captureInfo.ok;
            model.micStatus = state.micStatus;
            model.voice = &state.voiceStatus;
            model.prefs = &state.prefs;
            model.details = state.details;
            model.status = state.status;
            for (auto& a : state.overlay.frame(painter, w, h, pointer(state, w, h), model))
            {
                if (state.pending.size() < 32)
                {
                    state.pending.push_back(std::move(a));
                }
            }
        }
        else if (now < state.noticeUntil)
        {
            float alpha = std::min(1.0f, float(state.noticeUntil - now) / 1000.0f);
            Overlay::notice(painter, w, h, state.notice, alpha);
        }
    }
    catch (const std::exception&)
    {
        state.faulted = true;
        drawFunction.store(nullptr);
        log(L"C++ exception while drawing; mod disabled until the game restarts.");
    }
}

static void onProcessEvent(UObject* context, UFunction* function, void* params)
{
    auto target = drawFunction.load(std::memory_order_relaxed);
    if (target && function == target)
    {
        draw(context, params);
    }
}

static void tick(UEngine* engine, float)
{
    auto& state = runtime();
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock() || !state.active)
    {
        return;
    }
    if (state.faulted)
    {
        state.active = false;
        try
        {
            closeOverlay(state, engine);
        }
        catch (const std::exception&)
        {
        }
        return;
    }
    state.busy = true;

    struct Clear
    {
        bool& flag;

        ~Clear()
        {
            flag = false;
        }
    } clear{state.busy};

    try
    {
        auto now = GetTickCount64();
        if (state.firstTick)
        {
            state.firstTick = false;
            log(L"Game-thread engine-tick callback is running.");
        }
        DWORD foregroundProcess{};
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        constexpr std::array<int, 5> keys{VK_F7, VK_F8, VK_F9, VK_F10, VK_ESCAPE};
        for (size_t i = 0; i < keys.size(); ++i)
        {
            bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
            if (down && !state.keys[i] && foregroundProcess == GetCurrentProcessId())
            {
                switch (keys[i])
                {
                    case VK_F7:
                        state.open ? closeOverlay(state, engine) : openOverlay(state, engine, now);
                        break;
                    case VK_ESCAPE:
                        closeOverlay(state, engine);
                        break;
                    case VK_F8:
                        state.pending.push_back({UiKind::Diagnose});
                        break;
                    case VK_F9:
                        state.pending.push_back({UiKind::RepairNow});
                        break;
                    default:
                        state.pending.push_back({UiKind::ToggleAuto});
                        break;
                }
            }
            state.keys[i] = down;
        }

        bool wantDraw = state.open || now < state.noticeUntil;
        if (wantDraw && now >= state.nextHud)
        {
            state.nextHud = now + 1000;
            resolveHud(state, engine);
        }
        else if (!wantDraw && drawFunction.load())
        {
            drawFunction.store(nullptr);
            state.hudIdentity = 0;
        }
        // Never leave game input blocked behind an overlay nobody can see.
        if (state.open && !state.drawWarned && now - state.openedAt > 2500 &&
            state.lastDraw < state.openedAt)
        {
            state.drawWarned = true;
            log(L"HUD draw was not observed within 2.5 s; closing the overlay. Please send "
                L"RVVoiceFix.log.");
            closeOverlay(state, engine);
        }

        if (now >= state.nextVoice)
        {
            state.nextVoice = now + (state.open ? 1000 : 2000);
            runPass(state, engine, state.prefs.autoFix, false, now);
        }
        if (now >= state.nextMic)
        {
            state.nextMic = now + 1000;
            pollMicrophone(state, now);
        }
        auto actions = std::move(state.pending);
        state.pending.clear();
        for (auto& a : actions)
        {
            perform(state, engine, a, now);
        }
        if (state.prefsDirty && now >= state.prefsDue)
        {
            if (state.prefs.save(state.prefsPath))
            {
                state.prefsDirty = false;
            }
            else
            {
                state.prefsDue = now + 5000;
                log(L"Could not save " + state.prefsPath.wstring() + L"; retrying in 5 s.");
            }
        }
    }
    catch (const std::exception&)
    {
        state.faulted = true;
        drawFunction.store(nullptr);
        log(L"C++ exception in game-thread work; mod disabled until the game restarts.");
    }
}

class VoiceFix final : public RC::CppUserModBase
{
public:
    VoiceFix()
    {
        ModName = L"RV Voice Fix";
        ModVersion = Version;
        ModDescription = L"In-game microphone switch, per-player volume, proximity voice repair";
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
            RC::Unreal::Hook::RegisterProcessEventPostCallback(onProcessEvent);
            state.registered = true;
        }
        state.active = true;
        log(std::wstring(L"Loaded v") + Version +
            L" for UE4SS cdbc0b66. Engine-tick and ProcessEvent callbacks registered. F7 opens "
            L"the in-game overlay.");
    }

    ~VoiceFix() override
    {
        auto& state = runtime();
        std::lock_guard lock(state.mutex);
        state.active = false;
        drawFunction.store(nullptr);
        log(L"Stopped; DLL retained for the legacy callbacks.");
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
        auto modDirectory = modulePath(self).parent_path().parent_path();
        setLogPath(modDirectory / L"RVVoiceFix.log");
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
        state.prefsPath = modDirectory / L"RVVoiceFix.ini";
        state.prefs = {};
        bool loaded = state.prefs.load(state.prefsPath);
        state.firstTick = true;
        state.keys = {};
        log(L"Exact executable and loader hashes accepted.");
        log(loaded ? L"Settings loaded from RVVoiceFix.ini." : L"No RVVoiceFix.ini yet; defaults.");
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
