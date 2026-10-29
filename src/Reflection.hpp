#pragma once
#include "Common.hpp"
#include "Ue4ssAbi.hpp"
#include <array>
#include <string_view>

// Reflection helpers shared by the voice repair, overlay widgets, and input control.
// Everything here runs on the game thread. Objects returned are only valid
// within the callback that obtained them, except UFunctions of native /Script
// classes, which are never unloaded.
namespace rv::ue
{
using RC::Unreal::FProperty;
using RC::Unreal::UFunction;
using RC::Unreal::UObject;

// Readable, not a template, not being destroyed, not garbage.
bool live(UObject* p);
// Same identity checks, but true for objects already marked as garbage.
bool garbage(UObject* p);
// Live, or a class default object (used for static library calls).
bool callable(UObject* p);
std::wstring name(UObject* p);
UObject* worldOf(UObject* p);
// Exact object path, e.g. /Script/UMG.Default__WidgetBlueprintLibrary. May
// return a class default object.
UObject* find(const wchar_t* path);

struct Field
{
    FProperty* property{};
    void* address{};

    explicit operator bool() const
    {
        return property && address;
    }
};

Field field(UObject* object, const wchar_t* key);
// Member of a struct-valued field; the type comes from the property itself.
Field member(Field structure, const wchar_t* key);
UObject* objectValue(Field f);
bool setObject(Field f, UObject* value);
UObject* obj(UObject* object, const wchar_t* key);
bool boolValue(Field f, bool& value);
bool setBool(Field f, bool value);
bool realValue(Field f, double& value);
bool setReal(Field f, double value);
std::wstring stringValue(Field f);
// Valid pointer array property (TArray<UObject*>), bounded.
bool pointerArray(Field f, PointerArray& array, int limit);

struct Param
{
    int offset = 0;
    int size = 0;
    std::wstring kind;
    FProperty* property{};
};

// A function and its parameter layout, resolved by name from a live object.
class Method
{
    UFunction* function_{};
    int size_ = 0;
    std::vector<std::pair<std::wstring, Param>> params_;

public:
    static constexpr int MaxParms = 512;
    Method() = default;
    Method(UObject* target, const wchar_t* functionName);

    explicit operator bool() const
    {
        return function_ != nullptr;
    }

    UFunction* function() const
    {
        return function_;
    }

    int size() const
    {
        return size_;
    }

    size_t count() const
    {
        return params_.size();
    }

    const std::pair<std::wstring, Param>& at(size_t i) const
    {
        return params_.at(i);
    }

    const Param* param(std::wstring_view key) const;
};

// Engine-compatible FString view for by-value/const-ref parameters. The engine
// copies it; it never frees or resizes caller-owned parameter memory.
struct ParamString
{
    const wchar_t* data;
    int32_t num;
    int32_t max;
};

ParamString paramString(const std::wstring& text);

// One ProcessEvent call. Any failed set() makes invoke() refuse to call.
class Call
{
    const Method& method_;
    alignas(8) std::array<std::byte, Method::MaxParms> buffer_{};
    bool ok_;

public:
    explicit Call(const Method& method)
        : method_(method), ok_(method && method.size() <= Method::MaxParms)
    {
    }

    explicit Call(Method&&) = delete; // The layout must outlive the call.

    template <class T> Call& set(std::wstring_view key, const T& value)
    {
        auto p = method_.param(key);
        if (!p || p->size != int(sizeof(T)))
        {
            ok_ = false;
            return *this;
        }
        std::memcpy(buffer_.data() + p->offset, &value, sizeof(T));
        return *this;
    }

    // Writes a parameter of runtime size, e.g. an engine struct copied from a property.
    Call& bytes(std::wstring_view key, const void* data, size_t size);
    // Writes a float or double parameter, whichever the engine declared.
    Call& real(std::wstring_view key, double value);
    // Address of a parameter inside the buffer, or null.
    void* slot(std::wstring_view key);
    bool invoke(UObject* target);

    template <class T> T get(std::wstring_view key) const
    {
        T value{};
        auto p = method_.param(key);
        if (p && p->size == int(sizeof(T)))
        {
            std::memcpy(&value, buffer_.data() + p->offset, sizeof(T));
        }
        return value;
    }

    double getReal(std::wstring_view key) const;
};

// Common zero-argument queries returning an object or bool.
UObject* callObject(UObject* target, const wchar_t* method);
bool callBool(UObject* target, const wchar_t* method, bool& result);
} // namespace rv::ue
