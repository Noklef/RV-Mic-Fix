#pragma once
// Narrow ABI declarations for experimental UE4SS cdbc0b66, MSVC release /MD, x64.
// CppUserModBase layout/vtable follows the public MIT-licensed header at that
// commit. Opaque Unreal objects are NEVER constructed, copied, or sized here.
// This is deliberately release-pinned, not a replacement general-purpose SDK.
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#define RV_IMPORT __declspec(dllimport)

namespace RC
{
namespace GUI
{
class GUITab;
}

namespace LuaMadeSimple
{
class Lua;
} // namespace LuaMadeSimple

class CppUserModBase
{
protected:
    std::vector<std::shared_ptr<GUI::GUITab>> GUITabs;

public:
    std::wstring ModName;
    std::wstring ModVersion;
    std::wstring ModDescription;
    std::wstring ModAuthors;
    std::wstring ModIntendedSDKVersion;
    RV_IMPORT CppUserModBase();
    RV_IMPORT virtual ~CppUserModBase();

    virtual void on_update()
    {
    }

    virtual void on_unreal_init()
    {
    }

    virtual void on_ui_init()
    {
    }

    virtual void on_program_start()
    {
    }

    virtual void on_lua_start(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                              LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&)
    {
    }

    virtual void on_lua_start(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                              std::vector<LuaMadeSimple::Lua*>&)
    {
    }

    virtual void on_lua_stop(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                             LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&)
    {
    }

    virtual void on_lua_stop(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                             std::vector<LuaMadeSimple::Lua*>&)
    {
    }

    virtual void on_dll_load(std::wstring_view)
    {
    }

    virtual void render_tab()
    {
    }

    virtual void on_lua_start(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                              LuaMadeSimple::Lua&, LuaMadeSimple::Lua*)
    {
    }

    virtual void on_lua_start(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                              LuaMadeSimple::Lua*)
    {
    }

    virtual void on_lua_stop(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                             LuaMadeSimple::Lua&, LuaMadeSimple::Lua*)
    {
    }

    virtual void on_lua_stop(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&,
                             LuaMadeSimple::Lua*)
    {
    }

    virtual void on_cpp_mods_loaded()
    {
    }
};

static_assert(sizeof(CppUserModBase) == 192);

namespace Unreal
{
class UObject;
class UClass;
class UFunction;
class UScriptStruct;
class UWorld;
class UEngine;
class AActor;
class FProperty;
class FFieldClass;
// UE5.6 UObject/ObjectMacros.h; values also listed in UE4SS EObjectFlags docs.
// These flags reject templates, objects whose destruction has begun, and
// objects already marked as garbage (destroyed actors/components awaiting GC).
enum class EObjectFlags : uint32_t
{
    ClassDefaultObject = 0x00000010,
    ArchetypeObject = 0x00000020,
    BeginDestroyed = 0x00008000,
    FinishDestroyed = 0x00010000,
    MirroredGarbage = 0x40000000
};

// Layout-compatible with UE4SS TObjectPtr (one raw pointer); declared only so
// FStructProperty::GetStruct mangles to the exported name.
template <class T> class TObjectPtr
{
public:
    T* pointer;
};

class UObjectBase
{
public:
    RV_IMPORT EObjectFlags& GetObjectFlags();
    RV_IMPORT UObject*& GetOuterPrivate();
    RV_IMPORT UClass*& GetClassPrivate();
};

class UObject : public UObjectBase
{
public:
    RV_IMPORT bool IsUnreachable();
    RV_IMPORT std::wstring GetFullName(UObject*) const;
    RV_IMPORT UWorld* GetWorld() const;
    RV_IMPORT FProperty* GetPropertyByNameInChain(const wchar_t*);
    RV_IMPORT void* GetValuePtrByPropertyNameInChain(const wchar_t*);
    RV_IMPORT UFunction* GetFunctionByNameInChain(const wchar_t*);
    RV_IMPORT void ProcessEvent(UFunction*, void*);
};

class FFieldClass
{
public:
    RV_IMPORT std::wstring GetName() const;
};

class FField
{
    RV_IMPORT FField*& GetNext();
    RV_IMPORT FFieldClass*& GetClassPrivate();

public:
    RV_IMPORT std::wstring GetName();

    FField* next()
    {
        return GetNext();
    }

    std::wstring kind()
    {
        auto c = GetClassPrivate();
        return c ? c->GetName() : L"";
    }
};

class FProperty : public FField
{
public:
    RV_IMPORT int& GetOffset_Internal();
    RV_IMPORT int GetSize();
    // Runs the engine destructor for one value of this property type.
    RV_IMPORT void DestroyValue(void*);
    // Runs the engine constructor (vtable pointer included) for one value.
    RV_IMPORT void InitializeValue(void*) const;
    // Copy-assigns one value of this property type (reference counts included).
    RV_IMPORT void CopyCompleteValue(void* dest, const void* src) const;
};

class FObjectPropertyBase : public FProperty
{
public:
    RV_IMPORT UObject* GetObjectPropertyValue(const void*) const;
};

class FStructProperty : public FProperty
{
public:
    RV_IMPORT TObjectPtr<UScriptStruct>& GetStruct();
};

class FBoolProperty : public FProperty
{
public:
    RV_IMPORT uint8_t& GetByteOffset();
    RV_IMPORT uint8_t& GetFieldMask();
};

class UStruct : public UObject
{
public:
    RV_IMPORT FField*& GetChildProperties();
    RV_IMPORT int& GetPropertiesSize();
};

class UClass : public UStruct
{
public:
    RV_IMPORT uint32_t& GetClassFlags();
};

class UFunction : public UStruct
{
public:
    RV_IMPORT unsigned short& GetParmsSize();
    RV_IMPORT unsigned short& GetReturnValueOffset();
};

struct FWeakObjectPtr
{
    int32_t index;
    int32_t serial;
    RV_IMPORT UObject* Get() const;
    RV_IMPORT void operator=(UObject*);
};

namespace UObjectGlobals
{
RV_IMPORT void FindAllOf(const wchar_t*, std::vector<UObject*>&);
RV_IMPORT UObject* StaticFindObject_InternalSlow(UClass*, UObject*, const wchar_t*, bool);
} // namespace UObjectGlobals

namespace Hook
{
RV_IMPORT void RegisterEngineTickPostCallback(std::function<void(UEngine*, float)>);
} // namespace Hook
} // namespace Unreal
} // namespace RC
