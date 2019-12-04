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

static pthread_cond_t qas_corr_cond;
static pthread_mutex_t qas_corr_mutex;

static TAILQ_HEAD(,qas_corr_in_data) qas_corr_in_head = TAILQ_HEAD_INITIALIZER(qas_corr_in_head);

double *qas_mon_decay;

struct qas_corr_out_data *
qas_corr_out_alloc(size_t samples)
{
	struct qas_corr_out_data *pout;
	const size_t size = sizeof(*pout) + samples * sizeof(double);

	pout = (struct qas_corr_out_data *)malloc(size);
	if (pout != 0)
		memset(pout, 0, size);
	return (pout);
}

void
qas_corr_out_free(struct qas_corr_out_data *pout)
{
	free(pout);
}

struct qas_corr_in_data *
qas_corr_in_alloc(size_t samples)
{
	struct qas_corr_in_data *pin;

	pin = (struct qas_corr_in_data *)malloc(sizeof(*pin) + samples * sizeof(double));
	return (pin);
}

void
qas_corr_in_free(struct qas_corr_in_data *pin)
{
	free(pin);
}

void
qas_corr_in_insert(struct qas_corr_in_data *pin)
{
  	qas_corr_lock();
	TAILQ_INSERT_TAIL(&qas_corr_in_head, pin, entry);
	qas_corr_signal();
	qas_corr_unlock();
}

struct qas_corr_in_data *
qas_corr_in_job_dequeue()
{
	struct qas_corr_in_data *pin;

  	qas_corr_lock();
	while ((pin = TAILQ_FIRST(&qas_corr_in_head)) == 0)
		qas_corr_wait();
	TAILQ_REMOVE(&qas_corr_in_head, pin, entry);
	qas_corr_unlock();

	return (pin);
}

void
qas_corr_signal()
{
	pthread_cond_signal(&qas_corr_cond);
}

void
qas_corr_wait()
{
	pthread_cond_wait(&qas_corr_cond, &qas_corr_mutex);
}

void
qas_corr_lock()
{
	pthread_mutex_lock(&qas_corr_mutex);
}

void
qas_corr_unlock()
{
	pthread_mutex_unlock(&qas_corr_mutex);
}

static void *
qas_corr_worker(void *arg)
{
	while (1) {
		const size_t data_size = qas_window_size + (2 * QAS_CORR_SIZE);
		struct qas_corr_in_data *pin;
		struct qas_corr_out_data *pout;
		struct qas_wave_job *pjob;

		pin = qas_corr_in_job_dequeue();

		pout = qas_corr_out_alloc(data_size + (2 * qas_num_bands / QAS_WAVE_STEP));

		pout->sequence_number = pin->sequence_number;
		pout->refcount = qas_num_bands / QAS_WAVE_STEP;
		pout->data_size = data_size;

		/* do correlation */
		for (size_t x = 0; x != data_size; x += QAS_CORR_SIZE) {
			qas_x3_multiply_double(pin->data + x,
			    pin->data + data_size - QAS_CORR_SIZE,
			    pout->data_array + x, QAS_CORR_SIZE);
		}

		/* free input data */
		qas_corr_in_free(pin);

		atomic_graph_lock();
		for (size_t x = 0; x != qas_window_size; x++) {
			qas_mon_decay[x] *= qas_view_decay;
			qas_mon_decay[x] += pout->data_array[x + QAS_CORR_SIZE];
			pout->data_array[x + QAS_CORR_SIZE] = qas_mon_decay[x];
		}
		atomic_graph_unlock();

		/* generate jobs for output data */
		for (size_t band = 0; band != qas_num_bands; band += QAS_WAVE_STEP) {
			pjob = qas_wave_job_alloc();
			pjob->data_offset = data_size + (2 * band / QAS_WAVE_STEP);
			pjob->band_start = band;
			pjob->data = pout;
			qas_wave_job_insert(pjob);
		}
	}
	return 0;
}

void
qas_corr_init()
{
	pthread_mutex_init(&qas_corr_mutex, 0);
	pthread_cond_init(&qas_corr_cond, 0);

	qas_mon_decay = (double *)malloc(sizeof(double) * qas_window_size);
	memset(qas_mon_decay, 0, sizeof(double) * qas_window_size);

	for (int i = 0; i != qas_num_workers; i++) {
		pthread_t qas_corr_thread;
		pthread_create(&qas_corr_thread, 0, &qas_corr_worker, 0);
	}
}
