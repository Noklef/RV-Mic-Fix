#pragma once
#include "Overlay.hpp"
#include "Reflection.hpp"

namespace rv
{
// Painter over the HUD canvas. Valid only inside AHUD::ReceiveDrawHUD, when
// the engine has assigned the canvas.
class HudPainter final : public Painter
{
    RC::Unreal::UObject* hud_;

public:
    explicit HudPainter(RC::Unreal::UObject* hud);
    bool ready() const;
    void rect(float x, float y, float w, float h, Color c) override;
    void text(std::wstring_view s, float x, float y, Color c, float scale) override;
    float lineHeight() override;
    float charWidth() override;
};

// While the overlay is open: the viewport ignores game input and the cursor is
// free. Close restores the previous cursor/input style.
class InputControl
{
    bool open_ = false;
    bool cursorWasVisible_ = false;
    bool hudForced_ = false;

public:
    static RC::Unreal::UObject* controller(RC::Unreal::UEngine* engine);
    static RC::Unreal::UObject* hud(RC::Unreal::UEngine* engine);
    std::wstring open(RC::Unreal::UEngine* engine);
    std::wstring close(RC::Unreal::UEngine* engine);
};
} // namespace rv
