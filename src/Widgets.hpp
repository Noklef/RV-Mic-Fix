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
} // namespace rv
