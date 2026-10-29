-- Engine-independent selection. Mutations happen only in the runtime adapter.
local Repair = {}

function Repair.plan(snapshot)
    if not snapshot.host then
        return {}, { snapshot.reason or "not a local listen-server host" }
    end
    local groups, skipped = {}, {}
    for _, talker in ipairs(snapshot.talkers or {}) do
        if talker.alive and talker.world == snapshot.world then
            local strong, attached = {}, {}
            for _, player in ipairs(snapshot.players or {}) do
                if player.alive and player.world == snapshot.world then
                    if talker.cachedState == player.state
                        or talker.owner == player.state or talker.owner == player.pawn
                        or (player.controller and talker.owner == player.controller)
                        or talker.outer == player.state or talker.outer == player.pawn
                        or (player.controller and talker.outer == player.controller) then
                        strong[#strong + 1] = player
                    end
                    if talker.attachmentOwner == player.pawn then
                        attached[#attached + 1] = player
                    end
                end
            end
            local matches = #strong > 0 and strong or attached
            if #matches ~= 1 then
                skipped[#skipped + 1] = talker.name .. ": ambiguous or missing current player association"
            elseif matches[1].localPlayer then
                -- This patch repairs what the host hears from remote players.
            else
                local player = matches[1]
                local key = player.state
                groups[key] = groups[key] or {}
                groups[key][#groups[key] + 1] = { talker = talker, player = player }
            end
        end
    end
    local actions = {}
    for _, group in pairs(groups) do
        if #group == 1 then
            actions[#actions + 1] = group[1]
        else
            skipped[#skipped + 1] = group[1].player.name .. ": multiple current talkers; refusing to guess"
        end
    end
    table.sort(actions, function(a, b) return a.talker.name < b.talker.name end)
    return actions, skipped
end

function Repair.run(snapshot, runtime, config, log, diagnosticOnly)
    local actions, skipped = Repair.plan(snapshot)
    for _, reason in ipairs(skipped) do log("SKIP " .. reason) end
    local completed, failed = 0, 0
    for _, action in ipairs(actions) do
        log("PAIR talker=" .. action.talker.name .. " player=" .. action.player.name)
        if not diagnosticOnly then
            local ok, message = runtime:apply(action, config)
            if ok then completed = completed + 1 else failed = failed + 1 end
            log((ok and "REGISTERED " or "SKIP ") .. action.talker.name .. ": " .. tostring(message))
        end
    end
    log(string.format("RESULT mode=%s candidates=%d registered=%d failed=%d skipped=%d",
        diagnosticOnly and "diagnostic" or "repair", #actions, completed, failed, #skipped))
    return { candidates = #actions, registered = completed, failed = failed, skipped = #skipped }
end

return Repair
