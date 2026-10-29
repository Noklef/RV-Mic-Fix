#pragma once
#include "Common.hpp"

namespace rv
{
struct Device
{
    GUID id{};
    std::wstring name;
};

std::vector<Device> enumerateDevices();
bool defaultCaptureGuid(GUID& guid);

struct SwitchResult
{
    bool ok;
    bool retry;
    std::wstring message;
};

// Read-only view of the game's single network voice capture.
struct CaptureInfo
{
    int count = 0;   // initialized captures with the expected vtable
    bool ok = false; // exactly one, identity readable
    GUID id{};
    int state = 0;
};

class CaptureControl
{
    uintptr_t base_ = 0;
    void* locate(void*& manager, int& count) const;

public:
    explicit CaptureControl(uintptr_t image = 0) : base_(image)
    {
    }

    SwitchResult apply(const Device& device);
    CaptureInfo current() const;
};

// When to re-apply a saved microphone after the game recreates capture.
// "Retry" refusals (busy capture) do not count as failures.
struct Reapply
{
    static constexpr unsigned MaxFailures = 3;
    static constexpr uint64_t Spacing = 5000;
    unsigned failures = 0;
    uint64_t next = 0;

    bool due(bool matchesSaved, uint64_t now)
    {
        if (matchesSaved)
        {
            failures = 0;
            return false;
        }
        return failures < MaxFailures && now >= next;
    }

    void result(const SwitchResult& r, uint64_t now)
    {
        failures = r.ok ? 0 : r.retry ? failures : failures + 1;
        next = now + Spacing;
    }

    void reset()
    {
        failures = 0;
        next = 0;
    }
};
} // namespace rv
