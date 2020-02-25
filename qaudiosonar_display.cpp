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

static pthread_cond_t qas_display_cond;
static pthread_mutex_t qas_display_mutex;

static TAILQ_HEAD(,qas_wave_job) qas_display_in_head =
    TAILQ_HEAD_INITIALIZER(qas_display_in_head);

double *qas_display_data;
double *qas_display_band;
size_t qas_display_hist_max;
uint8_t *qas_iso_table;

void
qas_display_job_insert(struct qas_wave_job *pjob)
{
	qas_display_lock();
	TAILQ_INSERT_TAIL(&qas_display_in_head, pjob, entry);
	qas_display_signal();
	qas_display_unlock();
}

struct qas_wave_job *
qas_display_job_dequeue()
{
	struct qas_wave_job *pjob;

  	qas_display_lock();
	while ((pjob = TAILQ_FIRST(&qas_display_in_head)) == 0)
		qas_display_wait();
	TAILQ_REMOVE(&qas_display_in_head, pjob, entry);
	qas_display_unlock();

	return (pjob);
}

void
qas_display_signal()
{
	pthread_cond_signal(&qas_display_cond);
}

void
qas_display_wait()
{
	pthread_cond_wait(&qas_display_cond, &qas_display_mutex);
}

void
qas_display_lock()
{
	pthread_mutex_lock(&qas_display_mutex);
}

void
qas_display_unlock()
{
	pthread_mutex_unlock(&qas_display_mutex);
}

struct table {
	double value;
	size_t band;
};

static int
qas_table_compare(const void *_a, const void *_b)
{
	const struct table *a = (const struct table *)_a;
	const struct table *b = (const struct table *)_b;

	if (a->value > b->value)
		return (1);
	else if (a->value < b->value)
		return (-1);
	else if (a->band > b->band)
		return (1);
	else if (a->band < b->band)
		return (-1);
	else
		return (0);
}

static void
qas_display_worker_done(double *data, double *band)
{
	size_t wi = qas_display_width() / 3;
	size_t bwi = qas_display_band_width() / 3;
	size_t x;

	memset(band, 0, sizeof(double) * 3 * bwi);

	for (x = 0; x != bwi; x++)
		band[3 * x + 1] = x;

	for (x = 1; x != (wi - 1); x++) {
		double value = data[3 * x];
		size_t y = (x % bwi);

		if (value >= data[3 * (x - 1)] &&
		    value >= data[3 * (x + 1)]) {
			if (value >= band[3 * y]) {
				band[3 * y] = value;
				band[3 * y + 2] = data[3 * x + 2];
			}
		}
	}
}

static void *
qas_display_worker(void *arg)
{
	const size_t table_size = qas_num_bands / QAS_WAVE_STEP;
	struct table table[table_size];

	while (1) {
		struct qas_wave_job *pjob;
		struct qas_corr_data *pcorr;
		const double *data_old;
		double *data;
		double *band;

		pjob = qas_display_job_dequeue();

		/* get parent structure */
		pcorr = pjob->data;
		/* get relevant data line */
		data = qas_display_get_line(pcorr->sequence_number);
		/* get relevant band */
		band = qas_display_get_band(pcorr->sequence_number);

		atomic_graph_lock();
		switch (pcorr->state) {
			size_t off;
		case QAS_STATE_1ST_SCAN:
		case QAS_STATE_2ND_SCAN:
		case QAS_STATE_3RD_SCAN:
			off = 3 * (pjob->band_start / QAS_WAVE_STEP);

			/* collect a data point */
			data[off + 0] = pcorr->band_data[pjob->band_start / QAS_WAVE_STEP];
			data[off + 1] = 0;
			data[off + 2] = pjob->band_start;
			break;
		}
		atomic_graph_unlock();

		/* free current job */
		qas_wave_job_free(pjob);

		if (--(pcorr->refcount))
			continue;

		switch (pcorr->state++) {
		size_t y;
		case QAS_STATE_1ST_SCAN:
			for (size_t x = 0; x != table_size; x++) {
				table[x].value = pcorr->band_data[x];
				table[x].band = x;
			}
			mergesort(table, table_size, sizeof(table[0]), &qas_table_compare);

			y = table[table_size - 1].band;

			/* avoid beginning and end band */
			if (y == 0)
				y++;
			else if (y == table_size - 1)
				y = table_size - 2;

			/* submit three new jobs */
			pcorr->refcount += 3;
			
			pjob = qas_wave_job_alloc();
			pjob->band_start = y * QAS_WAVE_STEP;
			pjob->data = pcorr;
			qas_wave_job_insert(pjob);

			pjob = qas_wave_job_alloc();
			pjob->band_start = (y - 1) * QAS_WAVE_STEP;
			pjob->data = pcorr;
			qas_wave_job_insert(pjob);

			pjob = qas_wave_job_alloc();
			pjob->band_start = (y + 1) * QAS_WAVE_STEP;
			pjob->data = pcorr;
			qas_wave_job_insert(pjob);
			break;

		case QAS_STATE_2ND_SCAN:
			/* find largest value */
			for (size_t x = y = 0; x != table_size; x++) {
				if (data[3 * x + 0] > data[3 * y + 0])
					y = x;
			}

			/* get remainder of band */
			y = data[3 * y + 2];
			y %= QAS_WAVE_STEP;

			pcorr->refcount = qas_num_bands / QAS_WAVE_STEP;

			/* generate jobs for output data */
			for (size_t x = 0; x != qas_num_bands; x += QAS_WAVE_STEP) {
				pjob = qas_wave_job_alloc();
				pjob->band_start = x + y;
				pjob->data = pcorr;
				qas_wave_job_insert(pjob);
			}
			break;

		case QAS_STATE_3RD_SCAN:
			qas_display_worker_done(data, band);
			qas_corr_free(pcorr);

			data_old = qas_display_get_line(pcorr->sequence_number - 1);

			atomic_graph_lock();
			for (size_t x = 0; x != table_size; x++) {
				data[3 * x] += data_old[3 * x] * qas_view_decay;
			}
			atomic_graph_unlock();

			atomic_lock();
			qas_out_sequence_number++;
			atomic_unlock();
			break;
		}
	}
	return 0;
}

double *
qas_display_get_line(size_t which)
{
	return (qas_display_data + qas_display_width() * (which % qas_display_hist_max));
}

size_t
qas_display_width()
{
	return ((qas_num_bands / QAS_WAVE_STEP) * 3);
}

double *
qas_display_get_band(size_t which)
{
	return (qas_display_band + qas_display_band_width() * (which % qas_display_hist_max));
}

size_t
qas_display_band_width()
{
	return (12 * 3);
}

size_t
qas_display_height()
{
	return (qas_display_hist_max);
}

size_t
qas_display_lag()
{
	size_t retval;

	atomic_lock();
	retval = ((size_t)(qas_in_sequence_number - qas_out_sequence_number) %
		qas_display_hist_max);
	atomic_unlock();

	return (retval);
}

void
qas_display_init()
{
	pthread_mutex_init(&qas_display_mutex, 0);
	pthread_cond_init(&qas_display_cond, 0);
	size_t size;

	qas_display_hist_max = 256;

	size = sizeof(double) * qas_display_width() * qas_display_hist_max;
	qas_display_data = (double *)malloc(size);
	memset(qas_display_data, 0, size);

	size = qas_display_width() / 3;
	qas_iso_table = (uint8_t *)malloc(size);

	for (size_t x = 0; x != size; x++) {
		qas_iso_table[x] = qas_find_iso(qas_freq_table[
		    x * QAS_WAVE_STEP]);
	}

	size = sizeof(double) * qas_display_band_width() * qas_display_hist_max;
	qas_display_band = (double *)malloc(size);
	memset(qas_display_band, 0, size);

	pthread_t qas_display_thread;
	pthread_create(&qas_display_thread, 0, &qas_display_worker, 0);
}
