// A human's recorded Star Wars Arcade run, replayed through the cabinet inputs only: the route
// probe's input prefix to the Super Star Destroyer trench, then manual play that completes the
// trench objective, reaches the next stage, crashes into the surface twice and is shot down to the
// continue offer. The recording holds no game state, so it reproduces only if emulation does.
// Every game-state and shield change from the trench handoff on, and the RAM checksums at the three
// handoff points the recordings were made at, must match exactly on the interpreter (silent) and
// the native recompile (with sound), with no interpreter fallback.
#include "../../../src/games/swa/profile.h"
#include "../../../src/host/input_replay.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace game::swa;

namespace {

constexpr const char *RECORDING = "tests/games/swa/recordings/manual-trench-to-continue.inputs";
constexpr uint32_t SEQUENCE = 0x40f008, SUBSEQUENCE = 0x40f009, SHIELD = 0x40f772;
constexpr size_t TRACE_FROM = 16600;

struct checkpoint { size_t frames; uint64_t hash; const char *what; };
const checkpoint CHECKPOINTS[] = {
	{ 16600, 0xc16a83c62483b8dbull, "trench handoff (probe route)" },
	{ 21635, 0x2a75b44aa4d36d73ull, "next stage reached (manual)" },
	{ 24755, 0x58d26030f4c3e5cbull, "continue offer (manual)" },
};

// (frame index whose run made the change, sequence, subsequence, shield)
struct state { size_t frame; unsigned seq, sub, shield; bool operator==(const state &) const = default; };
const std::vector<state> EXPECTED = {
	{ 16618, 13, 5, 576 },     // ceiling hit just after the neutral-control handoff
	{ 17610, 13, 5, 512 },
	{ 17774, 13, 5, 448 },
	{ 17956, 13, 5, 384 },
	{ 17961, 13, 6, 384 },     // trench objective complete
	{ 18203, 13, 8, 384 },
	{ 19385, 13, 9, 384 },
	{ 19394, 13, 1, 384 },     // next stage: briefing
	{ 20418, 13, 2, 384 },
	{ 22016, 13, 3, 384 },     // next-stage flight
	{ 22330, 13, 3, 1 },       // surface crash
	{ 24174, 13, 3, 0 },       // second surface crash
	{ 24498, 13, 3, 65535 },   // shot down
	{ 24499, 13, 11, 65535 },
	{ 24644, 13, 12, 65535 },  // continue offer
};

std::vector<state> replay(const std::string &dir, const input_replay::recording &rec, bool native)
{
	model1 m(load_regions(dir, ID));
	if (native) {
		m.enable_sound();
		rec::hints map;
		assert(map.read("assets/games/swa/swa_map.txt"));
		rec::attach(m.cpu(), "swa");
		assert(rec::build_native(m.program_rom(), map));
		m.run_cpu = [](int n) { rec::run(n); };
	}
	m.reset();
	if (!native) m.run_cpu = [&](int c) { m.cpu().run(c); };
	m.set_drawing(false);
	const char *mode = native ? "native" : "interpreter";
	std::vector<state> changes;
	state last{};
	size_t next_checkpoint = 0;
	for (size_t f = 0; f < rec.frames.size(); f++) {
		const auto error = input_replay::apply(m, rec.frames[f]);
		if (!error.empty()) { fprintf(stderr, "frame %zu: %s\n", f, error.c_str()); assert(false); }
		m.run_frame();
		m.m_audio_out.clear();
		const state now{ f, m.read8(SEQUENCE), m.read8(SUBSEQUENCE), m.read16(SHIELD) };
		if (f >= TRACE_FROM && (now.seq != last.seq || now.sub != last.sub || now.shield != last.shield)) changes.push_back(now);
		last = now;
		if (next_checkpoint < std::size(CHECKPOINTS) && f + 1 == CHECKPOINTS[next_checkpoint].frames) {
			const auto &c = CHECKPOINTS[next_checkpoint++];
			const uint64_t h = input_replay::ram_hash(m);
			if (h != c.hash) {
				fprintf(stderr, "SWA recording (%s): %s after %zu frames: RAM hash %016llx, expected %016llx\n", mode, c.what, c.frames,
					(unsigned long long)h, (unsigned long long)c.hash);
				assert(false);
			}
			printf("SWA recording (%s): %s at %zu frames matches\n", mode, c.what, c.frames);
		}
	}
	assert(next_checkpoint == std::size(CHECKPOINTS));
	return changes;
}

void check(const std::vector<state> &changes, const char *mode)
{
	if (changes == EXPECTED) return;
	fprintf(stderr, "SWA recording (%s): game-state changes differ\n", mode);
	for (const auto &s : changes) fprintf(stderr, "  frame %zu seq %u sub %u shield %u\n", s.frame, s.seq, s.sub, s.shield);
	assert(false);
}

}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <swa regions dir>\n", argv[0]); return 2; }
	input_replay::recording rec;
	if (const auto error = input_replay::load(RECORDING, ID, rec); !error.empty()) { fprintf(stderr, "%s\n", error.c_str()); return 1; }
	assert(rec.frames.size() == CHECKPOINTS[std::size(CHECKPOINTS) - 1].frames && rec.expected_ram == CHECKPOINTS[2].hash);
	const auto interpreter = replay(argv[1], rec, false);
	check(interpreter, "interpreter");
	const auto native = replay(argv[1], rec, true);
	check(native, "native");
	if (rec::fallback_count()) {
		fprintf(stderr, "SWA recording: native code fell back to the interpreter %zu times, first at %06x (regenerate assets/games/swa/swa_map.txt)\n",
			rec::fallback_count(), rec::first_fallback_address());
		return 1;
	}
	puts("SWA recording: trench objective, next stage, crashes and continue offer reproduce on interpreter and native");
	return 0;
}
