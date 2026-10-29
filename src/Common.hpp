#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
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
bool readable(const void* pointer, size_t length);

template <class T> bool read(const void* pointer, T& output)
{
    if (!readable(pointer, sizeof(T)))
    {
        return false;
    }
    std::memcpy(&output, pointer, sizeof(T));
    return true;
}

template <class T> T at(const void* pointer, size_t offset)
{
    T value{};
    if (pointer)
    {
        read(static_cast<const char*>(pointer) + offset, value);
    }
    return value;
}

void log(const std::wstring& message);
void setLogPath(const std::filesystem::path& path);

struct PointerArray
{
    void** data;
    int32_t num;
    int32_t max;
};

inline bool validArray(const PointerArray& array, int limit)
{
    return array.num >= 0 && array.num <= limit && array.max >= array.num && array.max <= 65536 &&
           (array.num == 0 || readable(array.data, size_t(array.num) * sizeof(void*)));
}

inline bool wasCapturing(int state)
{
    return state == 2 || state == 3;
}

inline bool switchable(int state)
{
    return state == 1 || wasCapturing(state);
}
} // namespace rv
