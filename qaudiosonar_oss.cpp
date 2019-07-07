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

#include "qaudiosonar.h"

int	qas_sample_rate = 8000;
int	qas_source_0;
int	qas_source_1;
int	qas_output_0;
int	qas_output_1;
int	qas_freeze;
int	qas_record;
PaDeviceIndex qas_rx_device = paNoDevice;
PaDeviceIndex qas_tx_device = paNoDevice;
double qas_band_pass_filter[QAS_CORR_SIZE];
double qas_midi_level = 1LL << 62;
double qas_noise_level = 1.0;
double qas_view_decay = 0;

static struct dsp_buffer qas_read_buffer[2];
static struct dsp_buffer qas_write_buffer[2];
static double *qas_mon_buffer;
static size_t qas_mon_size;
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
	uint32_t temp;
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
	uint32_t temp;
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
	static double buffer[5][QAS_DSP_SIZE];
	static double noise[2][QAS_DSP_SIZE];
	static double temp[2][QAS_CORR_SIZE + QAS_DSP_SIZE];

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
			buffer[1][x] = noise[0][x];
			buffer[2][x] = noise[1][x];
			buffer[3][x] = temp[0][x];
			buffer[4][x] = temp[1][x];

			dsp_put_sample(&qas_write_buffer[0], buffer[qas_output_0][x]);
			dsp_put_sample(&qas_write_buffer[1], buffer[qas_output_1][x]);
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
	static double dsp_rd_data[4][QAS_CORR_SIZE];

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
				dsp_rd_data[2][x] = dsp_get_monitor_sample(&qas_write_buffer[0]);
				dsp_rd_data[3][x] = dsp_get_monitor_sample(&qas_write_buffer[1]);
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

		struct qas_corr_in_data *pin =
		    qas_corr_in_alloc(qas_mon_size + QAS_CORR_SIZE);

		atomic_lock();
		pin->sequence_number = qas_in_sequence_number++;
		atomic_unlock();

		atomic_graph_lock();
		for (size_t x = 0; x != qas_mon_size; x += QAS_CORR_SIZE) {
			size_t y = (x + qas_mon_level) % qas_mon_size;
			memcpy(pin->data + x, qas_mon_buffer + y,
			       sizeof(double) * QAS_CORR_SIZE);
		}
		atomic_graph_unlock();

		/* compute reversed audio samples */
		for (size_t x = 0; x != QAS_CORR_SIZE; x++)
			pin->data[qas_mon_size + x] = dsp_rd_audio[QAS_CORR_SIZE - 1 - x];

		qas_corr_in_insert(pin);
	}
	return (0);
}

static void *
qas_dsp_write_thread(void *)
{
	static union {
		int32_t s32[2 * QAS_DSP_SIZE];
		char c[0];
	} buffer;
	PaStreamParameters param = {};
	PaDeviceIndex currdev;
	PaStream *stream = 0;
	const PaDeviceInfo *info;
	int channels;
	PaError err;

	while (1) {
		if (stream != 0) {
			Pa_CloseStream(stream);
			stream = 0;
		}

		usleep(250000);

		atomic_lock();
		currdev = qas_tx_device;
		atomic_unlock();

		if (currdev == paNoDevice)
			continue;

		info = Pa_GetDeviceInfo(currdev);
		if (info == 0)
			continue;

		if (info->maxOutputChannels >= 2) {
			channels = 2;
		} else {
			channels = 1;
		}

		param.channelCount = channels;
		param.sampleFormat = paInt32;
		param.suggestedLatency = info->defaultHighOutputLatency;

		if (Pa_OpenStream(&stream, NULL, &param,
		    qas_sample_rate, 0, paClipOff, NULL, NULL) != paNoError)
			continue;

		if (Pa_StartStream(stream) != paNoError)
			continue;

		while (1) {
			atomic_lock();
			while (dsp_read_space(&qas_write_buffer[0]) < QAS_DSP_SIZE ||
			       dsp_read_space(&qas_write_buffer[1]) < QAS_DSP_SIZE) {
				atomic_wakeup();
				atomic_wait();
			}
			if (channels == 2) {
				for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
					buffer.s32[2*x+0] = dsp_get_sample(&qas_write_buffer[0]);
					buffer.s32[2*x+1] = dsp_get_sample(&qas_write_buffer[1]);
				}
			} else {
				for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
					buffer.s32[x] = dsp_get_sample(&qas_write_buffer[0]);
					(void) dsp_get_sample(&qas_write_buffer[1]);
				}
			}
			atomic_wakeup();
			if (currdev != qas_tx_device) {
				atomic_unlock();
				break;
			}
			atomic_unlock();

			err = Pa_WriteStream(stream, buffer.c, QAS_DSP_SIZE);
			if (err != paNoError && err != paOutputUnderflowed)
				break;
		}
	}
	return (0);
}

static void *
qas_dsp_read_thread(void *arg)
{
	static union {
		int32_t s32[2 * QAS_DSP_SIZE];
		char c[0];
	} buffer;
	PaStreamParameters param = {};
	PaDeviceIndex currdev;
	PaStream *stream = 0;
	const PaDeviceInfo *info;
	int channels;
	PaError err;

	while (1) {
		if (stream != 0) {
			Pa_CloseStream(stream);
			stream = 0;
		}
	  
		usleep(250000);

		atomic_lock();
		currdev = qas_rx_device;
		atomic_unlock();

		if (currdev == paNoDevice)
			continue;

		info = Pa_GetDeviceInfo(currdev);
		if (info == 0)
			continue;

		if (info->maxInputChannels >= 2) {
			channels = 2;
		} else {
			channels = 1;
		}

		param.channelCount = channels;
		param.sampleFormat = paInt32;
		param.suggestedLatency = info->defaultHighInputLatency;

		if (Pa_OpenStream(&stream, &param, NULL,
				  qas_sample_rate, 0, paClipOff, NULL, NULL) != paNoError)
			continue;

		if (Pa_StartStream(stream) != paNoError)
			continue;

		while (1) {
			err = Pa_ReadStream(stream, buffer.c, QAS_DSP_SIZE);
			if (err != paNoError && err != paInputOverflowed)
				break;

			atomic_lock();
			if (channels == 2) {
				for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
					dsp_put_sample(&qas_read_buffer[0], buffer.s32[2*x+0]);
					dsp_put_sample(&qas_read_buffer[1], buffer.s32[2*x+1]);
				}
			} else {
				for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
					dsp_put_sample(&qas_read_buffer[0], buffer.s32[x]);
					dsp_put_sample(&qas_read_buffer[1], 0);
				}
			}
			atomic_wakeup();
			if (currdev != qas_rx_device) {
				atomic_unlock();
				break;
			}
			atomic_unlock();
		}
	}
	return (0);
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
	pthread_create(&td, 0, &qas_dsp_write_thread, 0);
	pthread_create(&td, 0, &qas_dsp_read_thread, 0);
}
