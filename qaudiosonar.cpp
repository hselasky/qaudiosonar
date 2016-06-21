/*-
 * Copyright (c) 2016 Hans Petter Selasky. All rights reserved.
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

static pthread_mutex_t atomic_mtx;
static pthread_mutex_t atomic_graph;
static pthread_mutex_t atomic_filter;
static pthread_cond_t atomic_cv;

#define	QAS_STANDARD_AUDIO_BANDS 31

static const double qas_freq_table[2 + QAS_STANDARD_AUDIO_BANDS] = {
	15, 20, 25, 31.5, 40, 50, 63, 80, 100, 125,
	160, 200, 250, 315, 400, 500, 630, 800, 1000,
	1250, 1600, 2000, 2500, 3150, 4000, 5000,
	6300, 8000, 10000, 12500, 16000, 20000, 23500,
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

static void
drawGraph(QPainter &paint, int64_t *temp,
    int x_off, int y_off, int w, int h, unsigned num, unsigned type)
{
	unsigned x;
	unsigned y;
	unsigned z;

	QColor red(255,64,64);
	QColor avg(192,32,32);
	QColor black(0,0,0);

	int64_t sum = 0;
	for (x = y = z = 0; x != num; x++) {
		if (temp[x] > temp[y])
			y = x;
		if (temp[x] < temp[z])
			z = x;
		sum += temp[x];
	}
	if (num)
		sum /= num;

	int64_t min = temp[z];
	int64_t max = temp[y];

	if (min < 0)
		min = -min;
	if (max < 0)
		max = -max;
	if (min < 1)
		min = 1;
	if (max < 1)
		max = 1;
	if (max < min) {
		int64_t temp = max;
		max = min;
		min = temp;
	}

	int64_t range = 2.0 * 1.125 * max;
	if (range == 0)
		range = 1;

	double delta = w / (double)num;

	paint.setPen(QPen(red,0));
	paint.setBrush(red);

	QRectF box;
	for (x = 0; x != num; x++) {
		int64_t a = -temp[x];
		if (a < 0) {
			box = QRectF(
			  x_off + (x * delta),
			  y_off + (h / 2),
			  delta,
			  (-a * h) / range);
		} else {
			box = QRectF(
			  x_off + (x * delta),
			  y_off + (h / 2) - (a * h) / range,
			  delta,
			  (a * h) / range);
		}
		paint.drawRect(box);
	}

	paint.setPen(QPen(avg,0));
	paint.setBrush(avg);
	
	box = QRectF(0, y_off + (h / 2) - (sum * h) / range,
		     w, 1);
	paint.drawRect(box);
	
	QString str;
	switch (type) {
	case TYPE_CORR:
		str = QString("CORRELATION MAX=%1dB@%2")
		  .arg(10.0 * log(max) / log(10)).arg(y);
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
drawImage(QPainter &paint, int64_t *temp,
    int x_off, int y_off, int w, int h, unsigned num, unsigned sel)
{
	unsigned x;
	unsigned y;
	unsigned z;

	for (x = y = z = 0; x != QAS_HISTORY_SIZE * num; x++) {
		if (temp[x] > temp[y])
			y = x;
		if (temp[x] < temp[z])
			z = x;
	}
	int64_t min = temp[z];
	int64_t max = temp[y];

	if (min < 0)
		min = -min;
	if (max < 0)
		max = -max;
	if (min < 1)
		min = 1;
	if (max < 1)
		max = 1;
	if (max < min) {
		int64_t temp = max;
		max = min;
		min = temp;
	}

	for (x = 0; x != num; x++) {
		for (y = 0; y != QAS_HISTORY_SIZE; y++) {
			int64_t value = temp[y + x * QAS_HISTORY_SIZE];
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

static double
qas_to_decibel(double x)
{
	if (x == 0)
		;
	else if (x < 0)
		x = -1000.0 * log(-x) / log(10);
	else
		x = 1000.0 * log(x) / log(10);
	return (x);
}

void
QasGraph :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	int w = width();
	int h = height();

	QColor white(255,255,255);
	QColor grey(127,127,127);
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
	int64_t power[num][QAS_HISTORY_SIZE];
	QString descr[num];

	num = 0;
	int64_t sum = 0;
	TAILQ_FOREACH(f, &qas_filter_head, entry) {
		freq[num] = f->freq;
		if (f->descr)
			descr[num] = *f->descr;
		for (unsigned x = 0; x != QAS_HISTORY_SIZE; x++) {
			power[num][x] =
			  qas_to_decibel(f->power[(QAS_HISTORY_SIZE - 1 +
			      qas_power_index - x) % QAS_HISTORY_SIZE]) -
			  qas_to_decibel(f->power_ref);
			sum += power[num][x];
		}
		num++;
	}
	if (num)
		sum /= num * QAS_HISTORY_SIZE;
	for (unsigned x = 0; x != num; x++)
		for (unsigned y = 0; y != QAS_HISTORY_SIZE; y++)
			power[x][y] -= sum;
	atomic_filter_unlock();

	int64_t corr[QAS_MON_SIZE];

	atomic_graph_lock();
	memcpy(corr, qas_graph_data, sizeof(corr));
	atomic_graph_unlock();

	double xs = w / (double)num;
	double xv = mw->sb->value() * xs;

	paint.setPen(QPen(grey,0));
	paint.setBrush(grey);
	paint.drawRect(QRectF(xv,0,xs,h));

	drawGraph(paint, corr, 0, 0, w, h / 2, QAS_MON_SIZE, TYPE_CORR);
	drawImage(paint, &power[0][0], 0, h / 2, w, h / 2, num, mw->sb->value());

	QFont fnt = paint.font();
	QString str;

	fnt.setPixelSize(16);
	paint.setFont(fnt);
	paint.setPen(QPen(black,0));
	paint.setBrush(black);

	paint.rotate(-90);

	for (unsigned x = 0; x != num; x++) {
		str = QString("%1 Hz").arg((double)(int)(2.0 * freq[x]) / 2.0);
		str += descr[x];
		paint.drawText(QPoint(-h, xs * x + 8 + xs / 2.0), str);
		paint.drawRect(QRectF(-h, xs * x, 16, 2));
	}

	paint.rotate(90);
}

void
QasGraph :: handle_watchdog()
{
	update();
}

QasMainWindow :: QasMainWindow()
{
	QPushButton *pb;

	gl = new QGridLayout(this);

	qg = new QasGraph(this);

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

	pb = new QPushButton(tr("Apply"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_apply()));
	gl->addWidget(pb, 0,2,1,1);

	pb = new QPushButton(tr("Reset"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_reset()));
	gl->addWidget(pb, 0,3,1,1);

	spn = new QSpinBox();
	spn->setRange(1,1024);
	spn->setSuffix(tr(" bands"));
	spn->setValue(32);
	gl->addWidget(spn, 1,2,1,1);
	
	pb = new QPushButton(tr("AddISO"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_iso()));
	gl->addWidget(pb, 0,4,1,1);

	pb = new QPushButton(tr("AddLog"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_log()));
	gl->addWidget(pb, 0,5,1,1);

	pb = new QPushButton(tr("AddLin"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_lin()));
	gl->addWidget(pb, 0,6,1,1);

	pb = new QPushButton(tr("AddPiano"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_add_piano()));
	gl->addWidget(pb, 0,7,1,1);
	
	pb = new QPushButton(tr("DelAll"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_del_all()));
	gl->addWidget(pb, 1,3,1,1);

	pb = new QPushButton(tr("SetProfile"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_set_profile()));
	gl->addWidget(pb, 1,4,1,1);

	pb = new QPushButton(tr("MuteTog"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_mute()));
	gl->addWidget(pb, 1,5,1,1);

	pb = new QPushButton(tr("TogFrz"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_freeze()));
	gl->addWidget(pb, 1,6,1,1);

	pb = new QPushButton(tr("TogNoise"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_tog_noise()));
	gl->addWidget(pb, 1,7,1,1);
	
	gl->addWidget(sb, 3,0,1,8);
	gl->addWidget(qg, 2,0,1,8);

	gl->setRowStretch(1,1);

	setWindowTitle(tr("Quick Audio Sonar v1.0"));
	setWindowIcon(QIcon(":/qaudiosonar.png"));
}

void
QasMainWindow :: handle_apply()
{
	QString dsp_rd = led_dsp_read->text().trimmed();
	QString dsp_wr = led_dsp_write->text().trimmed();
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
	atomic_wakeup();
	atomic_unlock();
}

void
QasMainWindow :: handle_reset()
{
	qas_dsp_sync();
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

void
QasMainWindow :: handle_add_iso()
{
	qas_block_filter *f;
	double max_width;
	double width;
	double cf;

	max_width = (qas_freq_table[QAS_STANDARD_AUDIO_BANDS + 1] -
		     qas_freq_table[QAS_STANDARD_AUDIO_BANDS - 1]) / 4.0;

	for (unsigned x = 0; x != QAS_STANDARD_AUDIO_BANDS; x++) {
		if (qas_freq_table[x + 2] >= (qas_sample_rate / 2.0))
			break;
		width = (qas_freq_table[x + 2] - qas_freq_table[x]) / 4.0;
		cf = qas_freq_table[x + 1];

		f = new qas_block_filter(sqrt(max_width / width), cf - width, cf + width);

		qas_queue_block_filter(f, &qas_filter_head);
	}
	update_sb();
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

		qas_queue_block_filter(f, &qas_filter_head);
	}
	update_sb();
}

void
QasMainWindow :: handle_add_piano()
{
	qas_block_filter *f;
	unsigned max_bands = spn->value();
	double max_width;
	unsigned x;
	double cf;
	double lf;
	double hf;
	double pf;

	pf = pow(2.0, 1.0 / 12.0);

	x = max_bands;
	lf = 440.0 * pow(pf, (int)x - 1 - (int)max_bands / 2);
	cf = 440.0 * pow(pf, (int)x - (int)max_bands / 2);
	hf = 440.0 * pow(pf, (int)x + 1 - (int)max_bands / 2);

	lf = (lf + cf) / 2.0;
	hf = (hf + cf) / 2.0;

	max_width = (hf - lf);
	
	for (x = 0; x != max_bands; x++) {
		lf = 440.0 * pow(pf, (int)x - 1 - (int)max_bands / 2);
		cf = 440.0 * pow(pf, (int)x - (int)max_bands / 2);
		hf = 440.0 * pow(pf, (int)x + 1 - (int)max_bands / 2);

		lf = (lf + cf) / 2.0;
		hf = (hf + cf) / 2.0;

		f = new qas_block_filter(sqrt(max_width / (hf - lf)), lf, hf);

		int key = (12 + x - ((max_bands / 2) % 12) + 9) % 12;
		
		switch (key) {
		case 9:
			f->descr = new QString(" - A");
			break;
		case 11:
			f->descr = new QString(" - H");
			break;
		case 0:
			f->descr = new QString(" - C");
			break;
		case 2:
			f->descr = new QString(" - D");
			break;
		case 4:
			f->descr = new QString(" - E");
			break;
		case 5:
			f->descr = new QString(" - F");
			break;
		case 7:
			f->descr = new QString(" - G");
			break;
		case 10:
			f->descr = new QString(" - Hb");
			break;
		case 8:
			f->descr = new QString(" - Ab");
			break;
		case 6:
			f->descr = new QString(" - Gb");
			break;
		case 3:
			f->descr = new QString(" - Eb");
			break;
		case 1:
			f->descr = new QString(" - Db");
			break;
		default:
			break;
		}
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
  	qas_wave_filter *w;

	atomic_filter_lock();
	while ((f = TAILQ_FIRST(&qas_filter_head))) {
		TAILQ_REMOVE(&qas_filter_head, f, entry);
		delete f;
	}
	while ((w = TAILQ_FIRST(&qas_wave_head))) {
		TAILQ_REMOVE(&qas_wave_head, w, entry);
		delete w;
	}
	atomic_filter_unlock();

	sb->setRange(0,0);
}

void
QasMainWindow :: handle_set_profile()
{
  	qas_block_filter *f;

	atomic_filter_lock();
	TAILQ_FOREACH(f, &qas_filter_head, entry)
		f->power_ref = f->power[0];
	atomic_filter_unlock();
}

void
QasMainWindow :: handle_tog_mute()
{
	atomic_lock();
	qas_mute = (qas_mute + 1) % 3;
	atomic_unlock();
}

void
QasMainWindow :: handle_tog_freeze()
{
	atomic_lock();
	qas_freeze = !qas_freeze;
	atomic_unlock();
}

void
QasMainWindow :: handle_tog_noise()
{
	atomic_lock();
	qas_noise_type = !qas_noise_type;
	atomic_unlock();
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

	while ((c = getopt(argc, argv, "r:h")) != -1) {
		switch (c) {
		case 'r':
			qas_sample_rate = atoi(optarg);
			if (qas_sample_rate < 1)
				qas_sample_rate = 1;
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

	QasMainWindow *mw = new QasMainWindow();

	mw->show();

	return (app.exec());
}
