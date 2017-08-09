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

struct qas_mul_double_context *qas_mul_context;

static uint8_t
qas_sumbits32(uint32_t val)
{
	val = ((val & (1U * 0xAAAAAAAAU)) / 2U) + (val & (1U * 0x55555555U));
	val = ((val & (3U * 0x44444444U)) / 4U) + (val & (3U * 0x11111111U));
	val = ((val & (15U * 0x10101010U)) / 16U) + (val & (15U * 0x01010101U));
	val += val >> 8;
	val += val >> 16;
	return (val & 63);
}

static void
qas_mul_xform_inv_sub_double(double *ptr, uint8_t log2_max)
{
	const uint32_t max = 1U << log2_max;
	uint32_t x;
	uint32_t y;
	uint32_t z;

#ifdef CHECK_ZERO
	double sum = 0;

#endif

	if (max < 2)
		return;

	for (y = 0; y != max; y += 2) {
#ifdef CHECK_ZERO
		sum += fabs(ptr[y + 1]) + fabs(ptr[y]);
#endif
		ptr[y + 1] -= ptr[y];
	}
#ifdef CHECK_ZERO
	if (sum == 0.0)
		return;
#endif
	if (max < 4)
		return;

	for (y = 0; y != max; y += 4) {
		ptr[y + 2] -= ptr[y];
		ptr[y + 3] -= ptr[y + 1];
	}

	for (x = 4; x != max; x *= 2) {
		for (y = 0; y != max; y += (2 * x)) {
			for (z = 0; z != x; z += 4) {
				ptr[y + z + x] -= ptr[y + z];
				ptr[y + z + 1 + x] -= ptr[y + z + 1];
				ptr[y + z + 2 + x] -= ptr[y + z + 2];
				ptr[y + z + 3 + x] -= ptr[y + z + 3];
			}
		}
	}
}

static void
qas_mul_xform_fwd_sub_double(double *ptr, uint8_t log2_max)
{
	const uint32_t max = 1U << log2_max;
	uint32_t x;
	uint32_t y;
	uint32_t z;

#ifdef CHECK_ZERO
	double sum = 0;

#endif

	if (max < 2)
		return;

	for (y = 0; y != max; y += 2) {
#ifdef CHECK_ZERO
		sum += fabs(ptr[y + 1]) + fabs(ptr[y]);
#endif
		ptr[y + 1] += ptr[y];
	}
#ifdef CHECK_ZERO
	if (sum == 0.0)
		return;
#endif
	if (max < 4)
		return;

	for (y = 0; y != max; y += 4) {
		ptr[y + 2] += ptr[y];
		ptr[y + 3] += ptr[y + 1];
	}

	for (x = 4; x != max; x *= 2) {
		for (y = 0; y != max; y += (2 * x)) {
			for (z = 0; z != x; z += 4) {
				ptr[y + z + x] += ptr[y + z];
				ptr[y + z + 1 + x] += ptr[y + z + 1];
				ptr[y + z + 2 + x] += ptr[y + z + 2];
				ptr[y + z + 3 + x] += ptr[y + z + 3];
			}
		}
	}
}

static uint32_t
qas_mul_map_fwd_32(uint32_t x, uint32_t k, uint32_t msb)
{
	x &= ~k;

	while ((msb /= 2) != 0) {
		if (k & msb)
			x -= (x & -msb) / 2;
	}
	return (x);
}

struct qas_mul_double_context *
qas_mul_double_context_alloc(void)
{
	const uint32_t imax = QAS_MUL_SIZE;
	const size_t size = sizeof(struct qas_mul_double_context);
	struct qas_mul_double_context *table =
	    (struct qas_mul_double_context *)malloc(size);
	uint32_t x, y, z, u;

	if (table != NULL) {
		for (z = x = 0; x != imax; x++) {
			table->offset[x] = z;
			u = qas_sumbits32(imax - 1 - x);
			z += (1U << u);
		}
		for (z = x = 0; x != imax; x++) {
			for (y = 0; y != imax; y++, z++) {
				y |= x;
				table->table[z] =
				    table->offset[imax - 1 - y] +
				    qas_mul_map_fwd_32(y & ~x, ~y & ~x, imax);
			}
		}
	}
	return (table);
}

void
qas_mul_xform_inv_double(double *ptr, uint32_t imax)
{
	uint32_t x, y, z;

	for (z = x = 0; x != imax; x++) {
		y = qas_sumbits32(imax - 1 - x);
		qas_mul_xform_inv_sub_double(ptr + z, y);
		z += (1U << y);
	}
}

void
qas_mul_xform_fwd_double(double *ptr, uint32_t imax)
{
	uint32_t x, y, z;

	for (z = x = 0; x != imax; x++) {
		y = qas_sumbits32(imax - 1 - x);
		qas_mul_xform_fwd_sub_double(ptr + z, y);
		z += (1U << y);
	}
}

void
qas_mul_import_double(const double *src, double *dst, uint32_t imax)
{
	uint32_t x, y, z;

	for (z = x = 0; x != imax; x++) {
		for (y = 0; y != imax; y++, z++) {
			y |= x;
			dst[z] = src[y];
		}
	}
}

void
qas_mul_export_double(const double *src, double *dst, uint32_t imax)
{
	uint32_t x, y, z, t;

	for (t = (2 * imax) - 2, z = x = 0; x != imax; x++, t--) {
		for (y = 0; y != imax; y++, z++) {
			y |= x;
			dst[t - y] += src[z];
		}
	}
}
