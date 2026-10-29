#include "Hud.hpp"
#include <cmath>

namespace rv
{
using namespace RC::Unreal;
using namespace ue;

namespace
{
struct LinearColor
{
    float r, g, b, a;
};

float linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

LinearColor convert(Color c)
{
    return {linear(c.r), linear(c.g), linear(c.b), c.a};
}

// AHUD drawing functions are native /Script/Engine functions; their UFunctions
// and layouts are permanent, so they are resolved once.
struct HudMethods
{
    bool resolved = false;
    bool ok = false;
    Method drawRect;
    Method drawText;
    Method textSize;
    bool measured = false;
    float lineHeight = 24;
    float charWidth = 11;
};

HudMethods& methods()
{
    static HudMethods m;
    return m;
}

bool measure(UObject* hud, const std::wstring& sample, float& width, float& height)
{
    auto& m = methods();
    if (!m.textSize)
    {
        return false;
    }
    auto text = paramString(sample);
    Call call(m.textSize);
    call.set(L"Text", text).set<UObject*>(L"Font", nullptr).real(L"Scale", 1.0);
    if (!call.invoke(hud))
    {
        return false;
    }
    width = float(call.getReal(L"OutWidth"));
    height = float(call.getReal(L"OutHeight"));
    return width > 0 && height > 0;
}
} // namespace

HudPainter::HudPainter(UObject* hud) : hud_(hud)
{
    auto& m = methods();
    if (!m.resolved && live(hud))
    {
        m.drawRect = Method(hud, L"DrawRect");
        m.drawText = Method(hud, L"DrawText");
        m.textSize = Method(hud, L"GetTextSize");
        m.resolved = true;
        m.ok = m.drawRect && m.drawText;
        log(m.ok ? L"HUD drawing functions resolved."
                 : L"HUD drawing functions are unavailable; the overlay cannot draw.");
    }
    if (m.ok && !m.measured && live(hud))
    {
        m.measured = true;
        float w{};
        float h{};
        if (measure(hud, L"Hg", w, h))
        {
            m.lineHeight = h;
        }
        const std::wstring sample = L"The quick brown fox jumps over the lazy dog 0123456789";
        if (measure(hud, sample, w, h))
        {
            m.charWidth = w / float(sample.size());
        }
        log(L"HUD font metrics: line " + std::to_wstring(m.lineHeight) + L" px, average char " +
            std::to_wstring(m.charWidth) + L" px at scale 1.");
    }
}

bool HudPainter::ready() const
{
    return methods().ok && live(hud_);
}

void HudPainter::rect(float x, float y, float w, float h, Color c)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }
    Call call(methods().drawRect);
    call.set(L"RectColor", convert(c))
        .real(L"ScreenX", x)
        .real(L"ScreenY", y)
        .real(L"ScreenW", w)
        .real(L"ScreenH", h)
        .invoke(hud_);
}

void HudPainter::text(std::wstring_view s, float x, float y, Color c, float scale)
{
    if (s.empty())
    {
        return;
    }
    std::wstring owned(s);
    auto text = paramString(owned);
    Call call(methods().drawText);
    call.set(L"Text", text)
        .set(L"TextColor", convert(c))
        .real(L"ScreenX", x)
        .real(L"ScreenY", y)
        .set<UObject*>(L"Font", nullptr)
        .real(L"Scale", scale)
        .set(L"bScalePosition", false)
        .invoke(hud_);
}

float HudPainter::lineHeight()
{
    return methods().lineHeight;
}

float HudPainter::charWidth()
{
    return methods().charWidth;
}

UObject* InputControl::controller(UEngine* engine)
{
    auto viewport = obj(reinterpret_cast<UObject*>(engine), L"GameViewport");
    auto instance = obj(viewport, L"GameInstance");
    PointerArray players{};
    if (!pointerArray(field(instance, L"LocalPlayers"), players, 8))
    {
        return nullptr;
    }
    for (int i = 0; i < players.num; ++i)
    {
        if (auto pc = obj(static_cast<UObject*>(players.data[i]), L"PlayerController"))
        {
            return pc;
        }
    }
    return nullptr;
}

UObject* InputControl::hud(UEngine* engine)
{
    return obj(controller(engine), L"MyHUD");
}

static bool inputMode(const wchar_t* function, UObject* pc, bool flush)
{
    auto library = find(L"/Script/UMG.Default__WidgetBlueprintLibrary");
    Method m(library, function);
    if (!m || !m.param(L"PlayerController"))
    {
        return false;
    }
    Call call(m);
    call.set<UObject*>(L"PlayerController", pc);
    if (flush && m.param(L"bFlushInput"))
    {
        call.set(L"bFlushInput", true);
    }
    return call.invoke(library);
}

static bool showCursor(UObject* pc, bool visible)
{
    Method m(pc, L"SetShowMouseCursor");
    if (m && m.count() == 1)
    {
        return Call(m).set(m.at(0).first, visible).invoke(pc);
    }
    return setBool(field(pc, L"bShowMouseCursor"), visible);
}

std::wstring InputControl::open(UEngine* engine)
{
    auto pc = controller(engine);
    if (!pc)
    {
        return L"Overlay: no local player controller; input was not changed.";
    }
    bool visible = false;
    boolValue(field(pc, L"bShowMouseCursor"), visible);
    cursorWasVisible_ = visible;
    bool mode = inputMode(L"SetInputMode_UIOnlyEx", pc, false);
    bool cursor = showCursor(pc, true);
    auto hudField = field(obj(pc, L"MyHUD"), L"bShowHUD");
    bool shown = true;
    if (boolValue(hudField, shown) && !shown)
    {
        hudForced_ = setBool(hudField, true);
    }
    open_ = true;
    return std::wstring(L"Overlay opened: UI-only input ") + (mode ? L"set" : L"UNAVAILABLE") +
           L", cursor " + (cursor ? L"shown" : L"UNAVAILABLE") +
           (cursorWasVisible_ ? L" (was already visible)" : L"") +
           (hudForced_ ? L", HUD drawing enabled for the overlay." : L".");
}

std::wstring InputControl::close(UEngine* engine)
{
    if (!open_)
    {
        return {};
    }
    open_ = false;
    auto pc = controller(engine);
    if (!pc)
    {
        return L"Overlay closed: no local player controller to restore.";
    }
    bool mode = cursorWasVisible_ ? inputMode(L"SetInputMode_GameAndUIEx", pc, false)
                                  : inputMode(L"SetInputMode_GameOnly", pc, true);
    showCursor(pc, cursorWasVisible_);
    if (hudForced_)
    {
        setBool(field(obj(pc, L"MyHUD"), L"bShowHUD"), false);
        hudForced_ = false;
    }
    return std::wstring(L"Overlay closed: ") + (cursorWasVisible_ ? L"game-and-UI" : L"game-only") +
           L" input " + (mode ? L"restored." : L"could not be restored; press Esc in game.");
}
} // namespace rv
