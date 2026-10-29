local function log(message)
    print("[RVProximityFix] " .. tostring(message) .. "\n")
end

-- Resolve this mod's modules independently of the game's working directory.
local source = debug.getinfo(1, "S").source
local directory = source:match("^@(.+[/\\])")
if not directory then error("RVProximityFix cannot resolve its Scripts directory") end
package.path = directory .. "?.lua;" .. package.path

local config = require("config")
local Engine = require("engine")
local Repair = require("repair")
local Controller = require("controller")

if type(ExecuteInGameThreadWithDelay) ~= "function"
    or type(ExecuteInGameThread) ~= "function" then
    log("DISABLED: requires a UE5.6-compatible UE4SS build exposing ExecuteInGameThreadWithDelay and ExecuteInGameThread.")
    return
end

assert(type(config.AutoRepair) == "boolean", "AutoRepair must be boolean")
assert(type(config.SettleDelayMs) == "number" and config.SettleDelayMs >= 1000,
    "SettleDelayMs must be at least 1000")
assert(type(config.RetryOffsetsMs) == "table" and #config.RetryOffsetsMs > 0,
    "RetryOffsetsMs must contain one or more offsets")
for _, offset in ipairs(config.RetryOffsetsMs) do
    assert(type(offset) == "number" and offset >= 0, "retry offsets must be nonnegative")
end

local runtime = Engine.new({ FindFirstOf = FindFirstOf, FindAllOf = FindAllOf,
    StaticFindObject = StaticFindObject, config = config })
local controller = Controller.new(config, runtime, Repair, ExecuteInGameThreadWithDelay, log)

local function onGameThread(callback)
    ExecuteInGameThread(callback)
end

local function changed(reason)
    -- Object-construction callbacks do not dereference the constructed object.
    onGameThread(function() controller:request(reason, false) end)
end

local function installNotification(class, reason)
    local ok, err = pcall(function()
        NotifyOnNewObject(class, function() changed(reason) end)
    end)
    if not ok then log("HOOK unavailable " .. class .. ": " .. tostring(err)) end
end

installNotification("/Script/Engine.World", "world constructed")
installNotification("/Script/Engine.PlayerState", "player state constructed")
installNotification("/Script/Engine.VOIPTalker", "voice talker constructed")

-- Avoid the native LoadMap detour; startup, object notifications and the
-- reflected restart RPC cover new worlds without depending on that ABI hook.
local hookOK, hookError = pcall(function()
    RegisterHook("/Script/Engine.PlayerController:ClientRestart", function() end,
        function() changed("ClientRestart") end)
end)
if not hookOK then log("HOOK ClientRestart unavailable: " .. tostring(hookError)) end

local function bind(label, callback)
    local key = Key[config[label]]
    assert(key ~= nil, "Unknown key for " .. label .. ": " .. tostring(config[label]))
    RegisterKeyBind(key, function() onGameThread(callback) end)
end
bind("DiagnosticKey", function() controller:pass("manual diagnostic", true) end)
bind("RepairKey", function() controller:request("manual repair", true) end)
bind("ToggleAutoKey", function() controller:toggle() end)

log("Loaded v0.1.0 (experimental). " .. config.DiagnosticKey .. "=diagnose "
    .. config.RepairKey .. "=repair " .. config.ToggleAutoKey .. "=toggle auto.")
log("Registration success is not audible verification. After repair, remote players should pause speech briefly, then speak again.")
changed("startup")
