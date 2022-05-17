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
#include <QThread>
#include <QScrollBar>
#include <QSpinBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QImage>
#include <QSlider>
#include <QStackedWidget>
#include <QMessageBox>

#define	QAS_WINDOW_TITLE	"Quick Audio Sonar v1.8.0"
#define	QAS_WINDOW_ICON		":/qaudiosonar.png"

#define	QAS_SAMPLE_RATE 48000	/* HZ */
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

#if (QAS_BUFFER_SIZE == 0)
#error "Invalid QAS_BUFFER_SIZE is zero"
#endif

#if (QAS_DSP_SIZE == 0)
#error "Invalid QAS_DSP_SIZE is zero"
#endif

#define	QAS_NO_SIGNAL(a,b) do {	\
  a.blockSignals(true);		\
  a.b;				\
  a.blockSignals(false);	\
} while (0)

/* ============== GENERIC SUPPORT ============== */

class QasButtonMap;
class QasMainWindow;
class QasConfigDlg;
class QasSpectrum;

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
extern int qas_sensitivity;
extern double qas_view_decay;
extern QasMainWindow *qas_mw;
extern double qas_low_octave;
extern const double qas_base_freq;
extern size_t qas_mon_size;

void atomic_lock();
void atomic_unlock();
void atomic_graph_lock();
void atomic_graph_unlock();
void atomic_wait();
void atomic_wakeup();

/* ============== MULTIPLY SUPPORT ============== */

void qas_x3_multiply_double(double *, double *, double *, const size_t);

/* ============== WAVE SUPPORT ============== */

extern double qas_tuning;
extern double *qas_freq_table;
extern uint8_t *qas_iso_table;
extern size_t qas_num_bands;
extern QString *qas_descr_table;

struct qas_wave_job {
	TAILQ_ENTRY(qas_wave_job) entry;
	size_t band_start;
	struct qas_corr_data *data;
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

struct qas_corr_data {
	TAILQ_ENTRY(qas_corr_data) entry;
	size_t sequence_number;
	size_t refcount;
	size_t state;
#define	QAS_STATE_1ST_SCAN 0
#define	QAS_STATE_2ND_SCAN 1
	double *monitor_data;
	double *input_data;
	double *corr_data;
	double *band_data;
	double internal_data[];
};

extern double *qas_mon_decay;
extern struct qas_corr_data *qas_corr_alloc(void);
extern void qas_corr_free(struct qas_corr_data *);
extern void qas_corr_insert(struct qas_corr_data *);
extern struct qas_corr_data *qas_corr_job_dequeue();
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

extern double qas_noise_level;

extern void qas_midi_init();
extern void qas_midi_key_send(uint8_t, uint8_t, uint8_t, uint8_t);
extern void qas_midi_delay_send(uint8_t);

/* ============== FTT SUPPORT ============== */

extern double qas_ftt_cos(double);
extern double qas_ftt_sin(double);

/* ============== SOUND APIs ============== */

extern void qas_sound_rescan();
extern bool qas_sound_init(const char *, bool);
extern void qas_sound_uninit();
extern int qas_sound_toggle_buffer_samples(int);
extern QString qas_sound_get_device_name(int);
extern int qas_sound_set_input_device(int);
extern int qas_sound_set_output_device(int);
extern int qas_sound_set_input_channel(int, int);
extern int qas_sound_set_output_channel(int, int);
extern bool qas_sound_is_input_device(int);
extern bool qas_sound_is_output_device(int);
extern int qas_sound_max_input_channel();
extern int qas_sound_max_output_channel();
extern int qas_sound_max_devices();

extern void qas_sound_get_input_status(QString &);
extern void qas_sound_get_output_status(QString &);
extern void qas_sound_process(float *, float *, size_t);
extern int qas_midi_process(uint8_t *ptr);

#endif			/* _QAUDIOSONAR_H_ */
