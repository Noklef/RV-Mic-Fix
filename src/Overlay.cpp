#include "Overlay.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rv
{
namespace
{
Item text(std::wstring s, Tone tone = Tone::Ink, float width = 0)
{
    return {ItemKind::Text, std::move(s), tone, width};
}

Item small(std::wstring s, Tone tone = Tone::Dim, float width = 0)
{
    return {ItemKind::Small, std::move(s), tone, width};
}

Item button(std::wstring s, UiAction action, Tone tone = Tone::Ink, float width = 0)
{
    return {ItemKind::Button, std::move(s), tone, width, std::move(action)};
}

std::wstring distanceText(double metres)
{
    if (metres < 0)
    {
        return L"-";
    }
    wchar_t buffer[32];
    swprintf(buffer, 32, metres < 10 ? L"%.1f m" : L"%.0f m", metres);
    return buffer;
}

Item chip(const PlayerInfo& p)
{
    if (!p.pawn)
    {
        return small(L"NO PAWN", Tone::Dim, 130);
    }
    if (p.speaking)
    {
        return p.positional ? small(L"POSITIONAL", Tone::Good, 130)
                            : small(L"GLOBAL", Tone::Bad, 130);
    }
    return p.talker ? small(L"quiet", Tone::Dim, 130) : small(L"NO TALKER", Tone::Warn, 130);
}
} // namespace

std::wstring fit(std::wstring_view text, size_t maxChars)
{
    if (text.size() <= maxChars)
    {
        return std::wstring(text);
    }
    if (maxChars <= 3)
    {
        return std::wstring(text.substr(0, maxChars));
    }
    return std::wstring(text.substr(0, maxChars - 3)) + L"...";
}

std::vector<Row> describe(const OverlayModel& model)
{
    std::vector<Row> rows;
    rows.push_back({{ItemKind::Title, L"RV VOICE FIX", Tone::Accent},
                    small(L"v0.3.0-test"),
                    button(L"Close", {UiKind::Close})});

    // Microphone
    rows.push_back({{ItemKind::Heading, L"MICROPHONE", Tone::Accent}});
    static const std::vector<Device> none;
    const auto& devices = model.devices ? *model.devices : none;
    std::wstring active = L"unknown (no voice capture open yet)";
    for (size_t i = 1; i < devices.size() && model.activeKnown; ++i)
    {
        if (IsEqualGUID(devices[i].id, model.activeDevice))
        {
            active = devices[i].name;
        }
    }
    rows.push_back({small(L"In use:", Tone::Dim, 70), text(fit(active, 70))});
    std::wstring saved = model.prefs ? model.prefs->microphone : L"";
    for (size_t i = 0; i < devices.size() && i < MaxDevices; ++i)
    {
        bool inUse = i > 0 && model.activeKnown && IsEqualGUID(devices[i].id, model.activeDevice);
        bool isSaved = i > 0 && !saved.empty() && devices[i].name == saved;
        auto name = i == 0 ? std::wstring(L"Windows default (resolved when chosen)")
                           : fit(devices[i].name, 60);
        std::wstring tag = inUse     ? (isSaved ? L"IN USE - SAVED" : L"IN USE")
                           : isSaved ? L"SAVED"
                                     : L"";
        rows.push_back({button(name, {UiKind::SelectDevice, devices[i]},
                               inUse ? Tone::Selected : Tone::Ink, 520),
                        small(tag, inUse ? Tone::Good : Tone::Dim)});
    }
    if (devices.size() > MaxDevices)
    {
        rows.push_back(
            {small(L"+" + std::to_wstring(devices.size() - MaxDevices) + L" more not shown")});
    }
    rows.push_back(
        {button(L"Refresh list", {UiKind::RefreshDevices}), small(fit(model.micStatus, 80))});

    // Players
    const VoiceStatus empty;
    const auto& voice = model.voice ? *model.voice : empty;
    rows.push_back({{ItemKind::Heading,
                     !voice.inSession ? L"PLAYERS  -  not in a session"
                     : voice.host     ? L"PLAYERS  -  you are the host"
                                      : L"PLAYERS  -  you are a client",
                     Tone::Accent}});
    if (voice.players.empty())
    {
        rows.push_back({text(L"No other players yet.", Tone::Dim)});
    }
    else
    {
        rows.push_back({small(L"Name", Tone::Dim, 210), small(L"Distance", Tone::Dim, 80),
                        small(L"Voice", Tone::Dim, 130), small(L"Volume", Tone::Dim, 260)});
    }
    for (auto& p : voice.players)
    {
        auto pref = model.prefs ? model.prefs->player(p.key) : PlayerPref{};
        Item slider{ItemKind::Slider, L"", Tone::Ink, 190, {UiKind::SetVolume, {}, p.key},
                    pref.percent};
        rows.push_back({text(fit(p.name, 24), Tone::Ink, 210),
                        text(distanceText(p.distance), Tone::Ink, 80), chip(p), slider,
                        text(pref.muted ? L"muted" : std::to_wstring(pref.percent) + L"%",
                             pref.muted ? Tone::Warn : Tone::Ink, 60),
                        button(pref.muted ? L"Unmute" : L"Mute", {UiKind::ToggleMute, {}, p.key},
                               pref.muted ? Tone::Selected : Tone::Ink, 80)});
        if (model.details)
        {
            rows.push_back({small(fit(p.detail, 110))});
        }
    }

    // Proximity repair
    rows.push_back({{ItemKind::Heading, L"PROXIMITY FIX", Tone::Accent}});
    bool autoFix = model.prefs && model.prefs->autoFix;
    rows.push_back({button(autoFix ? L"Auto-fix: ON" : L"Auto-fix: OFF", {UiKind::ToggleAuto},
                           autoFix ? Tone::Selected : Tone::Ink),
                    button(L"Fix now", {UiKind::RepairNow}),
                    button(L"Write log", {UiKind::Diagnose}),
                    button(model.details ? L"Details: ON" : L"Details: OFF",
                           {UiKind::ToggleDetails}, model.details ? Tone::Selected : Tone::Ink)});
    wchar_t summary[256];
    swprintf(summary, 256,
             L"Talkers %u (%u stale)   Voice streams %u   Global now %u   Re-registered %u   "
             L"Restarted %u   Scan %.1f ms",
             voice.talkers, voice.staleTalkers, voice.synths, voice.globalVoices, voice.registered,
             voice.restarted, voice.scanMs);
    rows.push_back({small(summary, voice.globalVoices ? Tone::Warn : Tone::Dim)});
    // Always present, so the first status message does not rebuild the panel.
    rows.push_back({text(fit(model.status, 100))});
    rows.push_back({small(L"F7 / Esc close     F8 write log     F9 fix now")});
    return rows;
}

std::vector<Option> describeOptions(const OverlayModel& model)
{
    std::vector<Option> options;
    static const std::vector<Device> none;
    const auto& devices = model.devices ? *model.devices : none;
    options.push_back({OptionKind::Heading, L"Microphone"});
    // The saved choice, not the device in use: it changes as soon as a switch
    // succeeds, and a refused switch steps back.
    Option mic{OptionKind::Stepper, L"Device"};
    std::wstring saved = model.prefs ? model.prefs->microphone : L"";
    for (size_t i = 0; i < devices.size(); ++i)
    {
        // The stepper shows about 26 characters.
        mic.choices.push_back(i == 0 ? std::wstring(L"Windows default") : fit(devices[i].name, 26));
        mic.actions.push_back({UiKind::SelectDevice, devices[i]});
        if (i > 0 && !saved.empty() && devices[i].name == saved)
        {
            mic.value = int(i);
        }
    }
    if (!mic.choices.empty())
    {
        options.push_back(std::move(mic));
    }
    options.push_back(
        {OptionKind::Button, L"Device list", L"Refresh", {}, 0, {{UiKind::RefreshDevices}}});

    const VoiceStatus empty;
    const auto& voice = model.voice ? *model.voice : empty;
    if (!voice.players.empty())
    {
        options.push_back({OptionKind::Heading, L"Players"});
    }
    for (auto& p : voice.players)
    {
        auto pref = model.prefs ? model.prefs->player(p.key) : PlayerPref{};
        auto name = fit(p.name, 24);
        options.push_back({OptionKind::Slider,
                           name + L" volume",
                           L"",
                           {},
                           snapVolume(pref.percent), // the slider moves in 5% steps
                           {{UiKind::SetVolume, {}, p.key}}});
        UiAction mute{UiKind::ToggleMute, {}, p.key};
        options.push_back({OptionKind::Stepper,
                           L"Mute " + name,
                           L"",
                           {L"Off", L"On"},
                           pref.muted ? 1 : 0,
                           {mute, mute},
                           true});
    }

    options.push_back({OptionKind::Heading, L"Troubleshooting"});
    UiAction autoFix{UiKind::ToggleAuto};
    options.push_back({OptionKind::Stepper,
                       L"Proximity auto-fix",
                       L"",
                       {L"Off", L"On"},
                       model.prefs && model.prefs->autoFix ? 1 : 0,
                       {autoFix, autoFix}});
    options.push_back(
        {OptionKind::Button, L"Proximity fix", L"Fix now", {}, 0, {{UiKind::RepairNow}}});
    options.push_back(
        {OptionKind::Button, L"Diagnostic log", L"Write log", {}, 0, {{UiKind::Diagnose}}});
    return options;
}

std::wstring optionShape(const std::vector<Option>& options)
{
    std::wstring s;
    for (auto& o : options)
    {
        s += L'|' + std::to_wstring(int(o.kind)) + L':' + o.label + L':' + o.button;
        for (auto& c : o.choices)
        {
            s += L';' + c;
        }
    }
    return s;
}

std::vector<Row> describeNotice(std::wstring_view text)
{
    return {{{ItemKind::Text, fit(text, 120), Tone::Ink}}};
}

std::wstring shape(const std::vector<Row>& rows)
{
    std::wstring s;
    for (auto& row : rows)
    {
        s += L'|';
        for (auto& item : row)
        {
            s += wchar_t(L'0' + int(item.kind));
            s += std::to_wstring(int(item.width));
            if (item.kind == ItemKind::Button || item.kind == ItemKind::Slider)
            {
                s += L':' + std::to_wstring(int(item.action.kind)) + L':' + item.action.player +
                     L':' + item.action.device.name;
            }
            s += L';';
        }
    }
    return s;
}

int snapVolume(double raw)
{
    if (!std::isfinite(raw))
    {
        return 100;
    }
    return int(std::lround(std::clamp(raw, 0.0, double(Prefs::MaxPercent)) / 5.0)) * 5;
}

Color color(Tone tone)
{
    switch (tone)
    {
        case Tone::Dim:
            return {0.60f, 0.64f, 0.69f, 1};
        case Tone::Accent:
            return {0.96f, 0.71f, 0.26f, 1};
        case Tone::Good:
            return {0.40f, 0.86f, 0.50f, 1};
        case Tone::Bad:
            return {0.98f, 0.40f, 0.34f, 1};
        case Tone::Warn:
            return {0.98f, 0.76f, 0.30f, 1};
        default:
            return {0.93f, 0.94f, 0.95f, 1};
    }
}

Color buttonColor(Tone tone)
{
    return tone == Tone::Selected ? Color{0.18f, 0.31f, 0.50f, 1} : Color{0.16f, 0.18f, 0.21f, 1};
}

Color panelColor()
{
    return {0.05f, 0.06f, 0.07f, 0.92f};
}
} // namespace rv
