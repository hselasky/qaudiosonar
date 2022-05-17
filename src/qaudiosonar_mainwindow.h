/*-
 * Copyright (c) 2020-2022 Hans Petter Selasky. All rights reserved.
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

#ifndef _QAS_MAINWINDOW_H_
#define	_QAS_MAINWINDOW_H_

#include "qaudiosonar.h"

class QasSpectrum;
class QasSigGen;
class QasConfig;

class QasMainButton : public QPushButton {
	Q_OBJECT;
public:
	QasMainButton(const QString &str) : QPushButton(str) {
		flashing = false;
		flashstate = false;
		connect(&watchdog, SIGNAL(timeout()), this, SLOT(handle_timeout()));
		connect(this, SIGNAL(released()), this, SLOT(handle_released()));
	};
	bool flashing;
	bool flashstate;
	QTimer watchdog;

	void paintEvent(QPaintEvent *);
	void setFlashing() {
		watchdog.start(1000);
		flashing = true;
		flashstate = false;
	};
public slots:
	void handle_timeout();
	void handle_released();
};

class QasMainWindow : public QWidget {
	Q_OBJECT;
public:
	QasMainWindow();
	QGridLayout gl;
	QStackedWidget w_stack;
	QasMainButton b_spectrum;
	QasMainButton b_siggen;
	QasMainButton b_config;
	QTimer watchdog;

	QasSpectrum *w_spectrum;
	QasSigGen *w_siggen;
	QasConfigDlg *w_config;

	/* settings */
	int input_device;
	int output_device;
	int input_left;
	int output_left;
	int input_right;
	int output_right;
	int buffer_samples;

	void closeEvent(QCloseEvent *event);

public slots:
	void handle_spectrum();
	void handle_siggen();
	void handle_config();
};

#endif		/* _QAS_MAINWINDOW_H_ */
