/*-
 * Copyright (c) 2021-2022 Hans Petter Selasky.
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

#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include "../src/qaudiosonar.h"

#include <asiosys.h>
#include <asio.h>
#include <asiodrivers.h>

#define	MAX_CHANNELS 128
#define	MAX_DRIVERS 16

extern AsioDrivers * asioDrivers;
extern bool loadAsioDriver(char *name);

static ASIOBufferInfo bufferInfo[2 * MAX_CHANNELS];
static ASIOChannelInfo channelInfo[2 * MAX_CHANNELS];
static ASIODriverInfo audioDriverInfo;
static ASIOCallbacks audioCallbacks;
static bool audioPostOutput;
static bool audioInit;
static uint32_t audioBufferSamples;
static uint32_t audioBufferDefSamples = 2 * QAS_DEF_SAMPLES;
static unsigned audioInputSelection[2];
static unsigned audioOutputSelection[2];
static long audioInputChannels;
static long audioOutputChannels;
static float *audioInputBuffer[3];
static QMutex audioMutex;
static uint32_t audioDeviceSelection;
static uint32_t audioMaxSelection;
static char *audioDeviceNames[MAX_DRIVERS];

template <typename T> void
qas_audio_import(const ASIOBufferInfo &bi, const int gain, const unsigned index, float *dst)
{
	const T *buf = static_cast <const T *>(bi.buffers[index]);

	for (uint32_t x = 0; x != audioBufferSamples; x++)
		dst[x] = buf[x].get() * gain;
}

template <typename T> void
qas_audio_export(const ASIOBufferInfo &bi, const int gain, const unsigned index, const float *src)
{
	T *buf = static_cast <T *>(bi.buffers[index]);

	for (uint32_t x = 0; x != audioBufferSamples; x++)
		buf[x].put(src[x] / gain);
}

static bool
qas_check_sample_type_supported(const ASIOSampleType type)
{
	switch (type) {
	case ASIOSTInt16LSB:
	case ASIOSTInt24LSB:
	case ASIOSTInt32LSB:
	case ASIOSTFloat32LSB:
	case ASIOSTFloat64LSB:
	case ASIOSTInt32LSB16:
	case ASIOSTInt32LSB18:
	case ASIOSTInt32LSB20:
	case ASIOSTInt32LSB24:
	case ASIOSTInt16MSB:
	case ASIOSTInt24MSB:
	case ASIOSTInt32MSB:
	case ASIOSTFloat32MSB:
	case ASIOSTFloat64MSB:
	case ASIOSTInt32MSB16:
	case ASIOSTInt32MSB18:
	case ASIOSTInt32MSB20:
	case ASIOSTInt32MSB24:
		return (true);
	default:
		return (false);
	}
}

static long
qas_asio_messages(long selector, long, void *, double *)
{
	switch (selector) {
	case kAsioEngineVersion:
		return (2);
	default:
		return (0);
	}
}

static constexpr float FACTOR16 = 32767.0f;
static constexpr float FACTOR16_INV = 1.0f / 32767.0f;

struct sample16LSB {
	int16_t	data[1];
	float	get() const {
		return (data[0] * FACTOR16_INV);
	}
	void	put(const float value) {
		data[0] = (int16_t)(value * FACTOR16);
	}
};

struct sample16MSB {
	uint8_t	data[2];
	float	get() const {
		const int16_t temp = data[1] | (data[0] << 8);
			return (temp * FACTOR16_INV);
	}
	void	put(const float value) {
		const int16_t temp = (int16_t)(value * FACTOR16);
			data[0] = (uint8_t)(temp >> 8);
			data[1] = (uint8_t)(temp);
	}
};

static constexpr float FACTOR24 = 2147483647 - 127;
static constexpr float FACTOR24_INV = 1.0f / (2147483647 - 127);

struct sample24LSB {
	uint8_t	data[3];
	float	get() const {
		const int32_t temp = (data[0] << 8) | (data[1] << 16) | (data[2] << 24);
			return (temp * FACTOR24_INV);
	}
	void	put(const float value) {
		const int32_t temp = (int32_t)(value * FACTOR24);
			data[0] = (uint8_t)(temp >> 8);
			data[1] = (uint8_t)(temp >> 16);
			data[2] = (uint8_t)(temp >> 24);
	}
};

struct sample24MSB {
	uint8_t	data[3];
	float	get() const {
		const int32_t temp = (data[2] << 8) | (data[1] << 16) | (data[0] << 24);
			return (temp * FACTOR24_INV);
	}
	void	put(const float value) {
		const int32_t temp = (int32_t)(value * FACTOR24);
			data[0] = (uint8_t)(temp >> 24);
			data[1] = (uint8_t)(temp >> 16);
			data[2] = (uint8_t)(temp >> 8);
	}
};

static constexpr float FACTOR32 = 2147483647 - 127;
static constexpr float FACTOR32_INV = 1.0f / (2147483647 - 127);

struct sample32LSB {
	int32_t	data[1];
	float	get() const {
		return (data[0] * FACTOR32_INV);
	}
	void	put(const float value) {
		data[0] = (int32_t)(value * FACTOR32);
	}
};

struct sample32MSB {
	uint8_t	data[4];
	float	get() const {
		const int32_t temp = (data[3] << 0) | (data[2] << 8) |
		(data[1] << 16) | (data[0] << 24);
			return (temp * FACTOR32_INV);
	}
	void	put(const float value) {
		const int32_t temp = (int32_t)(value * FACTOR32);
			data[0] = (uint8_t)(temp >> 24);
			data[1] = (uint8_t)(temp >> 16);
			data[2] = (uint8_t)(temp >> 8);
			data[3] = (uint8_t)(temp >> 0);
	}
};

union sampleFloat32Data {
	uint8_t	data[4];
	float	value;
};

struct sampleFloat32LSB {
	float	data[1];
	float	get() const {
		return (data[0]);
	}
	void	put(const float value) {
		data[0] = value;
	}
};

struct sampleFloat32MSB {
	uint8_t	data[4];
	float	get() const {
		sampleFloat32Data temp;
			temp.data[0] = data[3];
			temp.data[1] = data[2];
			temp.data[2] = data[1];
			temp.data[3] = data[0];
			return (temp.value);
	}
	void	put(const float value) {
		sampleFloat32Data temp;
			temp.value = value;
			data[0] = temp.data[3];
			data[1] = temp.data[2];
			data[2] = temp.data[1];
			data[3] = temp.data[0];
	}
};

union sampleFloat64Data {
	uint8_t	data[8];
	double	value;
};

struct sampleFloat64LSB {
	double	data[1];
	float	get() const {
		return (data[0]);
	}
	void	put(const float value) {
		data[0] = value;
	}
};

struct sampleFloat64MSB {
	uint8_t	data[8];
	float	get() const {
		sampleFloat64Data temp;
			temp.data[0] = data[7];
			temp.data[1] = data[6];
			temp.data[2] = data[5];
			temp.data[3] = data[4];
			temp.data[4] = data[3];
			temp.data[5] = data[2];
			temp.data[6] = data[1];
			temp.data[7] = data[0];
			return (temp.value);
	}
	void	put(const float value) {
		sampleFloat64Data temp;
			temp.value = value;
			data[0] = temp.data[7];
			data[1] = temp.data[6];
			data[2] = temp.data[5];
			data[3] = temp.data[4];
			data[4] = temp.data[3];
			data[5] = temp.data[2];
			data[6] = temp.data[1];
			data[7] = temp.data[0];
	}
};

static void
qas_asio_buffer_switch(long index, ASIOBool)
{
	QMutexLocker locker(&audioMutex);

	for (unsigned ch = 0; ch != 2; ch++) {
		const unsigned i = audioInputSelection[ch];
		const ASIOBufferInfo &bi = bufferInfo[i];
		float *dst = audioInputBuffer[ch];

		switch (channelInfo[i].type) {
		case ASIOSTInt16LSB:
			qas_audio_import <sample16LSB> (bi, 1, index, dst);
			break;

		case ASIOSTInt16MSB:
			qas_audio_import <sample16MSB> (bi, 1, index, dst);
			break;

		case ASIOSTInt24LSB:
			qas_audio_import <sample24LSB> (bi, 1, index, dst);
			break;

		case ASIOSTInt24MSB:
			qas_audio_import <sample24MSB> (bi, 1, index, dst);
			break;

		case ASIOSTInt32LSB:
			qas_audio_import <sample32LSB> (bi, 1, index, dst);
			break;

		case ASIOSTInt32MSB:
			qas_audio_import <sample32MSB> (bi, 1, index, dst);
			break;

		case ASIOSTFloat32LSB:
			qas_audio_import <sampleFloat32LSB> (bi, 1, index, dst);
			break;

		case ASIOSTFloat32MSB:
			qas_audio_import <sampleFloat32MSB> (bi, 1, index, dst);
			break;

		case ASIOSTFloat64LSB:
			qas_audio_import <sampleFloat64LSB> (bi, 1, index, dst);
			break;

		case ASIOSTFloat64MSB:
			qas_audio_import <sampleFloat64MSB> (bi, 1, index, dst);
			break;

		case ASIOSTInt32LSB16:
			qas_audio_import <sample32LSB> (bi, 1 << 16, index, dst);
			break;

		case ASIOSTInt32MSB16:
			qas_audio_import <sample32MSB> (bi, 1 << 16, index, dst);
			break;

		case ASIOSTInt32LSB18:
			qas_audio_import <sample32LSB> (bi, 1 << 14, index, dst);
			break;

		case ASIOSTInt32MSB18:
			qas_audio_import <sample32MSB> (bi, 1 << 14, index, dst);
			break;

		case ASIOSTInt32LSB20:
			qas_audio_import <sample32LSB> (bi, 1 << 12, index, dst);
			break;

		case ASIOSTInt32MSB20:
			qas_audio_import <sample32MSB> (bi, 1 << 12, index, dst);
			break;

		case ASIOSTInt32LSB24:
			qas_audio_import <sample32LSB> (bi, 1 << 8, index, dst);
			break;

		case ASIOSTInt32MSB24:
			qas_audio_import <sample32MSB> (bi, 1 << 8, index, dst);
			break;

		default:
			assert(0);
		}
	}

	/* process audio on output */
	qas_sound_process(
	    audioInputBuffer[0], audioInputBuffer[1], audioBufferSamples);

	/* check for mono output */
	if (audioInputSelection[0] == audioInputSelection[1]) {
		for (uint32_t x = 0; x != audioBufferSamples; x++) {
			audioInputBuffer[0][x] =
			    (audioInputBuffer[0][x] + audioInputBuffer[1][x]) / 2.0f;
		}
	}

	for (long x = 0; x != audioOutputChannels; x++) {
		const unsigned i = audioInputChannels + x;
		const ASIOBufferInfo &bi = bufferInfo[i];
		const float *src = audioInputBuffer[
		     (x == audioOutputSelection[0]) ? 0 :
		    ((x == audioOutputSelection[1]) ? 1 : 2)];

		switch (channelInfo[i].type) {
		case ASIOSTInt16LSB:
			qas_audio_export<sample16LSB> (bi, 1, index, src);
			break;

		case ASIOSTInt16MSB:
			qas_audio_export<sample16MSB> (bi, 1, index, src);
			break;

		case ASIOSTInt24LSB:
			qas_audio_export<sample24LSB> (bi, 1, index, src);
			break;

		case ASIOSTInt24MSB:
			qas_audio_export<sample24MSB> (bi, 1, index, src);
			break;

		case ASIOSTInt32LSB:
			qas_audio_export<sample32LSB> (bi, 1, index, src);
			break;

		case ASIOSTInt32MSB:
			qas_audio_export<sample32MSB> (bi, 1, index, src);
			break;

		case ASIOSTFloat32LSB:
			qas_audio_export<sampleFloat32LSB> (bi, 1, index, src);
			break;

		case ASIOSTFloat32MSB:
			qas_audio_export<sampleFloat32MSB> (bi, 1, index, src);
			break;

		case ASIOSTFloat64LSB:
			qas_audio_export<sampleFloat64LSB> (bi, 1, index, src);
			break;

		case ASIOSTFloat64MSB:
			qas_audio_export<sampleFloat64MSB> (bi, 1, index, src);
			break;

		case ASIOSTInt32LSB16:
			qas_audio_export<sample32LSB> (bi, 1 << 16, index, src);
			break;

		case ASIOSTInt32MSB16:
			qas_audio_export<sample32MSB> (bi, 1 << 16, index, src);
			break;

		case ASIOSTInt32LSB18:
			qas_audio_export<sample32LSB> (bi, 1 << 14, index, src);
			break;

		case ASIOSTInt32MSB18:
			qas_audio_export<sample32MSB> (bi, 1 << 14, index, src);
			break;

		case ASIOSTInt32LSB20:
			qas_audio_export<sample32LSB> (bi, 1 << 12, index, src);
			break;

		case ASIOSTInt32MSB20:
			qas_audio_export<sample32MSB> (bi, 1 << 12, index, src);
			break;

		case ASIOSTInt32LSB24:
			qas_audio_export<sample32LSB> (bi, 1 << 8, index, src);
			break;

		case ASIOSTInt32MSB24:
			qas_audio_export<sample32MSB> (bi, 1 << 8, index, src);
			break;
		default:
			assert(0);
			break;
		}
	}

	if (audioPostOutput)
		ASIOOutputReady();
}

static ASIOTime *
qas_asio_buffer_switch_time_info(ASIOTime *, long index, ASIOBool processNow)
{
	qas_asio_buffer_switch(index, processNow);
	return (NULL);
}

static void
qas_asio_sample_rate_changed(ASIOSampleRate)
{

}

static bool
qas_asio_check_dev_caps()
{
	unsigned index = 0;
	ASIOError error;

	error = ASIOCanSampleRate(QAS_SAMPLE_RATE);
	if ((error == ASE_NoClock) || (error == ASE_NotPresent))
		return (true);

	error = ASIOSetSampleRate(QAS_SAMPLE_RATE);
	if ((error == ASE_NoClock) || (error == ASE_InvalidMode) || (error == ASE_NotPresent))
		return (true);

	ASIOGetChannels(&audioInputChannels, &audioOutputChannels);

	if (audioInputChannels == 0 || audioInputChannels > MAX_CHANNELS)
		return (true);

	if (audioOutputChannels == 0 || audioOutputChannels > MAX_CHANNELS)
		return (true);

	for (long i = 0; i != audioInputChannels; i++, index++)
	{
		ASIOChannelInfo & ci = channelInfo[index];

		ci.isInput = ASIOTrue;
		ci.channel = i;
		ASIOGetChannelInfo(&ci);
		if (!qas_check_sample_type_supported(ci.type))
			return (true);
	}

	for (long i = 0; i != audioOutputChannels; i++, index++)
	{
		ASIOChannelInfo & ci = channelInfo[index];

		ci.isInput = ASIOFalse;
		ci.channel = i;

		ASIOGetChannelInfo(&ci);

		if (!qas_check_sample_type_supported(ci.type))
			return (true);
	}
	return (false);
}

static unsigned
qas_asio_get_buffer_size()
{
	long lMinSize;
	long lMaxSize;
	long lPreferredSize;
	long lGranularity;
	long lBufSize;

	ASIOGetBufferSize(&lMinSize, &lMaxSize, &lPreferredSize, &lGranularity);

	/* range check arguments */
	if (lGranularity <= 0)
		lGranularity = 1;
	if (lMaxSize > (long)audioBufferDefSamples)
		lMaxSize = (long)audioBufferDefSamples;
	if (lMinSize <= 0)
		lMinSize = lGranularity;

	/* compute nearest buffer size or use the preferred size */
	for (lBufSize = lMinSize; lBufSize + lGranularity <= lMaxSize &&
	       lBufSize != lPreferredSize; lBufSize += lGranularity)
		;

	return (lBufSize);
}

Q_DECL_EXPORT bool
qas_sound_init(const char *, bool)
{
	QMutexLocker locker(&audioMutex);
	unsigned index = 0;

	if (audioInit == true)
		return (true);

	if (audioDeviceSelection >= audioMaxSelection)
		return (true);

	loadAsioDriver(audioDeviceNames[audioDeviceSelection]);

	if (ASIOInit(&audioDriverInfo) != ASE_OK) {
		asioDrivers->removeCurrentDriver();
		return (true);
	}

	if (qas_asio_check_dev_caps()) {
		asioDrivers->removeCurrentDriver();
		return (true);
	}

	audioBufferSamples = qas_asio_get_buffer_size();

	ASIODisposeBuffers();

	for (long i = 0; i != audioInputChannels; i++, index++) {
		ASIOBufferInfo &bi = bufferInfo[index];

		bi.isInput = ASIOTrue;
		bi.channelNum = i;
		bi.buffers[0] = 0;
		bi.buffers[1] = 0;
	}

	for (long i = 0; i != audioOutputChannels; i++, index++) {
		ASIOBufferInfo &bi = bufferInfo[index];

		bi.isInput = ASIOFalse;
		bi.channelNum = i;
		bi.buffers[0] = 0;
		bi.buffers[1] = 0;
	}

	ASIOCreateBuffers(bufferInfo, index, audioBufferSamples, &audioCallbacks);

	audioPostOutput = (ASIOOutputReady() == ASE_OK);

	audioInputBuffer[0] = new float[audioBufferSamples];
	audioInputBuffer[1] = new float[audioBufferSamples];
	audioInputBuffer[2] = new float[audioBufferSamples];

	memset(audioInputBuffer[2], 0, sizeof(float) * audioBufferSamples);

	audioInit = true;

	qas_sound_set_input_channel(0, 0);
	qas_sound_set_input_channel(1, 1);
	qas_sound_set_output_channel(0, 0);
	qas_sound_set_output_channel(1, 1);

	ASIOStart();

	return (false);
}

Q_DECL_EXPORT void
qas_sound_uninit()
{
	if (audioInit == false)
		return;

	audioInit = false;

	ASIOStop();
	ASIODisposeBuffers();
	ASIOExit();
	asioDrivers->removeCurrentDriver();

	QMutexLocker locker(&audioMutex);

	delete[] audioInputBuffer[0];
	delete[] audioInputBuffer[1];
	delete[] audioInputBuffer[2];

	audioInputBuffer[0] = 0;
	audioInputBuffer[1] = 0;
	audioInputBuffer[2] = 0;
}

Q_DECL_EXPORT int
qas_sound_set_input_device(int value)
{
	if (value < 0) {
		if (audioInit == false)
			return (-1);
		else
			return (audioDeviceSelection);
	}

	if ((unsigned)value >= audioMaxSelection)
		value = 0;

	if ((unsigned)value == audioDeviceSelection &&
	    audioInit == true)
		return (value);

	qas_sound_uninit();

	audioDeviceSelection = value;

	if (qas_sound_init(0,0) == false)
		return (audioDeviceSelection);
	else
		return (-1);
}

Q_DECL_EXPORT int
qas_sound_set_output_device(int value)
{
	/* output follows input */
	return (-1);
}

Q_DECL_EXPORT int
qas_sound_set_input_channel(int ch, int which)
{
	if (audioInit == false)
		return (-1);

	if (which > -1)
		audioInputSelection[ch] = which;

	if (audioInputChannels != 0)
		audioInputSelection[ch] %= audioInputChannels;
	else
		audioInputSelection[ch] = 0;

	return (audioInputSelection[ch]);
}

Q_DECL_EXPORT int
qas_sound_set_output_channel(int ch, int which)
{
	if (audioInit == false)
		return (-1);

	if (which > -1)
		audioOutputSelection[ch] = which;

	if (audioOutputChannels != 0)
		audioOutputSelection[ch] %= audioOutputChannels;
	else
		audioOutputSelection[ch] = 0;

	return (audioOutputSelection[ch]);
}

Q_DECL_EXPORT int
qas_sound_max_input_channel()
{
	if (audioInputChannels == 0)
		return (1);
	else
		return (audioInputChannels);
}

Q_DECL_EXPORT int
qas_sound_max_output_channel()
{
	if (audioOutputChannels == 0)
		return (1);
	else
		return (audioOutputChannels);
}

Q_DECL_EXPORT void
qas_sound_get_input_status(QString &status)
{
	const int adev = qas_sound_set_input_device(-1);

	if (adev < 0) {
		status = "Selection of audio device failed";
	} else {
		status = QString("Selected audio device is %1")
		    .arg(audioDeviceNames[adev] ? audioDeviceNames[adev] : "");
	}
}

Q_DECL_EXPORT void
qas_sound_get_output_status(QString &status)
{
	status = "";
}

Q_DECL_EXPORT int
qas_sound_toggle_buffer_samples(int value)
{
	if (value > 0 && value <= QAS_MAX_BUFFER_SAMPLES) {
		if (audioBufferDefSamples != (uint32_t)value) {
			audioBufferDefSamples = value;
			qas_sound_uninit();
			qas_sound_init(0,0);
		}
	}
	return (audioBufferDefSamples);
}

Q_DECL_EXPORT void
qas_sound_rescan()
{
	static char dummy[] = { "dummy" };

	if (audioMaxSelection != 0)
		return;

	for (unsigned x = 0; x != MAX_DRIVERS; x++)
		audioDeviceNames[x] = new char[32];

	for (int timeout = 0;; timeout++) {
		loadAsioDriver(dummy);
		audioMaxSelection = asioDrivers->getDriverNames(audioDeviceNames, MAX_DRIVERS);
		asioDrivers->removeCurrentDriver();

		if (audioMaxSelection == 0) {
			if (timeout == 0)
				ASIOControlPanel();
			else
				break;
		} else {
			break;
		}
	}

	for (unsigned x = audioMaxSelection; x < MAX_DRIVERS; x++) {
		delete[] audioDeviceNames[x];
		audioDeviceNames[x] = 0;
	}

	audioCallbacks.bufferSwitch = &qas_asio_buffer_switch;
	audioCallbacks.sampleRateDidChange = &qas_asio_sample_rate_changed;
	audioCallbacks.asioMessage = &qas_asio_messages;
	audioCallbacks.bufferSwitchTimeInfo = &qas_asio_buffer_switch_time_info;
}

Q_DECL_EXPORT int
qas_sound_max_devices()
{
	return (audioMaxSelection);
}

Q_DECL_EXPORT QString
qas_sound_get_device_name(int index)
{
	if (index < 0 || (unsigned)index >= audioMaxSelection)
		return (QString("Unknown"));
	else
		return (audioDeviceNames[index]);
}

Q_DECL_EXPORT bool
qas_sound_is_input_device(int)
{
	return (true);
}

Q_DECL_EXPORT bool
qas_sound_is_output_device(int)
{
	return (true);
}
