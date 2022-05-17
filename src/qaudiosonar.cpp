/*-
 * Copyright (c) 2016-2022 Hans Petter Selasky. All rights reserved.
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

#include "qaudiosonar_mainwindow.h"

static pthread_mutex_t atomic_mtx;
static pthread_mutex_t atomic_graph;
static pthread_cond_t atomic_cv;

int qas_num_workers = 2;
size_t qas_window_size;
size_t qas_in_sequence_number;
size_t qas_out_sequence_number;
QasMainWindow *qas_mw;

static void
atomic_init(void)
{
	pthread_mutex_init(&atomic_mtx, NULL);
	pthread_mutex_init(&atomic_graph, NULL);
	pthread_cond_init(&atomic_cv, NULL);
}

void
atomic_lock(void)
{
	pthread_mutex_lock(&atomic_mtx);
}

void
atomic_unlock(void)
{
	pthread_mutex_unlock(&atomic_mtx);
}

void
atomic_graph_lock(void)
{
	pthread_mutex_lock(&atomic_graph);
}

void
atomic_graph_unlock(void)
{
	pthread_mutex_unlock(&atomic_graph);
}

void
atomic_wait(void)
{
	pthread_cond_wait(&atomic_cv, &atomic_mtx);
}

void
atomic_wakeup(void)
{
	pthread_cond_broadcast(&atomic_cv);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: qaudiosonar "
	    "[-n <workers>] [-w <windowsize>]\n"
	    "\t" "-r <samplerate: 8000, 9600, 12000, 16000, 24000, 48000>\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	QApplication app(argc, argv);
	int c;

        /* must be first, before any threads are created */
        signal(SIGPIPE, SIG_IGN);

	while ((c = getopt(argc, argv, "n:r:hw:")) != -1) {
		switch (c) {
		case 'n':
			qas_num_workers = atoi(optarg);
			if (qas_num_workers < 1)
				qas_num_workers = 1;
			else if (qas_num_workers > 16)
				qas_num_workers = 16;
			break;
		case 'r':
			qas_sample_rate = atoi(optarg);
			if (qas_sample_rate < 8000)
				qas_sample_rate = 8000;
			else if (qas_sample_rate > QAS_SAMPLE_RATE)
				qas_sample_rate = QAS_SAMPLE_RATE;
			if ((QAS_SAMPLE_RATE % qas_sample_rate) != 0)
				usage();
			break;
		case 'w':
			qas_window_size = atoi(optarg);
			break;
		default:
			usage();
			break;
		}
	}

	atomic_init();

	/* range check window size */
	if (qas_window_size == 0)
		qas_window_size = QAS_CORR_SIZE;
	else if (qas_window_size >= (size_t)(16 * qas_sample_rate))
		qas_window_size = (size_t)(16 * qas_sample_rate);

	/* align window size */
	qas_window_size -= (qas_window_size % QAS_CORR_SIZE);
	if (qas_window_size == 0)
		errx(EX_USAGE, "Invalid window size\n");

	qas_mw = new QasMainWindow();

	qas_wave_init();
	qas_corr_init();
	qas_display_init();
	qas_midi_init();
	qas_dsp_init();

	qas_mw->show();

	QThread::currentThread()->setPriority(QThread::LowPriority);

	return (app.exec());
}
