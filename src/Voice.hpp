#pragma once
#include "Model.hpp"
#include "Prefs.hpp"
#include "Reflection.hpp"
#include <optional>
#include <unordered_map>

namespace rv
{
// Remote-voice repair and per-player volume. See docs/DESIGN.md.
class Voice
{
    struct Restart
    {
        unsigned count = 0;
        uint64_t last = 0;
    };

    // Keys are pointer values used only as identities; never dereferenced.
    std::unordered_map<uintptr_t, Restart> restarts_;
    std::unordered_map<uintptr_t, uintptr_t> registeredWith_;
    std::unordered_map<std::wstring, bool> lastPositional_;
    // Weak references survive between callbacks and resolve to null after GC.
    std::vector<std::pair<std::wstring, RC::Unreal::FWeakObjectPtr>> volumeTargets_;
    // Game multiplier per voice component (heard as 100%), read before the first
    // change. Entries whose component is gone are dropped, so a reused address is
    // read anew.
    std::unordered_map<uintptr_t, std::pair<RC::Unreal::FWeakObjectPtr, double>> baseVolumes_;

    double baseVolume(RC::Unreal::UObject* target);

public:
    // Rescans the session. repair: fix talker settings, re-register talkers and
    // restart voices playing without position. force: ignore restart spacing.
    // events receives one line per notable change for the log/notice.
    VoiceStatus pass(RC::Unreal::UEngine* engine, const Prefs& prefs, bool repair, bool force,
                     uint64_t now, std::vector<std::wstring>& events);
    // Re-applies saved volumes to the voices found by the last pass. Returns
    // whether player's volume was set; empty when player has no live voice.
    std::optional<bool> applyVolumes(const Prefs& prefs, const std::wstring& player);
    static void logStatus(const VoiceStatus& status);
    // F8: every voice synth with its filter results, and playing audio components
    // with a runtime-generated sound, to find where remote voice really plays.
    static void logVoiceObjects(RC::Unreal::UEngine* engine);
};
} // namespace rv
