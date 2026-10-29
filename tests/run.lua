package.path = "Mods/RVProximityFix/Scripts/?.lua;" .. package.path
local Repair, Engine, Controller = require("repair"), require("engine"), require("controller")
local total, failures = 0, 0
local function equal(actual, expected, label)
    if actual ~= expected then error((label or "value") .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual), 2) end
end
local function test(label, callback)
    total = total + 1
    local ok, err = pcall(callback)
    if ok then print("PASS " .. label) else failures = failures + 1; print("FAIL " .. label .. ": " .. tostring(err)) end
end
local function object(label, world, class)
    local value = { label = label, world = world, class = class, alive = true }
    function value:IsValid() return self.alive end
    function value:HasAnyFlags() return self.excluded == true end
    function value:GetFullName() return self.label end
    function value:GetWorld() return self.world end
    function value:IsA(expected) return expected == self.class end
    function value:GetOwner() return self.owner end
    function value:GetOuter() return self.outer end
    function value:IsActorBeingDestroyed() return self.destroying == true end
    function value:IsLocalController() return self.localPlayer == true end
    function value:HasAuthority() return self.authority == true end
    function value:GetPawn() return self.pawn end
    function value:RegisterWithPlayerState(state)
        if self.fail then error("injected registration failure") end
        self.registered = state; self.calls = (self.calls or 0) + 1
    end
    return value
end
local function fixture()
    local f = { log = {} }
    f.world = object("World snow")
    f.engine = object("Engine")
    f.engine.GameViewport = object("Viewport", f.world)
    f.gameState = object("GameState", f.world)
    f.world.GameState = f.gameState
    f.hostState, f.state = object("HostState", f.world), object("RemoteState", f.world)
    f.hostPawn, f.pawn = object("HostPawn", f.world), object("RemotePawn", f.world)
    f.hostController, f.controller = object("HostController", f.world), object("RemoteController", f.world)
    f.hostController.localPlayer, f.hostController.authority = true, true
    f.hostPawn.PlayerState, f.pawn.PlayerState = f.hostState, f.state
    f.hostState.pawn, f.state.pawn = f.hostPawn, f.pawn
    f.hostPawn.Controller, f.pawn.Controller = f.hostController, f.controller
    f.component = object("Mesh", f.world); f.component.owner = f.pawn; f.pawn.Mesh = f.component
    f.root = object("Root", f.world); f.root.owner = f.pawn; f.pawn.RootComponent = f.root
    f.attenuation = object("SA_VOIP", nil, "/Script/Engine.SoundAttenuation")
    f.effect = object("SourceEffectChain")
    f.talker = object("VOIPTalker remote", f.world, "/Script/Engine.VOIPTalker")
    f.talker.owner, f.talker.outer = f.state, f.state
    f.talker.Settings = { ComponentToAttachTo = f.component, AttenuationSettings = f.attenuation,
        SourceEffectChain = f.effect }
    f.talkers = { f.talker }
    f.gameState.PlayerArray = { ForEach = function(_, callback)
        for i, state in ipairs({f.hostState, f.state}) do callback(i, { get = function() return state end }) end
    end }
    f.config = { AutoRepair = true, SettleDelayMs = 3000, RetryOffsetsMs = {0, 3000, 7000},
        RepairAttachment = true, RestoreMissingAttenuation = true, AttenuationPath = "SA_VOIP" }
    f.api = {
        config = f.config,
        FindFirstOf = function(class) equal(class, "Engine"); return f.engine end,
        FindAllOf = function(class) if class == "VOIPTalker" then return f.talkers end; return {} end,
        StaticFindObject = function() return f.attenuation end,
    }
    f.runtime = Engine.new(f.api)
    f.write = function(message) f.log[#f.log + 1] = message end
    function f:snapshot() return self.runtime:snapshot(self.write) end
    function f:run(diagnostic) return Repair.run(self:snapshot(), self.runtime, self.config, self.write, diagnostic) end
    return f
end

test("registers an exactly associated remote talker", function()
    local f = fixture(); equal(f:run().registered, 1); equal(f.talker.registered, f.state)
end)
test("diagnostics perform no registration or settings writes", function()
    local f = fixture(); f.talker.Settings.ComponentToAttachTo = nil
    equal(f:run(true).candidates, 1); equal(f.talker.calls, nil); equal(f.talker.Settings.ComponentToAttachTo, nil)
end)
test("a non-host client cannot repair", function()
    local f = fixture(); f.hostController.authority = false
    equal(f:run().registered, 0); equal(f.talker.calls, nil)
end)
test("remote server authority is insufficient without a local host", function()
    local f = fixture(); f.hostController.localPlayer = false; f.controller.authority = true
    equal(f:run().registered, 0)
end)
test("host's own microphone talker is excluded", function()
    local f = fixture(); f.talker.owner = f.hostState; f.talker.outer = f.hostState
    equal(f:run().candidates, 0)
end)
test("duplicate talkers for one player are not guessed", function()
    local f = fixture(); local duplicate = object("Duplicate", f.world, "/Script/Engine.VOIPTalker")
    duplicate.owner = f.state; duplicate.outer = f.state; duplicate.Settings = f.talker.Settings
    f.talkers[#f.talkers + 1] = duplicate
    equal(f:run().registered, 0); equal(f.talker.calls, nil); equal(duplicate.calls, nil)
end)
test("conflicting strong associations are skipped", function()
    local f = fixture(); f.talker.owner = f.state; f.talker.outer = f.hostState
    equal(f:run().registered, 0)
end)
test("valid attachment can establish an otherwise unowned talker", function()
    local f = fixture(); f.talker.owner = nil; f.talker.outer = nil
    equal(f:run().registered, 1)
end)
test("unassociated talker is never mapped by name or proximity", function()
    local f = fixture(); f.talker.owner = nil; f.talker.outer = nil; f.talker.Settings.ComponentToAttachTo = nil
    equal(f:run().registered, 0)
end)
test("old-world talkers are ignored after checkpoint replacement", function()
    local f = fixture(); f.talker.world = object("RetiredWorld")
    equal(f:run().registered, 0)
end)
test("destroying and template talkers are ignored", function()
    local f = fixture(); f.talker.excluded = true; equal(f:run().registered, 0)
end)
test("destroying player states are ignored", function()
    local f = fixture(); f.state.destroying = true; equal(f:run().registered, 0)
end)
test("replaced pawn ownership is rejected", function()
    local f = fixture(); f.pawn.PlayerState = f.hostState; equal(f:run().registered, 0)
end)
test("current pawn mesh replaces a retired attachment", function()
    local f = fixture(); local oldMesh = object("RetiredMesh", object("OldWorld"))
    f.talker.Settings.ComponentToAttachTo = oldMesh
    equal(f:run().registered, 1); equal(f.talker.Settings.ComponentToAttachTo, f.component)
end)
test("missing attachment falls back to verified pawn root", function()
    local f = fixture(); f.pawn.Mesh = nil; f.talker.Settings.ComponentToAttachTo = nil
    equal(f:run().registered, 1); equal(f.talker.Settings.ComponentToAttachTo, f.root)
end)
test("valid existing attachment is preserved", function()
    local f = fixture(); local original = object("Mouth", f.world); original.owner = f.pawn
    f.talker.Settings.ComponentToAttachTo = original
    equal(f:run().registered, 1); equal(f.talker.Settings.ComponentToAttachTo, original)
end)
test("valid game-selected attenuation and effects are preserved", function()
    local f = fixture(); local original = object("CustomAttenuation", nil, "/Script/Engine.SoundAttenuation")
    f.talker.Settings.AttenuationSettings = original
    equal(f:run().registered, 1); equal(f.talker.Settings.AttenuationSettings, original)
    equal(f.talker.Settings.SourceEffectChain, f.effect)
end)
test("missing attenuation uses already loaded SA_VOIP", function()
    local f = fixture(); f.talker.Settings.AttenuationSettings = nil
    equal(f:run().registered, 1); equal(f.talker.Settings.AttenuationSettings, f.attenuation)
end)
test("missing unloaded attenuation skips without partial writes", function()
    local f = fixture(); f.talker.Settings.AttenuationSettings = nil; f.talker.Settings.ComponentToAttachTo = nil
    f.api.StaticFindObject = function() return nil end
    equal(f:run().registered, 0); equal(f.talker.Settings.ComponentToAttachTo, nil)
end)
test("no verified current attachment means no registration", function()
    local f = fixture(); f.pawn.Mesh = nil; f.pawn.RootComponent = nil; f.talker.Settings.ComponentToAttachTo = nil
    equal(f:run().registered, 0)
end)
test("failed registration restores changed settings", function()
    local f = fixture(); f.talker.Settings.ComponentToAttachTo = nil; f.talker.Settings.AttenuationSettings = nil
    f.talker.fail = true
    equal(f:run().failed, 1); equal(f.talker.Settings.ComponentToAttachTo, nil)
    equal(f.talker.Settings.AttenuationSettings, nil)
end)
test("changed world between discovery and apply aborts", function()
    local f = fixture(); local actions = Repair.plan(f:snapshot())
    f.engine.GameViewport.world = object("NewWorld")
    equal(f.runtime:apply(actions[1], f.config), false); equal(f.talker.calls, nil)
end)
test("authority loss between discovery and apply aborts", function()
    local f = fixture(); local actions = Repair.plan(f:snapshot()); f.hostController.authority = false
    equal(f.runtime:apply(actions[1], f.config), false); equal(f.talker.calls, nil)
end)
test("missing GameState reports not-ready without scanning talkers", function()
    local f = fixture(); f.world.GameState = nil
    f.api.FindAllOf = function() error("should not scan") end
    equal(f:run().registered, 0)
end)
test("absent native talkers produce diagnostics without constructing replacements", function()
    local f = fixture(); f.talkers = {}; equal(f:run().registered, 0)
    local found = false; for _, line in ipairs(f.log) do if line:find("NO_TALKERS", 1, true) then found = true end end
    equal(found, true)
end)
test("transient talker is identified by its current attachment", function()
    local f = fixture(); f.talker.world = nil; f.talker.owner = nil; f.talker.outer = nil
    equal(f:run().registered, 1)
end)
test("reflected cached PlayerState identifies a transient detached talker", function()
    local f = fixture(); f.talker.world = nil; f.talker.owner = nil; f.talker.outer = nil
    f.talker.CachedPlayerState = f.state; f.talker.Settings.ComponentToAttachTo = nil
    equal(f:run().registered, 1); equal(f.talker.Settings.ComponentToAttachTo, f.component)
end)
test("weak attachment pointers resolve without unnecessary writes", function()
    local f = fixture(); local weak = {Get = function() return f.component end}
    f.talker.Settings.ComponentToAttachTo = weak
    equal(f:run().registered, 1); equal(f.talker.Settings.ComponentToAttachTo, weak)
end)
test("retired cached state cannot import a transient talker into new world", function()
    local f = fixture(); f.talker.world = nil; f.talker.owner = nil; f.talker.outer = nil
    f.talker.CachedPlayerState = object("OldState", object("OldWorld"))
    equal(f:run().registered, 0)
end)

local function coordinatorFixture()
    local f = fixture(); f.timers = {}; f.passes = 0
    local original = f.runtime.snapshot
    function f.runtime:snapshot(log) f.passes = f.passes + 1; return original(self, log) end
    f.coordinator = Controller.new(f.config, f.runtime, Repair,
        function(delay, callback) f.timers[#f.timers + 1] = {delay = delay, callback = callback} end, f.write)
    function f:flush() local timers = self.timers; self.timers = {}; for _, timer in ipairs(timers) do timer.callback() end end
    return f
end
test("new lifecycle event cancels earlier bounded burst", function()
    local f = coordinatorFixture(); f.coordinator:request("old", false); f.coordinator:request("new", false)
    f:flush(); equal(f.passes, 3); equal(f.talker.calls, 3)
end)
test("repair bursts honor settling and retry delays", function()
    local f = coordinatorFixture(); f.coordinator:request("reset", false)
    equal(f.timers[1].delay, 3000); equal(f.timers[2].delay, 6000); equal(f.timers[3].delay, 10000)
end)
test("disabling auto invalidates outstanding callbacks", function()
    local f = coordinatorFixture(); f.coordinator:request("reset", false); f.coordinator:toggle(); f:flush()
    equal(f.passes, 0)
end)
test("manual repair works while automatic repair is disabled", function()
    local f = coordinatorFixture(); f.coordinator:toggle(); f.coordinator:request("manual", true); f:flush()
    equal(f.passes, 3)
end)
test("each delayed attempt discovers the replacement world afresh", function()
    local f = coordinatorFixture(); f.coordinator:request("reset", false)
    local replacement = fixture()
    f.runtime.snapshot = function(_, log) return replacement.runtime:snapshot(log) end
    f.runtime.apply = function(_, action, cfg) return replacement.runtime:apply(action, cfg) end
    f:flush(); equal(f.talker.calls, nil); equal(replacement.talker.calls, 3)
end)
test("runtime error aborts a pass without escaping the callback", function()
    local f = coordinatorFixture(); f.runtime.snapshot = function() error("not ready") end
    f.coordinator:request("reset", false); f:flush(); equal(f.talker.calls, nil)
end)
test("entry point registers keys, notifications, and deferred startup", function()
    local keys, notifications, timers = {}, {}, {}
    _G.Key = {F8 = 119, F9 = 120, F10 = 121}
    _G.ExecuteInGameThread = function(callback) callback() end
    _G.ExecuteInGameThreadWithDelay = function(delay, callback) timers[#timers + 1] = {delay, callback} end
    _G.RegisterKeyBind = function(key, callback) keys[key] = callback end
    _G.NotifyOnNewObject = function(class, callback) notifications[class] = callback end
    _G.RegisterHook = function(path, before, after)
        equal(path, "/Script/Engine.PlayerController:ClientRestart")
        equal(type(after), "function")
    end
    local f = fixture()
    _G.FindFirstOf, _G.FindAllOf, _G.StaticFindObject = f.api.FindFirstOf, f.api.FindAllOf, f.api.StaticFindObject
    dofile("Mods/RVProximityFix/Scripts/main.lua")
    equal(type(keys[119]), "function"); equal(type(keys[120]), "function"); equal(type(keys[121]), "function")
    equal(type(notifications["/Script/Engine.VOIPTalker"]), "function"); equal(#timers, 3)
    keys[119](); equal(f.talker.calls, nil)
    notifications["/Script/Engine.VOIPTalker"](nil); equal(#timers, 6)
end)
test("unsupported UE4SS stops before installing callbacks", function()
    _G.ExecuteInGameThreadWithDelay = nil
    _G.NotifyOnNewObject = function() error("should not install") end
    dofile("Mods/RVProximityFix/Scripts/main.lua")
end)

print(string.format("\n%d tests, %d failures", total, failures))
if failures > 0 then error("test suite failed") end
