#include "Input.hpp"

namespace rv
{
using namespace RC::Unreal;
using namespace ue;

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
    open_ = true;
    return std::wstring(L"Overlay opened: UI-only input ") + (mode ? L"set" : L"UNAVAILABLE") +
           L", cursor " + (cursor ? L"shown" : L"UNAVAILABLE") +
           (cursorWasVisible_ ? L" (was already visible)." : L".");
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
    return std::wstring(L"Overlay closed: ") + (cursorWasVisible_ ? L"game-and-UI" : L"game-only") +
           L" input " + (mode ? L"restored." : L"could not be restored; press Esc in game.");
}
} // namespace rv
