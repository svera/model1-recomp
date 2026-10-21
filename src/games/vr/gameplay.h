#pragma once

#include "../game.h"

namespace game::vr {

struct gameplay_state : game::gameplay_state
{
	bool extended_draw_distance = true;
	bool scenery_pending = false;
	bool attract_rank_pending = false;
	bool standalone_car_color = false;
	std::array<bool, 256> scenery_selected{};
};

gameplay_callbacks create_gameplay(model1 &machine);
void set_extended_draw_distance(model1 &machine, bool extended);
int language_value(model1 &machine);   // 0 Japanese, 1 English, -1 before EEPROM initialization
bool set_language(model1 &machine, int language);
inline constexpr int FREE_PLAY = 26;
int coin_setting(model1 &machine);
bool set_coin_setting(model1 &machine, int value);
bool enable_standalone_car_color(model1 &machine);
class camera_cycle
{
public:
	void update(model1 &machine, bool pressed);
private:
	bool m_pressed = false;
	int m_frames = 0, m_view = 0;
};
enum class machine_setting { car_color, race_type, course_select, difficulty };
int machine_setting_value(model1 &machine, machine_setting setting);
bool set_machine_setting(model1 &machine, machine_setting setting, int value);
extern const video_traits VIDEO;

}
