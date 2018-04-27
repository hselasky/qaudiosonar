/*-
 * Copyright (c) 2016-2018 Hans Petter Selasky. All rights reserved.
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

#include "qaudiosonar_buttonmap.h"

static pthread_mutex_t atomic_mtx;
static pthread_mutex_t atomic_graph;
static pthread_cond_t atomic_cv;

int qas_num_workers = 2;
size_t qas_window_size;
size_t qas_in_sequence_number;
size_t qas_out_sequence_number;
QasMainWindow *qas_mw;

static void
atomic_init(void)
{
	pthread_mutex_init(&atomic_mtx, NULL);
	pthread_mutex_init(&atomic_graph, NULL);
	pthread_cond_init(&atomic_cv, NULL);
}

void
atomic_lock(void)
{
	pthread_mutex_lock(&atomic_mtx);
}

void
atomic_unlock(void)
{
	pthread_mutex_unlock(&atomic_mtx);
}

void
atomic_graph_lock(void)
{
	pthread_mutex_lock(&atomic_graph);
}

void
atomic_graph_unlock(void)
{
	pthread_mutex_unlock(&atomic_graph);
}

void
atomic_wait(void)
{
	pthread_cond_wait(&atomic_cv, &atomic_mtx);
}

void
atomic_wakeup(void)
{
	pthread_cond_broadcast(&atomic_cv);
}

QasBandPassBox :: QasBandPassBox()
{
	setTitle("Band pass center frequency: 1 Hz");

	grid = new QGridLayout(this);

	pSB = new QScrollBar(Qt::Horizontal);

	pSB->setRange(1, qas_sample_rate / 2);
	pSB->setSingleStep(1);
	pSB->setValue(1);
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

QasMidilevelBox :: QasMidilevelBox()
{
	setTitle("MIDI level: 2**62");

	grid = new QGridLayout(this);

	pSB = new QScrollBar(Qt::Horizontal);

	pSB->setRange(0, 62);
	pSB->setSingleStep(1);
	pSB->setValue(62);
	connect(pSB, SIGNAL(valueChanged(int)), this, SLOT(handle_value_changed(int)));

	grid->addWidget(pSB, 0,0,1,1);
}

void
QasMidilevelBox :: handle_value_changed(int value)
{
	setTitle(QString("MIDI level: 2**%1").arg(value));
	valueChanged(value);
}

QasConfig :: QasConfig(QasMainWindow *_mw)
{
	mw = _mw;

	gl = new QGridLayout(this);

	map_source_0 = new QasButtonMap("Main input channel\0"
					"INPUT-0\0" "INPUT-1\0"
					"OUTPUT-0\0" "OUTPUT-1\0", 4, 4);
	map_source_1 = new QasButtonMap("Correlation input channel\0"
					"INPUT-0\0" "INPUT-1\0"
					"OUTPUT-0\0" "OUTPUT-1\0", 4, 4);
	map_output_0 = new QasButtonMap("Output for channel 0\0"
					"SILENCE\0" "BROWN NOISE\0" "WHITE NOISE\0"
					"BROWN NOISE BP\0" "WHITE NOISE BP\0", 5, 3);

	map_output_1 = new QasButtonMap("Output for channel 1\0"
					"SILENCE\0" "BROWN NOISE\0" "WHITE NOISE\0"
					"BROWN NOISE BP\0" "WHITE NOISE BP\0", 5, 3);

	bp_box_0 = new QasBandPassBox();
	bw_box_0 = new QasBandWidthBox();
	ml_box_0 = new QasMidilevelBox();
	handle_filter_0(0);

	connect(map_source_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_source_0(int)));
	connect(map_source_1, SIGNAL(selectionChanged(int)), this, SLOT(handle_source_1(int)));
	connect(map_output_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_output_0(int)));
	connect(map_output_1, SIGNAL(selectionChanged(int)), this, SLOT(handle_output_1(int)));
	connect(bp_box_0, SIGNAL(valueChanged(int)), this, SLOT(handle_filter_0(int)));
	connect(bw_box_0, SIGNAL(valueChanged(int)), this, SLOT(handle_filter_0(int)));
	connect(ml_box_0, SIGNAL(valueChanged(int)), this, SLOT(handle_filter_0(int)));	

	gl->addWidget(map_source_0, 0,0,1,1);
	gl->addWidget(map_source_1, 1,0,1,1);
	gl->addWidget(map_output_0, 2,0,1,1);
	gl->addWidget(map_output_1, 3,0,1,1);
	gl->addWidget(bp_box_0, 4,0,1,1);
	gl->addWidget(bw_box_0, 5,0,1,1);
	gl->addWidget(ml_box_0, 6,0,1,1);

	setWindowTitle(tr("Quick Audio Sonar v1.5"));
	setWindowIcon(QIcon(":/qaudiosonar.png"));
}

void
QasConfig :: handle_source_0(int _value)
{
	atomic_lock();
	qas_source_0 = _value;
	atomic_unlock();

	mw->handle_reset();
}

void
QasConfig :: handle_source_1(int _value)
{
	atomic_lock();
	qas_source_1 = _value;
	atomic_unlock();

	mw->handle_reset();
}

void
QasConfig :: handle_output_0(int _value)
{
	atomic_lock();
	qas_output_0 = _value;
	atomic_unlock();

	mw->handle_reset();
}

QasView :: QasView(QasMainWindow *_mw)
{
	mw = _mw;

	gl = new QGridLayout(this);

	map_decay_0 = new QasButtonMap("Decay selection\0"
				       "OFF\0" "1/8\0" "1/16\0" "1/32\0"
				       "1/64\0" "1/128\0" "1/256\0", 7, 4);

	connect(map_decay_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_decay_0(int)));

	gl->addWidget(map_decay_0, 0,0,1,1);

	setWindowTitle(tr("Quick Audio Sonar v1.5"));
	setWindowIcon(QIcon(":/qaudiosonar.png"));
}

void
QasView :: handle_decay_0(int _value)
{
	atomic_graph_lock();
	if (_value == 0)
		qas_view_decay = 0.0;
	else
		qas_view_decay = 1.0 - 1.0 / pow(2.0, _value + 2);
	atomic_graph_unlock();
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
QasConfig :: handle_filter_0(int value)
{
	double temp[QAS_CORR_SIZE];
	double adjust = bw_box_0->pSB->value() / 2.0;
	double center = bp_box_0->pSB->value();
	int level = ml_box_0->pSB->value();

	memset(temp, 0, sizeof(temp));
	qas_band_pass(center - adjust, center + adjust, temp, QAS_CORR_SIZE);

	atomic_lock();
	for (size_t x = 0; x != QAS_CORR_SIZE; x++)
		qas_band_pass_filter[x] = temp[x];
	qas_midi_level = 1LL << level;
	atomic_unlock();
}

void
QasConfig :: handle_output_1(int _value)
{
	atomic_lock();
	qas_output_1 = _value;
	atomic_unlock();

	mw->handle_reset();
}

QasBand :: QasBand(QasMainWindow *_mw)
{
	mw = _mw;
	watchdog = new QTimer(this);
	connect(watchdog, SIGNAL(timeout()), this, SLOT(handle_watchdog()));

	setMinimumSize(192, 1);
	setMouseTracking(1);
	watchdog->start(500);
}

QasGraph :: QasGraph(QasMainWindow *_mw)
{
	mw = _mw;
	watchdog = new QTimer(this);
	connect(watchdog, SIGNAL(timeout()), this, SLOT(handle_watchdog()));

	setMinimumSize(256, 256);
	setMouseTracking(1);
	watchdog->start(500);
}

void
QasBand :: mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		mw->edit->appendPlainText("");
	} else {
		double *band = qas_display_get_band((event->y() * qas_display_height()) / height());
		size_t x_off = ((BAND_MAX * event->x()) / width()) % BAND_MAX;
		size_t real_band = band[3 * x_off + 2];

		QString str(qas_descr_table[real_band]);
		str += QString(" /* %1Hz */").arg(QAS_FREQ_TABLE_ROUNDED(real_band));
		mw->edit->appendPlainText(str);
	}
}

void
QasBand :: mouseMoveEvent(QMouseEvent *event)
{
	QString str;
	double *band = qas_display_get_band((event->y() * qas_display_height()) / height());
	size_t x_off = ((BAND_MAX * event->x()) / width()) % BAND_MAX;
	size_t real_band = band[3 * x_off + 2];

	str = qas_descr_table[real_band];
	str += QString(" - %1Hz").arg(QAS_FREQ_TABLE_ROUNDED(real_band));

	if (toolTip() != str)
		setToolTip(str);
}

void
QasBand :: paintEvent(QPaintEvent *event)
{
	int w = width();
	int h = height();
	size_t hd = qas_display_height();
	size_t hi = hd / 2 + 1;
	size_t real_band = 0;

	if (w == 0 || h == 0)
		return;

	QPainter paint(this);
	QColor white(255,255,255);

	QImage accu(BAND_MAX, hi, QImage::Format_ARGB32);

	accu.fill(white);

	atomic_lock();
	size_t seq = qas_out_sequence_number + hd - hi;
	atomic_unlock();

	atomic_graph_lock();
	for (size_t y = 0; y != hi; y++) {
		double *band = qas_display_get_band(y + seq);
		double simp[12] = {};
		double max;
		size_t x, z;

		for (z = x = 0; x != BAND_MAX; x++) {
			if (band[3 * x] > simp[x / QAS_WAVE_STEP_LOG2])
				simp[x / QAS_WAVE_STEP_LOG2] = band[3 * x];
			if (band[3 * x] > band[3 * z])
				z = x;
		}

		max = band[3 * z];
		if (max < 1.0)
			max = 1.0;
		real_band = band[3 * z + 2];

		for (size_t x = 0; x != BAND_MAX; x++) {
			double value = band[3 * x];
			if (value != max)
				value /= 2.0;
			int level = pow(value / max, 3) * 255.0;
			if (level > 255)
				level = 255;
			else if (level < 0)
				level = 0;

			int other = level + (simp[x / QAS_WAVE_STEP_LOG2] / max) * 255.0;
			if (other > 255)
				other = 255;
			else if (other < 0)
				other = 0;

			QColor hc(255 - other, 255 - level, 255 - other, 255);
			size_t yo = hi - 1 - y;
			accu.setPixelColor(x, yo, hc);
		}
	}
	atomic_graph_unlock();

	paint.drawImage(QRect(0,0,w,h),accu);

	QString str(qas_descr_table[real_band]);
	str += QString(" - %1Hz\nR=%2")
	  .arg(QAS_FREQ_TABLE_ROUNDED(real_band))
	  .arg((int)(1000.0 * ((double)(real_band % QAS_WAVE_STEP)) /
		     (double)QAS_WAVE_STEP) / 1000.0);
	mw->lbl_max->setText(str);
}

void
QasGraph :: mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::RightButton) {
		mw->edit->appendPlainText("");
	} else {
		int band = (qas_num_bands * event->x()) / width();
		if (band > -1 && (size_t)band < qas_num_bands) {
			QString str(qas_descr_table[band]);
			str += QString(" /* %1Hz */").arg(QAS_FREQ_TABLE_ROUNDED(band));
			mw->edit->appendPlainText(str);
		}
	}
}

void
QasGraph :: mouseMoveEvent(QMouseEvent *event)
{
	QString str;
	int band = (qas_num_bands * event->x()) / width();

	if (band > -1 && (size_t)band < qas_num_bands) {
		str = qas_descr_table[band];
		str += QString(" - %1Hz").arg(QAS_FREQ_TABLE_ROUNDED(band));
	}
	if (toolTip() != str)
		setToolTip(str);
}

void
QasGraph :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	int w = width();
	int h = height();
	size_t wi = qas_display_width() / 3;
	size_t hd = qas_display_height();
	size_t hi = hd / 2 + 1;
	size_t hg = h / 6 + 1;
	size_t rg = h / 4 + 1;
	size_t wc = qas_window_size;
	double corr_max_power;
	size_t corr_max_off;
	size_t zoom = mw->sb_zoom->value();
	QString corr_sign;

	if (w == 0 || h == 0)
		return;

	atomic_lock();
	size_t seq = qas_out_sequence_number + hd - hi;
	atomic_unlock();

	QImage hist(wi, hi, QImage::Format_ARGB32);
	QImage power(wi, hg, QImage::Format_ARGB32);
	QImage iso(wi, hg, QImage::Format_ARGB32);
	QImage phase(wi, hg, QImage::Format_ARGB32);
	QImage corr(w, hg, QImage::Format_ARGB32);

	QColor transparent = QColor(0,0,0,0);
	QColor power_c = QColor(0,0,0,255);
	QColor phase_c = QColor(192,192,0,255);
	QColor iso_c = QColor(192,192,192,255);
	QColor corr_c = QColor(255,0,0,255);
	QColor white(255,255,255);
	QColor black(0,0,0);

	iso.fill(transparent);
	hist.fill(white);
	power.fill(transparent);
	phase.fill(transparent);
	corr.fill(transparent);
	
	double mon_index[wc];
	double mon_sum = 0.0;
	for (size_t x = 0; x != wc; x++) {
		size_t delta = (x > zoom) ? x - zoom : zoom - x;
		mon_index[x] = mon_sum;
		mon_sum += wc * wc - delta * delta;
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
		size_t x, z, t;

		for (z = x = 0; x != wi; x++) {
			if (data[3 * x] > data[3 * z])
				z = x;
		}
		max = data[3 * z];
		if (max < 1.0)
			max = 1.0;

		if (y == hi - 1) {
			double iso_amp[QAS_STANDARD_AUDIO_BANDS];

			for (x = 0; x != QAS_STANDARD_AUDIO_BANDS; x++)
				iso_amp[x] = 1.0;

			for (x = 0; x != wi; x++) {
				t = qas_iso_table[x];
				if (iso_amp[t] < data[3 * x])
					iso_amp[t] = data[3 * x];
			}

			for (z = x = 0; x != wi; x++) {
				if (data[3 * x] < 1.0)
					continue;
				double power_new = data[3 * x];
				double phase_new = data[3 * x + 1];

				for (t = z; t != (x + 1); t++) {
					int power_y = (double)(power_new / max) * (hg - 1);
					if (power_y < 0)
						power_y = 0;
					else if (power_y > (int)(hg - 1))
						power_y = (int)(hg - 1);
					for (int n = 0; n != power_y; n++)
						power.setPixelColor(t, hg - 1 - n, power_c);

					int phase_y = (double)(phase_new / (2.0 * M_PI)) * (hg - 1);
					if (phase_y < 0)
						phase_y = 0;
					else if (phase_y > (int)(hg - 1))
						phase_y = (int)(hg - 1);
					for (int n = 0; n != phase_y; n++)
						phase.setPixelColor(t, hg - 1 - n, phase_c);

					int iso_y = (double)(iso_amp[qas_iso_table[z]] / max) * (hg - 1);
					if (iso_y < 0)
						iso_y = 0;
					else if (iso_y > (int)(hg - 1))
						iso_y = (int)(hg - 1);
					for (int n = 0; n != iso_y; n++)
						iso.setPixelColor(t, hg - 1 - n, iso_c);
				}
				z = x;
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

	paint.drawImage(QRect(0,2*rg,w,2*rg), hist);
	paint.drawImage(QRect(0,2*rg,w,rg), corr);
	paint.drawImage(QRect(0,0,w,rg), iso);
	paint.drawImage(QRect(0,0,w,rg), power);
	paint.drawImage(QRect(0,rg,w,rg), phase);

	/* fill any gaps */
	paint.setPen(QPen(black,0));
	paint.setBrush(black);
	paint.drawRect(QRectF(0,rg-2,w,4));
	
	QFont fnt(paint.font());

	fnt.setPixelSize(16);
	paint.setFont(fnt);

	QString str;
	str = QString("MAX=%1dB@%2samples;%3ms;%4m    LAG=%5F")
	    .arg(0.5 * 10.0 * log(corr_max_power) / log(10)).arg(corr_max_off)
	   .arg((double)((int)((corr_max_off * 100000ULL) / qas_sample_rate) / 100.0))
	   .arg((double)((int)((corr_max_off * 34000ULL) / qas_sample_rate) / 100.0))
	   .arg(qas_display_lag());

	paint.setPen(QPen(black,0));
	paint.setBrush(black);
	paint.drawText(QPoint(0,16),str);

	str = "POWER";
	paint.setPen(QPen(power_c,0));
	paint.setBrush(power_c);
	paint.drawText(QPoint(0,32),str);

	str = "PHASE";
	paint.setPen(QPen(phase_c,0));
	paint.setBrush(phase_c);
	paint.drawText(QPoint(6*16,32),str);

	str = "ISO";
	paint.setPen(QPen(iso_c,0));
	paint.setBrush(iso_c);
	paint.drawText(QPoint(12*16,32),str);

	str = "CORRELATION ";
	str += corr_sign;
	paint.setPen(QPen(corr_c,0));
	paint.setBrush(corr_c);
	paint.drawText(QPoint(16*16,32),str);
	
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

QasMainWindow :: QasMainWindow()
{
	QPushButton *pb;

	qc = new QasConfig(this);
	qv = new QasView(this);

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

	gl->addWidget(new QLabel(tr("DSP RX:")), 0,0,1,1);

	led_dsp_read = new QLineEdit("/dev/dsp");
	gl->addWidget(led_dsp_read, 0,1,1,1);

	gl->addWidget(new QLabel(tr("DSP TX:")), 1,0,1,1);

	led_dsp_write = new QLineEdit("/dev/dsp");
	gl->addWidget(led_dsp_write, 1,1,1,1);

	gl->addWidget(new QLabel(tr("MIDI TX:")), 2,0,1,1);

	led_midi_write = new QLineEdit("/dev/midi");
	gl->addWidget(led_midi_write, 2,1,1,1);
	
	pb = new QPushButton(tr("Apply"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_apply()));
	gl->addWidget(pb, 0,2,1,1);

	pb = new QPushButton(tr("Reset"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_reset()));
	gl->addWidget(pb, 1,2,1,1);

	pb = new QPushButton(tr("AudioConfig"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_config()));
	gl->addWidget(pb, 0,7,1,1);
       
	pb = new QPushButton(tr("ViewConfig"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_view()));
	gl->addWidget(pb, 1,7,1,1);

	pb = new QPushButton(tr("Toggle\nFreeze"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_freeze()));
	gl->addWidget(pb, 0,6,2,1);

	pb = new QPushButton(tr("RecordTog"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_record()));
	gl->addWidget(pb, 2,2,1,1);
	
	edit = new QPlainTextEdit();
	
	qbw = new QWidget();
	glb = new QGridLayout(qbw);

	gl->addWidget(qbw, 0,8,6,2);
	gl->addWidget(sb_zoom, 3,0,1,8);
	gl->addWidget(qg, 4,0,2,8);
	gl->setRowStretch(2,1);

	glb->addWidget(lbl_max, 1,0,1,1);
	glb->addWidget(qb, 2,0,1,1);
	glb->addWidget(edit, 3,0,1,1);
	glb->setRowStretch(2,1);

	connect(this, SIGNAL(handle_append_text(const QString)), edit, SLOT(appendPlainText(const QString &)));

	setWindowTitle(tr("Quick Audio Sonar v1.5"));
	setWindowIcon(QIcon(":/qaudiosonar.png"));
}

void
QasMainWindow :: handle_apply()
{
	QString dsp_rd = led_dsp_read->text().trimmed();
	QString dsp_wr = led_dsp_write->text().trimmed();
	QString midi_wr = led_midi_write->text().trimmed();
	int x;

	atomic_lock();
	for (x = 0; x != dsp_rd.length() &&
	       x != sizeof(dsp_read_device) - 1; x++) {
		dsp_read_device[x] = dsp_rd[x].toLatin1();
	}
	dsp_read_device[x] = 0;

	for (x = 0; x != dsp_wr.length() &&
	       x != sizeof(dsp_write_device) - 1; x++) {
		dsp_write_device[x] = dsp_wr[x].toLatin1();
	}
	dsp_write_device[x] = 0;

	for (x = 0; x != midi_wr.length() &&
	       x != sizeof(midi_write_device) - 1; x++) {
		midi_write_device[x] = midi_wr[x].toLatin1();
	}
	midi_write_device[x] = 0;
	atomic_wakeup();
	atomic_unlock();
}

void
QasMainWindow :: handle_reset()
{
	qas_dsp_sync();
}

void
QasMainWindow :: handle_config()
{
	qc->show();
}

void
QasMainWindow :: handle_view()
{
	qv->show();
}

void
QasMainWindow :: handle_slider(int value)
{
}

void
QasMainWindow :: handle_tog_freeze()
{
	atomic_lock();
	qas_freeze = !qas_freeze;
	atomic_unlock();
}

void
QasMainWindow :: handle_tog_record()
{
	atomic_lock();
	qas_record = !qas_record;
	atomic_unlock();
}

static void
usage(void)
{
	fprintf(stderr, "Usage: qaudiosonar [-r <samplerate>] "
	    "[-n <workers>] [-w <windowsize>]\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	QApplication app(argc, argv);
	int c;

        /* must be first, before any threads are created */
        signal(SIGPIPE, SIG_IGN);
	
	while ((c = getopt(argc, argv, "n:r:hw:")) != -1) {
		switch (c) {
		case 'n':
			qas_num_workers = atoi(optarg);
			if (qas_num_workers < 1)
				qas_num_workers = 1;
			else if (qas_num_workers > 16)
				qas_num_workers = 16;
			break;
		case 'r':
			qas_sample_rate = atoi(optarg);
			if (qas_sample_rate < 8000)
				qas_sample_rate = 8000;
			else if (qas_sample_rate > 96000)
				qas_sample_rate = 96000;
			break;
		case 'w':
			qas_window_size = atoi(optarg);
			break;
		default:
			usage();
			break;
		}
	}

	atomic_init();

	if (qas_window_size == 0)
		qas_window_size = qas_sample_rate;
	else if (qas_window_size >= (size_t)(16 * qas_sample_rate))
		qas_window_size = (size_t)(16 * qas_sample_rate);
	qas_window_size = qas_sample_rate - (qas_sample_rate % QAS_CORR_SIZE);
	if (qas_window_size == 0)
		errx(EX_USAGE, "Invalid window size\n");

	qas_wave_init();
	qas_corr_init();
	qas_display_init();
	qas_midi_init();
	qas_dsp_init();

	qas_mw = new QasMainWindow();
	qas_mw->show();

	return (app.exec());
}
