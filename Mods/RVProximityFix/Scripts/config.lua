return {
    -- Experimental: calls completed do not prove that audible proximity is fixed.
    AutoRepair = true,
    -- Each construction/restart event cancels the previous burst. No UObject
    -- references are held by timers, and each pass discovers the current world.
    SettleDelayMs = 3000,
    RetryOffsetsMs = { 0, 3000, 7000 },
    RepairAttachment = true,
    RestoreMissingAttenuation = true,
    -- Loaded assets only; never synchronously loads packages during travel.
    AttenuationPath = "/Game/Ride/Audio/Attenuation/SA_VOIP.SA_VOIP",
    -- A valid existing attenuation/effect chain is preserved.
    DiagnosticKey = "F8",
    RepairKey = "F9",
    ToggleAutoKey = "F10",
}
