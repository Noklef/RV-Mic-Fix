#include "Devices.hpp"
#include <mmsystem.h>
#include <dsound.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <algorithm>

namespace rv
{
static BOOL CALLBACK collect(LPGUID id, LPCSTR description, LPCSTR, LPVOID context)
{
    if (!id || !description)
    {
        return TRUE; // Null GUID is the duplicate default alias.
    }
    int length = MultiByteToWideChar(CP_ACP, 0, description, -1, nullptr, 0);
    if (length <= 1)
    {
        return TRUE;
    }
    std::wstring name(length, L'\0');
    MultiByteToWideChar(CP_ACP, 0, description, -1, name.data(), length);
    name.pop_back();
    static_cast<std::vector<Device>*>(context)->push_back({*id, std::move(name)});
    return TRUE;
}

std::vector<Device> enumerateDevices()
{
    std::vector<Device> devices;
    DirectSoundCaptureEnumerateA(collect, &devices);
    return devices;
}

bool defaultCaptureGuid(GUID& guid)
{
    auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    {
        return false;
    }
    IMMDeviceEnumerator* enumerator{};
    IMMDevice* device{};
    IPropertyStore* store{};
    PROPVARIANT value{};
    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                         CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))) &&
              SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)) &&
              SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
              SUCCEEDED(store->GetValue(PKEY_AudioEndpoint_GUID, &value)) &&
              value.vt == VT_LPWSTR && value.pwszVal &&
              SUCCEEDED(CLSIDFromString(value.pwszVal, &guid));
    PropVariantClear(&value);
    if (store)
    {
        store->Release();
    }
    if (device)
    {
        device->Release();
    }
    if (enumerator)
    {
        enumerator->Release();
    }
    if (SUCCEEDED(initialized))
    {
        CoUninitialize();
    }
    return ok;
}

// Only this executable hash is permitted. RVAs/field offsets are from focused
// analysis of Ride 1.3.0.20702. See docs/NATIVE-EVIDENCE.md before updating them.
constexpr uintptr_t ManagerRva = 0xa6c7210;
constexpr uintptr_t CaptureVtableRva = 0x8c1fd18;
constexpr uintptr_t ChangeRva = 0x4e352e0;
constexpr uintptr_t HashRva = 0x1097ca0;
constexpr uintptr_t FindRva = 0x3077020;

struct EngineString
{
    const wchar_t* data;
    int32_t num;
    int32_t max;
};

static EngineString engineString(const std::wstring& text)
{
    return {text.c_str(), int32_t(text.size() + 1), int32_t(text.size() + 1)};
}

static int memoryFaultFilter(DWORD code)
{
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

// Small SEH boundary has no C++ objects to unwind. Never dereference a guessed
// capture: manager membership, vtable, state, and buffers are checked first.
static bool engineHasDevice(uintptr_t base, void* manager, EngineString* name, const GUID* expected)
{
    __try
    {
        auto hash = reinterpret_cast<uint32_t (*)(int32_t, const wchar_t*)>(base + HashRva);
        auto find = reinterpret_cast<int32_t (*)(void*, uint32_t, EngineString*)>(base + FindRva);
        auto map = static_cast<char*>(manager) + 0x58;
        auto index = find(map, hash(name->num - 1, name->data), name);
        if (index < 0 || index > 65535)
        {
            return false;
        }
        auto entries = *reinterpret_cast<char**>(map);
        auto id = reinterpret_cast<const GUID*>(entries + size_t(index) * 0x40 + 0x20);
        return IsEqualGUID(*id, *expected);
    }
    __except (memoryFaultFilter(GetExceptionCode()))
    {
        return false;
    }
}

static int changeNative(uintptr_t base, void* capture, EngineString* name, uint32_t rate,
                        uint32_t channels, bool start)
{
    __try
    {
        auto change =
            reinterpret_cast<bool (*)(void*, EngineString*, uint32_t, uint32_t)>(base + ChangeRva);
        if (!change(capture, name, rate, channels))
        {
            return 0;
        }
        if (start)
        {
            auto vtable = *static_cast<uintptr_t**>(capture);
            if (!reinterpret_cast<bool (*)(void*)>(vtable[3])(capture))
            {
                return 0;
            }
        }
        return 1;
    }
    __except (memoryFaultFilter(GetExceptionCode()))
    {
        return -1;
    }
}

static bool captureMatches(void* capture, const GUID& expected, uint32_t rate, uint16_t channels,
                           int previousState)
{
    GUID actual{};
    auto internal = at<void*>(capture, 0xa0);
    return read(internal, actual) && IsEqualGUID(actual, expected) &&
           at<uint32_t>(internal, 0x34) == rate && at<uint16_t>(internal, 0x32) == channels &&
           at<uint16_t>(internal, 0x3e) == 16 && at<void*>(internal, 0x10) &&
           at<void*>(internal, 0x28) &&
           (wasCapturing(previousState) ? wasCapturing(at<int>(capture, 0xb0))
                                        : at<int>(capture, 0xb0) == 1);
}

SwitchResult CaptureControl::apply(const Device& requested)
{
    if (!base_)
    {
        return {false, false, L"Unsupported executable; microphone switching is disabled."};
    }
    Device target = requested;
    auto devices = enumerateDevices();
    if (IsEqualGUID(target.id, GUID_NULL))
    {
        GUID current{};
        if (!defaultCaptureGuid(current))
        {
            return {false, false, L"Windows default microphone could not be resolved."};
        }
        target.id = current;
    }
    auto selected = std::find_if(devices.begin(), devices.end(),
                                 [&](auto& d) { return IsEqualGUID(d.id, target.id); });
    if (selected == devices.end())
    {
        return {false, false, L"Selected microphone is disconnected. Refresh the list."};
    }
    target = *selected;
    // Engine maps by description, not endpoint ID. Refuse duplicate descriptions.
    if (std::count_if(devices.begin(), devices.end(),
                      [&](auto& d) { return d.name == target.name; }) != 1)
    {
        return {false, false,
                L"Two microphones share this name. Rename one in Windows, then restart the game."};
    }
    void* manager = at<void*>(reinterpret_cast<void*>(base_), ManagerRva);
    PointerArray captures{};
    if (!read(manager, captures) || !validArray(captures, 16))
    {
        return {false, true, L"Voice capture is not ready; join a voice session first."};
    }
    void* capture{};
    int count = 0;
    for (int i = 0; i < captures.num; ++i)
    {
        void* candidate = captures.data[i];
        if (at<uintptr_t>(candidate, 0) == base_ + CaptureVtableRva &&
            at<int>(candidate, 0xb0) != 0 && at<void*>(candidate, 0xa0))
        {
            capture = candidate;
            ++count;
        }
    }
    if (count != 1)
    {
        return {
            false, true,
            count
                ? L"Multiple voice captures are active. Close the game's microphone test and retry."
                : L"No initialized network voice capture yet. Join a session and retry."};
    }
    int state = at<int>(capture, 0xb0);
    if (!switchable(state))
    {
        return {false, true, L"Voice capture is stopping. Retry once it settles."};
    }
    void* internal = at<void*>(capture, 0xa0);
    GUID previous{};
    if (!read(internal, previous))
    {
        return {false, false, L"Capture device identity is unavailable."};
    }
    auto old = std::find_if(devices.begin(), devices.end(),
                            [&](auto& d) { return IsEqualGUID(d.id, previous); });
    if (old == devices.end())
    {
        return {
            false, false,
            L"Current microphone is unavailable; cannot guarantee rollback. Reconnect it first."};
    }
    uint32_t rate = at<uint32_t>(internal, 0x34);
    uint16_t channels = at<uint16_t>(internal, 0x32);
    if (rate < 8000 || rate > 48000 || channels < 1 || channels > 2 ||
        at<uint16_t>(internal, 0x3e) != 16 || !at<void*>(internal, 0x10) ||
        !at<void*>(internal, 0x28))
    {
        return {false, false, L"Unexpected capture format; device was left unchanged."};
    }
    auto name = engineString(target.name);
    if (!engineHasDevice(base_, manager, &name, &target.id))
    {
        return {false, false,
                L"This microphone does not match the game's cached devices. Restart once to "
                L"register it."};
    }
    // Even selecting the current device reopens it: useful after an audio-driver reset.
    auto oldName = engineString(old->name);
    if (!engineHasDevice(base_, manager, &oldName, &previous))
    {
        return {false, false, L"Current device cannot be resolved for rollback."};
    }
    int changed = changeNative(base_, capture, &name, rate, channels, wasCapturing(state));
    if (changed == 1 && captureMatches(capture, target.id, rate, channels, state))
    {
        return {true, false,
                L"Capture reopened: " + target.name +
                    L". Verify transmitted audio with another player."};
    }
    if (changed < 0)
    {
        base_ = 0;
        return {false, false,
                L"Native capture fault; switching disabled. Capture may be unavailable; rejoin or "
                L"restart."};
    }
    int rollback = changeNative(base_, capture, &oldName, rate, channels, wasCapturing(state));
    if (rollback < 0)
    {
        base_ = 0;
    }
    bool restored = rollback == 1 && captureMatches(capture, previous, rate, channels, state);
    return {false, false,
            restored
                ? L"Switch failed; the previous microphone was restored."
                : L"Switch and rollback failed. Rejoin the voice session or restart the game."};
}
} // namespace rv
