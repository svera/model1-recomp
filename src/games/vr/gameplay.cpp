#include "gameplay.h"
#include "../settings.h"
#include "../../machine/model1.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace game::vr {

namespace {
constexpr uint32_t TRACK_SELECT_CAMERA = 0x00502150;
constexpr uint32_t SCENERY_COUNT = 0x1cc0, VISIBLE_CELLS = 0x1aa0;
constexpr uint32_t SCENERY_READY_PC = 0xff7dc5;

void extend_scenery(model1 &machine)
{
	auto &ram = machine.work_ram();
	const uint32_t center = machine.cpu().reg(21), visible = machine.cpu().reg(22);
	const int count = ram[SCENERY_COUNT];
	if (center > 255 || count > 25) {
		fprintf(stderr, "VR draw distance: invalid scenery list (cell %u, count %d)\n", center, count);
		return;
	}
	const int cx = center & 15, cy = center >> 4;
	int n = count;
	// FF7C1E selects cells in a 5x5 grid. Extend its visible boundary by one 128-unit
	// cell, keeping the original near-cell lists used by collision and streaming intact.
	for (int dy = -3; dy <= 3; dy++)
		for (int dx = -3; dx <= 3; dx++) {
			if (std::abs(dx) != 3 && std::abs(dy) != 3) continue;
			const int x = cx + dx, y = cy + dy;
			if (x < 0 || x >= 16 || y < 0 || y >= 16) continue;
			const int parent = (std::clamp(dy, -2, 2) + 2) * 5 + std::clamp(dx, -2, 2) + 2;
			if (!(visible & (1u << parent))) continue;
			const int cell = y * 16 + x;
			ram[SCENERY_COUNT + 1 + n++] = uint8_t(cell);
			ram[VISIBLE_CELLS + cell / 8] |= uint8_t(1u << (cell % 8));
		}
	// At most 25 original + 24 outer cells: within the game's 63-entry list.
	ram[SCENERY_COUNT] = uint8_t(n);
}
}

gameplay_callbacks create_gameplay(model1 &machine)
{
	gameplay_callbacks callbacks;
	auto state = std::make_shared<gameplay_state>();
	callbacks.state = state;
	callbacks.work_ram_watch = 0x500000 + SCENERY_COUNT;
	callbacks.work_ram_written = [&machine, state](uint16_t mask) {
		if (state->extended_draw_distance && (mask & 0xff) && machine.cpu().pc() == SCENERY_READY_PC)
			extend_scenery(machine);
	};
	callbacks.wide_scene = [](uint32_t camera, bool) {
		return camera != 0 && camera != TRACK_SELECT_CAMERA;
	};
	return callbacks;
}

void set_extended_draw_distance(model1 &machine, bool extended)
{
	std::static_pointer_cast<gameplay_state>(machine.game_state())->extended_draw_distance = extended;
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
