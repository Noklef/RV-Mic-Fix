#pragma once
#include "Common.hpp"
#include <map>
#include <string_view>

namespace rv
{
struct PlayerPref
{
    int percent = 100; // 0-200
    bool muted = false;
};

// Saved user choices: RVVoiceFix.ini beside the log. Players are keyed by their
// in-game display name. One setting per line; names are always last.
class Prefs
{
    std::map<std::wstring, PlayerPref> players_;

public:
    static constexpr int MaxPercent = 200;
    bool autoFix = true;
    std::wstring microphone; // Empty: no saved choice; the game's default is left alone.

    PlayerPref player(const std::wstring& name) const;
    bool managed(const std::wstring& name) const;
    void setPercent(const std::wstring& name, int percent);
    void toggleMute(const std::wstring& name);
    float gain(const std::wstring& name) const;

    std::wstring serialize() const;
    void parse(std::wstring_view text);
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};
} // namespace rv
