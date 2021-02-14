/*-
 * Copyright (c) 2018-2020 Hans Petter Selasky. All rights reserved.
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

static pthread_cond_t qas_wave_cond;
static pthread_mutex_t qas_wave_mutex;

static TAILQ_HEAD(,qas_wave_job) qas_wave_head = TAILQ_HEAD_INITIALIZER(qas_wave_head);

double *qas_cos_table;
double *qas_sin_table;
double *qas_freq_table;
QString *qas_descr_table;
size_t qas_num_bands;
double qas_low_octave;

struct qas_wave_job *
qas_wave_job_alloc()
{
	struct qas_wave_job *pjob;

	pjob = (struct qas_wave_job *)malloc(sizeof(*pjob));
	return (pjob);
}

void
qas_wave_job_insert(struct qas_wave_job *pjob)
{
	qas_wave_lock();
	TAILQ_INSERT_TAIL(&qas_wave_head, pjob, entry);
	qas_wave_signal();
	qas_wave_unlock();
}

struct qas_wave_job *
qas_wave_job_dequeue()
{
	struct qas_wave_job *pjob;

  	qas_wave_lock();
	while ((pjob = TAILQ_FIRST(&qas_wave_head)) == 0)
		qas_wave_wait();
	TAILQ_REMOVE(&qas_wave_head, pjob, entry);
	qas_wave_unlock();

	return (pjob);
}

void
qas_wave_job_free(qas_wave_job *pjob)
{
	free(pjob);
}

void
qas_wave_signal()
{
	pthread_cond_signal(&qas_wave_cond);
}

void
qas_wave_wait()
{
	pthread_cond_wait(&qas_wave_cond, &qas_wave_mutex);
}

void
qas_wave_lock()
{
	pthread_mutex_lock(&qas_wave_mutex);
}

void
qas_wave_unlock()
{
	pthread_mutex_unlock(&qas_wave_mutex);
}

static void
qas_ftt_analyze(const double *indata, double *out)
{
	uint8_t log2_size;

	for (log2_size = 0; (1U << log2_size) < qas_window_size; log2_size++)
		;

	qas_complex_t temp[1U << log2_size];
	memset(temp, 0, sizeof(temp));

	for (size_t x = 0; x != qas_window_size; x++)
		temp[x].x = indata[x];

	qas_ftt_inv(temp, log2_size);

	const size_t nout = (qas_num_bands / QAS_WAVE_STEP);

	memset(out, 0, sizeof(out[0]) * nout);

	size_t y = 0;

	for (size_t x = 1; x != qas_window_size; x++) {
		const double amp = (fabs(temp[x].x) + fabs(temp[x].y)) /
		    ((double)qas_window_size * 0.5);
		const double freq = (double)x *
		    (double)qas_sample_rate / (double)(1U << log2_size);

		while (y != nout && freq > qas_freq_table[y * QAS_WAVE_STEP])
			y++;

		if (y != nout) {
			if (out[y] < amp)
				out[y] = amp;
		}
	}
}

static void
qas_wave_analyze(const double *indata, double delta_phase, double *out)
{
	double phase = 0.0;
	double cos_in = 0.0;
	double sin_in = 0.0;

	for (size_t x = 0; x != qas_window_size; x++) {
		cos_in += qas_ftt_cos(phase) * indata[x];
		sin_in += qas_ftt_sin(phase) * indata[x];
		phase += delta_phase;
	}

	out[0] = (fabs(cos_in) + fabs(sin_in)) / ((double)qas_window_size * 0.5);
	if (out[0] < 1.0)
		out[0] = 1.0;
}

static size_t
qas_wave_analyze_binary_search(const double *indata, double *out, size_t band, size_t rem)
{
	double dp;
	double temp[1];
	size_t pos = 0;

	dp = qas_freq_table[band + pos] / (double)qas_sample_rate;
	qas_wave_analyze(indata, dp, out);

	/* find maximum amplitude */
	while (rem != 0) {
		pos |= rem;

		dp = qas_freq_table[band + pos] / (double)qas_sample_rate;
		qas_wave_analyze(indata, dp, temp);
		if (out[0] > temp[0])
			pos &= ~rem;
		else
			memcpy(out, temp, sizeof(temp));
		rem /= 2;
	}
	return (pos);
}

static void *
qas_wave_worker(void *arg)
{
	while (1) {
		struct qas_wave_job *pjob;

		pjob = qas_wave_job_dequeue();

		switch (pjob->data->state) {
		case QAS_STATE_1ST_SCAN:
			qas_ftt_analyze(pjob->data->monitor_data, pjob->data->band_data);
			break;
		case QAS_STATE_2ND_SCAN:
			pjob->band_start +=
			    qas_wave_analyze_binary_search(pjob->data->monitor_data,
			        pjob->data->band_data + (pjob->band_start / QAS_WAVE_STEP),
			        pjob->band_start, QAS_WAVE_STEP / 2);
			break;
		}
		qas_display_job_insert(pjob);
	}
	return (0);
}

const double qas_base_freq = 440.0;	/* A-key in Hz */

void
qas_wave_init()
{
	const double min_hz = 8.0;
	const double max_hz = qas_sample_rate / 2.0;
	double num_low_octave = 0;
	double num_high_octave = 0;

	pthread_mutex_init(&qas_wave_mutex, 0);
	pthread_cond_init(&qas_wave_cond, 0);

	while ((qas_base_freq * pow(2.0, -num_low_octave)) > min_hz)
		num_low_octave++;

	while ((qas_base_freq * pow(2.0, num_high_octave)) < max_hz)
		num_high_octave++;

	num_high_octave--;
	num_low_octave--;

	if (num_high_octave < 1 || num_low_octave < 1)
		errx(EX_USAGE, "Invalid number of octaves\n");

	qas_low_octave = num_low_octave;
	qas_num_bands = (size_t)(num_high_octave + num_low_octave) * 12 * QAS_WAVE_STEP;

	qas_cos_table = (double *)malloc(sizeof(double) * qas_num_bands);
	qas_sin_table = (double *)malloc(sizeof(double) * qas_num_bands);
	qas_freq_table = (double *)malloc(sizeof(double) * qas_num_bands);
	qas_descr_table = new QString [qas_num_bands];

	for (size_t x = 0; x != qas_num_bands; x++) {
		const char *map[12] = {
			"A%1", "B%1B", "B%1", "C%1",
			"D%1B", "D%1", "E%1B", "E%1",
			"F%1", "G%1B", "G%1", "A%1B"
		};
		qas_freq_table[x] = qas_base_freq *
		    pow(2.0, (double)x / (double)(12 * QAS_WAVE_STEP) - num_low_octave);
		qas_descr_table[x] = QString(map[(x / QAS_WAVE_STEP) % 12])
		    .arg((x + 9 * QAS_WAVE_STEP) / (12 * QAS_WAVE_STEP));

		if (x % QAS_WAVE_STEP) {
			size_t y = 0;
			if (x & 1)
				y |= 128;
			if (x & 2)
				y |= 64;
			if (x & 4)
				y |= 32;
			if (x & 8)
				y |= 16;
			if (x & 16)
				y |= 8;
			if (x & 32)
				y |= 4;
			if (x & 64)
				y |= 2;
			if (x & 128)
				y |= 1;
			qas_descr_table[x] += QString(".%1").arg(y);
		}

		const double r = qas_freq_table[x] / (double)qas_sample_rate;
		qas_cos_table[x] = qas_ftt_cos(r);
		qas_sin_table[x] = qas_ftt_sin(r);
	}

	for (int i = 0; i != qas_num_workers; i++) {
		pthread_t qas_wave_thread;
		pthread_create(&qas_wave_thread, 0, &qas_wave_worker, 0);
	}
}
