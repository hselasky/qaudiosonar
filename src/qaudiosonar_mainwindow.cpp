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

#include <QPainter>
#include <QKeySequence>
#include <QSettings>

#include "qaudiosonar.h"

#include "qaudiosonar_mainwindow.h"
#include "qaudiosonar_configdlg.h"
#include "qaudiosonar_siggen.h"
#include "qaudiosonar_spectrum.h"

QasMainWindow :: QasMainWindow() : gl(this), b_spectrum(tr("&SPECTRUM")),
    b_siggen(tr("SIGNAL &GENERATOR")), b_config(tr("&AUDIO CONFIG"))
{
	setWindowTitle(QAS_WINDOW_TITLE);
	setWindowIcon(QIcon(QString(QAS_WINDOW_ICON)));

#if defined(Q_OS_MACX)
	b_spectrum.setShortcut(QKeySequence(Qt::ALT + Qt::Key_S));
	b_siggen.setShortcut(QKeySequence(Qt::ALT + Qt::Key_G));
	b_config.setShortcut(QKeySequence(Qt::ALT + Qt::Key_A));
#endif

	connect(&b_spectrum, SIGNAL(released()), this, SLOT(handle_spectrum()));
	connect(&b_siggen, SIGNAL(released()), this, SLOT(handle_siggen()));
	connect(&b_config, SIGNAL(released()), this, SLOT(handle_config()));

	gl.addWidget(&b_spectrum, 0,0);
	gl.addWidget(&b_siggen, 0,1);
	gl.addWidget(&b_config, 0,2);
	gl.addWidget(&w_stack, 1,0,1,4);
	gl.setColumnStretch(3,1);
	gl.setRowStretch(1,1);

	w_spectrum = new QasSpectrum();
	w_siggen = new QasSigGen();
	w_config = new QasConfigDlg();

	w_stack.addWidget(w_spectrum);
	w_stack.addWidget(w_siggen);
	w_stack.addWidget(w_config);
}

void
QasMainWindow :: handle_spectrum()
{
	w_stack.setCurrentWidget(w_spectrum);
	w_spectrum->setFocus();
}

void
QasMainWindow :: handle_siggen()
{
	w_stack.setCurrentWidget(w_siggen);
	w_siggen->setFocus();
}

void
QasMainWindow :: handle_config()
{
	w_stack.setCurrentWidget(w_config);
	w_config->setFocus();
}

void
QasMainWindow :: closeEvent(QCloseEvent *event)
{
	qas_sound_uninit();

	QCoreApplication::exit(0);
}

void
QasMainButton :: handle_timeout()
{
	if (flashing) {
		flashstate = !flashstate;
		update();
	}
}

void
QasMainButton :: handle_released()
{
	flashing = false;
	watchdog.stop();
	update();
}

void
QasMainButton :: paintEvent(QPaintEvent *event)
{
	static const QColor fg(255,255,255,192);

	QPushButton::paintEvent(event);

	if (flashing && flashstate) {
		QPainter paint(this);

		paint.setBrush(QBrush());
		paint.setPen(QPen(QBrush(fg), 2, Qt::SolidLine, Qt::RoundCap));
		paint.drawRect(QRect(0,0,width(),height()));
	}
}
