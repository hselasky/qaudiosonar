/*-
 * Copyright (c) 2017 Hans Petter Selasky. All rights reserved.
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

static uint8_t midi_write_buffer[QAS_MIDI_BUFSIZE];
static size_t midi_write_offset;

static uint8_t midi_read_buffer[4];
static size_t midi_read_offset;

void
qas_midi_key_send(uint8_t channel, uint8_t key, uint8_t vel, uint8_t delay)
{
	const uint8_t temp[4] = { (uint8_t)(0x90 | channel),
	    (uint8_t)(key & 0x7F), (uint8_t)(vel & 0x7F), delay };

	atomic_lock();
	if (midi_write_offset <= QAS_MIDI_BUFSIZE - sizeof(temp)) {
		memcpy(midi_write_buffer + midi_write_offset, temp, sizeof(temp));
		midi_write_offset += sizeof(temp);
		atomic_wakeup();
	}
	atomic_unlock();
}

void
qas_midi_delay_send(uint8_t delay)
{
	const uint8_t temp[4] = { 0, 0, 0, delay };

	atomic_lock();
	if (midi_write_offset <= QAS_MIDI_BUFSIZE - sizeof(temp)) {
		memcpy(midi_write_buffer + midi_write_offset, temp, sizeof(temp));
		midi_write_offset += sizeof(temp);
		atomic_wakeup();
	}
	atomic_unlock();
}

static void *
qas_midi_write_thread(void *)
{
	uint8_t buffer[QAS_MIDI_BUFSIZE];
	size_t offset;

	while (1) {
		atomic_lock();
		while (midi_write_offset == 0)
			atomic_wait();

		memcpy(buffer, midi_write_buffer, midi_write_offset);
		offset = midi_write_offset;
		midi_write_offset = 0;
		atomic_unlock();

		for (size_t x = 0; x != offset; x += 4) {
			uint32_t delay = buffer[x + 3];
			if (buffer[x] || buffer[x + 1] || buffer[x + 2]) {
				atomic_lock();
				memcpy(midi_read_buffer, buffer, 3);
				midi_read_offset = 3;
				while (midi_read_offset != 0)
					atomic_wait();
				atomic_unlock();
			}
			if (delay != 0)
				usleep(1000 * delay);
		}
	}
	return (0);
}

Q_DECL_EXPORT int
qas_midi_process(uint8_t *ptr)
{
	int retval;

	atomic_lock();
	if (midi_read_offset != 0) {
		retval = midi_read_offset;
		memcpy(ptr, midi_read_buffer, midi_read_offset);
		midi_read_offset = 0;
		atomic_wakeup();
	} else {
		retval = -1;
	}
	atomic_unlock();

	return (retval);
}

void
qas_midi_init()
{
	pthread_t td;

	pthread_create(&td, NULL, &qas_midi_write_thread, NULL);
}
