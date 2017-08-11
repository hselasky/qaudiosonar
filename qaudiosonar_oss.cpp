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
int64_t qas_graph_data[QAS_MON_SIZE];
double qas_band_power[QAS_HISTORY_SIZE][QAS_BAND_SIZE];
double dsp_rd_mon_filter[QAS_MON_COUNT][QAS_FILTER_SIZE];

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

	temp = noise_rem & 0xFFFFFF;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000)
		temp |= (-0x800000);
	return (temp);
}

void *
qas_dsp_audio_producer(void *arg)
{
	static int16_t buffer[4][QAS_DSP_SIZE];

	atomic_lock();
	while (1) {
		while (dsp_write_monitor_space(&qas_write_buffer[0]) < QAS_DSP_SIZE ||
		       dsp_write_monitor_space(&qas_write_buffer[1]) < QAS_DSP_SIZE)
			atomic_wait();
		for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
			buffer[1][x] = qas_brown_noise();
			buffer[2][x] = qas_white_noise();

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
	static double dsp_rd_aud_fwd_filter[QAS_FILTER_SIZE];
	static double dsp_rd_aud_rev_filter[QAS_FILTER_SIZE];
	static double dsp_rd_temp_filter[2][QAS_FILTER_SIZE];
	static double dsp_rd_correlation_temp[QAS_MON_SIZE + QAS_MUL_SIZE];
	static unsigned dsp_rd_mon_filter_index = 0;

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

		/* convert monitor samples */
		qas_mul_import_double(dsp_rd_monitor,
		    dsp_rd_mon_filter[dsp_rd_mon_filter_index], QAS_MUL_SIZE);
		qas_mul_xform_fwd_double(dsp_rd_mon_filter[dsp_rd_mon_filter_index],
		    QAS_MUL_SIZE);

		/* convert audio samples */
		qas_mul_import_double(dsp_rd_audio, dsp_rd_aud_fwd_filter, QAS_MUL_SIZE);
		qas_mul_xform_fwd_double(dsp_rd_aud_fwd_filter, QAS_MUL_SIZE);

		/* convert reversed audio samples */
		for (unsigned x = 0; x != QAS_MUL_SIZE / 2; x++) {
			double temp = dsp_rd_audio[x];
			dsp_rd_audio[x] = dsp_rd_audio[QAS_MUL_SIZE - 1 - x];
			dsp_rd_audio[QAS_MUL_SIZE - 1 - x] = temp;
		}
		qas_mul_import_double(dsp_rd_audio, dsp_rd_aud_rev_filter, QAS_MUL_SIZE);
		qas_mul_xform_fwd_double(dsp_rd_aud_rev_filter, QAS_MUL_SIZE);

		/* clear correlation buffer */
		memset(dsp_rd_correlation_temp, 0, sizeof(dsp_rd_correlation_temp));

		/* compute correlation filter */
		for (unsigned y = 0; y != QAS_MON_COUNT; y++) {
			unsigned x = (dsp_rd_mon_filter_index + 1 + y) % QAS_MON_COUNT;

			for (unsigned z = 0; z != QAS_FILTER_SIZE; z++) {
				dsp_rd_temp_filter[0][z] = dsp_rd_mon_filter[x][z] *
				    dsp_rd_aud_rev_filter[z];
			}

			/* compute correlation */
			qas_mul_xform_inv_double(dsp_rd_temp_filter[0], QAS_MUL_SIZE);

			/* re-order vector array */
			for (unsigned z = 0; z != QAS_FILTER_SIZE; z++) {
				dsp_rd_temp_filter[1][z] =
				    dsp_rd_temp_filter[0][qas_mul_context->table[z]];
			}

			/* final error correction */
			qas_mul_xform_inv_double(dsp_rd_temp_filter[1], QAS_MUL_SIZE);

			/* export */
			qas_mul_export_double(dsp_rd_temp_filter[1],
			    dsp_rd_correlation_temp + (QAS_MUL_SIZE * y), QAS_MUL_SIZE);
		}
		atomic_filter_unlock();

		atomic_graph_lock();
		for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
			qas_graph_data[x] *= (1.0 - 1.0 / 32.0);
			qas_graph_data[x] += dsp_rd_correlation_temp[x];
		}
		atomic_graph_unlock();

		atomic_filter_lock();
		memset(qas_band_power[qas_power_index], 0, sizeof(qas_band_power[qas_power_index]));

		qas_block_filter *f;
		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			f->do_mon_block_in(qas_graph_data);
			f->power[qas_power_index] = f->t_amp;
			qas_band_power[qas_power_index][f->band] += f->t_amp;
		}
		qas_power_index = (qas_power_index + 1) % QAS_HISTORY_SIZE;
		atomic_filter_unlock();
	}
	return (0);
}

void *
qas_dsp_write_thread(void *)
{
	static int16_t buffer[2 * QAS_DSP_SIZE];
	static char fname[1024];
	int f = -1;
	int err;
	int temp;
	int channels;
	int buflen;

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

		while (1) {
			atomic_lock();
			while (dsp_read_space(&qas_write_buffer[0]) < QAS_DSP_SIZE ||
			       dsp_read_space(&qas_write_buffer[1]) < QAS_DSP_SIZE) {
				atomic_wakeup();
				atomic_wait();
			}
			if (channels == 2) {
				for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
					buffer[2*x+0] = dsp_get_sample(&qas_write_buffer[0]);
					buffer[2*x+1] = dsp_get_sample(&qas_write_buffer[1]);
				}
			} else {
				for (unsigned x = 0; x != QAS_DSP_SIZE; x++) {
					buffer[x] = dsp_get_sample(&qas_write_buffer[0]);
					(void) dsp_get_sample(&qas_write_buffer[1]);
				}
			}
			atomic_wakeup();
			if (strcmp(fname, dsp_write_device)) {
				atomic_unlock();
				break;
			}
			atomic_unlock();
			buflen = (channels == 1 ? sizeof(buffer) / 2 : sizeof(buffer));
			err = write(f, buffer, buflen);
			if (err != buflen)
				break;
		}
	}
	return (0);
}

void *
qas_dsp_read_thread(void *arg)
{
	static int16_t buffer[2 * QAS_DSP_SIZE];
	static char fname[1024];
	int f = -1;
	int err;
	int temp;
	int channels;
	int buflen;

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

		while (1) {
			buflen = (channels == 1 ? sizeof(buffer) / 2 : sizeof(buffer));
			err = read(f, buffer, buflen);
			if (err != buflen)
				break;

			atomic_lock();
			if (channels == 2) {
				for (int x = 0; x != (err / 4); x++) {
					dsp_put_sample(&qas_read_buffer[0], buffer[2*x+0]);
					dsp_put_sample(&qas_read_buffer[1], buffer[2*x+1]);
				}
			} else {
				for (int x = 0; x != (err / 2); x++) {
					dsp_put_sample(&qas_read_buffer[0], buffer[x]);
					dsp_put_sample(&qas_read_buffer[1], 0);
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
