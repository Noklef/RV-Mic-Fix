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

VoiceStatus Voice::pass(UEngine* engine, const Prefs& prefs, bool repair, bool force, uint64_t now,
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
        auto parent = obj(s, L"AttachParent");
        bool playing = false;
        callBool(s, L"IsPlaying", playing);
        synths.push_back({s, parent, callObject(parent, L"GetOwner"),
                          obj(s, L"AttenuationSettings"), obj(s, L"AudioComponent"), playing});
    }
    st.synths = unsigned(synths.size());

    // Best talker per player; a talker claimed by two players is ambiguous.
    std::vector<int> chosen(r.players.size(), -1);
    for (size_t i = 0; i < r.players.size(); ++i)
    {
        int best = 0;
        int count = 0;
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
                ++count;
                chosen[i] = int(j);
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
            if (s.parent && ((p.pawn && s.parentOwner == p.pawn) || s.parent == attached))
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
            info.speaking = voice->playing;
            info.positional = voice->attenuation != nullptr;
            auto target = voice->audio ? voice->audio : voice->object;
            FWeakObjectPtr weak{};
            weak = target;
            // Settings are keyed by name; an unnamed player could inherit another's.
            if (!p.name.empty())
            {
                volumeTargets_.push_back({info.name, weak});
            }
            if (!p.name.empty() && prefs.managed(info.name))
            {
                setVolume(target, prefs.gain(info.name));
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
            events.push_back(L"Restarted a voice stream playing without attenuation; it "
                             L"resumes positional on the next words.");
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

void Voice::applyVolumes(const Prefs& prefs)
{
    for (auto& [player, weak] : volumeTargets_)
    {
        auto target = weak.Get();
        if (live(target) && prefs.managed(player))
        {
            setVolume(target, prefs.gain(player));
        }
    }
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
        log(line + p.name + L" | " + p.detail);
    }
}
} // namespace rv
