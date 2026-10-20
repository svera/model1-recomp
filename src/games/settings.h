#pragma once

#include "../machine/model1.h"

namespace game {

template <class F>
bool edit_eeprom_settings(model1 &machine, int crc_first, F edit)
{
	uint16_t *board_words = machine.eeprom_words();
	uint8_t *standin = board_words ? nullptr : machine.eeprom();
	uint16_t words[64];
	if (standin)
		for (int i = 0; i < 64; i++) words[i] = uint16_t(standin[2 * i] << 8 | standin[2 * i + 1]);
	uint16_t *e = board_words ? board_words : words;
	if (e[0] != 0x5345 || e[1] != 0x4741 || !edit(e)) return false;
	uint16_t crc = 0;
	for (int i = crc_first; i < 64; i++)
		for (uint8_t byte : { uint8_t(e[i] >> 8), uint8_t(e[i]) }) {
			crc ^= uint16_t(byte << 8);
			for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
		}
	e[4] = uint16_t(crc << 8 | crc >> 8);
	if (standin)
		for (int i = 0; i < 64; i++) { standin[2 * i] = uint8_t(words[i] >> 8); standin[2 * i + 1] = uint8_t(words[i]); }
	return true;
}

}
