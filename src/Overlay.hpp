#pragma once
#include "Model.hpp"
#include "Prefs.hpp"
#include <string_view>

namespace rv
{
// sRGB colour; the HUD painter converts to linear for the engine canvas.
struct Color
{
    float r, g, b, a;
};

class Painter
{
public:
    virtual ~Painter() = default;
    virtual void rect(float x, float y, float w, float h, Color c) = 0;
    // Top-left anchored text at the given scale.
    virtual void text(std::wstring_view s, float x, float y, Color c, float scale) = 0;
    // Metrics of the font at scale 1.
    virtual float lineHeight() = 0;
    virtual float charWidth() = 0;
};

struct Pointer
{
    float x = 0;
    float y = 0;
    bool down = false;
    bool valid = false;
};

enum class UiKind
{
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
    UiKind kind;
    Device device{};
    std::wstring player;
    int value = 0;
};

struct OverlayModel
{
    const std::vector<Device>* devices{}; // [0] is "Windows default" (GUID_NULL)
    GUID activeDevice{};
    bool activeKnown = false;
    std::wstring micStatus;
    const VoiceStatus* voice{};
    const Prefs* prefs{};
    bool details = false;
    std::wstring status;
};

struct Hit
{
    std::wstring id;
    float x, y, w, h;
};

// Immediate-mode in-game panel. All coordinates are canvas pixels.
class Overlay
{
    bool wasDown_ = false;
    std::wstring dragging_;
    int lastDragValue_ = -1;
    std::vector<Hit> hits_;

public:
    static constexpr size_t MaxDevices = 12;
    std::vector<UiAction> frame(Painter& painter, float width, float height, Pointer pointer,
                                const OverlayModel& model);
    // Small notice shown while the panel is closed.
    static void notice(Painter& painter, float width, float height, std::wstring_view text,
                       float alpha);

    const std::vector<Hit>& hits() const
    {
        return hits_;
    }
};

std::wstring fit(std::wstring_view text, size_t maxChars);
} // namespace rv
