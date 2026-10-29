#include "Widgets.hpp"
#include <cmath>
#include <map>

namespace rv
{
using namespace RC::Unreal;
using namespace ue;

namespace
{
constexpr uint32_t ClassAbstract = 0x1; // EClassFlags::CLASS_Abstract
constexpr uint8_t AlignCenter = 2;      // EVerticalAlignment::VAlign_Center
constexpr uint8_t HitTestInvisible = 3; // ESlateVisibility::HitTestInvisible
constexpr int32_t ZOrder = 1000;

// Last reflected path or function name used, named in fault reports.
const wchar_t* step = L"";
// Cleared if UE4SS cannot run property destructors; FText copies then leak.
bool canRelease = true;

struct Linear // FLinearColor
{
    float r, g, b, a;
};

struct Margin // FMargin
{
    float left, top, right, bottom;
};

struct Vector2 // FVector2D; double precision in UE5
{
    double x, y;
};

float linear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

Linear convert(Color c)
{
    return {linear(c.r), linear(c.g), linear(c.b), c.a};
}

// Native /Script classes, their default objects and UFunctions are never
// unloaded, so lookups are cached. Map keys are identities only.
UObject* native(const wchar_t* path)
{
    static std::map<std::wstring, UObject*> cache;
    step = path;
    auto it = cache.find(path);
    if (it != cache.end())
    {
        return it->second;
    }
    auto p = find(path);
    if (p)
    {
        cache.emplace(path, p);
    }
    return p;
}

const Method& method(UObject* target, const wchar_t* name)
{
    static const Method none;
    static std::map<std::pair<uintptr_t, std::wstring>, Method> cache;
    step = name;
    if (!callable(target))
    {
        return none;
    }
    std::pair key{reinterpret_cast<uintptr_t>(target->GetClassPrivate()), std::wstring(name)};
    auto it = cache.find(key);
    if (it == cache.end())
    {
        it = cache.emplace(std::move(key), Method(target, name)).first;
    }
    return it->second;
}

// Calls a function whose first parameter is value.
template <class T> bool callWith(UObject* target, const wchar_t* name, const T& value)
{
    auto& m = method(target, name);
    return m && m.count() >= 1 && Call(m).set(m.at(0).first, value).invoke(target);
}

bool query(UObject* target, const wchar_t* name, bool& result)
{
    auto& m = method(target, name);
    auto r = m.param(L"ReturnValue");
    if (!r || m.count() != 1 || r->size != 1)
    {
        return false;
    }
    Call call(m);
    if (!call.invoke(target))
    {
        return false;
    }
    result = call.get<bool>(L"ReturnValue");
    return true;
}

bool setText(UObject* block, const std::wstring& s)
{
    auto library = native(L"/Script/Engine.Default__KismetTextLibrary");
    auto& toText = method(library, L"Conv_StringToText");
    auto& set = method(block, L"SetText");
    auto result = toText.param(L"ReturnValue");
    if (!result || result->kind != L"TextProperty" || !result->property || set.count() != 1 ||
        set.at(0).second.size != result->size)
    {
        return false;
    }
    Call make(toText);
    make.set(L"InString", paramString(s));
    if (!make.invoke(library))
    {
        return false;
    }
    // The buffer owns one FText reference. SetText copies it; then it is released.
    auto text = make.slot(L"ReturnValue");
    bool ok = Call(set).bytes(set.at(0).first, text, size_t(result->size)).invoke(block);
    if (canRelease)
    {
        try
        {
            step = L"FProperty::DestroyValue";
            result->property->DestroyValue(text);
        }
        catch (const std::exception& e)
        {
            canRelease = false;
            log(L"FText release is unavailable in this UE4SS build (" + widen(e.what()) +
                L"); overlay text copies are no longer freed.");
        }
    }
    return ok;
}

// SetColorAndOpacity takes an FSlateColor: copy the current value and replace
// only its specified colour.
bool setColor(UObject* block, Color c)
{
    auto f = field(block, L"ColorAndOpacity");
    auto specified = member(f, L"SpecifiedColor");
    auto& m = method(block, L"SetColorAndOpacity");
    if (!specified || specified.property->GetSize() != int(sizeof(Linear)) || m.count() != 1)
    {
        return false;
    }
    std::vector<std::byte> value(size_t(f.property->GetSize()));
    std::memcpy(value.data(), f.address, value.size());
    auto offset = static_cast<std::byte*>(specified.address) - static_cast<std::byte*>(f.address);
    auto linearColor = convert(c);
    std::memcpy(value.data() + offset, &linearColor, sizeof(linearColor));
    return Call(m).bytes(m.at(0).first, value.data(), value.size()).invoke(block);
}

float fontSize(ItemKind kind)
{
    switch (kind)
    {
        case ItemKind::Title:
            return 18;
        case ItemKind::Heading:
            return 13;
        case ItemKind::Small:
            return 10.5f;
        default:
            return 12;
    }
}

// Creates widgets inside one widget tree. Failed cosmetic calls are counted,
// not fatal, so the log names the first one.
class Builder
{
    UObject* tree_;

public:
    int failures = 0;
    std::wstring first;

    explicit Builder(UObject* tree) : tree_(tree)
    {
    }

    void fail(const wchar_t* what)
    {
        if (!failures++)
        {
            first = what;
        }
    }

    template <class T> void call(UObject* target, const wchar_t* name, const T& value)
    {
        if (!callWith(target, name, value))
        {
            fail(name);
        }
    }

    UObject* spawn(const wchar_t* type)
    {
        auto statics = native(L"/Script/Engine.Default__GameplayStatics");
        auto& m = method(statics, L"SpawnObject");
        Call c(m);
        c.set<UObject*>(L"ObjectClass", native(type)).set<UObject*>(L"Outer", tree_);
        auto w = c.invoke(statics) ? c.get<UObject*>(L"ReturnValue") : nullptr;
        if (!live(w))
        {
            fail(type);
            return nullptr;
        }
        return w;
    }

    // Adds child to a panel; returns the slot.
    UObject* add(UObject* panel, const wchar_t* name, UObject* child)
    {
        auto& m = method(panel, name);
        if (!child || m.count() < 1)
        {
            fail(name);
            return nullptr;
        }
        Call c(m);
        c.set<UObject*>(m.at(0).first, child);
        auto slot = c.invoke(panel) ? c.get<UObject*>(L"ReturnValue") : nullptr;
        if (!live(slot))
        {
            fail(name);
            return nullptr;
        }
        return slot;
    }

    void label(UObject* block, ItemKind kind, const std::wstring& s, Color c)
    {
        // Written before Slate builds the widget, which reads the property.
        if (!setReal(member(field(block, L"Font"), L"Size"), fontSize(kind)))
        {
            fail(L"Font.Size");
        }
        if (!setText(block, s))
        {
            fail(L"SetText");
        }
        if (!setColor(block, c))
        {
            fail(L"SetColorAndOpacity");
        }
    }

    UObject* item(const Item& item, FWeakObjectPtr& widgetRef, FWeakObjectPtr& labelRef,
                  int& reported)
    {
        UObject* widget{};
        UObject* text{};
        if (item.kind == ItemKind::Button)
        {
            widget = spawn(L"/Script/UMG.Button");
            text = spawn(L"/Script/UMG.TextBlock");
            add(widget, L"AddChild", text);
            call(widget, L"SetBackgroundColor", convert(buttonColor(item.tone)));
            label(text, ItemKind::Text, item.text, color(Tone::Ink));
        }
        else if (item.kind == ItemKind::Slider)
        {
            widget = spawn(L"/Script/UMG.Slider");
            call(widget, L"SetMinValue", 0.0f);
            call(widget, L"SetMaxValue", float(Prefs::MaxPercent));
            call(widget, L"SetStepSize", 5.0f);
            // Dragging moves in the same 5% steps, so the thumb matches the saved value.
            if (!setBool(field(widget, L"MouseUsesStep"), true))
            {
                fail(L"MouseUsesStep");
            }
            call(widget, L"SetValue", float(item.value));
            reported = item.value;
        }
        else
        {
            widget = text = spawn(L"/Script/UMG.TextBlock");
            label(text, item.kind, item.text, color(item.tone));
        }
        widgetRef = widget;
        labelRef = text;
        if (!widget || item.width <= 0)
        {
            return widget;
        }
        auto box = spawn(L"/Script/UMG.SizeBox");
        call(box, L"SetWidthOverride", item.width);
        add(box, L"AddChild", widget);
        return box;
    }
};
} // namespace

// Moves child to the end of its vertical box with the same slot settings. They go
// through the setters: Slate reads slot values only when a slot is added or set.
static bool moveToEnd(UObject* panel, UObject* child)
{
    struct Setting
    {
        const wchar_t* property;
        const wchar_t* setter;
        std::vector<std::byte> value{};
    };

    std::array<Setting, 4> settings{{{L"Padding", L"SetPadding"},
                                     {L"Size", L"SetSize"},
                                     {L"HorizontalAlignment", L"SetHorizontalAlignment"},
                                     {L"VerticalAlignment", L"SetVerticalAlignment"}}};
    auto old = obj(child, L"Slot");
    for (auto& s : settings)
    {
        auto f = field(old, s.property);
        if (!f)
        {
            return false;
        }
        auto bytes = static_cast<const std::byte*>(f.address);
        s.value.assign(bytes, bytes + f.property->GetSize());
    }
    auto& remove = method(panel, L"RemoveChild");
    Call removed(remove);
    removed.set<UObject*>(L"Content", child);
    if (!removed.invoke(panel) || !removed.get<bool>(L"ReturnValue"))
    {
        return false;
    }
    auto& add = method(panel, L"AddChild");
    Call added(add);
    added.set<UObject*>(L"Content", child);
    auto slot = added.invoke(panel) ? added.get<UObject*>(L"ReturnValue") : nullptr;
    if (!live(slot))
    {
        log(L"Settings -> Sound: " + name(child) + L" was removed and could not be re-added.");
        return false;
    }
    bool ok = true;
    for (auto& s : settings)
    {
        auto& m = method(slot, s.setter);
        ok &= m && m.count() == 1 &&
              Call(m).bytes(m.at(0).first, s.value.data(), s.value.size()).invoke(slot);
    }
    return ok;
}

bool MenuButtons::attach(UObject* menu, UObject* panel, bool& added, std::wstring& report)
{
    added = false;
    // An entry ends when its button is gone or the game removed it from the panel.
    std::erase_if(entries_,
                  [](Entry& e)
                  {
                      auto button = e.button.Get();
                      return !live(e.panel.Get()) || !live(button) ||
                             callObject(button, L"GetParent") != e.panel.Get();
                  });
    for (auto& e : entries_)
    {
        if (e.panel.Get() == panel)
        {
            return true;
        }
    }
    auto tree = obj(menu, L"WidgetTree");
    if (!live(tree))
    {
        return true; // a menu being torn down: nothing to add to
    }
    // The game's rows, moved after the button below: UMG here has no InsertChildAt.
    std::vector<UObject*> rows;
    auto& count = method(panel, L"GetChildrenCount");
    auto& childAt = method(panel, L"GetChildAt");
    Call counted(count);
    int n = counted.invoke(panel) ? counted.get<int32_t>(L"ReturnValue") : 0;
    for (int i = 0; n <= 8 && i < n; ++i) // an unexpected layout is left unchanged
    {
        Call c(childAt);
        c.set<int32_t>(L"Index", i);
        if (auto row = c.invoke(panel) ? c.get<UObject*>(L"ReturnValue") : nullptr; live(row))
        {
            rows.push_back(row);
        }
    }
    Builder b(tree);
    Item item{ItemKind::Button, L"RV Voice Fix: players, volume, microphone  (F7)"};
    FWeakObjectPtr widget{};
    FWeakObjectPtr label{};
    int reported = 0;
    auto content = b.item(item, widget, label, reported);
    auto slot = b.add(panel, L"AddChild", content);
    if (!slot || !live(widget.Get()))
    {
        report = L"'" + b.first + L"' failed";
        return false;
    }
    b.call(slot, L"SetPadding", Margin{0, 0, 0, 12});
    b.call(slot, L"SetHorizontalAlignment", uint8_t(1)); // HAlign_Left (0 is Fill)
    auto& e = entries_.emplace_back();
    e.panel = panel;
    e.button = widget.Get();
    added = true;
    for (auto row : rows)
    {
        if (!moveToEnd(panel, row))
        {
            report = L"the game's rows could not be moved below the button";
            return true;
        }
    }
    if (b.failures)
    {
        report = L"cosmetic call '" + b.first + L"' failed";
    }
    return true;
}

bool MenuButtons::clicked()
{
    bool click = false;
    std::erase_if(entries_, [](Entry& e) { return !live(e.button.Get()); });
    for (auto& e : entries_)
    {
        auto button = e.button.Get();
        bool pressed = false;
        bool hovered = false;
        if (!live(button) || !query(button, L"IsPressed", pressed))
        {
            continue;
        }
        query(button, L"IsHovered", hovered);
        click |= e.pressed && !pressed && hovered; // released over the button
        e.pressed = pressed;
    }
    return click;
}

const wchar_t* lastUmgStep()
{
    return step;
}

bool setWidgetText(UObject* block, const std::wstring& text)
{
    return setText(block, text);
}

bool Screen::show(UObject* controller, const std::vector<Row>& rows, bool interactive, double x,
                  double y, std::wstring& report)
{
    auto s = shape(rows);
    if (built_ && s == shape_ && live(root_.Get()))
    {
        update(rows);
        return true;
    }
    hide();
    auto library = native(L"/Script/UMG.Default__WidgetBlueprintLibrary");
    auto type = static_cast<UClass*>(native(L"/Script/UMG.UserWidget"));
    auto& create = method(library, L"Create");
    if (!type || !create || !live(controller))
    {
        report = L"UMG widget creation is unavailable.";
        return false;
    }
    Call call(create);
    call.set<UObject*>(L"WorldContextObject", controller)
        .set<UObject*>(L"WidgetType", type)
        .set<UObject*>(L"OwningPlayer", controller);
    bool created = false;
    {
        // UUserWidget is abstract only to keep designers from placing the base
        // class. A native instance gets an empty WidgetTree, which is filled here.
        auto& flags = type->GetClassFlags();
        auto saved = flags;
        flags &= ~ClassAbstract;
        created = call.invoke(library);
        flags = saved;
    }
    auto user = created ? call.get<UObject*>(L"ReturnValue") : nullptr;
    auto tree = live(user) ? obj(user, L"WidgetTree") : nullptr;
    if (!tree)
    {
        report = L"CreateWidget returned no UserWidget with a widget tree.";
        return false;
    }

    Builder b(tree);
    auto canvas = b.spawn(L"/Script/UMG.CanvasPanel");
    if (!canvas || !setObject(field(tree, L"RootWidget"), canvas))
    {
        report = L"Could not create the widget root (" + b.first + L").";
        return false;
    }
    auto border = b.spawn(L"/Script/UMG.Border");
    auto slot = b.add(canvas, L"AddChildToCanvas", border);
    b.call(slot, L"SetAutoSize", true);
    b.call(slot, L"SetPosition", Vector2{x, y});
    b.call(border, L"SetBrushColor", convert(panelColor()));
    b.call(border, L"SetPadding", Margin{18, 12, 18, 14});
    auto column = b.spawn(L"/Script/UMG.VerticalBox");
    b.add(border, L"AddChild", column);
    for (auto& row : rows)
    {
        auto line = b.spawn(L"/Script/UMG.HorizontalBox");
        bool heading = !row.empty() && row[0].kind == ItemKind::Heading;
        b.call(b.add(column, L"AddChildToVerticalBox", line), L"SetPadding",
               Margin{0, heading ? 12.0f : 2.0f, 0, 2});
        for (auto& item : row)
        {
            auto& bound = bound_.emplace_back();
            bound.item = item;
            auto widget = b.item(bound.item, bound.widget, bound.label, bound.reported);
            auto cell = b.add(line, L"AddChildToHorizontalBox", widget);
            b.call(cell, L"SetPadding", Margin{0, 0, 10, 0});
            b.call(cell, L"SetVerticalAlignment", AlignCenter);
        }
    }
    if (!interactive)
    {
        b.call(user, L"SetVisibility", HitTestInvisible);
    }
    if (!callWith(user, L"AddToViewport", ZOrder))
    {
        bound_.clear();
        report = L"AddToViewport failed.";
        return false;
    }
    root_ = user;
    built_ = true;
    shape_ = std::move(s);
    report = L"Overlay widgets built: " + std::to_wstring(bound_.size()) + L" items";
    if (b.failures)
    {
        report += L"; " + std::to_wstring(b.failures) + L" UMG call(s) failed, first: " + b.first;
    }
    report += L".";
    return true;
}

void Screen::update(const std::vector<Row>& rows)
{
    size_t i = 0;
    for (auto& row : rows)
    {
        for (auto& item : row)
        {
            if (i >= bound_.size())
            {
                return;
            }
            auto& b = bound_[i++];
            auto label = b.label.Get();
            if (item.text != b.item.text && live(label) && setText(label, item.text))
            {
                b.item.text = item.text;
            }
            if (item.tone != b.item.tone &&
                (item.kind == ItemKind::Button ? callWith(b.widget.Get(), L"SetBackgroundColor",
                                                          convert(buttonColor(item.tone)))
                                               : setColor(label, color(item.tone))))
            {
                b.item.tone = item.tone;
            }
        }
    }
}

std::vector<UiAction> Screen::poll()
{
    std::vector<UiAction> actions;
    for (auto& b : bound_)
    {
        auto widget = b.widget.Get();
        if (!live(widget))
        {
            continue;
        }
        if (b.item.kind == ItemKind::Button)
        {
            bool pressed = false;
            bool hovered = false;
            if (!query(widget, L"IsPressed", pressed))
            {
                continue;
            }
            query(widget, L"IsHovered", hovered);
            // A click is a release over the button; releasing elsewhere cancels.
            if (b.pressed && !pressed && hovered)
            {
                actions.push_back(b.item.action);
            }
            b.pressed = pressed;
        }
        else if (b.item.kind == ItemKind::Slider)
        {
            auto& m = method(widget, L"GetValue");
            Call call(m);
            if (!m.param(L"ReturnValue") || !call.invoke(widget))
            {
                continue;
            }
            int v = snapVolume(call.getReal(L"ReturnValue"));
            if (v != b.reported)
            {
                auto a = b.item.action;
                a.value = v;
                actions.push_back(std::move(a));
                b.reported = v;
            }
        }
    }
    return actions;
}

void Screen::hide()
{
    // Never throws: callers restore game input after this, also during fault recovery.
    try
    {
        if (built_)
        {
            auto root = root_.Get();
            auto& m = method(root, L"RemoveFromParent");
            if (live(root) && m)
            {
                Call(m).invoke(root);
            }
        }
    }
    catch (const std::exception& e)
    {
        log(L"Overlay widget removal failed after '" + std::wstring(step) + L"' (" +
            widen(e.what()) + L").");
    }
    root_ = nullptr;
    built_ = false;
    shape_.clear();
    bound_.clear();
}

bool Screen::lost()
{
    if (!built_)
    {
        return false;
    }
    auto root = root_.Get();
    bool inViewport = true;
    return !live(root) || (query(root, L"IsInViewport", inViewport) && !inViewport);
}
} // namespace rv
