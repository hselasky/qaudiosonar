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

qas_block_filter :: qas_block_filter(double amp, double low_hz, double high_hz)
{
	memset(this, 0, sizeof(*this));

	freq = (low_hz + high_hz) / 2.0;

	for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
		double p = 2.0 * M_PI * freq *
		    (double)x / (double)qas_sample_rate;
		t_cos[x] = cos(p);
		t_sin[x] = sin(p);
	}
}

void
qas_block_filter :: do_reset()
{
	memset(power, 0, sizeof(power));
}

void
qas_block_filter :: do_mon_block_in(const double *output_lin, ssize_t size)
{
	double s_cos_in = 0;
	double s_sin_in = 0;

	for (ssize_t x = 0; x != size; x++) {
		s_cos_in += t_cos[x] * output_lin[-x];
		s_sin_in += t_sin[x] * output_lin[-x];
	}

	atomic_lock();
	t_amp = sqrt(s_cos_in * s_cos_in + s_sin_in * s_sin_in) / (double)size;
	if (t_amp < 1.0)
		t_amp = 0.0;
	t_phase = atan(s_sin_in / s_cos_in);
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
