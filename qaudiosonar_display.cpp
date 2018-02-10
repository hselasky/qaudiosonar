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
size_t qas_display_hist_max;

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

static void *
qas_display_worker(void *arg)
{
	while (1) {
		struct qas_wave_job *pjob;
		double *data;
		int band_max;

		pjob = qas_display_job_dequeue();
		data = qas_display_get_line(pjob->data->sequence_number);

		atomic_graph_lock();
		memcpy(data + (pjob->band_start * 2),
		    pjob->data->data_array + pjob->data_offset,
		    sizeof(double) * 2 * QAS_WAVE_STEP);
		band_max = qas_mw->band_max;
		atomic_graph_unlock();

		if (qas_wave_job_free(pjob)) {
		  	enum { MAX = (QAS_WAVE_STEP * 12) };
			size_t wi = qas_display_width() / 2;
			double temp[MAX];
			size_t x, z;
			static size_t last_z = -1ULL;

			for (x = 0; x != MAX; x++)
				temp[x] = 1.0;

			for (x = 0; x != wi; x++) {
				for (z = 0; z != ((x > MAX) ? MAX : (x + 1)); z++) {
					double value = data[2 * (x - z)];
					if (value < 1.0)
						continue;
					value = pow(value, 3.0);
					if (value > temp[x % MAX])
						temp[x % MAX] = value;
					break;
				}
			}
			for (z = x = 0; x != MAX; x++) {
				temp[x] = temp[x];
				if (temp[x] > temp[z])
					z = x;
			}
			if (temp[z] != 1.0 && z != last_z) {
				int band = z + (band_max - (band_max % MAX));
				int key = (band / QAS_WAVE_STEP) + 9;
				uint8_t chan = 0;

				if (z & 1)
					chan |= 8;
				if (z & 2)
					chan |= 4;
				if (z & 4)
					chan |= 2;
				if (z & 8)
					chan |= 1;

				if (qas_record != 0 && band > -1 && (size_t)band < qas_num_bands) {
					QString str(qas_descr_table[band]);
					str += QString(" /* %1Hz */").arg((int)qas_freq_table[band]);
					qas_mw->handle_append_text(str);

					if (key > -1 && key < 128) {
						qas_midi_key_send(chan, key, 90, 50);
						qas_midi_key_send(chan, key, 0, 0);
					}
				}
				last_z = z;
			}
		}
	}
	return 0;
}

double *
qas_display_get_line(size_t which)
{
	return (qas_display_data + (qas_num_bands * 2 * (which % qas_display_hist_max)));
}

size_t
qas_display_width()
{
	return (qas_num_bands * 2);
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

	qas_display_hist_max = 256;
	size_t size = 2 * sizeof(double) * qas_num_bands * qas_display_hist_max;
	qas_display_data = (double *)malloc(size);

	for (size_t x = 0; x != size; x += sizeof(double))
		qas_display_data[x / sizeof(double)] = -1.0;

	pthread_t qas_display_thread;
	pthread_create(&qas_display_thread, 0, &qas_display_worker, 0);
}
