#include "Prefs.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace rv
{
PlayerPref Prefs::player(const std::wstring& name) const
{
    auto found = players_.find(name);
    return found == players_.end() ? PlayerPref{} : found->second;
}

bool Prefs::managed(const std::wstring& name) const
{
    return players_.count(name) != 0;
}

void Prefs::setPercent(const std::wstring& name, int percent)
{
    if (!name.empty())
    {
        players_[name].percent = std::clamp(percent, 0, MaxPercent);
    }
}

void Prefs::toggleMute(const std::wstring& name)
{
    if (!name.empty())
    {
        players_[name].muted = !players_[name].muted;
    }
}

float Prefs::gain(const std::wstring& name) const
{
    auto p = player(name);
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
    for (auto& [name, p] : players_)
    {
        out << L"player " << p.percent << L' ' << (p.muted ? 1 : 0) << L' ' << name << L'\n';
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
                 !line.empty())
        {
            std::wstring name(line);
            players_[name] = {std::clamp(a, 0, MaxPercent), b != 0};
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
