#pragma once
#include "Overlay.hpp"
#include "Reflection.hpp"

namespace rv
{
// Panel rows as UMG widgets in the game viewport, built through reflection on
// the game thread. Only weak pointers are held between ticks: while the user
// widget is in the viewport, Slate keeps it and its widget tree alive.
class Screen
{
    struct Bound
    {
        Item item;
        RC::Unreal::FWeakObjectPtr widget{}; // Button, Slider or TextBlock
        RC::Unreal::FWeakObjectPtr label{};  // TextBlock: the text or button label
        bool pressed = false;
        int reported = 0; // Slider: last percent read or set
    };

    RC::Unreal::FWeakObjectPtr root_{};
    bool built_ = false;
    std::wstring shape_;
    std::vector<Bound> bound_;

    void update(const std::vector<Row>& rows);

public:
    // Builds the widgets, or updates them in place when the shape is unchanged.
    // False when UMG is unusable. report receives a log line after a build.
    bool show(RC::Unreal::UObject* controller, const std::vector<Row>& rows, bool interactive,
              double x, double y, std::wstring& report);
    // Button clicks and slider changes since the last poll.
    std::vector<UiAction> poll();
    void hide();
    // Built, but no longer in the viewport (for example after travel).
    bool lost();
};

// "RV Voice Fix" buttons added to game menu panels (Settings -> Sound). Each menu's
// widget tree owns its button; only weak pointers are held, so a rebuilt menu
// gets a new button.
class MenuButtons
{
    struct Entry
    {
        RC::Unreal::FWeakObjectPtr panel{};
        RC::Unreal::FWeakObjectPtr button{};
        bool pressed = false;
    };

    std::vector<Entry> entries_;

public:
    // Adds a button to panel, a panel in menu's widget tree, unless it has one.
    // added is set when a new button was built; report names a failure.
    bool attach(RC::Unreal::UObject* menu, RC::Unreal::UObject* panel, bool& added,
                std::wstring& report);
    // True once per click on any of the buttons.
    bool clicked();
};

// Last reflected class or function the widget code used, for fault reports.
const wchar_t* lastUmgStep();
// Sets the text of a game TextBlock (or subclass). False when it is not one.
bool setWidgetText(RC::Unreal::UObject* block, const std::wstring& text);
} // namespace rv
