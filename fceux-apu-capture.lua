-- Frame counter for simulated button presses
NSFSkipFrames = 2
NSFSkipFrame = -1

-- Whether or not to auto-trim music and make it looping (can be buggy)
autoTrim = true

-- Flag for skipping the first data dump line
skipFirstLine = true

-- Recording counters
quantizeToFrame = 1
frameRecordCounter = 0

-- Parse arguments
if arg == nil then
    arg = ""
end
arg = string.gsub(arg, "%s+", "")
if string.find(arg, "--oneshot") then
    autoTrim = false
    arg = string.gsub(arg, "--oneshot", "")
end
if string.find(arg, "-Q2") then
    quantizeToFrame = 2
    arg = string.gsub(arg, "-Q2", "")
end

-- Music index used to iterate through NSF
targetMusicIndex = arg
currentMusicIndex = 0

if (targetMusicIndex == "") then
    targetMusicIndex = 0
    -- Don't reset if no arguments are provided, e.g. for recording a game instead of a NSF
else
    -- NSF index starts at 1
    targetMusicIndex = targetMusicIndex - 1
    emu.speedmode("normal") -- Set the speed of the emulator
    emu.softreset() -- Reset emulator
end

-- Data record flag
recording = false

if targetMusicIndex == 0 then
    recording = true
end

-- Known PSG state
knownState = {
    square1 = {
        f = -1,
        v = -1,
        d = -1
    },
    square2 = {
        f = -1,
        v = -1,
        d = -1
    },
    triangle = {
        f = -1,
        v = -1
    },
    noise = {
        p = -1,
        v = -1,
        m = -1
    },
    dpcm = {
        v = -1
    }
}

-- Control byte constants
CONTROL_S1F = BIT(0)
CONTROL_S2F = BIT(1)
CONTROL_TF = BIT(2)
CONTROL_S1PARAM = BIT(3)
CONTROL_S2PARAM = BIT(4)
CONTROL_NPARAM = BIT(5)
CONTROL_STATUS_TV = BIT(6)
CONTROL_STATUS_X = BIT(7)

-- Captured data
data = {}

-- Tracking data, used for determining the loop point
trackingData = {}

-- Match identical data to find loop point and trim redundant frames
function lookupDataInterval(a, b)
    local index = -1
    local dataLength = table.getn(trackingData)
    local intervalLength = b - a + 1
    local endIndex = dataLength - intervalLength

    for i = 1, endIndex do
        local match = true
        for j = 0, intervalLength - 1 do
            if trackingData[i + j] ~= trackingData[a + j] then
                match = false
                break
            end
        end
        if match then
            index = i
            break
        end
    end

    return index
end

function count(base, pattern)
    return select(2, string.gsub(base, pattern, ""))
end

function getDataLengthAndLoopPointOffset(loopPointFrameIndex)
    local byteLength = 0
    local loopPointByteIndex = 0

    for i = 1, table.getn(data) do
        if i == loopPointFrameIndex then
            loopPointByteIndex = byteLength
        end
        byteLength = byteLength + count(data[i], ",")
    end

    local cBytes = bit.rshift(byteLength, 8) .. ", " .. AND(byteLength, 255) .. ", "
    cBytes = cBytes .. bit.rshift(loopPointByteIndex, 8) .. ", " .. AND(loopPointByteIndex, 255) .. ", "

    return cBytes;
end

function math.round(x)
    return math.floor(x + 0.5)
end

dmcStore = {}
dmcSamples = 0
dmcTrigger = false

function getDmcIndex(address)
    local index
    for i = 0, dmcSamples - 1 do
        if dmcStore[i].addr == address then
            index = i
            break
        end
    end
    return index
end

function registerDmc(address, length)
    local index = dmcSamples
    dmcStore[index] = {
        addr = address,
        len = length,
        data = {}
    }
    for i = 1, length do
        dmcStore[index].data[i] = memory.readbyteunsigned(address + i - 1)
    end
    dmcSamples = dmcSamples + 1
    return index
end

function processDmcSample()
    local address = sound.get().rp2a03.dpcm.dmcaddress
    local index = getDmcIndex(address)
    if index == nil then
        index = registerDmc(address, sound.get().rp2a03.dpcm.dmcsize)
    end
end

function onDmcSample()
    if recording then
        dmcTrigger = true
    end
end

memory.register(0x4010, onDmcSample)

function make2Bytes(number)
    return bit.rshift(number, 8) .. ", " .. AND(number, 255)
end

function printDmcSamples()
    emu.print(dmcSamples .. ", ")
    if dmcSamples > 0 then
        local lengths = ""
        for i = 1, dmcSamples do
            lengths = lengths .. make2Bytes(dmcStore[i - 1].len) .. ", "
        end
        emu.print(lengths)
        local samples = ""
        local sampleColCounter = 0
        for i = 1, dmcSamples do
            local dmcSample = dmcStore[i - 1]
            for j = 1, dmcSample.len do
                samples = samples .. dmcSample.data[j] .. ", "
                sampleColCounter = sampleColCounter + 1
                if sampleColCounter == 32 then
                    samples = samples .. "\r\n"
                    sampleColCounter = 0
                end
            end
            if i ~= dmcSamples then
                samples = samples .. "\r\n"
                sampleColCounter = 0
            end
        end
        emu.print(samples)
    end
end

-- Game loop
while true do
    if recording then
        frameRecordCounter = frameRecordCounter + 1
        if frameRecordCounter == quantizeToFrame then
            -- Compute new state
            rawState = sound.get().rp2a03
            state = {
                square1 = {
                    f = math.floor(rawState.square1.frequency),
                    v = math.floor((rawState.square1.volume * 15) + 0.5),
                    d = rawState.square1.duty
                },
                square2 = {
                    f = math.floor(rawState.square2.frequency),
                    v = math.floor((rawState.square2.volume * 15) + 0.5),
                    d = rawState.square2.duty
                },
                triangle = {
                    f = math.floor(rawState.triangle.frequency),
                    v = math.floor(rawState.triangle.volume + 0.5),
                },
                noise = {
                    p = rawState.noise.regs.frequency,
                    v = math.floor((rawState.noise.volume * 15) + 0.5),
                    m = rawState.noise.short
                },
                dpcm = {
                    v = math.floor(rawState.dpcm.volume + 0.5)
                }
            }
            -- Compute state changes
            checkS1F = (state.square1.f ~= knownState.square1.f)
            checkS2F = (state.square2.f ~= knownState.square2.f)
            checkTF = (state.triangle.f ~= knownState.triangle.f)
            checkS1Param = ((state.square1.v ~= knownState.square1.v) or (state.square1.d ~= knownState.square1.d))
            checkS2Param = ((state.square2.v ~= knownState.square2.v) or (state.square2.d ~= knownState.square2.d))
            checkNParam = ((state.noise.v ~= knownState.noise.v) or (state.noise.p ~= knownState.noise.p))
            checkDMCKill = (state.dpcm.v == 0) and (knownState.dpcm.v == 1)
            checkXParam = ((state.noise.m ~= knownState.noise.m) or (dmcTrigger) or (checkDMCKill))

            -- Build tracking state
            -- This only includes pitches, and records absolute values, not changes
            -- Used in tracking the loop point
            local trackingState = ""
            if rawState.square1.volume > 0 then
                trackingState = trackingState .. "[" .. math.round(rawState.square1.midikey) .. "]"
            end
            if rawState.square2.volume > 0 then
                trackingState = trackingState .. "[" .. math.round(rawState.square2.midikey) .. "]"
            end
            if rawState.triangle.volume > 0 then
                trackingState = trackingState .. "[" .. math.round(rawState.triangle.midikey) .. "]"
            end
            if rawState.noise.volume > 0 then
                trackingState = trackingState .. "[" .. rawState.noise.regs.frequency .. "]"
            end
            if rawState.dpcm.volume > 0 then
                trackingState = trackingState .. "[" .. rawState.dpcm.regs.frequency .. "]"
            end

            -- Set control byte
            control = 0;
            if checkS1F then
                control = OR(control, CONTROL_S1F)
            end
            if checkS2F then
                control = OR(control, CONTROL_S2F)
            end
            if checkTF then
                control = OR(control, CONTROL_TF)
            end
            if checkS1Param then
                control = OR(control, CONTROL_S1PARAM)
            end
            if checkS2Param then
                control = OR(control, CONTROL_S2PARAM)
            end
            if checkNParam then
                control = OR(control, CONTROL_NPARAM)
            end
            if state.triangle.v > 0.5 then
                control = OR(control, CONTROL_STATUS_TV)
            end
            if checkXParam then
                control = OR(control, CONTROL_STATUS_X)
            end

            -- Build state changes
            stateData = tostring(control)
            if checkS1F then
                stateData = stateData .. ", " .. bit.rshift(state.square1.f, 8) .. ", " .. AND(state.square1.f, 255)
            end
            if checkS2F then
                stateData = stateData .. ", " .. bit.rshift(state.square2.f, 8) .. ", " .. AND(state.square2.f, 255)
            end
            if checkTF then
                stateData = stateData .. ", " .. bit.rshift(state.triangle.f, 8) .. ", " .. AND(state.triangle.f, 255)
            end
            if checkS1Param then
                stateData = stateData .. ", " .. OR(bit.lshift(state.square1.d, 4), state.square1.v)
            end
            if checkS2Param then
                stateData = stateData .. ", " .. OR(bit.lshift(state.square2.d, 4), state.square2.v)
            end
            if checkNParam then
                stateData = stateData .. ", " .. OR(bit.lshift(state.noise.p, 4), state.noise.v)
            end
            if checkXParam then
                local dmcTriggerBit = 0
                if dmcTrigger then
                    dmcTriggerBit = 1
                end
                local noiseModeBit = 0
                if state.noise.m then
                    noiseModeBit = 1
                end
                local dmcKillBit = 0
                if checkDMCKill then
                    dmcKillBit = 1
                end
                stateData = stateData .. ", " .. OR(
                    OR(bit.lshift(noiseModeBit, 7), bit.lshift(dmcTriggerBit, 6)),
                    bit.lshift(dmcKillBit, 5)
                )
                if dmcTrigger then
                    processDmcSample()
                    local dmcLoopBit = 0
                    if rawState.dpcm.dmcloop then
                        dmcLoopBit = 1
                    end
                    stateData = stateData .. ", " .. OR(
                        bit.lshift(rawState.dpcm.regs.frequency, 4),
                        AND(getDmcIndex(rawState.dpcm.dmcaddress), 0x0F)
                    ) .. ", " .. OR(bit.lshift(dmcLoopBit, 7), rawState.dpcm.dmcseed)
                    -- Debug Sample Index
                    -- emu.print(getDmcIndex(rawState.dpcm.dmcaddress))
                end
            end

            -- Clear DMC trigger
            dmcTrigger = false

            -- Print frame state
            if skipFirstLine then
                skipFirstLine = false
            else
                table.insert(data, stateData .. ", ")
                table.insert(trackingData, trackingState)
            end

            -- Capture known state
            knownState = state

            -- Reset counter
            frameRecordCounter = 0
        end
    end

    -- Navigate to specified music and start recording
    if NSFSkipFrame == -1 then
        if currentMusicIndex < targetMusicIndex then
            joypad.set(1, {right = true})
            NSFSkipFrame = NSFSkipFrames
            currentMusicIndex = currentMusicIndex + 1
            if currentMusicIndex == targetMusicIndex then
                recording = true
            end
        end
    else
        NSFSkipFrame = NSFSkipFrame - 1
    end

    if NSFSkipFrame == 0 then
        joypad.set(1, { right = false })
        NSFSkipFrame = -1
    end

    if joypad.get(1).select then
        if autoTrim then
            local length = table.getn(data)
            local searchLength = 1
            local loopPoint = 0
            local found = false
            emu.print("Searching for loop point in " .. length .. " frames...")
            while found == false do
                local foundIndex = lookupDataInterval(length - searchLength + 1, length)
                if foundIndex == -1 then
                    found = true
                    searchLength = searchLength - 1
                    emu.print("Loop point determined at frame " .. loopPoint)
                    emu.print(searchLength .. " frames of data are redundant")
                    emu.print("Trimming...")
                    for i = 1, searchLength do
                        data[#data] = nil
                    end
                    emu.print("Done")
                    emu.print(getDataLengthAndLoopPointOffset(loopPoint))
                    emu.print("0,")
                    printDmcSamples()
                    for i = 1, table.getn(data) do
                        emu.print(data[i])
                    end
                else
                    loopPoint = foundIndex
                    searchLength = searchLength + 1
                end
            end
        else
            emu.print(getDataLengthAndLoopPointOffset(0))
            emu.print("0,")
            printDmcSamples()
            for i = 1, table.getn(data) do
                emu.print(data[i])
            end
        end
        joypad.set(1, { select = false })
    end

    -- Return control to FCEUX
    emu.frameadvance()
end
