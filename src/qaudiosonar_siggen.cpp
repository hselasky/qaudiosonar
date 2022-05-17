/*-
 * Copyright (c) 2016-2022 Hans Petter Selasky. All rights reserved.
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

#include "qaudiosonar_buttonmap.h"
#include "qaudiosonar_siggen.h"

QasBandPassBox :: QasBandPassBox()
{
	setTitle(QString("Band pass center frequency: %1 Hz").arg(qas_sample_rate / 4));

	grid = new QGridLayout(this);

	pSB = new QScrollBar(Qt::Horizontal);

	pSB->setRange(1, qas_sample_rate / 2);
	pSB->setSingleStep(1);
	pSB->setValue(qas_sample_rate / 4);
	connect(pSB, SIGNAL(valueChanged(int)), this, SLOT(handle_value_changed(int)));

	grid->addWidget(pSB, 0,0,1,1);
}

void
QasBandPassBox :: handle_value_changed(int value)
{
	setTitle(QString("Band pass center frequency: %1 Hz").arg(value));
	valueChanged(value);
}

QasBandWidthBox :: QasBandWidthBox()
{
	setTitle("Band width: 20 Hz");

	grid = new QGridLayout(this);

	pSB = new QScrollBar(Qt::Horizontal);

	pSB->setRange(1, qas_sample_rate);
	pSB->setSingleStep(1);
	pSB->setValue(20);
	connect(pSB, SIGNAL(valueChanged(int)), this, SLOT(handle_value_changed(int)));

	grid->addWidget(pSB, 0,0,1,1);
}

void
QasBandWidthBox :: handle_value_changed(int value)
{
	setTitle(QString("Band width: %1 Hz").arg(value));
	valueChanged(value);
}

QasNoiselevelBox :: QasNoiselevelBox()
{
	setTitle(QString("Noise level: %1").arg(-64));

	grid = new QGridLayout(this);

	pSB = new QScrollBar(Qt::Horizontal);

	pSB->setRange(-256, 256);
	pSB->setSingleStep(1);
	pSB->setValue(-64);
	connect(pSB, SIGNAL(valueChanged(int)), this, SLOT(handle_value_changed(int)));

	grid->addWidget(pSB, 0,0,1,1);
}

void
QasNoiselevelBox :: handle_value_changed(int value)
{
	setTitle(QString("Noise level: %1").arg(value));
	valueChanged(value);
}

QasSigGen :: QasSigGen()
{
	gl = new QGridLayout(this);

	map_source_0 = new QasButtonMap("Main input channel\0"
					"INPUT 0\0" "INPUT 1\0" "INPUT 0+1\0"
					"OUTPUT 0\0" "OUTPUT 1\0" "OUTPUT 0+1\0", 6, 3);
	map_source_1 = new QasButtonMap("Correlation input channel\0"
					"INPUT 0\0" "INPUT 1\0" "INPUT 0+1\0"
					"OUTPUT 0\0" "OUTPUT 1\0" "OUTPUT 0+1\0", 6, 3);
	map_output_0 = new QasButtonMap("Output for channel 0\0"
					"SILENCE\0" "BROWN NOISE\0" "WHITE NOISE\0"
					"BROWN NOISE BP\0" "WHITE NOISE BP\0", 5, 3);
	map_output_1 = new QasButtonMap("Output for channel 1\0"
					"SILENCE\0" "BROWN NOISE\0" "WHITE NOISE\0"
					"BROWN NOISE BP\0" "WHITE NOISE BP\0", 5, 3);

	bp_box_0 = new QasBandPassBox();
	bw_box_0 = new QasBandWidthBox();
	nl_box_0 = new QasNoiselevelBox();
	handle_filter_0(0);

	connect(map_source_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_source_0(int)));
	connect(map_source_1, SIGNAL(selectionChanged(int)), this, SLOT(handle_source_1(int)));
	connect(map_output_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_output_0(int)));
	connect(map_output_1, SIGNAL(selectionChanged(int)), this, SLOT(handle_output_1(int)));
	connect(bp_box_0, SIGNAL(valueChanged(int)), this, SLOT(handle_filter_0(int)));
	connect(bw_box_0, SIGNAL(valueChanged(int)), this, SLOT(handle_filter_0(int)));
	connect(nl_box_0, SIGNAL(valueChanged(int)), this, SLOT(handle_filter_0(int)));

	gl->addWidget(map_source_0, 0,0,1,1);
	gl->addWidget(map_source_1, 1,0,1,1);
	gl->addWidget(map_output_0, 2,0,1,1);
	gl->addWidget(map_output_1, 3,0,1,1);
	gl->addWidget(bp_box_0, 0,1,1,1);
	gl->addWidget(bw_box_0, 1,1,1,1);
	gl->addWidget(nl_box_0, 2,1,1,1);
	gl->setRowStretch(4,1);
	gl->setColumnStretch(2,1);
}

void
QasSigGen :: handle_source_0(int _value)
{
	atomic_lock();
	qas_source_0 = _value;
	atomic_unlock();
}

void
QasSigGen :: handle_source_1(int _value)
{
	atomic_lock();
	qas_source_1 = _value;
	atomic_unlock();
}

void
QasSigGen :: handle_output_0(int _value)
{
	atomic_lock();
	qas_output_0 = _value;
	atomic_unlock();
}

static void
qas_low_pass(double freq, double *factor, size_t window_size)
{
	int wh = window_size / 2;
	int x;

	freq /= (double)qas_sample_rate;
	freq *= (double)wh;

	factor[wh] += (2.0 * freq) / ((double)wh);
	freq *= (2.0 * M_PI) / ((double)wh);

	for (x = -wh+1; x < wh; x++) {
		if (x == 0)
			continue;
		factor[x + wh] +=
			sin(freq * (double)(x)) / (M_PI * (double)(x));
	}
}

static void
qas_band_pass(double freq_low, double freq_high,
    double *factor, size_t window_size)
{
	double low = qas_sample_rate / window_size;
	double high = qas_sample_rate / 2;

	if (low < 1.0)
		low = 1.0;
	/* lowpass */
	if (freq_low < low)
		freq_low = low;
	qas_low_pass(freq_low, factor, window_size);

	/* highpass */
	if (freq_high >= high)
		freq_high = high;
	qas_low_pass(-freq_high, factor, window_size);
}

void
QasSigGen :: handle_filter_0(int value)
{
	double temp[QAS_CORR_SIZE];
	double adjust = bw_box_0->pSB->value() / 2.0;
	double center = bp_box_0->pSB->value();
	int noiseLevel = nl_box_0->pSB->value();

	memset(temp, 0, sizeof(temp));
	qas_band_pass(center - adjust, center + adjust, temp, QAS_CORR_SIZE);

	atomic_lock();
	for (size_t x = 0; x != QAS_CORR_SIZE; x++)
		qas_band_pass_filter[x] = temp[x];
	qas_noise_level = pow(2.0, noiseLevel / 16.0);
	atomic_unlock();
}

void
QasSigGen :: handle_output_1(int _value)
{
	atomic_lock();
	qas_output_1 = _value;
	atomic_unlock();
}
