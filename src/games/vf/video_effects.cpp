// Sega Model 1 rendering extensions for Virtua Fighter's fighter records.
// Ported from the upstream Model 1 renderer (BSD-3-Clause).
#include "gameplay.h"
#include "../../machine/model1.h"
#include <glm/glm.hpp>
#include <cstdio>
#include <cstring>
#include <set>

namespace game::vf {

// A model's own points (its first two, then two a polygon, 10 words each), kept by address
const std::vector<std::array<float, 3>> &gameplay_state::model_points(const float *poly_data, uint32_t poly_adr)
{
	auto [it, fresh] = m_model_points.try_emplace(poly_adr);
	if (fresh) {
		auto &v = it->second;
		v.push_back({ poly_data[poly_adr], poly_data[poly_adr + 1], poly_data[poly_adr + 2] });
		v.push_back({ poly_data[poly_adr + 3], poly_data[poly_adr + 4], poly_data[poly_adr + 5] });
		// (to a type 0, within its region: the polygon RAM's 0x400000 words or the ROM's)
		const size_t words = poly_data == m.polygon_data(false) ? m.polygon_rom().size() / 4 : 0x400000;
		for (uint32_t a = poly_adr + 6, n = 0; n < 4096 && a + 10 <= words && (*reinterpret_cast<const uint32_t *>(poly_data + a) & 3); a += 10, n++) {
			v.push_back({ poly_data[a + 4], poly_data[a + 5], poly_data[a + 6] });
			v.push_back({ poly_data[a + 7], poly_data[a + 8], poly_data[a + 9] });
		}
	}
	return it->second;
}

// A random fighter's neck: a head made for another's skeleton and torso can sit with its neck's
// foot above this torso's top, a gap at the throat (Sarah's head over Jacky's shoulders). Each
// picture the torso's matrix is kept (neck_torso) and the head's lowest point near the neck
// measured in the torso's own frame (neck_head); the gap, if any, brings the head and its strands
// down along the torso's axis toward the head in the next (neck_settle). A head that sits into its
// torso is left as it is.
void gameplay_state::neck_torso(int pl, const float *poly_data, uint32_t poly_adr)
{
	neck_fit &n = m_neck[pl];
	const float *T = m.render_matrix();
	const glm::mat3 a(T[0], T[1], T[2], T[3], T[4], T[5], T[6], T[7], T[8]);
	if (std::fabs(glm::determinant(a)) < 1e-12f) return;
	const glm::mat3 inv = glm::inverse(a);
	for (int k = 0; k < 9; k++) { n.a[k] = a[k / 3][k % 3]; n.inv[k] = inv[k / 3][k % 3]; }
	n.t[0] = T[9], n.t[1] = T[10], n.t[2] = T[11];
	const auto &pts = model_points(poly_data, poly_adr);
	for (int k = 0; k < 3; k++) { n.lo[k] = 1e30f; n.hi[k] = -1e30f; }
	for (const auto &q : pts)
		for (int k = 0; k < 3; k++) { n.lo[k] = std::min(n.lo[k], q[k]); n.hi[k] = std::max(n.hi[k], q[k]); }
	n.torso = !pts.empty();
	n.torso_poly = poly_adr;
}

bool gameplay_state::neck_head(int pl, bool strand, const float *poly_data, uint32_t poly_adr, float tc, float ts, float *off)
{
	neck_fit &n = m_neck[pl];
	if (!n.torso) return false;
	const float *T = m.render_matrix();
	const glm::mat3 a(T[0], T[1], T[2], T[3], T[4], T[5], T[6], T[7], T[8]);
	const glm::mat3 ta(n.a[0], n.a[1], n.a[2], n.a[3], n.a[4], n.a[5], n.a[6], n.a[7], n.a[8]);
	const glm::mat3 tinv(n.inv[0], n.inv[1], n.inv[2], n.inv[3], n.inv[4], n.inv[5], n.inv[6], n.inv[7], n.inv[8]);
	const glm::vec3 tt(n.t[0], n.t[1], n.t[2]), ht(T[9], T[10], T[11]);
	if (!strand && n.pitch_seen == 0) {   // TESTONLY (VF_HEADLOG): the head's tilt against its torso
		glm::mat3 r = tinv * a;   // (the head's turn in the torso's frame, scales out)
		for (int c = 0; c < 3; c++) r[c] = glm::normalize(r[c]);
		for (int k = 0; k < 9; k++) n.rel[k] = r[k / 3][k % 3];
		glm::mat3 v = a;
		for (int c = 0; c < 3; c++) v[c] = glm::normalize(v[c]);
		for (int k = 0; k < 9; k++) n.view[k] = v[k / 3][k % 3];
		n.pitch_seen = 1;
	}
	if (!strand) {
		// (the axis toward the head: the torso's own axis the head's place lies most along)
		const glm::vec3 h = tinv * (ht - tt);
		int k = 0;
		for (int j = 1; j < 3; j++) if (std::fabs(h[j]) > std::fabs(h[k])) k = j;
		const float sg = h[k] < 0 ? -1.0f : 1.0f;
		const int u = (k + 1) % 3, w = (k + 2) % 3;
		// (near the neck: within a third of the torso's half width of the head's line)
		const float r = 0.33f * 0.5f * std::max(n.hi[u] - n.lo[u], n.hi[w] - n.lo[w]);
		float low = 1e30f;
		for (const auto &q : model_points(poly_data, poly_adr)) {
			glm::vec3 v(q[0], q[1], q[2]);
			if (ts != 0) { const float x = tc * v.x + ts * v.z; v.z = tc * v.z - ts * v.x; v.x = x; }
			const glm::vec3 p = tinv * (a * v + ht - tt);
			if ((p[u] - h[u]) * (p[u] - h[u]) + (p[w] - h[w]) * (p[w] - h[w]) > r * r) continue;
			low = std::min(low, sg * p[k]);
		}
		if (low < 1e29f) {
			if (!n.head_seen || n.axis != k || n.sign != sg) n.head_low = low;
			else n.head_low = std::min(n.head_low, low);
			n.head_seen = true;
			n.axis = k;
			n.sign = sg;
		}
	}
	if (n.drop <= 0 || std::fabs(glm::determinant(a)) < 1e-12f) return false;
	// (down the torso's axis, in this model's own frame)
	const glm::vec3 d = glm::inverse(a) * (ta * (glm::vec3(n.axis == 0, n.axis == 1, n.axis == 2) * (-n.sign * n.drop)));
	off[0] = d.x, off[1] = d.y, off[2] = d.z;
	return true;
}

void gameplay_state::neck_settle()
{
	static const bool off = getenv("VF_NO_NECK_FIT") != nullptr;   // TESTONLY
	static const bool log = getenv("VF_NECKLOG") != nullptr;      // TESTONLY
	m_torso_seen = 0;
	for (int pl = 0; pl < 2; pl++) {
		neck_fit &n = m_neck[pl];
		float gap = 0;
		if (n.torso && n.head_seen && m_torso_poly[pl]) {
			// (the torso's top near the neck: its points within the same reach of its axis)
			const int k = n.axis, u = (k + 1) % 3, w = (k + 2) % 3;
			const float r = 0.33f * 0.5f * std::max(n.hi[u] - n.lo[u], n.hi[w] - n.lo[w]);
			const float cu = 0.5f * (n.lo[u] + n.hi[u]), cw = 0.5f * (n.lo[w] + n.hi[w]);
			float top = -1e30f;
			if (auto it = m_model_points.find(n.torso_poly); it != m_model_points.end())
				for (const auto &q : it->second)
					if ((q[u] - cu) * (q[u] - cu) + (q[w] - cw) * (q[w] - cw) <= r * r) top = std::max(top, n.sign * q[k]);
			// (tucked a little in: the top measured is the back's, the front's sits lower)
			const float tuck = 0.05f * (n.hi[k] - n.lo[k]);
			// (a neck's gap is a few hundredths of the torso: more, and that head isn't on this torso,
			// another picture's or the select's: left alone)
			if (top > -1e29f) gap = std::max(0.0f, n.head_low - (top - tuck));
			if (gap > 0.25f * (n.hi[k] - n.lo[k])) gap = 0;
			if (log && gap > 0) fprintf(stderr, "NECK pl%d gap %.3f (head %.3f top %.3f axis %d%c, torso height %.3f)\n", pl, gap, n.head_low, top, k, n.sign > 0 ? '+' : '-', n.hi[k] - n.lo[k]);
		}
		// (eased: the gap changes with the pose; a jump bobbed the head. Once a shown picture: a
		// redraw of it (a screenshot, the GPU's check) doesn't step it again. Settled to 0: no
		// denormal tail kept on every head's points)
		if (!m.redrawing()) n.drop = off ? 0 : n.drop + (gap - n.drop) * 0.35f;
		if (n.drop < 1e-6f) n.drop = 0;
		static const bool hlog = getenv("VF_HEADLOG") != nullptr;   // TESTONLY
		if (hlog && n.pitch_seen) {
			const uint16_t move = uint16_t(m.nvram()[0x6c8 + 0x1800 * pl] | m.nvram()[0x6c9 + 0x1800 * pl] << 8);
			fprintf(stderr, "HEAD f%llu pl%d move %04x lean %.1f r", (unsigned long long)m.frame_number(), pl, move, m_lean_turn[pl]);
			for (int k = 0; k < 9; k++) fprintf(stderr, " %.4f", n.rel[k]);
			fprintf(stderr, " v");
			for (int k = 0; k < 9; k++) fprintf(stderr, " %.4f", n.view[k]);
			fprintf(stderr, "\n");
		}
		n.pitch_seen = 0;
		n.head_seen = false;
		if (!m_torso_poly[pl]) n.torso = false;
	}
}

// TESTONLY (VF_DUMP_FIGHTER=frame[:file], the trailer's): at that frame, each body part player 1's
// record draws, once: its tag (the record's offset, h for the head's models, k for the hands'), its
// matrix as drawn, its own points and its quads (point indices, BGR555 colour), one JSON line each
void gameplay_state::dump_part(uint32_t tex_adr, uint32_t poly_adr, uint32_t size)
{
	static const char *spec = getenv("VF_DUMP_FIGHTER");
	if (!spec || m.nvram().size() < 0x1f00 || m.frame_number() != strtoull(spec, nullptr, 10)) return;
	static FILE *out = [] { const char *c = strchr(spec, ':'); return c ? fopen(c + 1, "w") : stderr; }();
	static std::set<uint32_t> done;
	auto rd = [&](const uint8_t *p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); };
	const auto &rom = m.program_rom();
	const uint32_t rec = rd(&m.nvram()[0x700 - 0x50]);
	if ((rec >> 16) != 0xfd || rec + RECORD_SIZE > rom.size()) return;
	auto poly_of = [&](uint32_t m) -> uint32_t { return (m >> 16) == 0x2d && m + 12 <= rom.size() ? rd(&rom[m + 8]) & 0x7fffff : 0; };
	std::string tag;
	for (uint32_t o = 0x12; o < 0x4e && tag.empty(); o += 4)
		if (poly_of(rd(&rom[rec + o])) == (poly_adr & 0x7fffff)) tag = std::to_string(o);
	if (const uint32_t head = rd(&rom[rec + 0x72]); tag.empty() && (head >> 16) == 0xfd)
		for (uint32_t k = 0; k < 121 && tag.empty(); k++) { const uint32_t m = poly_of(rd(&rom[head + 0x40 + 4 * k])); if (!m) break; if (m == (poly_adr & 0x7fffff)) tag = "h" + std::to_string(k); }
	if (tag.empty() && poly_of(rd(&rom[rec + 0x16])) == (poly_adr & 0x7fffff)) tag = "h16";
	for (uint32_t t : { 0x5eu, 0x62u })
		for (uint32_t k = 0, tb = rd(&rom[rec + t]); k < 32 && tag.empty() && (tb >> 16) == 0xfd && tb + 4 * k + 4 <= rom.size(); k++)
			if (poly_of(rd(&rom[tb + 4 * k])) == (poly_adr & 0x7fffff)) tag = "k" + std::to_string(t) + "_" + std::to_string(k);
	if (tag.empty() || !done.insert(poly_adr).second) return;
	const float *pd = m.polygon_data(poly_adr & 0x800000);
	uint32_t a = poly_adr & 0x7fffff;
	std::vector<std::array<float, 3>> pts{ { pd[a], pd[a + 1], pd[a + 2] }, { pd[a + 3], pd[a + 4], pd[a + 5] } };
	int o0 = 0, o1 = 1;
	std::string quads;
	a += 6;
	for (uint32_t i = 0; i < std::min<uint32_t>(size ? size : 0xffffffffu, 4096); i++, a += 10) {
		const uint32_t flags = *reinterpret_cast<const uint32_t *>(pd + a);
		const int type = flags & 3;
		if (!type) break;
		if (flags & 0x1000) tex_adr++;
		const int n0 = int(pts.size());
		pts.push_back({ pd[a + 4], pd[a + 5], pd[a + 6] });
		if (type == 2) pts.push_back(pts.back()); else pts.push_back({ pd[a + 7], pd[a + 8], pd[a + 9] });
		const int link = (flags >> 8) & 3;
		if (link) {
			const int col = tex_adr >= 0x40000 && tex_adr < 0x100000 ? m.palette_word(0x1000 | (m.texture_word(tex_adr - 0x40000) & 0x3ff)) : 0x7fff;
			char b[64];
			snprintf(b, sizeof b, "%s[%d,%d,%d,%d,%d]", quads.empty() ? "" : ",", o1, o0, n0, n0 + 1, col);
			quads += b;
		}
		switch (link) { case 0: case 2: o0 = n0; o1 = n0 + 1; break; case 1: o1 = n0; break; case 3: o0 = n0 + 1; break; }
	}
	const float *T = m.render_matrix();
	fprintf(out, "{\"rec\":\"%06x\",\"tag\":\"%s\",\"poly\":%u,\"m\":[", rec, tag.c_str(), poly_adr);
	for (int k = 0; k < 12; k++) fprintf(out, "%s%g", k ? "," : "", T[k]);
	fprintf(out, "],\"p\":[");
	for (size_t k = 0; k < pts.size(); k++) fprintf(out, "%s[%g,%g,%g]", k ? "," : "", pts[k][0], pts[k][1], pts[k][2]);
	fprintf(out, "],\"q\":[%s]}\n", quads.c_str());
	fflush(out);
}

int gameplay_state::model_owner(uint32_t poly_adr) const
{
	int pl = -1, owners = 0;
	for (int k = 0; k < 2; k++) {
		if (!m_build_models[k].count(poly_adr)) continue;
		owners++;
		if (pl >= 0 && m_neck[0].torso && m_neck[1].torso) {
			const float *T = m.render_matrix();
			auto d2 = [&](const neck_fit &f) { float s = 0; for (int i = 0; i < 3; i++) s += (T[9 + i] - f.t[i]) * (T[9 + i] - f.t[i]); return s; };
			pl = d2(m_neck[k]) < d2(m_neck[pl]) ? k : pl;
			if (const int l = m_owner_last; l >= 0 && d2(m_neck[l]) <= 4 * d2(m_neck[l ^ 1])) pl = l;
			continue;
		}
		if (pl < 0) pl = k;
	}
	if (owners < 2) m_owner_last = pl;   // (a model only one has, or scenery: whose run this is)
	return pl;
}

void configure_video(model1 &machine, const std::shared_ptr<gameplay_state> &state, gameplay_callbacks &callbacks)
{
	if (const char *wf = getenv("VF_WIREFRAME")) {
		const uint32_t colour = uint32_t(strtoul(wf, nullptr, 16)) | 0xff000000u;
		callbacks.wireframe = [colour](uint32_t object, uint32_t pen) -> std::optional<uint32_t> {
			if ((object & 0x7fffff) < VIDEO.scenery_polys_end || (!(pen & 0x80000000) && (pen & 0x01000000))) return std::nullopt;
			return colour;
		};
	}
	callbacks.render_begin = [state] { state->m_head_seen.clear(); state->neck_settle(); };
	callbacks.object = [state, &machine](uint32_t, uint32_t tex_adr, uint32_t poly_adr, uint32_t size, const float *poly_data) {
		game::object_effects effect;
		const bool scenery = poly_adr < VIDEO.scenery_polys_end;
		if ((getenv("VF_FIGHTERS_ONLY") && scenery) || (getenv("VF_STAGE_ONLY") && !scenery)) { effect.hidden = true; return effect; }
		state->dump_part(tex_adr, poly_adr | (poly_data == machine.polygon_data(true) ? 0x800000 : 0), size);
		// (Random fighter's round-1 build: gameplay_state::random_fighter_build)
		gameplay_state::build_at bat;
		const bool built = (state->m_build[0].start >= 0 || state->m_build[1].start >= 0) && state->build_info(poly_adr, bat);
		// (a random fighter's clothes: their turn round the colour wheel, below)
		float outfit = -1;
		int outfit_theme = 0;
		gameplay_state::outfit_tones *tones = nullptr;   // (black and white's, black and gold's split)
		int chrome_lm = -1;              // (>= 0: Dural's chrome, her light mode)
		uint32_t skin_rec = 0;           // (the random fighter whose model this is: its one skin, below)
		const gameplay_state::skin_sum *skin_ref = nullptr;
		bool skin_prev = false;          // (the skin of a record just gone (gameplay_state::build_prev): not measured further)
		gameplay_state::skin_sum scratch_skin;
		static const char *outfit_test = getenv("VF_OUTFIT");   // TESTONLY (VF_OUTFIT=x: every fighter's turn x; VF_OUTFIT=off: none)
		static const char *theme_test = getenv("VF_OUTFIT_THEME");   // TESTONLY (VF_OUTFIT_THEME=n: every fighter's theme n)
		// (none at a select: its portraits are heads, the last match's records' models in them (a challenger's
		// select) not those fighters')
		// (the old scene's last pictures, up to 3 frames after a record changes: the old fighters' models as
		// they were, those a new random fighter shares too (made of the cast's parts), so no part takes the
		// new fighter's colours for the old scene's last frames before the cut)
		gameplay_state::build_prev *old_scene = nullptr;
		if (state->m_random_fighter && !state->m_select_seen)
			for (gameplay_state::build_prev &p : state->m_build_prev)
				if (p.rec && int64_t(machine.frame_number()) < p.until - gameplay_state::BUILD_PREV_FRAMES + gameplay_state::BUILD_SCENE_LAG && p.models.count(poly_adr)) { old_scene = &p; break; }
		if (state->m_random_fighter && !state->m_random_outfit.empty() && !state->m_select_seen && !old_scene) {
			const int pl = state->model_owner(poly_adr);
			const uint32_t rec = pl >= 0 ? state->m_build[pl].rec : 0;
			static const bool skin_off = getenv("VF_SKIN_MATCH") && !strcmp(getenv("VF_SKIN_MATCH"), "off");   // TESTONLY
			if (rec && state->m_random_names.count(rec) && !skin_off) { skin_rec = rec; const auto it = state->m_skin_ref.find(rec); if (it != state->m_skin_ref.end()) skin_ref = &it->second; }
			// (Random opponents' last fight, a random fighter in Dural's record: all of it in her chrome, with her shine)
			if (rec == 0xfd369b && state->m_random_cpu && state->m_random_names.count(rec)) chrome_lm = state->dural_lightmode();
			if (chrome_lm < 0 && rec && state->m_random_names.count(rec) && !(outfit_test && !strcmp(outfit_test, "off")))
				if (const auto it = state->m_random_outfit.find(rec); it != state->m_random_outfit.end()) {
					outfit = outfit_test && *outfit_test ? float(atof(outfit_test)) : it->second;
					const auto th = state->m_random_theme.find(rec);
					outfit_theme = theme_test ? atoi(theme_test) : th != state->m_random_theme.end() ? th->second : 0;
					tones = &state->m_outfit_tones[rec];
				}
		}
		// (not a player's record's now: one just gone, as it was (gameplay_state::build_prev), for the old scene's pictures)
		if (state->m_random_fighter && outfit < 0 && !skin_rec && !state->m_select_seen)
			for (gameplay_state::build_prev &p : state->m_build_prev)
				if (old_scene ? &p == old_scene : p.until >= int64_t(machine.frame_number()) && p.models.count(poly_adr)) {
					static const bool skin_off = getenv("VF_SKIN_MATCH") && !strcmp(getenv("VF_SKIN_MATCH"), "off");   // TESTONLY
					if (!(outfit_test && !strcmp(outfit_test, "off")) && p.outfit >= 0) {
						outfit = outfit_test && *outfit_test ? float(atof(outfit_test)) : p.outfit;
						outfit_theme = theme_test ? atoi(theme_test) : p.theme;
						tones = &p.tones;
					}
					if (!skin_off && p.has_skin) { skin_rec = p.rec; skin_ref = &p.skin; skin_prev = true; }
					break;
				}
		// Cheats' random fighter: a head on another's body, turned about its own side axis (y) to sit
		// as on its own (gameplay_state::random_fighter_heads)
		float turn_c = 1, turn_s = 0, head_hue = -1, head_metal = -1, head_bright = 1;
		const gameplay_state::head_style *st = nullptr;
		if (!state->m_head_turn.empty())
			if (auto t = state->m_head_turn.find(poly_adr); t != state->m_head_turn.end()) {
				st = &t->second;
				if (auto t2 = state->m_head_turn2.find(poly_adr); t2 != state->m_head_turn2.end()) {
					// (a model both players wear: whose it is by the nearer torso, both seen; else by order,
					// player 1's drawn first. By order alone, some of a computer's face models took the
					// random fighter's neck fit and left a hole in its jaw)
					const int n = state->m_head_seen[poly_adr]++;
					bool second = n == 1;
					if (state->m_neck[0].torso && state->m_neck[1].torso) {
						const float *T = machine.render_matrix();
						auto d2 = [&](const gameplay_state::neck_fit &f) { float s = 0; for (int k = 0; k < 3; k++) s += (T[9 + k] - f.t[k]) * (T[9 + k] - f.t[k]); return s; };
						second = d2(state->m_neck[1]) < d2(state->m_neck[0]);
					}
					if (second) st = &t2->second;
				}
				turn_c = st->c, turn_s = st->s, head_hue = st->hue, head_metal = st->metal, head_bright = st->bright;
			}
		// a random fighter's torso noted, its head brought down onto it (neck_torso, neck_head)
		float neck_off[3] = { 0, 0, 0 };
		bool neck_move = false;
		if (state->m_torso_poly[0] || state->m_torso_poly[1]) {
			int tp = -1;
			if (poly_adr == state->m_torso_poly[0] && poly_adr == state->m_torso_poly[1]) tp = state->m_torso_seen++ == 0 ? 0 : 1;   // (the same torso on both: player 1's drawn first)
			else if (poly_adr == state->m_torso_poly[0]) tp = 0;
			else if (poly_adr == state->m_torso_poly[1]) tp = 1;
			if (tp >= 0) state->neck_torso(tp, poly_data, poly_adr);
		}
		if (st && st->pl >= 0) neck_move = state->neck_head(st->pl, st->strand, poly_data, poly_adr, turn_c, turn_s, neck_off);

		effect.hidden = built && bat.e < bat.appear;
		effect.lightmode = chrome_lm;
		if (turn_s || neck_move) effect.point = [=](float &x, float &y, float &z) {
			if (turn_s) { const float tx = turn_c * x + turn_s * z; z = turn_c * z - turn_s * x; x = tx; }
			if (neck_move) { x += neck_off[0]; y += neck_off[1]; z += neck_off[2]; }
		};
		if (turn_s) effect.normal = [=](float &x, float &, float &z) {
			const float tx = turn_c * x + turn_s * z; z = turn_c * z - turn_s * x; x = tx;
		};
		if (built) effect.polygon = [bat](int i) {
			using S = gameplay_state;
			if (i >= S::build_front(bat.e, bat.appear, S::BUILD_SPAN, bat.polys)) return -1;
			if (i >= S::build_front(bat.e - S::BUILD_FLASH, bat.appear, S::BUILD_SPAN, bat.polys)) return 2;
			if (i >= S::build_front(bat.e, bat.colour, S::BUILD_COLOUR_SPAN, bat.polys)) return 3;
			if (i >= S::build_front(bat.e - S::BUILD_FLASH, bat.colour, S::BUILD_COLOUR_SPAN, bat.polys)) return 2;
			return 0;
		};
		if (built || head_hue >= 0 || outfit >= 0 || skin_rec || chrome_lm >= 0)
			effect.colour = [=, &machine](uint32_t poly_adr, int build, int &r, int &g, int &b) mutable {
			// (Cheats' random fighter: its head's hair, hat and bands in its own hue, skin, eyes and mouth kept;
			// dark hair lightened enough to show it)
			bool recoloured = false;   // (by its hair's hue or its clothes' theme: not skin, whatever it's become)
			if (head_hue >= 0) {
				const float fr = r / 31.f, fg = g / 31.f, fb = b / 31.f;
				const float mx = std::max({ fr, fg, fb }), mn = std::min({ fr, fg, fb }), dl = mx - mn;
				float hh = 0, ss = mx > 0 ? dl / mx : 0, vv = mx;
				if (dl > 0) { hh = mx == fr ? (fg - fb) / dl : mx == fg ? 2 + (fb - fr) / dl : 4 + (fr - fg) / dl; hh /= 6; if (hh < 0) hh += 1; }
				const bool skin = hh > 0.015f && hh < 0.13f && ss > 0.15f && ss < 0.75f && vv > 0.45f;
				const bool eyes = ss < 0.2f && vv > 0.55f;                       // (eyes and teeth, white or light grey: Jeffry's are grey)
				// (mouths: a full red, Sarah's lips too. A browner red (Wolf's hair, Kage's band) is hair or a band, and
				// Pai's pink lips take the hue as well: counted as a mouth, Wolf's hair kept its orange in every hue)
				const bool mouth = ss > 0.9f && hh < 0.04f;
				if (head_metal >= 0) {   // (Dural's head: all of it in the metal's tint, its shading kept)
					hh = head_hue;
					ss = head_metal;
					vv *= head_bright;
				}
				if (head_metal >= 0 || (!skin && !eyes && !mouth)) {
					recoloured = true;
					if (head_metal < 0) {
						hh = head_hue;
						ss = std::max(ss, 0.6f);
						vv = std::max(vv, 0.3f);
					}
					const float c = vv * ss, x = c * (1 - std::fabs(std::fmod(hh * 6, 2.f) - 1)), m = vv - c;
					float rr = 0, gg = 0, bb = 0; const int sx = int(hh * 6) % 6;
					if (sx == 0) rr = c, gg = x; else if (sx == 1) rr = x, gg = c; else if (sx == 2) gg = c, bb = x; else if (sx == 3) gg = x, bb = c; else if (sx == 4) rr = x, bb = c; else rr = c, bb = x;
					r = std::clamp(int(std::lround((rr + m) * 31)), 0, 31); g = std::clamp(int(std::lround((gg + m) * 31)), 0, 31); b = std::clamp(int(std::lround((bb + m) * 31)), 0, 31);
				}
			}

			// (a random fighter's clothes in its theme (gameplay_state::random_fighter_colour): turned round the colour
			// wheel, one turn a fighter (its colours together, two-tone still two-tone); or into cool or warm
			// colours, two opposite colours, neon, pastel; or black and white, black and gold, split at the
			// middle of its own clothes' tones (its darkest and lightest seen), so two-tone whatever they were.
			// Its skin, greys, black and white as they are; a dark yellow or brown a turn makes (olive, khaki:
			// muddy) lifted to a clean gold or orange)
			if (outfit >= 0 && head_hue < 0) {
				const float fr = r / 31.f, fg = g / 31.f, fb = b / 31.f;
				const float mx = std::max({ fr, fg, fb }), mn = std::min({ fr, fg, fb }), dl = mx - mn;
				float hh = 0, ss = mx > 0 ? dl / mx : 0, vv = mx;
				if (dl > 0) { hh = mx == fr ? (fg - fb) / dl : mx == fg ? 2 + (fb - fr) / dl : 4 + (fr - fg) / dl; hh /= 6; if (hh < 0) hh += 1; }
				const bool skin = hh > 0.015f && hh < 0.13f && ss > 0.15f && ss < 0.75f && vv > 0.45f;
				if (!skin && ss > 0.28f && vv > 0.12f) {
					recoloured = true;
					const float h0 = hh;
					// (light or dark: by the median of its clothes' polygons, ordered by brightness and then hue (two
					// colours as bright as each other, Jacky's blue top and pink trousers, still split), so about
					// half of them each way; not the middle of the extremes, which a bright stripe pulled up)
					bool light = true;
					if (outfit_theme == gameplay_state::THEME_BW || outfit_theme == gameplay_state::THEME_BLACK_GOLD) {
						const int key = std::clamp(int(std::lround(vv * 31)), 0, 31) * 16 + std::clamp(int(h0 * 16), 0, 15);
						auto split = [&](uint64_t &total, uint64_t &below, int &med) {
							total = below = 0;
							for (uint32_t c : tones->n) total += c;
							for (med = 0; med < 32 * 16 - 1 && (below + tones->n[med]) * 2 < total; med++) below += tones->n[med];
						};
						// (the split once a frame from what's been seen (512 counts summed twice a polygon was
						// most of a two-tone fighter's cost); its first frame's polygons, nothing seen, as they come)
						if (tones->at != machine.frame_number()) { split(tones->total, tones->below, tones->med); tones->at = machine.frame_number(); }
						tones->n[key]++;
						uint64_t total = tones->total, below = tones->below; int med = tones->med;
						if (!total) split(total, below, med);
						light = key > med || (key == med && below * 2 >= total / 2);
					}
					switch (outfit_theme) {
					case gameplay_state::THEME_BW: ss = 0; vv = light ? 0.95f : 0.12f; break;
					case gameplay_state::THEME_BLACK_GOLD: if (!light) { ss = 0; vv = 0.12f; } else { hh = 0.125f; ss = 0.75f; vv = 0.92f; } break;
					case 3: hh = 0.47f + 0.3f * h0; ss = std::max(ss, 0.6f); break;                          // cool: teal, blue, violet
					case 4: hh = std::fmod(0.97f + 0.17f * h0, 1.0f); ss = std::max(ss, 0.65f); vv = std::max(vv, 0.45f); break;   // warm: red, orange, gold
					case 5: hh = std::fmod(outfit + (h0 < 0.5f ? 0 : 0.5f), 1.0f); ss = std::max(ss, 0.65f); break;   // two opposite colours
					case 6: hh = std::fmod(h0 + outfit, 1.0f); ss = 1; vv = std::max(vv, 0.9f); break;         // neon
					case 7: hh = std::fmod(h0 + outfit, 1.0f); ss = 0.32f; vv = std::max(vv, 0.85f); break;    // pastel
					default:
						hh = std::fmod(hh + outfit, 1.0f);
						if (hh > 0.07f && hh < 0.28f && vv < 0.7f) { vv = std::min(1.0f, vv * 0.4f + 0.6f); ss = std::max(ss, 0.7f); }
					}
					const float c = vv * ss, x = c * (1 - std::fabs(std::fmod(hh * 6, 2.f) - 1)), m = vv - c;
					float rr = 0, gg = 0, bb = 0; const int sx = int(hh * 6) % 6;
					if (sx == 0) rr = c, gg = x; else if (sx == 1) rr = x, gg = c; else if (sx == 2) gg = c, bb = x; else if (sx == 3) gg = x, bb = c; else if (sx == 4) rr = x, bb = c; else rr = c, bb = x;
					r = std::clamp(int(std::lround((rr + m) * 31)), 0, 31); g = std::clamp(int(std::lround((gg + m) * 31)), 0, 31); b = std::clamp(int(std::lround((bb + m) * 31)), 0, 31);
				}
			}

			// (one skin a fighter, its face's: a head from one character on another's body had a dark face
			// on light arms, or the other way. The face's skin measured as it's drawn (lips not), and the
			// body's skin given its hue and saturation, each part's own shading kept (its brightness moved
			// by the difference between its skin's average and the face's). Dural's head, metal: the body's
			// own skin; hair and clothes it recoloured never skin, though their new colour may look it: a warm
			// theme's, black and gold's gold, orange hair, made skin and measured as the face)
			if (skin_rec && head_metal < 0 && !recoloured) {
				const float fr = r / 31.f, fg = g / 31.f, fb = b / 31.f;
				const float mx = std::max({ fr, fg, fb }), mn = std::min({ fr, fg, fb }), dl = mx - mn;
				float hh = 0, ss = mx > 0 ? dl / mx : 0, vv = mx;
				if (dl > 0) { hh = mx == fr ? (fg - fb) / dl : mx == fg ? 2 + (fb - fr) / dl : 4 + (fr - fg) / dl; hh /= 6; if (hh < 0) hh += 1; }
				const bool lips = head_hue >= 0 && ss > 0.6f && hh < 0.04f;
				if (hh > 0.015f && hh < 0.13f && ss > 0.15f && ss < 0.8f && vv > 0.28f && !lips) {
					if (head_hue >= 0) {
						gameplay_state::skin_sum &ref = skin_prev ? scratch_skin : state->m_skin_ref[skin_rec];
						if (ref.n < 100000) { ref.h += hh; ref.s += ss; ref.v += vv; ref.n++; }
					} else if (skin_ref && skin_ref->n) {
						gameplay_state::skin_sum &part = state->m_skin_part[poly_adr];
						if (part.n < 100000) { part.h += hh; part.s += ss; part.v += vv; part.n++; }
						const gameplay_state::skin_sum &ref = *skin_ref;
						hh = float(ref.h / ref.n); ss = float(ref.s / ref.n);
						vv = std::clamp(vv + float(ref.v / ref.n - part.v / part.n), 0.05f, 1.0f);
						const float c = vv * ss, x = c * (1 - std::fabs(std::fmod(hh * 6, 2.f) - 1)), m = vv - c;
						float rr = 0, gg = 0, bb = 0; const int sx = int(hh * 6) % 6;
						if (sx == 0) rr = c, gg = x; else if (sx == 1) rr = x, gg = c; else if (sx == 2) gg = c, bb = x; else if (sx == 3) gg = x, bb = c; else if (sx == 4) rr = x, bb = c; else rr = c, bb = x;
						r = std::clamp(int(std::lround((rr + m) * 31)), 0, 31); g = std::clamp(int(std::lround((gg + m) * 31)), 0, 31); b = std::clamp(int(std::lround((bb + m) * 31)), 0, 31);
					}
				}
			}

			if (chrome_lm >= 0) {   // (Dural's chrome: grey, a little blue, its shading kept and lifted)
				const float v = std::max({ r, g, b }) / 31.f, vv = 0.8f + 0.2f * v;
				r = std::clamp(int(std::lround(vv * 0.93f * 31)), 0, 31);
				g = std::clamp(int(std::lround(vv * 0.96f * 31)), 0, 31);
				b = std::clamp(int(std::lround(vv * 31)), 0, 31);
			}
			if (build == 2) r = g = b = 31;   // (the build: a flash as a part goes on)
			else if (build == 3) { const int v = (r * 30 + g * 59 + b * 11) / 100; r = g = b = std::clamp(v / 2 + 9, 0, 31); }   // (grey till all are on)

			};
		return effect;
	};
}

}
