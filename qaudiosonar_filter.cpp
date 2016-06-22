/*-
 * Copyright (c) 2016 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "qaudiosonar.h"

static void
qas_low_pass(double freq, double amp, double *factor, unsigned window_size)
{
#if 0
	int wq = window_size / 4;
#endif
	int wh = window_size / 2;
	int x;
	int z;

	freq /= qas_sample_rate;
	freq *= wh;

#if 0
	freq -= ((double)wq) * ((double)(int)(freq / (double)(wq)));

	z = (((double)wh) / (2.0 * freq)) * ((double)(int)(2.0 * freq));
	if (z < 0)
		z = -z;
	if (z > wh)
		z = wh;
#else
	z = wh;
#endif

	factor[wh] += (2.0 * amp * freq) / ((double)wh);

	freq *= (2.0 * M_PI) / ((double)wh);

	for (x = -z+1; x < z; x++) {
		if (x == 0)
			continue;
		factor[(x + wh)] +=
			(amp * sin(freq * (double)(x)) / (M_PI * (double)(x)));
	}
}

static void
qas_band_pass(double freq_high, double freq_low, double amp,
    double *factor, unsigned window_size)
{
	/* lowpass */
	qas_low_pass(freq_low, amp, factor, window_size);
	/* highpass */
	qas_low_pass(-freq_high, amp, factor, window_size);
}

double
fet_prescaler_double(double *filter)
{
	double limit = sqrt(QAS_FET_PRIME / 4.0);
	double sum = 0.0;
	double prescaler;

	for (unsigned x = 0; x != QAS_FET_SIZE; x++) {
		if (filter[x] < 0.0)
			sum -= filter[x];
		else
			sum += filter[x];
	}
	prescaler = (limit / sum);

	for (unsigned x = 0; x != QAS_FET_SIZE; x++)
		filter[x] *= prescaler;

	return (prescaler);
}

double
fet_prescaler_s64(int64_t *filter)
{
	double limit = sqrt(QAS_FET_PRIME / 4.0);
	double sum = 0.0;
	double prescaler;

	for (unsigned x = 0; x != QAS_FET_SIZE; x++) {
		if (filter[x] < 0.0)
			sum -= filter[x];
		else
			sum += filter[x];
	}
	prescaler = (limit / sum);

	for (unsigned x = 0; x != QAS_FET_SIZE; x++)
		filter[x] *= prescaler;

	return (prescaler);
}

qas_block_filter :: qas_block_filter(double amp, double low_hz, double high_hz)
{
	memset(this, 0, sizeof(*this));

	qas_band_pass(low_hz, high_hz, amp, filter_lin, QAS_WINDOW_SIZE);

	prescaler = fet_prescaler_double(filter_lin);

	for (unsigned x = 0; x != QAS_FET_SIZE; x++)
		filter_fast[x] = filter_lin[x];

	fet_16384_64(filter_fast);

	freq = (low_hz + high_hz) / 2.0;
}

void
qas_block_filter :: do_block(double pre, int64_t *input_fet, int64_t *output_lin)
{
	fet_conv_16384_64(input_fet, filter_fast, output[toggle]);

	fet_16384_64(output[toggle]);

	pre *= prescaler;

	for (unsigned x = 0; x != QAS_FET_SIZE; x++)
		output[toggle][x] = fet_to_lin_64(output[toggle][x]) / pre;

	for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++)
		output_lin[x] = output[toggle][x] + output[toggle ^ 1][x + QAS_WINDOW_SIZE];

	toggle ^= 1;
}

void
qas_block_filter :: do_reset()
{
	toggle = 0;
	memset(power, 0, sizeof(power));
	power_ref = 0;
	memset(output, 0, sizeof(output));
}

void
qas_queue_block_filter(qas_block_filter *f, qas_block_filter_head_t *phead)
{
	atomic_lock();
	TAILQ_INSERT_TAIL(phead, f, entry);
	atomic_wakeup();
	atomic_unlock();
}

qas_wave_filter :: qas_wave_filter(double _freq)
{
	memset(this, 0, sizeof(*this));

	freq = _freq;

	unsigned waves = (freq * QAS_WINDOW_SIZE) / qas_sample_rate;
	freq = (double)qas_sample_rate * (double)waves / (double)QAS_WINDOW_SIZE;

	for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++) {
		double p = 2.0 * M_PI * freq *
		    (double)x / (double)qas_sample_rate;
		t_cos[x] = cos(p);
		t_sin[x] = sin(p);
	}
}

void
qas_wave_filter :: do_block_in(int64_t *output_lin, unsigned size)
{
	for (unsigned x = 0; x != size; x++) {
		s_cos_in += t_cos[x % QAS_WINDOW_SIZE] * output_lin[x];
		s_sin_in += t_sin[x % QAS_WINDOW_SIZE] * output_lin[x];
	}
}

void
qas_wave_filter :: do_block_out(int64_t *output_lin, unsigned size)
{
	for (unsigned x = 0; x != size; x++) {
		s_cos_out += t_cos[x % QAS_WINDOW_SIZE] * output_lin[x];
		s_sin_out += t_sin[x % QAS_WINDOW_SIZE] * output_lin[x];
	}
}

void
qas_wave_filter :: do_flush_in()
{
	power_in *= (1.0 - 1.0 / 32.0);
	power_in += s_cos_in * s_cos_in + s_sin_in * s_sin_in;
	s_cos_in = 0;
	s_sin_in = 0;
}

void
qas_wave_filter :: do_flush_out()
{
	power_out *= (1.0 - 1.0 / 32.0);
	power_out += s_cos_out * s_cos_out + s_sin_out * s_sin_out;
	s_cos_out = 0;
	s_sin_out = 0;
}

void
qas_wave_filter :: do_reset()
{
	power_in = 0;
	power_out = 0;

	s_cos_in = 0;
	s_sin_in = 0;

	s_cos_out = 0;
	s_sin_out = 0;
}

void
qas_queue_wave_filter(qas_wave_filter *f, qas_wave_filter_head_t *phead)
{
	atomic_lock();
	TAILQ_INSERT_TAIL(phead, f, entry);
	atomic_wakeup();
	atomic_unlock();
}
