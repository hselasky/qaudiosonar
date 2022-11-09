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

#include "qaudiosonar_mainwindow.h"
#include "qaudiosonar_configdlg.h"

#define	QAS_LAT 0.016 /* seconds */
int	qas_sample_rate = 16000;
int	qas_source_0;
int	qas_source_1;
int	qas_output_0;
int	qas_output_1;
int	qas_freeze;
int	qas_sensitivity;
int	qas_record;
double qas_band_pass_filter[QAS_CORR_SIZE];
double qas_noise_level = 1.0;
double qas_view_decay = 0;
double qas_phase_curr;
double qas_phase_step;
size_t qas_mon_size;

static struct dsp_buffer qas_read_buffer[2];
static struct dsp_buffer qas_write_buffer[2];
static double *qas_mon_buffer;
static size_t qas_mon_level;

void
dsp_put_sample(struct dsp_buffer *dbuf, double sample)
{
	dbuf->buffer[dbuf->in_off++] = sample;
	dbuf->in_off %= QAS_BUFFER_SIZE;
}

double
dsp_get_sample(struct dsp_buffer *dbuf)
{
	double retval;

	retval = dbuf->buffer[dbuf->out_off++];
	dbuf->out_off %= QAS_BUFFER_SIZE;
	return (retval);
}

double
dsp_get_monitor_sample(struct dsp_buffer *dbuf)
{
	double retval;

	retval = dbuf->buffer[dbuf->mon_off++];
	dbuf->mon_off %= QAS_BUFFER_SIZE;
	return (retval);
}

unsigned
dsp_write_space(struct dsp_buffer *dbuf)
{
	return ((QAS_BUFFER_SIZE - 1 + dbuf->out_off -
		 dbuf->in_off) % QAS_BUFFER_SIZE);
}

static unsigned
dsp_write_monitor_space(struct dsp_buffer *dbuf)
{
	unsigned a = ((QAS_BUFFER_SIZE - 1 + dbuf->out_off -
		 dbuf->in_off) % QAS_BUFFER_SIZE);
	unsigned b = ((QAS_BUFFER_SIZE - 1 + dbuf->mon_off -
		 dbuf->in_off) % QAS_BUFFER_SIZE);
	if (a > b)
		return (b);
	else
		return (a);
}

unsigned
dsp_read_space(struct dsp_buffer *dbuf)
{
	return ((QAS_BUFFER_SIZE + dbuf->in_off -
	    dbuf->out_off) % QAS_BUFFER_SIZE);
}

unsigned
dsp_monitor_space(struct dsp_buffer *dbuf)
{
	return ((QAS_BUFFER_SIZE + dbuf->in_off -
	    dbuf->mon_off) % QAS_BUFFER_SIZE);
}

static int32_t
qas_brown_noise(void)
{
	int32_t temp;
	static uint32_t noise_rem = 1;
	const uint32_t prime = 0xFFFF1D;

	if (noise_rem & 1)
		noise_rem += prime;

	noise_rem /= 2;

	temp = noise_rem;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000)
		temp |= (-0x800000);
	return (temp * qas_noise_level);
}

static int32_t
qas_white_noise(void)
{
	int32_t temp;
	static uint32_t noise_rem;

	/* NOTE: White-noise formula used by ZynaddSubFX */

	noise_rem = noise_rem * 1103515245 + 12345;

	temp = noise_rem >> 8;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000)
		temp |= (-0x800000);
	return (temp * qas_noise_level);
}

static void *
qas_dsp_audio_producer(void *arg)
{
	static double noise[2][QAS_DSP_SIZE];
	static double cosinus[QAS_DSP_SIZE];
	static double temp[2][QAS_CORR_SIZE + QAS_DSP_SIZE];
	double buffer[6] = {};

	atomic_lock();
	while (1) {
		while (dsp_write_monitor_space(&qas_write_buffer[0]) < QAS_DSP_SIZE ||
		       dsp_write_monitor_space(&qas_write_buffer[1]) < QAS_DSP_SIZE)
			atomic_wait();
		atomic_unlock();

		for (size_t x = 0; x != QAS_DSP_SIZE; x++) {
			/* generate noise */
			noise[0][x] = qas_brown_noise();
			noise[1][x] = qas_white_noise();
			/* generate cosinus */
			qas_phase_curr += qas_phase_step;
			if (qas_phase_curr >= 2.0 * M_PI)
				qas_phase_curr -= 2.0 * M_PI;
			/* avoid computing cosinus when not needed */
			if (qas_output_0 == 5 || qas_output_1 == 5)
				cosinus[x] = qas_noise_level * cos(qas_phase_curr) * (1 << 24);
		}

		for (size_t x = 0; x != QAS_CORR_SIZE; x++) {
			/* shift down filter */
			temp[0][x] = temp[0][x + QAS_DSP_SIZE];
			temp[1][x] = temp[1][x + QAS_DSP_SIZE];
		}

		for (size_t x = 0; x != QAS_DSP_SIZE; x++) {
			/* zero rest of filter */
			temp[0][QAS_CORR_SIZE + x] = 0;
			temp[1][QAS_CORR_SIZE + x] = 0;
		}

		for (size_t x = 0; x != QAS_DSP_SIZE; x += QAS_CORR_SIZE) {
			qas_x3_multiply_double(qas_band_pass_filter, noise[0] + x, temp[0] + x, QAS_CORR_SIZE);
			qas_x3_multiply_double(qas_band_pass_filter, noise[1] + x, temp[1] + x, QAS_CORR_SIZE);
		}

		atomic_lock();
		for (size_t x = 0; x != QAS_DSP_SIZE; x++) {
			buffer[1] = noise[0][x];
			buffer[2] = noise[1][x];
			buffer[3] = temp[0][x];
			buffer[4] = temp[1][x];
			buffer[5] = cosinus[x];

			dsp_put_sample(&qas_write_buffer[0], buffer[qas_output_0]);
			dsp_put_sample(&qas_write_buffer[1], buffer[qas_output_1]);
		}
		atomic_wakeup();
	}
	atomic_unlock();
	return (0);
}

void
qas_dsp_sync(void)
{
	atomic_graph_lock();
	memset(qas_mon_buffer, 0, sizeof(double) * qas_mon_size);
	memset(qas_mon_decay, 0, sizeof(double) * qas_window_size);
	atomic_graph_unlock();

	atomic_lock();
	while (dsp_read_space(&qas_read_buffer[0]))
		dsp_get_sample(&qas_read_buffer[0]);
	while (dsp_read_space(&qas_read_buffer[1]))
		dsp_get_sample(&qas_read_buffer[1]);
	atomic_wakeup();
	atomic_unlock();
}

static void *
qas_dsp_audio_analyzer(void *arg)
{
	double *dsp_rd_audio;
	double *dsp_rd_monitor;
	static double dsp_rd_data[6][QAS_CORR_SIZE];

	while (1) {
		atomic_lock();
		do {
			while (dsp_monitor_space(&qas_write_buffer[0]) < QAS_CORR_SIZE ||
			       dsp_monitor_space(&qas_write_buffer[1]) < QAS_CORR_SIZE ||
			       dsp_read_space(&qas_read_buffer[0]) < QAS_CORR_SIZE ||
			       dsp_read_space(&qas_read_buffer[1]) < QAS_CORR_SIZE) {
					atomic_wait();
			}

			for (unsigned x = 0; x != QAS_CORR_SIZE; x++) {
				dsp_rd_data[0][x] = dsp_get_sample(&qas_read_buffer[0]);
				dsp_rd_data[1][x] = dsp_get_sample(&qas_read_buffer[1]);
				dsp_rd_data[2][x] = dsp_rd_data[0][x] + dsp_rd_data[1][x];
				dsp_rd_data[3][x] = dsp_get_monitor_sample(&qas_write_buffer[0]);
				dsp_rd_data[4][x] = dsp_get_monitor_sample(&qas_write_buffer[1]);
				dsp_rd_data[5][x] = dsp_rd_data[3][x] + dsp_rd_data[4][x];
			}
			atomic_wakeup();
		} while (qas_freeze);
		dsp_rd_monitor = dsp_rd_data[qas_source_0];
		dsp_rd_audio = dsp_rd_data[qas_source_1];
		atomic_unlock();

		/* copy monitor samples */
		atomic_graph_lock();
		memcpy(qas_mon_buffer + qas_mon_level,
		    dsp_rd_monitor, QAS_CORR_SIZE * sizeof(qas_mon_buffer[0]));
		atomic_graph_unlock();

		/* update counters */
		qas_mon_level += QAS_CORR_SIZE;
		qas_mon_level %= qas_mon_size;

		struct qas_corr_data *ptr = qas_corr_alloc();

		atomic_lock();
		ptr->sequence_number = qas_in_sequence_number++;
		atomic_unlock();

		atomic_graph_lock();
		for (size_t x = 0; x != qas_mon_size; x += QAS_CORR_SIZE) {
			size_t y = (x + qas_mon_level) % qas_mon_size;
			memcpy(ptr->monitor_data + x, qas_mon_buffer + y,
			       sizeof(double) * QAS_CORR_SIZE);
		}
		atomic_graph_unlock();

		/* compute reversed audio samples */
		for (size_t x = 0; x != QAS_CORR_SIZE; x++)
			ptr->input_data[x] = dsp_rd_audio[QAS_CORR_SIZE - 1 - x];

		qas_corr_insert(ptr);
	}
	return (0);
}

Q_DECL_EXPORT void
qas_sound_process(float *pl, float *pr, size_t num)
{
	static float input_l[QAS_SAMPLE_RATE / 8000];
	static float input_r[QAS_SAMPLE_RATE / 8000];
	static float output_l[QAS_SAMPLE_RATE / 8000];
	static float output_r[QAS_SAMPLE_RATE / 8000];
	static uint8_t remainder;
	const uint8_t factor = QAS_SAMPLE_RATE / qas_sample_rate;

	atomic_lock();

	while (num-- != 0) {
		if (remainder < factor) {
			input_l[remainder] = *pl;
			*pl = output_l[remainder];
			pl++;
			input_r[remainder] = *pr;
			*pr = output_r[remainder];
			pr++;
			remainder++;
		}

		if (remainder == factor) {
			float s_l = 0;
			float s_r = 0;

			/* low pass input buffer, if any */
			for (uint8_t x = 0; x != factor; x++) {
				s_l += input_l[x];
				s_r += input_r[x];
			}

			s_l /= (float)factor;
			s_r /= (float)factor;

			if (dsp_write_space(&qas_read_buffer[0]) != 0)
				dsp_put_sample(&qas_read_buffer[0], (float)0x7FFFFF00 * s_l);
			if (dsp_write_space(&qas_read_buffer[1]) != 0)
				dsp_put_sample(&qas_read_buffer[1], (float)0x7FFFFF00 * s_r);

			if (dsp_read_space(&qas_write_buffer[0]) != 0)
				s_l = dsp_get_sample(&qas_write_buffer[0]) / (float)0x7FFFFF00;
			else
				s_l = 0;

			if (dsp_read_space(&qas_write_buffer[1]) != 0)
				s_r = dsp_get_sample(&qas_write_buffer[1]) / (float)0x7FFFFF00;
			else
				s_r = 0;

			/* upsample output buffer, if any */
			for (uint8_t x = 0; x != factor; x++) {
				output_l[x] =
				    output_l[factor - 1] * (float)(factor - 1 - x) + s_l * (float)(x + 1);
				output_l[x] /= (float)factor;
				output_r[x] =
				    output_r[factor - 1] * (float)(factor - 1 - x) + s_r * (float)(x + 1);
				output_r[x] /= (float)factor;
			}
			remainder = 0;
		}
	}
	atomic_wakeup();
	atomic_unlock();
}

void
qas_dsp_init()
{
	pthread_t td;

	qas_mon_size = qas_window_size + QAS_CORR_SIZE;
	qas_mon_buffer = (double *)malloc(sizeof(double) * qas_mon_size);
	memset(qas_mon_buffer, 0, sizeof(double) * qas_mon_size);

	pthread_create(&td, 0, &qas_dsp_audio_producer, 0);
	pthread_create(&td, 0, &qas_dsp_audio_analyzer, 0);

#ifdef HAVE_JACK_AUDIO
	if (qas_sound_init("qaudiosonar", true)) {
		QMessageBox::information(qas_mw, QObject::tr("NO AUDIO"),
		    QObject::tr("Cannot connect to JACK server or \n"
				"sample rate is different from %1Hz or \n"
				"latency is too high").arg(QAS_SAMPLE_RATE));
	}
	qas_mw->w_config->audio_dev.refreshStatus();
#endif

#ifdef HAVE_MAC_AUDIO
	/* setup MIDI first */
	qas_midi_init("qaudiosonar");

	if (qas_sound_init(0, 0)) {
		QMessageBox::information(qas_mw, QObject::tr("NO AUDIO"),
		    QObject::tr("Cannot connect to audio subsystem.\n"
				"Check that you have an audio device connected and\n"
				"that the sample rate is set to %1Hz.").arg(QAS_SAMPLE_RATE));
	}
	qas_mw->w_config->audio_dev.refreshStatus();
#endif

#ifdef HAVE_ASIO_AUDIO
	if (qas_sound_init(0, 0)) {
		QMessageBox::information(qas_mw, QObject::tr("NO AUDIO"),
		    QObject::tr("Cannot connect to ASIO subsystem or \n"
				"sample rate is different from %1Hz or \n"
				"buffer size is different from 96 samples.").arg(QAS_SAMPLE_RATE));
	}
	qas_mw->w_config->audio_dev.refreshStatus();
#endif
}
