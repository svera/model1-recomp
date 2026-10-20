// Virtua Racing in the desktop host (src/host/game_host.h): its set and controls; the rest is the
// shared host's.
#include "../../host/game_host.h"
#include "controls.h"
#include "gameplay.h"
#include "romset.h"
#include <optional>

namespace game::vr {
namespace {

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	void load_settings(const settings_map &settings) override
	{
		const auto it = settings.find("draw_distance");
		if (it == settings.end()) return;
		if (it->second == "arcade" || it->second == "extended") extended = it->second == "extended";
		else fprintf(stderr, "VR: invalid draw_distance '%s'; expected arcade or extended\n", it->second.c_str());
	}

	void save_settings(FILE *f) const override
	{
		fprintf(f, "draw_distance = %s\n", extended ? "extended" : "arcade");
	}

	void start(host_context &context) override
	{
		ctx = &context;
		set_extended_draw_distance(context.machine, extended);
	}

	std::vector<menu_item> display_menu_items() override
	{
		return { { "Draw distance", "Extended draws one more row of scenery cells beyond the arcade's range. More scenery can lower performance. Applies without restarting.",
			[this] { return extended ? "Extended" : "Arcade"; },
			[this](int d) {
				extended = (int(extended) + d + 2) % 2;
				set_extended_draw_distance(ctx->machine, extended);
				ctx->settings_changed();
			} } };
	}

	std::vector<menu_item> game_menu_items() override
	{
		if (!language_before) {
			language_before = language_value(ctx->machine);
			language_choice = *language_before;
		}
		return { { "Language", "The game's own text: Japanese or English. Changing it restarts the game when the menu closes.",
			[this] {
				return std::string(language_choice < 0 ? "-" : language_choice ? "English" : "Japanese");
			},
			[this](int d) {
				if (language_choice < 0) {
					fprintf(stderr, "VR: cannot change language before the game initializes its EEPROM settings\n");
					return;
				}
				language_choice = ((language_choice + d) % 2 + 2) % 2;
			} } };
	}

	void keep_arcade_settings() override
	{
		apply_language();
	}

	restart menu_closed() override
	{
		return apply_language() ? restart::game : restart::none;
	}

private:
	bool apply_language()
	{
		const bool changed = language_before && *language_before != language_choice;
		language_before.reset();
		if (!changed) return false;
		if (!set_language(ctx->machine, language_choice)) {
			fprintf(stderr, "VR: could not apply language to the game's EEPROM settings\n");
			return false;
		}
		ctx->settings_changed();
		return true;
	}

	host_context *ctx = nullptr;
	bool extended = true;
	std::optional<int> language_before;
	int language_choice = -1;
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::vr::host>(); }
