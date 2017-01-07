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
#include <QSpinBox>

#define	QAS_SAMPLE_RATE	48000
#define	QAS_FET_SIZE	0x4000
#define	QAS_FET_PRIME	0x42000001LL
#define	QAS_WINDOW_SIZE ((QAS_FET_SIZE / 2) & ~3)
#define	QAS_BUFFER_SIZE (2 * QAS_WINDOW_SIZE)	/* samples */
#define	QAS_MON_SIZE	(4 * QAS_FET_SIZE)
#define	QAS_BAND_SIZE	13
#define	QAS_HISTORY_SIZE (QAS_SAMPLE_RATE * 8 / QAS_WINDOW_SIZE)

class qas_block_filter;
typedef TAILQ_CLASS_ENTRY(qas_block_filter) qas_block_filter_entry_t;
typedef TAILQ_CLASS_HEAD(,qas_block_filter) qas_block_filter_head_t;

class qas_block_filter {
public:
	qas_block_filter(double amp, double low_hz, double high_hz);
	~qas_block_filter() { delete descr; };
	void do_block(double, int64_t *, int64_t *);
	void do_reset();
	qas_block_filter_entry_t entry;
	QString *descr;
	double prescaler;
	double filter_lin[QAS_FET_SIZE];
	int64_t filter_fast[QAS_FET_SIZE];
	int64_t output[2][QAS_FET_SIZE];
	int64_t power[QAS_HISTORY_SIZE];
	int64_t power_ref;
	double freq;
	uint32_t tag;
	uint8_t toggle;
	uint8_t band;
};

class qas_wave_filter;
typedef TAILQ_CLASS_ENTRY(qas_wave_filter) qas_wave_filter_entry_t;
typedef TAILQ_CLASS_HEAD(,qas_wave_filter) qas_wave_filter_head_t;

class qas_wave_filter {
public:
	qas_wave_filter(double freq);
	~qas_wave_filter() { };
	void do_block_in(int64_t *, unsigned);
	void do_block_out(int64_t *, unsigned);
	void do_flush_in();
	void do_flush_out();
	void do_reset();

	qas_wave_filter_entry_t entry;

	double t_cos[QAS_WINDOW_SIZE];
	double t_sin[QAS_WINDOW_SIZE];

	double freq;

	double power_in;
	double power_out;

	double s_cos_in;
	double s_sin_in;

	double s_cos_out;
	double s_sin_out;
};

class QasMainWindow;
class QasBand : public QWidget {
	Q_OBJECT
public:
	QasBand(QasMainWindow *);
	~QasBand() { };
	QasMainWindow *mw;
	QTimer *watchdog;
	unsigned last_pi;
	void paintEvent(QPaintEvent *);

public slots:
	void handle_watchdog();
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
	void update_sb();

	QGridLayout *gl;
	QScrollBar *sb;
	QScrollBar *sb_zoom;
	QasBand *qb;
	QasGraph *qg;
	QLineEdit *led_dsp_read;
	QLineEdit *led_dsp_write;
	QSpinBox *spn;

public slots:
	void handle_apply();
	void handle_reset();
	void handle_del_all();
	void handle_add_iso();
	void handle_add_log();
	void handle_add_lin();
	void handle_add_piano();
	void handle_tog_mute();
	void handle_tog_freeze();
	void handle_tog_noise();
	void handle_set_profile();
	void handle_slider(int);
};

struct dsp_buffer {
	int16_t buffer[QAS_BUFFER_SIZE];
	unsigned in_off;
	unsigned mon_off;
	unsigned out_off;
};

extern qas_block_filter_head_t qas_filter_head;
extern qas_wave_filter_head_t qas_wave_head;
extern struct dsp_buffer qas_read_buffer;
extern struct dsp_buffer qas_write_buffer;
extern char dsp_read_device[1024];
extern char dsp_write_device[1024];
extern int qas_sample_rate;
extern int qas_mute;
extern int qas_noise_type;
extern int qas_freeze;
extern int64_t qas_graph_data[QAS_MON_SIZE];
extern int64_t qas_band_power[QAS_HISTORY_SIZE][QAS_BAND_SIZE];
extern unsigned qas_power_index;

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
void atomic_filter_lock();
void atomic_filter_unlock();
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

void qas_queue_block_filter(qas_block_filter *, qas_block_filter_head_t *);
void qas_queue_wave_filter(qas_wave_filter *, qas_wave_filter_head_t *);

#endif			/* _QAUDIOSONAR_H_ */
