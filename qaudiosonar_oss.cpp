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

#include "qaudiosonar.h"

qas_block_filter_head_t qas_filter_head = TAILQ_HEAD_INITIALIZER(qas_filter_head);
qas_wave_filter_head_t qas_wave_head = TAILQ_HEAD_INITIALIZER(qas_wave_head);
int	qas_sample_rate = 48000;
int	qas_mute;
int	qas_freeze;
int	qas_noise_type;
unsigned qas_power_index;
struct dsp_buffer qas_read_buffer;
struct dsp_buffer qas_write_buffer;
char	dsp_read_device[1024];
char	dsp_write_device[1024];
int64_t qas_graph_data[QAS_MON_SIZE];

void
dsp_put_sample(struct dsp_buffer *dbuf, int16_t sample)
{
	dbuf->buffer[dbuf->in_off++] = sample;
	dbuf->in_off %= QAS_BUFFER_SIZE;
}

int16_t
dsp_get_sample(struct dsp_buffer *dbuf)
{
	int16_t retval;

	retval = dbuf->buffer[dbuf->out_off++];
	dbuf->out_off %= QAS_BUFFER_SIZE;
	return (retval);
}

int16_t
dsp_get_monitor_sample(struct dsp_buffer *dbuf)
{
	int16_t retval;

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
qas_noise(void)
{
	uint32_t temp;

	if (qas_noise_type == 0) {
		static uint32_t noise_rem = 1;
		const uint32_t prime = 0xFFFF1D;

		if (noise_rem & 1)
			noise_rem += prime;

		noise_rem /= 2;

		temp = noise_rem;
	} else {
		static uint32_t noise_rem;

		/* NOTE: White-noise formula used by ZynaddSubFX */

		noise_rem = noise_rem * 1103515245 + 12345;

		temp = noise_rem & 0xFFFFFF;
	}

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000)
		temp |= (-0x800000);
	return (temp);
}

void *
qas_dsp_audio_producer(void *arg)
{
	atomic_lock();
	while (1) {
		while (dsp_write_monitor_space(&qas_write_buffer) < QAS_WINDOW_SIZE)
			atomic_wait();
		for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++)
			dsp_put_sample(&qas_write_buffer, qas_noise() >> 10);
		atomic_wakeup();
	}
	atomic_unlock();
	return (NULL);
}

void
qas_dsp_sync(void)
{
	qas_block_filter *f;
	qas_wave_filter *w;

	atomic_filter_lock();
	TAILQ_FOREACH(f, &qas_filter_head, entry)
		f->do_reset();
	TAILQ_FOREACH(w, &qas_wave_head, entry)
		w->do_reset();
	atomic_filter_unlock();

	atomic_graph_lock();
	memset(qas_graph_data, 0, sizeof(qas_graph_data));
	atomic_graph_unlock();

	atomic_lock();
	while (dsp_read_space(&qas_read_buffer))
		dsp_get_sample(&qas_read_buffer);
	atomic_wakeup();
	atomic_unlock();
}

void *
qas_dsp_audio_analyzer(void *arg)
{
	static int64_t dsp_rd_audio[QAS_WINDOW_SIZE];
	static int64_t dsp_rd_filtered[QAS_WINDOW_SIZE];
	static int16_t dsp_rd_monitor[QAS_MON_SIZE];
	static int64_t dsp_rd_monitor_temp[QAS_MON_SIZE];
	static int64_t dsp_rd_fet_array[QAS_FET_SIZE];
	static int64_t dsp_rd_curr_array[QAS_FET_SIZE];

	double prescaler;
	unsigned x,y;

	while (1) {
		atomic_lock();
		do {
			while (dsp_monitor_space(&qas_write_buffer) < QAS_WINDOW_SIZE ||
			       dsp_read_space(&qas_read_buffer) < QAS_WINDOW_SIZE) {
				atomic_wait();
			}

			for (x = 0; x != (QAS_MON_SIZE - 2 * QAS_WINDOW_SIZE); x++) {
				dsp_rd_monitor[x] =
				    dsp_rd_monitor[x + QAS_WINDOW_SIZE];
			}

			for (x = 0; x != QAS_WINDOW_SIZE; x++) {
				int16_t mon;

				dsp_rd_audio[x] =
				    dsp_get_sample(&qas_read_buffer);

				mon = dsp_get_monitor_sample(&qas_write_buffer);
				if (qas_mute == 2)
					dsp_rd_audio[x] = mon;
				dsp_rd_monitor[QAS_MON_SIZE - 2 * QAS_WINDOW_SIZE + x] = mon;
			}
			atomic_wakeup();
		} while (qas_freeze);
		atomic_unlock();

		memset(dsp_rd_monitor_temp, 0, sizeof(dsp_rd_monitor_temp));

		for (y = 0; y <= (QAS_MON_SIZE - QAS_FET_SIZE + QAS_WINDOW_SIZE);
		     y += (QAS_FET_SIZE - QAS_WINDOW_SIZE)) {

			for (x = 0; x != QAS_WINDOW_SIZE; x++) {
				dsp_rd_fet_array[x] = dsp_rd_audio[QAS_WINDOW_SIZE - 1 - x];
				dsp_rd_curr_array[x] = dsp_rd_monitor[x + y];
			}

			for (; x != (QAS_FET_SIZE - QAS_WINDOW_SIZE); x++) {
				dsp_rd_fet_array[x] = 0;
				dsp_rd_curr_array[x] = dsp_rd_monitor[x + y];
			}

			for (; x != QAS_FET_SIZE; x++) {
				dsp_rd_fet_array[x] = 0;
				dsp_rd_curr_array[x] = 0;
			}

			prescaler = fet_prescaler_s64(dsp_rd_fet_array) *
			    fet_prescaler_s64(dsp_rd_curr_array);

			fet_16384_64(dsp_rd_fet_array);
			fet_16384_64(dsp_rd_curr_array);
			fet_conv_16384_64(dsp_rd_curr_array, dsp_rd_fet_array,
			    dsp_rd_fet_array);
			fet_16384_64(dsp_rd_fet_array);

			for (x = 0; x != QAS_FET_SIZE; x++) {
				dsp_rd_monitor_temp[x + y] += 
				    fet_to_lin_64(dsp_rd_fet_array[x]) / prescaler;
			}
		}

		for (x = 0; x != QAS_WINDOW_SIZE; x++)
			dsp_rd_fet_array[x] = dsp_rd_audio[x];
		for (; x != QAS_FET_SIZE; x++)
			dsp_rd_fet_array[x] = 0;

		prescaler = fet_prescaler_s64(dsp_rd_fet_array);
		fet_16384_64(dsp_rd_fet_array);

		atomic_filter_lock();
		qas_block_filter *f;
		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			f->do_block(prescaler, dsp_rd_fet_array, dsp_rd_filtered);
			double sum = 0;
			double avg = 0;
			for (x = 0; x != QAS_WINDOW_SIZE; x++)
				avg += dsp_rd_filtered[x];
			avg /= QAS_WINDOW_SIZE;

			for (x = 0; x != QAS_WINDOW_SIZE; x++) {
				double y = (dsp_rd_filtered[x] - avg);
				sum += y * y;
			}
			f->power[qas_power_index] = sqrt(sum);
		}
		qas_power_index = (qas_power_index + 1) % QAS_HISTORY_SIZE;
		atomic_filter_unlock();

		atomic_graph_lock();
		for (x = 0; x != QAS_MON_SIZE; x++) {
			qas_graph_data[x] *= (1.0 - 1.0 / 32.0);
			qas_graph_data[x] += dsp_rd_monitor_temp[x];
		}
		atomic_graph_unlock();
	}
	return (0);
}

void *
qas_dsp_write_thread(void *)
{
	static int16_t buffer[QAS_WINDOW_SIZE];
	static char fname[1024];
	int f = -1;
	int err;
	int temp;
	int odly;

	while (1) {
		if (f > -1) {
			close(f);
			f = -1;
		}

		usleep(250000);

		atomic_lock();
		strlcpy(fname, dsp_write_device, sizeof(fname));
		atomic_unlock();

		if (fname[0] == 0)
			continue;

		f = open(fname, O_WRONLY | O_NONBLOCK);
		if (f < 0)
			continue;

		temp = 0;
		err = ioctl(f, FIONBIO, &temp);
		if (err)
			continue;

		temp = AFMT_S16_NE;
		err = ioctl(f, SNDCTL_DSP_SETFMT, &temp);
		if (err)
			continue;

		temp = 1;
		err = ioctl(f, SOUND_PCM_WRITE_CHANNELS, &temp);
		if (err)
			continue;

		temp = qas_sample_rate;
		err = ioctl(f, SNDCTL_DSP_SPEED, &temp);
		if (err)
			continue;

		while (1) {
			atomic_lock();
			while (dsp_read_space(&qas_write_buffer) < QAS_WINDOW_SIZE) {
				atomic_wakeup();
				atomic_wait();
			}
			for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++)
				buffer[x] = dsp_get_sample(&qas_write_buffer);

			if (qas_mute)
				memset(buffer, 0, sizeof(buffer));

			atomic_wakeup();
			if (strcmp(fname, dsp_write_device)) {
				atomic_unlock();
				break;
			}
			atomic_unlock();
get_odelay:
			odly = 0;
			err = ioctl(f, SNDCTL_DSP_GETODELAY, &odly);
			if (err)
				break;

			if (odly > 4 * QAS_WINDOW_SIZE) {
				usleep(8000);
				goto get_odelay;
			}
			err = write(f, buffer, sizeof(buffer));
			if (err != sizeof(buffer))
				break;
		}
	}
	return (0);
}

void *
qas_dsp_read_thread(void *arg)
{
	static int16_t buffer[QAS_WINDOW_SIZE];
	static char fname[1024];
	int f = -1;
	int err;
	int temp;

	while (1) {
		if (f > -1) {
			close(f);
			f = -1;
		}
	  
		usleep(250000);

		atomic_lock();
		strlcpy(fname, dsp_read_device, sizeof(fname));
		atomic_unlock();

		if (fname[0] == 0)
			continue;

		f = open(fname, O_RDONLY | O_NONBLOCK);
		if (f < 0)
			continue;

		temp = 0;
		err = ioctl(f, FIONBIO, &temp);
		if (err)
			continue;

		temp = AFMT_S16_NE;
		err = ioctl(f, SNDCTL_DSP_SETFMT, &temp);
		if (err)
			continue;

		temp = 1;
		err = ioctl(f, SOUND_PCM_WRITE_CHANNELS, &temp);
		if (err)
			continue;

		temp = qas_sample_rate;
		err = ioctl(f, SNDCTL_DSP_SPEED, &temp);
		if (err)
			continue;

		while (1) {
			err = read(f, buffer, sizeof(buffer));
			if (err != sizeof(buffer))
				break;

			atomic_lock();
			for (int x = 0; x != (err / 2); x++)
				dsp_put_sample(&qas_read_buffer, buffer[x]);
			atomic_wakeup();
			if (strcmp(fname, dsp_read_device)) {
				atomic_unlock();
				break;
			}
			atomic_unlock();
		}
	}
	return (0);
}
