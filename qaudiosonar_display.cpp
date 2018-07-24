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
	struct table *a = (struct table *)_a;
	struct table *b = (struct table *)_b;

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
	size_t x,y;

	memset(band, 0, sizeof(double) * 3 * bwi);

	for (x = 0; x != wi; x++) {
		y = (x % bwi);
		if (data[3 * x] > band[3 * y])
			memcpy(band + 3 * y, data + 3 * x, 3 * sizeof(double));
	}
}

static void *
qas_display_worker(void *arg)
{
	size_t table_size = qas_num_bands / QAS_WAVE_STEP;
	struct table table[table_size];

	while (1) {
		struct qas_wave_job *pjob;
		struct qas_corr_out_data *pcorr;
		double *data;
		double *band;
		size_t data_size;

		pjob = qas_display_job_dequeue();

		/* get parent structure */
		pcorr = pjob->data;
		/* get relevant data line */
		data = qas_display_get_line(pcorr->sequence_number);
		/* get relevant band */
		band = qas_display_get_band(pcorr->sequence_number);

		/* get data_size */
		data_size = pjob->data_offset - (pjob->band_start * 2);

		atomic_graph_lock();
		switch (pcorr->state) {
			size_t off;
			size_t x,z;
			double *p_value;

		case QAS_STATE_1ST_SCAN:
			off = (QAS_WAVE_STEP_LOG2 * pjob->band_start * 3) / QAS_WAVE_STEP;
			p_value = pcorr->data_array + pjob->data_offset;

			/* collect a data point */
			data[off + 0] = p_value[0];
			data[off + 1] = p_value[1];
			data[off + 2] = pjob->band_start;

			for (x = 1; x != QAS_WAVE_STEP_LOG2; x++) {
				/* collect a data point */
				data[off + (3 * x) + 0] = 0;
				data[off + (3 * x) + 1] = 0;
				data[off + (3 * x) + 2] = 0;
			}
			break;

		case QAS_STATE_2ND_SCAN:
			off = (QAS_WAVE_STEP_LOG2 * pjob->band_start * 3) / QAS_WAVE_STEP;
			for (x = 0; x != QAS_WAVE_STEP; x++) {
				p_value = pcorr->data_array + pjob->data_offset + 2 * x;
				if (p_value[0] > 0.0) {
					/* clear data point */
					data[off + 0] = 0;
					data[off + 1] = 0;
					data[off + 2] = 0;

					z = (QAS_WAVE_STEP_LOG2 * x) / QAS_WAVE_STEP;
					/* collect a data point */
					data[off + (3 * z) + 0] = p_value[0];
					data[off + (3 * z) + 1] = p_value[1];
					data[off + (3 * z) + 2] = pjob->band_start + x;
					break;
				}
			}
			break;
		}
		atomic_graph_unlock();

		/* free current job */
		qas_wave_job_free(pjob);

		if (--(pcorr->refcount) == 0) {
			switch (pcorr->state) {
			case QAS_STATE_1ST_SCAN:
				for (size_t x = 0; x != table_size; x++) {
					table[x].value = pcorr->data_array[data_size + 2 * x * QAS_WAVE_STEP];
					table[x].band = x * QAS_WAVE_STEP;
				}
				mergesort(table, table_size, sizeof(table[0]), &qas_table_compare);
				pcorr->state = QAS_STATE_2ND_SCAN;

				/* generate jobs for output data */
				for (size_t x = table_size - (table_size / 4); x != table_size; x++) {
					if (table[x].value < 16.0)
						continue;
					pcorr->refcount++;
					pjob = qas_wave_job_alloc();
					pjob->data_offset = data_size + (2 * table[x].band);
					pjob->band_start = table[x].band;
					pjob->data = pcorr;
					qas_wave_job_insert(pjob);
				}
				if (pcorr->refcount != 0)
					break;
			case QAS_STATE_2ND_SCAN:
				qas_display_worker_done(data, band);

				qas_corr_out_free(pcorr);

				atomic_lock();
				qas_out_sequence_number++;
				atomic_unlock();
				break;
			}
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
	return (((QAS_WAVE_STEP_LOG2 * qas_num_bands) / QAS_WAVE_STEP) * 3);
}

double *
qas_display_get_band(size_t which)
{
	return (qas_display_band + qas_display_band_width() * (which % qas_display_hist_max));
}

size_t
qas_display_band_width()
{
	return (12 * QAS_WAVE_STEP_LOG2 * 3);
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
		    (x / QAS_WAVE_STEP_LOG2) * QAS_WAVE_STEP]);
	}

	size = sizeof(double) * qas_display_band_width() * qas_display_hist_max;
	qas_display_band = (double *)malloc(size);
	memset(qas_display_band, 0, size);

	pthread_t qas_display_thread;
	pthread_create(&qas_display_thread, 0, &qas_display_worker, 0);
}
