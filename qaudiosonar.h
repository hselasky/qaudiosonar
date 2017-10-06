/*-
 * Copyright (c) 2016-2017 Hans Petter Selasky. All rights reserved.
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
#include <signal.h>

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
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QGroupBox>

#define	QAS_SAMPLE_RATE	16000
#define	QAS_MIDI_BUFSIZE 1024
#define	QAS_MUL_ORDER	9
#define	QAS_MUL_SIZE	(1U << QAS_MUL_ORDER) /* samples */
#define	QAS_BUFFER_SIZE ((QAS_SAMPLE_RATE / 8) - ((QAS_SAMPLE_RATE / 8) % QAS_MUL_SIZE)) /* samples */
#define	QAS_DSP_SIZE	((QAS_SAMPLE_RATE / 16) - ((QAS_SAMPLE_RATE / 16) % QAS_MUL_SIZE)) /* samples */
#define	QAS_MON_SIZE	((QAS_SAMPLE_RATE / 2) - ((QAS_SAMPLE_RATE / 2) % QAS_MUL_SIZE))
#define	QAS_MON_COUNT	(QAS_MON_SIZE / QAS_MUL_SIZE)
#define	QAS_BAND_SIZE	13
#define	QAS_HISTORY_SIZE (QAS_SAMPLE_RATE * 8 / QAS_MUL_SIZE)

#if (QAS_DSP_SIZE == 0 || QAS_BUFFER_SIZE == 0 || QAS_MON_SIZE == 0)
#error "Invalid parameters"
#endif

struct qas_band_info {
	double power;
	uint8_t band;
};

class qas_block_filter;
typedef TAILQ_CLASS_ENTRY(qas_block_filter) qas_block_filter_entry_t;
typedef TAILQ_CLASS_HEAD(,qas_block_filter) qas_block_filter_head_t;

class qas_block_filter {
public:
	qas_block_filter(double amp, double low_hz, double high_hz);
	~qas_block_filter() { delete descr; };
	void do_mon_block_in(const double *);
	void do_reset();
	qas_block_filter_entry_t entry;
	QString *descr;
	double power[QAS_HISTORY_SIZE];
	double t_cos[QAS_MON_SIZE];
	double t_sin[QAS_MON_SIZE];
	double t_amp;
	double t_phase;
	double freq;
	uint32_t tag;
	uint8_t iso_index;
	uint8_t band;
};

class QasBandPassBox : public QGroupBox {
	Q_OBJECT;
public:
	QasBandPassBox();
	~QasBandPassBox() {};

	QScrollBar *pSB;
	QGridLayout *grid;

signals:
	void valueChanged(int);

public slots:
	void handle_value_changed(int);
};

class QasBandWidthBox : public QGroupBox {
	Q_OBJECT;
public:
	QasBandWidthBox();
	~QasBandWidthBox() {};

	QScrollBar *pSB;
	QGridLayout *grid;

signals:
	void valueChanged(int);

public slots:
	void handle_value_changed(int);
};

class QasMainWindow;
class QasButtonMap;
class QasConfig : public QWidget {
	Q_OBJECT
public:
	QasConfig(QasMainWindow *);
	~QasConfig() { };

	QasMainWindow *mw;

	QGridLayout *gl;

	QasButtonMap *map_source_0;
	QasButtonMap *map_source_1;
	QasButtonMap *map_output_0;
	QasButtonMap *map_output_1;
	QasBandPassBox *bp_box_0;
	QasBandWidthBox *bw_box_0;

public slots:
	void handle_source_0(int);
	void handle_source_1(int);
	void handle_output_0(int);
	void handle_output_1(int);
	void handle_filter_0(int);
};

class QasBand : public QWidget {
	Q_OBJECT
public:
	QasBand(QasMainWindow *);
	~QasBand() { };
	QasMainWindow *mw;
	QTimer *watchdog;
	unsigned last_pi;
	uint8_t mapping[QAS_BAND_SIZE];
	struct qas_band_info band[QAS_HISTORY_SIZE][QAS_BAND_SIZE];

	void paintEvent(QPaintEvent *);
	void mousePressEvent(QMouseEvent *);

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

class QasRecord;
class QasRecordEntry;
typedef TAILQ_CLASS_ENTRY(QasRecordEntry) QasRecordEntry_t;
typedef TAILQ_CLASS_HEAD(,QasRecordEntry) QasRecordEntryHead_t;

class QasRecordShow : public QWidget {
	Q_OBJECT
public:
	QasRecordShow(QasRecord *);
	~QasRecordShow() { };
	QasRecord *qr;
	void paintEvent(QPaintEvent *);
	void mousePressEvent(QMouseEvent *);
	void keyPressEvent(QKeyEvent *);
};

class QasRecordEntry {
public:
	QasRecordEntry(const unsigned n) {
		pvalue = (double *)malloc(8 * n);
		pdesc = new QString [n];
		num = n;
	};
	~QasRecordEntry() {
		free(pvalue);
		delete [] pdesc;
	};
	QasRecordEntry_t entry;
	QString comment;
	QString *pdesc;
	double *pvalue;
	unsigned num;
};

class QasRecord : public QWidget {
	Q_OBJECT
public:
	QasRecord();
	~QasRecord();

	QasRecordEntryHead_t head;
	QPoint select;

	QGridLayout *gl;
	QPlainTextEdit *pEdit;
	QPushButton *pButReset;
	QPushButton *pButToggle;
	QPushButton *pButInsert;
	QScrollBar *pSB;
	QSpinBox *pSpin;
	QasRecordShow *pShow;
	QLineEdit *pLabel;
	int do_record;

	void insert_entry(QasRecordEntry *);

public slots:
	void handle_reset();
	void handle_toggle();
	void handle_insert();
	void handle_slider(int);
};

class QasMainWindow : public QWidget {
	Q_OBJECT
public:
	QasMainWindow();
	~QasMainWindow() { };
	void update_sb();
	void update_qr();

	QasConfig *qc;
	QasRecord *qr;
	QGridLayout *gl;
	QScrollBar *sb;
	QScrollBar *sb_zoom;
	QasBand *qb;
	QasGraph *qg;
	QLineEdit *led_dsp_read;
	QLineEdit *led_dsp_write;
	QLineEdit *led_midi_write;
	QSpinBox *spn;

public slots:
	void handle_apply();
	void handle_reset();
	void handle_del_all();
	void handle_add_log();
	void handle_add_lin();
	void handle_add_piano();
	void handle_tog_freeze();
	void handle_slider(int);
	void handle_show_record();
	void handle_config();
};

struct dsp_buffer {
	int16_t buffer[QAS_BUFFER_SIZE];
	unsigned in_off;
	unsigned mon_off;
	unsigned out_off;
};

extern qas_block_filter_head_t qas_filter_head;
extern struct dsp_buffer qas_read_buffer[2];
extern struct dsp_buffer qas_write_buffer[2];
extern char dsp_read_device[1024];
extern char dsp_write_device[1024];
extern char midi_write_device[1024];
extern int qas_sample_rate;
extern int qas_source_0;
extern int qas_source_1;
extern int qas_output_0;
extern int qas_output_1;
extern int qas_freeze;
extern double qas_graph_data[QAS_MON_SIZE];
extern double qas_band_pass_filter[QAS_MUL_SIZE];
extern double qas_band_power[QAS_HISTORY_SIZE][QAS_BAND_SIZE];
extern double dsp_rd_mon_filter[QAS_MON_COUNT][QAS_MUL_SIZE];
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
void *qas_midi_write_thread(void *);
void qas_midi_key_send(uint8_t, uint8_t);

void qas_dsp_sync(void);

void qas_x3_multiply_double(double *, double *, double *, const size_t);

void qas_queue_block_filter(qas_block_filter *, qas_block_filter_head_t *);

#endif			/* _QAUDIOSONAR_H_ */
