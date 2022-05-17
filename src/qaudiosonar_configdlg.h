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

#ifndef _QAS_CONFIGDLG_H_
#define	_QAS_CONFIGDLG_H_

#include "qaudiosonar.h"

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QListWidget>

class QasDeviceSelection : public QGroupBox {
	Q_OBJECT;
public:
	QasDeviceSelection() : gl(this),
	    b_input_device(),
	    b_output_device(),
	    b_toggle_buffer_samples(tr("Toggle buffer size")),
	    b_rescan_device("Rescan devices") {
		setTitle("Audio device configuration");

		s_input_left.setAccessibleDescription("Set left input channel index");
		s_input_left.setPrefix(QString("L-IN "));

		s_input_right.setAccessibleDescription("Set right input channel index");
		s_input_right.setPrefix(QString("R-IN "));

		s_output_left.setAccessibleDescription("Set left output channel index");
		s_output_left.setPrefix(QString("L-OUT "));

		s_output_right.setAccessibleDescription("Set right output channel index");
		s_output_right.setPrefix(QString("R-OUT "));

		gl.addWidget(&b_input_device, 0,0,2,1);
		gl.addWidget(&s_input_left, 1,1);
		gl.addWidget(&s_input_right, 1,2);
		gl.addWidget(&l_input, 1,3);

#if !defined(HAVE_ASIO_AUDIO)
		gl.addWidget(&b_output_device, 2,0,2,1);
#endif
		gl.addWidget(&s_output_left, 3,1);
		gl.addWidget(&s_output_right, 3,2);

#if !defined(HAVE_ASIO_AUDIO)
		gl.addWidget(&l_output, 3,3);
#endif

		gl.addWidget(&b_toggle_buffer_samples, 4,1);
		gl.addWidget(&l_buffer_samples, 4,2,1,2);
		gl.addWidget(&b_rescan_device, 4,0,1,1);

		connect(&b_input_device, SIGNAL(currentRowChanged(int)), this, SLOT(handle_set_input_device(int)));
		connect(&b_output_device, SIGNAL(currentRowChanged(int)), this, SLOT(handle_set_output_device(int)));

		connect(&s_input_left, SIGNAL(valueChanged(int)), this, SLOT(handle_set_input_left(int)));
		connect(&s_output_left, SIGNAL(valueChanged(int)), this, SLOT(handle_set_output_left(int)));

		connect(&s_input_right, SIGNAL(valueChanged(int)), this, SLOT(handle_set_input_right(int)));
		connect(&s_output_right, SIGNAL(valueChanged(int)), this, SLOT(handle_set_output_right(int)));

		connect(&b_toggle_buffer_samples, SIGNAL(released()), this, SLOT(handle_toggle_buffer_samples()));
		connect(&b_rescan_device, SIGNAL(released()), this, SLOT(handle_rescan_device()));

		handle_toggle_buffer_samples(0);
	};
	QGridLayout gl;
	QListWidget b_input_device;
	QSpinBox s_input_left;
	QSpinBox s_input_right;
	QListWidget b_output_device;
	QSpinBox s_output_left;
	QSpinBox s_output_right;
	QLabel l_input;
	QLabel l_output;
	QPushButton b_toggle_buffer_samples;
	QPushButton b_rescan_device;
	QLabel l_buffer_samples;

	void refreshStatus();

public slots:
	void handle_rescan_device(bool = true);
	int handle_set_input_device(int);
	int handle_set_output_device(int);
	int handle_set_input_left(int);
	int handle_set_output_left(int);
	int handle_set_input_right(int);
	int handle_set_output_right(int);
	int handle_toggle_buffer_samples(int = -1);
};

class QasConfigDlg : public QWidget {
	Q_OBJECT;
public:
	QasConfigDlg() : gl(this) {
		gl.addWidget(&audio_dev, 0,0,1,1);
		gl.setRowStretch(1,1);
		gl.setColumnStretch(1,1);
	};
	QGridLayout gl;
	QasDeviceSelection audio_dev;
};

#endif		/* _QAS_CONFIGDLG_H_ */
