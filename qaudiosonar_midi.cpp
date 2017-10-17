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

char	midi_write_device[1024];
uint8_t midi_write_buffer[QAS_MIDI_BUFSIZE];
size_t	midi_write_offset;

void
qas_midi_key_send(uint8_t key, uint8_t vel)
{
	uint8_t temp[4] = { 0x90, (uint8_t)(key & 0x7F), (uint8_t)(vel & 0x7F), 0 };

	atomic_lock();
	if (midi_write_offset <= QAS_MIDI_BUFSIZE - sizeof(temp)) {
		memcpy(midi_write_buffer + midi_write_offset, temp, sizeof(temp));
		midi_write_offset += sizeof(temp);
	  	atomic_wakeup();
	}
	atomic_unlock();
}

void *
qas_midi_write_thread(void *)
{
	static char fname[1024];
	uint8_t buffer[QAS_MIDI_BUFSIZE];
	ssize_t offset;
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
		strlcpy(fname, midi_write_device, sizeof(fname));
		midi_write_offset = 0;
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

		while (1) {
			atomic_lock();
			while (midi_write_offset == 0)
				atomic_wait();
			memcpy(buffer, midi_write_buffer, midi_write_offset);
			offset = midi_write_offset;
			midi_write_offset = 0;
			atomic_wakeup();
			if (strcmp(fname, midi_write_device)) {
				atomic_unlock();
				break;
			}
			atomic_unlock();

			err = write(f, buffer, offset);
			if (err != offset)
				break;
		}
	}
	return (0);
}