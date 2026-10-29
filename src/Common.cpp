#include "Common.hpp"
#include <bcrypt.h>
#include <objbase.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <array>

namespace rv
{
static std::filesystem::path logPath;

std::filesystem::path modulePath(HMODULE module)
{
    std::wstring path(32768, L'\0');
    auto n = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    if (!n || n >= path.size())
    {
        return {};
    }
    path.resize(n);
    return path;
}

std::wstring sha256(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return {};
    }
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    {
        return {};
    }
    DWORD size{}, written{};
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                      sizeof(size), &written, 0);
    std::vector<UCHAR> object(size);
    bool ok = BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) >= 0;
    std::array<char, 65536> block{};
    while (ok && file)
    {
        file.read(block.data(), block.size());
        if (file.gcount())
        {
            ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(block.data()), ULONG(file.gcount()),
                                0) >= 0;
        }
    }
    if (file.bad())
    {
        ok = false;
    }
    UCHAR digest[32]{};
    if (ok)
    {
        ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    }
    if (hash)
    {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok)
    {
        return {};
    }
    std::wostringstream result;
    for (auto byte : digest)
    {
        result << std::hex << std::setfill(L'0') << std::setw(2) << unsigned(byte);
    }
    return result.str();
}

bool readable(const void* pointer, size_t length)
{
    auto p = reinterpret_cast<uintptr_t>(pointer);
    if (p < 65536 || !length || length > UINTPTR_MAX - p)
    {
        return false;
    }
    const auto end = p + length;
    while (p < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(p), &info, sizeof(info)))
        {
            return false;
        }
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        {
            return false;
        }
        if (!(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        {
            return false;
        }
        auto next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= p)
        {
            return false;
        }
        p = next;
    }
    return true;
}

std::wstring guidText(const GUID& guid)
{
    wchar_t text[40]{};
    StringFromGUID2(guid, text, 40);
    return text;
}

bool parseGuid(const std::wstring& text, GUID& guid)
{
    return SUCCEEDED(CLSIDFromString(text.c_str(), &guid));
}

void setLogPath(const std::filesystem::path& path)
{
    logPath = path;
}

void log(const std::wstring& message)
{
    auto line = L"[RVVoiceFix] " + message + L"\n";
    OutputDebugStringW(line.c_str());
    if (logPath.empty())
    {
        return;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, line.data(), int(line.size()), nullptr, 0, nullptr,
                                   nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), int(line.size()), utf8.data(), size, nullptr,
                        nullptr);
    std::ofstream file(logPath, std::ios::app | std::ios::binary);
    file << GetTickCount64() << ' ' << utf8;
}
} // namespace rv
