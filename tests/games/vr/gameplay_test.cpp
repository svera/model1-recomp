#include "../../../src/games/vr/gameplay.h"
#include "../../../src/machine/model1.h"
#include "../../../src/host/regions.h"
#include "../../../src/host/png.h"
#include "../../../src/games/script.h"
#include "../../../src/rt/native.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void language_settings(bool board)
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	if (board) regions.ioboard.resize(0x10000, 0xff);
	model1 machine(std::move(regions));
	assert(bool(machine.eeprom_words()) == board);
	using game::vr::language_value;
	using game::vr::set_language;
	auto read = [&] {
		std::array<uint16_t, 64> words;
		for (int i = 0; i < 64; i++)
			words[i] = board ? machine.eeprom_words()[i] :
				uint16_t(machine.eeprom()[2 * i] << 8 | machine.eeprom()[2 * i + 1]);
		return words;
	};
	auto write = [&](const std::array<uint16_t, 64> &words) {
		for (int i = 0; i < 64; i++)
			if (board) machine.eeprom_words()[i] = words[i];
			else {
				machine.eeprom()[2 * i] = uint8_t(words[i] >> 8);
				machine.eeprom()[2 * i + 1] = uint8_t(words[i]);
			}
	};
	std::array<uint16_t, 64> words{};
	write(words);
	assert(language_value(machine) == -1);
	assert(!set_language(machine, 1) && read() == words);
	for (int i = 0; i < 64; i++) words[i] = uint16_t(0x4321 + 173 * i);
	words[0] = 0x5345; words[1] = 0x4741;
	words[13] = 0xab00;
	write(words);
	assert(language_value(machine) == 0);
	assert(set_language(machine, 1));
	auto english = read();
	assert(language_value(machine) == 1 && english[13] == 0xab02);
	for (int i = 0; i < 64; i++)
		if (i != 4 && i != 13) assert(english[i] == words[i]);
	uint16_t crc = 0;
	for (int bit = 5 * 16; bit < 64 * 16; bit++) {
		const bool input = (english[bit / 16] >> (15 - bit % 16)) & 1;
		const bool top = crc & 0x8000;
		crc = uint16_t(crc << 1);
		if (top != input) crc ^= 0x1021;
	}
	assert(english[4] == uint16_t(crc >> 8 | crc << 8));
	assert(!set_language(machine, 1) && read() == english);
	assert(!set_language(machine, -1) && !set_language(machine, 2) && read() == english);
	assert(set_language(machine, 0) && language_value(machine) == 0);
	assert(read()[13] == 0xab00);
	words[13] = 0xab01;   // Keep an existing U.S. region when English is already selected.
	write(words);
	assert(language_value(machine) == 1);
	assert(!set_language(machine, 1) && read() == words);
}

static void scenery_lists()
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	model1 machine(std::move(regions));
	auto &ram = machine.work_ram();
	v60_bus &bus = machine;
	machine.cpu().pc_ref() = 0xff7dc5;
	machine.cpu().reg_ptr()[22] = (1u << 25) - 1;
	for (int center = 0; center < 256; center++) {
		std::fill(ram.begin(), ram.end(), 0xcd);
		std::fill_n(ram.begin() + 0x1aa0, 32, 0);
		machine.cpu().reg_ptr()[21] = center;
		const int cx = center & 15, cy = center >> 4;
		int n = 0;
		for (int dy = -2; dy <= 2; dy++)
			for (int dx = -2; dx <= 2; dx++) {
				const int x = cx + dx, y = cy + dy;
				if (x < 0 || x > 15 || y < 0 || y > 15) continue;
				const int cell = y * 16 + x;
				ram[0x1cc1 + n++] = uint8_t(cell);
				ram[0x1aa0 + cell / 8] |= uint8_t(1u << (cell % 8));
			}
		const std::vector<uint8_t> original(ram.begin() + 0x1cc1, ram.begin() + 0x1cc1 + n);
		bus.write8(0x501cc0, uint8_t(n));
		const int expected = (std::min(15, cx + 3) - std::max(0, cx - 3) + 1) *
			(std::min(15, cy + 3) - std::max(0, cy - 3) + 1);
		assert(ram[0x1cc0] == expected && expected <= 49);
		assert(std::equal(original.begin(), original.end(), ram.begin() + 0x1cc1));
		std::array<bool, 256> seen{};
		for (int i = 0; i < expected; i++) {
			const int cell = ram[0x1cc1 + i];
			assert(!seen[cell]);
			seen[cell] = true;
			assert(std::abs((cell & 15) - cx) <= 3 && std::abs((cell >> 4) - cy) <= 3);
			assert(ram[0x1aa0 + cell / 8] & (1u << (cell % 8)));
		}
		assert(ram[0x1d00] == 0xcd && ram[0x1c00] == 0xcd && ram[0x1c40] == 0xcd);
	}
	machine.cpu().reg_ptr()[21] = 0x88;
	machine.cpu().reg_ptr()[22] = 1u << 2;   // Only the middle of the far boundary is visible.
	bus.write8(0x501cc0, 0);
	assert(ram[0x1cc0] == 1 && ram[0x1cc1] == 0x58);
	machine.cpu().reg_ptr()[22] = 0;
	bus.write8(0x501cc0, 0);
	assert(ram[0x1cc0] == 0);
	machine.cpu().reg_ptr()[22] = (1u << 25) - 1;
	game::vr::set_extended_draw_distance(machine, false);
	bus.write8(0x501cc0, 1);
	assert(ram[0x1cc0] == 1);
	game::vr::set_extended_draw_distance(machine, true);
	machine.cpu().pc_ref() = 0;
	bus.write8(0x501cc0, 1);
	assert(ram[0x1cc0] == 1);
	machine.cpu().pc_ref() = 0xff7dc5;
	bus.write8(0x501cc1, 0);
	assert(ram[0x1cc0] == 1);   // A high-byte write is not a completed list.
}

static void rom_language(const char *dir, bool native)
{
	model1 machine(load_regions(dir, game::id{ "vr" }));
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(machine.cpu(), "vr");
		assert(rec::build_native(machine.program_rom(), map));
		machine.run_cpu = [](int n) { rec::run(n); };
	}
	machine.reset();
	for (int f = 0; f < 900; f++) machine.run_frame();
	assert(game::vr::language_value(machine) >= 0);
	if (native) assert(rec::fallback_count() == 0);
	const char *shots = getenv("VR_LANGUAGE_SHOTS");
	for (int language : { 1, 0 }) {
		game::vr::set_language(machine, language);
		assert(game::vr::language_value(machine) == language);
		const uint16_t settings = machine.eeprom_words()[13], crc = machine.eeprom_words()[4];
		machine.reset();
		if (native) rec::attach(machine.cpu(), "vr");
		for (int f = 1; f <= (shots ? 3500 : 900); f++) {
			machine.run_frame();
			if (shots && (f == 60 || f == 3150 || f == 3300 || f == 3500)) {
				const auto &image = machine.frame();
				const std::string path = std::string(shots) + "/" +
					(language ? "english" : "japanese") + "-" + std::to_string(f) + ".png";
				if (!write_png(path.c_str(), image.data(), image.width(), image.height())) {
					fprintf(stderr, "VR language: cannot write comparison picture to %s\n", path.c_str());
					std::abort();
				}
			}
		}
		assert(machine.nvram()[0xdc9b] == (language ? 2 : 0));
		assert(machine.eeprom_words()[13] == settings && machine.eeprom_words()[4] == crc);
		printf("VR language (%s): %s survives reset with native country %d and unchanged EEPROM checksum\n",
			native ? "native" : "interpreter", language ? "English" : "Japanese", machine.nvram()[0xdc9b]);
		if (native) {
			if (rec::fallback_count())
				fprintf(stderr, "VR language fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
			assert(rec::fallback_count() == 0);
		}
	}
}

static void rom_draw_distance(const char *dir, bool native)
{
	const game::id vr{ "vr" };
	model1 arcade(load_regions(dir, vr)), extended(load_regions(dir, vr));
	game::vr::set_extended_draw_distance(arcade, false);
	if (native) {
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(extended.cpu(), "vr");
		assert(rec::build_native(extended.program_rom(), map));
		extended.run_cpu = [](int n) { rec::run(n); };
	}
	arcade.reset();
	extended.reset();
	auto play = game::make_auto_play(vr, "", 12345, false);
	assert(play);
	int extra_quads_frames = 0, max_cells = 0, max_extra_cells = 0, most_extra_at = 0;
	size_t max_extra_quads = 0;
	for (int f = 1; f <= 6000; f++) {
		const auto inputs = play->frame(f);
		for (int c = 0; c < 3; c++) {
			if (inputs.analog[c] >= 0) {
				assert(arcade.set_analog_input(c, uint8_t(inputs.analog[c])));
				assert(extended.set_analog_input(c, uint8_t(inputs.analog[c])));
			}
			arcade.m_inputs[c] = extended.m_inputs[c] = uint8_t(~inputs.pressed[c]);
		}
		arcade.run_frame();
		extended.run_frame();
		max_cells = std::max(max_cells, int(extended.work_ram()[0x1cc0]));
		max_extra_cells = std::max(max_extra_cells, int(extended.work_ram()[0x1cc0]) - int(arcade.work_ram()[0x1cc0]));
		if (!memcmp(&extended.work_ram()[0x1b00], &arcade.work_ram()[0x1b00], 48) &&
			extended.frame_quads() > arcade.frame_quads()) {
			extra_quads_frames++;
			const size_t extra = size_t(extended.frame_quads() - arcade.frame_quads());
			if (extra > max_extra_quads) {
				max_extra_quads = extra;
				most_extra_at = f;
				if (const char *shots = getenv("VR_DISTANCE_SHOTS")) {
					const std::string dir(shots);
					const auto &a = arcade.frame(), &b = extended.frame();
					if (!write_png((dir + "/arcade.png").c_str(), a.data(), a.width(), a.height()) ||
						!write_png((dir + "/extended.png").c_str(), b.data(), b.width(), b.height())) {
						fprintf(stderr, "VR draw distance: cannot write comparison pictures to %s\n", shots);
						std::abort();
					}
				}
			}
		}
	}
	assert(max_extra_cells > 0 && max_cells <= 49);
	assert(extra_quads_frames > 100 && max_extra_quads > 100);
	if (native) {
		if (rec::fallback_count())
			fprintf(stderr, "VR extended draw distance fallback: %06x (%zu calls)\n", rec::first_fallback_address(), rec::fallback_count());
		assert(rec::fallback_count() == 0);
	}
	printf("VR extended (%s): up to %d scenery cells, %d matching-camera frames with more polygons, up to %zu extra quads at frame %d\n",
		native ? "native" : "interpreter", max_cells, extra_quads_frames, max_extra_quads, most_extra_at);
}

int main(int argc, char **argv)
{
	model1::regions regions;
	regions.title = game::id{ "vr" };
	model1 machine(std::move(regions));
	const auto callbacks = game::vr::create_gameplay(machine);
	assert(callbacks.wide_scene);
	assert(!callbacks.wide_scene(0, false));
	assert(!callbacks.wide_scene(0, true));
	assert(!callbacks.wide_scene(0x00502150, false));
	assert(callbacks.wide_scene(1, false));
	assert(callbacks.wide_scene(0x5020f0, false));
	language_settings(false);
	language_settings(true);
	scenery_lists();
	if (argc > 2 && (std::string(argv[2]) == "--language" || std::string(argv[2]) == "--language-native"))
		rom_language(argv[1], std::string(argv[2]) == "--language-native");
	else if (argc > 1) rom_draw_distance(argv[1], argc > 2 && std::string(argv[2]) == "--native");
	puts("VR gameplay tests passed");
}
