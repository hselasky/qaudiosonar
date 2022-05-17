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

#include "qaudiosonar_buttonmap.h"
#include "qaudiosonar_spectrum.h"

static const char *qas_key_map[12] = {
  "C%1", "D%1B", "D%1", "E%1B", "E%1",
  "F%1", "G%1B", "G%1", "A%1B",
  "A%1", "B%1B", "B%1",
};

void
QasSpectrum :: handle_decay_0(int _value)
{
	atomic_graph_lock();
	if (_value == 0)
		qas_view_decay = 0.0;
	else
		qas_view_decay = 1.0 - 1.0 / pow(2.0, _value + 2);
	atomic_graph_unlock();
}

QasBand :: QasBand(QasSpectrum *_ps)
{
	ps = _ps;
	watchdog = new QTimer(this);
	connect(watchdog, SIGNAL(timeout()), this, SLOT(handle_watchdog()));

	setMinimumSize(192, 1);
	setMouseTracking(1);
	watchdog->start(500);
}

QasGraph :: QasGraph(QasSpectrum *_ps)
{
	ps = _ps;
	watchdog = new QTimer(this);
	connect(watchdog, SIGNAL(timeout()), this, SLOT(handle_watchdog()));
	mon_index = new double [qas_window_size];
	memset(mon_index, 0, sizeof(double) * qas_window_size);

	setMinimumSize(256, 256);
	setMouseTracking(1);
	watchdog->start(500);
}

QasGraph :: ~QasGraph()
{
	delete mon_index;
}

QString
QasBand :: getText(QMouseEvent *event)
{
	int key = (9 + (12 * event->x()) / width()) % 12;

	return (QString(qas_key_map[key]).arg(5));
}

static size_t
QasGetSequenceNumber(size_t *phi)
{
	size_t hd = qas_display_height();
	size_t hi = hd / 2 + 1;

	*phi = hi;

	atomic_lock();
	size_t seq = qas_out_sequence_number + hd - hi;
	atomic_unlock();

	return (seq);
}

QString
QasBand :: getFullText(int ypos)
{
	size_t wi = qas_display_band_width() / 3;
	size_t hi;
	size_t seq = QasGetSequenceNumber(&hi);
	int ho = (hi * ypos) / height();
	QString str;
	ssize_t real_offset;
	ssize_t real_band;
	size_t y;
	double level = 1U << qas_sensitivity;

	if (ho < 0)
		ho = 0;
	else if (ho >= (int)hi)
		ho = hi - 1;

	double *band = qas_display_get_band(hi - 1 - ho + seq);

	for (size_t x = y = 0; x != wi; x++) {
		if (band[3 * x] > band[3 * y])
			y = x;

		if (band[3 * x] >= level) {
			size_t offset = band[3 * x + 2];
			size_t key = 9 + (offset + (QAS_WAVE_STEP / 2)) / QAS_WAVE_STEP;

			str += QString(qas_key_map[key % 12]).arg(key / 12);
			str += " ";
		}
	}
	if (qas_record != 0) {
		for (size_t x = 0; x != wi; x++) {
			if (band[3 * x] == 0.0)
				continue;
			if (band[3 * x] >= level) {
				size_t offset = band[3 * x + 2];
				size_t key = 9 + (offset + (QAS_WAVE_STEP / 2)) / QAS_WAVE_STEP;
				qas_midi_key_send(0, key, 90, 0);
			}
		}
		qas_midi_delay_send(50);

		for (size_t x = 0; x != wi; x++) {
			if (band[3 * x] == 0.0)
				continue;
			if (band[3 * x] >= level) {
				size_t offset = band[3 * x + 2];
				size_t key = 9 + (offset + (QAS_WAVE_STEP / 2)) / QAS_WAVE_STEP;
				qas_midi_key_send(0, key, 0, 0);
			}
		}
		qas_midi_delay_send(50);
	}

	/* get band */
	real_band = band[3 * y + 2];

	/* compute offset */
	real_offset = (real_band + (QAS_WAVE_STEP / 2));
	real_offset -= real_offset % QAS_WAVE_STEP;
	real_offset = (real_band - real_offset);
	real_offset %= QAS_WAVE_STEP;

	size_t key = 9 + (real_band + (QAS_WAVE_STEP / 2)) / QAS_WAVE_STEP;

	str += "/* ";
	str += QString(qas_key_map[key % 12]).arg(key / 12);
	str += QString(" %2Hz R=%3 */")
	    .arg(QAS_FREQ_TABLE_ROUNDED(real_band))
	    .arg((double)real_offset / (double)QAS_WAVE_STEP, 0, 'f', 3);

	return (str);
}

void
QasBand :: mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		ps->edit->appendPlainText("");
	} else if (event->button() == Qt::MiddleButton) {
		ps->edit->appendPlainText(getText(event));
	} else if (event->button() == Qt::LeftButton) {
		ps->edit->appendPlainText(getFullText(event->y()));
	}
	event->accept();
}

void
QasBand :: mouseMoveEvent(QMouseEvent *event)
{
	setToolTip(getText(event));
}

void
QasBand :: paintEvent(QPaintEvent *event)
{
	enum { LIMIT = 8 };

	int w = width();
	int h = height();
	size_t hi;
	ssize_t real_band = 0;
	ssize_t real_offset;
	double level = 1U << qas_sensitivity;

	if (w == 0 || h == 0)
		return;

	size_t seq = QasGetSequenceNumber(&hi);

	QPainter paint(this);
	QColor white(255,255,255);

	QImage accu(BAND_MAX, hi, QImage::Format_ARGB32);

	accu.fill(white);

	atomic_graph_lock();

	for (size_t y = 0; y != hi; y++) {
		double *band = qas_display_get_band(y + seq);
		double max;
		size_t x, z;

		for (z = x = 0; x != BAND_MAX; x++) {
			if (band[3 * x] > band[3 * z])
				z = x;
		}

		real_band = band[3 * z + 2];
		max = band[3 * z];

		if (max < 1.0)
			continue;

		for (size_t x = 0; x != BAND_MAX; x++) {
			double value = band[3 * x];

			if (value < level)
				continue;

			int bright = (value / max) * 255.0;
			if (bright > 255)
				bright = 255;
			else if (bright < 0)
				bright = 0;

			QColor hc(255 - bright, 255 - bright, 255 - bright, 255);
			size_t yo = hi - 1 - y;
			accu.setPixelColor(x, yo, hc);
		}
	}
	atomic_graph_unlock();

	if (qas_record != 0 && qas_freeze == 0) {
		QString str = getFullText(0);
		ps->handle_append_text(str);
	}

	paint.drawImage(QRect(0,0,w,h),accu);

	/* compute offset */
	real_offset = (real_band + (QAS_WAVE_STEP / 2));
	real_offset -= real_offset % QAS_WAVE_STEP;
	real_offset = (real_band - real_offset);
	real_offset %= QAS_WAVE_STEP;

	size_t key = 9 + (real_band + (QAS_WAVE_STEP / 2)) / QAS_WAVE_STEP;

	QString str = QString(qas_key_map[key % 12]).arg(key / 12) +
	  QString(" - %1Hz\nR=%2")
	  .arg(QAS_FREQ_TABLE_ROUNDED(real_band))
	  .arg((double)real_offset / (double)QAS_WAVE_STEP, 0, 'f', 3);
	ps->lbl_max->setText(str);
}

QString
QasGraph :: getText(QMouseEvent *event)
{
	int graph = (3 * event->y()) / height();
	int band;
	int key;

	switch (graph) {
	case 0:
	case 1:
		band = (qas_num_bands * event->x()) / width();
		if (band > -1 && band < (int)qas_num_bands) {
			band -= band % QAS_WAVE_STEP;
			return QString(qas_descr_table[band]) +
			  QString(" /* %1Hz */").arg(QAS_FREQ_TABLE_ROUNDED(band));
		} else {
			return QString();
		}
	default:
		for (key = 0; key != (int)qas_window_size; key++) {
			int pos = mon_index[key] /
			    mon_index[qas_window_size - 1] * (double)width();
			if (pos >= event->x())
				break;
		}
		key = qas_window_size - 1 - key;
		return (QString("/* %1ms %2m */")
		    .arg((double)((int)((key * 100000ULL) / qas_sample_rate) / 100.0))
		    .arg((double)((int)((key * 34000ULL) / qas_sample_rate) / 100.0)));
	}
}

void
QasGraph :: mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		ps->edit->appendPlainText("");
	} else if (event->button() == Qt::LeftButton) {
		ps->edit->appendPlainText(getText(event));
	}
	event->accept();
}

void
QasGraph :: mouseMoveEvent(QMouseEvent *event)
{
	setToolTip(getText(event));
}

static double
QasReference(double a, double b, double c)
{
	double max;

	if (a >= b && a >= c)
		max = a;
	else if (b >= a && b >= c)
		max = b;
	else
		max = c;

	return (max);
}

void
QasGraph :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	int w = width();
	int h = height();
	size_t wi = qas_display_width() / 3;
	size_t hi;
	size_t hg = h / 6 + 1;
	size_t rg = h / 3 + 1;
	size_t wc = qas_window_size;
	double corr_max_power;
	size_t corr_max_off;
	size_t zoom = ps->sb_zoom->value();
	QString corr_sign;

	if (w == 0 || h == 0)
		return;

	size_t seq = QasGetSequenceNumber(&hi);

	QImage hist(wi, hi, QImage::Format_ARGB32);
	QImage power(wi, hg, QImage::Format_ARGB32);
	QImage corr(w, hg, QImage::Format_ARGB32);

	QColor transparent = QColor(0,0,0,0);
	QColor corr_c = QColor(255,0,0,255);
	QColor white(255,255,255);
	QColor black(0,0,0);

	hist.fill(white);
	power.fill(transparent);
	corr.fill(transparent);

	double mon_sum = 0.0;
	for (size_t x = 0; x != wc; x++) {
		size_t delta = (x > zoom) ? x - zoom : zoom - x;
		mon_index[x] = mon_sum;
		mon_sum += pow(wc,4) - pow(delta,4);
	}
	atomic_graph_lock();
	do {
		size_t x, t;
		for (t = x = 0; x != wc; x++) {
			if (fabs(qas_mon_decay[x]) > fabs(qas_mon_decay[t]))
				t = x;
		}
		corr_sign = (qas_mon_decay[t] < 0) ? "(-)" : "(+)";
		corr_max_power = fabs(qas_mon_decay[t]);
		if (corr_max_power < 1.0)
			corr_max_power = 1.0;
		corr_max_off = wc - 1 - t;
		for (t = 0; t != wc; t++) {
			x = mon_index[t] / mon_index[wc - 1] * (double)(w - 1);
			if (x > (size_t)(w - 1))
				x = (size_t)(w - 1);
			int value = (1.0 - (qas_mon_decay[t] / corr_max_power)) * (double)(hg / 2);

			if (value < 0)
				value = 0;
			else if (value > (int)(hg - 1))
				value = (int)(hg - 1);

			while (value < (int)(hg / 2)) {
				if (corr.pixelColor(x, value).red() < corr_c.red())
					corr.setPixelColor(x, value, corr_c);
				value++;
			}
			while (value > (int)(hg / 2)) {
				if (corr.pixelColor(x, value).red() < corr_c.red())
					corr.setPixelColor(x, value, corr_c);
				value--;
			}
			if (corr.pixelColor(x, value).red() < corr_c.red())
				corr.setPixelColor(x, value, corr_c);
		}
	} while (0);

	for (size_t y = 0; y != hi; y++) {
		double *data = qas_display_get_line(y + seq);
		double max;
		size_t x, z;

		for (z = x = 0; x != wi; x++) {
			if (data[3 * x] > data[3 * z])
				z = x;
		}

		max = data[3 * z];
		if (max < 2.0) {
			max = 2.0;
			continue;
		}

		if (y == hi - 1) {
			for (x = 0; x != wi; x++) {
				double ref;

				if (x == 0)
					ref = QasReference(data[3 * x], data[3 * (x + 2)], data[3 * (x + 1)]);
				else if (x == (wi - 1))
					ref = QasReference(data[3 * x], data[3 * (x - 2)], data[3 * (x - 1)]);
				else
					ref = QasReference(data[3 * x], data[3 * (x + 1)], data[3 * (x - 1)]);

				QColor power_c = (data[3 * x] >= ref) ?
				  QColor(0,0,0,255) : QColor(127,127,127,255);

				double power_new = data[3 * x];

				int power_y = (double)(power_new / max) * (hg - 1);
				if (power_y < 0)
					power_y = 0;
				else if (power_y > (int)(hg - 1))
					power_y = (int)(hg - 1);
				for (int n = 0; n != power_y; n++)
					power.setPixelColor(x, hg - 1 - n, power_c);
			}
		}
		for (size_t x = 0; x != wi; x++) {
			double value = data[3 * x];
			if (value < 1.0)
				continue;
			int level = pow(value / max, 3.0) * 255.0;
			if (level > 255)
				level = 255;
			else if (level < 0)
				level = 0;
			QColor hc(255 - level, 255 - level, 255 - level, 255);
			if (hist.pixelColor(x, hi - 1 - y).red() > hc.red())
				hist.setPixelColor(x, hi - 1 - y, hc);
		}
	}
	atomic_graph_unlock();

	paint.setPen(QPen(white,0));
	paint.setBrush(white);

	paint.drawRect(QRectF(0,0,w,h));
	paint.setRenderHints(QPainter::Antialiasing|
			     QPainter::TextAntialiasing);

	paint.drawImage(QRect(0,rg,w,rg), hist);
	paint.drawImage(QRect(0,2*rg,w,rg), corr);
	paint.drawImage(QRect(0,0,w,rg), power);

	/* fill any gaps */
	paint.setPen(QPen(black,0));
	paint.setBrush(black);
	paint.drawRect(QRectF(0,rg-2,w,4));

	QFont fnt(paint.font());

	fnt.setPixelSize(16);
	paint.setFont(fnt);

	QString str;
	str = QString("MAX=%1dB@%2samples;%3ms;%4m    LAG=%5F")
	   .arg(10.0 * log(corr_max_power) / log(10)).arg(corr_max_off)
	   .arg((double)((int)((corr_max_off * 100000ULL) / qas_sample_rate) / 100.0))
	   .arg((double)((int)((corr_max_off * 34000ULL) / qas_sample_rate) / 100.0))
	   .arg(qas_display_lag());

	paint.setPen(QPen(black,0));
	paint.setBrush(black);
	paint.drawText(QPoint(0,16),str);

	str = "POWER";
	paint.setPen(QPen(black,0));
	paint.setBrush(black);
	paint.drawText(QPoint(0,32),str);

	str = "CORRELATION ";
	str += corr_sign;
	paint.setPen(QPen(corr_c,0));
	paint.setBrush(corr_c);
	paint.drawText(QPoint(12*16,32),str);

	uint8_t iso_num = 255;
	int last_x = 0;
	int diff_y = 0;
	for (size_t x = 0; x != wi; x++) {
		if (iso_num == qas_iso_table[x])
			continue;
		iso_num = qas_iso_table[x];
		double freq = qas_iso_freq_table[iso_num];
		if (freq >= 1000.0)
			str = QString("%1.%2kHz").arg((int)(freq / 1000.0)).arg((int)(freq / 100.0) % 10);
		else
			str = QString("%1Hz").arg((int)freq);

		paint.setPen(QPen(black,0));
		paint.setBrush(black);
		int next_x = (w * x) / wi;
		if ((next_x - last_x) < (5 * 16))
			diff_y += 20;
		else
			diff_y = 0;
		paint.drawText(QPoint(next_x, h - 20 - diff_y),str);
		if (diff_y == 0)
			last_x = next_x;
	}
}

void
QasBand :: handle_watchdog()
{
	update();
}

void
QasGraph :: handle_watchdog()
{
	update();
}

QasSpectrum :: QasSpectrum()
{
	QPushButton *pb;

	gl = new QGridLayout(this);

	qg = new QasGraph(this);
	qb = new QasBand(this);

	lbl_max = new QLabel();
	lbl_max->setAlignment(Qt::AlignCenter);

	QFont fnt(lbl_max->font());
	fnt.setPixelSize(16);
	lbl_max->setFont(fnt);

	sb_zoom = new QScrollBar(Qt::Horizontal);
	sb_zoom->setRange(0, qas_window_size - 1);
	sb_zoom->setSingleStep(1);
	sb_zoom->setValue(0);

	pb = new QPushButton(tr("Reset"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_reset()));
	gl->addWidget(pb, 1,0,1,1);

	pb = new QPushButton(tr("Toggle freeze"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_freeze()));
	gl->addWidget(pb, 1,1,1,1);

	pb = new QPushButton(tr("Toggle record"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_record()));
	gl->addWidget(pb, 1,2,1,1);

	tuning = new QSpinBox();
	tuning->setRange(-999,999);
	tuning->setValue(0);
	tuning->setPrefix("Fine tuning +/-999: ");
	connect(tuning, SIGNAL(valueChanged(int)), this, SLOT(handle_tuning()));

	sensitivity = new QSlider();
	sensitivity->setRange(0, 31);
	sensitivity->setValue(0);
	sensitivity->setToolTip("Sensitivity");
	sensitivity->setOrientation(Qt::Horizontal);
	connect(sensitivity, SIGNAL(valueChanged(int)), this, SLOT(handle_sensitivity()));

	map_decay_0 = new QasButtonMap("Decay selection\0"
				       "OFF\0" "1/8\0" "1/16\0" "1/32\0"
				       "1/64\0" "1/128\0" "1/256\0", 7, 7);

	connect(map_decay_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_decay_0(int)));

	edit = new QPlainTextEdit();

	qbw = new QWidget();
	glb = new QGridLayout(qbw);

	gl->addWidget(map_decay_0, 0,0,1,4);
	gl->addWidget(qbw, 0,4,4,1);
	gl->addWidget(sb_zoom, 2,0,1,4);
	gl->addWidget(qg, 3,0,2,4);
	gl->setRowStretch(3,2);
	gl->setColumnStretch(3,1);

	glb->addWidget(lbl_max, 1,0,1,1);
	glb->addWidget(tuning, 2,0,1,1);
	glb->addWidget(sensitivity, 3,0,1,1);
	glb->addWidget(qb, 4,0,1,1);
	glb->addWidget(edit, 5,0,1,1);
	glb->setRowStretch(4,1);

	connect(this, SIGNAL(handle_append_text(const QString)), edit, SLOT(appendPlainText(const QString &)));

	setWindowTitle(tr(QAS_WINDOW_TITLE));
	setWindowIcon(QIcon(QAS_WINDOW_ICON));
}

void
QasSpectrum :: handle_reset()
{
	qas_dsp_sync();
}


void
QasSpectrum :: handle_tuning()
{
	qas_tuning = pow(2.0, (double)tuning->value() / 12000.0);
}

void
QasSpectrum :: handle_sensitivity()
{
	atomic_lock();
	qas_sensitivity = sensitivity->value();
	atomic_unlock();

	qb->update();
}

void
QasSpectrum :: handle_slider(int value)
{
}

void
QasSpectrum :: handle_tog_freeze()
{
	atomic_lock();
	qas_freeze = !qas_freeze;
	atomic_unlock();
}

void
QasSpectrum :: handle_tog_record()
{
	atomic_lock();
	qas_record = !qas_record;
	atomic_unlock();
}
