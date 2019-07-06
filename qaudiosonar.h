/*-
 * Copyright (c) 2016-2019 Hans Petter Selasky. All rights reserved.
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

#include <sys/ioctl.h>
#include <sys/filio.h>
#if defined(__FreeBSD__) || defined(__linux__)
#include <sys/soundcard.h>
#endif
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
#include <QImage>
#include <QAudio>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioInput>
#include <QAudioOutput>

#define	QAS_SAMPLES_MAX	48000
#define	QAS_MIDI_BUFSIZE 1024
#define	QAS_MUL_ORDER	10
#define	QAS_MUL_SIZE	(1U << QAS_MUL_ORDER) /* samples */
#define	QAS_BUFFER_SIZE ((QAS_SAMPLES_MAX / 8) - ((QAS_SAMPLES_MAX / 8) % QAS_MUL_SIZE)) /* samples */
#define	QAS_DSP_SIZE	((QAS_SAMPLES_MAX / 16) - ((QAS_SAMPLES_MAX / 16) % QAS_MUL_SIZE)) /* samples */
#define	QAS_WAVE_STEP (1U << QAS_WAVE_STEP_LOG2)
#define	QAS_WAVE_STEP_LOG2 8

#define	QAS_FREQ_TABLE_ROUNDED(band) \
    ((double)(((int64_t)(1000.0 * qas_freq_table[band])) / 1000.0))

#if (QAS_DSP_SIZE == 0 || QAS_BUFFER_SIZE == 0)
#error "Invalid parameters"
#endif

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

class QasMidilevelBox : public QGroupBox {
	Q_OBJECT;
public:
	QasMidilevelBox();
	~QasMidilevelBox() {};

	QScrollBar *pSB;
	QGridLayout *grid;

signals:
	void valueChanged(int);

public slots:
	void handle_value_changed(int);
};

class QasNoiselevelBox : public QGroupBox {
	Q_OBJECT;
public:
	QasNoiselevelBox();
	~QasNoiselevelBox() {};

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
	QasMidilevelBox *ml_box_0;
	QasNoiselevelBox *nl_box_0;

public slots:
	void handle_source_0(int);
	void handle_source_1(int);
	void handle_output_0(int);
	void handle_output_1(int);
	void handle_filter_0(int);
};

class QasView : public QWidget {
	Q_OBJECT
public:
	QasView(QasMainWindow *);
	~QasView() { };

	QasMainWindow *mw;

	QGridLayout *gl;

	QasButtonMap *map_decay_0;

public slots:
	void handle_decay_0(int);
};

class QasBand : public QWidget {
	Q_OBJECT
public:
	enum { BAND_MAX = 12 * QAS_WAVE_STEP_LOG2 };
	QasBand(QasMainWindow *);
	~QasBand() { };
	QasMainWindow *mw;
	QTimer *watchdog;

	QString getText(QMouseEvent *);

	void paintEvent(QPaintEvent *);
	void mousePressEvent(QMouseEvent *);
	void mouseMoveEvent(QMouseEvent *);

public slots:
	void handle_watchdog();
};

class QasGraph : public QWidget {
	Q_OBJECT
public:
	QasGraph(QasMainWindow *);
	~QasGraph();
	QasMainWindow *mw;
	QTimer *watchdog;
	double *mon_index;

	QString getText(QMouseEvent *);

	void paintEvent(QPaintEvent *);
	void mousePressEvent(QMouseEvent *);
	void mouseMoveEvent(QMouseEvent *);

public slots:
	void handle_watchdog();
};

class QasAudioIO : public QIODevice
{
	Q_OBJECT
public:
	QasAudioIO(QasMainWindow *_mw) {
		mw = _mw;
		audio_input = 0;
		audio_output = 0;
		open(QIODevice::ReadWrite);
	};
	qint64 readData(char *data, qint64 maxlen) override;
	qint64 writeData(const char *data, qint64 len) override;
	bool try_format(int channels, int bits, bool isOutput);

	void stop() {
		delete audio_input;
		audio_input = 0;
		delete audio_output;
		audio_output = 0;
	};

	QAudioInput *audio_input;
	QAudioOutput *audio_output;
	QAudioDeviceInfo info;
	QAudioFormat format;

private:
	QasMainWindow *mw;

public slots:
	void handle_audio_state(QAudio::State);
};

class QasMainWindow : public QWidget {
	Q_OBJECT
public:
	QasMainWindow();
	~QasMainWindow() { };

	QasConfig *qc;
	QasView *qv;
	QGridLayout *gl;
	QGridLayout *glb;
	QScrollBar *sb_zoom;
	QLabel *lbl_max;
	QWidget *qbw;
	QasBand *qb;
	QasGraph *qg;
	QLineEdit *led_dsp_read;
	QLineEdit *led_dsp_write;
	QLineEdit *led_midi_write;
	QPlainTextEdit *edit;
	QSpinBox *tuning;
	QPushButton *but_dsp_rx;
	QPushButton *but_dsp_tx;
	QPushButton *but_midi_tx;

	QasAudioIO *audio_input;
  	QasAudioIO *audio_output;

signals:
	void handle_append_text(const QString);

public slots:
	void handle_apply();
	void handle_reset();
	void handle_tog_freeze();
	void handle_tog_record();
	void handle_slider(int);
	void handle_config();
	void handle_view();
	void handle_tuning();
	void handle_dsp_rx();
	void handle_dsp_tx();
};

/* ============== GENERIC SUPPORT ============== */

extern int qas_num_workers;
extern size_t qas_in_sequence_number;
extern size_t qas_out_sequence_number;
extern size_t qas_window_size;
extern int qas_sample_rate;
extern int qas_source_0;
extern int qas_source_1;
extern int qas_output_0;
extern int qas_output_1;
extern int qas_freeze;
extern int qas_record;
extern double qas_view_decay;
extern QasMainWindow *qas_mw;
extern double qas_low_octave;
extern const double qas_base_freq;

void atomic_lock();
void atomic_unlock();
void atomic_graph_lock();
void atomic_graph_unlock();
void atomic_wait();
void atomic_wakeup();

/* ============== MULTIPLY SUPPORT ============== */

void qas_x3_multiply_double(double *, double *, double *, const size_t);

/* ============== WAVE SUPPORT ============== */

extern double *qas_cos_table;
extern double *qas_sin_table;
extern double *qas_freq_table;
extern uint8_t *qas_iso_table;
extern size_t qas_num_bands;
extern QString *qas_descr_table;

struct qas_wave_job {
	TAILQ_ENTRY(qas_wave_job) entry;
	size_t data_offset;
	size_t band_start;
	struct qas_corr_out_data *data;
};

extern struct qas_wave_job *qas_wave_job_alloc();
extern void qas_wave_job_insert(struct qas_wave_job *);
extern struct qas_wave_job *qas_wave_job_dequeue();
extern void qas_wave_job_free(qas_wave_job *);
extern void qas_wave_signal();
extern void qas_wave_wait();
extern void qas_wave_lock();
extern void qas_wave_unlock();
extern void qas_wave_init();

/* ============== CORRELATION SUPPORT ============== */

#define	QAS_CORR_SIZE QAS_MUL_SIZE

struct qas_corr_in_data {
	TAILQ_ENTRY(qas_corr_in_data) entry;
	size_t sequence_number;
	double data[];
};

struct qas_corr_out_data {
	TAILQ_ENTRY(qas_corr_in_data) entry;
	size_t sequence_number;
	size_t refcount;
	size_t state;
#define	QAS_STATE_1ST_SCAN 0
#define	QAS_STATE_2ND_SCAN 1
	double data_array[];
};

extern double *qas_mon_decay;
extern struct qas_corr_in_data *qas_corr_in_alloc(size_t);
extern void qas_corr_in_free(struct qas_corr_in_data *);
extern struct qas_corr_out_data *qas_corr_out_alloc(size_t);
extern void qas_corr_out_free(struct qas_corr_out_data *);
extern void qas_corr_in_insert(struct qas_corr_in_data *);
extern struct qas_corr_in_data *qas_corr_in_job_dequeue();
extern void qas_corr_signal();
extern void qas_corr_wait();
extern void qas_corr_lock();
extern void qas_corr_unlock();
extern void qas_corr_init();

/* ============== DISPLAY SUPPORT ============== */

extern double *qas_display_data;
extern double *qas_display_band;
extern size_t qas_display_hist_max;	/* power of two */

extern void qas_display_job_insert(struct qas_wave_job *);
extern struct qas_wave_job *qas_display_job_dequeue();
extern void qas_display_signal();
extern void qas_display_wait();
extern void qas_display_lock();
extern void qas_display_unlock();
extern void qas_display_init();
extern double *qas_display_get_line(size_t);
extern size_t qas_display_width();
extern double *qas_display_get_band(size_t);
extern size_t qas_display_band_width();
extern size_t qas_display_height();
extern size_t qas_display_lag();

/* ============== ISO SUPPORT ============== */

#define	QAS_STANDARD_AUDIO_BANDS 31

extern const double qas_iso_freq_table[QAS_STANDARD_AUDIO_BANDS];
extern uint8_t qas_find_iso(double cf);

/* ============== OSS DSP SUPPORT ============== */

struct dsp_buffer {
	double buffer[QAS_BUFFER_SIZE];
	unsigned in_off;
	unsigned mon_off;
	unsigned out_off;
};

extern char dsp_read_device[1024];
extern char dsp_write_device[1024];
extern double qas_band_pass_filter[QAS_CORR_SIZE];

extern void qas_dsp_init();
extern void dsp_put_sample(struct dsp_buffer *, double);
extern double dsp_get_sample(struct dsp_buffer *);
extern double dsp_get_monitor_sample(struct dsp_buffer *);
extern unsigned dsp_write_space(struct dsp_buffer *);
extern unsigned dsp_read_space(struct dsp_buffer *);
extern unsigned dsp_monitor_space(struct dsp_buffer *);
extern void qas_dsp_sync(void);

/* ============== MIDI SUPPORT ============== */

extern char midi_write_device[1024];
extern double qas_midi_level;
extern double qas_noise_level;

extern void qas_midi_init();
extern void qas_midi_key_send(uint8_t, uint8_t, uint8_t, uint8_t);

#endif			/* _QAUDIOSONAR_H_ */
