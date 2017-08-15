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

QasRecord :: QasRecord() : QWidget()
{
	TAILQ_INIT(&head);

	do_record = 0;
	select = QPoint(0,0);
	
	gl = new QGridLayout(this);

	pSpin = new QSpinBox();
	pSpin->setRange(10,99);
	connect(pSpin, SIGNAL(valueChanged(int)), this, SLOT(update()));

	pLabel = new QLineEdit();
	pEdit = new QPlainTextEdit();
	pButReset = new QPushButton(tr("Reset REC"));
	connect(pButReset, SIGNAL(released()), this, SLOT(handle_reset()));
	pButToggle = new QPushButton(tr("Toggle REC"));
	connect(pButToggle, SIGNAL(released()), this, SLOT(handle_toggle()));
	pButInsert = new QPushButton(tr("Insert TXT"));
	connect(pButInsert, SIGNAL(released()), this, SLOT(handle_insert()));
	pSB = new QScrollBar(Qt::Vertical);
	pSB->setSingleStep(1);
	pSB->setRange(0,0);
	pSB->setValue(0);
	connect(pSB, SIGNAL(valueChanged(int)), this, SLOT(handle_slider(int)));
	pShow = new QasRecordShow(this);

	gl->addWidget(pLabel, 0,3,1,1);
	gl->addWidget(pSpin, 0,2,1,1);
	gl->addWidget(pButReset, 1,2,1,2);
	gl->addWidget(pButToggle, 2,2,1,2);
	gl->addWidget(pButInsert, 3,2,1,2);
	gl->addWidget(pEdit, 4,2,1,2);
	gl->addWidget(pSB, 0,1,5,1);
	gl->addWidget(pShow, 0,0,5,1);
	gl->setRowStretch(4,1);
	gl->setColumnStretch(0,1);

	setWindowTitle(tr("Quick Audio Sonar v1.1"));
	setWindowIcon(QIcon(":/qaudiosonar.png"));
}

QasRecord :: ~QasRecord()
{
	QasRecordEntry *entry;

	while ((entry = TAILQ_FIRST(&head))) {
		TAILQ_REMOVE(&head, entry, entry);
		delete entry;
	}
}

QasRecordShow :: QasRecordShow(QasRecord *qr) :
    QWidget(), qr(qr)
{
	setMinimumWidth(256);
	setFocusPolicy(Qt::ClickFocus);
}

static QString
extractScore(const QString &str)
{
	int index = str.indexOf("-");
	if (index < 0)
		return ("");
	else
		return (str.mid(index + 2));
}

void
QasRecordShow :: paintEvent(QPaintEvent *event)
{
	QasRecordEntry *entry;
	QasRecordEntry *start;
	size_t num = 0;
	size_t off = qr->pSB->value();
	size_t max = 1;
	float w = width();
	float h = height();
	float dw;
	float y = 0;

	TAILQ_FOREACH(entry, &qr->head, entry) {
		if (entry->num > max)
			max = entry->num;
	}

	dw = w / (float)max;

	TAILQ_FOREACH(entry, &qr->head, entry) {
		if (num == off)
			break;
		num++;
	}

	start = entry;

	QColor white(255,255,255);
	QColor black(0,0,0);

	QPainter paint(this);

	paint.setPen(QPen(white,0));
	paint.setBrush(white);
	paint.drawRect(QRectF(0,0,w,h));

	for (y = 0, entry = start; entry != 0 && y < h; y += dw,
	       entry = TAILQ_NEXT(entry, entry)) {
		int64_t maxpower = 1;

		if (entry->num == 0)
			continue;

		for (unsigned int x = 0; x != entry->num; x++) {
			int64_t power = entry->pvalue[x];
			if (power > maxpower)
				maxpower = power;
		}

		for (unsigned int x = 0; x != entry->num; x++) {
			QRectF rect(dw * x, y, dw, dw);
			int64_t power = entry->pvalue[x];
			if (power < 0)
				power = 0;
			else
				power = (power * 255ULL) / maxpower;
			QColor gradient;
			if (rect.contains(qr->select)) {
				int64_t limit = (10 * maxpower) / qr->pSpin->value();
				QString str;
				for (unsigned z = 0; z != entry->num; z++) {
					if (entry->pvalue[z] >= limit) {
						if (!str.isEmpty())
							str += " ";
						str += extractScore(entry->pdesc[z]);
					}
				}
				qr->pLabel->setText(str);
				gradient = QColor(0, 0, 0);
			} else {
				gradient = QColor(power, 255 - power, 0);
			}
			paint.setPen(QPen(gradient,0));
			paint.setBrush(gradient);
			paint.drawRect(rect);
		}

		QFont fnt = paint.font();
		fnt.setPixelSize(dw);
		paint.setFont(fnt);
		paint.setPen(QPen(black,0));
		paint.setBrush(black);
		paint.drawText(QPoint(0, y), entry->comment);
	}
}

void
QasRecordShow :: mousePressEvent(QMouseEvent *event)
{
	qr->select = QPoint(event->x(),event->y());
	update();
}

void
QasRecordShow :: keyPressEvent(QKeyEvent *event)
{
	QScrollBar *ps = qr->pSB;
	int value;

	value = ps->value();

	switch (event->key()) {
	case Qt::Key_PageDown:
	case Qt::Key_Down:
		value ++;
		break;
	case Qt::Key_Up:
	case Qt::Key_PageUp:
		value --;
		break;
	case Qt::Key_End:
		value = ps->maximum();
		break;
	case Qt::Key_Home:
		value = 0;
		break;
	default:
		goto done;
	}

	if (value >= ps->minimum() && value <= ps->maximum())
		ps->setValue(value);
done:
	event->accept();
}

void
QasRecord :: handle_reset()
{
	QasRecordEntry *entry;

	while ((entry = TAILQ_FIRST(&head))) {
		TAILQ_REMOVE(&head, entry, entry);
		delete entry;
	}

	pSB->setRange(0,0);
	pSB->setValue(0);

	update();
}

void
QasRecord :: handle_toggle()
{
	do_record = !do_record;
}

void
QasRecord :: handle_insert()
{
	QasRecordEntry *entry;

	entry = TAILQ_FIRST(&head);
	if (entry == 0)
		return;

	QTextCursor cursor(pEdit->textCursor());

        cursor.beginEditBlock();
	for (unsigned int x = 0; x != 16; x++) {
		cursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor, 1);
		cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor, 1);
		cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
		entry->comment = cursor.selectedText().trimmed();
		cursor.removeSelectedText();
		if (!entry->comment.isEmpty())
			break;
	}
	cursor.endEditBlock();
}

void
QasRecord :: handle_slider(int)
{
	pShow->update();
}

void
QasRecord :: insert_entry(QasRecordEntry *entry)
{
	if (do_record == 0) {
		delete entry;
		return;
	}
	TAILQ_INSERT_HEAD(&head, entry, entry);
	pSB->setMaximum(pSB->maximum() + 1);
	update();
}
