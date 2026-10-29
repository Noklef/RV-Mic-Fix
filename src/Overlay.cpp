#include "Overlay.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rv
{
namespace
{
constexpr Color Background{0.05f, 0.06f, 0.07f, 0.92f};
constexpr Color Accent{0.96f, 0.71f, 0.26f, 1};
constexpr Color Ink{0.93f, 0.94f, 0.95f, 1};
constexpr Color Dim{0.60f, 0.64f, 0.69f, 1};
constexpr Color Good{0.40f, 0.86f, 0.50f, 1};
constexpr Color Bad{0.98f, 0.40f, 0.34f, 1};
constexpr Color Warn{0.98f, 0.76f, 0.30f, 1};
constexpr Color Button{0.16f, 0.18f, 0.21f, 1};
constexpr Color Hover{0.25f, 0.28f, 0.32f, 1};
constexpr Color Selected{0.18f, 0.31f, 0.50f, 1};
constexpr Color Track{0.11f, 0.12f, 0.14f, 1};
constexpr Color Rule{0.96f, 0.71f, 0.26f, 0.35f};

struct Op
{
    bool text;
    float x, y, w, h;
    Color c;
    std::wstring s;
    float scale;
};

// Collects one frame of draw operations so the background can be sized first.
class Builder
{
    Painter& painter_;
    Pointer pointer_;
    std::vector<Hit>& hits_;
    std::vector<Op> ops_;

public:
    const float s;
    const bool pressed;
    float textScale;
    float textHeight;
    float charWidth;

    Builder(Painter& painter, Pointer pointer, float scale, bool press, std::vector<Hit>& hits)
        : painter_(painter), pointer_(pointer), hits_(hits), s(scale), pressed(press)
    {
        float base = std::max(painter.lineHeight(), 1.0f);
        textScale = 17.0f * s / base;
        textHeight = base * textScale;
        charWidth = std::max(painter.charWidth(), 0.1f) * textScale;
    }

    void rect(float x, float y, float w, float h, Color c)
    {
        ops_.push_back({false, x, y, w, h, c, {}, 0});
    }

    void text(std::wstring_view t, float x, float y, Color c, float size = 1.0f)
    {
        ops_.push_back({true, x, y, 0, 0, c, std::wstring(t), textScale * size});
    }

    // Vertically centred in a row of height h.
    void label(std::wstring_view t, float x, float y, float h, Color c, float size = 1.0f)
    {
        text(t, x, y + (h - textHeight * size) / 2, c, size);
    }

    size_t chars(float width, float size = 1.0f) const
    {
        return size_t(std::max(0.0f, width / (charWidth * size)));
    }

    float width(std::wstring_view t, float size = 1.0f) const
    {
        return float(t.size()) * charWidth * size;
    }

    bool inside(float x, float y, float w, float h) const
    {
        return pointer_.valid && pointer_.x >= x && pointer_.x < x + w && pointer_.y >= y &&
               pointer_.y < y + h;
    }

    bool hit(const std::wstring& id, float x, float y, float w, float h)
    {
        hits_.push_back({id, x, y, w, h});
        return pressed && inside(x, y, w, h);
    }

    bool button(const std::wstring& id, float x, float y, float w, float h, std::wstring_view t,
                bool selected = false)
    {
        rect(x, y, w, h, selected ? Selected : inside(x, y, w, h) ? Hover : Button);
        auto shown = fit(t, chars(w - 12 * s));
        label(shown, x + (w - width(shown)) / 2, y, h, Ink);
        return hit(id, x, y, w, h);
    }

    void flush(float x, float y, float w, float h)
    {
        painter_.rect(x, y, w, h, Background);
        painter_.rect(x, y, w, 3 * s, Accent);
        for (auto& op : ops_)
        {
            if (op.text)
            {
                painter_.text(op.s, op.x, op.y, op.c, op.scale);
            }
            else
            {
                painter_.rect(op.x, op.y, op.w, op.h, op.c);
            }
        }
    }
};

std::wstring percentText(int percent)
{
    return std::to_wstring(percent) + L"%";
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

std::vector<UiAction> Overlay::frame(Painter& painter, float width, float height, Pointer pointer,
                                     const OverlayModel& model)
{
    std::vector<UiAction> actions;
    hits_.clear();
    float s = std::clamp(height / 1080.0f, 0.7f, 2.5f);
    bool pressed = pointer.valid && pointer.down && !wasDown_;
    Builder ui(painter, pointer, s, pressed, hits_);

    const float panelW = std::min(780 * s, width - 32 * s);
    const float x0 = std::max(16 * s, std::min(48 * s, width - panelW - 16 * s));
    const float y0 = std::min(96 * s, height * 0.08f);
    const float pad = 18 * s;
    const float x = x0 + pad;
    const float inner = panelW - 2 * pad;
    float y = y0 + pad;

    auto section = [&](std::wstring_view title)
    {
        ui.text(title, x, y, Accent, 0.85f);
        y += ui.textHeight * 0.85f + 6 * s;
        ui.rect(x, y, inner, 1 * s, Rule);
        y += 8 * s;
    };

    // Title
    ui.label(L"RV VOICE FIX", x, y, 34 * s, Accent, 1.25f);
    ui.label(L"v0.3.0-test", x + ui.width(L"RV VOICE FIX", 1.25f) + 14 * s, y, 34 * s, Dim, 0.8f);
    if (ui.button(L"close", x + inner - 34 * s, y, 34 * s, 34 * s, L"X"))
    {
        actions.push_back({UiKind::Close});
    }
    y += 46 * s;

    // Microphone
    section(L"MICROPHONE");
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
    ui.label(L"In use:", x, y, 26 * s, Dim);
    ui.label(fit(active, ui.chars(inner - 80 * s)), x + 80 * s, y, 26 * s, Ink);
    y += 30 * s;
    std::wstring saved = model.prefs ? model.prefs->microphone : L"";
    for (size_t i = 0; i < devices.size() && i < MaxDevices; ++i)
    {
        const float rowH = 28 * s;
        bool inUse = i > 0 && model.activeKnown && IsEqualGUID(devices[i].id, model.activeDevice);
        bool isSaved = i > 0 && !saved.empty() && devices[i].name == saved;
        ui.rect(x, y, inner, rowH,
                inUse                          ? Selected
                : ui.inside(x, y, inner, rowH) ? Hover
                                               : Button);
        std::wstring tag = inUse     ? (isSaved ? L"IN USE - SAVED" : L"IN USE")
                           : isSaved ? L"SAVED"
                                     : L"";
        float tagW = ui.width(tag, 0.8f);
        auto name =
            i == 0 ? std::wstring(L"Windows default (resolved when chosen)") : devices[i].name;
        ui.label(fit(name, ui.chars(inner - tagW - 36 * s)), x + 12 * s, y, rowH, Ink);
        if (!tag.empty())
        {
            ui.label(tag, x + inner - tagW - 12 * s, y, rowH, inUse ? Good : Dim, 0.8f);
        }
        if (ui.hit(L"device:" + std::to_wstring(i), x, y, inner, rowH))
        {
            actions.push_back({UiKind::SelectDevice, devices[i]});
        }
        y += rowH + 3 * s;
    }
    if (devices.size() > MaxDevices)
    {
        ui.label(L"+" + std::to_wstring(devices.size() - MaxDevices) + L" more not shown", x, y,
                 22 * s, Dim, 0.8f);
        y += 24 * s;
    }
    y += 4 * s;
    if (ui.button(L"refresh", x, y, 150 * s, 30 * s, L"Refresh list"))
    {
        actions.push_back({UiKind::RefreshDevices});
    }
    ui.label(fit(model.micStatus, ui.chars(inner - 166 * s, 0.85f)), x + 164 * s, y, 30 * s, Dim,
             0.85f);
    y += 44 * s;

    // Players
    const VoiceStatus empty;
    const auto& voice = model.voice ? *model.voice : empty;
    section(!voice.inSession ? L"PLAYERS  -  not in a session"
            : voice.host     ? L"PLAYERS  -  you are the host"
                             : L"PLAYERS  -  you are a client");
    const float cName = x, cDist = x + 215 * s, cChip = x + 295 * s, cSlider = x + 428 * s,
                sliderW = 176 * s, cPercent = x + 614 * s, cMute = x + inner - 76 * s;
    if (voice.players.empty())
    {
        ui.label(L"No other players yet.", x, y, 26 * s, Dim);
        y += 32 * s;
    }
    else
    {
        ui.label(L"Name", cName, y, 20 * s, Dim, 0.8f);
        ui.label(L"Distance", cDist, y, 20 * s, Dim, 0.8f);
        ui.label(L"Voice", cChip, y, 20 * s, Dim, 0.8f);
        ui.label(L"Volume", cSlider, y, 20 * s, Dim, 0.8f);
        y += 24 * s;
    }
    for (auto& p : voice.players)
    {
        const float rowH = 36 * s;
        auto pref = model.prefs ? model.prefs->player(p.name) : PlayerPref{};
        ui.label(fit(p.name, ui.chars(205 * s)), cName, y, rowH, Ink);
        ui.label(distanceText(p.distance), cDist, y, rowH, Ink);
        std::wstring chip;
        Color chipColor = Dim;
        if (!p.pawn)
        {
            chip = L"NO PAWN";
        }
        else if (p.speaking)
        {
            chip = p.positional ? L"POSITIONAL" : L"GLOBAL";
            chipColor = p.positional ? Good : Bad;
        }
        else if (!p.talker)
        {
            chip = L"NO TALKER";
            chipColor = Warn;
        }
        else
        {
            chip = L"quiet";
        }
        ui.rect(cChip, y + 6 * s, 122 * s, rowH - 12 * s, Track);
        ui.label(chip, cChip + (122 * s - ui.width(chip, 0.8f)) / 2, y, rowH, chipColor, 0.8f);

        // Volume slider: 0-200%, snaps to 5% and to 100% when close.
        int value = pref.percent;
        auto id = L"volume:" + p.name;
        if (ui.hit(id, cSlider - 6 * s, y, sliderW + 12 * s, rowH))
        {
            dragging_ = p.name;
            lastDragValue_ = pref.percent;
        }
        if (dragging_ == p.name && pointer.down)
        {
            float t = std::clamp((pointer.x - cSlider) / sliderW, 0.0f, 1.0f);
            int v = int(std::lround(t * Prefs::MaxPercent / 5.0f)) * 5;
            if (std::abs(v - 100) <= 6)
            {
                v = 100;
            }
            if (v != lastDragValue_)
            {
                actions.push_back({UiKind::SetVolume, {}, p.name, v});
            }
            lastDragValue_ = v;
            value = v;
        }
        float fill = sliderW * float(value) / Prefs::MaxPercent;
        float trackY = y + rowH / 2 - 4 * s;
        ui.rect(cSlider, trackY, sliderW, 8 * s, Track);
        ui.rect(cSlider, trackY, fill, 8 * s, pref.muted ? Dim : Accent);
        ui.rect(cSlider + sliderW / 2 - 1 * s, trackY - 4 * s, 2 * s, 16 * s, Dim);
        ui.rect(cSlider + fill - 4 * s, trackY - 5 * s, 8 * s, 18 * s, Ink);
        ui.label(pref.muted ? L"muted" : percentText(value), cPercent, y, rowH,
                 pref.muted ? Warn : Ink, 0.9f);
        if (ui.button(L"mute:" + p.name, cMute, y + 3 * s, 76 * s, rowH - 6 * s,
                      pref.muted ? L"Unmute" : L"Mute", pref.muted))
        {
            actions.push_back({UiKind::ToggleMute, {}, p.name});
        }
        y += rowH + 2 * s;
        if (model.details && !p.detail.empty())
        {
            ui.label(fit(p.detail, ui.chars(inner - 12 * s, 0.75f)), x + 12 * s, y, 20 * s, Dim,
                     0.75f);
            y += 22 * s;
        }
    }
    y += 10 * s;

    // Proximity repair
    section(L"PROXIMITY FIX");
    bool autoFix = model.prefs && model.prefs->autoFix;
    if (ui.button(L"auto", x, y, 172 * s, 32 * s, autoFix ? L"Auto-fix: ON" : L"Auto-fix: OFF",
                  autoFix))
    {
        actions.push_back({UiKind::ToggleAuto});
    }
    if (ui.button(L"repair", x + 182 * s, y, 120 * s, 32 * s, L"Fix now"))
    {
        actions.push_back({UiKind::RepairNow});
    }
    if (ui.button(L"log", x + 312 * s, y, 130 * s, 32 * s, L"Write log"))
    {
        actions.push_back({UiKind::Diagnose});
    }
    if (ui.button(L"details", x + 452 * s, y, 150 * s, 32 * s,
                  model.details ? L"Details: ON" : L"Details: OFF", model.details))
    {
        actions.push_back({UiKind::ToggleDetails});
    }
    y += 42 * s;
    wchar_t summary[256];
    swprintf(summary, 256,
             L"Talkers %u (%u stale)   Voice streams %u   Global now %u   Re-registered %u   "
             L"Restarted %u   Scan %.1f ms",
             voice.talkers, voice.staleTalkers, voice.synths, voice.globalVoices, voice.registered,
             voice.restarted, voice.scanMs);
    ui.label(fit(summary, ui.chars(inner, 0.75f)), x, y, 20 * s, voice.globalVoices ? Warn : Dim,
             0.75f);
    y += 24 * s;
    if (!model.status.empty())
    {
        ui.label(fit(model.status, ui.chars(inner, 0.85f)), x, y, 24 * s, Ink, 0.85f);
        y += 28 * s;
    }
    y += 6 * s;
    ui.label(L"F7 / Esc close     F8 write log     F9 fix now     F10 auto-fix", x, y, 20 * s, Dim,
             0.75f);
    y += 22 * s + pad;

    // A small cursor dot, in case the game keeps its own cursor hidden.
    if (pointer.valid)
    {
        ui.rect(pointer.x - 4 * s, pointer.y - 4 * s, 8 * s, 8 * s, Color{0, 0, 0, 0.8f});
        ui.rect(pointer.x - 2.5f * s, pointer.y - 2.5f * s, 5 * s, 5 * s, Accent);
    }
    ui.flush(x0, y0, panelW, y - y0);
    if (!pointer.down)
    {
        dragging_.clear();
    }
    wasDown_ = pointer.down;
    return actions;
}

void Overlay::notice(Painter& painter, float width, float height, std::wstring_view text,
                     float alpha)
{
    float s = std::clamp(height / 1080.0f, 0.7f, 2.5f);
    float base = std::max(painter.lineHeight(), 1.0f);
    float scale = 16.0f * s / base;
    float charW = std::max(painter.charWidth(), 0.1f) * scale;
    auto shown = fit(text, size_t(std::max(0.0f, (width * 0.6f) / charW)));
    float w = float(shown.size()) * charW + 28 * s;
    float h = base * scale + 18 * s;
    float x = 24 * s;
    float y = 24 * s;
    painter.rect(x, y, w, h, Color{Background.r, Background.g, Background.b, 0.85f * alpha});
    painter.rect(x, y, 4 * s, h, Color{Accent.r, Accent.g, Accent.b, alpha});
    painter.text(shown, x + 16 * s, y + 9 * s, Color{Ink.r, Ink.g, Ink.b, alpha}, scale);
}
} // namespace rv
