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

qas_filter_head_t qas_filter_head = TAILQ_HEAD_INITIALIZER(qas_filter_head);
int	qas_sample_rate = 48000;
struct dsp_buffer qas_read_buffer;
struct dsp_buffer qas_write_buffer;
char	dsp_read_device[1024];
char	dsp_write_device[1024];
int64_t qas_graph_data[QAS_MON_SIZE];
int64_t *qas_max_data;
int64_t *qas_phase_data;
unsigned qas_bands = 31;
unsigned qas_which;
unsigned qas_logarithmic = 2;
unsigned qas_graph_count;
double qas_freq;

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
	static uint32_t noise_rem = 1;
	const uint32_t prime = 0xFFFF1D;
	uint32_t temp;

	if (noise_rem & 1)
		noise_rem += prime;

	noise_rem /= 2;

	temp = noise_rem;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000) {
		temp |= (-0x800000);
	}
	return (temp);
}

void *
qas_dsp_audio_producer(void *arg)
{
	static int16_t dsp_wr_noise_array[QAS_FET_SIZE];
	static int64_t dsp_wr_fet_array[QAS_FET_SIZE];
	static int64_t dsp_wr_curr_array[QAS_FET_SIZE];
	static int64_t dsp_wr_next_array[QAS_FET_SIZE];

	qas_filter *curr = NULL;
	qas_filter *next = NULL;

	double pre_a;
	double pre_b;

	atomic_lock();
	while (1) {
		while (dsp_write_monitor_space(&qas_write_buffer) < QAS_WINDOW_SIZE)
			atomic_wait();

		if (curr == NULL) {
			curr = TAILQ_FIRST(&qas_filter_head);
			if (curr != NULL) {
				TAILQ_REMOVE(&qas_filter_head, curr, entry);
				qas_graph_count = 0;
				qas_which = curr->which;
				qas_freq = curr->freq;
			}
		}
		if (next == NULL) {
			next = TAILQ_FIRST(&qas_filter_head);
			if (next != NULL) {
				TAILQ_REMOVE(&qas_filter_head, next, entry);
			}
		}
		if (curr == NULL && next == NULL) {
			for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++)
				dsp_put_sample(&qas_write_buffer, 0);
			atomic_wakeup();
		} else if (curr != NULL && next == NULL) {
			atomic_unlock();
			for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++) {
				dsp_wr_noise_array[x] =
				    dsp_wr_noise_array[x + QAS_WINDOW_SIZE];
				dsp_wr_noise_array[x + QAS_WINDOW_SIZE] =
				    qas_noise() >> 10;
			}
			for (unsigned x = 0; x != QAS_FET_SIZE; x++)
				dsp_wr_fet_array[x] = dsp_wr_noise_array[x];

			pre_a = fet_prescaler_s64(dsp_wr_fet_array) *
			    curr->prescaler;

			fet_16384_64(dsp_wr_fet_array);
			fet_conv_16384_64(dsp_wr_fet_array, curr->filter_fast,
			    dsp_wr_curr_array);
			fet_16384_64(dsp_wr_curr_array);

			atomic_lock();
			for (int64_t x = 0; x != QAS_WINDOW_SIZE; x++) {
				dsp_put_sample(&qas_write_buffer,
				    fet_to_lin_64(dsp_wr_curr_array[x +
				    QAS_WINDOW_SIZE]) / pre_a);
			}
			atomic_wakeup();
		} else if (curr != NULL && next != NULL) {
			atomic_unlock();
			for (unsigned x = 0; x != QAS_WINDOW_SIZE; x++) {
				dsp_wr_noise_array[x] =
				    dsp_wr_noise_array[x + QAS_WINDOW_SIZE];
				dsp_wr_noise_array[x + QAS_WINDOW_SIZE] =
				    qas_noise() >> 10;
			}
			for (unsigned x = 0; x != QAS_FET_SIZE; x++)
				dsp_wr_fet_array[x] = dsp_wr_noise_array[x];

			double pre = fet_prescaler_s64(dsp_wr_fet_array);
			pre_a = pre * curr->prescaler;
			pre_b = pre * next->prescaler;

			fet_16384_64(dsp_wr_fet_array);
			fet_conv_16384_64(dsp_wr_fet_array, curr->filter_fast,
			    dsp_wr_curr_array);
			fet_conv_16384_64(dsp_wr_fet_array, next->filter_fast,
			    dsp_wr_next_array);
			fet_16384_64(dsp_wr_curr_array);
			fet_16384_64(dsp_wr_next_array);

			atomic_lock();
			for (int64_t x = 0; x != QAS_WINDOW_SIZE; x++) {
				int64_t sc = fet_to_lin_64(dsp_wr_curr_array[x +
				    QAS_WINDOW_SIZE]) / pre_a;
				int64_t sn = fet_to_lin_64(dsp_wr_next_array[x +
				    QAS_WINDOW_SIZE]) / pre_b;
				int64_t samp = (sc * (QAS_WINDOW_SIZE - x) +
				    sn * x) / QAS_WINDOW_SIZE;
				dsp_put_sample(&qas_write_buffer, samp);
			}
			atomic_wakeup();
			delete curr;
			curr = next;
			next = NULL;
			qas_graph_count = 0;
			qas_which = curr->which;
			qas_freq = curr->freq;
		}
	}
	atomic_unlock();

	return (0);
}

void
qas_dsp_sync(void)
{
	atomic_graph_lock();
	atomic_lock();
#if 0
	while (dsp_monitor_space(&qas_write_buffer))
		dsp_get_monitor_sample(&qas_write_buffer);
#endif
	while (dsp_read_space(&qas_read_buffer))
		dsp_get_sample(&qas_read_buffer);
	memset(qas_phase_data, 0, sizeof(qas_phase_data[0])*qas_bands);
	memset(qas_max_data, 0, sizeof(qas_max_data[0])*qas_bands);
	qas_graph_count = 0;
	atomic_wakeup();
	atomic_unlock();
	atomic_graph_unlock();
}

void *
qas_dsp_audio_analyzer(void *arg)
{
	static int16_t dsp_rd_audio[QAS_FET_SIZE];
	static int16_t dsp_rd_monitor[QAS_MON_SIZE];
	static int64_t dsp_rd_fet_array[QAS_FET_SIZE];
	static int64_t dsp_rd_curr_array[QAS_FET_SIZE];

	unsigned x,y;

	while (1) {
		atomic_lock();
		while (dsp_monitor_space(&qas_write_buffer) < QAS_WINDOW_SIZE ||
		       dsp_read_space(&qas_read_buffer) < QAS_WINDOW_SIZE) {
			atomic_wait();
		}

		for (x = 0; x != (QAS_MON_SIZE - 2 * QAS_WINDOW_SIZE); x++) {
			dsp_rd_monitor[x] =
			    dsp_rd_monitor[x + QAS_WINDOW_SIZE];
		}

		for (x = 0; x != QAS_WINDOW_SIZE; x++) {
			dsp_rd_audio[QAS_WINDOW_SIZE -1 - x] =
			    dsp_get_sample(&qas_read_buffer);
			dsp_rd_monitor[QAS_MON_SIZE - 2 * QAS_WINDOW_SIZE + x] =
			    dsp_get_monitor_sample(&qas_write_buffer);
		}
		atomic_wakeup();
		atomic_unlock();

		atomic_graph_lock();
		atomic_lock();
		int do_zero = (qas_graph_count == 0);
		atomic_unlock();

		if (do_zero)
			memset(qas_graph_data, 0, sizeof(qas_graph_data));

		for (y = 0; y <= (QAS_MON_SIZE - QAS_FET_SIZE + QAS_WINDOW_SIZE);
		     y += (QAS_FET_SIZE - QAS_WINDOW_SIZE)) {

			for (x = 0; x != (QAS_FET_SIZE - QAS_WINDOW_SIZE); x++) {
				dsp_rd_fet_array[x] = dsp_rd_audio[x];
				dsp_rd_curr_array[x] = dsp_rd_monitor[x + y];
			}

			for (; x != QAS_FET_SIZE; x++) {
				dsp_rd_fet_array[x] = 0;
				dsp_rd_curr_array[x] = 0;
			}

			double prescaler = fet_prescaler_s64(dsp_rd_fet_array) *
			    fet_prescaler_s64(dsp_rd_curr_array);

			fet_16384_64(dsp_rd_fet_array);
			fet_16384_64(dsp_rd_curr_array);
			fet_conv_16384_64(dsp_rd_curr_array, dsp_rd_fet_array,
			    dsp_rd_fet_array);
			fet_16384_64(dsp_rd_fet_array);

			for (x = 0; x != QAS_FET_SIZE; x++) {
				qas_graph_data[x + y] +=
				    fet_to_lin_64(dsp_rd_fet_array[x]) / prescaler;
			}
		}
		for (x = y = 0; x != QAS_MON_SIZE; x++) {
			if (qas_graph_data[x] > qas_graph_data[y])
				y = x;
		}
		atomic_lock();
		qas_graph_count++;
		qas_max_data[qas_which] = qas_graph_data[y] / qas_graph_count;
		qas_phase_data[qas_which] = y;
		atomic_unlock();

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
