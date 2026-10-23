#include "../../../src/machine/model1.h"
#include "../../../src/games/vf/gameplay.h"
#include "../../../src/games/vf/profile.h"
#include "../../../src/games/script.h"
#include "../../../src/host/regions.h"
#include "../../../src/rt/native.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

using game::vf::gameplay;
using game::vf::gameplay_state;

static void settings_and_cheats()
{
	model1::regions regions;
	regions.title = game::vf::ID;
	regions.maincpu.resize(0x2000000, 0xff);
	model1 m(std::move(regions));
	gameplay vf(m);
	auto state = std::static_pointer_cast<gameplay_state>(m.game_state());

	assert(vf.assignment_value(gameplay::assignment::language) == -1);
	uint8_t *ee = m.eeprom();
	std::fill_n(ee, model1::EEPROM_SIZE, 0);
	ee[0] = 'S'; ee[1] = 'E'; ee[2] = 'G'; ee[3] = 'A';
	assert(vf.set_assignment(gameplay::assignment::language, 1));
	assert(ee[26] == 0 && ee[27] == 1);
	assert(vf.assignment_value(gameplay::assignment::language) == 1);
	assert(!vf.set_assignment(gameplay::assignment::language, 1));
	assert(vf.set_assignment(gameplay::assignment::language, 0));
	assert(vf.assignment_value(gameplay::assignment::language) == 0);

	auto &nv = m.nvram();
	nv[0xe007] = nv[0xe008] = 0;
	nv[0xe009] = 0xa0;
	vf.set_kage_unmasked(false);
	assert(nv[0xe009] == 0xa0);
	vf.set_kage_unmasked(true);
	state->frame_begin();
	assert(nv[0xe009] == 0xa1);
	vf.set_kage_unmasked(false);
	assert(nv[0xe009] == 0xa0);
	nv[0xe007] = 3;
	vf.set_kage_unmasked(true);
	vf.set_kage_unmasked(false);
	assert(nv[0xe009] == 0xa1);

	assert(vf.set_random_player(0, "*"));
	assert(state->m_player_mode[0] == 1);
	assert(vf.set_random_player(1, "#7"));
	assert(state->m_player_mode[1] == 3 && state->m_player_seed[1] == 7);
	assert(!vf.set_random_player(-1, "*"));
	assert(!vf.set_random_player(2, "*"));
	assert(!vf.set_random_player(1, "#8"));
	assert(state->m_player_mode[1] == 3 && state->m_player_seed[1] == 7);
	assert(vf.set_random_player(0, ""));
	assert(state->m_player_mode[0] == 0 && state->m_player_seed[0] == 0);

	constexpr uint32_t clock = 0xfe2190;
	const uint8_t instruction[] = { 0xd2, 0xf2, 0xf5, 0xf0, 0x51, 0xff };
	std::copy(std::begin(instruction), std::end(instruction), m.program_rom().begin() + clock);
	assert(vf.set_no_time_limit(true));
	assert(m.program_rom()[clock] == 0xf2);
	assert(!vf.set_no_time_limit(true));
	assert(vf.set_no_time_limit(false));
	assert(std::equal(std::begin(instruction), std::end(instruction), m.program_rom().begin() + clock));

	state->m_clock_held = true;
	state->m_run_on = true;
	state->m_fought.insert(1);
	state->m_built_run.insert(2);
	state->m_akira_held[0] = 3;
	m.reset();
	assert(!state->m_clock_held && !state->m_run_on);
	assert(state->m_fought.empty() && state->m_built_run.empty());
	assert(state->m_akira_held[0] == 0);
}

static void ranking_names()
{
	model1::regions regions;
	regions.title = game::vf::ID;
	model1 m(std::move(regions));
	gameplay vf(m);
	auto state = std::static_pointer_cast<gameplay_state>(m.game_state());
	constexpr uint32_t at = gameplay_state::RANKING;
	for (uint32_t b = 0; b < gameplay_state::RANK_ENTRY; b++) m.nvram()[at + b] = uint8_t(b);
	state->m_rank_names[0] = "JACPAIWOLF";
	const std::string text = vf.ranking_names();
	assert(text == "00010203040506070809 JACPAIWOLF\n");
	state->m_rank_names[0].clear();
	vf.set_ranking_names(text);
	assert(state->m_rank_names[0] == "JACPAIWOLF");
	assert(vf.ranking_names() == text);

	m.nvram()[at + 6] = 'A';
	const uint32_t version = vf.ranking_names_version();
	vf.set_ranking_names(text);
	assert(state->m_rank_names[0] == "JACPAIWOLF");
	assert(vf.ranking_names_version() == version + 1);
	m.nvram()[at] = 0xff;
	vf.set_ranking_names(text);
	assert(vf.ranking_names().empty());
}

static void rom_randomizer(const char *dir, bool native)
{
	model1 m(load_regions(dir, game::vf::ID));
	gameplay vf(m);
	auto state = std::static_pointer_cast<gameplay_state>(m.game_state());
	const auto original = m.program_rom();
	const auto &names = vf.random_fighter_names();
	assert(names.size() > 100);
	assert(m.program_rom() == original);
	for (const auto &[name, seed] : names) {
		assert(!name.empty() && name.size() <= 10);
		assert(std::all_of(name.begin(), name.end(), [](char c) { return c >= 'A' && c <= 'Z'; }));
		assert(seed >= 1 && seed <= gameplay::RANDOM_SEEDS);
	}
	const auto chosen = *names.begin();
	assert(vf.set_random_player(0, chosen.first));
	assert(state->m_player_seed[0] == chosen.second);
	assert(vf.set_random_player(1, "#2"));
	assert(!vf.set_random_player(0, "NOTAFIGHTER"));
	assert(state->m_player_seed[0] == chosen.second);
	vf.set_strands_moved(true);
	state->m_random_state = 12345;
	vf.set_random_fighter(true, true, true);
	assert(state->m_random_names[0xfd4f99] == chosen.first);
	assert(state->m_secret_names.at(0xfd3f6f) == "JEFF");
	const auto first = std::vector<uint8_t>(m.program_rom().begin() + 0xfd4f99, m.program_rom().begin() + 0xfd4f99 + game::vf::RECORD_SIZE);
	const uint32_t table = state->m_moves_held.at(0xfd4f99);
	const auto moves = std::vector<uint8_t>(m.program_rom().begin() + table, m.program_rom().begin() + table + game::vf::MOVE_COUNT * 4);
	state->random_akira_record(0xfd4f99, 2, chosen.second);
	state->random_fighter_specials();
	state->random_fighter_hold_moves();
	assert(std::equal(first.begin(), first.end(), m.program_rom().begin() + 0xfd4f99));
	assert(std::equal(moves.begin(), moves.end(), m.program_rom().begin() + table));

	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vf/vf_map.txt"));
		rec::attach(m.cpu(), "vf");
		assert(rec::build_native(m.program_rom(), map));
		m.run_cpu = [](int n) { rec::run(n); };
	}
	m.set_render_scale(2);
	m.set_smooth(true);
	m.reset();
	auto play = game::make_auto_play(game::vf::ID, "", 12345, false);
	assert(play);
	for (int f = 1; f <= 1800; f++) {
		auto inputs = play->frame(f + 2700);
		if (native) inputs.pressed[0] &= uint8_t(~0x20);   // Keep a solo run to exercise random CPU opponents.
		for (int p = 0; p < 3; p++) m.m_inputs[p] = uint8_t(~inputs.pressed[p]);
		m.run_frame();
		m.render_smooth(double(m.frame_number()) - 0.5);
	}
	assert(m.past_boot());
	if (native && rec::fallback_count())
		fprintf(stderr, "randomized native fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
	if (native) assert(rec::fallback_count() == 0);
	assert(!state->m_random_names.empty());
	if (native) assert(!state->m_fought.empty());
	assert(state->m_random_names[0xfd4f99] == chosen.first);
	vf.set_strands_moved(false);
	vf.set_random_fighter(false);
	assert(m.program_rom() == original);
	printf("VF randomizer: %zu named fighters; seeded/secret/CPU fighters, %s execution, restoration passed\n",
		names.size(), native ? "native" : "interpreter");
}

int main(int argc, char **argv)
{
	settings_and_cheats();
	ranking_names();
	if (argc > 1) rom_randomizer(argv[1], argc > 2 && std::string(argv[2]) == "--native");
	puts("VF gameplay tests passed");
}
