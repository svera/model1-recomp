#include "gameplay.h"
#include "../settings.h"
#include "../../machine/model1.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
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
		if (state->attract_rank_pending) {
			state->attract_rank_pending = false;
			if (machine.nvram()[CAR0_RANK] == 0) machine.nvram()[CAR0_RANK] = CAR0_ATTRACT_RANK;
		}
		const uint8_t phase = machine.work_ram()[0x1138];
		if (!state->standalone_car_color || (phase != 0x12 && phase != 0x14) ||
			machine.read8(0x40dc8c) != 0) return;
		const uint32_t color = machine.read8(0x40dca2);
		if (!color) return;   // Preserve the original standalone red model.
		const uint32_t player = machine.read32(0x501320);
		if (color >= 8 || player < 0x400d00 || player > 0x403280 || (player - 0x400d00) % 0x280) {
			fprintf(stderr, "VR: invalid standalone livery color %u or player record %06x\n", color, player);
			std::abort();
		}
		// FE8A91 uses fixed standalone models. Copy only the five rendering pointers from the
		// arcade's colored-model table (FE8DE9); never change car IDs, track or handling fields.
		const uint32_t table = 0xfcd195 + color * 32;
		for (const auto &[field, entry] : std::array<std::pair<uint32_t, uint32_t>, 5>{ {
			{ 0x18, 4 }, { 0xac, 0 }, { 0xa2, 8 }, { 0xa8, 12 }, { 0xb0, 16 }
		} })
			machine.write32(player + field, machine.read32(table + entry));
	};
	callbacks.work_ram_watch = 0x500000 + SCENERY_COUNT;
	callbacks.work_ram_written = [&machine, state](uint16_t mask) {
		if ((mask & 0xff) && machine.cpu().pc() == SCENERY_READY_PC)
			prepare_scenery(machine, *state);
	};
	callbacks.matrix_objects = [&machine, state] { return scenery_objects(machine, *state); };
	callbacks.record_visibility = [&machine](uint32_t owner) {
		std::vector<game::record_visibility> records;
		if (owner < 0x400d00 || owner > 0x401e80 || (owner - 0x400d00) % 0x280)
			return records;
		const auto &ram = machine.work_ram();
		if (ram[0x1138] != 0x03 && ram[0x1138] != 0x14) return records;
		for (uint32_t car = 0x400d00; car <= 0x401e80; car += 0x280) {
			if (!(machine.read32(car) & 0x80000000)) continue;
			const uint32_t cell = machine.read16(car + 0x34);
			if (cell >= 256) {
				fprintf(stderr, "VR car visibility: invalid course cell %u for record %06x\n", cell, car);
				std::abort();
			}
			game::record_visibility result{};
			result.owner = car;
			for (int i = 0; i < 3; i++) {
				const uint32_t bits = machine.read32(car + 0x1e + i * 4);
				memcpy(&result.position[i], &bits, sizeof bits);
				if (!std::isfinite(result.position[i])) {
					fprintf(stderr, "VR car visibility: non-finite position for record %06x\n", car);
					std::abort();
				}
			}
			result.culled = !(machine.read32(0x501ac0 + (cell / 32) * 4) & (1u << (cell % 32)));
			records.push_back(result);
		}
		return records;
	};
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

namespace {
struct setting_field { int byte, count; };
constexpr std::array<setting_field, 4> setting_fields{ {
	{ 0x22, 8 }, { 0x23, 2 }, { 0x21, 4 }, { 0x20, 4 }
} };

const setting_field *field_for(machine_setting setting)
{
	const auto index = size_t(setting);
	if (index < setting_fields.size()) return &setting_fields[index];
	fprintf(stderr, "VR: invalid machine setting %zu\n", index);
	return nullptr;
}
}

int machine_setting_value(model1 &machine, machine_setting setting)
{
	const auto *field = field_for(setting);
	if (!field) return -1;
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int shift = field->byte % 2 ? 0 : 8;
		const int stored = (e[field->byte / 2] >> shift) & 0xff;
		if (stored < field->count) value = stored;
		else fprintf(stderr, "VR: invalid machine setting at EEPROM byte %02x: %d\n", field->byte, stored);
		return false;
	});
	return value;
}

bool set_machine_setting(model1 &machine, machine_setting setting, int value)
{
	const auto *field = field_for(setting);
	if (!field) return false;
	if (value < 0 || value >= field->count) {
		fprintf(stderr, "VR: invalid value %d for machine setting %zu\n", value, size_t(setting));
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int shift = field->byte % 2 ? 0 : 8;
		auto &word = e[field->byte / 2];
		const uint16_t updated = uint16_t((word & ~(0xff << shift)) | (value << shift));
		if (word == updated) return false;
		word = updated;
		return true;
	});
}

void camera_cycle::update(model1 &machine, bool pressed)
{
	const uint8_t phase = machine.work_ram()[0x1138];
	if (phase != 0x12 && phase != 0x14) {
		m_pressed = pressed;
		m_frames = 0;
		return;
	}
	if (pressed && !m_pressed) {
		// FF1482 stores the actual camera index. Cycle from it, including after a race reset.
		constexpr std::array<int, 4> next{ 3, 0, 1, 2 };   // Red -> Blue -> Yellow -> Green.
		m_view = next[machine.read8(0x501d02) & 3];
		m_frames = 6;
	}
	m_pressed = pressed;
	if (!m_frames) return;
	// FF1441: indices 0/1/2/3 are Blue/Red/Green/Yellow.
	constexpr std::array<uint8_t, 4> system{ 0x40, 0x20, 0, 0x80 };
	machine.m_inputs[0] &= uint8_t(~system[m_view]);
	if (m_view == 2) machine.m_inputs[1] &= uint8_t(~0x01);
	m_frames--;
}

bool enable_standalone_car_color(model1 &machine)
{
	if (machine.program_rom().size() < 0xfcd295) {
		fprintf(stderr, "VR: cannot enable standalone car color: missing colored-model table\n");
		return false;
	}
	std::static_pointer_cast<gameplay_state>(machine.game_state())->standalone_car_color = true;
	return true;
}

int monitor_type(model1 &machine)
{
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int stored = e[5] & 0xff;
		if (stored <= 1) value = stored;
		else fprintf(stderr, "VR: invalid EEPROM monitor type %d; expected 4:3 or 16:9\n", stored);
		return false;
	});
	return value;
}

bool set_monitor_type(model1 &machine, int value)
{
	if (value < 0 || value > 1) {
		fprintf(stderr, "VR: invalid monitor type %d; expected 0 (4:3) or 1 (16:9)\n", value);
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const uint16_t updated = uint16_t((e[5] & 0xff00) | value);
		if (e[5] == updated) return false;
		e[5] = updated;
		return true;
	});
}

int coin_setting(model1 &machine)
{
	int value = -1;
	edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const int stored = e[8] >> 8;
		if (stored <= FREE_PLAY) value = stored;
		else fprintf(stderr, "VR: invalid EEPROM coin setting %d\n", stored);
		return false;
	});
	return value;
}

bool set_coin_setting(model1 &machine, int value)
{
	if (value < 0 || value > FREE_PLAY) {
		fprintf(stderr, "VR: invalid coin setting %d; expected 0 through %d\n", value, FREE_PLAY);
		return false;
	}
	// FC68EA derives the credit ratios and free-play flag from the game's coinage table.
	const auto &rom = machine.program_rom();
	const size_t at = 0xfc6924 + size_t(value) * 4;
	if (at + 4 > rom.size()) {
		fprintf(stderr, "VR: coinage table is missing from the program ROM\n");
		return false;
	}
	return edit_eeprom_settings(machine, 5, [&](uint16_t *e) {
		const uint16_t coin = uint16_t(value << 8 | rom[at]);
		const uint16_t ratios = uint16_t(rom[at + 2] << 8 | rom[at + 3]);
		const uint16_t mode = uint16_t(rom[at + 1] << 8 | (e[10] & 0xff));
		if (e[8] == coin && e[9] == ratios && e[10] == mode) return false;
		e[8] = coin;
		e[9] = ratios;
		e[10] = mode;
		return true;
	});
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
