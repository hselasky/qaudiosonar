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
static pthread_mutex_t atomic_gtx;
static pthread_cond_t atomic_cv;

static void
atomic_init(void)
{
	pthread_mutex_init(&atomic_mtx, NULL);
	pthread_mutex_init(&atomic_gtx, NULL);
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
	pthread_mutex_lock(&atomic_gtx);
}

void
atomic_graph_unlock(void)
{
	pthread_mutex_unlock(&atomic_gtx);
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
	TYPE_PHASE,
};

static void
drawGraph(QPainter &paint, const int64_t *graph,
    int x_off, int y_off, int w, int h, unsigned num, unsigned type)
{
	int64_t temp[num];
	unsigned x;
	unsigned y;
	unsigned z;

	QColor red(255,64,64);
	QColor avg(192,32,32);
	QColor black(0,0,0);

	atomic_graph_lock();
	memcpy(temp, graph, sizeof(temp));
	atomic_graph_unlock();

	atomic_lock();
	int64_t freq = qas_freq;
	atomic_unlock();

	double f = 1000.0 / log(10);

	switch (type) {
	case TYPE_AMP:
		for (x = 0; x != num; x++) {
			if (temp[x] <= 0)
				continue;
			temp[x] = f * log(temp[x]);
		}
		break;
	default:
		break;
	}

	switch (type) {
	case TYPE_CORR:
		for (x = y = z = 0; x != num; x++) {
			if (temp[x] > temp[y])
				y = x;
			if (temp[x] < temp[z])
				z = x;
		}
		break;
	default:
		for (x = y = z = 0; x != num; x++) {
			if (temp[x] == 0)
				continue;
			if (temp[y] == 0 || temp[x] > temp[y])
				y = x;
			if (temp[z] == 0 || temp[x] < temp[z])
				z = x;
		}
		break;
	}
	int64_t min = temp[z];
	int64_t max = temp[y];
	int64_t zero;

	switch (type) {
	case TYPE_CORR:
		if (min < 0)
			min = -min;
		if (max < 0)
			max = -max;
		if (max < min)
			max = min;
		min = -max;
		zero = 0;
		break;
	default:
		zero = (max + min) / 2;
		break;
	}

	int64_t range = (max - min) * 1.125;
	if (range == 0)
		range = 1;

	double delta = w / (double)num;

	paint.setPen(QPen(red,0));
	paint.setBrush(red);

	for (x = 0; x != num; x++) {
		int64_t a = temp[x] - zero;
		switch (type) {
		case TYPE_AMP:
		case TYPE_PHASE:
			if (temp[x] == 0)
				a = 0;
			break;
		}
		QRectF box;
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
	QString str;
	switch (type) {
	case TYPE_CORR:
		str = QString("CORRELATION MAX=%1dB@%2 FREQ=%3Hz")
		  .arg(log(max) * f / 100.0).arg(y).arg(freq);
		break;
	case TYPE_AMP:
		str = QString("AMPLITUDE MAX=%1dB MIN=%2dB")
		  .arg(max / 100.0).arg(min / 100.0);
		break;
	case TYPE_PHASE:
		str = QString("PHASE MAX=%1 MIN=%2 DELTA=%3=%4s")
		  .arg(max).arg(min).arg(max-min).arg((max-min)/(double)qas_sample_rate);
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

void
QasGraph :: paintEvent(QPaintEvent *event)
{
	QPainter paint(this);
	int w = width();
	int h = height();

	QColor white(255,255,255);
	QColor grey(127,127,127);

	paint.setPen(QPen(white,0));
	paint.setBrush(white);
	paint.drawRect(QRectF(0,0,w,h));

	double xs = w / (double)qas_bands;
	double xv = mw->sb->value() * xs;

	paint.setPen(QPen(grey,0));
	paint.setBrush(grey);
	paint.drawRect(QRectF(xv,0,xs,h));

	drawGraph(paint, qas_graph_data, 0, 0, w, h / 3, QAS_MON_SIZE, TYPE_CORR);
	drawGraph(paint, qas_max_data, 0, h / 3, w, h / 3, qas_bands, TYPE_AMP);
	drawGraph(paint, qas_phase_data, 0, 2 * h / 3, w, h / 3, qas_bands, TYPE_PHASE);

	QFont fnt = paint.font();
	QString str = QString("%1Hz").arg((int)mw->get_freq(mw->sb->value()));
	fnt.setPixelSize(16);
	paint.setFont(fnt);
	paint.setPen(QPen(grey,0));
	paint.setBrush(grey);
	paint.drawText(QPoint(xv+8,h-8),str);
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
	sb->setRange(0, qas_bands - 1);
	sb->setSingleStep(1);
	sb->setValue(1);
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
	connect(pb, SIGNAL(released()), this, SLOT(handle_sync()));
	gl->addWidget(pb, 0,3,1,1);

	pb = new QPushButton(tr("LogTog"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_logarithmic()));
	gl->addWidget(pb, 0,4,1,1);

	pb = new QPushButton(tr("<<"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_sweep_down()));
	gl->addWidget(pb, 1,2,1,1);

	pb = new QPushButton(tr("SweepLock"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_sweep_lock()));
	gl->addWidget(pb, 1,3,1,1);

	pb = new QPushButton(tr(">>"));
	connect(pb, SIGNAL(released()), this, SLOT(handle_sweep_up()));
	gl->addWidget(pb, 1,4,1,1);

	gl->addWidget(sb, 3,0,1,5);
	gl->addWidget(qg, 2,0,1,5);

	gl->setRowStretch(1,1);

	qas_sweeping = 0;
	sweep_timer = new QTimer(this);
	connect(sweep_timer, SIGNAL(timeout()), this, SLOT(handle_sweep_timer()));
	sweep_timer->start(1000);

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
QasMainWindow :: handle_sync()
{
	qas_sweeping = 0;
	qas_dsp_sync();
}

void
QasMainWindow :: handle_logarithmic()
{
	qas_logarithmic = !qas_logarithmic;
}

void
QasMainWindow :: set_filter(int value)
{
	double range = qas_sample_rate / 2.0;
	double step = range / qas_bands;
	qas_filter *f;

	if (qas_logarithmic) {
		double pf = pow((range - step) / step, 1.0 / qas_bands);
		double cf = step * pow(pf, value);

		f = new qas_filter(QAS_WINDOW_SIZE, value, cf, cf + step);
	} else {
		f = new qas_filter(QAS_WINDOW_SIZE, value,
		    value * step, (value + 1) * step);
	}
	qas_queue_filter(f);
}

double
QasMainWindow :: get_freq(int value)
{
	double range = qas_sample_rate / 2.0;
	double step = range / qas_bands;

	if (qas_logarithmic) {
		double pf = pow((range - step) / step, 1.0 / qas_bands);
		double cf = step * pow(pf, value);

		return (cf + step / 2.0);
	} else {
		return (value * step + step / 2.0);
	}
}

void
QasMainWindow :: handle_slider(int value)
{
	if (qas_sweeping == 0)
		return;
	set_filter(value);
}

void
QasMainWindow :: handle_sweep_up()
{
	qas_sweeping = 1;
}

void
QasMainWindow :: handle_sweep_lock()
{
	qas_sweeping = 0;
	set_filter(sb->value());
}

void
QasMainWindow :: handle_sweep_down()
{
	qas_sweeping = -1;
}

void
QasMainWindow :: handle_sweep_timer()
{
	int value = sb->value();
	if (value == sb->maximum() && qas_sweeping == 1)
		qas_sweeping = -1;
	if (value == sb->minimum() && qas_sweeping == -1)
		qas_sweeping = 1;
	value += qas_sweeping;
	sb->setValue(value);
}

static void
usage(void)
{
	fprintf(stderr, "Usage: qaudiosonar [-r <samplerate>] [-b <bands>] [-l]\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	QApplication app(argc, argv);
	int c;

	while ((c = getopt(argc, argv, "b:lr:h")) != -1) {
		switch (c) {
		case 'b':
			qas_bands = atoi(optarg);
			if (qas_bands < 1)
				qas_bands = 1;
			break;
		case 'r':
			qas_sample_rate = atoi(optarg);
			if (qas_sample_rate < 1)
				qas_sample_rate = 1;
			break;
		case 'l':
			qas_logarithmic = 1;
			break;
		default:
			usage();
			break;
		}
	}

	qas_max_data = (int64_t *)calloc(1,
	    sizeof(qas_max_data[0]) * qas_bands);
	if (qas_max_data == NULL)
		errx(EX_SOFTWARE, "Out of memory");

	qas_phase_data = (int64_t *)calloc(1,
	    sizeof(qas_phase_data[0]) * qas_bands);
	if (qas_phase_data == NULL)
		errx(EX_SOFTWARE, "Out of memory");

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
