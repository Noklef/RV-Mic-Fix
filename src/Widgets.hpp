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

// Option rows in the game's own list widget (RGListView), so they look like the
// game's settings. The game draws each row from a plain item object; values are
// read back from the items, and button clicks from the entry widgets on screen.
// Each heading starts a section: a game heading widget, then its own list.
class OptionPage
{
    struct Bound
    {
        Option option;
        RC::Unreal::FWeakObjectPtr item{};
        size_t index = 0;     // in the options the page was built from
        size_t section = 0;   // in sections_
        int reported = 0;     // Slider, Stepper: last value read or set
        int pending = -1;     // Stepper: a microphone choice waiting to settle
        uint64_t due = 0;     // when it applies
        bool pressed = false; // Button
    };

    // A list view sizes to its rows only in a fixed height: in an auto-sized
    // slot it generates one row. The box height follows the measured rows.
    struct Section
    {
        RC::Unreal::FWeakObjectPtr list{};
        RC::Unreal::FWeakObjectPtr box{}; // SizeBox
        int rows = 0;
    };

    RC::Unreal::FWeakObjectPtr root_{};  // ScrollBox
    RC::Unreal::FWeakObjectPtr model_{}; // the game list the section lists copy
    std::vector<Section> sections_;
    double rowHeight_ = 60; // layout units; grows to the tallest row shown
    std::wstring shape_;
    std::vector<Bound> bound_;

    void update(const std::vector<Option>& options);
    bool resize(); // false when a box height could not be set

public:
    // Builds the page from model (a game option list) the first time, fills or
    // updates it. model may be null once given. report receives a log line
    // after a build.
    bool show(RC::Unreal::UObject* model, const std::vector<Option>& options, std::wstring& report);
    // Value changes and button clicks since the last poll; now is in ms.
    std::vector<UiAction> poll(uint64_t now);
    RC::Unreal::UObject* widget();
};

// A "Voice Fix" tab in the game's options menu. Its page is an OptionPage placed
// in the menu's tab switcher.
class MenuTab
{
    RC::Unreal::FWeakObjectPtr tabs_{};
    std::vector<std::byte> id_; // the tab's FName

public:
    OptionPage page;
    // Registers the tab with tabs (a CommonTabListWidgetBase) unless it has it.
    // model is the menu's own option list, copied for the page. added is set on a
    // new registration; report names a failure or a cosmetic problem. True with
    // added unset and a report: the menu is not ready yet.
    bool attach(RC::Unreal::UObject* tabs, RC::Unreal::UObject* model,
                const std::vector<Option>& options, bool& added, std::wstring& report);
    // The tab is the one selected in its menu.
    bool selected();
};

// Last reflected class or function the widget code used, for fault reports.
const wchar_t* lastUmgStep();
// Sets the text of a game TextBlock (or subclass). False when it is not one.
bool setWidgetText(RC::Unreal::UObject* block, const std::wstring& text);
} // namespace rv
