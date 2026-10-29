-- All methods in this module must be called on the game thread. Never cache a
-- UObject across callbacks: a checkpoint reload replaces the world and actors.
local Engine = {}
Engine.__index = Engine
local EXCLUDED_FLAGS = 0x10 + 0x20 + 0x8000 + 0x10000 -- CDO, archetype, destruction

local function read(object, field)
    local ok, value = pcall(function() return object[field] end)
    if ok then return value end
end

local function call(object, method, ...)
    local args = { ... }
    local ok, value = pcall(function() return object[method](object, table.unpack(args)) end)
    if ok then return value end
end

local function valid(object)
    if object == nil then return false end
    local ok, result = pcall(function()
        return object:IsValid() and not object:HasAnyFlags(EXCLUDED_FLAGS)
    end)
    return ok and result == true
end

local function resolve(value)
    if valid(value) then return value end
    -- Some engine properties are exposed as FWeakObjectPtr instead of UObject.
    local object = call(value, "Get")
    if valid(object) then return object end
end

local function name(object)
    if not valid(object) then return "<invalid>" end
    return call(object, "GetFullName") or "<unnamed>"
end

local function kind(object, class)
    return valid(object) and call(object, "IsA", class) == true
end

local function worldOf(object)
    if valid(object) then return call(object, "GetWorld") end
end

local function sameWorld(object, world)
    return valid(object) and worldOf(object) == world
end

local function liveActor(object, world)
    return sameWorld(object, world) and call(object, "IsActorBeingDestroyed") ~= true
end

local function talkerWorld(object, attachment, cachedState)
    -- Dynamically created talkers may have a transient outer and no GetWorld().
    -- A real old-world identity always takes precedence over a new attachment.
    for _, candidate in ipairs({ object, cachedState or false,
        call(object, "GetOwner") or false, call(object, "GetOuter") or false,
        attachment or false }) do
        local world = worldOf(candidate)
        if valid(world) then return world end
    end
end

function Engine.new(api)
    return setmetatable({ api = api }, Engine)
end

function Engine:currentWorld()
    local engine = self.api.FindFirstOf("Engine")
    if not valid(engine) then return nil end
    local viewport = resolve(read(engine, "GameViewport"))
    local world = worldOf(viewport)
    if valid(world) then return world end
end

function Engine:currentPlayers(world)
    local state = resolve(read(world, "GameState"))
    if not liveActor(state, world) then return nil, "current GameState not ready" end
    local array = read(state, "PlayerArray")
    if array == nil then return nil, "current PlayerArray not ready" end
    local players = {}
    local ok, err = pcall(function()
        array:ForEach(function(_, element)
            local playerState = element:get()
            if not liveActor(playerState, world) then return end
            local pawn = call(playerState, "GetPawn")
            -- Exact two-way ownership prevents using a retired pawn/PlayerState.
            if not liveActor(pawn, world) or read(pawn, "PlayerState") ~= playerState then return end
            local controller = resolve(read(pawn, "Controller"))
            if not liveActor(controller, world) then controller = nil end
            local component = resolve(read(pawn, "Mesh"))
            if not sameWorld(component, world) or call(component, "GetOwner") ~= pawn then
                component = resolve(read(pawn, "RootComponent"))
            end
            if not sameWorld(component, world) or call(component, "GetOwner") ~= pawn then component = nil end
            players[#players + 1] = {
                name = name(playerState), state = playerState, pawn = pawn,
                controller = controller, attachment = component, world = world,
                alive = true, localPlayer = controller ~= nil and call(controller, "IsLocalController") == true,
            }
        end)
    end)
    if not ok then return nil, "unable to read current PlayerArray: " .. tostring(err) end
    return players
end

function Engine:snapshot(log)
    local world = self:currentWorld()
    local snapshot = { world = world, host = false, players = {}, talkers = {} }
    if not valid(world) then snapshot.reason = "current viewport world not ready"; return snapshot end
    snapshot.players, snapshot.reason = self:currentPlayers(world)
    if not snapshot.players then snapshot.players = {}; return snapshot end
    for _, player in ipairs(snapshot.players) do
        if player.localPlayer and call(player.controller, "HasAuthority") == true then
            snapshot.host = true
        end
    end
    log("WORLD " .. name(world) .. " host=" .. tostring(snapshot.host) .. " players=" .. #snapshot.players)
    if not snapshot.host then snapshot.reason = "local host authority not established"; return snapshot end
    for _, player in ipairs(snapshot.players) do
        log("PLAYER " .. player.name .. " pawn=" .. name(player.pawn)
            .. " controller=" .. name(player.controller) .. " local=" .. tostring(player.localPlayer))
    end
    local asset = self.api.StaticFindObject(self.api.config.AttenuationPath)
    if kind(asset, "/Script/Engine.SoundAttenuation") then snapshot.attenuation = asset end
    -- Bounded, event-driven scans only. Never scans the object array every frame.
    for _, object in ipairs(self.api.FindAllOf("VOIPTalker") or {}) do
        if kind(object, "/Script/Engine.VOIPTalker") then
            local settings = read(object, "Settings")
            local attachment = settings and resolve(read(settings, "ComponentToAttachTo"))
            local cachedState = resolve(read(object, "CachedPlayerState"))
            local effectiveWorld = talkerWorld(object, attachment, cachedState)
            if effectiveWorld == world then
            local record = {
                object = object, name = name(object), alive = true, world = world,
                cachedState = cachedState,
                owner = call(object, "GetOwner"), outer = call(object, "GetOuter"),
                attachmentOwner = valid(attachment) and call(attachment, "GetOwner") or nil,
                attenuationAsset = snapshot.attenuation,
            }
            snapshot.talkers[#snapshot.talkers + 1] = record
            log("TALKER " .. record.name .. " owner=" .. name(record.owner)
                .. " outer=" .. name(record.outer) .. " attachment=" .. name(attachment)
                .. " cachedState=" .. name(cachedState)
                .. " attenuation=" .. name(settings and resolve(read(settings, "AttenuationSettings"))))
            end
        end
    end
    if #snapshot.talkers == 0 then
        log("NO_TALKERS: native VOIPTalker instances absent; no components will be created.")
        -- Blueprint may wrap rather than inherit VOIPTalker. Log its schema for
        -- the next investigation, but do not invoke unknown Blueprint functions.
        for _, object in ipairs(self.api.FindAllOf("BP_VOIPTalker_C") or {}) do
            if sameWorld(object, world) then
                log("BLUEPRINT " .. name(object))
                local class = call(object, "GetClass")
                pcall(function()
                    class:ForEachProperty(function(property) log("  PROPERTY " .. property:GetFName():ToString()) end)
                    class:ForEachFunction(function(fn) log("  FUNCTION " .. fn:GetFullName()) end)
                end)
            end
        end
    end
    return snapshot
end

function Engine:apply(action, config)
    local talker, player = action.talker, action.player
    local world = self:currentWorld()
    if world ~= talker.world or world ~= player.world then return false, "world changed" end
    if not liveActor(player.state, world) or not liveActor(player.pawn, world)
        or call(player.state, "GetPawn") ~= player.pawn
        or read(player.pawn, "PlayerState") ~= player.state
        or not valid(talker.object) then
        return false, "player or talker no longer current"
    end
    -- Recheck host authority immediately before mutation.
    local currentPlayers = self:currentPlayers(world)
    local host = false
    for _, current in ipairs(currentPlayers or {}) do
        if current.localPlayer and call(current.controller, "HasAuthority") == true then host = true end
    end
    if not host then return false, "host authority lost" end
    local settings = read(talker.object, "Settings")
    if not settings then return false, "voice Settings not exposed" end
    local previousAttachment = read(settings, "ComponentToAttachTo")
    local previousAttenuation = read(settings, "AttenuationSettings")
    local oldAttachmentObject = resolve(previousAttachment)
    if talkerWorld(talker.object, oldAttachmentObject,
        resolve(read(talker.object, "CachedPlayerState"))) ~= world then
        return false, "talker no longer belongs to current world"
    end
    local newAttachment = oldAttachmentObject
    if config.RepairAttachment and (not sameWorld(oldAttachmentObject, world)
        or call(oldAttachmentObject, "GetOwner") ~= player.pawn) then
        newAttachment = player.attachment
    end
    if not sameWorld(newAttachment, world) or call(newAttachment, "GetOwner") ~= player.pawn then
        return false, "no verified attachment on current pawn"
    end
    local oldAttenuationObject = resolve(previousAttenuation)
    local newAttenuation = oldAttenuationObject
    if not valid(oldAttenuationObject) and config.RestoreMissingAttenuation then
        newAttenuation = talker.attenuationAsset
    end
    if not kind(newAttenuation, "/Script/Engine.SoundAttenuation") then
        return false, "attenuation missing; SA_VOIP must already be loaded"
    end
    local changedAttachment, changedAttenuation = false, false
    local ok, err = pcall(function()
        if newAttachment ~= oldAttachmentObject then
            settings.ComponentToAttachTo = newAttachment
            changedAttachment = true
        end
        if newAttenuation ~= oldAttenuationObject then
            settings.AttenuationSettings = newAttenuation
            changedAttenuation = true
        end
        -- Leaves SourceEffectChain and valid game-selected attenuation intact.
        talker.object:RegisterWithPlayerState(player.state)
    end)
    if not ok then
        local rollbackOK, rollbackError = pcall(function()
            if changedAttachment then settings.ComponentToAttachTo = previousAttachment end
            if changedAttenuation then settings.AttenuationSettings = previousAttenuation end
        end)
        return false, "registration failed: " .. tostring(err)
            .. (rollbackOK and "; settings restored" or "; rollback failed: " .. tostring(rollbackError))
    end
    return true, "registration call completed; attachmentChanged=" .. tostring(changedAttachment)
        .. " attenuationChanged=" .. tostring(changedAttenuation)
        .. "; audible result requires a multiplayer test"
end

return Engine
