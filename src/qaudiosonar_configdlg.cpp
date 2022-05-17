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

#include <QMutexLocker>
#include <QKeyEvent>
#include <QListWidgetItem>

#include "qaudiosonar.h"
#include "qaudiosonar_configdlg.h"

void
QasDeviceSelection :: handle_rescan_device(bool forced)
{
	int max;
	int in_index;
	int out_index;

	if (forced)
		qas_sound_rescan();

	max = qas_sound_max_devices();
	in_index = qas_sound_set_input_device(-1);
	out_index = qas_sound_set_output_device(-1);

	QAS_NO_SIGNAL(b_input_device,clear());
	QAS_NO_SIGNAL(b_output_device,clear());

	for (int x = 0; x < max; x++) {
		QString name = qas_sound_get_device_name(x);
		QListWidgetItem item(name);

		if (qas_sound_is_input_device(x)) {
			item.setFlags(item.flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
			QAS_NO_SIGNAL(b_input_device,addItem(new QListWidgetItem(item)));
		} else {
			item.setFlags(item.flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
			QAS_NO_SIGNAL(b_input_device,addItem(new QListWidgetItem(item)));
		}
		if (qas_sound_is_output_device(x)) {
			item.setFlags(item.flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
			QAS_NO_SIGNAL(b_output_device,addItem(new QListWidgetItem(item)));
		} else {
			item.setFlags(item.flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
			QAS_NO_SIGNAL(b_output_device,addItem(new QListWidgetItem(item)));
		}
	}

	if (max > 0 && in_index > -1 && in_index < max) {
		QAS_NO_SIGNAL(b_input_device,setCurrentRow(in_index));
		if (forced)
			qas_sound_set_input_device(in_index);
	}

	if (max > 0 && out_index > -1 && out_index < max) {
		QAS_NO_SIGNAL(b_output_device,setCurrentRow(out_index));
		if (forced)
			qas_sound_set_output_device(out_index);
	}
}

int
QasDeviceSelection :: handle_set_input_device(int value)
{
	if (value < 0 || qas_sound_is_input_device(value)) {
		const int input = qas_sound_set_input_device(value);
		refreshStatus();
		return (input);
	} else {
		return (-1);
	}
}

int
QasDeviceSelection :: handle_set_input_left(int value)
{
	const int input = qas_sound_set_input_channel(0, value - 1);
	return (input);
}

int
QasDeviceSelection :: handle_set_input_right(int value)
{
	const int input = qas_sound_set_input_channel(1, value - 1);
	return (input);
}

int
QasDeviceSelection :: handle_set_output_device(int value)
{
	if (value < 0 || qas_sound_is_output_device(value)) {
		const int output = qas_sound_set_output_device(value);
		refreshStatus();
		return (output);
	} else {
		return (-1);
	}
}

int
QasDeviceSelection :: handle_set_output_left(int value)
{
	const int output = qas_sound_set_output_channel(0, value - 1);
	return (output);
}

int
QasDeviceSelection :: handle_set_output_right(int value)
{
	const int output = qas_sound_set_output_channel(1, value - 1);
	return (output);
}

int
QasDeviceSelection :: handle_toggle_buffer_samples(int value)
{
	if (value < 0) {
		value = qas_sound_toggle_buffer_samples(-1);
		if (value <= 32)
			value = 64;
		else if (value <= 64)
			value = 96;
		else if (value <= 96)
			value = 128;
		else
			value = 32;
		value = qas_sound_toggle_buffer_samples(value);
	} else if (value == 0) {
		value = qas_sound_toggle_buffer_samples(-1);
	} else {
		value = qas_sound_toggle_buffer_samples(value);
	}
	if (value > -1)
		l_buffer_samples.setText(QString("%1 samples").arg(value));
	else
		l_buffer_samples.setText(QString("System default buffer size"));
	return (value);
}

void
QasDeviceSelection :: refreshStatus()
{
	QString status;
	int ch;

	qas_sound_get_input_status(status);
	l_input.setText(status);

	qas_sound_get_output_status(status);
	l_output.setText(status);

	s_input_left.setRange(1, qas_sound_max_input_channel());
	s_input_right.setRange(1, qas_sound_max_input_channel());

	s_output_left.setRange(1, qas_sound_max_output_channel());
	s_output_right.setRange(1, qas_sound_max_output_channel());

	ch = qas_sound_set_input_channel(0, -1);
	if (ch < 0)
		ch = 0;
	QAS_NO_SIGNAL(s_input_left,setValue(ch + 1));

	ch = qas_sound_set_input_channel(1, -1);
	if (ch < 0)
		ch = 0;
	QAS_NO_SIGNAL(s_input_right,setValue(ch + 1));

	ch = qas_sound_set_output_channel(0, -1);
	if (ch < 0)
		ch = 0;
	QAS_NO_SIGNAL(s_output_left,setValue(ch + 1));

	ch = qas_sound_set_output_channel(1, -1);
	if (ch < 0)
		ch = 0;
	QAS_NO_SIGNAL(s_output_right,setValue(ch + 1));

	handle_rescan_device(false);
}
