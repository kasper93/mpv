local mp_globals = {
    "mp",
    "mp.command",
    "mp.commandv",
    "mp.command_native",
    "mp.command_native_async",
    "mp.abort_async_command",
    "mp.del_property",
    "mp.get_property",
    "mp.get_property_osd",
    "mp.get_property_bool",
    "mp.get_property_number",
    "mp.get_property_native",
    "mp.set_property",
    "mp.set_property_bool",
    "mp.set_property_number",
    "mp.set_property_native",
    "mp.get_time",
    "mp.add_key_binding",
    "mp.add_forced_key_binding",
    "mp.remove_key_binding",
    "mp.register_event",
    "mp.unregister_event",
    "mp.observe_property",
    "mp.unobserve_property",
    "mp.add_timeout",
    "mp.add_periodic_timer",
    "mp.get_opt",
    "mp.get_script_name",
    "mp.get_script_directory",
    "mp.osd_message",
    "mp.get_wakeup_pipe",
    "mp.get_next_timeout",
    "mp.dispatch_events",
    "mp.register_idle",
    "mp.unregister_idle",
    "mp.enable_messages",
    "mp.register_script_message",
    "mp.unregister_script_message",
    "mp.create_osd_overlay",
    "mp.get_osd_size",
    "mp.msg.fatal",
    "mp.msg.error",
    "mp.msg.warn",
    "mp.msg.info",
    "mp.msg.verbose",
    "mp.msg.debug",
    "mp.msg.trace",
}
std = "min+mp"
stds = { mp = { read_globals = mp_globals } }
files["player/lua/defaults.lua"] = { globals = mp_globals }
files["player/lua/auto_profiles.lua"] = { globals = { "p", "get" } }
max_line_length = 100

-- TODO: Remove everything below this line
allow_defined = true
max_line_length = 120
local fixed = {
    "player/lua/assdraw.lua",
    "player/lua/auto_profiles.lua",
    "player/lua/defaults.lua",
    "player/lua/fzy.lua",
    "player/lua/input.lua",
    "player/lua/options.lua",
    "player/lua/stats.lua",
    "TOOLS/lua/acompressor.lua",
    "TOOLS/lua/audio-hotplug-test.lua",
    "TOOLS/lua/ontop-playback.lua",
    "TOOLS/lua/pause-when-minimize.lua",
    "TOOLS/lua/skip-logo.lua",
}
for _, path in ipairs(fixed) do
    files[path]["allow_defined"] = false
    files[path]["max_line_length"] = 100
end
