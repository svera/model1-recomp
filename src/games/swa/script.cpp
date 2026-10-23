// Star Wars Arcade's scripted play: attract, two coins (three for PILOT & GUNNER), the mode and
// mission menus confirmed with Start, then seeded flying and firing until the shields or the time
// run out, the rankings and back to attract; with the checkpoints such a run must reach.
//
// The battery RAM the checks read was found by watching runs against the screen, not from the
// program's code: 0x40f008 the sequence (1-2 attract, 11-13 credited and playing, 14 game over and
// rankings, then attract again), 0x40f334 the time left in 1/30 s (TIME on screen), 0x40f410 and
// 0x40f510 the Pilot's and Gunner's scores (BCD), 0x40f100 the PILOT rankings (ten 8-byte entries:
// BCD score, a name) and 0x40f150 the PILOT & GUNNER ones (ten 12-byte entries: the team's total,
// both names).
#include "../script.h"
#include "../../machine/model1.h"
#include "profile.h"
#include <cstdio>

namespace game::swa {

namespace {

constexpr uint32_t SEQUENCE = 0xf008, TIME = 0xf334, PILOT_SCORE = 0xf410, GUNNER_SCORE = 0xf510,
	PILOT_RANKINGS = 0xf100, TEAM_RANKINGS = 0xf150;
constexpr int RANKING_ENTRIES = 10, PILOT_RANKING_SIZE = 8, TEAM_RANKING_SIZE = 12;
constexpr uint8_t SEQ_ATTRACT = 1, SEQ_PLAYING = 13, SEQ_GAME_OVER = 14;

// the analog channels the script sets (channels 4-5, the Gunner's stick, stay centred)
constexpr int STICK_X = 0, STICK_Y = 1, THROTTLE = 2;
constexpr int STICK_LOW = 27, STICK_CENTRE = 127, STICK_HIGH = 227;   // IO_PROFILE's travel

// Inputs, by frame from power-on: the coins land in the attract's Star Destroyer fly-by; Start
// opens the mode menu, the stick picks PILOT (centred) or PILOT & GUNNER (back), Start confirms it
// and then the mission (Rebel Attack, the first).
constexpr int COIN_FRAMES[] = { 1300, 1340, 1380 };
constexpr int START_FRAMES[] = { 1400, 1490, 1560 };
constexpr int SELECT_FROM = 1420, SELECT_TO = 1470, FLY_FROM = 1600, PRESS = 6;

class play final : public auto_play
{
public:
	play(bool gunner, uint32_t seed, bool checks)
		: m_gunner(gunner), m_rng(seed * 2654435761u + 1), m_checks(checks) {}

	script_inputs frame(int f) override
	{
		script_inputs s;
		for (int i = 0; i < (m_gunner ? 3 : 2); i++)
			if (f >= COIN_FRAMES[i] && f < COIN_FRAMES[i] + PRESS) s.pressed[0] |= COIN;
		for (int start : START_FRAMES)
			if (f >= start && f < start + PRESS) s.pressed[0] |= START;
		if (f < FLY_FROM) {
			s.analog[STICK_X] = STICK_CENTRE;
			s.analog[STICK_Y] = m_gunner && f >= SELECT_FROM && f < SELECT_TO ? STICK_HIGH : STICK_CENTRE;
			return s;
		}
		// flying: the stick eases towards a new random point every second or two, the throttle
		// changes now and then, the lasers fire in bursts and a torpedo every five seconds
		if (f >= m_next_turn) {
			m_target_x = STICK_LOW + rnd(STICK_HIGH - STICK_LOW + 1);
			m_target_y = STICK_LOW + rnd(STICK_HIGH - STICK_LOW + 1);
			if (rnd(3) == 0) m_throttle = 28 + rnd(101);   // FAST (28) to half
			m_next_turn = f + 60 + rnd(60);
		}
		m_x += (m_target_x - m_x) / 8;
		m_y += (m_target_y - m_y) / 8;
		s.analog[STICK_X] = m_x;
		s.analog[STICK_Y] = m_y;
		s.analog[THROTTLE] = m_throttle;
		const int t = f - FLY_FROM;
		if ((t / 8) % 2) s.pressed[1] |= PILOT_LASER;
		if (t % 300 < PRESS) s.pressed[1] |= PILOT_TORPEDO;
		if (m_gunner) {
			if ((t / 8) % 2 == 0) s.pressed[1] |= GUNNER_LASER;
			if ((t + 150) % 300 < PRESS) s.pressed[1] |= GUNNER_TORPEDO;
		}
		return s;
	}

	std::string check(int f, model1 &m) override
	{
		if (!m_checks) return {};
		const auto &ram = m.nvram();
		const uint8_t sequence = ram[SEQUENCE];
		const unsigned time = ram[TIME] | ram[TIME + 1] << 8;
		auto reached = [&](const char *name, bool &recorded) {
			if (!recorded) {
				printf("SWA scenario reached: %s (frame %d, time %u, pilot %x, gunner %x)\n", name, f,
					time == 0xffff ? 0 : time / 30, score(m, PILOT_SCORE), score(m, GUNNER_SCORE));
				recorded = true;
			}
		};
		if (!m_credited && f >= COIN_FRAMES[0] && sequence == SEQ_PLAYING)
			reached("credited", m_credited);
		// the clock starts with the flight (180 s for Rebel Attack) and counts down
		if (m_credited && !m_flying && time != 0xffff && time != 0) {
			if (m_last_time && time < m_last_time) reached("flight (time counting down)", m_flying);
			m_last_time = time;
		}
		if (m_flying && !m_pilot_hit && score(m, PILOT_SCORE) != 0)
			reached("pilot's first hit", m_pilot_hit);
		if (m_gunner && m_flying && !m_gunner_hit && score(m, GUNNER_SCORE) != 0)
			reached("gunner's first hit", m_gunner_hit);
		if (m_flying && !m_game_over && sequence == SEQ_GAME_OVER) {
			reached("game over", m_game_over);
			m_final_pilot = score(m, PILOT_SCORE);
			m_final_gunner = score(m, GUNNER_SCORE);
		}
		// back in attract the name entry is over: PILOT ranks the Pilot's score, PILOT & GUNNER the
		// team's total
		if (m_game_over && !m_attract && sequence == SEQ_ATTRACT) {
			const uint32_t table = m_gunner ? TEAM_RANKINGS : PILOT_RANKINGS;
			const int size = m_gunner ? TEAM_RANKING_SIZE : PILOT_RANKING_SIZE;
			const uint32_t final_score = m_gunner ? bcd_add(m_final_pilot, m_final_gunner) : m_final_pilot;
			if (!ranked(m, table, size, final_score)) {
				char text[200];
				snprintf(text, sizeof text, "SWA %s rankings at frame %d lack the final score %x (top three %x, %x, %x)",
					m_gunner ? "PILOT & GUNNER" : "PILOT", f, final_score, score(m, table),
					score(m, table + size), score(m, table + 2 * size));
				return text;
			}
			reached("attract after game over", m_attract);
			reached("rankings entry", m_ranked);
		}
		if (m_game_over && sequence == SEQ_PLAYING)
			return "SWA started another game after game over";
		return {};
	}

	std::string finish() override
	{
		if (!m_checks) return {};
		if (!(m_credited && m_flying && m_pilot_hit && m_game_over && m_ranked && m_attract))
			return "SWA scenario did not reach every required checkpoint";
		if (m_gunner && !m_gunner_hit)
			return "SWA gunner scenario: the Gunner never scored";
		return {};
	}

	int min_frames() const override { return m_checks ? 22000 : 0; }

private:
	static uint32_t score(model1 &m, uint32_t at)
	{
		const auto &ram = m.nvram();
		const uint32_t v = ram[at] | ram[at + 1] << 8 | ram[at + 2] << 16 | uint32_t(ram[at + 3]) << 24;
		return v == 0xffffffff ? 0 : v;
	}
	static bool ranked(model1 &m, uint32_t table, int size, uint32_t final_score)
	{
		if (!final_score) return false;
		for (int i = 0; i < RANKING_ENTRIES; i++)
			if (score(m, table + i * size) == final_score) return true;
		return false;
	}
	static uint32_t bcd_add(uint32_t a, uint32_t b)
	{
		uint32_t sum = 0, carry = 0;
		for (int shift = 0; shift < 32; shift += 4) {
			uint32_t digit = ((a >> shift) & 0xf) + ((b >> shift) & 0xf) + carry;
			carry = digit > 9;
			sum |= (carry ? digit - 10 : digit) << shift;
		}
		return sum;
	}
	int rnd(int k) { m_rng = m_rng * 1103515245 + 12345; return int((m_rng >> 16) % unsigned(k)); }

	const bool m_gunner;
	uint32_t m_rng;
	const bool m_checks;
	int m_next_turn = 0, m_target_x = STICK_CENTRE, m_target_y = STICK_CENTRE, m_x = STICK_CENTRE,
		m_y = STICK_CENTRE, m_throttle = 60;
	unsigned m_last_time = 0;
	uint32_t m_final_pilot = 0, m_final_gunner = 0;
	bool m_credited = false, m_flying = false, m_pilot_hit = false, m_gunner_hit = false,
		m_game_over = false, m_ranked = false, m_attract = false;
};

}

std::unique_ptr<auto_play> make_auto_play(const std::string &scenario, uint32_t seed, bool checks)
{
	if (!scenario.empty() && scenario != "gunner") return nullptr;
	return std::make_unique<play>(scenario == "gunner", seed, checks);
}

}
