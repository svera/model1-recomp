// Virtua Fighter in the desktop host (src/host/game_host.h): its settings (the ring, the cheats,
// Arcade mode's coins), its pictures, the Arcade settings and Cheats pages, and the cheats each frame.
#include "../../host/game_host.h"
#include "art.h"
#include "controls.h"
#include "gameplay.h"
#include "romset.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <cctype>

namespace game::vf {
namespace {

std::string on_off(bool b) { return b ? "On" : "Off"; }
int wrap(int v, int n) { return (v % n + n) % n; }

class host final : public game_host
{
public:
	const romset::definition &romset() const override { return ROMSET; }
	const control_profile &controls() const override { return CONTROLS; }

	void load_settings(const settings_map &kv) override
	{
		auto num = [&](const char *k, int &v, int lo, int hi) {
			auto it = kv.find(k);
			if (it != kv.end()) v = std::clamp(atoi(it->second.c_str()), lo, hi);
		};
		auto flag = [&](const char *k, bool &v) { int i = v; num(k, i, 0, 1); v = i; };
		num("ring_size", ring, 0, 1500);
		if (ring && (ring < 1200 || ring % 100)) ring = 0;   // 0: by difficulty
		num("arcade_coins", arcade_coin, -1, 0xffff);
		num("akira_slot", akira_slot, 0, int(std::size(UNUSED_CHARACTERS)) - 1);
		num("infinite_energy", infinite_energy, 0, 3);
		flag("one_hit_ko", one_hit_ko);
		flag("no_time_limit", no_time_limit);
		flag("kage_unmasked", kage_unmasked);
		flag("english_set", english_set);
		num("ring_cheat", ring_cheat, 0, 2);
		num("random_fighter", random_fighter, 0, 2);
		flag("random_opponents", random_cpu);
		auto trimmed = [](std::string v) { while (!v.empty() && isspace(uint8_t(v.back()))) v.pop_back(); return v; };
		for (int p = 0; p < 2; p++)
			if (auto it = kv.find(p ? "random_p2_name" : "random_p1_name"); it != kv.end()) random_named[p] = trimmed(it->second);
		for (int p = 0; p < 2; p++)
			if (auto it = kv.find(p ? "random_p2" : "random_p1"); it != kv.end()) random_player[p] = trimmed(it->second);
			else if (random_fighter) random_player[p] = "*";
		if (auto it = kv.find("random_name"); it != kv.end() && random_fighter && kv.find("random_p1") == kv.end())
			random_player[0] = random_player[1] = trimmed(it->second);
		for (int p = 0; p < 2; p++)
			if (random_named[p].empty() && !random_player[p].empty() && random_player[p] != "*" && random_player[p][0] != '#') random_named[p] = random_player[p];
		if (akira_slot > 0 && random_player[0].empty() && kv.find("random_p1") == kv.end()) random_player[0] = "#" + std::to_string(akira_slot);
		akira_slot = 0;
		random_now();
	}

	void save_settings(FILE *f) const override
	{
		fprintf(f, "ring_size = %d\n", ring);
		if (arcade_coin >= 0) fprintf(f, "arcade_coins = %d\n", arcade_coin);
		if (akira_slot) fprintf(f, "akira_slot = %d\n", akira_slot);
		for (int p = 0; p < 2; p++) {
			if (!random_player[p].empty()) fprintf(f, "random_p%d = %s\n", p + 1, random_player[p].c_str());
			if (!random_named[p].empty()) fprintf(f, "random_p%d_name = %s\n", p + 1, random_named[p].c_str());
		}
		if (random_cpu) fprintf(f, "random_opponents = 1\n");
		if (infinite_energy) fprintf(f, "infinite_energy = %d\n", infinite_energy);
		if (one_hit_ko) fprintf(f, "one_hit_ko = 1\n");
		if (no_time_limit) fprintf(f, "no_time_limit = 1\n");
		if (kage_unmasked) fprintf(f, "kage_unmasked = 1\n");
		if (english_set) fprintf(f, "english_set = 1\n");
		if (ring_cheat) fprintf(f, "ring_cheat = %d\n", ring_cheat);
	}

	void start(host_context &c) override
	{
		ctx = &c;
		vf.emplace(c.machine);
		vf->set_no_time_limit(no_time_limit);   // (Cheats: an instruction of the game's, so before it's recompiled)
		vf->set_strands_moved(random_fighter > 0);
	}

	const char *save_sidecar_name() const override { return "ranking_names.txt"; }
	std::string save_sidecar() const override { return vf->ranking_names(); }
	void load_sidecar(const std::string &text) override { vf->set_ranking_names(text); }
	uint32_t save_sidecar_version() const override { return vf->ranking_names_version(); }
	bool hide_frame() const override { return vf->random_select_hidden(); }

	void apply_players()
	{
		for (int p = 0; p < 2; p++)
			if (!vf->set_random_player(p, random_fighter ? random_player[p] : std::string())) {
				if (random_named[p] == random_player[p]) random_named[p].clear();
				random_player[p] = "*";
				vf->set_random_player(p, "*");
				changed();
			}
	}

	bool before_restart() override
	{
		settings_before.reset();
		apply_players();
		bool code = vf->set_no_time_limit(no_time_limit);
		code |= vf->set_strands_moved(random_fighter > 0);
		vf->set_random_fighter(random_fighter > 0, random_fighter > 0, random_cpu);
		vf->set_akira_slot(0);
		load_secret_icon(*vf, 0, icon_dirs());
		return code;
	}

	void before_boot() override
	{
		load_screens(*vf, ctx->assets_dir + "/games/vf/backgrounds");   // the title and select's widescreen sides
		load_screens(*vf, ctx->data_dir + "/backgrounds");     // the player's own, which win
		vf->set_ring_width(ring_now());
		vf->set_kage_unmasked(kage_unmasked);
		before_restart();
	}

	// The play style's coins: PC mode is free play (its FREE PLAY text hidden); Arcade mode has the
	// cabinet's own coin setting, kept in settings.ini while PC mode has free play in its place (it
	// was left at free play, so Arcade mode after PC mode had no credits to put in, and a cabinet's
	// coin slot did nothing). The settings are read at boot: when they change, boot again.
	bool booted() override
	{
		settings_before.reset();   // (a restart applies them)
		bool language = false;
		if (!english_set && vf->assignment_value(gameplay::assignment::language) >= 0) {
			english_set = true;
			changed();
			language = vf->set_assignment(gameplay::assignment::language, 1);
		}
		const int now = vf->coin_setting();
		if (now < 0) return language;                       // (the game's very first boot writes them)
		if (ctx->pc_mode) {
			if (now != gameplay::FREE_PLAY && now != arcade_coin) { arcade_coin = now; ctx->settings_changed(); }
			return vf->force_free_play() || language;
		}
		// Arcade: its own setting back; free play left by PC mode before one was kept: the game's first
		return vf->set_coin_setting(arcade_coin >= 0 ? arcade_coin : now == gameplay::FREE_PLAY ? 0 : now) || language;
	}

	bool has_play_style() const override { return true; }

	// Arcade mode's setting as it is now (the service menu may have changed it), kept for when PC mode replaces it
	void keep_arcade_settings() override
	{
		const int now = vf->coin_setting();
		if (!ctx->pc_mode && now >= 0 && now != arcade_coin) { arcade_coin = now; ctx->settings_changed(); }
	}

	void play_style_changed() override { if (ctx->pc_mode) vf->hide_free_play_text(); else vf->show_free_play_text(); }

	std::vector<menu_item> menu_items() override
	{
		return { { "Arcade settings", "Difficulty, ring size, rounds, continues and the service menu. Changing them restarts the game.", {}, {},
				   [this] { ctx->push_page(arcade_page()); } } };
	}

	std::vector<menu_item> menu_items_after_controls() override
	{
		return { { "Cheats", "Secret characters, random fighters, Kage's mask and fight switches. Some restart the game.", {}, {},
				   [this] { ctx->push_page(cheats_page()); } } };
	}

	// the arcade settings are read as the game starts: restart it if one is different
	restart menu_closed() override
	{
		if (!settings_before) return restart::none;
		const bool changed = *settings_before != settings_now();
		settings_before.reset();
		if (!changed) return restart::none;
		return restart::game;
	}

	void before_frame() override
	{
		if (infinite_energy || one_hit_ko || no_time_limit) vf->fight_cheats(infinite_energy, one_hit_ko, no_time_limit);
	}

	// health (NVRAM b4, b6), the round wins' top bits (182, 202), the fight's state (work RAM 1281, 1282)
	void trace_fields(unsigned (&fields)[6]) const override
	{
		model1 &m = ctx->machine;
		auto nv16 = [&](uint32_t a) { return unsigned(m.nvram()[a] | m.nvram()[a + 1] << 8); };
		const unsigned values[6] = { nv16(0xb4), nv16(0xb6), nv16(0x182) >> 15, nv16(0x202) >> 15,
									 unsigned(m.work_ram()[0x1281]), unsigned(m.work_ram()[0x1282]) };
		std::copy(std::begin(values), std::end(values), fields);
	}

private:
	host_context *ctx = nullptr;
	std::optional<gameplay> vf;

	int ring = 0;              // Arcade settings: the ring's width (0: by difficulty)
	int arcade_coin = -1;      // Arcade mode's coin/credit setting, kept while PC mode makes it free play (-1: not seen yet)
	int akira_slot = 0;        // Cheats: who plays in Akira's place (UNUSED_CHARACTERS; 0 Akira)
	bool random_cpu = false, kage_unmasked = false, english_set = false;
	int random_fighter = 0;
	std::string random_player[2], random_named[2];
	void random_now() { random_fighter = !random_player[0].empty() || !random_player[1].empty() || random_cpu; }
	int infinite_energy = 0;   // Cheats: bit 0 player 1, bit 1 player 2
	bool one_hit_ko = false, no_time_limit = false;   // Cheats
	int ring_cheat = 0;        // Cheats: 0 the arcade's, 1 tiny (600), 2 huge (5000: no ring outs)
	int ring_now() const { return ring_cheat == 1 ? 600 : ring_cheat == 2 ? 5000 : ring; }

	std::vector<std::string> icon_dirs() const { return { ctx->data_dir + "/icons", ctx->assets_dir + "/games/vf/select_icons" }; }   // (the player's first)

	// ---- the arcade's own settings (test mode's GAME ASSIGNMENTS): the game reads them as it starts,
	// so closing the menu after changing one restarts it (only if something is different) ----
	using ga = gameplay::assignment;
	static constexpr int N_ASSIGN = 6;
	using settings_values = std::array<int, N_ASSIGN + 6>;
	std::optional<settings_values> settings_before;   // as the Arcade settings or Cheats page opened
	settings_values settings_now()
	{
		settings_values v;
		for (int i = 0; i < N_ASSIGN; i++) v[i] = vf->assignment_value(ga(i));
		v[N_ASSIGN] = ring;
		v[N_ASSIGN + 1] = akira_slot;   // (Cheats: read as a fight loads, so it restarts too)
		v[N_ASSIGN + 2] = ring_cheat;
		v[N_ASSIGN + 3] = no_time_limit;   // (an instruction of the game's: a restart recompiles it)
		v[N_ASSIGN + 4] = (random_fighter != 0) * 2 + random_cpu;
		v[N_ASSIGN + 5] = int(std::hash<std::string>{}(random_player[0] + "/" + random_player[1]));
		return v;
	}
	void changed() { ctx->settings_changed(); }

	menu_page arcade_page()
	{
		if (!settings_before) settings_before = settings_now();
		menu_page p{ "Arcade settings: a change restarts the game when the menu closes", "", {} };   // always in view
		gameplay *g = &*vf;
		auto choice = [&](const char *label, const char *help, ga a, std::function<std::string(int)> name) {
			p.items.push_back({ label, help,
				[g, a, name] { int v = g->assignment_value(a); return v < 0 ? std::string("-") : name(v); },
				[this, g, a](int d) {
					int v = g->assignment_value(a);
					if (v >= 0) g->set_assignment(a, v + d);
					if (v >= 0 && a == ga::language && !english_set) { english_set = true; changed(); }
				} });
		};
		static const char *LANGUAGES[] = { "Japanese", "English" };
		choice("Language", "The game's own text, as the rules shown between demos: Japanese, as the arcade board comes set, or English.",
			ga::language, [](int v) { return std::string(LANGUAGES[v]); });
		static const char *DIFFICULTY[] = { "Easy", "Normal", "Hard", "Hardest" };
		choice("Difficulty", "How hard the computer fights. Harder also gives both fighters less energy, and a smaller ring while Ring size is Default.",
			ga::difficulty, [](int v) { return std::string(DIFFICULTY[v]); });
		static const int RINGS[] = { 0, 1200, 1300, 1400, 1500 };   // the arcade's widths (STAGE WIDTH)
		static const char *RING_NAMES[] = { "Default", "Small", "Medium", "Large", "Extra large" };
		p.items.push_back({ "Ring size",
			"Default follows the difficulty, as on the arcade: Extra large on Easy, down to Small on Hardest. A smaller ring makes ring outs easier.",
			[this] { return std::string(RING_NAMES[std::find(std::begin(RINGS), std::end(RINGS), ring) - std::begin(RINGS)]); },
			[this](int d) {
				int i = int(std::find(std::begin(RINGS), std::end(RINGS), ring) - std::begin(RINGS));
				ring = RINGS[std::clamp(i + d, 0, int(std::size(RINGS)) - 1)];
				changed();
				vf->set_ring_width(ring_now());
			} });
		choice("Rounds to win (vs CPU)", "Rounds needed to win a match against the computer.", ga::match_1p, [](int v) { return std::to_string(v); });
		choice("Rounds to win (vs player)", "Rounds needed to win a match between two players.", ga::match_vs, [](int v) { return std::to_string(v); });
		choice("Continue", "Off ends the game when you lose to the computer.", ga::continues, [](int v) { return on_off(v); });
		choice("Demo sound", "Sound during the demo that plays between games.", ga::advertise_sound, [](int v) { return on_off(v); });
		p.items.push_back({ "Service menu",
			"The arcade's own test menu: input and sound tests, coin settings and bookkeeping. Down moves, Start selects (or 9 and F2 on a keyboard). Choose EXIT to come back.",
			{}, {}, [this] { ctx->service_menu(); } });
		p.items.push_back(ctx->back_item());
		return p;
	}

	// Each player's fighter's row state: typing a seed in it, what was typed, a mistake; and where the
	// page put the rows (to rebuild it with the same one selected)
	struct seed_entry { bool typing = false, pending = false; std::string typed, error; int secret = 1; };   // (pending: Seed chosen, none typed yet: plays as Random)
	std::shared_ptr<seed_entry> seed_st[2] = { std::make_shared<seed_entry>(), std::make_shared<seed_entry>() };
	int fighter_row[2] = { 0, 0 }, fighter_sub[2] = { -1, -1 };
	// (out here: the rows' functions outlive the page's building)
	int kind_of(int pl) const { const std::string &v = random_player[pl]; return v.empty() ? 0 : v == "*" ? 1 : v[0] == '#' ? 3 : 2; }
	inline static const char *KINDS[] = { "Normal", "Random", "Seed", "Secret character" };
	// (the page made first: where a player's rows land depends on the rows above)
	void rebuild(int pl, bool sub) { menu_page pg = cheats_page(); ctx->replace_page(std::move(pg), sub && fighter_sub[pl] >= 0 ? fighter_sub[pl] : fighter_row[pl]); }
	static constexpr int N_SECRET = int(std::size(UNUSED_CHARACTERS));
	menu_page cheats_page()
	{
		if (!settings_before) settings_before = settings_now();
		menu_page p{ "Cheats: most changes restart the game when the menu closes", "", {} };
		auto heading = [&p](const char *t) { menu_item h{ t, "", {}, {}, {}, true }; h.heading = true; p.items.push_back(h); };
		heading("FIGHTERS");
		// Each player's fighter (Normal, Random, Seed, Secret character), and under it, only when it
		// needs one, a row of its own: the seed (typed right in it, a mistake said under it) or the
		// secret character. Anything but Normal is picked for that player at the select
		for (int pl = 0; pl < 2; pl++) {
			auto st = seed_st[pl];
			if (kind_of(pl) == 3) st->secret = std::clamp(atoi(random_player[pl].c_str() + 1), 1, N_SECRET - 1);
			auto entry = std::make_shared<std::function<bool(const SDL_Event &)>>();
			static const char *HINT = "Letters type   Backspace delete   Enter play it   Esc cancel   "
									  "(a pad: Up / Down the letter, Right the next, Left delete, A play it, B cancel)";
			auto set_kind = [this, pl](const std::string &v) { random_player[pl] = v; if (v != "" && v != "*" && v[0] != '#') random_named[pl] = v; random_now(); changed(); };
			auto start_typing = [this, pl, st, entry] {
				st->typing = true; st->typed.clear(); st->error.clear();
				rebuild(pl, true);
				ctx->capture(HINT, *entry, [this, pl, st] { st->typing = false; st->error.clear(); rebuild(pl, false); });
			};
			*entry = [this, pl, st, set_kind](const SDL_Event &e) {
				bool enter = false;
				if (e.type == SDL_EVENT_KEY_DOWN) {
					const SDL_Scancode sc = e.key.scancode;
					SDL_Keycode k = e.key.key;   // (the letter on the key, the keyboard's layout: AZERTY's A is the scancode's Q)
					if (k == SDLK_UNKNOWN && sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) k = SDLK_A + (sc - SDL_SCANCODE_A);   // (none given: by its place)
					if (k >= SDLK_A && k <= SDLK_Z) { if (st->typed.size() < 12) st->typed += char('A' + (k - SDLK_A)); }
					else if (sc == SDL_SCANCODE_BACKSPACE) { if (!st->typed.empty()) st->typed.pop_back(); }
					else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) enter = true;
					else return false;
				} else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
					std::string &t = st->typed;
					auto turn = [&](int d) { if (t.empty()) t.push_back('A'); else t.back() = char('A' + ((t.back() - 'A' + d) % 26 + 26) % 26); };
					switch (e.gbutton.button) {
					case SDL_GAMEPAD_BUTTON_EAST: st->typing = false; st->error.clear(); rebuild(pl, false); return true;   // (B: as it was)
					case SDL_GAMEPAD_BUTTON_SOUTH: enter = true; break;
					case SDL_GAMEPAD_BUTTON_DPAD_UP: turn(+1); break;
					case SDL_GAMEPAD_BUTTON_DPAD_DOWN: turn(-1); break;
					case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: if (t.size() < 12) t.push_back(t.empty() ? 'A' : t.back()); break;
					case SDL_GAMEPAD_BUTTON_DPAD_LEFT: if (!t.empty()) t.pop_back(); break;
					default: return false;
					}
				} else
					return false;
				st->error.clear();
				if (enter) {
					if (st->typed.empty()) st->error = "Type a seed: a random fighter's name, from its energy bar";
					else if (!vf->random_fighter_names().count(st->typed)) st->error = "No fighter has the seed " + st->typed;
					else { set_kind(st->typed); st->typing = st->pending = false; rebuild(pl, true); return true; }
				}
				ctx->refresh_menu();
				return false;
			};
			fighter_row[pl] = int(p.items.size());
			p.items.push_back({ pl ? "Player 2 fighter" : "Player 1 fighter",
				"Normal: pick at the select. Random: a new mixed fighter every game. Seed: one mixed fighter, the same build and "
				"moves every game on any machine. Colours are new every game. Secret character: one cut from the game, or Dural. Anything but "
				"Normal is picked for you at the select.",
				[this, pl, st] { return std::string(KINDS[st->typing || st->pending ? 2 : kind_of(pl)]); },
				[this, pl, st, set_kind, start_typing](int d) {
					const int k = wrap((st->pending ? 2 : kind_of(pl)) + d, 4);
					st->typing = st->pending = false; st->error.clear();
					if (k == 0) set_kind("");
					else if (k == 1) set_kind("*");
					else if (k == 3) set_kind("#" + std::to_string(st->secret));
					else if (!random_named[pl].empty() && vf->random_fighter_names().count(random_named[pl])) set_kind(random_named[pl]);
					else { st->pending = true; set_kind("*"); }   // (no seed yet: Random until one is typed in its row)
					rebuild(pl, false);
				},
				{}, false,
				[this, pl, st, set_kind] { st->typing = st->pending = false; set_kind(""); rebuild(pl, false); } });
			fighter_sub[pl] = -1;
			const int kind = st->typing || st->pending ? 2 : kind_of(pl);
			if (kind == 2) {
				fighter_sub[pl] = int(p.items.size());
				menu_item it{ "Seed",
					"A random fighter's name, from its energy bar: the same seed is the same build and moves on any machine, in new colours each game. Enter: type a new one.",
					[this, pl, st] { return st->typing ? st->typed + "_" : st->pending ? std::string("Enter to type a seed (Random till then)") : random_player[pl]; },
					{}, start_typing };
				it.below = [st] { return st->typing ? st->error : std::string(); };
				it.sub = true;
				p.items.push_back(it);
			} else if (kind == 3) {
				fighter_sub[pl] = int(p.items.size());
				static const char *ABOUT[] = { "",
					"Siba (Majido): an Arabian fighter cut before release; his moves are mostly Lau's. Unfinished: borrowed moves, no throws.",
					"Jeff: an early Jeffry, cut before release; his moves are Jacky's. Unfinished: borrowed moves, no throws.",
					"Takeru: the early Akira, before he was renamed. Unfinished: borrowed moves, no throws.",
					"Rekka: one of the Koskol brothers, meant as the final bosses. Unfinished: borrowed moves, no throws.",
					"Score: one of the Koskol brothers, meant as the final bosses. Unfinished: borrowed moves, no throws.",
					"Aeon: one of the Koskol brothers, meant as the final bosses. Unfinished: borrowed moves, no throws.",
					"Dural: the final boss, playable." };
				menu_item ch{ "Character", ABOUT[std::clamp(st->secret, 1, 7)],
					[st] { return std::string(UNUSED_CHARACTERS[std::clamp(st->secret, 1, int(std::size(UNUSED_CHARACTERS)) - 1)]); },
					[this, pl, st, set_kind](int d) {
						st->secret = 1 + wrap(st->secret - 1 + d, N_SECRET - 1);
						set_kind("#" + std::to_string(st->secret));
						rebuild(pl, true);   // (its help: this character's)
					} };
				ch.sub = true;
				p.items.push_back(ch);
			}
		}
		p.items.push_back({ "Random opponents",
			"Every computer opponent a random fighter, looks and moves, made afresh for each match.",
			[this] { return on_off(random_cpu); },
			[this](int) {
				random_cpu = !random_cpu;   // (applied as the game restarts)
				random_now();
				changed();
			} });
		static const char *WHO[] = { "Off", "Player 1", "Player 2", "Both" };
		heading("FIGHT");
		p.items.push_back({ "Infinite energy", "Energy stays full: hits land but never knock out.",
			[this] { return std::string(WHO[infinite_energy]); },
			[this](int d) { infinite_energy = wrap(infinite_energy + d, 4); changed(); } });
		p.items.push_back({ "One-hit KO", "Every fighter (but one with infinite energy) is down to their last bit of energy: any hit wins the round.",
			[this] { return on_off(one_hit_ko); },
			[this](int) { one_hit_ko = !one_hit_ko; changed(); } });
		p.items.push_back({ "No time limit", "The round's clock stays at its start and never runs out: a round ends by knockout or ring out.",
			[this] { return on_off(no_time_limit); },
			[this](int) { no_time_limit = !no_time_limit; changed(); } });
		static const char *RING[] = { "Normal", "Tiny", "Huge" };
		p.items.push_back({ "Ring", "Tiny is half the smallest ring: ring outs everywhere. Huge is far wider than the stage: no ring outs.",
			[this] { return std::string(RING[ring_cheat]); },
			[this](int d) { ring_cheat = wrap(ring_cheat + d, 3); changed(); vf->set_ring_width(ring_now()); } });
		heading("LOOKS");
		p.items.push_back({ "Kage's mask", "Comes off: a hit that knocks Kage's hood back shows his face, as on a cabinet that's been played for nine hours. As the arcade: it stays on until this game has been played that long.",
			[this] { return std::string(kage_unmasked ? "Comes off" : "As the arcade"); },
			[this](int) { kage_unmasked = !kage_unmasked; changed(); vf->set_kage_unmasked(kage_unmasked); } });
		p.items.push_back(ctx->back_item());
		return p;
	}
};

}
}

std::unique_ptr<game_host> make_game_host() { return std::make_unique<game::vf::host>(); }
