#pragma once
#include "Common.hpp"
namespace rv {
struct Device { GUID id{}; std::wstring name; };
std::vector<Device> enumerateDevices();
bool defaultCaptureGuid(GUID& guid);
struct SwitchResult { bool ok; bool retry; std::wstring message; };
class CaptureControl {
    uintptr_t base_ = 0;
public:
    explicit CaptureControl(uintptr_t image = 0) : base_(image) {}
    SwitchResult apply(const Device& device);
};
}
