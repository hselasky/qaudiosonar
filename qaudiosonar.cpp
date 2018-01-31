/*-
 * Copyright (c) 2016-2017 Hans Petter Selasky. All rights reserved.
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
static pthread_mutex_t atomic_filter;
static pthread_cond_t atomic_cv;

int qas_number_bands = QAS_BAND_DEFAULT;

#define	QAS_STANDARD_AUDIO_BANDS 31

static const double qas_freq_table[QAS_STANDARD_AUDIO_BANDS] = {
	20, 25, 31.5, 40, 50, 63, 80, 100, 125,
	160, 200, 250, 315, 400, 500, 630, 800, 1000,
	1250, 1600, 2000, 2500, 3150, 4000, 5000,
	6300, 8000, 10000, 12500, 16000, 20000
};

static void
atomic_init(void)
{
	pthread_mutex_init(&atomic_mtx, NULL);
	pthread_mutex_init(&atomic_graph, NULL);
	pthread_mutex_init(&atomic_filter, NULL);
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
atomic_filter_lock(void)
{
	pthread_mutex_lock(&atomic_filter);
}

void
atomic_filter_unlock(void)
{
	pthread_mutex_unlock(&atomic_filter);
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

	setWindowTitle(tr("Quick Audio Sonar v1.1"));
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

	map_view_0 = new QasButtonMap("Main view\0"
				      "AMP-LIN\0" "AMP-LOG\0"
				      "PHASE-LIN\0", 3, 3);

	map_decay_0 = new QasButtonMap("Decay selection\0"
				       "1/8\0" "1/16\0" "1/32\0"
				       "1/64\0" "1/128\0" "1/256\0", 6, 6);

	connect(map_view_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_view_0(int)));
	connect(map_decay_0, SIGNAL(selectionChanged(int)), this, SLOT(handle_decay_0(int)));

	gl->addWidget(map_view_0, 0,0,1,1);
	gl->addWidget(map_decay_0, 1,0,1,1);

	setWindowTitle(tr("Quick Audio Sonar v1.1"));
	setWindowIcon(QIcon(":/qaudiosonar.png"));
}

void
QasView :: handle_view_0(int _value)
{
  	atomic_filter_lock();
	qas_view_what = (QasView_t)_value;
	atomic_filter_unlock();
}

void
QasView :: handle_decay_0(int _value)
{
	atomic_graph_lock();
	qas_view_decay = 1.0 - 1.0 / (1 << (_value + 3));
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
	double temp[QAS_MUL_SIZE];
	double adjust = bw_box_0->pSB->value() / 2.0;
	double center = bp_box_0->pSB->value();
	int level = ml_box_0->pSB->value();

	memset(temp, 0, sizeof(temp));
	qas_band_pass(center - adjust, center + adjust, temp, QAS_MUL_SIZE);

	atomic_lock();
	for (size_t x = 0; x != QAS_MUL_SIZE; x++)
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
	last_pi = 0;

	for (unsigned x = 0; x != QAS_BAND_SIZE; x++)
		mapping[x] = x;

	memset(band, 0, sizeof(band));

	setMinimumSize(200,480);
	watchdog->start(500);
}

QasGraph :: QasGraph(QasMainWindow *_mw)
{
	mw = _mw;
	watchdog = new QTimer(this);
	connect(watchdog, SIGNAL(timeout()), this, SLOT(handle_watchdog()));
	watchdog->start(500);
	setMinimumSize(640,320);
}

enum {
	TYPE_CORR,
	TYPE_AMP,
};

struct qas_corr {
	double value;
	double width;
};

static void
drawGraph(QPainter &paint, struct qas_corr *temp,
    int x_off, int y_off, int w, int h, unsigned num, unsigned type,
    double peak_sel)
{
	unsigned x;
	unsigned y;
	unsigned z;

	QColor red(255,64,64);
	QColor avg(192,32,32);
	QColor black(0,0,0);

	double sum = 0;
	double total_width = 0;
	for (x = y = z = 0; x != num; x++) {
		if (temp[x].value > temp[y].value)
			y = x;
		if (temp[x].value < temp[z].value)
			z = x;
		sum += temp[x].value;
		total_width += temp[x].width;
	}
	if (num)
		sum /= num;

	double min = temp[z].value;
	double max = temp[y].value;

	if (min < 0)
		min = -min;
	if (max < 0)
		max = -max;
	if (min < 1)
		min = 1;
	if (max < 1)
		max = 1;
	if (max < min) {
		double temp = max;
		max = min;
		min = temp;
	}

	double range = 2.0 * 1.125 * max;
	if (range == 0)
		range = 1;

	paint.setPen(QPen(red,0));
	paint.setBrush(red);

	double x_tmp = x_off;
	QRectF box;
	for (x = 0; x != num; x++) {
		double a = temp[x].value;
		double delta = (double)w * temp[x].width / total_width;
		if (a < 0) {
			box = QRectF(
			  x_tmp,
			  y_off + (h / 2),
			  delta,
			  (-a * h) / range);
		} else {
			box = QRectF(
			  x_tmp,
			  y_off + (h / 2) - (a * h) / range,
			  delta,
			  (a * h) / range);
		}
		paint.drawRect(box);
		x_tmp += delta;
	}

	paint.setPen(QPen(black,0));
	paint.setBrush(black);

	x_tmp = x_off;
	for (x = 0; x < num; x++) {
		double delta = (double)w * temp[x].width / total_width;

		if ((x % ((num + 15) / 16)) == 0) {
			box = QRectF(x_tmp,
				     y_off + (h / 2) - 8,
				     1.0,
				     16);
			paint.drawRect(box);
		}
		x_tmp += delta;
	}

	paint.setPen(QPen(avg,0));
	paint.setBrush(avg);
	
	box = QRectF(0, y_off + (h / 2) - (sum * h) / range,
		     w, 1);
	paint.drawRect(box);
	
	QString str;
	switch (type) {
	case TYPE_CORR:
		z = QAS_MON_SIZE - 1 - y;
		if (peak_sel < 1.0)
			peak_sel = 1.0;
		str = QString("CORRELATION MAX=%1dB@%2samples;%3ms;%4m      "
			      "SELECTION MAX=%5dB")
		    .arg(10.0 * log(max) / log(10)).arg(z)
		    .arg((double)((int)((z * 100000ULL) / qas_sample_rate) / 100.0))
		    .arg((double)((int)((z * 34000ULL) / qas_sample_rate) / 100.0))
		    .arg(10.0 * log(peak_sel) / log(10));
		break;
	case TYPE_AMP:
		str = QString("AMPLITUDE MAX=%1dB MIN=%2dB")
		  .arg(max / 100.0).arg(min / 100.0);
		break;
	default:
		break;
	}
	QFont fnt = paint.font();
	fnt.setPixelSize(16);
	paint.setFont(fnt);
	paint.setPen(QPen(black,0));
	paint.setBrush(black);
	paint.drawText(QPoint(x_off,y_off + 16),str);
}

static void
drawImage(QPainter &paint, double *temp, int x_off, int y_off, int w, int h,
    unsigned num, unsigned sel)
{
	unsigned x;
	unsigned y;

	for (y = 0; y != QAS_HISTORY_SIZE; y++) {
		double max = 0;

		for (x = 0; x != num; x++) {
			double value = temp[y + x * QAS_HISTORY_SIZE];
			if (value < 0)
				value = -value;
			if (value > max)
				max = value;
		}

		if (max == 0)
			max = 1;

		for (x = 0; x != num; x++) {
			double value = temp[y + x * QAS_HISTORY_SIZE];
			if (value < 0)
				value = -value;

			uint8_t code = (value > 0) ? (255 * value) / max : 0;
			QColor white(code, 255-code, (x != sel) ? 0 : 127);

			paint.setPen(QPen(white,0));
			paint.setBrush(white);

			QRectF box(x_off + (w * x) / (double)num,
				   y_off + (h * y) / (double)QAS_HISTORY_SIZE,
				   (w * 1) / (double)num,
				   (h * 1) / (double)QAS_HISTORY_SIZE);
			paint.drawRect(box);
		}
	}
}

static const QString
qas_band_to_key(uint8_t key)
{
	switch (key) {
	case 9:
		return ("A%1");
	case 11:
		return ("H%1");
	case 0:
		return ("C%1");
	case 2:
		return ("D%1");
	case 4:
		return ("E%1");
	case 5:
		return ("F%1");
	case 7:
		return ("G%1");
	case 10:
		return ("H%1B");
	case 8:
		return ("A%1B");
	case 6:
		return ("G%1B");
	case 3:
		return ("E%1B");
	case 1:
		return ("D%1B");
	default:
		return ("??");
	}
}

int
qas_band_power_compare(const void *_pa, const void *_pb)
{
	struct qas_band_info *pa = (struct qas_band_info *)_pa;
	struct qas_band_info *pb = (struct qas_band_info *)_pb;

	if (pa->power < (pa + 1)->power)
		pa++;
	if (pb->power < (pb + 1)->power)
		pb++;

	if (pa->power > pb->power)
		return (-1);
	else if (pa->power < pb->power)
		return (1);
	return (0);
}

int
qas_band_number_compare(const void *_pa, const void *_pb)
{
	struct qas_band_info *pa = (struct qas_band_info *)_pa;
	struct qas_band_info *pb = (struct qas_band_info *)_pb;

	if (pa->band > pb->band)
		return (1);
	else if (pa->band < pb->band)
		return (-1);
	return (0);
}

void
QasBand :: mousePressEvent(QMouseEvent *event)
{
	unsigned h = height() ? height() : 1;
	unsigned index = (QAS_HISTORY_SIZE * event->y()) / h;
	struct qas_band_info temp[QAS_BAND_SIZE];

	if (event->y() >= height() - 16) {
		for (unsigned x = 0; x != QAS_BAND_SIZE; x++)
			mapping[x] = x;
		update();
		return;
	}

	for (unsigned x = 0; x != QAS_BAND_SIZE; x++) {
		temp[x].power = band[index][x].power;
		temp[x].band = x;
	}

	mergesort(temp + 1, QAS_SCALE_SIZE / 2, 2 * sizeof(temp[0]), &qas_band_power_compare);

	for (unsigned x = 0; x != QAS_BAND_SIZE; x++)
		mapping[x] = temp[x].band;

	update();
}

void
QasBand :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	int w = width();
	int h = height();
	unsigned pi;

	QColor white(255,255,255);
	QColor grey(127,127,127);
	QColor black(0,0,0);

	paint.setPen(QPen(white,0));
	paint.setBrush(white);
	paint.drawRect(QRectF(0,0,w,h));

	atomic_filter_lock();
	pi = qas_power_index;
	for (unsigned y = 0; y != QAS_HISTORY_SIZE; y++) {
		unsigned z = (QAS_HISTORY_SIZE - 1 + pi - y) % QAS_HISTORY_SIZE;
		band[y][0].power = qas_band_power[y][0];
		band[y][0].band = 255;
		for (unsigned x = 1; x != QAS_BAND_SIZE; x++) {
			band[y][x].power = qas_band_power[z][x];
			band[y][x].band = x - 1;
		}
	}
	atomic_filter_unlock();

	for (unsigned y = 0; y != QAS_HISTORY_SIZE; y++) {
		double max = 0;
		for (unsigned x = 1; x != QAS_BAND_SIZE; x++) {
			if (band[y][mapping[x]].power > max)
				max = band[y][mapping[x]].power;
		}
		if (max == 0)
			max = 1;
		for (unsigned x = 1; x != QAS_BAND_SIZE; x++) {
			QRectF rect((w * (x - 1)) / QAS_SCALE_SIZE, (h * y) / QAS_HISTORY_SIZE,
			    (w + 11) / QAS_SCALE_SIZE, (h + QAS_HISTORY_SIZE - 1) / QAS_HISTORY_SIZE);

			double code = (band[y][mapping[x]].power * 255LL) / max;
			if (code > 255)
				code = 255;
			else if (code < 0)
				code = 0;

			QColor gradient(code, 255 - code, 0);
			paint.setPen(QPen(gradient,0));
			paint.setBrush(gradient);
			paint.drawRect(rect);
		}
	}

	last_pi = pi;

	QFont fnt(paint.font());
	qreal max_height = 0;

	fnt.setPixelSize(16);
	paint.setFont(fnt);
	paint.setPen(QPen(black,0));
	paint.setBrush(black);

	paint.rotate(-90);

	for (unsigned x = 1; x != QAS_BAND_SIZE; x++) {
		uint8_t key = mapping[x] - 1;
		if (key % 2)
			continue;
		QString str = QString(qas_band_to_key(key / 2)).arg(5);
		qreal xs = (qreal)w / (qreal)QAS_SCALE_SIZE;
		QRectF rect;
		paint.drawText(QRectF(-h, xs * x + 8 - 2 * xs, w,h), Qt::AlignLeft, str, &rect);
		paint.drawRect(QRectF(-h, xs * (x - 1), 16, 2));

		if (rect.width() > max_height)
			max_height = rect.width();
	}

	for (unsigned x = 1; x != QAS_BAND_SIZE; x++) {
		uint8_t key = mapping[x] - 1;
		if (key % 2)
			continue;
		QString str = QString(qas_band_to_key(key / 2)).arg(5);
		qreal xs = (qreal)w / (qreal)QAS_SCALE_SIZE;
		paint.drawText(QRectF(-max_height, xs * x + 8 - 2 * xs, w,h), Qt::AlignLeft, str);
		paint.drawRect(QRectF(-max_height, xs * (x - 1), 16, 2));
	}

	paint.rotate(90);

}

void
QasGraph :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	int w = width();
	int h = height();

	QColor white(255,255,255);
	QColor grey(127,127,127);
	QColor lightgrey(192,192,192);
	QColor black(0,0,0);

	paint.setPen(QPen(white,0));
	paint.setBrush(white);
	paint.drawRect(QRectF(0,0,w,h));

	atomic_filter_lock();
	unsigned num;
	qas_block_filter *f;
	num = 0;
	TAILQ_FOREACH(f, &qas_filter_head, entry)
		num++;

	double freq[num];
	double amp[num];
	double iso_amp[QAS_STANDARD_AUDIO_BANDS + 1] = {};
	uint8_t iso_num[num];
	double iso_div[QAS_STANDARD_AUDIO_BANDS + 1] = {};
	uint32_t iso_count[QAS_STANDARD_AUDIO_BANDS + 1] = {};
	double iso_xpos[QAS_STANDARD_AUDIO_BANDS + 1] = {};
	double power[num][QAS_HISTORY_SIZE];
	QString *descr = new QString [num];

	num = 0;

	TAILQ_FOREACH(f, &qas_filter_head, entry) {
		freq[num] = f->freq;
		if (f->descr)
			descr[num] = *f->descr;
		for (unsigned x = 0; x != QAS_HISTORY_SIZE; x++) {
			power[num][x] =
			  f->power[(QAS_HISTORY_SIZE - 1 +
			      qas_power_index - x) % QAS_HISTORY_SIZE];
		}
		num++;
	}
	atomic_filter_unlock();

	struct qas_corr corr[QAS_MON_SIZE];
	double corr_raw[QAS_MON_SIZE];

	int zoom = mw->sb_zoom->value();

	atomic_graph_lock();
	for (unsigned x = 0; x != QAS_MON_SIZE; x++) {
		double offset = (double)x - (double)zoom;
		if (offset < 0)
			offset = -offset;
		corr[x].value = qas_graph_data[x];
		corr[x].width = pow(QAS_MON_SIZE - offset, 4.0);

		corr_raw[x] = qas_graph_data[x];
	}
	atomic_graph_unlock();

	double amp_max;

	atomic_filter_lock();
	unsigned fn = 0;
	switch (qas_view_what) {
	case VIEW_AMP_LIN:
		amp_max = 1.0;
		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			double t_amp = f->t_amp;

			if (t_amp > iso_amp[f->iso_index])
				iso_amp[f->iso_index] = t_amp;
			if (fn < num) {
				iso_num[fn] = f->iso_index;
				if (t_amp >= 1.0) {
					amp[fn++] = t_amp;
					if (t_amp > amp_max)
						amp_max = t_amp;
				} else {
					amp[fn++] = 0.0;
				}
			}
		}
		break;
	case VIEW_AMP_LOG:
		amp_max = 1.0;
		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			double t_amp = f->t_amp;

			if (t_amp < 1.0)
				t_amp = 0;
			else
				t_amp = log(t_amp);
		  
			if (t_amp > iso_amp[f->iso_index])
				iso_amp[f->iso_index] = t_amp;
			if (fn < num) {
				iso_num[fn] = f->iso_index;
				if (t_amp >= 1.0) {
					amp[fn++] = t_amp;
					if (t_amp > amp_max)
						amp_max = t_amp;
				} else {
					amp[fn++] = 0.0;
				}
			}
		}
		break;
	case VIEW_PHASE_LIN:
		amp_max = 2.0 * M_PI;
		TAILQ_FOREACH(f, &qas_filter_head, entry)
			iso_div[f->iso_index] ++;

		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			double t_phase = f->t_phase;

			if (t_phase < 0.0)
				t_phase += 2.0 * M_PI;
			if (t_phase >= 2.0 * M_PI)
				t_phase -= 2.0 * M_PI;

			iso_amp[f->iso_index] += t_phase / iso_div[f->iso_index];
			if (fn < num) {
				iso_num[fn] = f->iso_index;
				amp[fn++] = t_phase;
			}
		}
		break;
	default:
		amp_max = 1.0;
		break;
	}
	while (fn < num) {
		iso_num[fn] = 0;
		amp[fn++] = 0;
	}
	atomic_filter_unlock();

	double xs = w / (double)num;
	double xv = mw->sb->value() * xs;

	paint.setPen(QPen(grey,0));
	paint.setBrush(grey);
	paint.drawRect(QRectF(xv,0,xs,h));

	int sel = mw->sb->value();

	drawGraph(paint, corr, 0, 0, w, h / 2, QAS_MON_SIZE, TYPE_CORR, power[0][sel]);
	drawImage(paint, &power[0][0], 0, h / 2, w, h / 2, num, sel);

	QFont fnt = paint.font();
	QString str;

	fnt.setPixelSize(16);
	paint.setFont(fnt);

	paint.setPen(QPen(lightgrey,0));
	paint.setBrush(lightgrey);
	
	for (unsigned x = 0; x != num; x++) {
		double hhh = h / 4.0;
		double hh = h / 2.0;
		double ih = (iso_amp[iso_num[x]] / amp_max) * hhh;

		iso_count[iso_num[x]]++;
		iso_xpos[iso_num[x]] += xs * x;

		paint.drawRect(QRectF(xs * x, hh - ih, xs, ih));
	}

	paint.setPen(QPen(black,0));
	paint.setBrush(black);

	for (unsigned x = 0; x != QAS_STANDARD_AUDIO_BANDS; x++) {
		if (iso_count[x + 1] == 0)
			continue;
		double ih = (h / 2.0);
		if (qas_freq_table[x] >= 1000.0)
			str = QString("%1K").arg(qas_freq_table[x] / 1000.0);
		else
			str = QString("%1").arg(qas_freq_table[x]);
		paint.drawText(QPoint(iso_xpos[x + 1] / iso_count[x + 1], ih - 2.0), str);
	}

	for (unsigned x = 0; x != num; x++) {
		double hhh = h / 4.0;
		double hh = h / 2.0;

		paint.drawRect(QRectF(xs * x, hh -
		    (amp[x] / amp_max) * hhh, xs, 2.0));
	}

	paint.rotate(-90);

	for (unsigned x = 0; x != num; x++) {
		str = QString("%1 Hz").arg((double)(int)(2.0 * freq[x]) / 2.0);
		str += descr[x];
		paint.drawText(QPoint(-h, xs * x + 8 + xs / 2.0), str);
		paint.drawRect(QRectF(-h, xs * x, 16, 2));
	}

	paint.rotate(90);

	delete [] descr;
}

void
QasBand :: handle_watchdog()
{
	update();
}

void
QasGraph :: handle_watchdog()
{
	mw->update_qr();
	update();
}

QasMainWindow :: QasMainWindow()
{
	QPushButton *pb;

	qr = new QasRecord();
	qc = new QasConfig(this);
	qv = new QasView(this);

	gl = new QGridLayout(this);

	qg = new QasGraph(this);
	qb = new QasBand(this);

	sb_zoom = new QScrollBar(Qt::Horizontal);
	sb_zoom->setRange(0, QAS_MON_SIZE - 1);
	sb_zoom->setSingleStep(1);
	sb_zoom->setValue(0);

	sb = new QScrollBar(Qt::Horizontal);
	sb->setRange(0, 0);
	sb->setSingleStep(1);
	sb->setValue(0);
	connect(sb, SIGNAL(valueChanged(int)), this, SLOT(handle_slider(int)));

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

	spn = new QSpinBox();
	spn->setRange(1,1024);
	spn->setSuffix(tr(" bands"));
	spn->setValue(qas_number_bands);
	gl->addWidget(spn, 1,3,1,1);
	
	pb = new QPushButton(tr("AddLog"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_log()));
	gl->addWidget(pb, 0,4,1,1);

	pb = new QPushButton(tr("AddLin"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_lin()));
	gl->addWidget(pb, 0,5,1,1);

	pb = new QPushButton(tr("AddPiano"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_piano()));
	gl->addWidget(pb, 1,4,1,1);

	pb = new QPushButton(tr("AudioConfig"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_config()));
	gl->addWidget(pb, 0,7,1,1);
       
	pb = new QPushButton(tr("DelAll"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_del_all()));
	gl->addWidget(pb, 0,3,1,1);

	pb = new QPushButton(tr("TogFrz"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_freeze()));
	gl->addWidget(pb, 0,6,1,1);

	pb = new QPushButton(tr("ViewConfig"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_view()));
	gl->addWidget(pb, 1,6,1,1);

	pb = new QPushButton(tr("ShowRecord"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_show_record()));
	gl->addWidget(pb, 1,7,1,1);

	gl->addWidget(qb, 0,8,6,2);
	gl->addWidget(sb_zoom, 3,0,1,8);
	gl->addWidget(sb, 6,0,1,8);
	gl->addWidget(qg, 4,0,2,8);

	gl->setRowStretch(2,1);

	setWindowTitle(tr("Quick Audio Sonar v1.1"));
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

	atomic_filter_lock();
	memset(qas_band_power, 0, sizeof(qas_band_power));
	memset(dsp_rd_mon_filter, 0, sizeof(dsp_rd_mon_filter));
	atomic_filter_unlock();
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
QasMainWindow :: update_sb()
{
	qas_block_filter *f;
	int num = 0;

	atomic_filter_lock();
	TAILQ_FOREACH(f, &qas_filter_head, entry)
		num++;
	atomic_filter_unlock();

	sb->setRange(0, num ? num - 1 : num);
}

static uint8_t
qas_find_iso(double cf)
{
	unsigned y;

	for (y = 0; y != QAS_STANDARD_AUDIO_BANDS; y++) {
		if (y == 0) {
			double limit = (qas_freq_table[0] + qas_freq_table[1]) / 2.0;
			if (cf < limit)
				return (y + 1);
		} else if (y == QAS_STANDARD_AUDIO_BANDS - 1) {
			double limit = (qas_freq_table[y] + qas_freq_table[y - 1]) / 2.0;
			if (cf >= limit)
				return (y + 1);
		} else {
			double lower_limit = (qas_freq_table[y] + qas_freq_table[y - 1]) / 2.0;
			double upper_limit = (qas_freq_table[y] + qas_freq_table[y + 1]) / 2.0;
			if (cf >= lower_limit && cf < upper_limit)
				return (y + 1);
		}
	}
	return (0);
}

void
QasMainWindow :: handle_add_log()
{
	qas_block_filter *f;
	unsigned max_bands = spn->value();
	double max_width;
	double range;
	double width;
	double step;
	double cf;
	double pf;

	for (unsigned x = 0; x != max_bands; x++) {
		range = qas_sample_rate / 2.0;
		step = range / max_bands;
		pf = pow((range - step) / step, 1.0 / max_bands);
		max_width = (range - step) * (1.0 - (1.0 / pf)) / 2.0;
		width = step * (pow(pf, x + 1) - pow(pf, x)) / 2.0;
		cf = step * pow(pf, x);

		f = new qas_block_filter(sqrt(max_width / width), cf - width, cf + width);

		f->iso_index = qas_find_iso(cf);
		
		qas_queue_block_filter(f, &qas_filter_head);
	}
	update_sb();
}

void
QasMainWindow :: handle_add_lin()
{
	qas_block_filter *f;
	unsigned max_bands = spn->value();
	double range;
	double step;

	for (unsigned x = 0; x != max_bands; x++) {

		range = qas_sample_rate / 2.0;
		step = range / (max_bands + 3);

		f = new qas_block_filter(1.0, step * (x + 1), step * (x + 2));

		f->iso_index = qas_find_iso(step * x);

		qas_queue_block_filter(f, &qas_filter_head);
	}
	update_sb();
}

void
QasMainWindow :: handle_add_piano()
{
	qas_block_filter *f;
	QString str;
	unsigned max_bands = spn->value();
	double max_width;
	unsigned x;
	unsigned octave;
	int y;
	double cf;
	double lf;
	double hf;
	double pf;

	QAS_BAND_ALIGN(max_bands);

	pf = pow(2.0, 1.0 / (double)QAS_SCALE_SIZE);

	x = max_bands;
	y = x - (max_bands / 2);
	lf = 440.0 * pow(pf, y - 1);
	cf = 440.0 * pow(pf, y);
	hf = 440.0 * pow(pf, y + 1);

	lf = (lf + cf) / 2.0;
	hf = (hf + cf) / 2.0;

	max_width = (hf - lf);
	
	for (x = 0; x != max_bands; x++) {
	  	y = x - (max_bands / 2);
		lf = 440.0 * pow(pf, y - 1);
		cf = 440.0 * pow(pf, y);
		hf = 440.0 * pow(pf, y + 1);

		lf = (lf + cf) / 2.0;
		hf = (hf + cf) / 2.0;

		f = new qas_block_filter(sqrt(max_width / (hf - lf)), lf, hf);

		f->num_index = y + ((QAS_SCALE_SIZE / 12) * 9) + (5 * QAS_SCALE_SIZE);
		f->iso_index = qas_find_iso(cf);

		octave = f->num_index / QAS_SCALE_SIZE;

		if (f->num_index % 2) {
			str = "";
		} else switch ((f->num_index % QAS_SCALE_SIZE) / 2) {
		case 9:
			str = QString(" - A%1").arg(octave);
			break;
		case 11:
			str = QString(" - H%1").arg(octave);
			break;
		case 0:
			str = QString(" - C%1").arg(octave);
			break;
		case 2:
			str = QString(" - D%1").arg(octave);
			break;
		case 4:
			str = QString(" - E%1").arg(octave);
			break;
		case 5:
			str = QString(" - F%1").arg(octave);
			break;
		case 7:
			str = QString(" - G%1").arg(octave);
			break;
		case 10:
			str = QString(" - H%1B").arg(octave);
			break;
		case 8:
			str = QString(" - A%1B").arg(octave);
			break;
		case 6:
			str = QString(" - G%1B").arg(octave);
			break;
		case 3:
			str = QString(" - E%1B").arg(octave);
			break;
		case 1:
			str = QString(" - D%1B").arg(octave);
			break;
		default:
			str = "";
			break;
		}
		f->descr = new QString(str);
		qas_queue_block_filter(f, &qas_filter_head);
	}
	update_sb();
}

void
QasMainWindow :: handle_slider(int value)
{
}

void
QasMainWindow :: handle_del_all()
{
  	qas_block_filter *f;

	atomic_filter_lock();
	while ((f = TAILQ_FIRST(&qas_filter_head))) {
		TAILQ_REMOVE(&qas_filter_head, f, entry);
		delete f;
	}
	atomic_filter_unlock();

	sb->setRange(0,0);
}

void
QasMainWindow :: handle_tog_freeze()
{
	atomic_lock();
	qas_freeze = !qas_freeze;
	atomic_unlock();
}

void
QasMainWindow :: handle_show_record()
{
	qr->show();
}

void
QasMainWindow :: update_qr()
{
	unsigned int num;
	qas_block_filter *f;
	double power;
	static unsigned int qas_last_index;

	atomic_filter_lock();
	num = 0;
	TAILQ_FOREACH(f, &qas_filter_head, entry)
		num++;
	while ((QAS_HISTORY_SIZE + qas_power_index - qas_last_index) % QAS_HISTORY_SIZE) {
		QasRecordEntry *rec = new QasRecordEntry(num);
		num = 0;
		TAILQ_FOREACH(f, &qas_filter_head, entry) {
			QString str;
			power = f->power[(QAS_HISTORY_SIZE - 1 +
			    qas_last_index) % QAS_HISTORY_SIZE];
			if (power < 0)
				power = 0;
			rec->pvalue[num] = power;
			str = QString("%1 Hz").arg((double)(int)(2.0 * f->freq) / 2.0);
			if (f->descr != 0)
				str += f->descr;
			rec->pdesc[num] = str;
			num++;
		}
		qas_last_index = (qas_last_index + 1) % QAS_HISTORY_SIZE;
		qr->insert_entry(rec);
	}
	atomic_filter_unlock();
}

static void
usage(void)
{
	fprintf(stderr, "Usage: qaudiosonar [-r <samplerate>] [-b <bands>] [-l <n>]\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	QApplication app(argc, argv);
	int c;

        /* must be first, before any threads are created */
        signal(SIGPIPE, SIG_IGN);
	
	while ((c = getopt(argc, argv, "r:b:h")) != -1) {
		switch (c) {
		case 'b':
			qas_number_bands = atoi(optarg);
			if (qas_number_bands < QAS_SCALE_SIZE)
				qas_number_bands = QAS_SCALE_SIZE;
			else if (qas_number_bands > QAS_BAND_MAX)
				qas_number_bands = QAS_BAND_MAX;
			QAS_BAND_ALIGN(qas_number_bands);
			break;
		case 'r':
			qas_sample_rate = atoi(optarg);
			if (qas_sample_rate < 8000)
				qas_sample_rate = 8000;
			else if (qas_sample_rate > 96000)
				qas_sample_rate = 96000;
			break;
		default:
			usage();
			break;
		}
	}

	pthread_t td;

	atomic_init();

	pthread_create(&td, NULL, &qas_dsp_audio_producer, NULL);
	pthread_create(&td, NULL, &qas_dsp_audio_analyzer, NULL);
	pthread_create(&td, NULL, &qas_dsp_write_thread, NULL);
	pthread_create(&td, NULL, &qas_dsp_read_thread, NULL);
	pthread_create(&td, NULL, &qas_midi_write_thread, NULL);

	QasMainWindow *mw = new QasMainWindow();

	mw->show();

	return (app.exec());
}
