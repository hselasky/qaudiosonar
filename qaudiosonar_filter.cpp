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

qas_block_filter :: qas_block_filter(double amp, double low_hz, double high_hz)
{
	memset(this, 0, sizeof(*this));

	qas_band_pass(low_hz, high_hz, amp, filter_lin, QAS_MUL_SIZE);

	qas_mul_import_double(filter_lin, filter_fast, QAS_MUL_SIZE);
	qas_mul_xform_fwd_double(filter_fast, QAS_MUL_SIZE);
	
	freq = (low_hz + high_hz) / 2.0;

	for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
		double p = 2.0 * M_PI * freq *
		    (double)x / (double)qas_sample_rate;
		t_cos[x] = cos(p);
		t_sin[x] = sin(p);
	}
}

void
qas_block_filter :: do_block(const double *input_filter, double *output_lin)
{
	/* multiply the two vectors */
	for (unsigned x = 0; x != QAS_FILTER_SIZE; x++)
		output[toggle][0][x] = input_filter[x] * filter_fast[x];

	/* compute correlation */
	qas_mul_xform_inv_double(output[toggle][0], QAS_MUL_SIZE);

	/* re-order vector array */
	for (unsigned x = 0; x != QAS_FILTER_SIZE; x++)
		output[toggle][1][x] = output[toggle][0][qas_mul_context->table[x]];

	/* final error correction */
	qas_mul_xform_inv_double(output[toggle][1], QAS_MUL_SIZE);

	/* zero the result before exporting */
	memset(output[toggle][0], 0, 2 * QAS_MUL_SIZE * sizeof(double));

	/* export */
	qas_mul_export_double(output[toggle][1], output[toggle][0], QAS_MUL_SIZE);

	for (unsigned x = 0; x != QAS_MUL_SIZE; x++) {
		output_lin[x] = output[toggle][0][x] +
		    output[toggle ^ 1][0][x + QAS_MUL_SIZE];
	}

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
qas_block_filter :: do_mon_block_in(const int64_t *output_lin)
{
	double s_cos_in = 0;
	double s_sin_in = 0;

	for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
		s_cos_in += t_cos[x] * output_lin[x];
		s_sin_in += t_sin[x] * output_lin[x];
	}

	atomic_lock();
	t_amp = sqrt(s_cos_in * s_cos_in + s_sin_in * s_sin_in);
	atomic_unlock();
}

void
qas_queue_block_filter(qas_block_filter *f, qas_block_filter_head_t *phead)
{
	atomic_lock();
	TAILQ_INSERT_TAIL(phead, f, entry);
	atomic_wakeup();
	atomic_unlock();
}
