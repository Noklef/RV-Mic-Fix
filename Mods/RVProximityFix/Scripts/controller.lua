-- Debounced, bounded repair bursts. Timers capture scalars and this controller,
-- never game objects. The runtime discovers fresh objects inside every pass.
local Controller = {}
Controller.__index = Controller

function Controller.new(config, runtime, repair, schedule, log)
    return setmetatable({ config = config, runtime = runtime, repair = repair,
        schedule = schedule, log = log, epoch = 0, enabled = config.AutoRepair }, Controller)
end

function Controller:pass(reason, diagnosticOnly)
    self.log("PASS " .. reason)
    local ok, err = pcall(function()
        local snapshot = self.runtime:snapshot(self.log)
        self.repair.run(snapshot, self.runtime, self.config, self.log, diagnosticOnly)
    end)
    if not ok then self.log("ERROR pass aborted: " .. tostring(err)) end
end

function Controller:request(reason, manual)
    if not manual and not self.enabled then return end
    self.epoch = self.epoch + 1
    local epoch = self.epoch
    self.log("QUEUED " .. reason .. "; waiting for player initialization")
    for index, offset in ipairs(self.config.RetryOffsetsMs) do
        local passIndex = index
        self.schedule(self.config.SettleDelayMs + offset, function()
            if self.epoch == epoch and (manual or self.enabled) then
                self:pass(reason .. " attempt=" .. passIndex, false)
            end
        end)
    end
end

function Controller:toggle()
    self.enabled = not self.enabled
    self.epoch = self.epoch + 1 -- invalidate all queued repair passes
    self.log("AUTO " .. (self.enabled and "enabled" or "disabled"))
    if self.enabled then self:request("automatic repair enabled", false) end
end

return Controller
