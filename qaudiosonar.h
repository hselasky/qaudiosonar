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

#ifndef _QAUDIOSONAR_H_
#define	_QAUDIOSONAR_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <math.h>
#include <pthread.h>
#include <sysexits.h>

#include <sys/filio.h>
#include <sys/soundcard.h>
#include <sys/queue.h>

#include <QApplication>
#include <QPushButton>
#include <QLineEdit>
#include <QGridLayout>
#include <QLabel>
#include <QWidget>
#include <QPainter>
#include <QColor>
#include <QTimer>
#include <QScrollBar>

#define	QAS_FET_SIZE	0x4000
#define	QAS_FET_PRIME	0x42000001LL
#define	QAS_WINDOW_SIZE ((QAS_FET_SIZE / 3) & ~3)
#define	QAS_BUFFER_SIZE (2 * QAS_WINDOW_SIZE)	/* samples */
#define	QAS_MON_SIZE	(4 * QAS_FET_SIZE)

class qas_filter;
typedef TAILQ_CLASS_ENTRY(qas_filter) qas_filter_entry_t;
typedef TAILQ_CLASS_HEAD(,qas_filter) qas_filter_head_t;

class QasMainWindow;

class qas_filter {
public:
	qas_filter(unsigned filter_size, unsigned which, double low_hz, double high_hz);
	~qas_filter() { };
	qas_filter_entry_t entry;
	double prescaler;
	double freq;
	unsigned which;
	double filter_lin[QAS_FET_SIZE];
	int64_t filter_fast[QAS_FET_SIZE];
};

class QasGraph : public QWidget {
	Q_OBJECT
public:
	QasGraph(QasMainWindow *);
	~QasGraph() { };
	QasMainWindow *mw;
	QTimer *watchdog;
	void paintEvent(QPaintEvent *);

public slots:
	void handle_watchdog();
};

class QasMainWindow : public QWidget {
	Q_OBJECT
public:
	QasMainWindow();
	~QasMainWindow() { };
	void set_filter(int);
	double get_freq(int);

	QGridLayout *gl;
	QScrollBar *sb;
	QasGraph *qg;
	QLineEdit *led_dsp_read;
	QLineEdit *led_dsp_write;
	QTimer *sweep_timer;
	int qas_sweeping;

public slots:
	void handle_apply();
	void handle_sync();
	void handle_logarithmic();
	void handle_slider(int);
	void handle_sweep_up();
	void handle_sweep_lock();
	void handle_sweep_down();
	void handle_sweep_timer();
};

struct dsp_buffer {
	int16_t buffer[QAS_BUFFER_SIZE];
	unsigned in_off;
	unsigned mon_off;
	unsigned out_off;
};

extern qas_filter_head_t qas_filter_head;
extern struct dsp_buffer qas_read_buffer;
extern struct dsp_buffer qas_write_buffer;
extern char dsp_read_device[1024];
extern char dsp_write_device[1024];
extern int qas_sample_rate;
extern int64_t qas_graph_data[QAS_MON_SIZE];
extern int64_t *qas_max_data;
extern int64_t *qas_phase_data;
extern unsigned qas_logarithmic;
extern unsigned qas_bands;
extern unsigned qas_which;
extern double qas_freq;

void dsp_put_sample(struct dsp_buffer *, int16_t);
int16_t dsp_get_sample(struct dsp_buffer *);
int16_t dsp_get_monitor_sample(struct dsp_buffer *);
unsigned dsp_write_space(struct dsp_buffer *);
unsigned dsp_read_space(struct dsp_buffer *);
unsigned dsp_monitor_space(struct dsp_buffer *);

void atomic_lock();
void atomic_unlock();
void atomic_graph_lock();
void atomic_graph_unlock();
void atomic_wait();
void atomic_wakeup();

void *qas_dsp_audio_producer(void *);
void *qas_dsp_audio_analyzer(void *);
void *qas_dsp_write_thread(void *);
void *qas_dsp_read_thread(void *);
void qas_dsp_sync(void);

double fet_prescaler_double(double *);
double fet_prescaler_s64(int64_t *);
void fet_conv_16384_64(const int64_t *, const int64_t *, int64_t *);
void fet_16384_64(int64_t *);
int64_t fet_to_lin_64(int64_t);

void qas_queue_filter(qas_filter *);

#endif			/* _QAUDIOSONAR_H_ */
