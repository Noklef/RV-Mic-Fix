#include "Voice.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rv
{
using namespace RC::Unreal;
using namespace ue;

namespace
{
constexpr unsigned MaxRestarts = 3;
constexpr uint64_t RestartSpacing = 15000;

bool inWorld(UObject* p, UObject* world)
{
    return live(p) && worldOf(p) == world;
}

UObject* currentWorld(UEngine* engine)
{
    return worldOf(obj(reinterpret_cast<UObject*>(engine), L"GameViewport"));
}

std::wstring shortName(UObject* p)
{
    if (!live(p))
    {
        return L"none";
    }
    auto full = p->GetFullName(nullptr);
    auto cut = full.find_last_of(L".:");
    return cut == std::wstring::npos ? full : full.substr(cut + 1);
}

struct Player
{
    UObject* state;
    UObject* pawn;
    UObject* attachment;
    bool local;
    std::wstring name;
};

struct Roster
{
    UObject* world{};
    std::vector<Player> players;
    bool host = false;
    UObject* localPawn{};
};

Roster roster(UEngine* engine)
{
    Roster r;
    r.world = currentWorld(engine);
    if (!live(r.world))
    {
        return r;
    }
    auto gameState = obj(r.world, L"GameState");
    PointerArray array{};
    if (!inWorld(gameState, r.world) || !pointerArray(field(gameState, L"PlayerArray"), array, 64))
    {
        return r;
    }
    for (int i = 0; i < array.num; ++i)
    {
        auto state = static_cast<UObject*>(array.data[i]);
        if (!inWorld(state, r.world))
        {
            continue;
        }
        auto playerName = stringValue(field(state, L"PlayerNamePrivate"));
        // The owning controller decides local/host, so a player between pawns
        // is never shown as remote.
        auto controller = callObject(state, L"GetOwner");
        auto pawn = callObject(state, L"GetPawn");
        if (!inWorld(pawn, r.world) || obj(pawn, L"PlayerState") != state)
        {
            pawn = nullptr;
        }
        if (!inWorld(controller, r.world) && pawn)
        {
            controller = obj(pawn, L"Controller");
        }
        bool local = false;
        bool authority = false;
        if (inWorld(controller, r.world))
        {
            callBool(controller, L"IsLocalController", local);
            callBool(controller, L"HasAuthority", authority);
        }
        r.host |= local && authority;
        if (local && pawn)
        {
            r.localPawn = pawn;
        }
        UObject* attachment = nullptr;
        if (pawn)
        {
            attachment = obj(pawn, L"Mesh");
            if (!inWorld(attachment, r.world) || callObject(attachment, L"GetOwner") != pawn)
            {
                attachment = obj(pawn, L"RootComponent");
            }
            if (!inWorld(attachment, r.world) || callObject(attachment, L"GetOwner") != pawn)
            {
                attachment = nullptr;
            }
        }
        r.players.push_back({state, pawn, attachment, local, playerName});
    }
    return r;
}

struct Talker
{
    UObject* object;
    Field attach, attenuation;
    UObject* state;
    UObject* owner;
    UObject* outer;
    UObject* attachmentOwner;
};

struct Synth
{
    UObject* object;
    UObject* parent;
    UObject* parentOwner;
    UObject* attenuation;
    UObject* audio;
    bool playing;
    std::wstring owner{}; // remote player matched to this stream, for the log
};

// 2: tied to the current pawn. 1: tied only through the PlayerState (a
// talker that survives respawns, or a stale one). 0: unrelated.
int rank(const Talker& t, const Player& p)
{
    if (p.pawn && (t.owner == p.pawn || t.outer == p.pawn || t.attachmentOwner == p.pawn))
    {
        return 2;
    }
    if (t.state == p.state || t.owner == p.state || t.outer == p.state)
    {
        return 1;
    }
    return 0;
}

UObject* effectiveWorld(std::initializer_list<UObject*> objects)
{
    for (auto p : objects)
    {
        if (auto w = worldOf(p))
        {
            return w;
        }
    }
    return nullptr;
}

// The player's Steam ID from PlayerState.UniqueID, or empty when the game has
// none (offline, or another online service).
std::wstring steamIdOf(UObject* state)
{
    auto library = find(L"/Script/OnlineSubsystemBlueprints.Default__OnlineHelpers");
    auto id = field(state, L"UniqueID");
    if (!library || !id || id.property->kind() != L"StructProperty")
    {
        return {};
    }
    Method m(library, L"Conv_FUniqueNetIdReplToString");
    auto in = m.param(L"InNetId");
    auto out = m.param(L"ReturnValue");
    if (!in || !out || out->kind != L"StrProperty" || !out->property ||
        out->size != int(sizeof(ParamString)) || in->size != id.property->GetSize())
    {
        return {};
    }
    // A borrowed copy: the engine reads it by reference or copies it with its own
    // reference count, and Call never destroys its buffer.
    Call call(m);
    call.bytes(L"InNetId", id.address, size_t(in->size));
    if (!call.invoke(library))
    {
        return {};
    }
    auto slot = call.slot(L"ReturnValue");
    ParamString s{};
    std::memcpy(&s, slot, sizeof(s));
    std::wstring text;
    if (s.data && s.num > 1 && s.num <= 64)
    {
        text.assign(s.data, size_t(s.num - 1));
    }
    release(out->property, slot); // the buffer owns the returned FString
    return steamId(text) ? text : std::wstring{};
}

UObject* voipAttenuation()
{
    constexpr auto path = L"/Game/Ride/Audio/Attenuation/SA_VOIP.SA_VOIP";
    if (auto p = find(path); live(p))
    {
        return p;
    }
    std::vector<UObject*> found;
    UObjectGlobals::FindAllOf(L"SoundAttenuation", found);
    for (auto p : found)
    {
        if (live(p) && p->GetFullName(nullptr) == std::wstring(L"SoundAttenuation ") + path)
        {
            return p;
        }
    }
    return nullptr;
}

// Exactly one object parameter and no return value; avoids positional guesses
// across changed game/engine versions.
bool registerTalker(UObject* talker, UObject* state)
{
    Method m(talker, L"RegisterWithPlayerState");
    if (!m || m.count() != 1 || m.param(L"ReturnValue"))
    {
        return false;
    }
    auto& [key, p] = m.at(0);
    if (p.offset != 0 || p.size != 8 ||
        (p.kind != L"ObjectProperty" && p.kind != L"ObjectPtrProperty"))
    {
        return false;
    }
    return Call(m).set<UObject*>(key, state).invoke(talker);
}

double volumeOf(UObject* target)
{
    double value = -1;
    realValue(field(target, L"VolumeMultiplier"), value);
    return value;
}

bool setVolume(UObject* target, float gain)
{
    double current = volumeOf(target);
    if (current >= 0 && std::abs(current - gain) < 0.005)
    {
        return true;
    }
    Method m(target, L"SetVolumeMultiplier");
    if (!m || m.count() != 1)
    {
        return false;
    }
    return Call(m).real(m.at(0).first, gain).invoke(target);
}

double distance(UObject* from, UObject* to)
{
    Method m(from, L"GetDistanceTo");
    if (!m || !m.param(L"OtherActor") || !m.param(L"ReturnValue"))
    {
        return -1;
    }
    Call call(m);
    call.set<UObject*>(L"OtherActor", to);
    if (!call.invoke(from))
    {
        return -1;
    }
    return call.getReal(L"ReturnValue") / 100.0; // centimetres to metres
}

bool stop(UObject* synth)
{
    Method m(synth, L"Stop");
    return m && m.count() == 0 && Call(m).invoke(synth);
}

std::wstring volumeText(double v)
{
    if (v < 0)
    {
        return L"?";
    }
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%.2f", v);
    return buffer;
}
} // namespace

VoiceStatus Voice::pass(UEngine* engine, Prefs& prefs, bool repair, bool force, uint64_t now,
                        std::vector<std::wstring>& events)
{
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&start);
    VoiceStatus st;
    auto r = roster(engine);
    volumeTargets_.clear();
    if (!live(r.world))
    {
        return st;
    }
    st.inSession = true;
    st.host = r.host;
    // The defect is host-side; clients never mutate talkers or streams.
    repair = repair && r.host;
    if (std::none_of(r.players.begin(), r.players.end(), [](auto& p) { return !p.local; }))
    {
        return st;
    }

    std::vector<UObject*> objects;
    UObjectGlobals::FindAllOf(L"VOIPTalker", objects);
    std::vector<Talker> talkers;
    for (auto t : objects)
    {
        if (garbage(t))
        {
            ++st.staleTalkers;
            continue;
        }
        if (!live(t))
        {
            continue;
        }
        auto outer = t->GetOuterPrivate();
        if (garbage(outer))
        {
            ++st.staleTalkers;
            continue;
        }
        auto settings = field(t, L"Settings");
        auto attach = member(settings, L"ComponentToAttachTo");
        auto attenuation = member(settings, L"AttenuationSettings");
        auto component = objectValue(attach);
        auto state = obj(t, L"CachedPlayerState");
        auto owner = callObject(t, L"GetOwner");
        if (effectiveWorld({t, state, owner, outer, component}) != r.world)
        {
            continue;
        }
        talkers.push_back(
            {t, attach, attenuation, state, owner, outer, callObject(component, L"GetOwner")});
    }
    st.talkers = unsigned(talkers.size());

    objects.clear();
    UObjectGlobals::FindAllOf(L"VoipListenerSynthComponent", objects);
    std::vector<Synth> synths;
    for (auto s : objects)
    {
        auto w = worldOf(s);
        if (!live(s) || (w && w != r.world))
        {
            continue;
        }
        // The synth itself is not attached in this game; its inner AudioComponent is
        // attached to the talker's parent and carries the attenuation that is heard.
        auto audio = obj(s, L"AudioComponent");
        if (!live(audio))
        {
            audio = nullptr;
        }
        auto source = audio ? audio : s;
        auto parent = obj(source, L"AttachParent");
        bool playing = false;
        callBool(s, L"IsPlaying", playing);
        synths.push_back({s, parent, callObject(parent, L"GetOwner"),
                          obj(source, L"AttenuationSettings"), audio, playing});
    }
    st.synths = unsigned(synths.size());

    // Best talker per player; a talker claimed by two players is ambiguous.
    std::vector<int> chosen(r.players.size(), -1);
    for (size_t i = 0; i < r.players.size(); ++i)
    {
        int best = 0;
        int count = 0;
        UObject* chosenAttach = nullptr;
        UObject* chosenAttenuation = nullptr;
        for (size_t j = 0; j < talkers.size(); ++j)
        {
            int score = rank(talkers[j], r.players[i]);
            if (score > best)
            {
                best = score;
                count = 0;
            }
            if (score == best && score > 0)
            {
                // After a load the game gives a PlayerState two talkers with the same
                // attach component and attenuation: one voice path, not an ambiguity.
                auto attach = objectValue(talkers[j].attach);
                auto attenuation = objectValue(talkers[j].attenuation);
                if (count > 0 && attach && attach == chosenAttach &&
                    attenuation == chosenAttenuation)
                {
                    continue;
                }
                ++count;
                chosen[i] = int(j);
                chosenAttach = attach;
                chosenAttenuation = attenuation;
            }
        }
        if (count != 1)
        {
            chosen[i] = -1;
        }
    }
    for (size_t i = 0; i < chosen.size(); ++i)
    {
        if (chosen[i] >= 0 && std::count(chosen.begin(), chosen.end(), chosen[i]) > 1)
        {
            chosen[i] = -1;
        }
    }

    UObject* fallbackAttenuation = nullptr;
    bool attenuationLooked = false;
    for (size_t i = 0; i < r.players.size(); ++i)
    {
        auto& p = r.players[i];
        if (p.local)
        {
            continue;
        }
        PlayerInfo info;
        info.name = p.name.empty() ? L"Player " + std::to_wstring(i + 1) : p.name;
        // Empty for an unnamed player without an ID: it could inherit another's settings.
        info.key = prefs.identify(steamIdOf(p.state), p.name);
        info.pawn = p.pawn != nullptr;
        if (r.localPawn && p.pawn)
        {
            info.distance = distance(r.localPawn, p.pawn);
        }
        Talker* t = chosen[i] >= 0 ? &talkers[size_t(chosen[i])] : nullptr;
        info.talker = t != nullptr;

        if (t && repair && p.pawn && p.attachment && t->attach && t->attenuation)
        {
            auto oldAttachment = objectValue(t->attach);
            auto oldAttenuation = objectValue(t->attenuation);
            auto nextAttachment =
                inWorld(oldAttachment, r.world) && callObject(oldAttachment, L"GetOwner") == p.pawn
                    ? oldAttachment
                    : p.attachment;
            if (!oldAttenuation && !attenuationLooked)
            {
                fallbackAttenuation = voipAttenuation();
                attenuationLooked = true;
            }
            auto nextAttenuation = oldAttenuation ? oldAttenuation : fallbackAttenuation;
            if (nextAttenuation &&
                (nextAttachment != oldAttachment || nextAttenuation != oldAttenuation))
            {
                if (setObject(t->attach, nextAttachment) &&
                    setObject(t->attenuation, nextAttenuation))
                {
                    events.push_back(info.name + L": talker settings restored (attach " +
                                     shortName(nextAttachment) + L", attenuation " +
                                     shortName(nextAttenuation) + L").");
                }
                else
                {
                    bool restored = setObject(t->attach, oldAttachment) &&
                                    setObject(t->attenuation, oldAttenuation);
                    events.push_back(restored ? info.name + L": settings update failed; restored."
                                              : info.name + L": settings rollback INCOMPLETE; "
                                                            L"restart the game before testing.");
                }
            }
            // Idempotent: overwrites the engine's player->talker map entry, which
            // a destroyed talker can clear at a later garbage collection.
            if (registerTalker(t->object, p.state))
            {
                ++st.registered;
                auto& with = registeredWith_[reinterpret_cast<uintptr_t>(t->object)];
                if (with != reinterpret_cast<uintptr_t>(p.state))
                {
                    with = reinterpret_cast<uintptr_t>(p.state);
                    log(L"REGISTERED " + name(t->object) + L" with " + name(p.state));
                }
            }
        }

        auto attached = t ? objectValue(t->attach) : nullptr;
        Synth* voice = nullptr;
        for (auto& s : synths)
        {
            // The game attaches voice to the PlayerState's DefaultSceneRoot.
            if (s.parent && ((p.pawn && s.parentOwner == p.pawn) || s.parentOwner == p.state ||
                             s.parent == attached))
            {
                voice = &s;
                break;
            }
        }
        std::wstring detail = t ? L"talker " + shortName(t->object) + L", attach " +
                                      shortName(attached) + L", attenuation " +
                                      shortName(objectValue(t->attenuation))
                                : L"no matching talker";
        if (voice)
        {
            voice->owner = info.name;
            info.speaking = voice->playing;
            info.positional = voice->attenuation != nullptr;
            auto target = voice->audio ? voice->audio : voice->object;
            double base = baseVolume(target);
            FWeakObjectPtr weak{};
            weak = target;
            if (!info.key.empty())
            {
                volumeTargets_.push_back({info.key, weak});
            }
            if (!info.key.empty() && prefs.managed(info.key))
            {
                setVolume(target, float(base * prefs.gain(info.key)));
            }
            detail += L" | stream " + std::wstring(voice->playing ? L"playing" : L"idle") +
                      L", attenuation " + shortName(voice->attenuation) + L", parent " +
                      shortName(voice->parent) + L", volume " + volumeText(volumeOf(target));
        }
        else
        {
            detail += L" | no voice stream attached to this player";
        }
        info.detail = detail;
        if (info.speaking)
        {
            auto previous = lastPositional_.find(info.name);
            if (previous == lastPositional_.end() ? !info.positional
                                                  : previous->second != info.positional)
            {
                events.push_back(info.positional
                                     ? info.name + L": voice is positional again."
                                     : info.name + L": voice is GLOBAL (no distance falloff).");
            }
            lastPositional_[info.name] = info.positional;
        }
        st.players.push_back(std::move(info));
    }

    // Playing streams without attenuation are heard everywhere. The engine applies a
    // talker's settings only when a stream starts, so a stop lets the next voice
    // packet restart it with the repaired registration.
    for (auto& s : synths)
    {
        if (!s.playing || s.attenuation)
        {
            continue;
        }
        ++st.globalVoices;
        if (!repair || st.registered == 0)
        {
            continue;
        }
        auto& restart = restarts_[reinterpret_cast<uintptr_t>(s.object)];
        if (force)
        {
            restart.count = 0;
        }
        if (restart.count >= MaxRestarts ||
            (!force && restart.last && now - restart.last < RestartSpacing))
        {
            continue;
        }
        if (stop(s.object))
        {
            ++restart.count;
            restart.last = now;
            ++st.restarted;
            events.push_back(L"RESTARTED " +
                             (s.owner.empty() ? std::wstring(L"an unmatched stream") : s.owner) +
                             L" (" + shortName(s.object) +
                             L"): voice played without attenuation; it resumes positional "
                             L"on the next words.");
        }
    }
    if (restarts_.size() > 256)
    {
        restarts_.clear();
    }
    if (registeredWith_.size() > 256)
    {
        registeredWith_.clear();
    }
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);
    st.scanMs = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
    return st;
}

double Voice::baseVolume(UObject* target)
{
    // Drop only destroyed components; a garbage-marked one still holds the mod's write.
    std::erase_if(baseVolumes_, [](auto& entry) { return !entry.second.first.Get(); });
    auto key = reinterpret_cast<uintptr_t>(target);
    if (auto found = baseVolumes_.find(key); found != baseVolumes_.end())
    {
        return found->second.second;
    }
    // First sight, before any change: the game's own level is heard as 100%.
    double base = volumeOf(target);
    if (base <= 0)
    {
        return 1; // unreadable (or silenced by the game): read again next pass
    }
    FWeakObjectPtr weak{};
    weak = target;
    baseVolumes_[key] = {weak, base};
    return base;
}

std::optional<bool> Voice::applyVolumes(const Prefs& prefs, const std::wstring& player)
{
    std::optional<bool> result;
    for (auto& [name, weak] : volumeTargets_)
    {
        auto target = weak.Get();
        if (live(target) && prefs.managed(name))
        {
            bool ok = setVolume(target, float(baseVolume(target) * prefs.gain(name)));
            if (name == player)
            {
                result = ok;
            }
        }
    }
    return result;
}

void Voice::logVoiceObjects(UEngine* engine)
{
    auto world = currentWorld(engine);
    // Ownership chains with full names: talkers and streams are matched to players
    // through these objects.
    for (auto& p : roster(engine).players)
    {
        log(L"VOICE-DIAG player " + p.name + L" local=" + std::to_wstring(int(p.local)) +
            L" | state " + name(p.state) + L" | pawn " + name(p.pawn));
    }
    std::vector<UObject*> objects;
    UObjectGlobals::FindAllOf(L"VOIPTalker", objects);
    for (size_t i = 0; i < objects.size() && i < 16; ++i)
    {
        auto t = objects[i];
        if (!live(t))
        {
            log(L"VOICE-DIAG talker " + shortName(t) + L" not live");
            continue;
        }
        auto attach = objectValue(member(field(t, L"Settings"), L"ComponentToAttachTo"));
        log(L"VOICE-DIAG talker " + name(t) + L" | state " + name(obj(t, L"CachedPlayerState")) +
            L" | owner " + name(callObject(t, L"GetOwner")) + L" | attach " + name(attach) +
            L" (owner " + name(callObject(attach, L"GetOwner")) + L")");
    }
    objects.clear();
    UObjectGlobals::FindAllOf(L"VoipListenerSynthComponent", objects);
    log(L"VOICE-DIAG VoipListenerSynthComponent objects: " + std::to_wstring(objects.size()));
    for (size_t i = 0; i < objects.size() && i < 16; ++i)
    {
        auto s = objects[i];
        bool playing = false;
        if (live(s))
        {
            callBool(s, L"IsPlaying", playing);
        }
        log(L"VOICE-DIAG synth " + shortName(s) + L" live=" + std::to_wstring(int(live(s))) +
            L" garbage=" + std::to_wstring(int(garbage(s))) + L" sameWorld=" +
            std::to_wstring(int(worldOf(s) == world)) + L" noWorld=" +
            std::to_wstring(int(worldOf(s) == nullptr)) + L" playing=" +
            std::to_wstring(int(playing)) + L" parent " +
            (live(s) ? shortName(obj(s, L"AttachParent")) : std::wstring(L"?")));
    }
    // Voice may instead play through a plain AudioComponent; list the playing ones
    // whose sound is generated at runtime (synth or procedural), not an asset.
    objects.clear();
    UObjectGlobals::FindAllOf(L"AudioComponent", objects);
    unsigned shown = 0;
    for (auto a : objects)
    {
        bool playing = false;
        if (!live(a) || !callBool(a, L"IsPlaying", playing) || !playing)
        {
            continue;
        }
        auto sound = name(obj(a, L"Sound"));
        if (sound.find(L"Synth") == std::wstring::npos &&
            sound.find(L"Procedural") == std::wstring::npos &&
            sound.find(L"Voip") == std::wstring::npos && sound.find(L"Voice") == std::wstring::npos)
        {
            continue;
        }
        if (++shown > 16)
        {
            break;
        }
        log(L"VOICE-DIAG playing " + name(a) + L" | sound " + sound + L" | attenuation " +
            shortName(obj(a, L"AttenuationSettings")) + L" | parent " +
            name(obj(a, L"AttachParent")) + L" (owner " +
            name(callObject(obj(a, L"AttachParent"), L"GetOwner")) + L") | volume " +
            volumeText(volumeOf(a)));
    }
    log(L"VOICE-DIAG playing runtime-sound AudioComponents: " + std::to_wstring(shown) + L" of " +
        std::to_wstring(objects.size()) + L" AudioComponents.");
}

void Voice::logStatus(const VoiceStatus& st)
{
    wchar_t line[256];
    swprintf(line, 256,
             L"SESSION in=%d host=%d players=%zu talkers=%u stale=%u streams=%u global=%u "
             L"registered=%u restarted=%u scan=%.2fms",
             int(st.inSession), int(st.host), st.players.size(), st.talkers, st.staleTalkers,
             st.synths, st.globalVoices, st.registered, st.restarted, st.scanMs);
    log(line);
    for (auto& p : st.players)
    {
        swprintf(line, 256, L"PLAYER distance=%.1fm pawn=%d talker=%d speaking=%d positional=%d ",
                 p.distance, int(p.pawn), int(p.talker), int(p.speaking), int(p.positional));
        log(line + p.name + L" [" + (p.key.empty() ? std::wstring(L"no settings key") : p.key) +
            L"] | " + p.detail);
    }
}
} // namespace rv
