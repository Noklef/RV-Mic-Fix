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
    std::wstring name{}; // display name, for the file and the log
};

// A Steam ID as the game prints it: 5-20 decimal digits.
bool steamId(std::wstring_view text);

// Saved user choices: RVVoiceFix.ini beside the log. A player is keyed by Steam
// ID ("steam:<id>") when the game reports one, else by in-game display name.
// One setting per line; names are always last.
class Prefs
{
    std::map<std::wstring, PlayerPref> players_;
    std::map<std::wstring, std::wstring> names_; // key -> name seen this session

    void rememberName(const std::wstring& key);

public:
    static constexpr int MaxPercent = 200;
    static constexpr int StepPercent = 5; // slider step
    bool autoFix = true;
    std::wstring microphone; // Empty: no saved choice; the game's default is left alone.
    bool changed = false;    // set when identify() moved or renamed a saved player

    // The key for a player. A name-keyed entry moves to the Steam ID on first sight.
    std::wstring identify(const std::wstring& steamId, const std::wstring& name);
    std::wstring label(const std::wstring& key) const;
    PlayerPref player(const std::wstring& key) const;
    bool managed(const std::wstring& key) const;
    void setPercent(const std::wstring& key, int percent);
    void toggleMute(const std::wstring& key);
    float gain(const std::wstring& key) const;

    std::wstring serialize() const;
    void parse(std::wstring_view text);
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
};
} // namespace rv
