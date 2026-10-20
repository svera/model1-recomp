#pragma once

#include <cstdint>
#include <map>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include "../game.h"

class model1;

namespace game {
struct gameplay_callbacks;

namespace vf {

extern const video_traits VIDEO;

inline constexpr const char *UNUSED_CHARACTERS[] = {
	"Akira", "Siba (Majido)", "Jeff", "Takeru (early Akira)",
	"Rekka (Koskol brother)", "Score (Koskol brother)", "Aeon (Koskol brother)", "Dural"
};
inline constexpr uint32_t RECORD_SIZE = 0x176;
inline constexpr uint32_t MOVE_COUNT = 187;

extern const char *const SECRET_ICON_NAMES[];

struct gameplay_state : game::gameplay_state
{
	explicit gameplay_state(model1 &machine) : m(machine) {}
	model1 &m;
	void reset();
	void frame_begin();
	void frame_end();
	bool set_ring_width(int width);
	std::string ranking_names() const;
	void set_ranking_names(const std::string &text);
	void set_kage_unmasked(bool on);
	bool m_kage_unmasked = false;
	// The unused characters left in the game (TCRF: Jeff, Majido, Takeru, the Koskol brothers)
	// play in Akira's place: his entries in the character tables point at theirs. 0 is Akira; the
	// rest are UNUSED_CHARACTERS. Read as a fight loads, so a change wants a restart.
	void set_akira_slot(int which);
	void akira_slot_heads(uint32_t record);   // (set_akira_slot: a select portrait for a head without one)
	// The select's icon for the character in Akira's place, in his icon's tiles (56 x 64 pixels,
	// xRGB, 15 colours at most; nullptr: his own). Returns false for a picture it can't show.
	bool set_akira_icon(const uint32_t *xrgb);
	// Cheats, before each frame: energy kept full for the players in `infinite` (bit 0 player 1,
	// bit 1 player 2), the rest left with 1 when `one_hit` (any hit a knockout), and the round's
	// clock stopped when `no_time`.
	void fight_cheats(int infinite, bool one_hit, bool no_time);
	bool set_no_time_limit(bool on);   // (true: the game's code changed, to be recompiled)
	bool m_clock_held = false;
	struct secret_icon {
		bool on = false, active = false;   // a picture set; in the tiles now
		uint32_t at = 0;                   // where the icon's map is (tile RAM word)
		std::vector<uint16_t> chars, pal, map;            // ours: the tiles, the colours, the map
		std::vector<uint16_t> keep_chars, keep_pal, keep_map;   // VF's own, put back after
	} m_icon;
	void secret_icon_frame();
	// Cheats: a random fighter in Akira's place, a patchwork of the cast's (and the cut characters')
	// body parts, a pair of limbs from one of them, and a head of one, made afresh each time the
	// player select comes up. Both of Akira's costumes (player 1's and player 2's) get their own.
	void set_random_fighter(bool on, bool moves = false, bool cpu = false);
	// Cheats' fighter names: a name picks the players' random fighter (the same one every game, on
	// every machine): the first of seeds 1-RANDOM_SEEDS whose fighter has that name. Empty: none, a
	// new one each game. False (nothing changed) for a name no seed gives
	static constexpr uint32_t RANDOM_SEEDS = 32768;
	// Each player's fighter with Random fighter on: "" the select as ever (Akira is Akira), "*" a
	// random one, a new one each game, or a name's. False (nothing changed) for a name no seed gives.
	// Applied as the fighters are next made (set_random_fighter, the select)
	bool set_random_player(int pl, const std::string &setting);
	const std::map<std::string, uint32_t> &random_fighter_names();   // (every name, its seed; made once)
	// The strands' lookup (an instruction of the game's) to our list, and the special commands'
	// lookup to ours by record, for Random fighter: before the code is recompiled (true: it changed)
	void strand_log();   // TESTONLY
	bool set_strands_moved(bool on);   // moves: each command's move from a random fighter too; cpu: every opponent random too
	bool m_random_fighter = false, m_random_moves = false, m_random_cpu = false, m_select_seen = false;
	static constexpr uint32_t RECORD_SIZE = 0x176, MOVE_COUNT = 187;
	struct random_cast {
		bool made = false;
		std::map<uint32_t, std::vector<uint8_t>> orig;   // each record's own
		std::vector<uint32_t> recs, heads, tables;       // the records, those with a head list, the moves tables
		std::map<std::string, std::vector<uint32_t>> by_name;   // each character's records (costumes)
		std::vector<std::pair<uint32_t, uint32_t>> move_groups;   // the moves tables' commands in groups (first, end)
		std::map<uint32_t, uint8_t> flags;               // each record's flags byte (+0d) with its key
		std::unordered_map<uint16_t, float> move_lean;   // each move's character's head lean (NaN: shared by several)
	} m_random_cast;
	std::map<uint32_t, uint32_t> m_special_list;   // (each random record's special commands: its list)
	std::map<uint32_t, uint32_t> m_stance_from;    // (each random record's stance: the moves table it came from)
	std::map<uint32_t, uint8_t> m_voice_of;        // (each random record's voice: a character's number, VOICE_OURS)
	std::map<uint32_t, std::array<uint8_t, 6>> m_move_ids;   // (each random record's character number for VF's checks by character on its moves, MOVE_SITES: the one whose command it has)
	float m_look_share = 1;                         // (the fighter just made: the share of its looks one character's, random_fighter_make)
	bool m_look_same_body = false;                  // (... its face and torso one character's)
	std::map<uint32_t, uint32_t> m_moves_held;     // (the random records' moves tables, held back while the boot builds the stances' table)
	uint64_t m_boot_at = 0;                        // (the frame of the last reset)
	void random_fighter_hold_moves();
	void random_fighter_specials();
	uint32_t m_random_seen[2] = { 0, 0 };   // (each player's fighter last seen in their block)
	// (Random opponents: a credit's run, its select to GAME OVER (continues in it): the computer's fighters as they
	// were fought, kept through a continue's select and shown so in the ending's replays, not made again
	// nor built again)
	bool m_run_on = false;
	int m_run_idle = 0;                   // (frames with no one in)
	std::set<uint32_t> m_fought;          // (the computer's records this run)
	std::set<uint32_t> m_built_run;       // (the computer's records built in this run)
	struct cpu_kept {   // (a random record as it was: its bytes and what it's known by)
		std::vector<uint8_t> bytes; std::string name; float hue = 0, outfit = 0; int theme = 0; uint32_t stance = 0, list = 0;
		bool has_hue = false, has_outfit = false, has_stance = false, has_list = false, has_move_ids = false; int voice = -1; std::array<uint8_t, 6> move_ids{};
	};
	std::map<uint32_t, cpu_kept> m_cpu_akira;   // (the computer's Akira as fought this run, by record: put back at a continue, onto the one no player has)
	int m_dural_lm = -2;                  // (Dural's own polygons' light mode, her shine: -2 not looked for yet, -1 none)
	int dural_lightmode();
	uint32_t m_cpu_human[2] = { 0, 0 };    // (the records the players picked at the last select: random_fighter_cpu)
	bool cpu_keeps(uint32_t r) const;
	void random_fighter_ids();             // (a random record's character number: Akira's in a match, its own outside one)
	int m_akira_track = -1;                // (VF's stage music for Akira, fd04f8's entry 11: random_fighter_ids)
	random_cast &random_fighter_cast();
	uint32_t random_next(uint32_t n);
	void random_fighter_make(uint32_t rec);
	void random_fighter_make_named(uint32_t rec);   // (Random's: one of the named fighters, random_fighter_names)
	void random_fighter_colour(uint32_t rec);
	void random_fighter_strands();   // (the strands' list for every slot, as the records are now)
	std::vector<uint8_t> m_strands_orig;   // (the fight's swinging-strand list, fda917, as VF has it)
	bool m_strands_moved = false, m_specials_moved = false;
	bool m_random_applied = false;   // (records made random since the cast was last put back)
	std::map<uint32_t, std::string> m_random_names;   // (each random record's name, from what it's made of)
	static const char *record_name(uint32_t rec);
	static std::string blend_name(const std::map<std::string, int> &made_of);
	void random_fighter_hud();
	static constexpr uint32_t RANKING = 0xf800, RANK_ENTRIES = 18, RANK_ENTRY = 10;   // (in the backup RAM, 40f800: time, 0, character, initials, 0)
	uint8_t m_rank_prev[RANK_ENTRIES * RANK_ENTRY] = {};
	bool m_rank_seen = false;
	std::string m_rank_names[RANK_ENTRIES];
	uint32_t m_rank_version = 0;
	void ranking_frame();
	int m_akira_which = 0;   // (Cheats' Secret character: set_akira_slot's which, 0 none)
	uint8_t m_akira_ribbon[2] = { 0, 0 };   // (set_akira_slot's portrait slot bytes for Akira's places, fd2b60 and fd2b5b; 0 not set yet)
	void select_cover(bool select);
	void random_fighter_cpu();
	// Random fighter's round-1 build (random_fighter_build): as a match loads, a random fighter's
	// parts are built from the feet up during the intro's camera sweep, polygon by polygon along each
	// part's strip, each polygon a white flash as it lands, grey until all are on; then the colour
	// sweeps up the same way before READY. Each player's: its record, the frame it starts, each of its
	// models' group (0 feet ... 8 head) and polygon count
	struct build_model { int group, polys; };
	struct fighter_build { uint32_t rec = 0; int64_t start = -1; std::unordered_map<uint32_t, build_model> group; } m_build[2];
	std::unordered_set<uint32_t> m_build_models[2];   // (each player's record's models, built or not: a shared one's owner by the nearer torso)
	bool m_build_select = false;   // (a select showing last frame: its end builds every record afresh)
	void random_fighter_build(bool select);
	struct build_at { int64_t e; int appear, colour, polys; };   // (a model's: frames into the build, its polygons' start and colour's)
	bool build_info(uint32_t poly_adr, build_at &b) const;   // (model1_video.cpp: false for a model as ever)
	static int build_front(int64_t e, int start, int span, int polys) { return e < start ? 0 : int(std::min<int64_t>((e - start) * polys / span + 1, polys)); }   // (its polygons on by e)
	static constexpr int BUILD_DELAY = 8, BUILD_STEP = 6, BUILD_SPAN = 16, BUILD_FLASH = 4, BUILD_COLOUR = 70, BUILD_COLOUR_STEP = 2, BUILD_COLOUR_SPAN = 10;   // (frames: the sweep's close half, READY's start, colour fully in before the fight)
	void secret_akira_record(uint32_t rec, int which);
	void akira_portrait_of_head(uint32_t rec, uint32_t head);
	void akira_camera(int which);
	int akira_camera_for(uint32_t rec);
	int m_akira_cam_now = -1;
	std::map<uint32_t, std::string> m_secret_names;   // (Akira's records holding a player's secret character: its name)
	bool m_cpu_pending = false, m_rolling_at_select = false;   // (Random opponents' records to make when the select is over)
	bool select_showing() const;
	uint32_t m_secret_recs[2] = { 0, 0 };   // (Cheats' Secret character: its records in Akira's place, else 0)
	// Heads worn on another's body, turned to sit as on their own (by poly address: the cosine and
	// sine of a turn about the head's own side axis; see random_fighter_heads)
	// and the hue its hair, hats and bands take (skin kept; -1: as they are)
	struct head_style { float c = 1, s = 0, hue = -1, metal = -1, bright = 1; int pl = -1; bool strand = false; };   // (metal >= 0: Dural's head, tinted to that saturation, its brightness scaled; pl: whose; strand: hair that swings)
	// A head on another's torso: its neck's foot can sit above the torso's top (another's skeleton,
	// a shorter torso): the head (and its strands) brought down onto it, in the torso's own frame
	// (model1_video.cpp, neck_*). Each player's torso as last drawn, and the head's lowest point
	// near the neck this picture, for the next
	struct neck_fit {
		bool torso = false;              // (seen: a, inv, t, top, axis, sign hold)
		float a[9] = {}, inv[9] = {}, t[3] = {};   // its matrix (columns), the inverse of its 3x3, its place
		float lo[3] = {}, hi[3] = {};    // its model's extent, its own axes
		int axis = 1; float sign = 1;    // toward its head
		float head_low = 0; bool head_seen = false;   // (this picture's, along axis*sign)
		float drop = 0;                  // how far the head comes down (the last picture's gap)
		uint32_t torso_poly = 0;
		float pitch = 0, rel[9] = {}, view[9] = {}; int pitch_seen = 0;   // TESTONLY (VF_HEADLOG): the head against its torso, and as drawn
	};
	neck_fit m_neck[2];
	mutable int m_owner_last = -1;   // (the player whose model was drawn last, not a shared one: model_owner)
	uint32_t m_torso_poly[2] = { 0, 0 };
	int m_torso_seen = 0;
	std::unordered_map<uint32_t, std::vector<std::array<float, 3>>> m_model_points;   // (a model's own points, by its poly address)
	const std::vector<std::array<float, 3>> &model_points(const float *poly_data, uint32_t poly_adr);
	void neck_torso(int pl, const float *poly_data, uint32_t poly_adr);
	bool neck_head(int pl, bool strand, const float *poly_data, uint32_t poly_adr, float tc, float ts, float *off);
	void neck_settle();
	void dump_part(uint32_t tex_adr, uint32_t poly_adr, uint32_t size);   // TESTONLY
	std::unordered_map<uint32_t, head_style> m_head_turn;
	// (a model both players wear, the same head on both: the first drawn in a list is player 1's,
	// the second player 2's, with player 2's style here; the game draws player 1's first)
	std::unordered_map<uint32_t, head_style> m_head_turn2;
	std::unordered_map<uint32_t, int> m_head_seen;
	std::map<uint32_t, float> m_random_hue;   // (each random record's head's hue, 0-1)
	std::map<uint32_t, float> m_random_outfit;   // (each random record's clothes' turn round the colour wheel, 0-1)
	std::map<uint32_t, int> m_random_theme;      // (... and their theme: OUTFIT_THEMES)
	struct outfit_tones {
		uint32_t n[32 * 16] = {};   // (polygons drawn at each brightness (32) and hue (16): their median is the split)
		uint64_t at = ~0ull, below = 0, total = 0; int med = 0;   // (the split as the frame began: once a frame, not a polygon)
	};
	std::unordered_map<uint32_t, outfit_tones> m_outfit_tones;
	struct skin_sum { double h = 0, s = 0, v = 0; uint64_t n = 0; };
	std::unordered_map<uint32_t, skin_sum> m_skin_ref, m_skin_part;
	// (a player's record just changed: the last one's models and colours, as they were, for the
	// pictures of the old scene still drawn before the cut (its clothes and skin snapped back to the
	// cast's for a frame, or took the record's new ones as Random opponents remade it))
	struct build_prev { uint32_t rec = 0; int64_t until = -1; std::unordered_set<uint32_t> models; float outfit = -1; int theme = 0; outfit_tones tones; skin_sum skin; bool has_skin = false; } m_build_prev[2];
	static constexpr int BUILD_PREV_FRAMES = 12;   // (one skin a fighter: each record's face's skin, each body model's own)   // (each record's clothes' brightnesses seen: black and white's, black and gold's split)
	static constexpr int BUILD_SCENE_LAG = 4;   // (a record changed, the old scene's pictures go on for 3 frames: the new scene's from the 4th)
	enum { THEME_TURN, THEME_BW, THEME_BLACK_GOLD, THEME_COOL, THEME_WARM, THEME_OPPOSITE, THEME_NEON, THEME_PASTEL, OUTFIT_THEMES };
	int model_owner(uint32_t poly_adr) const;   // (a record's model: whose, by the nearer torso if both's; -1 neither's)
	// each player's styled models last frame, and those of the record before for a few frames after
	// it changes (the picture is a few frames behind the players' blocks)
	std::vector<std::pair<uint32_t, head_style>> m_head_last[2], m_head_lag[2];
	uint32_t m_head_rec[2] = { 0, 0 };
	float m_lean_body[2] = { NAN, NAN }, m_lean_turn[2] = { 0, 0 };   // (each player's: the move's character's lean, last known; the head's turn now, degrees)
	int m_head_lag_left[2] = { 0, 0 };
	void random_fighter_heads();
	void strand_polys(int pl, std::vector<uint32_t> &out) const;
	void random_fighter_strands_settle();
	std::map<uint32_t, std::vector<float>> m_strand_roots;   // (each strand task's chains' first points last frame: x y z, and 1 if still as the game started it)
	static float head_lean(uint32_t rec);
	int64_t m_pick_at = -1;   // (the frame the select came up: the random fighter is picked for the players)
	uint32_t m_random_state = 0;
	int m_player_mode[2] = { 1, 1 };                    // (each player's: 0 the select as ever, 1 random, 2 a name's)
	uint32_t m_player_seed[2] = { 0, 0 };
	std::map<uint32_t, std::pair<int, uint32_t>> m_akira_made;   // (each Akira record as made: mode, seed)
	void random_akira_record(uint32_t rec, int mode, uint32_t seed);
	void random_fighter_picks();
	uint32_t akira_record_of(int pl) const;
	uint32_t m_akira_held[2] = { 0, 0 };   // (the Akira record each random player picked, kept while it plays)
	void random_fighter_portrait_strands();
	std::map<uint32_t, std::vector<uint32_t>> m_portrait_strand_models;   // (each Akira record's portrait strands' models: its hue)
	bool picked_now(int pl) const;
	std::map<std::string, uint32_t> m_seed_names;
	void random_fighter_roll();
	void random_fighter_frame();
	// (the select hidden: one player alone at it, with a random fighter: who they got is a surprise
	// when the fight comes in. Two players in (from the start, or a challenger): it shows, portraits
	// and all)
	// (shown: the picture's question. The select's first frames, before the game marks who's in (its
	// words come a frame or more after it): hidden while a random fighter is set, as it may be the one
	// playing alone: it showed for a frame before. The pick's pace (run_frame) asks only once they're in)
	bool random_select_hidden(bool shown = true) const;
	static constexpr int64_t RANDOM_PREVIEW = 140;   // (frames the select shows a random fighter's portrait before its pick, none picking as ever)
	// Replaces n bytes of the main CPU program, if their CRC-32 is `crc` (guards against other sets).
	bool patch_program(uint32_t addr, size_t n, uint32_t crc, const uint8_t *to);
	// Blanks the FREE PLAY notices (the attract/title one and the fight's): for the PC build,
	// which is always free play. Returns how many it found.
	int hide_free_play_text();
	void show_free_play_text();          // (puts back what hide_free_play_text changed)
};

class gameplay
{
public:
	explicit gameplay(model1 &machine) : m_machine(machine) {}

	static constexpr int FREE_PLAY = 0x1901;
	enum class assignment { match_1p, match_vs, difficulty, advertise_sound, continues, language };

	bool force_free_play() { return set_coin_setting(FREE_PLAY); }
	int coin_setting();
	bool set_coin_setting(int value);
	int assignment_value(assignment which);
	bool set_assignment(assignment which, int value);
	bool set_ring_width(int width);
	void set_akira_slot(int which);
	bool set_akira_icon(const uint32_t *xrgb);
	void fight_cheats(int infinite, bool one_hit, bool no_time);
	bool set_no_time_limit(bool on);
	void set_random_fighter(bool on, bool moves = false, bool cpu = false);
	static constexpr uint32_t RANDOM_SEEDS = gameplay_state::RANDOM_SEEDS;
	void set_kage_unmasked(bool on);
	bool set_strands_moved(bool on);
	bool set_random_player(int pl, const std::string &setting);
	const std::map<std::string, uint32_t> &random_fighter_names();
	std::string ranking_names() const;
	void set_ranking_names(const std::string &text);
	uint32_t ranking_names_version() const;
	bool random_select_hidden(bool shown = true) const;

	int hide_free_play_text();
	void show_free_play_text();
	std::vector<std::string> screen_names() const;
	bool load_screen(const std::string &name, std::vector<uint32_t> xrgb, int w, int h);

private:
	model1 &m_machine;
};

void configure_video(model1 &machine, const std::shared_ptr<gameplay_state> &state, gameplay_callbacks &callbacks);
gameplay_callbacks create_gameplay(model1 &machine);

}
}
