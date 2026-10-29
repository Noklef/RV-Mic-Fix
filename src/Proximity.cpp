#include "Proximity.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace rv {
using namespace RC::Unreal;
static bool live(UObject* p) {
    constexpr auto rejected = uint32_t(EObjectFlags::ClassDefaultObject) | uint32_t(EObjectFlags::ArchetypeObject)
        | uint32_t(EObjectFlags::BeginDestroyed) | uint32_t(EObjectFlags::FinishDestroyed);
    return readable(p, 40) && !(uint32_t(p->GetObjectFlags()) & rejected) && !p->IsUnreachable();
}
static std::wstring name(UObject* p) { return live(p) ? p->GetFullName(nullptr) : L"<none>"; }
struct Field { FProperty* property{}; void* address{}; };
static UObject* objectValue(Field f) {
    if (!f.property || !f.address || f.property->GetSize() != 8) return nullptr;
    auto kind = f.property->kind();
    UObject* result{};
    if (kind == L"WeakObjectProperty") result = static_cast<FWeakObjectPtr*>(f.address)->Get();
    else if (kind == L"ObjectProperty" || kind == L"ObjectPtrProperty")
        result = static_cast<FObjectPropertyBase*>(f.property)->GetObjectPropertyValue(f.address);
    return live(result) ? result : nullptr;
}
static bool setObject(Field f, UObject* value) {
    if (!f.property || !f.address || f.property->GetSize() != 8 || (value && !live(value))) return false;
    auto kind = f.property->kind();
    if (kind == L"WeakObjectProperty") *static_cast<FWeakObjectPtr*>(f.address) = value;
    else if (kind == L"ObjectProperty" || kind == L"ObjectPtrProperty")
        static_cast<FObjectPropertyBase*>(f.property)->SetObjectPropertyValue(f.address, value);
    else return false;
    return objectValue(f) == value;
}
static Field field(UObject* object, const wchar_t* key) {
    if (!live(object)) return {};
    auto p = object->GetPropertyByNameInChain(key);
    if (!p) return {};
    return {p, object->GetValuePtrByPropertyNameInChain(key)};
}
static UObject* obj(UObject* object, const wchar_t* key) { return objectValue(field(object, key)); }
static FProperty* child(UStruct* structure, const wchar_t* key) {
    if (!structure) return nullptr;
    int remaining = 256;
    for (auto p = structure->GetChildProperties(); p && remaining--; p = p->next())
        if (p->GetName() == key) return static_cast<FProperty*>(p);
    return nullptr;
}
static Field member(Field structure, UStruct* type, const wchar_t* key) {
    if (!structure.property || !structure.address || structure.property->kind() != L"StructProperty") return {};
    auto p = child(type, key);
    if (!p) return {};
    int offset = p->GetOffset_Internal(), size = p->GetSize();
    if (offset < 0 || size < 1 || offset + size > structure.property->GetSize()) return {};
    return {p, static_cast<char*>(structure.address) + offset};
}
template<class T> static T callReturn(UObject* target, const wchar_t* method) {
    T result{};
    if (!live(target)) return result;
    auto f = target->GetFunctionByNameInChain(method);
    if (!f) return result;
    auto size = f->GetParmsSize(), offset = f->GetReturnValueOffset();
    // These specific calls have only a return value, no input/out parameters.
    if (offset != 0 || size < sizeof(T) || size > 16) return result;
    alignas(16) std::array<std::byte, 32> buffer{};
    target->ProcessEvent(f, buffer.data());
    memcpy(&result, buffer.data(), sizeof(T)); return result;
}
static UObject* callObject(UObject* target, const wchar_t* method) {
    auto p = callReturn<UObject*>(target, method); return live(p) ? p : nullptr;
}
static UObject* worldOf(UObject* p) { return live(p) ? reinterpret_cast<UObject*>(p->GetWorld()) : nullptr; }
static bool inWorld(UObject* p, UObject* world) { return live(p) && worldOf(p) == world; }
static UObject* currentWorld(UEngine* engine) { return worldOf(obj(reinterpret_cast<UObject*>(engine), L"GameViewport")); }
struct Player { UObject* state; UObject* pawn; UObject* attachment; bool local; };
struct Roster { UObject* world{}; std::vector<Player> players; bool host = false; };
static Roster roster(UEngine* engine) {
    Roster r; r.world = currentWorld(engine);
    if (!live(r.world)) return r;
    auto gameState = obj(r.world, L"GameState");
    if (!inWorld(gameState, r.world)) return r;
    auto players = field(gameState, L"PlayerArray");
    PointerArray array{};
    if (!players.property || players.property->kind() != L"ArrayProperty"
        || players.property->GetSize() != sizeof(PointerArray)
        || !read(players.address, array) || !validArray(array, 64)) return r;
    for (int i = 0; i < array.num; ++i) {
        auto state = static_cast<UObject*>(array.data[i]);
        if (!inWorld(state, r.world)) continue;
        auto pawn = callObject(state, L"GetPawn");
        if (!inWorld(pawn, r.world) || obj(pawn, L"PlayerState") != state) continue;
        auto controller = obj(pawn, L"Controller");
        bool local = inWorld(controller, r.world) && callReturn<bool>(controller, L"IsLocalController");
        r.host |= local && callReturn<bool>(controller, L"HasAuthority");
        auto attachment = obj(pawn, L"Mesh");
        if (!inWorld(attachment, r.world) || callObject(attachment, L"GetOwner") != pawn) attachment = obj(pawn, L"RootComponent");
        if (!inWorld(attachment, r.world) || callObject(attachment, L"GetOwner") != pawn) attachment = nullptr;
        r.players.push_back({state, pawn, attachment, local});
    }
    return r;
}
std::vector<uintptr_t> rosterSignature(UEngine* engine) {
    auto r = roster(engine);
    if (!r.host) return {};
    std::vector<uintptr_t> signature{reinterpret_cast<uintptr_t>(r.world)};
    std::vector<uintptr_t> pairs;
    for (auto& p : r.players) if (!p.local) {
        pairs.push_back(reinterpret_cast<uintptr_t>(p.state)); pairs.push_back(reinterpret_cast<uintptr_t>(p.pawn));
    }
    // PlayerArray order may change, so sort whole state/pawn pairs.
    std::vector<std::pair<uintptr_t, uintptr_t>> ordered;
    for (size_t i = 0; i < pairs.size(); i += 2) ordered.emplace_back(pairs[i], pairs[i + 1]);
    std::sort(ordered.begin(), ordered.end());
    for (auto [state, pawn] : ordered) { signature.push_back(state); signature.push_back(pawn); }
    return signature;
}
static UObject* loaded(const wchar_t* className, const wchar_t* fullName) {
    std::vector<UObject*> found; UObjectGlobals::FindAllOf(className, found);
    for (auto p : found) if (live(p) && p->GetFullName(nullptr) == fullName) return p;
    return nullptr;
}
static UObject* effectiveWorld(UObject* talker, UObject* state, UObject* owner, UObject* outer, UObject* attach) {
    for (auto p : {talker, state, owner, outer, attach}) if (auto w = worldOf(p)) return w;
    return nullptr;
}
struct Talker { UObject* object; Field attach, attenuation; UObject* state; UObject* owner; UObject* outer; UObject* attachmentOwner; };
static bool matches(const Talker& t, const Player& p) {
    return t.state == p.state || t.owner == p.state || t.owner == p.pawn || t.outer == p.state
        || t.outer == p.pawn || t.attachmentOwner == p.pawn;
}
std::wstring repairProximity(UEngine* engine, bool diagnoseOnly) {
    auto r = roster(engine);
    log(L"WORLD " + name(r.world) + L" host=" + std::to_wstring(r.host) + L" players=" + std::to_wstring(r.players.size()));
    if (!r.host) return L"Proximity repair needs the listen-server host and current player pawns.";
    auto voiceType = reinterpret_cast<UStruct*>(loaded(L"ScriptStruct", L"ScriptStruct /Script/Engine.VoiceSettings"));
    if (!voiceType) return L"VoiceSettings reflection is unavailable; no changes made.";
    auto attenuation = loaded(L"SoundAttenuation", L"SoundAttenuation /Game/Ride/Audio/Attenuation/SA_VOIP.SA_VOIP");
    std::vector<UObject*> objects; UObjectGlobals::FindAllOf(L"VOIPTalker", objects);
    std::vector<Talker> talkers;
    for (auto talker : objects) {
        if (!live(talker)) continue;
        auto settings = field(talker, L"Settings");
        if (!settings.property || settings.property->GetSize() != voiceType->GetPropertiesSize()) continue;
        auto attach = member(settings, voiceType, L"ComponentToAttachTo");
        auto att = member(settings, voiceType, L"AttenuationSettings");
        auto component = objectValue(attach);
        auto state = obj(talker, L"CachedPlayerState"), owner = callObject(talker, L"GetOwner");
        auto outer = talker->GetOuterPrivate();
        if (effectiveWorld(talker, state, owner, outer, component) != r.world) continue;
        auto attachmentOwner = callObject(component, L"GetOwner");
        talkers.push_back({talker, attach, att, state, owner, outer, attachmentOwner});
        log(L"TALKER " + name(talker) + L" cached=" + name(state) + L" owner=" + name(owner)
            + L" outer=" + name(outer) + L" attachment=" + name(component) + L" attenuation=" + name(objectValue(att)));
    }
    unsigned registered = 0, skipped = 0;
    for (auto& t : talkers) {
        std::vector<const Player*> possible;
        for (auto& p : r.players) if (matches(t, p)) possible.push_back(&p);
        if (possible.size() != 1 || possible[0]->local) { ++skipped; continue; }
        const auto& player = *possible[0];
        auto duplicates = std::count_if(talkers.begin(), talkers.end(), [&](auto& other) { return matches(other, player); });
        if (duplicates != 1 || !player.attachment) { ++skipped; continue; }
        auto f = t.object->GetFunctionByNameInChain(L"RegisterWithPlayerState");
        // Exactly one object parameter, with no return. Avoid positional guesses
        // across changed game/engine versions.
        auto parameter = f ? f->GetChildProperties() : nullptr;
        if (!f || f->GetParmsSize() != sizeof(void*) || f->GetReturnValueOffset() != 0xffff
            || !parameter || parameter->next() || parameter->kind() != L"ObjectProperty"
            || static_cast<FProperty*>(parameter)->GetOffset_Internal() != 0
            || static_cast<FProperty*>(parameter)->GetSize() != 8) { ++skipped; continue; }
        auto oldAttachment = objectValue(t.attach), oldAttenuation = objectValue(t.attenuation);
        auto nextAttachment = inWorld(oldAttachment, r.world) && callObject(oldAttachment, L"GetOwner") == player.pawn
            ? oldAttachment : player.attachment;
        auto nextAttenuation = oldAttenuation ? oldAttenuation : attenuation;
        if (!nextAttenuation || !t.attach.property || !t.attenuation.property) { ++skipped; continue; }
        if (diagnoseOnly) continue;
        // No persistent UObject pointers: all association and authority checks
        // above were read in this game-thread callback, immediately before use.
        if (currentWorld(engine) != r.world || callObject(player.state, L"GetPawn") != player.pawn
            || obj(player.pawn, L"PlayerState") != player.state) { ++skipped; continue; }
        if (!setObject(t.attach, nextAttachment) || !setObject(t.attenuation, nextAttenuation)) {
            bool restoredAttach = setObject(t.attach, oldAttachment);
            bool restoredAttenuation = setObject(t.attenuation, oldAttenuation);
            log(restoredAttach && restoredAttenuation ? L"SKIP: property update failed; previous settings restored."
                : L"ERROR: property rollback incomplete; stop testing and restart the game.");
            ++skipped; continue;
        }
        alignas(8) UObject* parameterValue = player.state;
        t.object->ProcessEvent(f, &parameterValue);
        ++registered;
        log(L"REGISTERED " + name(t.object) + L" with " + name(player.state) + L"; audible result requires multiplayer verification.");
    }
    if (talkers.empty()) return L"No current native VOIPTalkers found. See RVVoiceFix.log; no replacement talkers created.";
    return diagnoseOnly ? L"Snapshot written to RVVoiceFix.log (" + std::to_wstring(talkers.size()) + L" talkers)."
        : L"Registration calls completed: " + std::to_wstring(registered) + L"; skipped: " + std::to_wstring(skipped)
          + L". Players must verify distance and direction.";
}
}
