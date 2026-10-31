#include "Widgets.hpp"
#include <algorithm>
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

// Runs a property destructor on value, unless this UE4SS build cannot.
void release(FProperty* property, void* value)
{
    if (!canRelease)
    {
        return;
    }
    try
    {
        step = L"FProperty::DestroyValue";
        property->DestroyValue(value);
    }
    catch (const std::exception& e)
    {
        canRelease = false;
        log(L"FText release is unavailable in this UE4SS build (" + widen(e.what()) +
            L"); overlay text copies are no longer freed.");
    }
}

// Calls a function whose only parameter is an FText.
bool callText(UObject* block, const wchar_t* function, const std::wstring& s)
{
    auto library = native(L"/Script/Engine.Default__KismetTextLibrary");
    auto& toText = method(library, L"Conv_StringToText");
    auto& set = method(block, function);
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
    release(result->property, text);
    return ok;
}

bool setText(UObject* block, const std::wstring& s)
{
    return callText(block, L"SetText", s);
}

// The FText return value of Conv_StringToText, or null.
const Param* textResult()
{
    auto& m = method(native(L"/Script/Engine.Default__KismetTextLibrary"), L"Conv_StringToText");
    auto r = m.param(L"ReturnValue");
    return r && r->kind == L"TextProperty" && r->property ? r : nullptr;
}

// Moves a new FText for s into out, which must hold textResult()->size bytes and
// then owns one reference.
bool newText(const std::wstring& s, void* out)
{
    auto library = native(L"/Script/Engine.Default__KismetTextLibrary");
    auto r = textResult();
    Call make(method(library, L"Conv_StringToText"));
    make.set(L"InString", paramString(s));
    if (!r || !make.invoke(library))
    {
        return false;
    }
    std::memcpy(out, make.slot(L"ReturnValue"), size_t(r->size)); // the buffer has no destructor
    return true;
}

// Replaces the value of an FText property; the old value is released first.
bool setTextField(Field f, const std::wstring& s)
{
    auto r = textResult();
    if (!f || !r || !canRelease || f.property->kind() != L"TextProperty" ||
        f.property->GetSize() != r->size)
    {
        return false;
    }
    std::vector<std::byte> text(size_t(r->size));
    if (!newText(s, text.data()))
    {
        return false;
    }
    release(f.property, f.address);
    std::memcpy(f.address, text.data(), text.size());
    return true;
}

// Calls SetLabels(TArray<FText>) with an array the engine copies, then frees it.
bool setLabels(UObject* item, const std::vector<std::wstring>& labels)
{
    auto& m = method(item, L"SetLabels");
    auto r = textResult();
    if (!r || !canRelease || m.count() != 1)
    {
        return false;
    }
    auto size = size_t(r->size);
    std::vector<std::byte> texts(labels.size() * size);
    size_t made = 0;
    while (made < labels.size() && newText(labels[made], texts.data() + made * size))
    {
        ++made;
    }
    bool ok = false;
    if (made == labels.size())
    {
        PointerArray array{reinterpret_cast<void**>(texts.data()), int32_t(made), int32_t(made)};
        ok = Call(m).bytes(m.at(0).first, &array, sizeof(array)).invoke(item);
    }
    for (size_t i = 0; i < made; ++i)
    {
        release(r->property, texts.data() + i * size);
    }
    return ok;
}

bool setInt(Field f, int32_t value)
{
    if (!f || f.property->kind() != L"IntProperty")
    {
        return false;
    }
    *static_cast<int32_t*>(f.address) = value;
    return true;
}

bool intValue(Field f, int32_t& value)
{
    if (!f || f.property->kind() != L"IntProperty")
    {
        return false;
    }
    value = *static_cast<int32_t*>(f.address);
    return true;
}

UObject* spawnIn(UObject* type, UObject* outer)
{
    auto statics = native(L"/Script/Engine.Default__GameplayStatics");
    auto& m = method(statics, L"SpawnObject");
    Call c(m);
    c.set<UObject*>(L"ObjectClass", type).set<UObject*>(L"Outer", outer);
    auto o = type && c.invoke(statics) ? c.get<UObject*>(L"ReturnValue") : nullptr;
    return live(o) ? o : nullptr;
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
        auto w = spawnIn(native(type), tree_);
        if (!w)
        {
            fail(type);
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
            call(widget, L"SetStepSize", float(Prefs::StepPercent));
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

// The live children of a panel widget; empty for other widgets.
static std::vector<UObject*> children(UObject* panel)
{
    std::vector<UObject*> all;
    auto& count = method(panel, L"GetChildrenCount");
    auto& childAt = method(panel, L"GetChildAt");
    Call counted(count);
    int n = counted.invoke(panel) ? counted.get<int32_t>(L"ReturnValue") : 0;
    for (int i = 0; i < n && i < 64; ++i)
    {
        Call c(childAt);
        c.set<int32_t>(L"Index", i);
        if (auto child = c.invoke(panel) ? c.get<UObject*>(L"ReturnValue") : nullptr; live(child))
        {
            all.push_back(child);
        }
    }
    return all;
}

// Sets the first text found in widget: its own SetText, else a text block in
// its children or its widget tree. Returns the widget that took the text.
static UObject* labelWidget(UObject* widget, const std::wstring& text, int depth = 0)
{
    if (!live(widget) || depth > 10)
    {
        return nullptr;
    }
    if (setText(widget, text))
    {
        return widget;
    }
    if (auto tree = obj(widget, L"WidgetTree"); live(tree))
    {
        if (auto found = labelWidget(obj(tree, L"RootWidget"), text, depth + 1))
        {
            return found;
        }
    }
    for (auto child : children(widget))
    {
        if (auto found = labelWidget(child, text, depth + 1))
        {
            return found;
        }
    }
    return nullptr;
}

// An FName as call parameter bytes, or empty.
static std::vector<std::byte> nameBytes(const std::wstring& text)
{
    auto library = native(L"/Script/Engine.Default__KismetStringLibrary");
    auto& m = method(library, L"Conv_StringToName");
    auto r = m.param(L"ReturnValue");
    Call c(m);
    c.set(L"InString", paramString(text));
    if (!r || !c.invoke(library))
    {
        return {};
    }
    auto p = static_cast<const std::byte*>(c.slot(L"ReturnValue"));
    return {p, p + r->size};
}

// Moves child to the end of its vertical box with the same slot settings. They go
// through the setters: Slate reads slot values only when a slot is added or set.
struct SlotSetting
{
    const wchar_t* property;
    const wchar_t* setter;
    std::vector<std::byte> value{};
};

// Reads each setting's value from a panel slot. Values are plain structs and enums.
static bool readSlot(UObject* slot, std::vector<SlotSetting>& settings)
{
    for (auto& s : settings)
    {
        auto f = field(slot, s.property);
        if (!f)
        {
            return false;
        }
        auto bytes = static_cast<const std::byte*>(f.address);
        s.value.assign(bytes, bytes + f.property->GetSize());
    }
    return true;
}

// Applies read settings through the slot's setters.
static bool applySlot(UObject* slot, const std::vector<SlotSetting>& settings)
{
    bool ok = live(slot);
    for (auto& s : settings)
    {
        auto& m = method(slot, s.setter);
        ok &= m && m.count() == 1 &&
              Call(m).bytes(m.at(0).first, s.value.data(), s.value.size()).invoke(slot);
    }
    return ok;
}

static bool moveToEnd(UObject* panel, UObject* child)
{
    std::vector<SlotSetting> settings{{L"Padding", L"SetPadding"},
                                      {L"Size", L"SetSize"},
                                      {L"HorizontalAlignment", L"SetHorizontalAlignment"},
                                      {L"VerticalAlignment", L"SetVerticalAlignment"}};
    if (!readSlot(obj(child, L"Slot"), settings))
    {
        return false;
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
    return applySlot(slot, settings);
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
    auto rows = children(panel);
    if (rows.size() > 8)
    {
        rows.clear(); // an unexpected layout is left unchanged
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

// The game's option row classes (Blueprint), loaded while a settings menu exists.
static const wchar_t* itemClass(OptionKind kind)
{
    switch (kind)
    {
        case OptionKind::Slider:
            return L"/Game/Ride/Menu/OptionListItemBar.OptionListItemBar_C";
        case OptionKind::Stepper:
            return L"/Game/Ride/Menu/OptionListItemStepper.OptionListItemStepper_C";
        default:
            return L"/Game/Ride/Menu/OptionListItemButton.OptionListItemButton_C";
    }
}

// A new list of model's class beside it, with model's look and behaviour. The
// values are copied before Slate builds the list, which reads them once.
static UObject* copyList(UObject* model, std::wstring& report)
{
    static constexpr std::array settings{L"EntryWidgetClass",
                                         L"WidgetStyle",
                                         L"ScrollBarStyle",
                                         L"Orientation",
                                         L"SelectionMode",
                                         L"ConsumeMouseWheel",
                                         L"bClearSelectionOnClick",
                                         L"bIsFocusable",
                                         L"bReturnFocusToSelection",
                                         L"ScrollIntoViewAlignment",
                                         L"HorizontalEntrySpacing",
                                         L"VerticalEntrySpacing",
                                         L"ScrollBarPadding",
                                         L"WheelScrollMultiplier",
                                         L"bEnableScrollAnimation",
                                         L"bIsPointerScrollingEnabled",
                                         L"bIsGamepadScrollingEnabled",
                                         L"bSelectItemOnNavigation"};
    auto list = live(model) ? spawnIn(model->GetClassPrivate(), model->GetOuterPrivate()) : nullptr;
    if (!list)
    {
        report = L"the game's option list could not be copied";
        return nullptr;
    }
    int missing = 0;
    for (auto key : settings)
    {
        auto from = field(model, key);
        auto to = field(list, key);
        if (from && to && from.property == to.property)
        {
            step = key;
            from.property->CopyCompleteValue(to.address, from.address);
        }
        else
        {
            ++missing;
        }
    }
    if (missing)
    {
        report = std::to_wstring(missing) + L" list setting(s) not copied; ";
    }
    return list;
}

// A game option row for o. A failed cosmetic step is named in failed; the row
// is still usable.
static UObject* makeItem(UObject* list, const Option& o, std::wstring& failed)
{
    auto item = spawnIn(find(itemClass(o.kind)), list);
    if (!item)
    {
        failed = itemClass(o.kind);
        return nullptr;
    }
    bool ok = setTextField(field(item, L"Text"), o.label);
    if (o.sub)
    {
        ok &= setBool(field(item, L"IsSubOption"), true); // the entry indents it
    }
    if (o.kind == OptionKind::Slider)
    {
        ok &= setReal(field(item, L"MinValue"), 0) &&
              setReal(field(item, L"MaxValue"), Prefs::MaxPercent) &&
              // The entry's slider runs 0-1 and takes this step unscaled.
              setReal(field(item, L"StepSize"), double(Prefs::StepPercent) / Prefs::MaxPercent) &&
              setReal(field(item, L"CurrentValue"), o.value) &&
              setInt(field(item, L"MaximumNumberOfDecimals"), 0) &&
              setInt(field(item, L"MinimumNumberOfDecimals"), 0);
    }
    else if (o.kind == OptionKind::Stepper)
    {
        ok &= setLabels(item, o.choices) && callWith(item, L"SetSelectedIndex", int32_t(o.value));
    }
    else
    {
        ok &= callText(item, L"OverrideButtonText", o.button);
    }
    if (!ok && failed.empty())
    {
        failed = o.label;
    }
    return item;
}

// Adds child to a panel; returns the slot, or null.
static UObject* addChild(UObject* panel, UObject* child)
{
    Call c(method(panel, L"AddChild"));
    c.set<UObject*>(L"Content", child);
    auto slot = live(child) && c.invoke(panel) ? c.get<UObject*>(L"ReturnValue") : nullptr;
    return live(slot) ? slot : nullptr;
}

// The game's section heading (as on its Multiplayer page) for text, or null.
static UObject* headline(UObject* context, const std::wstring& text)
{
    auto type = find(L"/Game/Ride/Menu/WG_HeadlineDivider.WG_HeadlineDivider_C");
    auto library = native(L"/Script/UMG.Default__WidgetBlueprintLibrary");
    Call c(method(library, L"Create"));
    c.set<UObject*>(L"WorldContextObject", context).set<UObject*>(L"WidgetType", type);
    auto w = type && c.invoke(library) ? c.get<UObject*>(L"ReturnValue") : nullptr;
    if (!live(w))
    {
        return nullptr;
    }
    // PreConstruct copies DisplayText to the label when Slate builds the widget.
    bool ok = setTextField(field(w, L"DisplayText"), text);
    ok |= setText(obj(w, L"Lbl_Headline"), text);
    return ok ? w : nullptr;
}

// Calls each(entry) for the entry widgets a list shows, then frees the array
// the engine made.
template <class F> static void forDisplayed(UObject* list, F each)
{
    auto& shown = method(list, L"GetDisplayedEntryWidgets");
    auto r = shown.param(L"ReturnValue");
    Call c(shown);
    if (!r || !canRelease || !c.invoke(list))
    {
        return;
    }
    PointerArray entries{};
    if (pointerArray({r->property, c.slot(L"ReturnValue")}, entries, 256))
    {
        for (int32_t i = 0; i < entries.num; ++i)
        {
            auto entry = static_cast<UObject*>(entries.data[i]);
            if (live(entry))
            {
                each(entry);
            }
        }
    }
    release(r->property, c.slot(L"ReturnValue"));
}

// A widget's desired height in layout units, or 0.
static double desiredHeight(UObject* widget)
{
    auto& m = method(widget, L"GetDesiredSize");
    auto r = m.param(L"ReturnValue");
    Call c(m);
    if (!r || r->size != int(sizeof(Vector2)) || !c.invoke(widget))
    {
        return 0;
    }
    Vector2 size{};
    std::memcpy(&size, c.slot(L"ReturnValue"), sizeof(size));
    return size.y;
}

bool OptionPage::show(UObject* model, const std::vector<Option>& options, std::wstring& report)
{
    if (live(model))
    {
        model_ = model;
    }
    model = model_.Get();
    auto root = root_.Get();
    if (!live(root))
    {
        root = live(model) ? spawnIn(native(L"/Script/UMG.ScrollBox"), model->GetOuterPrivate())
                           : nullptr;
        if (!root)
        {
            report = L"the page could not be created";
            return false;
        }
        // Both are FScrollBarStyle: the page scrolls with the game's scroll bar.
        auto from = field(model, L"ScrollBarStyle");
        auto to = field(root, L"WidgetBarStyle");
        if (from && to && from.property->kind() == L"StructProperty" &&
            to.property->kind() == L"StructProperty" &&
            from.property->GetSize() == to.property->GetSize())
        {
            step = L"WidgetBarStyle";
            from.property->CopyCompleteValue(to.address, from.address);
        }
        root_ = root;
        shape_.clear();
    }
    auto s = optionShape(options);
    if (s == shape_)
    {
        update(options);
        return true;
    }
    if (!live(model))
    {
        report = L"the game's option list is gone";
        return false;
    }
    bound_.clear();
    sections_.clear();
    shape_.clear(); // a failed build is never taken for the old page
    Call(method(root, L"ClearChildren")).invoke(root);
    std::wstring failed;
    std::wstring copied;
    size_t rows = 0;
    for (size_t i = 0; i < options.size(); ++i)
    {
        auto& o = options[i];
        bool heading = o.kind == OptionKind::Heading;
        if (heading || sections_.empty())
        {
            if (heading)
            {
                auto slot = addChild(root, headline(model, o.label));
                if (!slot && failed.empty())
                {
                    failed = L"heading " + o.label;
                }
                if (slot && !sections_.empty())
                {
                    callWith(slot, L"SetPadding", Margin{0, 16, 0, 0});
                }
            }
            auto list = copyList(model, copied);
            auto box = spawnIn(native(L"/Script/UMG.SizeBox"), model->GetOuterPrivate());
            if (!list || !box || !addChild(root, box) || !addChild(box, list))
            {
                report = L"a page section could not be built; " + copied;
                return false;
            }
            auto& section = sections_.emplace_back();
            section.list = list;
            section.box = box;
            if (heading)
            {
                continue;
            }
        }
        ++rows;
        auto& section = sections_.back();
        auto list = section.list.Get();
        auto item = makeItem(list, o, failed);
        Call c(method(list, L"AddItem"));
        c.set<UObject*>(L"Item", item);
        if (item && c.invoke(list))
        {
            auto& b = bound_.emplace_back();
            b.option = o;
            b.item = item;
            b.index = i;
            b.section = sections_.size() - 1;
            b.reported = o.value;
            ++section.rows;
        }
    }
    if (bound_.empty() && rows)
    {
        report = L"no option row could be added (" + failed + L")";
        return false;
    }
    if (!resize() && failed.empty())
    {
        failed = L"SetHeightOverride";
    }
    shape_ = std::move(s);
    report = L"Voice Fix tab: " + copied + std::to_wstring(bound_.size()) + L" of " +
             std::to_wstring(rows) + L" rows in " + std::to_wstring(sections_.size()) +
             L" sections built" +
             (failed.empty() ? std::wstring(L".") : L"; first problem: " + failed + L".");
    return true;
}

bool OptionPage::resize()
{
    bool ok = true;
    for (auto& section : sections_)
    {
        auto box = section.box.Get();
        double spacing = 0;
        realValue(field(section.list.Get(), L"VerticalEntrySpacing"), spacing);
        // A little slack, so rounding never makes the list scroll.
        auto height = float(section.rows * (rowHeight_ + spacing) + 4);
        ok &= live(box) && callWith(box, L"SetHeightOverride", height);
    }
    return ok;
}

void OptionPage::update(const std::vector<Option>& options)
{
    std::vector<bool> regenerate(sections_.size());
    for (auto& b : bound_)
    {
        if (b.index >= options.size())
        {
            continue;
        }
        auto& o = options[b.index];
        b.option.actions = o.actions;
        auto item = b.item.Get();
        if (o.kind == OptionKind::Button || o.value == b.reported || b.pending >= 0 || !live(item))
        {
            continue;
        }
        // Changed elsewhere (the overlay, or a refused microphone switch): show it.
        if (o.kind == OptionKind::Stepper)
        {
            if (callWith(item, L"SetSelectedIndex", int32_t(o.value)))
            {
                b.reported = o.value;
            }
        }
        else if (setReal(field(item, L"CurrentValue"), o.value))
        {
            b.reported = o.value;
            regenerate[b.section] = true; // the slider entry reads the value only when generated
        }
    }
    for (size_t i = 0; i < sections_.size(); ++i)
    {
        auto list = sections_[i].list.Get();
        if (regenerate[i])
        {
            Call(method(list, L"RegenerateAllEntries")).invoke(list);
        }
    }
}

std::vector<UiAction> OptionPage::poll(uint64_t now)
{
    std::vector<UiAction> actions;
    for (auto& b : bound_)
    {
        auto item = b.item.Get();
        if (!live(item) || b.option.kind == OptionKind::Button)
        {
            continue;
        }
        if (b.option.kind == OptionKind::Slider)
        {
            double raw = 0;
            int v = realValue(field(item, L"CurrentValue"), raw) ? snapVolume(raw) : b.reported;
            if (v != b.reported && !b.option.actions.empty())
            {
                auto a = b.option.actions[0];
                a.value = v;
                actions.push_back(std::move(a));
                b.reported = v;
            }
            continue;
        }
        int32_t v = b.reported;
        if (!intValue(field(item, L"SelectedIndex"), v) || v < 0 ||
            size_t(v) >= b.option.actions.size())
        {
            continue;
        }
        // A microphone switch waits until the stepper rests, so stepping past
        // devices does not reopen the capture for each one.
        bool settle = b.option.actions[size_t(v)].kind == UiKind::SelectDevice;
        if (v == b.reported)
        {
            b.pending = -1;
        }
        else if (settle && v != b.pending)
        {
            b.pending = v;
            b.due = now + 800;
        }
        else if (!settle || now >= b.due)
        {
            actions.push_back(b.option.actions[size_t(v)]);
            b.reported = v;
            b.pending = -1;
        }
    }
    // The entry widgets on screen: their height sizes the sections, and button
    // rows hold the item and its button.
    double tallest = 0;
    for (auto& section : sections_)
    {
        forDisplayed(section.list.Get(),
                     [&](UObject* entry)
                     {
                         tallest = std::max(tallest, desiredHeight(entry));
                         auto data = obj(entry, L"ButtonData");
                         auto button = data ? obj(entry, L"WG_SignButton_111") : nullptr;
                         for (auto& b : bound_)
                         {
                             bool pressed = false;
                             bool hovered = false;
                             if (b.item.Get() != data || !live(button) ||
                                 !query(button, L"IsPressed", pressed))
                             {
                                 continue;
                             }
                             query(button, L"IsHovered", hovered);
                             if (b.pressed && !pressed && hovered && !b.option.actions.empty())
                             {
                                 actions.push_back(b.option.actions[0]);
                             }
                             b.pressed = pressed;
                         }
                     });
    }
    // Entry heights do not depend on the box, so this settles after one change.
    if (tallest > 0 && std::abs(tallest - rowHeight_) > 0.5)
    {
        rowHeight_ = tallest;
        resize();
    }
    return actions;
}

UObject* OptionPage::widget()
{
    auto root = root_.Get();
    return live(root) ? root : nullptr;
}

bool MenuTab::attach(UObject* tabs, UObject* model, const std::vector<Option>& options, bool& added,
                     std::wstring& report)
{
    added = false;
    if (id_.empty())
    {
        id_ = nameBytes(L"RVVoiceFix");
    }
    auto& byId = method(tabs, L"GetTabButtonBaseByID");
    auto idParam = byId.param(L"TabNameID");
    if (id_.empty() || !idParam || idParam->size != int(id_.size()))
    {
        report = L"tab IDs are not names of the expected size";
        return false;
    }
    auto button = [&](const void* id)
    {
        Call c(byId);
        c.bytes(L"TabNameID", id, id_.size());
        return c.invoke(tabs) ? c.get<UObject*>(L"ReturnValue") : nullptr;
    };
    auto switcher = callObject(tabs, L"GetLinkedSwitcher");
    if (!live(switcher))
    {
        report = L"the tab list has no page switcher";
        return false;
    }
    bool registered = live(button(id_.data()));
    if (registered && tabs_.Get() == tabs && callObject(page.widget(), L"GetParent") == switcher)
    {
        return true;
    }
    // The game's first tab gives the button class, so the new tab looks the same.
    auto& idAt = method(tabs, L"GetTabIdAtIndex");
    Call first(idAt);
    first.set<int32_t>(L"Index", 0);
    auto tabModel = first.invoke(tabs) ? button(first.slot(L"ReturnValue")) : nullptr;
    if (!live(tabModel) || (registered && tabModel == button(id_.data())))
    {
        // The menu registers its tabs after it is shown (or only ours is left);
        // try again later.
        report = L"the menu " + name(tabs) + L" has no tab to copy yet";
        return true;
    }
    if (registered) // left from an earlier page, or its page moved to another menu
    {
        Call(method(tabs, L"RemoveTab")).bytes(L"TabNameID", id_.data(), id_.size()).invoke(tabs);
    }
    std::wstring built;
    if (!page.show(model, options, built))
    {
        report = L"page: " + built;
        return false;
    }
    auto content = page.widget();
    auto& registerTab = method(tabs, L"RegisterTab");
    Call r(registerTab);
    r.bytes(L"TabNameID", id_.data(), id_.size())
        .set<UObject*>(L"ButtonWidgetType", tabModel->GetClassPrivate())
        .set<UObject*>(L"ContentWidget", content)
        .set<int32_t>(L"TabIndex", -1);
    if (!r.invoke(tabs) || !r.get<bool>(L"ReturnValue"))
    {
        report = L"RegisterTab refused the tab";
        return false;
    }
    // Whether RegisterTab places the page in the switcher depends on the CommonUI
    // version. AddChild also detaches it from an earlier menu's switcher.
    if (callObject(content, L"GetParent") != switcher)
    {
        callWith(switcher, L"AddChild", content);
    }
    if (callObject(content, L"GetParent") != switcher)
    {
        // Without a parent nothing keeps the page alive; retrying would loop.
        Call(method(tabs, L"RemoveTab")).bytes(L"TabNameID", id_.data(), id_.size()).invoke(tabs);
        report = L"the page could not be added to " + name(switcher);
        return false;
    }
    tabs_ = tabs;
    added = true;
    report = built;
    // The page takes the game list's place: the same padding and alignment as
    // the switcher child that holds it.
    auto holder = model;
    for (int depth = 0; live(holder) && depth < 8; ++depth)
    {
        auto parent = callObject(holder, L"GetParent");
        if (parent == switcher || !live(parent))
        {
            break;
        }
        holder = parent;
    }
    std::vector<SlotSetting> placement{{L"Padding", L"SetPadding"},
                                       {L"HorizontalAlignment", L"SetHorizontalAlignment"},
                                       {L"VerticalAlignment", L"SetVerticalAlignment"}};
    if (callObject(holder, L"GetParent") != switcher ||
        !readSlot(obj(holder, L"Slot"), placement) || !applySlot(obj(content, L"Slot"), placement))
    {
        report += L" The game list's placement was not copied.";
    }
    if (holder != model)
    {
        report += L" The game list sits in " + name(holder) + L".";
    }
    auto text = labelWidget(button(id_.data()), L"Voice Fix");
    report += text ? L" Label set on " + name(text) + L"." : L" The tab label could not be set.";
    return true;
}

bool MenuTab::selected()
{
    auto tabs = tabs_.Get();
    if (!live(tabs) || id_.empty())
    {
        return false;
    }
    auto& m = method(tabs, L"GetSelectedTabId");
    auto r = m.param(L"ReturnValue");
    Call c(m);
    return r && r->size == int(id_.size()) && c.invoke(tabs) &&
           std::memcmp(c.slot(L"ReturnValue"), id_.data(), id_.size()) == 0;
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
