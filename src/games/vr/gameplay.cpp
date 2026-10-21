#include "gameplay.h"
#include "../settings.h"
#include "../../machine/model1.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace game::vr {

namespace {
constexpr uint32_t TRACK_SELECT_CAMERA = 0x00502150;
constexpr uint32_t SCENERY_COUNT = 0x1cc0;
constexpr uint32_t SCENERY_READY_PC = 0xff7dc5;
// Car 0's rank index (record 0x400d00 + 0x123). The boot code clears battery RAM and the first
// attract race (the medium course) leaves it alone; the next attract race sets it to 15 and every
// later medium-course race starts from that. SEGA AGES Virtua Racing shows that race from the start.
constexpr uint32_t CAR0_RANK = 0xe23;
constexpr uint8_t CAR0_ATTRACT_RANK = 15;

void prepare_scenery(model1 &machine, gameplay_state &state)
{
	const auto &ram = machine.work_ram();
	const int count = ram[SCENERY_COUNT];
	if (count > 25) {
		fprintf(stderr, "VR draw distance: invalid scenery count %d\n", count);
		std::abort();
	}
	state.scenery_selected.fill(false);
	for (int i = 0; i < count; i++) {
		const int cell = ram[SCENERY_COUNT + 1 + i];
		if (state.scenery_selected[cell]) {
			fprintf(stderr, "VR draw distance: duplicate scenery cell %d\n", cell);
			std::abort();
		}
		state.scenery_selected[cell] = true;
	}
	state.scenery_pending = true;
}

std::vector<render_object> scenery_objects(model1 &machine, gameplay_state &state)
{
	if (!state.extended_draw_distance || !state.scenery_pending || machine.cpu().pc() != 0xff8500) return {};
	v60_bus &bus = machine;
	if (bus.read32(machine.cpu().reg(31)) != 0xff846f) return {};
	state.scenery_pending = false;
	std::vector<render_object> objects;
	const uint32_t table = bus.read32(0x501948);
	auto read_rom = [&](uint32_t address, uint32_t bytes) {
		if (address > 0x1000000 - bytes ||
			!((address < 0x300000 && address + bytes <= 0x300000) || address >= 0xf80000)) {
			fprintf(stderr, "VR draw distance: invalid scenery ROM address %08x\n", address);
			std::abort();
		}
		return bus.read32(address);
	};
	read_rom(table, 256 * 4);
	std::array<std::pair<int, uint8_t>, 256> cells;
	const int center = int(machine.cpu().reg(22));
	if (center < 0 || center > 255) {
		fprintf(stderr, "VR draw distance: invalid anchor cell %d\n", center);
		std::abort();
	}
	for (int cell = 0; cell < 256; cell++) {
		const int dx = (cell & 15) - (center & 15), dy = (cell >> 4) - (center >> 4);
		cells[cell] = { dx * dx + dy * dy, uint8_t(cell) };
	}
	std::sort(cells.begin(), cells.end());
	for (const auto &[distance, cell] : cells) {
		if (state.scenery_selected[cell]) continue;
		uint32_t record = read_rom(table + uint32_t(cell) * 4, 4);
		if (!record || record == 0xffffffff) continue;
		for (int guard = 0; ; guard++, record += 12) {
			if (guard == 4096) {
				fprintf(stderr, "VR draw distance: unterminated scenery cell %d\n", cell);
				std::abort();
			}
			const uint32_t polygon = read_rom(record, 4);
			if (!polygon) break;
			read_rom(record, 12);
			if (polygon >= machine.polygon_rom().size() / 4) {
				fprintf(stderr, "VR draw distance: invalid scenery polygon %08x in cell %d\n", polygon, cell);
				std::abort();
			}
			objects.push_back({ bus.read32(record + 4), polygon, 0 });
		}
	}
	return objects;
}
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.reset = [state] {
		state->scenery_pending = false;
		state->attract_rank_pending = true;
	};
	callbacks.frame_end = [&machine, state] {
		if (!state->attract_rank_pending) return;
		state->attract_rank_pending = false;
		if (machine.nvram()[CAR0_RANK] == 0) machine.nvram()[CAR0_RANK] = CAR0_ATTRACT_RANK;
	};
	callbacks.work_ram_watch = 0x500000 + SCENERY_COUNT;
	callbacks.work_ram_written = [&machine, state](uint16_t mask) {
		if ((mask & 0xff) && machine.cpu().pc() == SCENERY_READY_PC)
			prepare_scenery(machine, *state);
	};
	callbacks.matrix_objects = [&machine, state] { return scenery_objects(machine, *state); };
	callbacks.wide_scene = [](uint32_t camera, bool) {
		return camera != 0 && camera != TRACK_SELECT_CAMERA;
	};
	return callbacks;
}

void set_extended_draw_distance(model1 &machine, bool extended)
{
	auto state = std::static_pointer_cast<gameplay_state>(machine.game_state());
	state->extended_draw_distance = extended;
	state->scenery_pending = false;
}

int language_value(model1 &machine)
{
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int country = e[13] & 0xff;
		if (country <= 2) value = country != 0;
		else fprintf(stderr, "VR: invalid EEPROM country %d; expected Japan, U.S. or Export\n", country);
		return false;
	});
	return value;
}

bool set_language(model1 &machine, int language)
{
	if (language < 0 || language > 1) {
		fprintf(stderr, "VR: invalid language %d; expected 0 (Japanese) or 1 (English)\n", language);
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int country = e[13] & 0xff;
		if (country <= 2 && int(country != 0) == language) return false;
		e[13] = uint16_t((e[13] & 0xff00) | (language ? 2 : 0));
		return true;
	});
}

}
