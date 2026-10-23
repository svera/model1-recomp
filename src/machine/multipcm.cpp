// Yamaha YMW-258-F "GEW8" / Sega 315-5560 MultiPCM, the Model 1 sound board's sample chips.
// MAME's gew.cpp and multipcm.cpp (BSD-3-Clause, copyright-holders: Miguel Angel Horna),
// without the device framework.
#include "multipcm.h"
#include <cmath>

#define M1_LOG(...) ((void)0)


/*******************************
        ENVELOPE SECTION
*******************************/

// Times are based on a 44100Hz timebase. It's adjusted to the actual sampling rate on startup

const double gew_pcm_device::BASE_TIMES[64] = {
	0,          0,          0,          0,
	6222.95,    4978.37,    4148.66,    3556.01,
	3111.47,    2489.21,    2074.33,    1778.00,
	1555.74,    1244.63,    1037.19,    889.02,
	777.87,     622.31,     518.59,     444.54,
	388.93,     311.16,     259.32,     222.27,
	194.47,     155.60,     129.66,     111.16,
	97.23,      77.82,      64.85,      55.60,
	48.62,      38.91,      32.43,      27.80,
	24.31,      19.46,      16.24,      13.92,
	12.15,      9.75,       8.12,       6.98,
	6.08,       4.90,       4.08,       3.49,
	3.04,       2.49,       2.13,       1.90,
	1.72,       1.41,       1.18,       1.04,
	0.91,       0.73,       0.59,       0.50,
	0.45,       0.45,       0.45,       0.45
};

constexpr uint32_t gew_pcm_device::TL_SHIFT;
constexpr uint32_t gew_pcm_device::EG_SHIFT;

void gew_pcm_device::retrigger_sample(slot_t &slot)
{
	slot.m_offset = 0;
	slot.m_prev_sample = 0;
	slot.m_total_level = slot.m_dest_total_level << TL_SHIFT;

	envelope_generator_calc(slot);
	slot.m_envelope_gen.m_state = state_t::ATTACK;
	slot.m_envelope_gen.m_volume = (0x3ff - 0x2a0) << EG_SHIFT;

#if MULTIPCM_LOG_SAMPLES
	dump_sample(slot);
#endif
}

void  gew_pcm_device::update_step(slot_t &slot)
{
	const uint8_t oct = (slot.m_octave - 1) & 0xf;
	uint32_t pitch = m_freq_step_table[slot.m_pitch];
	if (oct & 0x8)
	{
		pitch >>= (16 - oct);
	}
	else
	{
		pitch <<= oct;
	}
	slot.m_step = pitch / m_rate;
}

void gew_pcm_device::envelope_generator_init(const double (&rates)[64], double attack_decay_ratio)
{
	for (int32_t i = 4; i < 0x40; ++i)
	{
		// Times are based on 44100Hz clock, adjust to real chip clock
		m_attack_step[i] = (float)(0x400 << EG_SHIFT) / (float)(rates[i] * 44100.0 / 1000.0);
		m_decay_release_step[i] = (float)(0x400 << EG_SHIFT) / (float)(rates[i] * attack_decay_ratio * 44100.0 / 1000.0);
	}
	m_attack_step[0] = m_attack_step[1] = m_attack_step[2] = m_attack_step[3] = 0;
	m_attack_step[0x3f] = 0x400 << EG_SHIFT;
	m_decay_release_step[0] = m_decay_release_step[1] = m_decay_release_step[2] = m_decay_release_step[3] = 0;
}

int32_t gew_pcm_device::envelope_generator_update(slot_t &slot)
{
	switch (slot.m_envelope_gen.m_state)
	{
	case state_t::ATTACK:
		slot.m_envelope_gen.m_volume += (int64_t((0x817 << (EG_SHIFT - 1)) - slot.m_envelope_gen.m_volume) * slot.m_envelope_gen.m_attack_rate) >> 24;
		if (slot.m_envelope_gen.m_volume >= (0x3ff << EG_SHIFT))
		{
			slot.m_envelope_gen.m_state = state_t::DECAY1;
			if (slot.m_envelope_gen.m_decay1_rate >= (0x400 << EG_SHIFT)) //Skip DECAY1, go directly to DECAY2
			{
				slot.m_envelope_gen.m_state = state_t::DECAY2;
			}
			slot.m_envelope_gen.m_volume = 0x3ff << EG_SHIFT;
		}
		break;
	case state_t::DECAY1:
		slot.m_envelope_gen.m_volume -= slot.m_envelope_gen.m_decay1_rate;
		if (slot.m_envelope_gen.m_volume <= 0)
		{
			slot.m_envelope_gen.m_volume = 0;
		}
		if (slot.m_envelope_gen.m_volume >> (EG_SHIFT + 6) <= slot.m_envelope_gen.m_decay_level)
		{
			slot.m_envelope_gen.m_state = state_t::DECAY2;
		}
		break;
	case state_t::DECAY2:
		slot.m_envelope_gen.m_volume -= slot.m_envelope_gen.m_decay2_rate;
		if (slot.m_envelope_gen.m_volume <= 0)
		{
			slot.m_envelope_gen.m_volume = 0;
		}
		break;
	case state_t::RELEASE:
		slot.m_envelope_gen.m_volume -= slot.m_envelope_gen.m_release_rate;
		if (slot.m_envelope_gen.m_volume <= 0)
		{
			slot.m_envelope_gen.m_volume = 0;
			slot.m_playing = false;
		}
		break;
	default:
		return 1 << TL_SHIFT;
	}

	// TODO: this is currently only implemented for GEW7, it's probably not accurate
	if (slot.m_envelope_gen.m_reverb && slot.m_envelope_gen.m_state != state_t::ATTACK
		&& (slot.m_envelope_gen.m_volume >> EG_SHIFT) <= 0x300)
	{
		slot.m_envelope_gen.m_decay1_rate  = m_decay_release_step[17];
		slot.m_envelope_gen.m_decay2_rate  = m_decay_release_step[17];
		slot.m_envelope_gen.m_release_rate = m_decay_release_step[17];
	}

	return m_linear_to_exp_volume[slot.m_envelope_gen.m_volume >> EG_SHIFT];
}

uint32_t gew_pcm_device::get_rate(uint32_t *steps, int32_t rate, uint32_t val)
{
	if (val == 0)
	{
		return steps[0];
	}
	if (val == 0xf)
	{
		return steps[0x3f];
	}

	const int r = std::clamp(4 * (int)val + rate, 0, 0x3f);
	return steps[r];
}

void gew_pcm_device::envelope_generator_calc(slot_t &slot)
{
	int32_t octave = slot.m_octave;
	if (octave & 8) {
		octave = octave - 16;
	}

	int32_t rate;
	if (slot.m_sample.m_key_rate_scale != 0xf)
	{
		rate = (octave + slot.m_sample.m_key_rate_scale) * 2 + BIT(slot.m_pitch, 9);
	}
	else
	{
		rate = 0;
	}

	slot.m_envelope_gen.m_attack_rate = get_rate(m_attack_step.get(), rate, slot.m_sample.m_attack_reg);
	slot.m_envelope_gen.m_decay1_rate = get_rate(m_decay_release_step.get(), rate, slot.m_sample.m_decay1_reg);
	slot.m_envelope_gen.m_decay2_rate = get_rate(m_decay_release_step.get(), rate, slot.m_sample.m_decay2_reg);
	slot.m_envelope_gen.m_release_rate = get_rate(m_decay_release_step.get(), rate, slot.m_sample.m_release_reg);
	slot.m_envelope_gen.m_decay_level = 0xf - slot.m_sample.m_decay_level;
	slot.m_envelope_gen.m_reverb = false;
}

/*****************************
        LFO  SECTION
*****************************/

constexpr uint32_t gew_pcm_device::LFO_SHIFT;

const float gew_pcm_device::LFO_FREQ[8] = // In Hertz
{
	0.168f,
	2.019f,
	3.196f,
	4.206f,
	5.215f,
	5.888f,
	6.224f,
	7.066f
};

const float gew_pcm_device::PHASE_SCALE_LIMIT[8] = // In Cents
{
	0.0f,
	3.378f,
	5.065f,
	6.750f,
	10.114f,
	20.170f,
	40.180f,
	79.307f
};

const float gew_pcm_device::AMPLITUDE_SCALE_LIMIT[8] = // In Decibels
{
	0.0f,
	0.4f,
	0.8f,
	1.5f,
	3.0f,
	6.0f,
	12.0f,
	24.0f
};

// effect send level, 32 = unity. Steps of 3dB like the pan levels. (values above 8 not verified)
const uint8_t gew_pcm_device::DSP_SEND_LEVEL[16] =
{
	0, 3, 4, 6, 8, 12, 16, 24, 32, 32, 32, 32, 32, 32, 32, 32
};

void gew_pcm_device::lfo_init()
{
	m_pitch_table = make_unique_clear<int32_t[]>(256);
	m_amplitude_table = make_unique_clear<int32_t[]>(256);
	for (int32_t i = 0; i < 256; ++i)
	{
		if (i < 64)
		{
			m_pitch_table[i] = i * 2 + 128;
		}
		else if (i < 128)
		{
			m_pitch_table[i] = 383 - i * 2;
		}
		else if (i < 192)
		{
			m_pitch_table[i] = 384 - i * 2;
		}
		else
		{
			m_pitch_table[i] = i * 2 - 383;
		}

		if (i < 128)
		{
			m_amplitude_table[i] = 255 - (i * 2);
		}
		else
		{
			m_amplitude_table[i] = (i * 2) - 256;
		}
	}

	for (int32_t table = 0; table < 8; ++table)
	{
		float limit = PHASE_SCALE_LIMIT[table];
		m_pitch_scale_tables[table] = make_unique_clear<int32_t[]>(256);
		for (int32_t i = -128; i < 128; ++i)
		{
			const float value = (limit * (float)i) / 128.0f;
			const float converted = powf(2.0f, value / 1200.0f);
			m_pitch_scale_tables[table][i + 128] = value_to_fixed(LFO_SHIFT, converted);
		}

		limit = -AMPLITUDE_SCALE_LIMIT[table];
		m_amplitude_scale_tables[table] = make_unique_clear<int32_t[]>(256);
		for (int32_t i = 0; i < 256; ++i)
		{
			const float value = (limit * (float)i) / 256.0f;
			const float converted = powf(10.0f, value / 20.0f);
			m_amplitude_scale_tables[table][i] = value_to_fixed(LFO_SHIFT, converted);
		}
	}
}

uint32_t gew_pcm_device::value_to_fixed(const uint32_t bits, const float value)
{
	const float float_shift = float(1 << bits);
	return uint32_t(float_shift * value);
}

int32_t gew_pcm_device::pitch_lfo_step(lfo_t &lfo)
{
	lfo.m_phase += lfo.m_phase_step;
	int32_t p = lfo.m_table[(lfo.m_phase >> LFO_SHIFT) & 0xff];
	p = lfo.m_scale[p];
	return p << (TL_SHIFT - LFO_SHIFT);
}

int32_t gew_pcm_device::amplitude_lfo_step(lfo_t &lfo)
{
	lfo.m_phase += lfo.m_phase_step;
	int32_t p = lfo.m_table[(lfo.m_phase >> LFO_SHIFT) & 0xff];
	p = lfo.m_scale[p];
	return p << (TL_SHIFT - LFO_SHIFT);
}

void gew_pcm_device::lfo_compute_step(lfo_t &lfo, uint32_t lfo_frequency, uint32_t lfo_scale, int32_t amplitude_lfo)
{
	float step = (float)LFO_FREQ[lfo_frequency] * 256.0f / (float)m_rate;
	lfo.m_phase_step = uint32_t(float(1 << LFO_SHIFT) * step);
	if (amplitude_lfo)
	{
		lfo.m_table = m_amplitude_table.get();
		lfo.m_scale = m_amplitude_scale_tables[lfo_scale].get();
	}
	else
	{
		lfo.m_table = m_pitch_table.get();
		lfo.m_scale = m_pitch_scale_tables[lfo_scale].get();
	}
}

/* MAME access functions */



//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void gew_pcm_device::start()
{
	m_rate = (float)m_clock / m_clock_divider;

	// Volume + pan table
	m_left_pan_table = make_unique_clear<int32_t[]>(0x800);
	m_right_pan_table = make_unique_clear<int32_t[]>(0x800);
	for (int32_t level = 0; level < 0x80; ++level)
	{
		const float vol_db = (float)level * (-24.0f) / 64.0f;
		const float total_level = powf(10.0f, vol_db / 20.0f) / 4.0f;

		for (int32_t pan = 0; pan < 0x10; ++pan)
		{
			float pan_left, pan_right;
			if (pan == 0x8)
			{
				pan_left = 0.0;
				pan_right = 0.0;
			}
			else if (pan == 0x0)
			{
				pan_left = 1.0;
				pan_right = 1.0;
			}
			else if (pan & 0x8)
			{
				pan_left = 1.0;

				const int32_t inverted_pan = 0x10 - pan;
				const float pan_vol_db = (float)inverted_pan * (-12.0f) / 4.0f;

				pan_right = pow(10.0f, pan_vol_db / 20.0f);

				if ((inverted_pan & 0x7) == 7)
				{
					pan_right = 0.0;
				}
			}
			else
			{
				pan_right = 1.0;

				const float pan_vol_db = (float)pan * (-12.0f) / 4.0f;

				pan_left = pow(10.0f, pan_vol_db / 20.0f);

				if ((pan & 0x7) == 7)
				{
					pan_left = 0.0;
				}
			}

			m_left_pan_table[(pan << 7) | level] = value_to_fixed(TL_SHIFT, pan_left * total_level);
			m_right_pan_table[(pan << 7) | level] = value_to_fixed(TL_SHIFT, pan_right * total_level);
		}
	}

	// Pitch steps
	m_freq_step_table = make_unique_clear<uint32_t[]>(0x400);
	for (int32_t i = 0; i < 0x400; ++i)
	{
		const float fcent = m_rate * (1024.0f + (float)i) / 1024.0f;
		m_freq_step_table[i] = value_to_fixed(TL_SHIFT, fcent);
	}

	// Envelope steps
	m_attack_step = make_unique_clear<uint32_t[]>(0x40);
	m_decay_release_step = make_unique_clear<uint32_t[]>(0x40);
	envelope_generator_init(BASE_TIMES, 14.32833);

	// Total level interpolation steps
	m_total_level_steps = make_unique_clear<int32_t[]>(2);
	m_total_level_steps[0] = -(float)(0x80 << TL_SHIFT) / (78.2f * 44100.0f / 1000.0f); // lower
	m_total_level_steps[1] = (float)(0x80 << TL_SHIFT) / (78.2f * 2 * 44100.0f / 1000.0f); // raise

	// build the linear->exponential ramps
	m_linear_to_exp_volume = make_unique_clear<int32_t[]>(0x400);
	for (int32_t i = 0; i < 0x400; ++i)
	{
		const float db = -(96.0f - (96.0f * (float)i / (float)0x400));
		const float exp_volume = powf(10.0f, db / 20.0f);
		m_linear_to_exp_volume[i] = value_to_fixed(TL_SHIFT, exp_volume);
	}

	// Slots
	m_slots = std::make_unique<slot_t[]>(m_voices);


	for (int32_t slot = 0; slot < m_voices; ++slot)
	{


	}

	lfo_init();
}

void gew_pcm_device::reset()
{
	for (int32_t slot = 0; slot < m_voices; ++slot)
	{
		m_slots[slot].m_playing = false;
	}
}

//-------------------------------------------------
//  sound_stream_update - handle a stream update
//-------------------------------------------------

void gew_pcm_device::generate(int16_t *outl, int16_t *outr, int samples)
{
	for (int32_t i = 0; i < samples; ++i)
	{
		int32_t smpl = 0;
		int32_t smpr = 0;
		int32_t send = 0;
		for (int32_t sl = 0; sl < m_voices; ++sl)
		{
			slot_t& slot = m_slots[sl];
			if (slot.m_playing)
			{
				uint32_t vol = (slot.m_total_level >> TL_SHIFT) | (slot.m_pan << 7);
				uint32_t spos = slot.m_offset >> TL_SHIFT;
				uint32_t step = slot.m_step;
				int32_t csample = 0;
				int32_t fpart = slot.m_offset & ((1 << TL_SHIFT) - 1);

				if (slot.m_reverse)
				{
					spos = slot.m_sample.m_end - spos - 1;
				}

				if (slot.m_sample.m_format & 4)  // 12-bit linear
				{
					offs_t adr = slot.m_sample.m_start + (spos >> 1) * 3;
					if (!(spos & 1))
					{ // ab.c ..
						s16 w0 = read_byte(adr) << 8 | ((read_byte(adr + 1) & 0xf) << 4);
						csample = w0;
					}
					else
					{ // ..C. AB
						s16 w0 = (read_byte(adr + 2) << 8) | (read_byte(adr + 1) & 0xf0);
						csample = w0;
					}
				}
				else
				{
					csample = (int16_t)(read_byte(slot.m_sample.m_start + spos) << 8);
				}

				int32_t sample = (csample * fpart + slot.m_prev_sample * ((1 << TL_SHIFT) - fpart)) >> TL_SHIFT;

				if (slot.m_vibrato) // Vibrato enabled
				{
					step = step * pitch_lfo_step(slot.m_pitch_lfo);
					step >>= TL_SHIFT;
				}

				slot.m_offset += step;

				if (spos ^ (slot.m_offset >> TL_SHIFT))
				{
					slot.m_prev_sample = csample;
				}

				if (slot.m_offset >= (slot.m_sample.m_end << TL_SHIFT))
				{
					slot.m_offset -= (slot.m_sample.m_end - slot.m_sample.m_loop) << TL_SHIFT;
					// DD-9 expects the looped silence at the end of some samples to be the same whether reversed or not
					slot.m_reverse = false;
				}

				if ((slot.m_total_level >> TL_SHIFT) != slot.m_dest_total_level)
				{
					slot.m_total_level += slot.m_total_level_step;
				}

				if (slot.m_tremolo) // Tremolo enabled
				{
					sample = sample * amplitude_lfo_step(slot.m_amplitude_lfo);
					sample >>= TL_SHIFT;
				}

				sample = (sample * envelope_generator_update(slot)) >> 10;

				smpl += (m_left_pan_table[vol] * sample) >> TL_SHIFT;
				smpr += (m_right_pan_table[vol] * sample) >> TL_SHIFT;

				// the effect send is mono and taken before the pan
				if (slot.m_dsp_send)
					send += (((m_left_pan_table[vol & 0x7f] * sample) >> TL_SHIFT) * DSP_SEND_LEVEL[slot.m_dsp_send]) >> 5;
			}
		}

		outl[i] = int16_t(std::clamp(smpl, -32768, 32767));
		outr[i] = int16_t(std::clamp(smpr, -32768, 32767));
		(void)send;
	}
}



const int32_t multipcm_device::VALUE_TO_CHANNEL[32] =
{
	0, 1, 2, 3, 4, 5, 6 , -1,
	7, 8, 9, 10,11,12,13, -1,
	14,15,16,17,18,19,20, -1,
	21,22,23,24,25,26,27, -1,
};

void multipcm_device::init_sample(sample_t &sample, uint32_t index)
{
	uint32_t address = index * 12;

	sample.m_start = (read_byte(address) << 16) | (read_byte(address + 1) << 8) | read_byte(address + 2);
	sample.m_format = (sample.m_start>>20) & 0xfe;
	sample.m_start &= 0x3fffff;
	sample.m_loop = (read_byte(address + 3) << 8) | read_byte(address + 4);
	sample.m_end = 0x10000 - ((read_byte(address + 5) << 8) | read_byte(address + 6));
	sample.m_attack_reg = (read_byte(address + 8) >> 4) & 0xf;
	sample.m_decay1_reg = read_byte(address + 8) & 0xf;
	sample.m_decay2_reg = read_byte(address + 9) & 0xf;
	sample.m_decay_level = (read_byte(address + 9) >> 4) & 0xf;
	sample.m_release_reg = read_byte(address + 10) & 0xf;
	sample.m_key_rate_scale = (read_byte(address + 10) >> 4) & 0xf;
	sample.m_lfo_vibrato_reg = read_byte(address + 7);
	sample.m_lfo_amplitude_reg = read_byte(address + 11) & 0xf;
}

void multipcm_device::write_slot(slot_t &slot, int32_t reg, uint8_t data)
{
	slot.m_regs[reg] = data;

	switch(reg)
	{
		case 0: // PANPOT
			slot.m_pan = (data >> 4) & 0xf;
			slot.m_dsp_send = data & 0xf;
			break;

		case 1: // Sample
		{
			// according to YMF278 sample write causes some base params written to the regs (envelope+lfos)
			init_sample(slot.m_sample, slot.m_regs[1] | ((slot.m_regs[2] & 1) << 8));
			slot.m_regs[7] = (slot.m_sample.m_attack_reg << 4) | slot.m_sample.m_decay1_reg;
			slot.m_regs[8] = (slot.m_sample.m_decay_level << 4) | slot.m_sample.m_decay2_reg;
			slot.m_regs[9] = (slot.m_sample.m_key_rate_scale << 4) | slot.m_sample.m_release_reg;
			write_slot(slot, 6, slot.m_sample.m_lfo_vibrato_reg);
			write_slot(slot, 10, slot.m_sample.m_lfo_amplitude_reg);

			// retrigger if key is on
			if (slot.m_playing)
				retrigger_sample(slot);

			break;
		}
		case 2: // Pitch
		case 3:
			{
				slot.m_octave = slot.m_regs[3] >> 4;
				slot.m_pitch = ((slot.m_regs[3] & 0xf) << 6) | (slot.m_regs[2] >> 2);
				update_step(slot);
			}
			break;
		case 4: // KeyOn/Off
			if (data & 0x80) // KeyOn
			{
				slot.m_playing = true;
				retrigger_sample(slot);
			}
			else
			{
				if (slot.m_playing)
				{
					if (slot.m_sample.m_release_reg != 0xf)
					{
						slot.m_envelope_gen.m_state = state_t::RELEASE;
					}
					else
					{
						slot.m_playing = false;
					}
				}
			}
			break;
		case 5: // TL + Interpolation
			slot.m_dest_total_level = (data >> 1) & 0x7f;
			if (!(data & 1)) // Interpolate TL
			{
				if ((slot.m_total_level >> TL_SHIFT) > slot.m_dest_total_level)
				{
					slot.m_total_level_step = m_total_level_steps[0]; // decrease
				}
				else
				{
					slot.m_total_level_step = m_total_level_steps[1]; // increase
				}
			}
			else
			{
				slot.m_total_level = slot.m_dest_total_level << TL_SHIFT;
			}
			break;
		case 6: // LFO frequency + Pitch LFO
		case 10: // Amplitude LFO
			slot.m_lfo_frequency = (slot.m_regs[6] >> 3) & 7;
			slot.m_vibrato = slot.m_regs[6] & 7;
			slot.m_tremolo = slot.m_regs[10] & 7;
			if (data)
			{
				lfo_compute_step(slot.m_pitch_lfo, slot.m_lfo_frequency, slot.m_vibrato, 0);
				lfo_compute_step(slot.m_amplitude_lfo, slot.m_lfo_frequency, slot.m_tremolo, 1);
			}
			break;
		case 7: // Attack rate + Decay 1 rate
		case 8: // Decay level + Decay 2 rate
		case 9: // Rate correction + Release rate
			slot.m_sample.m_attack_reg = slot.m_regs[7] >> 4;
			slot.m_sample.m_decay1_reg = slot.m_regs[7] & 0xf;
			slot.m_sample.m_decay_level = slot.m_regs[8] >> 4;
			slot.m_sample.m_decay2_reg = slot.m_regs[8] & 0xf;
			slot.m_sample.m_key_rate_scale = slot.m_regs[9] >> 4;
			slot.m_sample.m_release_reg = slot.m_regs[9] & 0xf;
			envelope_generator_calc(slot);
			break;
	}
}

uint8_t multipcm_device::read()
{
	return 0;
}

void multipcm_device::write(offs_t offset, uint8_t data)
{
	switch(offset)
	{
		case 0: // Data write
			if (m_address < 11)
				write_slot(m_slots[m_cur_slot], m_address, data);
			else
				M1_LOG("unknown voice register %02x = %02x (slot %d)\n", m_address, data, m_cur_slot);
			break;
		case 1:
			m_cur_slot = VALUE_TO_CHANNEL[data & 0x1f];
			break;

		case 2:
			m_address = data;
			break;

	}
}



