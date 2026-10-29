#include "Reflection.hpp"

namespace rv::ue
{
using namespace RC::Unreal;

static bool alive(UObject* p)
{
    constexpr auto rejected =
        uint32_t(EObjectFlags::BeginDestroyed) | uint32_t(EObjectFlags::FinishDestroyed);
    return readable(p, 40) && !(uint32_t(p->GetObjectFlags()) & rejected) && !p->IsUnreachable();
}

static bool identity(UObject* p)
{
    constexpr auto templates =
        uint32_t(EObjectFlags::ClassDefaultObject) | uint32_t(EObjectFlags::ArchetypeObject);
    return alive(p) && !(uint32_t(p->GetObjectFlags()) & templates);
}

static bool markedGarbage(UObject* p)
{
    return (uint32_t(p->GetObjectFlags()) & uint32_t(EObjectFlags::MirroredGarbage)) != 0;
}

bool live(UObject* p)
{
    return identity(p) && !markedGarbage(p);
}

bool garbage(UObject* p)
{
    return identity(p) && markedGarbage(p);
}

bool callable(UObject* p)
{
    return alive(p) && !markedGarbage(p);
}

std::wstring name(UObject* p)
{
    return identity(p) ? p->GetFullName(nullptr) : L"<none>";
}

UObject* worldOf(UObject* p)
{
    return live(p) ? reinterpret_cast<UObject*>(p->GetWorld()) : nullptr;
}

UObject* find(const wchar_t* path)
{
    auto p = UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, path, false);
    return callable(p) ? p : nullptr;
}

Field field(UObject* object, const wchar_t* key)
{
    if (!live(object))
    {
        return {};
    }
    auto p = object->GetPropertyByNameInChain(key);
    if (!p)
    {
        return {};
    }
    return {p, object->GetValuePtrByPropertyNameInChain(key)};
}

static FProperty* child(UStruct* structure, std::wstring_view key)
{
    if (!structure)
    {
        return nullptr;
    }
    int remaining = 256;
    for (auto p = structure->GetChildProperties(); p && remaining--; p = p->next())
    {
        if (p->GetName() == key)
        {
            return static_cast<FProperty*>(p);
        }
    }
    return nullptr;
}

Field member(Field structure, const wchar_t* key)
{
    if (!structure || structure.property->kind() != L"StructProperty")
    {
        return {};
    }
    auto type = reinterpret_cast<UStruct*>(
        static_cast<FStructProperty*>(structure.property)->GetStruct().pointer);
    if (!readable(type, 40) || type->GetPropertiesSize() != structure.property->GetSize())
    {
        return {};
    }
    auto p = child(type, key);
    if (!p)
    {
        return {};
    }
    int offset = p->GetOffset_Internal();
    int size = p->GetSize();
    if (offset < 0 || size < 1 || offset + size > structure.property->GetSize())
    {
        return {};
    }
    return {p, static_cast<char*>(structure.address) + offset};
}

UObject* objectValue(Field f)
{
    if (!f || f.property->GetSize() != 8)
    {
        return nullptr;
    }
    auto kind = f.property->kind();
    UObject* result{};
    if (kind == L"WeakObjectProperty")
    {
        result = static_cast<FWeakObjectPtr*>(f.address)->Get();
    }
    else if (kind == L"ObjectProperty" || kind == L"ObjectPtrProperty")
    {
        result = static_cast<FObjectPropertyBase*>(f.property)->GetObjectPropertyValue(f.address);
    }
    return live(result) ? result : nullptr;
}

bool setObject(Field f, UObject* value)
{
    if (!f || f.property->GetSize() != 8 || (value && !live(value)))
    {
        return false;
    }
    auto kind = f.property->kind();
    if (kind == L"WeakObjectProperty")
    {
        *static_cast<FWeakObjectPtr*>(f.address) = value;
    }
    else if (kind == L"ObjectProperty" || kind == L"ObjectPtrProperty")
    {
        static_cast<FObjectPropertyBase*>(f.property)->SetObjectPropertyValue(f.address, value);
    }
    else
    {
        return false;
    }
    return objectValue(f) == value;
}

UObject* obj(UObject* object, const wchar_t* key)
{
    return objectValue(field(object, key));
}

// FBoolProperty value address is container + offset; ByteOffset/FieldMask
// select the bit (FieldMask 0xff for a native bool).
static uint8_t* boolByte(Field f, uint8_t& mask)
{
    if (!f || f.property->kind() != L"BoolProperty")
    {
        return nullptr;
    }
    auto b = static_cast<FBoolProperty*>(f.property);
    mask = b->GetFieldMask();
    auto byte = static_cast<uint8_t*>(f.address) + b->GetByteOffset();
    return mask && b->GetByteOffset() < f.property->GetSize() && readable(byte, 1) ? byte : nullptr;
}

bool boolValue(Field f, bool& value)
{
    uint8_t mask{};
    auto byte = boolByte(f, mask);
    if (!byte)
    {
        return false;
    }
    value = (*byte & mask) != 0;
    return true;
}

bool setBool(Field f, bool value)
{
    uint8_t mask{};
    auto byte = boolByte(f, mask);
    if (!byte)
    {
        return false;
    }
    *byte = value ? uint8_t(*byte | mask) : uint8_t(*byte & ~mask);
    return true;
}

bool realValue(Field f, double& value)
{
    if (!f)
    {
        return false;
    }
    auto kind = f.property->kind();
    if (kind == L"FloatProperty" && f.property->GetSize() == 4)
    {
        value = *static_cast<float*>(f.address);
        return true;
    }
    if (kind == L"DoubleProperty" && f.property->GetSize() == 8)
    {
        value = *static_cast<double*>(f.address);
        return true;
    }
    return false;
}

std::wstring stringValue(Field f)
{
    ParamString s{};
    if (!f || f.property->kind() != L"StrProperty" || f.property->GetSize() != sizeof(s) ||
        !read(f.address, s) || s.num < 2 || s.num > 256 || s.max < s.num ||
        !readable(s.data, size_t(s.num) * sizeof(wchar_t)))
    {
        return {};
    }
    return std::wstring(s.data, size_t(s.num - 1));
}

bool pointerArray(Field f, PointerArray& array, int limit)
{
    return f && f.property->kind() == L"ArrayProperty" &&
           f.property->GetSize() == sizeof(PointerArray) && read(f.address, array) &&
           validArray(array, limit);
}

Method::Method(UObject* target, const wchar_t* functionName)
{
    if (!live(target))
    {
        return;
    }
    auto f = target->GetFunctionByNameInChain(functionName);
    if (!f)
    {
        return;
    }
    int size = f->GetParmsSize();
    int remaining = 64;
    // Blueprint functions also list locals after the parameters; keep only
    // properties inside the parameter block.
    for (auto p = f->GetChildProperties(); p && remaining--; p = p->next())
    {
        auto property = static_cast<FProperty*>(p);
        int offset = property->GetOffset_Internal();
        int length = property->GetSize();
        if (offset >= 0 && length > 0 && offset + length <= size)
        {
            params_.push_back({p->GetName(), {offset, length, p->kind()}});
        }
    }
    function_ = f;
    size_ = size;
}

const Param* Method::param(std::wstring_view key) const
{
    for (auto& [name, p] : params_)
    {
        if (name == key)
        {
            return &p;
        }
    }
    return nullptr;
}

ParamString paramString(const std::wstring& text)
{
    if (text.empty())
    {
        return {nullptr, 0, 0};
    }
    return {text.c_str(), int32_t(text.size() + 1), int32_t(text.size() + 1)};
}

Call& Call::real(std::wstring_view key, double value)
{
    auto p = method_.param(key);
    if (p && p->size == 4)
    {
        return set(key, float(value));
    }
    return set(key, value);
}

bool Call::invoke(UObject* target)
{
    if (!ok_ || !callable(target))
    {
        return false;
    }
    target->ProcessEvent(method_.function(), buffer_.data());
    return true;
}

double Call::getReal(std::wstring_view key) const
{
    auto p = method_.param(key);
    if (p && p->size == 4)
    {
        return get<float>(key);
    }
    return get<double>(key);
}

UObject* callObject(UObject* target, const wchar_t* method)
{
    Method m(target, method);
    if (!m || m.count() != 1)
    {
        return nullptr;
    }
    Call call(m);
    if (!call.invoke(target))
    {
        return nullptr;
    }
    auto p = call.get<UObject*>(L"ReturnValue");
    return live(p) ? p : nullptr;
}

bool callBool(UObject* target, const wchar_t* method, bool& result)
{
    Method m(target, method);
    auto r = m ? m.param(L"ReturnValue") : nullptr;
    if (!r || m.count() != 1 || r->size != 1)
    {
        return false;
    }
    Call call(m);
    if (!call.invoke(target))
    {
        return false;
    }
    result = call.get<bool>(L"ReturnValue");
    return true;
}
} // namespace rv::ue
