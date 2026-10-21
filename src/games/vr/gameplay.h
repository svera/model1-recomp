#pragma once

#include "../game.h"

namespace game::vr {

struct gameplay_state : game::gameplay_state
{
	bool extended_draw_distance = true;
	bool scenery_pending = false;
	bool attract_rank_pending = false;
	std::array<bool, 256> scenery_selected{};
};

gameplay_callbacks create_gameplay(model1 &machine);
void set_extended_draw_distance(model1 &machine, bool extended);
int language_value(model1 &machine);   // 0 Japanese, 1 English, -1 before EEPROM initialization
bool set_language(model1 &machine, int language);
extern const video_traits VIDEO;

}
