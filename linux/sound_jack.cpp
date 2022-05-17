/*-
 * Copyright (c) 2020-2022 Hans Petter Selasky.
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
#include <QString>

#include "../src/qaudiosonar.h"

#include <jack/jack.h>
#include <jack/midiport.h>

static jack_port_t *input_port_left;
static jack_port_t *input_port_right;
static jack_port_t *output_port_left;
static jack_port_t *output_port_right;
static jack_port_t *output_port_midi;
static jack_port_t *input_port_midi;

static jack_client_t *jack_client;
static int jack_is_shutdown;

static int
qas_sound_process_cb(jack_nframes_t nframes, void *arg)
{
	jack_midi_event_t event;
	int events;
	int error;

	const float *in_left = (jack_default_audio_sample_t *)
	    jack_port_get_buffer(input_port_left, nframes);
	const float *in_right = (jack_default_audio_sample_t *)
	    jack_port_get_buffer(input_port_right, nframes);

	float *out_left = (jack_default_audio_sample_t *)
	    jack_port_get_buffer(output_port_left, nframes);
	float *out_right = (jack_default_audio_sample_t *)
	    jack_port_get_buffer(output_port_right, nframes);

	uint8_t *in_midi = (uint8_t *)
	    jack_port_get_buffer(input_port_midi, nframes);
	uint8_t *out_midi = (uint8_t *)
	    jack_port_get_buffer(output_port_midi, nframes);

#ifdef JACK_MIDI_NEEDS_NFRAMES
	jack_midi_clear_buffer(out_midi, nframes);
#else
	jack_midi_clear_buffer(out_midi);
#endif
	if (jack_is_shutdown != 0) {
		memset(out_left, 0, sizeof(out_left[0]) * nframes);
		memset(out_right, 0, sizeof(out_right[0]) * nframes);
	} else {
		memcpy(out_left, in_left, sizeof(out_left[0]) * nframes);
		memcpy(out_right, in_right, sizeof(out_right[0]) * nframes);

#ifdef JACK_MIDI_NEEDS_NFRAMES
		events = jack_midi_get_event_count(in_midi, nframes);
#else
		events = jack_midi_get_event_count(in_midi);
#endif
		for (int i = 0; i < events; i++) {
#ifdef JACK_MIDI_NEEDS_NFRAMES
			error = jack_midi_event_get(&event, in_midi, i, nframes);
#else
			error = jack_midi_event_get(&event, in_midi, i);
#endif
			if (error)
				continue;
		}

		qas_sound_process(out_left, out_right, nframes);

		for (int i = 0;; i++) {
			uint8_t mbuf[4] = {};
			void *buffer;
			int len = qas_midi_process(mbuf);
			if (len < 1)
				break;
#ifdef JACK_MIDI_NEEDS_NFRAMES
			buffer = jack_midi_event_reserve(out_midi, i, len, nframes);
#else
			buffer = jack_midi_event_reserve(out_midi, i, len);
#endif
			if (buffer == 0)
				break;
			memcpy(buffer, mbuf, len);
		}
	}
	return (0);
}

static int
qas_sound_buffer_size_cb(jack_nframes_t, void *arg)
{
	return (0);
}

static void
qas_sound_shutdown_cb(void *arg)
{

}

Q_DECL_EXPORT bool
qas_sound_init(const char *name, bool auto_connect)
{
	jack_status_t status;

	jack_client = jack_client_open(name, JackNoStartServer, &status);
	if (jack_client == 0)
		return (true);

	jack_set_process_callback(jack_client, qas_sound_process_cb, 0);
	jack_set_buffer_size_callback(jack_client, qas_sound_buffer_size_cb, 0);
	jack_on_shutdown(jack_client, qas_sound_shutdown_cb, 0);

	if (jack_get_sample_rate(jack_client) != QAS_SAMPLE_RATE) {
		jack_client_close(jack_client);
		jack_client = 0;
		return (true);
	}

	input_port_left = jack_port_register(jack_client, "input_1",
	    JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

	input_port_right = jack_port_register(jack_client, "input_2",
	    JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

	output_port_left = jack_port_register(jack_client, "output_1",
	    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	output_port_right = jack_port_register(jack_client, "output_2",
	    JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	input_port_midi = jack_port_register(jack_client, "input_midi",
	    JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

	output_port_midi = jack_port_register(jack_client, "output_midi",
	    JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (input_port_left == 0 || input_port_right == 0 ||
	    input_port_midi == 0 ||
	    output_port_left == 0 || output_port_right == 0 ||
	    output_port_midi == 0) {
		jack_client_close(jack_client);
		jack_client = 0;
		return (true);
	}

	if (jack_activate(jack_client)) {
		jack_client_close(jack_client);
		jack_client = 0;
		return (true);
	}

	if (auto_connect) {
		const char **ports;

		if ((ports = jack_get_ports(jack_client, 0, 0,
		    JackPortIsPhysical | JackPortIsOutput)) != 0) {
			jack_connect(jack_client, ports[0], jack_port_name(input_port_left));

			/*
			 * Try to connect both input ports, because
			 * the application expects stereo data:
			 */
			if (ports[1]) {
				jack_connect(jack_client, ports[1], jack_port_name(input_port_right));
			} else {
				jack_connect(jack_client, ports[0], jack_port_name(input_port_right));
			}
			jack_free(ports);
		}

		if ((ports = jack_get_ports(jack_client, 0, 0,
		    JackPortIsPhysical | JackPortIsInput)) != 0) {
			jack_connect(jack_client, jack_port_name(output_port_left), ports[0]);

			if (ports[1]) {
				jack_connect(jack_client, jack_port_name(output_port_right), ports[1]);
			}
			jack_free(ports);
		}
	}
	return (false);			/* success */
}

Q_DECL_EXPORT void
qas_sound_uninit()
{
	if (jack_client == 0)
		return;

	jack_is_shutdown = 1;

	usleep(100000);

	jack_port_disconnect(jack_client, input_port_left);
	jack_port_disconnect(jack_client, input_port_right);
	jack_port_disconnect(jack_client, output_port_left);
	jack_port_disconnect(jack_client, output_port_right);
	jack_port_disconnect(jack_client, input_port_midi);
	jack_port_disconnect(jack_client, output_port_midi);

	jack_deactivate(jack_client);
	jack_port_unregister(jack_client, input_port_left);
	jack_port_unregister(jack_client, input_port_right);
	jack_port_unregister(jack_client, output_port_left);
	jack_port_unregister(jack_client, output_port_right);
	jack_port_unregister(jack_client, input_port_midi);
	jack_port_unregister(jack_client, output_port_midi);
	jack_client_close(jack_client);
	jack_client = 0;
}

Q_DECL_EXPORT int
qas_sound_set_input_device(int)
{
	return (0);
}

Q_DECL_EXPORT int
qas_sound_set_output_device(int)
{
	return (0);
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

Q_DECL_EXPORT int
qas_sound_set_input_channel(int ch, int)
{
	return (ch);
}

Q_DECL_EXPORT int
qas_sound_set_output_channel(int ch, int)
{
	return (ch);
}

Q_DECL_EXPORT int
qas_sound_max_input_channel()
{
	return (2);
}

Q_DECL_EXPORT int
qas_sound_max_output_channel()
{
	return (2);
}

Q_DECL_EXPORT void
qas_sound_get_input_status(QString &status)
{
	status = "Default audio input device";
}

Q_DECL_EXPORT void
qas_sound_get_output_status(QString &status)
{
	status = "Default audio output device";
}

Q_DECL_EXPORT int
qas_sound_toggle_buffer_samples(int)
{
	return (-1);
}

Q_DECL_EXPORT void
qas_sound_rescan()
{

}

Q_DECL_EXPORT int
qas_sound_max_devices()
{
	return (1);
}

Q_DECL_EXPORT QString
qas_sound_get_device_name(int)
{
	return (QString("JACK"));
}
