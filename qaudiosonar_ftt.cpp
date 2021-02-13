/*-
 * Copyright (c) 2021 Hans Petter Selasky. All rights reserved.
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

/*
 * This file implements the fast triangle wave transform. This
 * transform has the advantage over the fast fourier transform, that
 * no precision is lost, and no irrational values are used. That means
 * the result is always exact.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "qaudiosonar.h"

/* Helper function to add one bitreversed starting at "mask" */

static uint32_t
qas_ftt_add_bitreversed(uint32_t x, uint32_t mask)
{
	do {
		x ^= mask;
	} while ((x & mask) == 0 && (mask /= 2) != 0);

	return (x);
}

static uint32_t
qas_bitrev32(uint32_t a)
{
	a = ((a & 0x55555555) << 1) | ((a & 0xAAAAAAAA) >> 1);
	a = ((a & 0x33333333) << 2) | ((a & 0xCCCCCCCC) >> 2);
	a = ((a & 0x0F0F0F0F) << 4) | ((a & 0xF0F0F0F0) >> 4);
	a = ((a & 0x00FF00FF) << 8) | ((a & 0xFF00FF00) >> 8);
	a = ((a & 0x0000FFFF) << 16) | ((a & 0xFFFF0000) >> 16);
	return (a);
}

/*
 * Returns the phase of a triangular function.
 */
static double
qas_ftt_acos(double x)
{
	x = fabs(x);

	/* check for special case */
	if (x == 1.0)
		return (0.0);
	else if (x == 0.0)
		return (0.25);
	else
		return (ceil(x) - x) * (1.0 / 4.0);
}

/*
 * Returns a triangular wave function based on phase "x".
 */
double
qas_ftt_cos(double x)
{
	x = x - floor(x);

	/* check for special case */
	if (x == 0.0)
		return (1.0);
	else if (x == 0.5)
		return (-1.0);

	x = x * 4.0;

	/* check quadrant */
	if (x < 1.0)
		x = ceil(x) - x;
	else if (x < 2.0)
		x = floor(x) - x;
	else if (x < 3.0)
		x = x - ceil(x);
	else
		x = x - floor(x);
	return (x);
}

/*
 * Returns a triangular wave function based on phase "x".
 */
double
qas_ftt_sin(double x)
{
	return (qas_ftt_cos(x + 0.75));
}

/*
 * Two dimensional vector multiplication for triangular wave functions.
 */
qas_complex_t
qas_ftt_multiply(qas_complex_t a, qas_complex_t b)
{
	/* Compute vector gain */
	const double ga = fabs(a.x) + fabs(a.y);
	const double gb = fabs(b.x) + fabs(b.y);

	/* Figure out quadrants */
	const uint8_t qa = (a.x < 0.0) + 2 * (a.y < 0.0);
	const uint8_t qb = (b.x < 0.0) + 2 * (b.y < 0.0);

	/* Normalize input vectors, "cosine" argument */
	if (ga != 0.0)
		a.x /= ga;

	if (gb != 0.0)
		b.x /= gb;

	/* Compute output vector gain */
	const double gr = ga * gb;

	double angle;

	/* Add the two angles, that's part of vector multiplication */
	switch (qa) {
	case 0:
		angle = qas_ftt_acos(a.x);
		break;
	case 1:
		angle = 0.5 - qas_ftt_acos(a.x);
		break;
	case 2:
		angle = 1.0 - qas_ftt_acos(a.x);
		break;
	case 3:
		angle = 0.5 + qas_ftt_acos(a.x);
		break;
	default:
		angle = 0.0;
		break;
	}

	switch (qb) {
	case 0:
		angle += qas_ftt_acos(b.x);
		break;
	case 1:
		angle += 0.5 - qas_ftt_acos(b.x);
		break;
	case 2:
		angle += 1.0 - qas_ftt_acos(b.x);
		break;
	case 3:
		angle += 0.5 + qas_ftt_acos(b.x);
		break;
	default:
		break;
	}

	/* Restore output vector */
	return ((qas_complex_t){
	    qas_ftt_cos(angle) * gr,
	    qas_ftt_sin(angle) * gr
	});
}

/*
 * Two dimensional vector multiplication for triangular wave functions.
 */
static qas_complex_t
qas_ftt_angleadd(qas_complex_t a, double angle)
{
	/* Compute vector gain */
	const double ga = fabs(a.x) + fabs(a.y);

	/* Figure out quadrants */
	const uint8_t qa = (a.x < 0.0) + 2 * (a.y < 0.0);

	/* Normalize input vectors, "cosine" argument */
	if (ga != 0.0)
		a.x /= ga;

	/* Add the two angles, that's part of vector multiplication */
	switch (qa) {
	case 0:
		angle += qas_ftt_acos(a.x);
		break;
	case 1:
		angle += 0.5 - qas_ftt_acos(a.x);
		break;
	case 2:
		angle += 1.0 - qas_ftt_acos(a.x);
		break;
	case 3:
		angle += 0.5 + qas_ftt_acos(a.x);
		break;
	default:
		break;
	}

	/* Restore output vector */
	return ((qas_complex_t){
	    qas_ftt_cos(angle) * ga,
	    qas_ftt_sin(angle) * ga
	});
}

/* Two dimensional vector addition for triangular wave functions. */

qas_complex_t
qas_ftt_add(qas_complex_t a, qas_complex_t b)
{
	return ((qas_complex_t){ a.x + b.x, a.y + b.y });
}

/* Two dimensional vector subtraction for triangular wave functions. */

qas_complex_t
qas_ftt_sub(qas_complex_t a, qas_complex_t b)
{
	return ((qas_complex_t){ a.x - b.x, a.y - b.y });
}

/* Fast Forward Triangular Transform for two dimensional vector data. */

void
qas_ftt_fwd(qas_complex_t *ptr, uint8_t log2_size)
{
	const uint32_t max = 1U << log2_size;
	qas_complex_t t[2];
	uint32_t y;
	uint32_t z;

	for (uint32_t step = max; (step /= 2);) {
		for (y = z = 0; y != max; y += 2 * step) {
			const double angle = (double)z / (double)max;

			/* do transform */
			for (uint32_t x = 0; x != step; x++) {
				t[0] = ptr[x + y];
				t[1] = qas_ftt_angleadd(ptr[x + y + step], angle);

				ptr[x + y] = qas_ftt_add(t[0], t[1]);
				ptr[x + y + step] = qas_ftt_sub(t[0], t[1]);
			}

			/* update index */
			z = qas_ftt_add_bitreversed(z, max / 4);
		}
	}

	/* bitreverse */
	for (uint32_t x = 0; x != max; x++) {
		y = qas_bitrev32(x << (32 - log2_size));
		if (y < x) {
			/* swap */
			t[0] = ptr[x];
			ptr[x] = ptr[y];
			ptr[y] = t[0];
		}
	}
}

/* Fast Inverse Triangular Transform for two dimensional vector data. */

void
qas_ftt_inv(qas_complex_t *ptr, uint8_t log2_size)
{
	const uint32_t max = 1U << log2_size;
	qas_complex_t t[2];
	uint32_t y;
	uint32_t z;

	/* bitreverse */
	for (uint32_t x = 0; x != max; x++) {
		y = qas_bitrev32(x << (32 - log2_size));
		if (y < x) {
			/* swap */
			t[0] = ptr[x];
			ptr[x] = ptr[y];
			ptr[y] = t[0];
		}
	}

	for (uint32_t step = 1; step != max; step *= 2) {
		for (y = z = 0; y != max; y += 2 * step) {
			const double angle = (double)(max - z) / (double)max;

			/* do transform */
			for (uint32_t x = 0; x != step; x++) {
				t[0] = qas_ftt_add(ptr[x + y], ptr[x + y + step]);
				t[1] = qas_ftt_sub(ptr[x + y], ptr[x + y + step]);

				ptr[x + y] = t[0];
				ptr[x + y + step] = qas_ftt_angleadd(t[1], angle);
			}

			/* update index */
			z = qas_ftt_add_bitreversed(z, max / 4);
		}
	}
}
