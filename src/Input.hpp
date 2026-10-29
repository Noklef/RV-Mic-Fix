#pragma once
#include "Reflection.hpp"

namespace rv
{
// While the overlay is open: the viewport ignores game input and the cursor is
// free. Close restores the previous cursor/input style.
class InputControl
{
    bool open_ = false;
    bool cursorWasVisible_ = false;

public:
    static RC::Unreal::UObject* controller(RC::Unreal::UEngine* engine);
    std::wstring open(RC::Unreal::UEngine* engine);
    std::wstring close(RC::Unreal::UEngine* engine);
};
} // namespace rv
