#include "../src/machine/model1.h"
#include <cassert>
#include <cmath>

struct model1_sky_test
{
	static void near(float actual, float expected)
	{
		assert(std::fabs(actual - expected) < 0.0001f);
	}

	static void run()
	{
		model1::regions regions;
		regions.title = game::id{ "vr" };
		model1 m(std::move(regions));
		v60_bus &bus = m;
		auto pair = [&](uint16_t h, uint16_t v, uint16_t last_h, uint16_t last_v) {
			bus.write16(0x501408, h);
			bus.write16(0x50140a, v);
			bus.write16(0x501418, last_h);
			bus.write16(0x50141a, last_v);
		};
		pair(101, 0x2029, 100, 0x2028);
		auto scroll = m.sky_scroll_from_ram(true);
		assert(scroll.h == 100 && scroll.v == 0x2028);
		near(scroll.hx, 100.5f);
		near(scroll.vy, 40.5f);
		scroll = m.sky_scroll_from_ram(false);
		assert(scroll.h == 101 && scroll.v == 0x2029);
		near(scroll.hx, 101);
		near(scroll.vy, 41);
		bus.write16(0x501420, 4);
		assert(m.sky_scroll_from_ram(false).v == 0xa029);
		bus.write16(0x501420, 0);

		// The current/previous scrolls can change after vblank, before record_frame predicts
		// the next write. Its fractional endpoint must match the next frame's real write.
		auto write_scroll = [&](bool between) {
			bus.write8(0x500501, between ? 0 : 1);
			const auto s = m.sky_scroll_from_ram(between);
			bus.write16(0x70a004, s.h);
			bus.write16(0x70a00c, s.v);
		};
		write_scroll(false);
		pair(102, 0x202a, 101, 0x2029);
		m.record_frame();
		const auto first = m.m_sky_hist[0];
		assert(first.next_h && !first.next_v);
		near(first.hx, 101);
		near(first.nx, 101.5f);
		near(first.ny, 41.5f);
		m.m_frame = 1;
		write_scroll(true);
		m.record_frame();
		const auto second = m.m_sky_hist[1];
		assert(second.next_h && !second.next_v);
		near(second.hx, first.nx);
		near(second.vy, first.ny);
		near(second.nx, 102);
		near(second.ny, 42);

		m.m_frame = 2;
		m.m_replaying = true;
		float dx, dy;
		for (int k = 0; k <= 4; k++) {
			m.m_draw_time = -1 + k / 4.0;
			m.smooth_sky_shift(dx, dy);
			near(dx, -0.5f - k / 8.0f);
			near(dy, 0.5f + k / 8.0f);
		}

		// A horizontal ring crossing is a small move, not a camera cut.
		pair(0, 0x2000, 1023, 0x23ff);
		scroll = m.sky_scroll_from_ram(true);
		near(scroll.hx, 511.5f);
		near(scroll.vy, 511.5f);
		m.m_sky_hist[1] = { 1, 511, 0, 511.5f, 0, 0, 0, true, true };
		m.m_draw_time = -0.5;
		m.m_tiles.tile_w(0x5002, 511, 0xffff);
		m.m_tiles.tile_w(0x5006, 0, 0xffff);
		m.smooth_sky_shift(dx, dy);
		near(dx, -0.75f);
		near(dy, 0);

		// Unrelated writes do not acquire fractions, and cuts are never interpolated.
		bus.write16(0x70a004, 123);
		assert(!m.m_sky_read_h);
		near(m.m_sky_frac_h, 0);
		m.m_sky_hist[1] = { 1, 123, 0, 123, 0, 223, 0, true, true };
		m.smooth_sky_shift(dx, dy);
		near(dx, 0);
		m.m_sky_hist[1].frame = -1;
		m.smooth_sky_shift(dx, dy);
		near(dx, 0);
		m.m_replaying = false;
		m.smooth_sky_shift(dx, dy);
		near(dx, 0);
		near(dy, 0);

		// The default camera layout still follows VF's original 1/32-pixel formula.
		m.m_video = &game::get_video(game::id{ "vf" });
		bus.write16(0x508004, 100 * 32 + 7);
		bus.write16(0x508008, 100 * 32 + 3);
		bus.write16(0x508006, 20);
		bus.write16(0x50800a, 18);
		scroll = m.sky_scroll_from_ram(true);
		assert(scroll.h == 100 && scroll.v == 51);
		near(scroll.hx, 100 + 5 / 32.0f);
		near(scroll.vy, 51.5f);

		// Both composition paths must place the moving tile pixels identically, including
		// a stationary fraction and a stretched widescreen backdrop.
		m.m_video = &game::get_video(game::id{ "vr" });
		m.m_replaying = true;
		m.set_render_scale(4);
		for (bool wide : { false, true }) {
			m.set_aspect(wide ? 16.0f / 9 : 4.0f / 3);
			m.m_wide_frame = wide;
			m.m_tile_tmp.allocate(model1::SCREEN_W + 2 * m.m_ext, model1::SCREEN_H);
			for (int y = 0; y < model1::SCREEN_H; y++)
				for (int x = 0; x < m.m_tile_tmp.width(); x++)
					m.m_tile_tmp.pix(y, x) = uint32_t((x & 255) << 16 | (y & 255) << 8 | ((x + y) & 255));
			m.m_l4_frame = m.m_frame;
			m.m_l4_ext = m.m_ext;
			m.m_l4_coords = false;
			m.m_l4_gen++;
			m.m_tiles.tile_w(0x5002, 100, 0xffff);
			m.m_tiles.tile_w(0x5006, 40, 0xffff);
			bitmap_rgb32 cpu;
			cpu.allocate(m.frame_w(), m.frame_h());
			for (int direction : { -1, 0, 1 })
				for (int k = 0; k < 4; k++) {
					m.m_sky_hist[1] = { 1, 100, 40, 100, 40, 100 + direction * 0.5f, 40 + direction * 0.5f, true, true };
					m.m_draw_time = -1 + k / 4.0;
					cpu.fill(0);
					m.draw_tiles(cpu, cpu.cliprect(), 4, segas24_tile::DRAW_OPAQUE);
					model1::gpu_parts parts;
					m.m_parts = &parts;
					m.draw_tiles(m.m_bitmap, m.m_bitmap.cliprect(), 4, segas24_tile::DRAW_OPAQUE);
					m.m_parts = nullptr;
					std::vector<uint32_t> gpu;
					model1::compose_cpu(parts, cpu.width(), cpu.height(), gpu);
					assert(std::equal(gpu.begin(), gpu.end(), cpu.data()));
				}
		}
		m.set_aspect(4.0f / 3);
		m.set_render_scale(1);
		m.m_draw_time = -0.5;
		m.smooth_sky_shift(dx, dy);
		assert(dx != 0 && dy != 0);
		m.draw_tiles(m.m_bitmap, m.m_bitmap.cliprect(), 4, segas24_tile::DRAW_OPAQUE);
		const std::vector<uint32_t> arcade(m.frame().data(), m.frame().data() + model1::SCREEN_W * model1::SCREEN_H);
		m.m_replaying = false;
		m.draw_tiles(m.m_bitmap, m.m_bitmap.cliprect(), 4, segas24_tile::DRAW_OPAQUE);
		assert(std::equal(arcade.begin(), arcade.end(), m.frame().data()));
	}
};

int main()
{
	model1_sky_test::run();
}
