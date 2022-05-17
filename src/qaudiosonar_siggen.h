/*-
 * Copyright (c) 2022 Hans Petter Selasky. All rights reserved.
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

#ifndef _QAS_SIGGEN_H_
#define	_QAS_SIGGEN_H_

#include "qaudiosonar.h"

class QasBandPassBox : public QGroupBox {
	Q_OBJECT;
public:
	QasBandPassBox();
	~QasBandPassBox() {};

	QScrollBar *pSB;
	QGridLayout *grid;

signals:
	void valueChanged(int);

public slots:
	void handle_value_changed(int);
};

class QasBandWidthBox : public QGroupBox {
	Q_OBJECT;
public:
	QasBandWidthBox();
	~QasBandWidthBox() {};

	QScrollBar *pSB;
	QGridLayout *grid;

signals:
	void valueChanged(int);

public slots:
	void handle_value_changed(int);
};

class QasNoiselevelBox : public QGroupBox {
	Q_OBJECT;
public:
	QasNoiselevelBox();
	~QasNoiselevelBox() {};

	QScrollBar *pSB;
	QGridLayout *grid;

signals:
	void valueChanged(int);

public slots:
	void handle_value_changed(int);
};

class QasMainWindow;
class QasButtonMap;

class QasSigGen : public QWidget {
	Q_OBJECT
public:
	QasSigGen();

	QGridLayout *gl;

	QasButtonMap *map_source_0;
	QasButtonMap *map_source_1;
	QasButtonMap *map_output_0;
	QasButtonMap *map_output_1;
	QasBandPassBox *bp_box_0;
	QasBandWidthBox *bw_box_0;
	QasNoiselevelBox *nl_box_0;

public slots:
	void handle_source_0(int);
	void handle_source_1(int);
	void handle_output_0(int);
	void handle_output_1(int);
	void handle_filter_0(int);
};

#endif		/* _QAS_SIGGEN_H_ */
