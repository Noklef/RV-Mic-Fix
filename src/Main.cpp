#include "Input.hpp"
#include "Voice.hpp"
#include "Widgets.hpp"
#include <array>
#include <exception>
#include <mutex>

namespace rv
{
using RC::Unreal::UEngine;
using RC::Unreal::UObject;

constexpr wchar_t Version[] = L"0.3.0-test";

// The legacy UE4SS hook cannot unregister. Pin this DLL and keep callback state
// alive until process exit. No mod-instance pointer enters the callback.
struct Runtime
{
    std::recursive_mutex mutex;
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
    Screen panel;
    Screen noticeScreen;
    bool noticeFailed = false; // a notice could not be shown; do not retry every tick
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
    uint64_t nextVoice = 0;
    uint64_t nextMic = 0;
    std::wstring lastSnapshot; // session and player key of the last logged snapshot
    std::wstring lastCounts;   // its stale-talker and global-voice counts
    uint64_t nextCountSnapshot = 0;
    std::wstring volumePlayer; // last volume change, logged once the slider rests
    std::optional<bool> volumeApplied;
    uint64_t volumeLogDue = 0;
    std::array<bool, 4> keys{}; // F7, F8, F9, Esc
};

static Runtime& runtime()
{
    static auto state = new Runtime;
    return *state;
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
    state.noticeFailed = false;
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
    // Snapshot on a session or player-list change, and on a fault-count change at
    // most every 30 s (a global voice starts and stops with each utterance), so an
    // intermittent proximity fault is in the log without pressing F8.
    auto& st = state.voiceStatus;
    auto key = std::to_wstring(int(st.inSession)) + L' ' + std::to_wstring(int(st.host)) + L' ' +
               std::to_wstring(st.talkers);
    for (auto& p : st.players)
    {
        key += L'|' + p.name;
    }
    auto counts = std::to_wstring(st.staleTalkers) + L' ' + std::to_wstring(st.globalVoices);
    if (key != state.lastSnapshot || (counts != state.lastCounts && now >= state.nextCountSnapshot))
    {
        state.lastSnapshot = std::move(key);
        state.lastCounts = std::move(counts);
        state.nextCountSnapshot = now + 30000;
        Voice::logStatus(st);
    }
}

static void logVolume(Runtime& state)
{
    state.volumeLogDue = 0;
    auto p = state.prefs.player(state.volumePlayer);
    log(L"VOLUME " + state.volumePlayer + L" " + std::to_wstring(p.percent) + L"%" +
        (p.muted ? L" (muted)" : L"") + L": " +
        (!state.volumeApplied   ? L"no voice stream yet; applied when one appears."
         : *state.volumeApplied ? L"applied."
                                : L"FAILED to set the voice volume."));
}

static void changeVolume(Runtime& state, const std::wstring& player, uint64_t due)
{
    if (state.volumeLogDue && state.volumePlayer != player)
    {
        logVolume(state);
    }
    state.volumePlayer = player;
    state.volumeApplied = state.voice.applyVolumes(state.prefs, player);
    state.volumeLogDue = due;
}

static void openOverlay(Runtime& state, UEngine* engine, uint64_t now)
{
    if (state.open)
    {
        return;
    }
    log(L"F7 received; opening in-game overlay.");
    log(state.input.open(engine));
    state.devices = deviceList();
    state.savedResolved = false;
    state.open = true;
    state.nextVoice = now;
}

static void closeOverlay(Runtime& state, UEngine* engine)
{
    if (!state.open)
    {
        return;
    }
    state.open = false;
    state.panel.hide();
    log(state.input.close(engine));
}

static void perform(Runtime& state, UEngine* engine, const UiAction& action, uint64_t now)
{
    switch (action.kind)
    {
        case UiKind::None:
            break;
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
            changeVolume(state, action.player, now + 1000); // one line once the slider rests
            markDirty(state, now);
            break;
        case UiKind::ToggleMute:
            state.prefs.toggleMute(action.player);
            changeVolume(state, action.player, now); // logged on this tick
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
            state.lastSnapshot.clear(); // forces the snapshot in runPass
            runPass(state, engine, false, false, now);
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

static OverlayModel model(const Runtime& state)
{
    OverlayModel m;
    m.devices = &state.devices;
    m.activeDevice = state.captureInfo.id;
    m.activeKnown = state.captureInfo.ok;
    m.micStatus = state.micStatus;
    m.voice = &state.voiceStatus;
    m.prefs = &state.prefs;
    m.details = state.details;
    m.status = state.status;
    return m;
}

// Builds or updates the UMG panel and notice, and queues panel clicks.
static void showScreens(Runtime& state, UEngine* engine, uint64_t now)
{
    std::wstring report;
    if (state.open)
    {
        if (state.panel.lost())
        {
            log(L"The game removed the overlay widget; closing the overlay.");
            closeOverlay(state, engine);
        }
        else if (!state.panel.show(InputControl::controller(engine), describe(model(state)), true,
                                   48, 96, report))
        {
            log(L"Overlay could not be shown: " + report + L" Please send RVVoiceFix.log.");
            closeOverlay(state, engine);
        }
        else
        {
            if (!report.empty())
            {
                log(report);
            }
            for (auto& a : state.panel.poll())
            {
                if (state.pending.size() < 32)
                {
                    state.pending.push_back(std::move(a));
                }
            }
        }
    }
    if (state.open || now >= state.noticeUntil || state.noticeFailed)
    {
        state.noticeScreen.hide();
    }
    else if (state.noticeScreen.lost() ||
             !state.noticeScreen.show(InputControl::controller(engine),
                                      describeNotice(state.notice), false, 24, 24, report))
    {
        state.noticeFailed = true;
        state.noticeScreen.hide();
        log(L"Notice could not be shown: " + report);
    }
}

static void tick(UEngine* engine, float)
{
    auto& state = runtime();
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        return;
    }
    if (!state.active)
    {
        // Unloaded while open: the callback stays registered, so give input
        // back here, on the game thread.
        if (state.open)
        {
            try
            {
                closeOverlay(state, engine);
            }
            catch (const std::exception&)
            {
            }
        }
        return;
    }
    if (state.faulted)
    {
        state.active = false;
        try
        {
            closeOverlay(state, engine);
            state.noticeScreen.hide();
        }
        catch (const std::exception&)
        {
        }
        return;
    }
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
        constexpr std::array<int, 4> keys{VK_F7, VK_F8, VK_F9, VK_ESCAPE};
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
                    default: // VK_F9
                        state.pending.push_back({UiKind::RepairNow});
                        break;
                }
            }
            state.keys[i] = down;
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
        try
        {
            showScreens(state, engine, now);
        }
        catch (const std::exception& e)
        {
            // Contained: the voice fix keeps running; only the overlay closes.
            log(L"C++ exception in the overlay widgets after '" + std::wstring(lastUmgStep()) +
                L"': " + widen(e.what()) + L". Please send RVVoiceFix.log.");
            state.noticeFailed = true;
            state.noticeScreen.hide();
            closeOverlay(state, engine);
        }
        auto actions = std::move(state.pending);
        state.pending.clear();
        for (auto& a : actions)
        {
            perform(state, engine, a, now);
        }
        if (state.volumeLogDue && now >= state.volumeLogDue)
        {
            logVolume(state);
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
    catch (const std::exception& e)
    {
        state.faulted = true;
        log(L"C++ exception in game-thread work (" + widen(e.what()) +
            L"); mod disabled until the game restarts.");
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
            state.registered = true;
        }
        state.active = true;
        log(std::wstring(L"Loaded v") + Version +
            L" for UE4SS cdbc0b66. Engine-tick callback registered. F7 opens the in-game "
            L"overlay.");
    }

    ~VoiceFix() override
    {
        auto& state = runtime();
        std::lock_guard lock(state.mutex);
        state.active = false;
        if (state.volumeLogDue)
        {
            logVolume(state);
        }
        log(L"Stopped; DLL retained for the legacy callback.");
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
