/*-
 * Copyright (c) 2018 Hans Petter Selasky. All rights reserved.
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

const double qas_iso_freq_table[QAS_STANDARD_AUDIO_BANDS] = {
	20, 25, 31.5, 40, 50, 63, 80, 100, 125,
	160, 200, 250, 315, 400, 500, 630, 800, 1000,
	1250, 1600, 2000, 2500, 3150, 4000, 5000,
	6300, 8000, 10000, 12500, 16000, 20000
};

uint8_t
qas_find_iso(double cf)
{
	unsigned y;

	for (y = 0; y != QAS_STANDARD_AUDIO_BANDS; y++) {
		if (y == 0) {
			double limit = (qas_iso_freq_table[0] + qas_iso_freq_table[1]) / 2.0;
			if (cf < limit)
				return (y + 1);
		} else if (y == QAS_STANDARD_AUDIO_BANDS - 1) {
			double limit = (qas_iso_freq_table[y] + qas_iso_freq_table[y - 1]) / 2.0;
			if (cf >= limit)
				return (y + 1);
		} else {
			double lower_limit = (qas_iso_freq_table[y] + qas_iso_freq_table[y - 1]) / 2.0;
			double upper_limit = (qas_iso_freq_table[y] + qas_iso_freq_table[y + 1]) / 2.0;
			if (cf >= lower_limit && cf < upper_limit)
				return (y + 1);
		}
	}
	return (0);
}
