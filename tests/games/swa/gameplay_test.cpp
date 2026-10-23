// Star Wars Arcade's scripted play (src/games/swa/script.cpp) on the interpreter, both scenarios
// with checks on: credits, the flight, hits, game over, the rankings entry and back to attract.
// The default run also checks the Digital Sound Board's in-game music (a looping stream other than
// the attract theme), and that a silent run (the headless tools': model1cov, model1lock) plays the
// same game as one with sound. Last, the default run again on the native recompile of the shipped map
// (assets/games/swa/swa_map.txt), with no interpreter fallback.
#include "../../../src/games/script.h"
#include "../../../src/games/swa/profile.h"
#include "../../../src/host/regions.h"
#include "../../../src/machine/dsbz80.h"
#include "../../../src/machine/m1audio.h"
#include "../../../src/rt/native.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace game::swa;

namespace {

constexpr uint32_t THEME_START = 0x334000, THEME_END = 0x3f4000;   // the attract's title theme
constexpr int SILENT_FRAMES = 8000;

uint64_t ram_hash(model1 &m)
{
	uint64_t h = 1469598103934665603ull;
	for (auto *ram : { &m.nvram(), &m.work_ram() })
		for (uint8_t b : *ram) h = (h ^ b) * 1099511628211ull;
	return h;
}

struct music
{
	bool in_game = false, looped = false;
	uint32_t last = 0;
	uint64_t last_triggers = 0;
	int last_state = 0;

	void watch(const dsbz80 &d, bool flying)
	{
		const uint32_t pos = d.position();
		if (flying && d.state() == 2 && (pos < THEME_START || pos >= THEME_END)) in_game = true;
		// the stream jumped back on its own: the loop, not a new command
		if (flying && d.state() == 2 && last_state == 2 && d.triggers() == last_triggers && pos + 0x1000 < last)
			looped = true;
		last = pos;
		last_triggers = d.triggers();
		last_state = d.state();
	}
};

uint64_t run(const std::string &dir, const std::string &scenario, bool sound, int frames, bool checks, music *dsb = nullptr,
	bool native = false)
{
	model1 m(load_regions(dir, ID));
	if (sound) m.enable_sound();
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/swa/swa_map.txt"));
		rec::attach(m.cpu(), "swa");
		assert(rec::build_native(m.program_rom(), map));
		m.run_cpu = [](int n) { rec::run(n); };
	}
	m.reset();
	if (!native) m.run_cpu = [&](int c) { m.cpu().run(c); };
	auto play = game::make_auto_play(ID, scenario, 12345, checks);
	assert(play);
	if (checks) assert(frames >= play->min_frames());
	bool flying = false, over = false;
	for (int f = 1; f <= frames; f++) {
		const game::script_inputs in = play->frame(f, m);
		for (int c = 0; c < 3; c++)
			if (in.analog[c] >= 0) assert(m.set_analog_input(c, uint8_t(in.analog[c])));
		for (int p = 0; p < 3; p++) m.m_inputs[p] = uint8_t(~in.pressed[p]);
		m.run_frame();
		m.m_audio_out.clear();
		const std::string problem = play->check(f, m);
		if (!problem.empty()) {
			fprintf(stderr, "%s\n", problem.c_str());
			assert(false);
		}
		if (dsb) {
			const uint16_t time = uint16_t(m.nvram()[0xf334] | m.nvram()[0xf335] << 8);
			if (!flying && time != 0xffff && time != 0) flying = true;
			if (flying && m.nvram()[0xf008] == 14) over = true;
			dsb->watch(*m.audio()->dsb(), flying && !over);
		}
	}
	if (checks) {
		const std::string problem = play->finish();
		if (!problem.empty()) {
			fprintf(stderr, "%s\n", problem.c_str());
			assert(false);
		}
	}
	return ram_hash(m);
}

}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <swa regions dir>\n", argv[0]);
		return 1;
	}
	assert(!game::make_auto_play(ID, "no-such-scenario", 1, false));
	auto quiet = game::make_auto_play(ID, "", 1, false);
	assert(quiet && quiet->finish().empty());

	music dsb;
	run(argv[1], "", true, 22000, true, &dsb);
	assert(dsb.in_game && dsb.looped);
	puts("SWA in-game DSB music: a looping stream outside the attract theme");

	run(argv[1], "gunner", true, 22000, true);

	const uint64_t with_sound = run(argv[1], "", true, SILENT_FRAMES, false);
	const uint64_t silent = run(argv[1], "", false, SILENT_FRAMES, false);
	assert(with_sound == silent);
	printf("SWA silent run matches the run with sound through frame %d\n", SILENT_FRAMES);

	music native_dsb;
	run(argv[1], "", true, 22000, true, &native_dsb, true);
	assert(native_dsb.in_game && native_dsb.looped);
	assert(rec::fallback_count() == 0);
	puts("SWA native: the scripted game and its music with no interpreter fallback");
	puts("SWA gameplay tests passed");
}
