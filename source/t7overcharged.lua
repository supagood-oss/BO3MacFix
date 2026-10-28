-- Implemented contracts only. Unknown functions must remain absent.
local createDirectory, directoryExists, clipboardGet, clipboardSet, nativePrint, nativeCall = ...
if EnableGlobals then EnableGlobals() end

local function log(message)
    nativePrint("[T7Overcharged] " .. tostring(message))
end
local function call(operation, ...)
    if type(nativeCall) ~= "function" then
        error("BO3MacFix native helper is missing: " .. operation, 2)
    end
    return nativeCall(operation, ...)
end
local function available(name)
    return type(nativeCall) == "function" and nativeCall("capability", name) == "1"
end
local function provide(name, members)
    local library = rawget(_G, name) or {}
    -- Remove the old PR's catch-all __index when reusing a VM.
    setmetatable(library, nil)
    for key in pairs(library) do library[key] = nil end
    for key, implementation in pairs(members) do library[key] = implementation end
    _G[name] = library
    return library
end
-- Current WriteFile accepts exactly true or 1 after string conversion.
-- Its fourth argument is not read (AAE DLL RVA 0x17da58).
local function flag(value)
    return value == true or value == 1 or value == "1" or value == "true"
end
local function path(value)
    return (tostring(value):gsub("\\", "/"))
end
local function setup()
    local io = require("io")
    local function exists(filename)
        local file = io.open(path(filename), "rb")
        if not file then return false end
        file:close()
        return true
    end
    local function read(filename)
        local file = io.open(path(filename), "rb")
        if not file then return nil end
        local bytes = file:read("*a")
        file:close()
        return bytes
    end
    local function write(filename, contents, append)
        filename = path(filename)
        local parent = filename:match("^(.*)/[^/]+$")
        if parent and parent ~= "" and not directoryExists(parent) and not createDirectory(parent) then
            return false
        end
        local file = io.open(filename, flag(append) and "ab" or "wb")
        if not file then return false end
        local ok = file:write(tostring(contents or ""))
        local closed = file:close()
        return ok ~= nil and closed ~= nil
    end
    provide("FileIO", {
        FileExists = exists, ReadFile = read, WriteFile = write,
        Copy = function(source, destination, failIfExists)
            if flag(failIfExists) and exists(destination) then return false end
            local bytes = read(source)
            return bytes ~= nil and write(destination, bytes, false) or false
        end,
        FileSize = function(filename)
            local file = io.open(path(filename), "rb")
            if not file then return 0 end
            local size = file:seek("end")
            file:close()
            return size or 0
        end,
        DirectoryExists = function(filename) return directoryExists(path(filename)) end,
        CreateDirectory = function(filename) return createDirectory(path(filename)) end,
        ClipboardGet = clipboardGet,
        ClipboardSet = function(text) clipboardSet(tostring(text or "")) end,
        Wine = function() return false end,
    })
    provide("Files", {
        Exists = function(filename) return exists(filename) or directoryExists(path(filename)) end,
        IsDir = function(filename) return directoryExists(path(filename)) end,
        MakeDir = function(filename) return createDirectory(path(filename)) end,
        Delete = function(filename) return call("file.delete", path(filename)) == "1" end,
    })
    local crypto = provide("Crypto", {})
    -- JSON is also a common map-owned encoder namespace. Extend its native
    -- configuration members without deleting the map's encode/decode helpers.
    local json = rawget(_G, "JSON") or {}
    json.ReadJson = function(filename, section, key, kind, default, readonly)
            return call("json.read", path(filename), tostring(section or ""), tostring(key), tostring(kind),
                default == nil and "" or tostring(default), (readonly == false or readonly == 0 or readonly == "0") and "0" or "1")
        end
    json.WriteJson = function(filename, section, key, kind, value)
            return call("json.write", path(filename), tostring(section or ""), tostring(key), tostring(kind), tostring(value)) == "1"
        end
    _G.JSON = json
    local legacy = available("save_codec")
    if legacy then
        crypto.Encode = function(text) return call("save.encode", tostring(text)) end
        crypto.Decode = function(text) return call("save.decode", tostring(text)) or "" end
    end
    provide("Console", {
        Print = function(...)
            local values = {}
            for i = 1, select("#", ...) do values[i] = tostring(select(i, ...)) end
            log(table.concat(values, " "))
        end,
        PrintFile = function(name, ...)
            local values = {}
            for i = 1, select("#", ...) do values[i] = tostring(select(i, ...)) end
            return write("BO3MacFix/" .. path(name), table.concat(values, " ") .. "\n", true)
        end,
        ShowExternalConsole = function() return false end,
    })
    local axios = provide("Axios", {})
    if available("http") then
        axios.Get = function(url, timeout)
            -- Dead High's older helper takes a map version instead of seconds.
            if legacy or (type(timeout) == "string" and not tonumber(timeout)) then
                return call("legacy.get", tostring(url), tostring(timeout or ""))
            end
            return call("http.get", tostring(url), tostring(timeout or 5))
        end
        axios.GetUpdate = function(name, url, timeout)
            return call("http.submit", "GET", tostring(name), tostring(url), "", tostring(timeout or 5)) == "1"
        end
        axios.GetRetrieve = function(name)
            -- Missing/pending jobs return empty text; this is not a GET alias.
            return call("http.retrieve", tostring(name)) or ""
        end
        axios.Post = function(name, url, body, timeout)
            if tostring(name):match("^https?://") then
                return call("legacy.post", tostring(name), tostring(url or ""), tostring(body or ""))
            end
            return call("http.submit", "POST", tostring(name), tostring(url), tostring(body or ""), tostring(timeout or 5)) == "1"
        end
        axios.PostAndResponse = function(url, body, version)
            return call("legacy.post_response", tostring(url), tostring(body or ""), tostring(version or ""))
        end
        axios.PostQueued = function(url, body, version, timeoutMs)
            return tonumber(call("legacy.post_queued", tostring(url), tostring(body or ""),
                tostring(version or ""), tostring(timeoutMs or 10000))) or 0
        end
        axios.Result = function(ticket) return call("legacy.result", tostring(ticket)) end
    else
        axios.Get, axios.GetUpdate, axios.GetRetrieve, axios.Post = nil, nil, nil, nil
    end
    -- Credential-dependent APIs stay absent. provide() also removes stale
    -- members when a UI VM is reused, allowing the mod's presence guards to
    -- skip these optional online integrations.
    provide("MainLUA", {
        Revision = function() return tonumber(call("revision")) or 0 end,
        GetHardwareID = function() return call("system.hardware_id") end,
        GetHardwareCPU = function() return call("system.cpu") end,
        ModuleLoaded = function(name) return call("module.loaded", tostring(name)) == "1" end,
        UnloadMod = function()
            local result, err = call("mod.unload")
            if result == nil then error(err or "Mac mod lifecycle adapter is unavailable", 2) end
            return result == "1"
        end,
    })
    provide("DiscordRPC", {
        IsAvailable = function() return false end,
        Enable = function() return false end,
        ClearPresence = function() return false end,
        Shutdown = function() return false end,
    })
    provide("T7Overcharged", {
        HasCapability = available,
        CompatibilityStatus = function() return call("status") end,
    })
    -- Optional authoring/debug libraries: unsupported members remain nil.
    provide("UIErrorHash", {
        Remove = function()
            -- This API enables readable UI diagnostics, not gameplay patches.
            if not Engine or type(Engine.SetDvar) ~= "function" then return false end
            local ok = pcall(function()
                Engine.SetDvar("ui_error_callstack_ship", "1")
                Engine.SetDvar("ui_error_report_delay", "0")
            end)
            return ok
        end,
    })
    provide("HotReload", {})
    -- ZoneWriter uses `if winOs and prefix` for optional Windows debug output.
    -- An empty table passes that guard and then calls a missing execute member.
    _G.winOs = nil
    if Engine and Engine.SetModel == nil then Engine.SetModel = Engine.CreateModel end
    log("Lua contracts initialized; " .. tostring(call("status")))
end
local ok, err = pcall(setup)
if not ok then
    log("initialization failed: " .. tostring(err))
    return false, tostring(err)
end
return true
