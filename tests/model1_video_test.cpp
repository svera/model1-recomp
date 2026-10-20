#include "../src/machine/model1.h"
#include "../src/games/vr/gameplay.h"
#include "../src/host/regions.h"
#include "../src/rt/native.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>

struct model1_video_test
{
	static void scenery_records()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		m.set_smooth(true);
		m.cpu().pc_ref() = 0xff8500;
		m.cpu().reg_ptr()[19] = 0x401700;
		m.cpu().reg_ptr()[25] = 0x40a300;
		m.cpu().reg_ptr()[31] = 0x500100;
		for (uint32_t caller : { 0xffddd4, 0xffddeb, 0xffde6e, 0xffdedc, 0xffdf42, 0xff8470 }) {
			m.write32(0x500100, caller);
			m.list_written(0, 8);
			assert(m.m_list_owner[0][8] == (caller == 0xff8470 ? 0x401700 : 0x40a300));
		}
		m.cpu().pc_ref() = 0xff85d8;
		m.list_written(1, 8);
		assert(m.m_list_owner[1][8] == 0x40a300);
		m.cpu().pc_ref() = 0xff8500;
		m.cpu().reg_ptr()[19] = 0x2e0000;
		m.list_written(0, 8);
		assert(m.m_list_owner[0][8] == 1);

		model1::logic_frame a, b;
		a.frame = 0;
		b.frame = 2;
		a.has_camera = b.has_camera = true;
		for (int i : { 0, 4, 8 }) a.camera[i] = b.camera[i] = 1;
		b.camera[9] = 1;
		model1::slot car, palm;
		car.owner = 0x401700;
		car.polys = { 0x13b908 };
		car.mat[11] = 20;
		palm.owner = 0x40a300;
		palm.polys = { 0x172c3f };
		palm.mat[9] = 10;
		palm.mat[11] = 20;
		a.slots = { car, palm };
		car.mat[9] = 6;
		palm.mat[9] = 11;
		palm.polys = { 0x172fa6 };
		model1::slot wheel = car;
		wheel.polys = { 0x128975 };
		b.slots = { car, palm, wheel };
		m.blend_slots(a, b, .5);
		assert(m.m_blend_ok[1]);
		assert(std::fabs(m.m_blend[1][9] - 10.5f) < .0001f);
		a.slots[1].owner = b.slots[1].owner = car.owner;
		m.blend_slots(a, b, .5);
		assert(std::fabs(m.m_blend[1][9] - 10.5f) > 2);
	}

	static void scenery_motion(const char *dir, bool extended)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		game::vr::set_extended_draw_distance(m, extended);
		m.set_smooth(true);
		m.reset();
		std::set<uint32_t> palm_records;
		int checked = 0, views = 0;
		m.on_instruction = [&](uint32_t pc) {
			if (pc != 0xff8500) return;
			const uint32_t poly = m.read32(m.cpu().reg(0));
			if (poly >= 0x16b000 && poly < 0x180000) palm_records.insert(m.cpu().reg(25));
		};
		for (int f = 1; f <= 2000; f++) {
			m.run_frame();
			const auto &a = m.m_logic[0], &b = m.m_logic[1];
			if (!a.has_camera || !b.has_camera || b.frame != f - 1) continue;
			m.blend_slots(a, b, .5);
			if (m.m_blend_cut) continue;
			bool palms = false;
			for (size_t i = 0; i < b.slots.size(); i++) {
				const auto &s = b.slots[i];
				if (s.polys.empty() || s.polys[0] < 0x16b000 || s.polys[0] >= 0x180000) continue;
				assert(palm_records.count(s.owner));
				palms = true;
				std::vector<size_t> old_parts, new_parts;
				for (size_t j = 0; j < a.slots.size(); j++)
					if (a.slots[j].owner == s.owner && !a.slots[j].polys.empty()) old_parts.push_back(j);
				for (size_t j = 0; j < b.slots.size(); j++)
					if (b.slots[j].owner == s.owner && !b.slots[j].polys.empty()) new_parts.push_back(j);
				if (old_parts.size() != new_parts.size()) continue;
				const auto part = std::find(new_parts.begin(), new_parts.end(), i) - new_parts.begin();
				const auto &old = a.slots[old_parts[part]];
				assert(m.m_blend_ok[i]);
				for (int k = 0; k < 12; k++)
					assert(std::fabs(m.m_blend_from[i][k] - old.mat[k]) < .0001f);
				checked++;
			}
			if (palms) views++;
		}
		assert(views > 100 && checked > 500 && palm_records.size() > 1);
		printf("VR palm motion (%s): %d views, %d parts paired to their own scenery records\n",
			extended ? "extended" : "arcade", views, checked);
	}

	static void foreground_order()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		std::array<model1::quad_t, 5> quads;
		quads[0].z = -1;
		quads[1].z = 10;
		quads[2].z = 0;
		quads[3].z = .1f;
		quads[3].foreground = true;
		quads[4].z = .2f;
		quads[4].foreground = true;
		assert(quads[1].compare(&quads[0]) < 0);
		assert(quads[2].compare(&quads[0]) < 0);
		for (int i = 0; i < 3; i++) {
			assert(quads[i].compare(&quads[3]) < 0);
			assert(quads[3].compare(&quads[i]) > 0);
		}

		assert(quads[4].compare(&quads[3]) < 0);
		std::array<model1::point_t, 4> points;
		m.m_quadpt = m.m_quaddb.get();
		m.m_pointpt = m.m_pointdb.get();
		m.m_view->set_viewport(248, 192, 0, 495, 0, 383);
		m.m_view->set_zoom(128, 128);
		points[0] = { -.2f, -.2f, 1 };
		points[1] = { .2f, -.2f, 1 };
		points[2] = { .2f, .2f, 1 };
		points[3] = { -.2f, .2f, -.1f };
		model1::quad_t q(0xffffff, -.1f, &points[0], &points[1], &points[2], &points[3]);
		q.foreground = true;
		m.fclip_push_quad(0, q);
		assert(m.m_quadpt > m.m_quaddb.get());
		for (auto *p = m.m_quaddb.get(); p < m.m_quadpt; p++)
			assert(p->foreground && p->z == -.1f);
	}

	static void native_motion(const char *dir)
	{
		model1 reference(load_regions(dir, game::id{ "vr" })), native(load_regions(dir, game::id{ "vr" }));
		rec::hints map;
		assert(map.read("assets/games/vr/vr_map.txt"));
		rec::attach(native.cpu(), "vr");
		assert(rec::build_native(native.program_rom(), map));
		native.run_cpu = [](int n) { rec::run(n); };
		for (auto *m : { &reference, &native }) {
			m->set_smooth(true);
			m->set_render_scale(2);
			m->set_aspect(16.0f / 9);
			m->reset();
		}
		int pictures = 0;
		for (int f = 1; f <= 1200; f++) {
			reference.run_frame();
			native.run_frame();
			for (int buffer = 0; buffer < 2; buffer++)
				assert(reference.m_list_owner[buffer] == native.m_list_owner[buffer]);
			if (f < 850 || f % 5) continue;
			for (double fraction : { .25, .75 }) {
				reference.render_smooth(double(reference.frame_number()) - fraction);
				native.render_smooth(double(native.frame_number()) - fraction);
				const size_t pixels = size_t(reference.frame_w()) * reference.frame_h();
				assert(std::equal(reference.frame().data(), reference.frame().data() + pixels, native.frame().data()));
				pictures++;
			}
		}
		assert(pictures > 100 && rec::fallback_count() == 0);
		printf("VR native palm motion: %d 2x widescreen pictures identical, zero fallback\n", pictures);
	}

	static void logo(const char *dir, bool smooth, bool extended, int scale = 1, float aspect = 4.0f / 3)
	{
		model1 m(load_regions(dir, game::id{ "vr" }));
		game::vr::set_extended_draw_distance(m, extended);
		m.set_render_scale(scale);
		m.set_aspect(aspect);
		m.set_smooth(smooth);
		m.reset();
		const auto *video = m.m_video;
		auto original_video = *video;
		original_video.foreground_objects.clear();
		size_t tested = 0, logo_pixels = 0, originally_hidden = 0;
		for (int f = 1; f <= 3600; f++) {
			m.run_frame();
			if (f < 1700 || (f > 1900 && f < 3350) || f % 5) continue;
			if (smooth) m.render_smooth(double(m.frame_number()) - 0.5);
			const size_t n = size_t(m.frame_w()) * m.frame_h();
			const std::vector<uint32_t> full(m.frame().data(), m.frame().data() + n);
			m.m_video = &original_video;
			m.redraw();
			const std::vector<uint32_t> baseline(m.frame().data(), m.frame().data() + n);
			m.m_video = video;
			auto original = m.m_gameplay.object;
			m.m_gameplay.object = [](uint32_t, uint32_t poly, uint32_t, const float *) {
				game::object_effects effect;
				effect.hidden = poly != 0x1031c7;
				return effect;
			};
			m.redraw();
			const std::vector<uint32_t> isolated(m.frame().data(), m.frame().data() + n);
			m.m_gameplay.object = [](uint32_t, uint32_t, uint32_t, const float *) {
				game::object_effects effect;
				effect.hidden = true;
				return effect;
			};
			m.redraw();
			size_t pixels = 0, hidden = 0;
			for (size_t i = 0; i < n; i++) {
				if (isolated[i] == m.frame().data()[i]) {
					assert(full[i] == baseline[i]);
					continue;
				}
				pixels++;
				if (baseline[i] != isolated[i]) originally_hidden++;
				if (full[i] != isolated[i]) {
					if (!hidden)
						fprintf(stderr, "First hidden logo pixel: %zu,%zu (%06x instead of %06x)\n",
							i % m.frame_w(), i / m.frame_w(), full[i], isolated[i]);
					hidden++;
				}
			}
			m.m_gameplay.object = std::move(original);
			m.redraw();
			if (hidden)
				fprintf(stderr, "VR SEGA logo: frame %d (%s, %s): %zu/%zu pixels hidden\n",
					f, smooth ? "smooth" : "arcade motion", extended ? "extended distance" : "arcade distance", hidden, pixels);
			assert(hidden == 0);
			tested++;
			logo_pixels += pixels;
		}
		assert(tested > 80 && logo_pixels > 10000);
		assert(originally_hidden > 100);
		printf("VR SEGA logo (%s, %s, %dx, %.3f): %zu views, %zu foreground pixels preserved (%zu previously hidden), other pixels unchanged\n",
			smooth ? "smooth" : "arcade motion", extended ? "extended distance" : "arcade distance",
			scale, aspect, tested, logo_pixels, originally_hidden);
	}
};

int main(int argc, char **argv)
{
	model1_video_test::foreground_order();
	model1_video_test::scenery_records();
	if (argc > 2 && std::string(argv[2]) == "--motion") {
		model1_video_test::scenery_motion(argv[1], false);
		model1_video_test::scenery_motion(argv[1], true);
		model1_video_test::native_motion(argv[1]);
		return 0;
	}
	if (argc > 1)
		for (bool smooth : { false, true })
			for (bool extended : { false, true })
				model1_video_test::logo(argv[1], smooth, extended);
	if (argc > 1) {
		model1_video_test::logo(argv[1], true, true, 2, 16.0f / 9);
		model1_video_test::logo(argv[1], false, false, 2);
		model1_video_test::scenery_motion(argv[1], false);
		model1_video_test::scenery_motion(argv[1], true);
		model1_video_test::native_motion(argv[1]);
	}
	puts("Model 1 video tests passed");
}
