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
int	qas_sample_rate = QAS_SAMPLE_RATE;
int	qas_source_0;
int	qas_source_1;
int	qas_output_0;
int	qas_output_1;
int	qas_freeze;
unsigned qas_power_index;
struct dsp_buffer qas_read_buffer[2];
struct dsp_buffer qas_write_buffer[2];
char	dsp_read_device[1024];
char	dsp_write_device[1024];
double qas_graph_data[QAS_MON_SIZE];
double qas_band_power[QAS_HISTORY_SIZE][QAS_BAND_SIZE];
double dsp_rd_mon_filter[QAS_MON_COUNT][QAS_MUL_SIZE];
double qas_band_pass_filter[QAS_MUL_SIZE];
double qas_midi_level = 1LL << 62;
QasView_t qas_view_what = VIEW_AMP_LIN;
double qas_view_decay = 1.0 - 1.0 / 8.0;

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
	return (temp);
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
	return (temp);
}

void *
qas_dsp_audio_producer(void *arg)
{
	static double buffer[6][QAS_DSP_SIZE];
	static double noise[2][QAS_DSP_SIZE];
	static double temp[2][QAS_MUL_SIZE + QAS_DSP_SIZE];

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

		for (size_t x = 0; x != QAS_MUL_SIZE; x++) {
			/* shift down filter */
			temp[0][x] = temp[0][x + QAS_DSP_SIZE];
			temp[1][x] = temp[1][x + QAS_DSP_SIZE];
		}

		for (size_t x = 0; x != QAS_DSP_SIZE; x++) {
			/* zero rest of filter */
			temp[0][QAS_MUL_SIZE + x] = 0;
			temp[1][QAS_MUL_SIZE + x] = 0;
		}

		for (size_t x = 0; x != QAS_DSP_SIZE; x += QAS_MUL_SIZE) {
			qas_x3_multiply_double(qas_band_pass_filter, noise[0] + x, temp[0] + x, QAS_MUL_SIZE);
			qas_x3_multiply_double(qas_band_pass_filter, noise[1] + x, temp[1] + x, QAS_MUL_SIZE);
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
	return (NULL);
}

void
qas_dsp_sync(void)
{
	qas_block_filter *f;

	atomic_filter_lock();
	TAILQ_FOREACH(f, &qas_filter_head, entry)
		f->do_reset();
	atomic_filter_unlock();

	atomic_graph_lock();
	memset(qas_graph_data, 0, sizeof(qas_graph_data));
	atomic_graph_unlock();

	atomic_lock();
	while (dsp_read_space(&qas_read_buffer[0]))
		dsp_get_sample(&qas_read_buffer[0]);
	while (dsp_read_space(&qas_read_buffer[1]))
		dsp_get_sample(&qas_read_buffer[1]);
	atomic_wakeup();
	atomic_unlock();
}

void *
qas_dsp_audio_analyzer(void *arg)
{
	double *dsp_rd_audio;
	double *dsp_rd_monitor;
	static double dsp_rd_data[3][QAS_MUL_SIZE];
	static double dsp_rd_aud_rev[QAS_MUL_SIZE];
	static double dsp_rd_correlation_temp[QAS_MON_SIZE + QAS_MUL_SIZE];
	static unsigned dsp_rd_mon_filter_index = 0;
	struct qas_band_info info[256];
	uint8_t pressed[128] = {};

	while (1) {
		atomic_lock();
		do {
			while (dsp_monitor_space(&qas_write_buffer[0]) < QAS_MUL_SIZE ||
			       dsp_monitor_space(&qas_write_buffer[1]) < QAS_MUL_SIZE ||
			       dsp_read_space(&qas_read_buffer[0]) < QAS_MUL_SIZE ||
			       dsp_read_space(&qas_read_buffer[1]) < QAS_MUL_SIZE) {
					atomic_wait();
			}
	
			for (unsigned x = 0; x != QAS_MUL_SIZE; x++) {
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

		atomic_filter_lock();
		dsp_rd_mon_filter_index++;
		if (dsp_rd_mon_filter_index == QAS_MON_COUNT)
			dsp_rd_mon_filter_index = 0;

		/* copy monitor samples */
		memcpy(dsp_rd_mon_filter[dsp_rd_mon_filter_index],
		    dsp_rd_monitor, QAS_MUL_SIZE * sizeof(dsp_rd_monitor[0]));

		/* compute reversed audio samples */
		for (unsigned x = 0; x != QAS_MUL_SIZE; x++)
			dsp_rd_aud_rev[x] = dsp_rd_audio[QAS_MUL_SIZE - 1 - x];

		/* clear correlation buffer */
		memset(dsp_rd_correlation_temp, 0, sizeof(dsp_rd_correlation_temp));

		/* compute correlation */
		for (unsigned y = 0; y != QAS_MON_COUNT; y++) {
			unsigned x = (dsp_rd_mon_filter_index + 1 + y) % QAS_MON_COUNT;

			qas_x3_multiply_double(dsp_rd_mon_filter[x], dsp_rd_aud_rev,
			    dsp_rd_correlation_temp + (QAS_MUL_SIZE * y), QAS_MUL_SIZE);
		}
		atomic_filter_unlock();

		atomic_graph_lock();
		for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
			qas_graph_data[x] *= qas_view_decay;
			qas_graph_data[x] += dsp_rd_correlation_temp[x];
		}
		atomic_graph_unlock();


		unsigned peak_x = 0;
		for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
			if (fabs(qas_graph_data[x]) > fabs(qas_graph_data[peak_x]))
				peak_x = x;
		}

		atomic_filter_lock();
		memset(qas_band_power[qas_power_index], 0, sizeof(qas_band_power[qas_power_index]));

		qas_block_filter *f;
		unsigned num = 0;

		TAILQ_FOREACH(f, &qas_filter_head, entry)
			num++;

		size_t samples = (QAS_MON_SIZE - peak_x) * num;
		if (samples > 65536)
			samples = 65536;

		if (num == 0)
			num = 1;

		samples /= num;
		if (samples == 0)
			samples = 1;

		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			uint8_t band = num2band(f->num_index);

			f->do_mon_block_in(qas_graph_data + peak_x, samples);
			f->power[qas_power_index] = f->t_amp;
			if (qas_band_power[qas_power_index][band] < f->t_amp)
				qas_band_power[qas_power_index][band] = f->t_amp;
		}

		memset(info, 0, sizeof(info));

		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			if (f->num_index == 0)
				continue;

			unsigned y = f->num_index;
			if (y < 256) {
				info[y].power = f->t_amp;
				info[y].band = y;
			}
		}
		qas_power_index = (qas_power_index + 1) % QAS_HISTORY_SIZE;
		atomic_filter_unlock();

		uint8_t state[128] = {};

		for (unsigned x = 1; x != 255; x++) {
			unsigned y = info[x].band;

			if (y == 0 || y >= 256)
				continue;

			y /= 2;

			state[y] |= (info[x].power > qas_midi_level &&
				     info[x].power > info[x - 1].power &&
				     info[x].power > info[x + 1].power);
		}

		for (unsigned x = 0; x != 128; x++) {
			if (state[x] != 0) {
				if (pressed[x] == 0)
					qas_midi_key_send(x, 90);
				pressed[x] = 1;
			} else if (pressed[x] != 0) {
				pressed[x] = 0;
				qas_midi_key_send(x, 0);
			}
		}
	}
	return (0);
}

void *
qas_dsp_write_thread(void *)
{
	static union {
		int16_t s16[2 * QAS_DSP_SIZE];
		int32_t s32[2 * QAS_DSP_SIZE];
	} buffer;
	static char fname[1024];
	int f = -1;
	int err;
	int temp;
	int channels;
	int buflen;
	int afmt;

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

		afmt = AFMT_S32_NE;
		err = ioctl(f, SNDCTL_DSP_SETFMT, &afmt);
		if (err) {
			afmt = AFMT_S16_NE;
			err = ioctl(f, SNDCTL_DSP_SETFMT, &afmt);
			if (err)
				continue;
		}
		
		channels = 2;
		err = ioctl(f, SOUND_PCM_WRITE_CHANNELS, &channels);
		if (err) {
			channels = 1;
			err = ioctl(f, SOUND_PCM_WRITE_CHANNELS, &channels);
			if (err)
				continue;
		}

		temp = qas_sample_rate;
		err = ioctl(f, SNDCTL_DSP_SPEED, &temp);
		if (err || temp != qas_sample_rate)
			continue;

		temp = 0;
		while ((1U << temp) < QAS_DSP_SIZE)
			temp++;

		temp |= (2 << 16);
		if (channels == 2)
			temp += 2;
		else
			temp += 1;

		err = ioctl(f, SNDCTL_DSP_SETFRAGMENT, &temp);
		if (err)
			continue;

		/* compute buffer length */
		buflen = (channels == 1 ?
		    (afmt == AFMT_S32_NE ? sizeof(buffer.s32) / 2 : sizeof(buffer.s16) / 2) :
		    (afmt == AFMT_S32_NE ? sizeof(buffer.s32) : sizeof(buffer.s16)));

		while (1) {
			atomic_lock();
			while (dsp_read_space(&qas_write_buffer[0]) < QAS_DSP_SIZE ||
			       dsp_read_space(&qas_write_buffer[1]) < QAS_DSP_SIZE) {
				atomic_wakeup();
				atomic_wait();
			}
			if (channels == 2) {
				if (afmt == AFMT_S32_NE) {
					for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
						buffer.s32[2*x+0] = dsp_get_sample(&qas_write_buffer[0]);
						buffer.s32[2*x+1] = dsp_get_sample(&qas_write_buffer[1]);
					}
				} else {
					for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
						buffer.s16[2*x+0] = dsp_get_sample(&qas_write_buffer[0]) / 65536.0;
						buffer.s16[2*x+1] = dsp_get_sample(&qas_write_buffer[1]) / 65536.0;
					}
				}
			} else {
				if (afmt == AFMT_S32_NE) {
					for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
						buffer.s32[x] = dsp_get_sample(&qas_write_buffer[0]);
						(void) dsp_get_sample(&qas_write_buffer[1]);
					}
				} else {
					for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
						buffer.s16[x] = dsp_get_sample(&qas_write_buffer[0]) / 65536.0;
						(void) dsp_get_sample(&qas_write_buffer[1]);
					}
				}
			}
			atomic_wakeup();
			if (strcmp(fname, dsp_write_device)) {
				atomic_unlock();
				break;
			}
			atomic_unlock();
			err = write(f, &buffer, buflen);
			if (err != buflen)
				break;
		}
	}
	return (0);
}

void *
qas_dsp_read_thread(void *arg)
{
	static union {
		int16_t s16[2 * QAS_DSP_SIZE];
		int32_t s32[2 * QAS_DSP_SIZE];
	} buffer;
	static char fname[1024];
	int f = -1;
	int err;
	int temp;
	int channels;
	int buflen;
	int afmt;

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

		afmt = AFMT_S32_NE;
		err = ioctl(f, SNDCTL_DSP_SETFMT, &afmt);
		if (err) {
			afmt = AFMT_S16_NE;
			err = ioctl(f, SNDCTL_DSP_SETFMT, &afmt);
			if (err)
				continue;
		}

		channels = 2;
		err = ioctl(f, SOUND_PCM_WRITE_CHANNELS, &channels);
		if (err) {
			channels = 1;
			err = ioctl(f, SOUND_PCM_WRITE_CHANNELS, &channels);
			if (err)
				continue;
		}

		temp = qas_sample_rate;
		err = ioctl(f, SNDCTL_DSP_SPEED, &temp);
		if (err || temp != qas_sample_rate)
			continue;

		temp = 0;
		while ((1U << temp) < QAS_DSP_SIZE)
			temp++;

		temp |= (2 << 16);
		if (channels == 2)
			temp += 2;
		else
			temp += 1;

		err = ioctl(f, SNDCTL_DSP_SETFRAGMENT, &temp);
		if (err)
			continue;

		/* compute buffer length */
		buflen = (channels == 1 ?
		    (afmt == AFMT_S32_NE ? sizeof(buffer.s32) / 2 : sizeof(buffer.s16) / 2) :
		    (afmt == AFMT_S32_NE ? sizeof(buffer.s32) : sizeof(buffer.s16)));

		while (1) {
			err = read(f, &buffer, buflen);
			if (err != buflen)
				break;

			atomic_lock();
			if (channels == 2) {
				if (afmt == AFMT_S32_NE) {
					for (int x = 0; x != (err / 8); x++) {
						dsp_put_sample(&qas_read_buffer[0], buffer.s32[2*x+0]);
						dsp_put_sample(&qas_read_buffer[1], buffer.s32[2*x+1]);
					}
				} else {
					for (int x = 0; x != (err / 4); x++) {
						dsp_put_sample(&qas_read_buffer[0], buffer.s16[2*x+0]);
						dsp_put_sample(&qas_read_buffer[1], buffer.s16[2*x+1]);
					}
				}
			} else {
				if (afmt == AFMT_S32_NE) {
					for (int x = 0; x != (err / 4); x++) {
						dsp_put_sample(&qas_read_buffer[0], buffer.s32[x]);
						dsp_put_sample(&qas_read_buffer[1], 0);
					}
				} else {
					for (int x = 0; x != (err / 2); x++) {
						dsp_put_sample(&qas_read_buffer[0], buffer.s16[x]);
						dsp_put_sample(&qas_read_buffer[1], 0);
					}
				}
			}
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
