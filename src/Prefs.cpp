#include "Prefs.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace rv
{
static constexpr std::wstring_view SteamPrefix = L"steam:";

static bool steamKey(std::wstring_view key)
{
    return key.starts_with(SteamPrefix);
}

std::wstring Prefs::identify(const std::wstring& steamId, const std::wstring& name)
{
    if (steamId.empty())
    {
        // A name that looks like a Steam key would be saved as that account's settings.
        return steamKey(name) ? std::wstring{} : name;
    }
    auto key = std::wstring(SteamPrefix) + steamId;
    names_[key] = name;
    auto found = players_.find(key);
    if (found == players_.end())
    {
        auto legacy = players_.find(name);
        if (legacy != players_.end() && !name.empty())
        {
            players_[key] = legacy->second;
            players_.erase(name);
            players_[key].name = name;
            changed = true;
        }
    }
    else if (!name.empty() && found->second.name != name)
    {
        found->second.name = name;
        changed = true;
    }
    return key;
}

std::wstring Prefs::label(const std::wstring& key) const
{
    if (auto found = players_.find(key); found != players_.end() && !found->second.name.empty())
    {
        return found->second.name;
    }
    auto seen = names_.find(key);
    return seen != names_.end() && !seen->second.empty() ? seen->second : key;
}

PlayerPref Prefs::player(const std::wstring& key) const
{
    auto found = players_.find(key);
    return found == players_.end() ? PlayerPref{} : found->second;
}

bool Prefs::managed(const std::wstring& key) const
{
    return players_.count(key) != 0;
}

void Prefs::setPercent(const std::wstring& key, int percent)
{
    if (!key.empty())
    {
        players_[key].percent = std::clamp(percent, 0, MaxPercent);
        rememberName(key);
    }
}

void Prefs::toggleMute(const std::wstring& key)
{
    if (!key.empty())
    {
        players_[key].muted = !players_[key].muted;
        rememberName(key);
    }
}

void Prefs::rememberName(const std::wstring& key)
{
    if (auto seen = names_.find(key); seen != names_.end() && !seen->second.empty())
    {
        players_[key].name = seen->second;
    }
}

float Prefs::gain(const std::wstring& key) const
{
    auto p = player(key);
    return p.muted ? 0.0f : float(p.percent) / 100.0f;
}

std::wstring Prefs::serialize() const
{
    std::wostringstream out;
    out << L"# RV Voice Fix settings. Edit while the game is closed.\n";
    out << L"autofix " << (autoFix ? 1 : 0) << L'\n';
    if (!microphone.empty())
    {
        out << L"microphone " << microphone << L'\n';
    }
    for (auto& [key, p] : players_)
    {
        if (steamKey(key))
        {
            out << L"steam " << p.percent << L' ' << (p.muted ? 1 : 0) << L' '
                << key.substr(SteamPrefix.size()) << L' ' << p.name << L'\n';
        }
        else
        {
            out << L"player " << p.percent << L' ' << (p.muted ? 1 : 0) << L' ' << key << L'\n';
        }
    }
    return out.str();
}

static std::wstring_view word(std::wstring_view& line)
{
    auto end = line.find(L' ');
    auto result = line.substr(0, end);
    line = end == std::wstring_view::npos ? std::wstring_view{} : line.substr(end + 1);
    return result;
}

static bool number(std::wstring_view text, int& value)
{
    if (text.empty() || text.size() > 4)
    {
        return false;
    }
    value = 0;
    for (auto c : text)
    {
        if (c < L'0' || c > L'9')
        {
            return false;
        }
        value = value * 10 + (c - L'0');
    }
    return true;
}

bool steamId(std::wstring_view text)
{
    return text.size() >= 5 && text.size() <= 20 &&
           std::all_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

void Prefs::parse(std::wstring_view text)
{
    while (!text.empty())
    {
        auto end = text.find(L'\n');
        auto line = text.substr(0, end);
        text = end == std::wstring_view::npos ? std::wstring_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == L'\r')
        {
            line.remove_suffix(1);
        }
        auto key = word(line);
        int a{};
        int b{};
        if (key == L"autofix" && number(line, a))
        {
            autoFix = a != 0;
        }
        else if (key == L"microphone")
        {
            microphone = line;
        }
        else if (key == L"player" && number(word(line), a) && number(word(line), b) &&
                 !line.empty() && !steamKey(line))
        {
            std::wstring name(line);
            players_[name] = {std::clamp(a, 0, MaxPercent), b != 0, name};
        }
        else if (key == L"steam" && number(word(line), a) && number(word(line), b))
        {
            auto id = word(line);
            if (steamId(id))
            {
                players_[std::wstring(SteamPrefix) + std::wstring(id)] = {
                    std::clamp(a, 0, MaxPercent), b != 0, std::wstring(line)};
            }
        }
    }
}

bool Prefs::load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::string utf8((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (utf8.size() > 65536)
    {
        return false;
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring text(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), text.data(), size);
    parse(text);
    return true;
}

bool Prefs::save(const std::filesystem::path& path) const
{
    auto text = serialize();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr,
                                   nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), utf8.data(), size, nullptr,
                        nullptr);
    // Write then replace, so a crash never leaves a truncated file.
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(utf8.data(), utf8.size()))
        {
            return false;
        }
    }
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
} // namespace rv
