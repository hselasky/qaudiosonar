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

#ifndef _QAS_SPECTRUM_H_
#define	_QAS_SPECTRUM_H_

#include "qaudiosonar.h"

class QasBand : public QWidget {
	Q_OBJECT
public:
	enum { BAND_MAX = 12 };
	QasBand(QasSpectrum *);
	~QasBand() { };
	QasSpectrum *ps;
	QTimer *watchdog;

	QString getText(QMouseEvent *);
	QString getFullText(int);

	void paintEvent(QPaintEvent *);
	void mousePressEvent(QMouseEvent *);
	void mouseMoveEvent(QMouseEvent *);

public slots:
	void handle_watchdog();
};

class QasGraph : public QWidget {
	Q_OBJECT
public:
	QasGraph(QasSpectrum *);
	~QasGraph();
	QasSpectrum *ps;
	QTimer *watchdog;
	double *mon_index;

	QString getText(QMouseEvent *);

	void paintEvent(QPaintEvent *);
	void mousePressEvent(QMouseEvent *);
	void mouseMoveEvent(QMouseEvent *);

public slots:
	void handle_watchdog();
};

class QasSpectrum : public QWidget {
	Q_OBJECT
public:
	QasSpectrum();

	void closeEvent (QCloseEvent *event) {
		QCoreApplication::exit();
	};

	QGridLayout *gl;
	QGridLayout *glb;
	QScrollBar *sb_zoom;
	QLabel *lbl_max;
	QWidget *qbw;
	QasBand *qb;
	QasGraph *qg;
	QPlainTextEdit *edit;
	QSpinBox *tuning;
	QSlider *sensitivity;
	QasButtonMap *map_decay_0;

signals:
	void handle_append_text(const QString);

public slots:
	void handle_reset();
	void handle_tog_freeze();
	void handle_tog_record();
	void handle_slider(int);
	void handle_tuning();
	void handle_sensitivity();
	void handle_decay_0(int);
};

#endif		/* _QAS_SPECTRUM_H_ */
