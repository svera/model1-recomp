#include "../../host/game_host.h"
#include "controls.h"
#include "profile.h"
#include "romset.h"

namespace game::swa {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	uint8_t analog_key_input(model1 &machine, int channel, bool low, bool high, uint8_t value) override
	{
		value = game_host::analog_key_input(machine, channel, low, high, value);
		return invert_y && (channel == 1 || channel == 5) ? uint8_t(255 - value) : value;
	}

	void load_settings(const settings_map &settings) override
	{
		const auto it = settings.find("invert_flight_y");
		if (it == settings.end()) return;
		if (it->second == "on" || it->second == "off") invert_y = it->second == "on";
		else fprintf(stderr, "SWA: invalid invert_flight_y '%s'; expected on or off\n", it->second.c_str());
	}
	void save_settings(FILE *f) const override { fprintf(f, "invert_flight_y = %s\n", invert_y ? "on" : "off"); }

	void start(host_context &context) override
	{
		ctx = &context;
		fprintf(stderr, "SWA: experimental interpreter bring-up; boot detection and native recompilation "
						"are not implemented.\n");
	}

	std::vector<menu_item> game_menu_items() override
	{
		return { { "Invert flight Y", "Off: as the cabinet, pushing the flight stick forward dives. On: forward climbs. "
										"Both seats' sticks, pads and keys.",
			[this] { return invert_y ? "On" : "Off"; },
			[this](int) {
				invert_y = !invert_y;
				ctx->settings_changed();
			} } };
	}

private:
	host_context *ctx = nullptr;
	bool invert_y = false;
};

}
}

std::unique_ptr<game_host> make_game_host()
{
	return std::make_unique<game::swa::host>();
}
