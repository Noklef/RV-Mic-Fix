#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rv
{
inline constexpr wchar_t GameHash[] =
    L"12b24146b30d550fe47a4179c7b05d1f21b69f058524e0bba4b41397e3e66221";
inline constexpr wchar_t LoaderHash[] =
    L"c273e7228687b647226fd63d7ca65888d313ece6be18996ec1320d00a5efe1f8";
std::wstring sha256(const std::filesystem::path& path);
std::filesystem::path modulePath(HMODULE module);
bool readable(const void* p, size_t n);

template <class T> bool read(const void* p, T& out)
{
    if (!readable(p, sizeof(T)))
    {
        return false;
    }
    memcpy(&out, p, sizeof(T));
    return true;
}

template <class T> T at(const void* p, size_t offset)
{
    T value{};
    if (p)
    {
        read(static_cast<const char*>(p) + offset, value);
    }
    return value;
}

std::wstring guidText(const GUID& guid);
bool parseGuid(const std::wstring& text, GUID& guid);
void log(const std::wstring& message);
void setLogPath(const std::filesystem::path& path);

struct PointerArray
{
    void** data;
    int32_t num;
    int32_t max;
};

inline bool validArray(const PointerArray& a, int limit)
{
    return a.num >= 0 && a.num <= limit && a.max >= a.num && a.max <= 65536 &&
           (a.num == 0 || readable(a.data, size_t(a.num) * sizeof(void*)));
}

inline bool wasCapturing(int state)
{
    return state == 2 || state == 3;
}

inline bool switchable(int state)
{
    return state == 1 || wasCapturing(state);
}

// No raw UObject identities survive a callback; these integers only detect changes.
struct Settler
{
    std::vector<uintptr_t> previous;
    uint64_t due = 0;
    unsigned remaining = 0;
    bool automatic = true;

    void schedule(uint64_t now)
    {
        remaining = 3;
        due = now + 3000;
    }

    void observe(std::vector<uintptr_t> signature, uint64_t now)
    {
        if (signature != previous)
        {
            previous = std::move(signature);
            due = 0;
            remaining = 0;
            if (automatic && !previous.empty())
            {
                schedule(now);
            }
        }
    }

    bool ready(uint64_t now)
    {
        if (!due || now < due)
        {
            return false;
        }
        if (--remaining)
        {
            due = now + 3500;
        }
        else
        {
            due = 0;
        }
        return true;
    }

    void toggle()
    {
        automatic = !automatic;
        due = 0;
        remaining = 0;
    }
};
} // namespace rv
