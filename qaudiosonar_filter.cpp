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
	int wq = window_size / 4;
	int wh = window_size / 2;
	int x;
	int z;

	freq /= qas_sample_rate;
	freq *= QAS_WINDOW_SIZE / 2;

	freq -= ((double)wq) * ((double)(int)(freq / (double)(wq)));

	z = (((double)wh) / (2.0 * freq)) * ((double)(int)(2.0 * freq));

	if (z < 0)
		z = -z;
	if (z > wh)
		z = wh;

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
	double limit = sqrt(QAS_FET_PRIME / 4);
	double sum = 0.0;
	double prescaler;

	for (unsigned x = 0; x != QAS_FET_SIZE; x++) {
		if (filter[x] < 0)
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
	double limit = sqrt(QAS_FET_PRIME / 4);
	double sum = 0.0;
	double prescaler;

	for (unsigned x = 0; x != QAS_FET_SIZE; x++) {
		if (filter[x] < 0)
			sum -= filter[x];
		else
			sum += filter[x];
	}
	prescaler = (limit / sum);

	for (unsigned x = 0; x != QAS_FET_SIZE; x++)
		filter[x] *= prescaler;

	return (prescaler);
}

qas_filter :: qas_filter(unsigned filter_size, unsigned which, double low, double high)
  : which(which)
{
	if (which >= qas_bands)
		which = 0;

	if (filter_size > QAS_FET_SIZE)
		filter_size = QAS_FET_SIZE;

	filter_size &= ~3;

	memset(filter_lin, 0, sizeof(filter_lin));

	qas_band_pass(low, high, 1.0, filter_lin, filter_size);

	prescaler = fet_prescaler_double(filter_lin);

	for (unsigned x = 0; x != QAS_FET_SIZE; x++)
		filter_fast[x] = filter_lin[x];

	fet_16384_64(filter_fast);

	freq = (low + high) / 2.0;
}

void
qas_queue_filter(qas_filter *f)
{
	atomic_lock();
	TAILQ_INSERT_TAIL(&qas_filter_head, f, entry);
	atomic_wakeup();
	atomic_unlock();
}
