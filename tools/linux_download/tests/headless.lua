-- Run by inside.sh with the download's spite9-lua: a session with the Dummy backend, PgD4S's instrument and
-- effect each on a track, a few seconds of processing, then save and close.
local function say(s) io.stderr:write("STEP " .. s .. "\n"); io.stderr:flush() end
local dir = os.getenv("TEST_SESSION_DIR")
AudioEngine:set_backend("None (Dummy)", "", "")
local s = create_session(dir, "download-test", 48000)
assert(s, "create_session failed")
say("session created")
for _, uri in ipairs({ "https://github.com/timothyschoen/plugdata", "https://github.com/timothyschoen/plugdata-fx" }) do
	local p = ARDOUR.LuaAPI.new_plugin(Session, uri, ARDOUR.PluginType.LV2, "")
	assert(not p:isnil(), "could not load " .. uri)
	local tl = Session:new_audio_track(2, 2, ARDOUR.RouteGroup(), 1, "pd", ARDOUR.PresentationInfo.max_order, ARDOUR.TrackMode.Normal, true, false)
	tl:front():add_processor_by_index(p, 0, nil, true)
	say("loaded " .. p:display_name() .. " (" .. uri .. ")")
end
sleep(3)
say("processed for 3 seconds")
Session:save_state("", false, false, false, false, false)
say("session saved")
close_session()
say("DONE")
