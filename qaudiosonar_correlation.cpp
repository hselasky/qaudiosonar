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

static pthread_cond_t qas_corr_cond;
static pthread_mutex_t qas_corr_mutex;

static TAILQ_HEAD(,qas_corr_data) qas_corr_head =
    TAILQ_HEAD_INITIALIZER(qas_corr_head);

double *qas_mon_decay;

struct qas_corr_data *
qas_corr_alloc(void)
{
	struct qas_corr_data *ptr;
	const size_t size = sizeof(*ptr) + (
	    qas_mon_size +
	    QAS_CORR_SIZE +
	    qas_mon_size + QAS_CORR_SIZE +
	    (qas_num_bands / QAS_WAVE_STEP)
	) * sizeof(double);

	ptr = (struct qas_corr_data *)malloc(size);
	if (ptr != 0) {
		memset(ptr, 0, size);
		ptr->monitor_data = ptr->internal_data;
		ptr->input_data = ptr->monitor_data + qas_mon_size;
		ptr->corr_data = ptr->input_data + QAS_CORR_SIZE;
		ptr->band_data = ptr->corr_data + qas_mon_size + QAS_CORR_SIZE;
	}
	return (ptr);
}

void
qas_corr_free(struct qas_corr_data *ptr)
{
	free(ptr);
}

void
qas_corr_insert(struct qas_corr_data *ptr)
{
  	qas_corr_lock();
	TAILQ_INSERT_TAIL(&qas_corr_head, ptr, entry);
	qas_corr_signal();
	qas_corr_unlock();
}

struct qas_corr_data *
qas_corr_job_dequeue()
{
	struct qas_corr_data *ptr;

  	qas_corr_lock();
	while ((ptr = TAILQ_FIRST(&qas_corr_head)) == 0)
		qas_corr_wait();
	TAILQ_REMOVE(&qas_corr_head, ptr, entry);
	qas_corr_unlock();

	return (ptr);
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
		struct qas_corr_data *ptr;
		struct qas_wave_job *pjob;

		ptr = qas_corr_job_dequeue();

		/* do correlation */
		for (size_t x = 0; x != qas_mon_size; x += QAS_CORR_SIZE) {
			qas_x3_multiply_double(ptr->monitor_data + x,
			    ptr->input_data,
			    ptr->corr_data + x, QAS_CORR_SIZE);
		}

		atomic_graph_lock();
		for (size_t x = 0; x != qas_window_size; x++) {
			qas_mon_decay[x] *= qas_view_decay;
			qas_mon_decay[x] += ptr->corr_data[x + QAS_CORR_SIZE];
		}
		atomic_graph_unlock();

		/* set refcount */
		ptr->refcount = 1;

		/* generate job for output data */
		pjob = qas_wave_job_alloc();
		pjob->band_start = 0;
		pjob->data = ptr;
		qas_wave_job_insert(pjob);
	}
	return (0);
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
