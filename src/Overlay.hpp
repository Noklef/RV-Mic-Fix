#pragma once
#include "Devices.hpp"
#include "Model.hpp"
#include "Prefs.hpp"
#include <string_view>

namespace rv
{
// sRGB colour; the widget layer converts to linear for the engine.
struct Color
{
    float r, g, b, a;
};

enum class UiKind
{
    None,
    Close,
    SelectDevice,
    RefreshDevices,
    SetVolume,
    ToggleMute,
    ToggleAuto,
    RepairNow,
    Diagnose,
    ToggleDetails
};

struct UiAction
{
    UiKind kind = UiKind::None;
    Device device{};
    std::wstring player;
    int value = 0;
};

struct OverlayModel
{
    const std::vector<Device>& devices; // [0] is "Windows default" (GUID_NULL)
    const VoiceStatus& voice;
    const Prefs& prefs;
    GUID activeDevice{};
    bool activeKnown = false;
    std::wstring micStatus;
    bool details = false;
    std::wstring status;
};

enum class ItemKind
{
    Title,
    Heading,
    Text,
    Small,
    Button,
    Slider
};

enum class Tone
{
    Ink,
    Dim,
    Accent,
    Good,
    Bad,
    Warn,
    Selected // buttons: chosen or switched on
};

// One widget of the panel. Width is in 1080p layout units (UMG applies the
// viewport DPI scale); 0 keeps the natural size.
struct Item
{
    ItemKind kind = ItemKind::Text;
    std::wstring text;
    Tone tone = Tone::Ink;
    float width = 0;
    UiAction action{}; // Button: emitted on click. Slider: SetVolume target.
    int value = 0;     // Slider: percent
};

// Items in one row are laid out left to right.
using Row = std::vector<Item>;

enum class OptionKind
{
    Heading,
    Slider,
    Stepper,
    Button
};

// One row of the game's own option widgets (the Settings "Voice Fix" tab).
struct Option
{
    OptionKind kind = OptionKind::Button;
    std::wstring label;
    std::wstring button;               // Button: its caption
    std::vector<std::wstring> choices; // Stepper
    int value = 0;                     // Slider: percent. Stepper: the selected choice
    std::vector<UiAction> actions;     // Stepper: one per choice. Slider, Button: one
    bool sub = false;                  // indented under the row above
};

// Panel content for the current state. Pure; the widget layer only maps it.
std::vector<Row> describe(const OverlayModel& model);
// Settings-tab rows: controls only; diagnostics stay in the overlay. Pure.
std::vector<Option> describeOptions(const OverlayModel& model);
// Kinds, labels and choices. Values are updated in place; a different shape rebuilds.
std::wstring optionShape(const std::vector<Option>& options);
// The small notice shown while the panel is closed.
std::vector<Row> describeNotice(std::wstring_view text);
// Structure only (kinds, widths, actions). Text and tones are updated in place,
// slider positions are owned by the widget; a different shape rebuilds them.
std::wstring shape(const std::vector<Row>& rows);
// Slider position to a saved percent in 5% steps (the slider moves in the same steps).
int snapVolume(double raw);
Color color(Tone tone);
// Background colour of a button with this tone.
Color buttonColor(Tone tone);
Color panelColor();

constexpr size_t MaxDevices = 12;
std::wstring fit(std::wstring_view text, size_t maxChars);
} // namespace rv
