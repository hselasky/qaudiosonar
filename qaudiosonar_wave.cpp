/*-
 * Copyright (c) 2018 Hans Petter Selasky. All rights reserved.
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

double **qas_cos_table;
double **qas_sin_table;
double *qas_freq_table;
uint8_t *qas_iso_table;
QString *qas_descr_table;
size_t qas_num_bands;

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

int
qas_wave_job_free(qas_wave_job *pjob)
{
	int free_data;

	qas_corr_lock();
	free_data = (--(pjob->data->refcount) == 0);	
	qas_corr_unlock();

	if (free_data) {
		qas_corr_out_free(pjob->data);
		atomic_lock();
		qas_out_sequence_number++;
		atomic_unlock();
	}
	free(pjob);
	return (free_data);
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
qas_wave_analyze(double *indata, double *pcos, double *psin, double *out)
{
	double cos_in = 0;
	double sin_in = 0;

	indata += qas_window_size + QAS_CORR_SIZE;

	for (size_t x = 0; x != qas_window_size; x++, indata--) {
		cos_in += pcos[x] * indata[0];
		sin_in += psin[x] * indata[0];
	}

	cos_in /= (double)qas_window_size * 0.5;
	sin_in /= (double)qas_window_size * 0.5;

	out[0] = sqrt(cos_in * cos_in + sin_in * sin_in);
	if (out[0] < 1.0) {
		out[0] = 0.0;
		out[1] = 0.0;
	} else {
		uint8_t pol = ((cos_in < 0) ? 2 : 0) | ((sin_in < 0) ? 1 : 0);

		out[1] = acos(fabs(cos_in) / out[0]);

		switch (pol) {
		case 0:
			break;
		case 1:
			out[1] = -out[1];
			break;
		case 2:
			out[1] = -M_PI - out[1];
			break;
		case 3:
			out[1] = -M_PI + out[1];
			break;
		default:
			break;
		}

		out[1] += M_PI;
		if (out[1] < 0.0)
			out[1] += 2.0 * M_PI;
		if (out[1] >= 2.0 * M_PI)
			out[1] -= 2.0 * M_PI;

		out[0] = sqrt(out[0]);
	}
}

static void
qas_wave_analyze_binary_search(double *indata, double *out, size_t band, size_t rem)
{
	size_t pos = 0;

	qas_wave_analyze(indata, qas_cos_table[band + pos],
	    qas_sin_table[band + pos], out + (2 * pos));

	/* find maximum amplitude */
	while (rem != 0) {
		pos |= rem;
	  	qas_wave_analyze(indata, qas_cos_table[band + pos],
		    qas_sin_table[band + pos], out + (2 * pos));
		if (out[2 * pos] < out[2 * pos - 2 * rem])
			pos &= ~rem;
		rem /= 2;
	}
}

static void
qas_wave_data_init(double *pdata, size_t size)
{
  	for (size_t x = 0; x != size; x += sizeof(double))
		pdata[x / sizeof(double)] = -1.0;
}

static void *
qas_wave_worker(void *arg)
{
	while (1) {
		struct qas_wave_job *pjob;

		pjob = qas_wave_job_dequeue();

		qas_wave_data_init(pjob->data->data_array + pjob->data_offset,
		    sizeof(double) * 2 * QAS_WAVE_STEP);

		qas_wave_analyze_binary_search(pjob->data->data_array,
		    pjob->data->data_array + pjob->data_offset,
		    pjob->band_start, QAS_WAVE_STEP / 2);

		qas_display_job_insert(pjob);
	}
	return 0;
}

static const double qas_base_freq = 440.0;	/* Hz */

void
qas_wave_init()
{
	const double min_hz = 16.0;
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
	
	qas_num_bands = (size_t)(num_high_octave + num_low_octave) * 12 * QAS_WAVE_STEP;

	qas_cos_table = (double **)malloc(sizeof(void *) * qas_num_bands);
	qas_sin_table = (double **)malloc(sizeof(void *) * qas_num_bands);
	qas_freq_table = (double *)malloc(sizeof(double) * qas_num_bands);
	qas_iso_table = (uint8_t *)malloc(sizeof(uint8_t) * qas_num_bands);
	qas_descr_table = new QString [qas_num_bands];

	for (size_t x = 0; x != qas_num_bands; x++) {
		const char *map[12] = {
			"A%1", "H%1B", "H%1", "C%1",
			"D%1B", "D%1", "E%1B", "E%1",
			"F%1", "G%1B", "G%1", "A%1B"
		};
		qas_freq_table[x] = qas_base_freq *
		    pow(2.0, (double)x / (double)(12.0 * QAS_WAVE_STEP) - num_low_octave);
		qas_iso_table[x] = qas_find_iso(qas_freq_table[x]);
		qas_cos_table[x] = (double *)malloc(sizeof(double) * qas_window_size);
		qas_sin_table[x] = (double *)malloc(sizeof(double) * qas_window_size);
		qas_descr_table[x] = QString(map[(x / 16) % 12]).arg(x / (12 * 16));
		if (x % 16) {
			size_t y = 0;
			if (x & 1)
				y |= 8;
			if (x & 2)
				y |= 4;
			if (x & 4)
				y |= 2;
			if (x & 8)
				y |= 1;
			qas_descr_table[x] += QString(".%1").arg(y);
		}
		for (size_t y = 0; y != qas_window_size; y++) {
			double r = 2.0 * M_PI * qas_freq_table[x] *
			    (double)y / (double)qas_sample_rate;
			qas_cos_table[x][y] = cos(r);
			qas_sin_table[x][y] = sin(r);
		}
	}

	for (int i = 0; i != qas_num_workers; i++) {
		pthread_t qas_wave_thread;
		pthread_create(&qas_wave_thread, 0, &qas_wave_worker, 0);
	}
}
