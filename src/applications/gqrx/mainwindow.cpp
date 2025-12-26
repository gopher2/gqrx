/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2011-2014 Alexandru Csete OZ9AEC.
 * Copyright (C) 2013 by Elias Oenal <EliasOenal@gmail.com>
 * Copyright 2025 David Kierzkowski K9DPD
 *
 * Gqrx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Gqrx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gqrx; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include <string>
#include <vector>
#include <cmath>
#include <volk/volk.h>

// CPU and disk I/O monitoring
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <libproc.h>
#include <sys/resource.h>
#elif defined(__linux__)
#include <fstream>
#endif
#include <unistd.h>

#include <QSettings>
#include <QByteArray>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QGroupBox>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResource>
#include <QShortcut>
#include <QString>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QSvgWidget>
#include "qtgui/ioconfig.h"
#include "qtgui/multi_tuner_plotter.h"
#include "qtgui/tuner_list.h"
#include "mainwindow.h"
#include "filename_template.h"
#include "qtgui/dxc_options.h"
#include "qtgui/dxc_spots.h"

/* Qt Designer files */
#include "ui_mainwindow.h"

/* DSP */
#include "receiver.h"
#include "remote_control_settings.h"
#include "backends/receiver_backend_factory.h"

#include "qtgui/bookmarkstaglist.h"
#include "qtgui/bandplan.h"

MainWindow::MainWindow(const QString& cfgfile, bool edit_conf, QWidget *parent) :
    QMainWindow(parent),
    configOk(true),
    ui(new Ui::MainWindow),
    d_lnb_lo(0),
    d_hw_freq(0),
    d_fftAvg(0.25),
    d_fftWindowType(0),
    d_fftNormalizeEnergy(false),
    d_have_audio(true),
    dec_afsk1200(nullptr),
    d_prev_cpu_user(0),
    d_prev_cpu_system(0),
    d_prev_cpu_idle(0),
    d_cpu_usage(0.0f),
    d_prev_disk_read(0),
    d_prev_disk_write(0),
    d_last_diskio_time_ms(0),
    d_disk_read_rate(0.0f),
    d_disk_write_rate(0.0f),
    d_main_gain_linear(1.0f),
    center_zoom_state(0),
    center_zoom_tuner_id(-1),
    center_zoom_original_span(0)
{
    ui->setupUi(this);
    BandPlan::create();
    Bookmarks::create();
    DXCSpots::create();

    /* Initialise default configuration directory */
    QByteArray xdg_dir = qgetenv("XDG_CONFIG_HOME");
    if (xdg_dir.isEmpty())
    {
        // Qt takes care of conversion to native separators
        m_cfg_dir = QString("%1/.config/gqrx").arg(QDir::homePath());
    }
    else
    {
        m_cfg_dir = QString("%1/gqrx").arg(xdg_dir.data());
    }

    setWindowTitle(QString("Gqrx %1").arg(VERSION));

    // Set fixed widths for labels so they don't move around when set
    QFontMetrics metrics(font);
    QRect markerRect = metrics.boundingRect("99,999,999.999 kHz");
    ui->markerLabelA->setFixedWidth(markerRect.width());
    ui->markerLabelB->setFixedWidth(markerRect.width());
    QRect deltaFreqRect = metrics.boundingRect("Δ99,999,999.999 kHz   ⨏99,999,999.999 kHz");
    ui->deltaFreqLabel->setFixedWidth(deltaFreqRect.width());
    setMarkerA(MARKER_OFF);
    setMarkerB(MARKER_OFF);
    d_show_markers = true;

    /* frequency control widget */
    ui->freqCtrl->setup(0, 0, 9999e6, 1, FCTL_UNIT_HZ);
    ui->freqCtrl->setFrequency(144500000);
    // Style to match SPAN/L/R labels: #fc9 (light orange)
    ui->freqCtrl->setDigitColor(QColor(0xFF, 0xCC, 0x99));      // Light orange for digits
    ui->freqCtrl->setBgColor(QColor(0x3A, 0x3A, 0x3A));         // Gray background per digit
    ui->freqCtrl->setUnitsColor(QColor(0xFF, 0xCC, 0x99));      // Light orange for "Hz"
    ui->freqCtrl->setHighlightColor(QColor(0x5A, 0x5A, 0x5A));  // Slightly lighter for hover

    d_filter_shape = receiver::FILTER_SHAPE_NORMAL;

    /* create receiver object */
    rx = new receiver("", "", 1);

    /* create multi-tuner manager */
    tuner_manager = std::make_shared<TunerManager>();

    /* create tuner list dock widget */
    tuner_list_widget = new TunerList(this);
    tuner_list_widget->set_tuner_manager(tuner_manager.get());
    uiDockTunerList = new QDockWidget("Tuner Manager", this);
    uiDockTunerList->setObjectName("DockTunerList");
    uiDockTunerList->setWidget(tuner_list_widget);
    connect(tuner_list_widget, &TunerList::tuner_add_requested, this, &MainWindow::addTuner);
    connect(tuner_list_widget, &TunerList::tuner_add_requested_with_type, this, &MainWindow::addTunerWithType);
    connect(tuner_list_widget, &TunerList::tuner_remove_requested, this, &MainWindow::onTunerRemoved);
    connect(tuner_list_widget, &TunerList::tuner_type_changed, this, &MainWindow::onTunerTypeChanged);
    connect(tuner_list_widget, &TunerList::tuner_enabled_changed, this, &MainWindow::onTunerEnabledChanged);
    connect(tuner_list_widget, &TunerList::tuner_name_changed, this, &MainWindow::onTunerNameChanged);
    connect(tuner_list_widget, &TunerList::tuner_color_changed, this, &MainWindow::onTunerColorChanged);
    connect(tuner_list_widget, &TunerList::tuner_alpha_changed, this, &MainWindow::onTunerAlphaChanged);
    connect(tuner_list_widget, &TunerList::tuner_volume_changed, this, &MainWindow::onTunerVolumeChanged);
    connect(tuner_list_widget, &TunerList::tuner_mute_toggled, this, &MainWindow::onTunerMuteToggled);
    connect(tuner_list_widget, &TunerList::tuner_recording_toggled, this, &MainWindow::onTunerRecordingToggled);
    connect(tuner_list_widget, &TunerList::tuner_center_requested, this, &MainWindow::onTunerCenterRequested);
    connect(tuner_list_widget, &TunerList::tuner_zoom_requested, this, &MainWindow::onTunerZoomRequested);
    connect(tuner_list_widget, &TunerList::tuner_frequency_changed, this, &MainWindow::onTunerFrequencyChanged);
    connect(tuner_list_widget, &TunerList::tuner_filter_width_changed, this, &MainWindow::onTunerFilterWidthChanged);
    connect(tuner_list_widget, &TunerList::tuner_squelch_changed, this, &MainWindow::onTunerSquelchChanged);
    connect(tuner_list_widget, &TunerList::tuner_auto_squelch_requested, this, &MainWindow::onTunerAutoSquelchRequested);
    connect(tuner_list_widget, &TunerList::tuner_filter_preset_changed, this, &MainWindow::onTunerFilterPresetChanged);
    connect(tuner_list_widget, &TunerList::tuner_agc_preset_changed, this, &MainWindow::onTunerAgcPresetChanged);
    connect(tuner_list_widget, &TunerList::tuner_nb_state_changed, this, &MainWindow::onTunerNbStateChanged);
    connect(tuner_list_widget, &TunerList::tuner_filter_shape_changed, this, &MainWindow::onTunerFilterShapeChanged);
    connect(tuner_list_widget, &TunerList::tuner_nb1_threshold_changed, this, &MainWindow::onTunerNb1ThresholdChanged);
    connect(tuner_list_widget, &TunerList::tuner_nb2_threshold_changed, this, &MainWindow::onTunerNb2ThresholdChanged);
    connect(tuner_list_widget, &TunerList::tuner_agc_hang_changed, this, &MainWindow::onTunerAgcHangChanged);
    connect(tuner_list_widget, &TunerList::tuner_agc_threshold_changed, this, &MainWindow::onTunerAgcThresholdChanged);
    connect(tuner_list_widget, &TunerList::tuner_agc_decay_changed, this, &MainWindow::onTunerAgcDecayChanged);
    connect(tuner_list_widget, &TunerList::tuner_agc_gain_changed, this, &MainWindow::onTunerAgcGainChanged);
    if (tuner_manager)
        tuner_manager->set_rf_freq(144500000.0);

    if (tuner_manager) {
        TunerList* list_widget = tuner_list_widget;

        tuner_manager->on_channel_created([list_widget](channel_id id) {
            if (list_widget) {
                QMetaObject::invokeMethod(list_widget, "refresh_tuner_list", Qt::QueuedConnection);
            }
        });

        tuner_manager->on_channel_destroyed([list_widget](channel_id id) {
            if (list_widget) {
                QMetaObject::invokeMethod(list_widget, "refresh_tuner_list", Qt::QueuedConnection);
            }
        });

    }

    // remote controller
    remote = new RemoteControl();

    /* meter timer */
    meter_timer = new QTimer(this);
    connect(meter_timer, SIGNAL(timeout()), this, SLOT(meterTimeout()));

    /* FFT timer & data */
    d_iqFftData.resize(receiver::DEFAULT_FFT_SIZE);
    iq_fft_timer = new QTimer(this);
    iq_fft_timer->setTimerType(Qt::PreciseTimer);
    connect(iq_fft_timer, SIGNAL(timeout()), this, SLOT(iqFftTimeout()));
    d_last_fft_ms = 0;
    d_avg_fft_rate = 0.0;
    d_frame_drop = false;

    d_audioFftData.resize(receiver::DEFAULT_FFT_SIZE);
    audio_fft_timer = new QTimer(this);
    connect(audio_fft_timer, SIGNAL(timeout()), this, SLOT(audioFftTimeout()));

    /* timer for data decoders */
    dec_timer = new QTimer(this);
    connect(dec_timer, SIGNAL(timeout()), this, SLOT(decoderTimeout()));

    // create I/Q tool widget
    iq_tool = new CIqTool(this);

    // create DXC Objects
    dxc_options = new DXCOptions(this);

    /* create dock widgets */
    uiDockRxOpt = new DockRxOpt();
    uiDockRxOpt->setTunerManager(tuner_manager.get());
    uiDockRxOpt->hide();  // Per-tuner docks used instead
    uiDockRDS = new DockRDS();
    uiDockAudio = new DockAudio();
    uiDockInputCtl = new DockInputCtl();
    uiDockFft = new DockFft();
    BandPlan::Get().setConfigDir(m_cfg_dir);
    Bookmarks::Get().setConfigDir(m_cfg_dir);
    BandPlan::Get().load();
    uiDockBookmarks = new DockBookmarks(this);

    remote->setTunerManager(tuner_manager);

    // setup some toggle view shortcuts
    uiDockInputCtl->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    uiDockFft->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    uiDockAudio->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    uiDockBookmarks->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    ui->mainToolBar->toggleViewAction()->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));

    /* frequency setting shortcut */
    auto *freq_shortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    QObject::connect(freq_shortcut, &QShortcut::activated, this, &MainWindow::frequencyFocusShortcut);

    // zero cursor (rx filter offset)
    auto *rx_offset_zero_shortcut = new QShortcut(QKeySequence(Qt::Key_Z), this);
    QObject::connect(rx_offset_zero_shortcut, &QShortcut::activated, this, &MainWindow::rxOffsetZeroShortcut);
    // toggle markers on/off
    auto *toggle_markers_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    QObject::connect(toggle_markers_shortcut, &QShortcut::activated, this, &MainWindow::toggleMarkers);
    // clear waterfall
    auto *clear_waterfall_shortcut = new QShortcut(Qt::Key_Delete, this);
    QObject::connect(clear_waterfall_shortcut, SIGNAL(activated()), ui->plotter, SLOT(clearWaterfall()));

    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    /* Add dock widgets to main window. This should be done even for
       dock widgets that are going to be hidden, otherwise they will
       end up floating in their own top-level window and can not be
       docked to the mainwindow.
    */
    addDockWidget(Qt::RightDockWidgetArea, uiDockInputCtl);
    addDockWidget(Qt::RightDockWidgetArea, uiDockFft);
    tabifyDockWidget(uiDockInputCtl, uiDockFft);
    uiDockFft->raise();

    addDockWidget(Qt::RightDockWidgetArea, uiDockAudio);
    addDockWidget(Qt::RightDockWidgetArea, uiDockRDS);
    tabifyDockWidget(uiDockAudio, uiDockRDS);
    uiDockAudio->raise();

    addDockWidget(Qt::BottomDockWidgetArea, uiDockBookmarks);
    addDockWidget(Qt::LeftDockWidgetArea, uiDockTunerList);

    /* hide docks that we don't want to show initially */
    uiDockBookmarks->hide();
    uiDockRDS->hide();

    /* Add dock widget actions to View menu. By doing it this way all signal/slot
       connections will be established automagially.
    */
    ui->menu_View->addAction(uiDockInputCtl->toggleViewAction());
    ui->menu_View->addAction(uiDockRDS->toggleViewAction());
    ui->menu_View->addAction(uiDockAudio->toggleViewAction());
    ui->menu_View->addAction(uiDockFft->toggleViewAction());
    ui->menu_View->addAction(uiDockBookmarks->toggleViewAction());
    ui->menu_View->addAction(uiDockTunerList->toggleViewAction());
    ui->menu_View->addSeparator();
    ui->menu_View->addAction(ui->mainToolBar->toggleViewAction());
    ui->menu_View->addSeparator();
    ui->menu_View->addAction(ui->actionFullScreen);

    /* connect signals and slots */
    connect(ui->freqCtrl, SIGNAL(newFrequency(qint64)), this, SLOT(setNewFrequency(qint64)));
    connect(ui->freqCtrl, SIGNAL(newFrequency(qint64)), remote, SLOT(setNewFrequency(qint64)));
    connect(ui->freqCtrl, SIGNAL(newFrequency(qint64)), uiDockAudio, SLOT(setRxFrequency(qint64)));
    connect(ui->freqCtrl, SIGNAL(newFrequency(qint64)), uiDockRxOpt, SLOT(setRxFreq(qint64)));
    connect(uiDockInputCtl, SIGNAL(lnbLoChanged(double)), this, SLOT(setLnbLo(double)));
    connect(uiDockInputCtl, SIGNAL(lnbLoChanged(double)), remote, SLOT(setLnbLo(double)));
    connect(uiDockInputCtl, SIGNAL(gainChanged(QString, double)), this, SLOT(setGain(QString,double)));
    connect(uiDockInputCtl, SIGNAL(gainChanged(QString, double)), remote, SLOT(setGain(QString,double)));
    connect(uiDockInputCtl, SIGNAL(autoGainChanged(bool)), this, SLOT(setAutoGain(bool)));
    connect(uiDockInputCtl, SIGNAL(freqCorrChanged(double)), this, SLOT(setFreqCorr(double)));
    connect(uiDockInputCtl, SIGNAL(iqSwapChanged(bool)), this, SLOT(setIqSwap(bool)));
    connect(uiDockInputCtl, SIGNAL(dcCancelChanged(bool)), this, SLOT(setDcCancel(bool)));
    connect(uiDockInputCtl, SIGNAL(iqBalanceChanged(bool)), this, SLOT(setIqBalance(bool)));
    connect(uiDockInputCtl, SIGNAL(ignoreLimitsChanged(bool)), this, SLOT(setIgnoreLimits(bool)));
    connect(uiDockInputCtl, SIGNAL(antennaSelected(QString)), this, SLOT(setAntenna(QString)));
    connect(uiDockInputCtl, SIGNAL(freqCtrlResetChanged(bool)), this, SLOT(setFreqCtrlReset(bool)));
    connect(uiDockInputCtl, SIGNAL(invertScrollingChanged(bool)), this, SLOT(setInvertScrolling(bool)));
    connect(uiDockRxOpt, SIGNAL(rxFreqChanged(qint64)), ui->freqCtrl, SLOT(setFrequency(qint64)));
    connect(uiDockRxOpt, SIGNAL(filterOffsetChanged(qint64)), this, SLOT(setFilterOffset(qint64)));
    connect(uiDockRxOpt, SIGNAL(filterOffsetChanged(qint64)), remote, SLOT(setFilterOffset(qint64)));
    connect(uiDockRxOpt, SIGNAL(demodSelected(int)), this, SLOT(selectDemod(int)));
    connect(uiDockRxOpt, SIGNAL(demodSelected(int)), remote, SLOT(setMode(int)));
    connect(uiDockRxOpt, SIGNAL(fmMaxdevSelected(float)), this, SLOT(setFmMaxdev(float)));
    connect(uiDockRxOpt, SIGNAL(fmEmphSelected(double)), this, SLOT(setFmEmph(double)));
    connect(uiDockRxOpt, SIGNAL(amDcrToggled(bool)), this, SLOT(setAmDcr(bool)));
    connect(uiDockRxOpt, SIGNAL(cwOffsetChanged(int)), this, SLOT(setCwOffset(int)));
    connect(uiDockRxOpt, SIGNAL(amSyncDcrToggled(bool)), this, SLOT(setAmSyncDcr(bool)));
    connect(uiDockRxOpt, SIGNAL(amSyncPllBwSelected(float)), this, SLOT(setAmSyncPllBw(float)));
    connect(uiDockRxOpt, SIGNAL(agcToggled(bool)), this, SLOT(setAgcOn(bool)));
    connect(uiDockRxOpt, SIGNAL(agcHangToggled(bool)), this, SLOT(setAgcHang(bool)));
    connect(uiDockRxOpt, SIGNAL(agcThresholdChanged(int)), this, SLOT(setAgcThreshold(int)));
    connect(uiDockRxOpt, SIGNAL(agcSlopeChanged(int)), this, SLOT(setAgcSlope(int)));
    connect(uiDockRxOpt, SIGNAL(agcGainChanged(int)), this, SLOT(setAgcGain(int)));
    connect(uiDockRxOpt, SIGNAL(agcDecayChanged(int)), this, SLOT(setAgcDecay(int)));
    connect(uiDockRxOpt, SIGNAL(noiseBlankerChanged(int,bool,float)), this, SLOT(setNoiseBlanker(int,bool,float)));
    connect(uiDockRxOpt, SIGNAL(sqlLevelChanged(double)), this, SLOT(setSqlLevel(double)));
    connect(uiDockRxOpt, &DockRxOpt::sqlAutoClicked, this, &MainWindow::setSqlLevelAuto);
    connect(uiDockAudio, SIGNAL(audioGainChanged(float)), this, SLOT(setAudioGain(float)));
    connect(uiDockAudio, SIGNAL(audioGainChanged(float)), remote, SLOT(setAudioGain(float)));
    connect(uiDockAudio, SIGNAL(audioStreamingStarted(QString,int,bool)), this, SLOT(startAudioStream(QString,int,bool)));
    connect(uiDockAudio, SIGNAL(audioStreamingStopped()), this, SLOT(stopAudioStreaming()));
    connect(uiDockAudio, SIGNAL(audioRecStarted(QString)), this, SLOT(startAudioRec(QString)));
    connect(uiDockAudio, SIGNAL(audioRecStarted(QString)), remote, SLOT(startAudioRecorder(QString)));
    connect(uiDockAudio, SIGNAL(audioRecStopped()), this, SLOT(stopAudioRec()));
    connect(uiDockAudio, SIGNAL(audioRecStopped()), remote, SLOT(stopAudioRecorder()));
    connect(uiDockAudio, SIGNAL(audioPlayStarted(QString)), this, SLOT(startAudioPlayback(QString)));
    connect(uiDockAudio, SIGNAL(audioPlayStopped()), this, SLOT(stopAudioPlayback()));
    connect(uiDockAudio, SIGNAL(fftRateChanged(int)), this, SLOT(setAudioFftRate(int)));

    // FFT Dock
    connect(uiDockFft, SIGNAL(fftSizeChanged(int)), this, SLOT(setIqFftSize(int)));
    connect(uiDockFft, SIGNAL(fftRateChanged(int)), this, SLOT(setIqFftRate(int)));
    connect(uiDockFft, SIGNAL(fftWindowChanged(int)), this, SLOT(setIqFftWindow(int)));
    connect(uiDockFft, SIGNAL(wfSpanChanged(quint64)), this, SLOT(setWfTimeSpan(quint64)));
    connect(uiDockFft, SIGNAL(fftSplitChanged(int)), this, SLOT(setIqFftSplit(int)));
    connect(uiDockFft, SIGNAL(fftAvgChanged(float)), ui->plotter, SLOT(setFftAvg(float)));
    connect(uiDockFft, SIGNAL(fftZoomChanged(float)), ui->plotter, SLOT(zoomOnXAxis(float)));
    connect(uiDockFft, SIGNAL(waterfallModeChanged(int)), ui->plotter, SLOT(setWaterfallMode(int)));
    connect(uiDockFft, SIGNAL(plotModeChanged(int)), ui->plotter, SLOT(setPlotMode(int)));
    connect(uiDockFft, SIGNAL(plotScaleChanged(int, bool)), ui->plotter, SLOT(setPlotScale(int, bool)));
    connect(uiDockFft, SIGNAL(plotScaleChanged(int, bool)), this, SLOT(plotScaleChanged(int, bool)));
    connect(uiDockFft, SIGNAL(resetFftZoom()), ui->plotter, SLOT(resetHorizontalZoom()));
    connect(uiDockFft, SIGNAL(gotoFftCenter()), ui->plotter, SLOT(moveToCenterFreq()));
    connect(uiDockFft, SIGNAL(gotoDemodFreq()), ui->plotter, SLOT(moveToDemodFreq()));
    connect(uiDockFft, SIGNAL(bandPlanChanged(bool)), ui->plotter, SLOT(enableBandPlan(bool)));
    connect(uiDockFft, SIGNAL(markersChanged(bool)), ui->plotter, SLOT(enableMarkers(bool)));
    connect(uiDockFft, SIGNAL(markersChanged(bool)), this, SLOT(enableMarkers(bool)));
    connect(uiDockFft, SIGNAL(wfColormapChanged(const QString)), ui->plotter, SLOT(setWfColormap(const QString)));
    connect(uiDockFft, SIGNAL(wfColormapChanged(const QString)), uiDockAudio, SLOT(setWfColormap(const QString)));
    connect(uiDockFft, SIGNAL(pandapterRangeChanged(float,float)),
            ui->plotter, SLOT(setPandapterRange(float,float)));
    connect(uiDockFft, SIGNAL(waterfallRangeChanged(float,float)),
            ui->plotter, SLOT(setWaterfallRange(float,float)));
    connect(uiDockFft, SIGNAL(fftColorChanged(QColor)), this, SLOT(setFftColor(QColor)));
    connect(uiDockFft, SIGNAL(fftFillToggled(bool)), this, SLOT(enableFftFill(bool)));
    connect(uiDockFft, SIGNAL(fftMaxHoldToggled(bool)), ui->plotter, SLOT(enableMaxHold(bool)));
    connect(uiDockFft, SIGNAL(fftMinHoldToggled(bool)), ui->plotter, SLOT(enableMinHold(bool)));
    connect(uiDockFft, SIGNAL(peakDetectToggled(bool)), ui->plotter, SLOT(enablePeakDetect(bool)));
    connect(uiDockRDS, SIGNAL(rdsDecoderToggled(bool)), this, SLOT(setRdsDecoder(bool)));

    // Plotter
    connect(ui->plotter, SIGNAL(pandapterRangeChanged(float,float)),
            uiDockFft, SLOT(setPandapterRange(float,float)));
    connect(ui->plotter, SIGNAL(newZoomLevel(float)),
            uiDockFft, SLOT(setZoomLevel(float)));
    connect(ui->plotter, &CPlotter::newZoomLevel, this, [this](float) {
        // Update the span label when zoom level changes
        qint64 span_freq = ui->plotter->getSpan();
        QString span_text;
        if (span_freq >= 1e6) {
            span_text = QString("SPAN: %1 MHz").arg((double)span_freq / 1e6, 0, 'f', 2);
        } else if (span_freq >= 1e3) {
            span_text = QString("SPAN: %1 kHz").arg((double)span_freq / 1e3, 0, 'f', 0);
        } else {
            span_text = QString("SPAN: %1 Hz").arg(span_freq);
        }
        ui->fftSpanLabel->setText(span_text);

        // Update edge frequency labels
        qint64 center_freq = ui->freqCtrl->getFrequency();
        qint64 left_edge = center_freq - span_freq / 2;
        qint64 right_edge = center_freq + span_freq / 2;

        ui->fftLeftEdgeLabel->setText(QString("L: %1 MHz").arg((double)left_edge / 1e6, 0, 'f', 3));
        ui->fftRightEdgeLabel->setText(QString("R: %1 MHz").arg((double)right_edge / 1e6, 0, 'f', 3));
    });
    connect(ui->plotter, SIGNAL(newSize()), this, SLOT(setWfSize()));
    connect(ui->plotter, SIGNAL(markerSelectA(qint64)), this, SLOT(setMarkerA(qint64)));
    connect(ui->plotter, SIGNAL(markerSelectB(qint64)), this, SLOT(setMarkerB(qint64)));
    connect(ui->plotter, SIGNAL(tuneToFrequency(int, qint64)), this, SLOT(onTunerDragged(int, qint64)));
    connect(ui->plotter, SIGNAL(filterResized(int, int, int)), this, SLOT(onFilterResized(int, int, int)));
    connect(ui->plotter, SIGNAL(panSdrFrequency(int,qint64)), this, SLOT(onPanSdrFrequency(int,qint64)));
    connect(ui->plotter, SIGNAL(newCenterFreqRequest(qint64)), this, SLOT(setNewFrequency(qint64)));

    // Bookmarks
    connect(uiDockBookmarks, SIGNAL(newBookmarkActivated(qint64, QString, int)), this, SLOT(onBookmarkActivated(qint64, QString, int)));
    connect(uiDockBookmarks->actionAddBookmark, SIGNAL(triggered()), this, SLOT(on_actionAddBookmark_triggered()));
    connect(&Bookmarks::Get(), SIGNAL(BookmarksChanged()), ui->plotter, SLOT(updateOverlay()));

    //DXC Spots
    connect(&DXCSpots::Get(), SIGNAL(dxcSpotsUpdated()), this, SLOT(updateClusterSpots()));

    // I/Q playback
    connect(iq_tool, SIGNAL(startRecording(QString, QString)), this, SLOT(startIqRecording(QString, QString)));
    connect(iq_tool, SIGNAL(startRecording(QString, QString)), remote, SLOT(startIqRecorder(QString, QString)));
    connect(iq_tool, SIGNAL(stopRecording()), this, SLOT(stopIqRecording()));
    connect(iq_tool, SIGNAL(stopRecording()), remote, SLOT(stopIqRecorder()));
    connect(iq_tool, SIGNAL(startPlayback(QString,float,qint64)), this, SLOT(startIqPlayback(QString,float,qint64)));
    connect(iq_tool, SIGNAL(stopPlayback()), this, SLOT(stopIqPlayback()));
    connect(iq_tool, SIGNAL(seek(qint64)), this,SLOT(seekIqFile(qint64)));

    // remote control
    connect(remote, SIGNAL(newRDSmode(bool)), uiDockRDS, SLOT(setRDSmode(bool)));
    connect(remote, SIGNAL(newFilterOffset(qint64)), this, SLOT(setFilterOffset(qint64)));
    connect(remote, SIGNAL(newFilterOffset(qint64)), uiDockRxOpt, SLOT(setFilterOffset(qint64)));
    connect(remote, SIGNAL(newFrequency(qint64)), ui->freqCtrl, SLOT(setFrequency(qint64)));
    connect(remote, SIGNAL(newLnbLo(double)), uiDockInputCtl, SLOT(setLnbLo(double)));
    connect(remote, SIGNAL(newLnbLo(double)), this, SLOT(setLnbLo(double)));
    connect(remote, SIGNAL(newMode(int)), this, SLOT(selectDemod(int)));
    connect(remote, SIGNAL(newMode(int)), uiDockRxOpt, SLOT(setCurrentDemod(int)));
    connect(remote, SIGNAL(newSquelchLevel(double)), this, SLOT(setSqlLevel(double)));
    connect(remote, SIGNAL(newSquelchLevel(double)), uiDockRxOpt, SLOT(setSquelchLevel(double)));
    connect(remote, SIGNAL(newAudioGain(float)), uiDockAudio, SLOT(setAudioGainDb(float)));
    connect(uiDockRxOpt, SIGNAL(sqlLevelChanged(double)), remote, SLOT(setSquelchLevel(double)));
    connect(remote, SIGNAL(startAudioRecorderEvent()), uiDockAudio, SLOT(startAudioRecorder()));
    connect(remote, SIGNAL(stopAudioRecorderEvent()), uiDockAudio, SLOT(stopAudioRecorder()));
    connect(remote, SIGNAL(startIqRecorderEvent()), iq_tool, SLOT(startIqRecorder()));
    connect(remote, SIGNAL(stopIqRecorderEvent()), iq_tool, SLOT(stopIqRecorder()));
    connect(ui->plotter, SIGNAL(newFilterFreq(int, int)), remote, SLOT(setPassband(int, int)));
    connect(remote, SIGNAL(newPassband(int)), this, SLOT(setPassband(int)));
    connect(remote, SIGNAL(gainChanged(QString, double)), uiDockInputCtl, SLOT(setGain(QString,double)));
    connect(remote, SIGNAL(dspChanged(bool)), this, SLOT(on_actionDSP_triggered(bool)));
    connect(uiDockRDS, SIGNAL(rdsPI(QString)), remote, SLOT(rdsPI(QString)));
    connect(uiDockRDS, SIGNAL(stationChanged(QString)), remote, SLOT(setRdsStation(QString)));
    connect(uiDockRDS, SIGNAL(radiotextChanged(QString)), remote, SLOT(setRdsRadiotext(QString)));
    connect(remote, SIGNAL(newAudioMuted(bool)), uiDockAudio, SLOT(setAudioMuted(bool)));
    connect(uiDockAudio, SIGNAL(audioMuted(bool)), remote, SLOT(setAudioMuted(bool)));

    rds_timer = new QTimer(this);
    connect(rds_timer, SIGNAL(timeout()), this, SLOT(rdsTimeout()));

    // enable frequency tooltips on FFT plot
    ui->plotter->setTooltipsEnabled(true);

    // Create list of input devices. This must be done before the configuration is
    // restored because device probing might change the device configuration
    CIoConfig::getDeviceList(devList);

    m_recent_config = new RecentConfig(m_cfg_dir, ui->menu_RecentConfig);
    connect(m_recent_config, SIGNAL(loadConfig(const QString &)), this, SLOT(loadConfigSlot(const QString &)));

    // restore last session
    if (!loadConfig(cfgfile, true, true))
    {

      // first time config
        qDebug() << "Launching I/O device editor";
        if (firstTimeConfig() != QDialog::Accepted)
        {
            qDebug() << "I/O device configuration cancelled.";
            configOk = false;
        }
        else
        {
            configOk = true;
        }
    }
    else if (edit_conf)
    {
        qDebug() << "Launching I/O device editor";
        if (on_actionIoConfig_triggered() != QDialog::Accepted)
        {
            qDebug() << "I/O device configuration cancelled.";
            configOk = false;
        }
        else
        {
            configOk = true;
        }
    }

    // Load saved tuner list (shows tuners before DSP starts)
    if (tuner_list_widget && m_settings) {
        tuner_list_widget->load_from_settings(m_settings);
    }

    qsvg_dummy = new QSvgWidget();
}

MainWindow::~MainWindow()
{
    on_actionDSP_triggered(false);

    /* stop and delete timers */
    dec_timer->stop();
    delete dec_timer;

    meter_timer->stop();
    delete meter_timer;

    iq_fft_timer->stop();
    delete iq_fft_timer;

    audio_fft_timer->stop();
    delete audio_fft_timer;

    if (m_settings)
    {
        m_settings->setValue("configversion", 4);
        m_settings->setValue("crashed", false);

        // hide toolbar (default=false)
        if (ui->mainToolBar->isHidden())
            m_settings->setValue("gui/hide_toolbar", true);
        else
            m_settings->remove("gui/hide_toolbar");

        m_settings->setValue("gui/geometry", saveGeometry());
        m_settings->setValue("gui/state", saveState());

        // save session
        storeSession();

        m_settings->sync();
        delete m_settings;
    }

    delete m_recent_config;

    delete iq_tool;
    delete dxc_options;
    delete ui;
    delete uiDockRxOpt;
    delete uiDockAudio;
    delete uiDockBookmarks;
    delete uiDockFft;
    delete uiDockInputCtl;
    delete uiDockRDS;
    delete rx;
    delete remote;
    delete qsvg_dummy;
}

/**
 * Load new configuration.
 * @param cfgfile
 * @returns True if config is OK, False if not (e.g. no input device specified).
 *
 * If cfgfile is an absolute path it will be used as is, otherwise it is assumed
 * to be the name of a file under m_cfg_dir.
 *
 * If cfgfile does not exist it will be created.
 *
 * If no input device is specified, we return false to signal that the I/O
 * configuration dialog should be run.
 *
 * FIXME: Refactor.
 */
bool MainWindow::loadConfig(const QString& cfgfile, bool check_crash,
                            bool restore_mainwindow)
{

    double      actual_rate = 0.0;
    qint64      int64_val;
    int         int_val;
    bool        bool_val;
    bool        conf_ok = false;
    bool        conv_ok;
    bool        skip_loading_cfg = false;

    qDebug() << "Loading configuration from:" << cfgfile;

    if (m_settings)
    {
        // set current config to not crashed before loading new config
        m_settings->setValue("crashed", false);
        m_settings->sync();
        delete m_settings;
    }

    if (QDir::isAbsolutePath(cfgfile))
        m_settings = new QSettings(cfgfile, QSettings::IniFormat);
    else
        m_settings = new QSettings(QString("%1/%2").arg(m_cfg_dir).arg(cfgfile),
                                   QSettings::IniFormat);

    qDebug() << "Configuration file:" << m_settings->fileName();

    // Warn user early if config file exists but is not writable (e.g., wrong
    // ownership after running as root). QSettings silently fails to save,
    // leaving users confused about why settings don't persist.
    QFileInfo configFileInfo(m_settings->fileName());
    if (configFileInfo.exists() && !configFileInfo.isWritable())
    {
        QMessageBox::warning(this, tr("Configuration File Permission Error"),
            tr("<p>The configuration file is not writable:</p>"
               "<p><code>%1</code></p>"
               "<p>Settings changes will not be saved. "
               "Please check file ownership and permissions.</p>")
            .arg(m_settings->fileName()));
    }

    if (check_crash)
    {
        if (m_settings->value("crashed", false).toBool())
        {
            qDebug() << "Crash guard triggered!";
            auto* askUserAboutConfig =
                    new QMessageBox(QMessageBox::Warning, tr("Crash Detected!"),
                                    tr("<p>Gqrx has detected problems with the current configuration. "
                                       "Loading the configuration again could cause the application to crash.</p>"
                                       "<p>Do you want to edit the settings?</p>"),
                                    QMessageBox::Yes | QMessageBox::No);
            askUserAboutConfig->setDefaultButton(QMessageBox::Yes);
            askUserAboutConfig->setTextFormat(Qt::RichText);
            askUserAboutConfig->exec();
            if (askUserAboutConfig->result() == QMessageBox::Yes)
                skip_loading_cfg = true;

            delete askUserAboutConfig;
        }
        else
        {
            m_settings->setValue("crashed", true); // clean exit will set this to FALSE
            m_settings->sync();
        }
    }

    if (skip_loading_cfg)
        return false;

    // manual reconf (FIXME: check status)
    conv_ok = false;

    // hide toolbar
    bool_val = m_settings->value("gui/hide_toolbar", false).toBool();
    if (bool_val)
        ui->mainToolBar->hide();

    // main window settings
    if (restore_mainwindow)
    {
        restoreGeometry(m_settings->value("gui/geometry",
                                          saveGeometry()).toByteArray());
        restoreState(m_settings->value("gui/state", saveState()).toByteArray());
    }

    QString indev = m_settings->value("input/device", "").toString();
    if (!indev.isEmpty() && tuner_manager)
    {
        try
        {
            auto status = tuner_manager->set_input_device(indev.toStdString());
            if (status == TunerManager::STATUS_OK) {
                conf_ok = true;
            } else {
                throw std::runtime_error("TunerManager::set_input_device failed");
            }
        }
        catch (std::runtime_error &x)
        {
            QMessageBox::warning(nullptr,
                             QObject::tr("Failed to set input device"),
                             QObject::tr("<p><b>%1</b></p>"
                                         "Please select another device.")
                                     .arg(x.what()),
                             QMessageBox::Ok);
        }

        // Update window title
        setWindowTitle(QString("Gqrx %1 - %2").arg(VERSION).arg(indev));

        // Add available antenna connectors to the UI
        std::vector<std::string> antennas = tuner_manager->get_antennas();
        uiDockInputCtl->setAntennas(antennas);

        // Update gain stages.
        if (indev.contains("rtl", Qt::CaseInsensitive)
                && !m_settings->contains("input/gains"))
        {
            /* rtlsdr gain is 0 by default making users think their device is
             * deaf. Therefore, we don't read gain from the device, but initialize
             * it to the midpoint.
             */
            updateGainStages(false);
        }
        else
            updateGainStages(true);
    }

    QString outdev = m_settings->value("output/device", "").toString();

    try {
        rx->set_output_device(outdev.toStdString());
    } catch (std::exception &x) {
        QMessageBox::warning(nullptr,
                         QObject::tr("Failed to set output device"),
                         QObject::tr("<p><b>%1</b></p>"
                                     "Please select another device.")
                                 .arg(x.what()),
                         QMessageBox::Ok);
    }

    int_val = m_settings->value("input/sample_rate", 0).toInt(&conv_ok);
    if (conv_ok && (int_val > 0) && tuner_manager)
    {
        tuner_manager->set_input_rate(int_val);
        actual_rate = tuner_manager->get_input_rate();

        if (actual_rate == 0)
        {
            // There is an error with the device (perhaps not attached)
            // Warn user and use 100 ksps (rate used by gr-osmocom null_source)
            auto *dialog =
                    new QMessageBox(QMessageBox::Warning, tr("Device Error"),
                                    tr("There was an error configuring the input device.\n"
                                       "Please make sure that a supported device is attached "
                                       "to the computer and restart gqrx."),
                                    QMessageBox::Ok);
            dialog->setModal(true);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();

            actual_rate = int_val;
        }

        qDebug() << "Requested sample rate:" << int_val;
        qDebug() << "Actual sample rate   :" << QString("%1").arg(actual_rate, 0, 'f', 6);
    }
    else if (tuner_manager)
    {
        actual_rate = tuner_manager->get_input_rate();
    }

    if (actual_rate > 0. && tuner_manager)
    {
        int_val = m_settings->value("input/decimation", 1).toInt(&conv_ok);
        if (conv_ok && int_val >= 2)
        {
            tuner_manager->set_input_decim(int_val);
            unsigned int actual_decim = tuner_manager->get_input_decim();
            if (actual_decim != (unsigned int)int_val)
            {
                qDebug() << "Failed to set decimation" << int_val;
            }
            else
            {
                // update actual rate
                actual_rate /= (double)int_val;
                qDebug() << "Input decimation:" << int_val;
                qDebug() << "Quadrature rate:" << QString("%1").arg(actual_rate, 0, 'f', 6);
            }
        }
        else
        {
            tuner_manager->set_input_decim(1);
        }

        // update various widgets that need a sample rate
        uiDockRxOpt->setFilterOffsetRange((qint64)(actual_rate));
        uiDockFft->setSampleRate(actual_rate);
        ui->plotter->setSampleRate(actual_rate);
        ui->plotter->setSpanFreq((quint32)actual_rate);
        remote->setBandwidth((qint64)actual_rate);
        iq_tool->setSampleRate((qint64)actual_rate);
        updateSourceStatusLabels();
    }
    else
    {
        qDebug() << "Error: Actual sample rate is" << actual_rate;
    }

    int64_val = m_settings->value("input/bandwidth", 0).toInt(&conv_ok);
    if (conv_ok)
    {
        // set analog bw even if 0 since for some devices 0 Hz means "auto"
        double actual_bw = rx->set_analog_bandwidth((double) int64_val);
        qDebug() << "Requested bandwidth:" << int64_val << "Hz";
        qDebug() << "Actual bandwidth   :" << actual_bw << "Hz";
    }

    uiDockInputCtl->readSettings(m_settings); // this will also update freq range
    uiDockRxOpt->readSettings(m_settings);
    uiDockFft->readSettings(m_settings);
    uiDockAudio->readSettings(m_settings);
    dxc_options->readSettings(m_settings);

    {
        int64_val = m_settings->value("input/frequency", 14236000).toLongLong(&conv_ok);

        // If frequency is out of range set frequency to the center of the range.
        qint64 hw_freq = int64_val - d_lnb_lo - (qint64)(rx->get_filter_offset());
        if (hw_freq < d_hw_freq_start || hw_freq > d_hw_freq_stop)
        {
            int64_val = (d_hw_freq_stop - d_hw_freq_start) / 2 +
                        (qint64)(rx->get_filter_offset()) + d_lnb_lo;
        }

        ui->freqCtrl->setFrequency(int64_val);
        setNewFrequency(ui->freqCtrl->getFrequency()); // ensure all GUI and RF is updated
    }

    {
        // Center frequency for FFT plotter
        int64_val = m_settings->value("fft/fft_center", 0).toLongLong(&conv_ok);

        if (conv_ok) {
            ui->plotter->setFftCenterFreq(int64_val);
        }
    }

    {
        int flo = m_settings->value("receiver/filter_low_cut", 0).toInt(&conv_ok);
        int fhi = m_settings->value("receiver/filter_high_cut", 0).toInt(&conv_ok);

        if (conv_ok && uiDockRxOpt->currentDemod() != DockRxOpt::MODE_OFF && flo != fhi)
        {
            on_plotter_newFilterFreq(flo, fhi);
        }
    }

    iq_tool->readSettings(m_settings);

    /*
     * Initialization the remote control at the end.
     * We must be sure that all variables initialized before starting RC server.
     */
    remote->readSettings(m_settings);
    bool_val = m_settings->value("remote_control/enabled", false).toBool();
    if (bool_val)
    {
       remote->start_server();
       ui->actionRemoteControl->setChecked(true);
    }

    emit m_recent_config->configLoaded(m_settings->fileName());

    return conf_ok;
}

/**
 * @brief Save current configuration to a file.
 * @param cfgfile
 * @returns True if the operation was successful.
 *
 * If cfgfile is an absolute path it will be used as is, otherwise it is
 * assumed to be the name of a file under m_cfg_dir.
 *
 * If cfgfile already exists it will be overwritten (we assume that a file
 * selection dialog has already asked for confirmation of overwrite).
 *
 * Since QSettings does not support "save as" we do this by copying the current
 * settings to a new file.
 */
bool MainWindow::saveConfig(const QString& cfgfile)
{
    QString oldfile = m_settings->fileName();
    QString newfile;

    qDebug() << "Saving configuration to:" << cfgfile;

    m_settings->sync();

    if (QDir::isAbsolutePath(cfgfile))
        newfile = cfgfile;
    else
        newfile = QString("%1/%2").arg(m_cfg_dir).arg(cfgfile);

    if (newfile == oldfile) {
        qDebug() << "New file is equal to old file => SYNCING...";
        emit m_recent_config->configSaved(newfile);
        return true;
    }

    if (QFile::exists(newfile))
    {
        qDebug() << "File" << newfile << "already exists => DELETING...";
        if (QFile::remove(newfile))
            qDebug() << "Deleted" << newfile;
        else
            qDebug() << "Failed to delete" << newfile;
    }
    if (QFile::copy(oldfile, newfile))
    {
        loadConfig(cfgfile, false, false);
        return true;
    }
    else
    {
        qDebug() << "Error saving configuration to" << newfile;
        return false;
    }
}

/**
 * Store session-related parameters (frequency, gain,...)
 *
 * This needs to be called when we switch input source, otherwise the
 * new source would use the parameters stored on last exit.
 */
void MainWindow::storeSession()
{
    if (m_settings)
    {
        m_settings->setValue("input/frequency", ui->freqCtrl->getFrequency());
        m_settings->setValue("fft/fft_center", ui->plotter->getFftCenterFreq());

        uiDockInputCtl->saveSettings(m_settings);
        uiDockRxOpt->saveSettings(m_settings);
        uiDockFft->saveSettings(m_settings);
        uiDockAudio->saveSettings(m_settings);

        remote->saveSettings(m_settings);
        iq_tool->saveSettings(m_settings);
        dxc_options->saveSettings(m_settings);

        {
            int     flo, fhi;
            ui->plotter->getHiLowCutFrequencies(&flo, &fhi);
            if (flo != fhi)
            {
                m_settings->setValue("receiver/filter_low_cut", flo);
                m_settings->setValue("receiver/filter_high_cut", fhi);
            }
        }

        // Save tuner state so we can restore on next launch
        if (tuner_manager) {
            std::vector<channel_id> tuner_ids = tuner_manager->get_all_channels();
            m_settings->setValue("tuner/count", (int)tuner_ids.size());

            // Clear old tuner settings
            m_settings->beginGroup("tuners");
            m_settings->remove("");  // Remove all keys in this group
            m_settings->endGroup();

            // Save each tuner's state
            for (size_t i = 0; i < tuner_ids.size(); i++) {
                channel_id id = tuner_ids[i];
                ReceiverChannel* tuner = tuner_manager->get_channel_impl(id);
                if (tuner) {
                    QString prefix = QString("tuners/%1/").arg(i);

                    // Basic tuner settings
                    m_settings->setValue(prefix + "name", QString::fromStdString(tuner->get_channel_name()));
                    m_settings->setValue(prefix + "freq_offset", tuner->get_center_freq());
                    m_settings->setValue(prefix + "demod", (int)tuner->get_demod());
                    m_settings->setValue(prefix + "receiver_type", static_cast<int>(tuner->get_backend_type()));
                    m_settings->setValue(prefix + "squelch", tuner->get_sql_level());
                    m_settings->setValue(prefix + "audio_gain", tuner->get_audio_gain());
                    m_settings->setValue(prefix + "filter_offset", tuner->get_filter_offset());
                    m_settings->setValue(prefix + "enabled", tuner->is_enabled());

                    // Save filter bounds from the plotter marker
                    MultiTunerPlotter::TunerMarker marker = ui->plotter->getTunerMarker(id);
                    if (marker.tuner_id >= 0) {
                        m_settings->setValue(prefix + "filter_low", marker.filter_low);
                        m_settings->setValue(prefix + "filter_high", marker.filter_high);
                    }

                    // Save color, volume, and mute state from tuner list widget
                    if (tuner_list_widget) {
                        for (auto* child : tuner_list_widget->findChildren<TunerRowWidget*>()) {
                            if (child->tuner_id() == id) {
                                m_settings->setValue(prefix + "color", child->color().name());
                                m_settings->setValue(prefix + "volume", child->volume());
                                m_settings->setValue(prefix + "muted", child->isMuted());
                                break;
                            }
                        }
                    }
                }
            }

            // Save active tuner index
            channel_id active_id = tuner_manager->get_active_channel();
            for (size_t i = 0; i < tuner_ids.size(); i++) {
                if (tuner_ids[i] == active_id) {
                    m_settings->setValue("tuner/active_index", (int)i);
                    break;
                }
            }
        }
    }
}

/**
 * @brief Update hardware RF frequency range.
 * @param ignore_limits Whether ignore the hardware specd and allow DC-to-light
 *                      range.
 *
 * This function fetches the frequency range of the receiver. Useful when we
 * read a new configuration with a new input device or when the ignore_limits
 * setting is changed.
 */
void MainWindow::updateHWFrequencyRange(bool ignore_limits)
{
    double startd, stopd, stepd;

    if (ignore_limits)
    {
        d_hw_freq_start = (quint64) 0;
        d_hw_freq_stop  = (quint64) 9999e6;
    }
    else if (rx->get_rf_range(&startd, &stopd, &stepd) == receiver::STATUS_OK)
    {
        d_hw_freq_start = (quint64) startd;
        d_hw_freq_stop  = (quint64) stopd;
    }
    else
    {
        qDebug() << __func__ << "failed fetching new hardware frequency range";
        d_hw_freq_start = (quint64) 0;
        d_hw_freq_stop  = (quint64) 9999e6;
    }

    updateFrequencyRange(); // Also update the available frequency range
}

/**
 * @brief Update available frequency range.
 *
 * This function sets the available frequency range based on the hardware
 * frequency range, the selected filter offset and the LNB LO.
 *
 * This function must therefore be called whenever the LNB LO or the filter
 * offset has changed.
 */
void MainWindow::updateFrequencyRange()
{
    auto start = (qint64)(rx->get_filter_offset()) + d_hw_freq_start + d_lnb_lo;
    auto stop  = (qint64)(rx->get_filter_offset()) + d_hw_freq_stop  + d_lnb_lo;

    ui->freqCtrl->setup(0, start, stop, 1, FCTL_UNIT_HZ);
    uiDockRxOpt->setRxFreqRange(start, stop);
}

/**
 * @brief Update source status labels (source type, sample rate, decimation, bandwidth, span, LNB LO).
 */
void MainWindow::updateSourceStatusLabels()
{

    if (!tuner_manager) {
        ui->sourceStatusLabel->setText("INPUT: None");
        ui->sampleRateLabel->setText("RATE: 0.00 MSPS");
        ui->decimBwLabel->setText("BW: 0.00 MHz");
        ui->fftSpanLabel->setText("SPAN: 0.00 MHz");
        ui->lnbLoLabel->setText("");
        return;
    }

    // Get the input device string to determine source type
    QString device_str = QString::fromStdString(tuner_manager->get_input_device());
    QString source_type;

    if (device_str.contains("file=", Qt::CaseInsensitive)) {
        source_type = "IQ File";
    } else if (device_str.contains("uhd", Qt::CaseInsensitive) ||
               device_str.contains("usrp", Qt::CaseInsensitive)) {
        source_type = "USRP";
    } else if (device_str.contains("rtl", Qt::CaseInsensitive)) {
        source_type = "RTL-SDR";
    } else if (device_str.contains("soapy", Qt::CaseInsensitive)) {
        source_type = "SoapySDR";
    } else if (device_str.contains("hackrf", Qt::CaseInsensitive)) {
        source_type = "HackRF";
    } else if (device_str.contains("airspy", Qt::CaseInsensitive)) {
        source_type = "Airspy";
    } else if (device_str.contains("bladerf", Qt::CaseInsensitive)) {
        source_type = "BladeRF";
    } else if (device_str.isEmpty()) {
        source_type = "None";
    } else {
        source_type = "SDR";
    }

    ui->sourceStatusLabel->setText(QString("INPUT: %1").arg(source_type));

    // Get sample rate (raw input from SDR)
    double sample_rate = tuner_manager->get_input_rate();
    QString rate_text;
    if (sample_rate >= 1e6) {
        rate_text = QString("RATE: %1 MSPS").arg(sample_rate / 1e6, 0, 'f', 2);
    } else if (sample_rate >= 1e3) {
        rate_text = QString("RATE: %1 kSPS").arg(sample_rate / 1e3, 0, 'f', 0);
    } else {
        rate_text = QString("RATE: %1 SPS").arg(sample_rate, 0, 'f', 0);
    }
    ui->sampleRateLabel->setText(rate_text);

    // Get decimation and effective bandwidth (quad rate)
    unsigned int decim = tuner_manager->get_input_decim();
    double quad_rate = tuner_manager->get_quad_rate();  // Effective bandwidth after decimation
    QString decim_bw_text;
    if (decim > 1) {
        // Show decimation and bandwidth
        if (quad_rate >= 1e6) {
            decim_bw_text = QString("BW: %1 MHz (1/%2)").arg(quad_rate / 1e6, 0, 'f', 2).arg(decim);
        } else if (quad_rate >= 1e3) {
            decim_bw_text = QString("BW: %1 kHz (1/%2)").arg(quad_rate / 1e3, 0, 'f', 0).arg(decim);
        } else {
            decim_bw_text = QString("BW: %1 Hz (1/%2)").arg(quad_rate, 0, 'f', 0).arg(decim);
        }
    } else {
        // No decimation - just show bandwidth (same as sample rate)
        if (quad_rate >= 1e6) {
            decim_bw_text = QString("BW: %1 MHz").arg(quad_rate / 1e6, 0, 'f', 2);
        } else if (quad_rate >= 1e3) {
            decim_bw_text = QString("BW: %1 kHz").arg(quad_rate / 1e3, 0, 'f', 0);
        } else {
            decim_bw_text = QString("BW: %1 Hz").arg(quad_rate, 0, 'f', 0);
        }
    }
    ui->decimBwLabel->setText(decim_bw_text);

    // Get FFT span from plotter
    qint64 span_freq = ui->plotter->getSpan();
    QString span_text;
    if (span_freq >= 1e6) {
        span_text = QString("SPAN: %1 MHz").arg((double)span_freq / 1e6, 0, 'f', 2);
    } else if (span_freq >= 1e3) {
        span_text = QString("SPAN: %1 kHz").arg((double)span_freq / 1e3, 0, 'f', 0);
    } else {
        span_text = QString("SPAN: %1 Hz").arg(span_freq);
    }
    ui->fftSpanLabel->setText(span_text);

    // Update edge frequency labels
    qint64 center_freq = ui->freqCtrl->getFrequency();
    qint64 left_edge = center_freq - span_freq / 2;
    qint64 right_edge = center_freq + span_freq / 2;

    ui->fftLeftEdgeLabel->setText(QString("L: %1 MHz").arg((double)left_edge / 1e6, 0, 'f', 3));
    ui->fftRightEdgeLabel->setText(QString("R: %1 MHz").arg((double)right_edge / 1e6, 0, 'f', 3));

    // Get LNB LO (only show if non-zero)
    if (d_lnb_lo != 0) {
        QString lnb_text;
        double lnb_mhz = (double)d_lnb_lo / 1e6;
        if (lnb_mhz >= 0) {
            lnb_text = QString("LNB LO: +%1 MHz").arg(lnb_mhz, 0, 'f', 2);
        } else {
            lnb_text = QString("LNB LO: %1 MHz").arg(lnb_mhz, 0, 'f', 2);
        }
        ui->lnbLoLabel->setText(lnb_text);
        ui->lnbLoLabel->setVisible(true);
    } else {
        ui->lnbLoLabel->setText("");
        ui->lnbLoLabel->setVisible(false);
    }
}

/**
 * @brief Get current CPU usage percentage.
 * @return CPU usage as percentage (0-100).
 */
float MainWindow::getCpuUsage()
{
#ifdef __APPLE__
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;

    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        quint64 user = cpuinfo.cpu_ticks[CPU_STATE_USER];
        quint64 system = cpuinfo.cpu_ticks[CPU_STATE_SYSTEM];
        quint64 idle = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        quint64 nice = cpuinfo.cpu_ticks[CPU_STATE_NICE];

        quint64 total_user = user + nice;
        quint64 total_idle = idle;
        quint64 total_system = system;

        // Calculate deltas
        quint64 delta_user = total_user - d_prev_cpu_user;
        quint64 delta_system = total_system - d_prev_cpu_system;
        quint64 delta_idle = total_idle - d_prev_cpu_idle;
        quint64 total = delta_user + delta_system + delta_idle;

        // Store for next iteration
        d_prev_cpu_user = total_user;
        d_prev_cpu_system = total_system;
        d_prev_cpu_idle = total_idle;

        if (total > 0) {
            d_cpu_usage = 100.0f * (float)(delta_user + delta_system) / (float)total;
        }
    }
#elif defined(__linux__)
    std::ifstream stat_file("/proc/stat");
    if (stat_file.is_open()) {
        std::string line;
        std::getline(stat_file, line);
        // Parse: cpu user nice system idle iowait irq softirq
        quint64 user, nice, system, idle, iowait, irq, softirq;
        if (sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq) >= 4) {
            quint64 total_user = user + nice;
            quint64 total_system = system + irq + softirq;
            quint64 total_idle = idle + iowait;

            quint64 delta_user = total_user - d_prev_cpu_user;
            quint64 delta_system = total_system - d_prev_cpu_system;
            quint64 delta_idle = total_idle - d_prev_cpu_idle;
            quint64 total = delta_user + delta_system + delta_idle;

            d_prev_cpu_user = total_user;
            d_prev_cpu_system = total_system;
            d_prev_cpu_idle = total_idle;

            if (total > 0) {
                d_cpu_usage = 100.0f * (float)(delta_user + delta_system) / (float)total;
            }
        }
    }
#endif
    return d_cpu_usage;
}

/**
 * @brief Update left-side stats display (CPU, FFT rate, DSP status, tuners).
 */
void MainWindow::updateLeftStats()
{
    // Update CPU usage
    float cpu = getCpuUsage();
    QString cpu_text = QString("CPU: %1%").arg(cpu, 0, 'f', 0);
    ui->cpuUsageLabel->setText(cpu_text);

    // Color based on CPU usage
    if (cpu < 50) {
        ui->cpuUsageLabel->setStyleSheet("QLabel { color: #6c6; font-size: 10px; font-weight: bold; }");  // Green
    } else if (cpu < 80) {
        ui->cpuUsageLabel->setStyleSheet("QLabel { color: #cc6; font-size: 10px; font-weight: bold; }");  // Yellow
    } else {
        ui->cpuUsageLabel->setStyleSheet("QLabel { color: #c66; font-size: 10px; font-weight: bold; }");  // Red
    }

    // Update FFT rate
    QString fft_text = QString("FFT: %1 fps").arg(d_avg_fft_rate, 0, 'f', 1);
    ui->fftRateLabel->setText(fft_text);

    // Update DSP status
    bool dsp_running = ui->actionDSP->isChecked();
    if (dsp_running) {
        ui->dspStatusLabel->setText("DSP: Running");
        ui->dspStatusLabel->setStyleSheet("QLabel { color: #6c6; font-size: 10px; font-weight: bold; }");  // Green
    } else {
        ui->dspStatusLabel->setText("DSP: Stopped");
        ui->dspStatusLabel->setStyleSheet("QLabel { color: #c66; font-size: 10px; font-weight: bold; }");  // Red
    }

    // Update tuner count
    if (tuner_manager) {
        auto channels = tuner_manager->get_all_channels();
        int total = channels.size();
        int active = 0;
        for (int ch_id : channels) {
            auto* channel = tuner_manager->get_channel_impl(ch_id);
            if (channel && channel->is_enabled() && !channel->is_bypassed()) {
                active++;
            }
        }
        QString tuners_text = QString("Tuners: %1/%2").arg(active).arg(total);
        ui->tunersActiveLabel->setText(tuners_text);
    } else {
        ui->tunersActiveLabel->setText("Tuners: 0/0");
    }

    // Get actual disk I/O from the OS (gqrx process only)
    quint64 current_read = 0;
    quint64 current_write = 0;
    quint64 current_time_ms = QDateTime::currentMSecsSinceEpoch();

#ifdef __APPLE__
    // macOS: use proc_pid_rusage for actual disk I/O
    struct rusage_info_v3 rusage;
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V3, (void**)&rusage) == 0) {
        current_read = rusage.ri_diskio_bytesread;
        current_write = rusage.ri_diskio_byteswritten;
    }
#elif defined(__linux__)
    // Linux: read from /proc/self/io
    std::ifstream io_file("/proc/self/io");
    if (io_file.is_open()) {
        std::string line;
        while (std::getline(io_file, line)) {
            if (line.find("read_bytes:") == 0) {
                current_read = std::stoull(line.substr(12));
            } else if (line.find("write_bytes:") == 0) {
                current_write = std::stoull(line.substr(13));
            }
        }
    }
#endif

    // Calculate rates (bytes per second) with exponential moving average smoothing
    // EMA smoothing factor: 0.1 = very smooth, 0.2 = smooth, 0.5 = moderate, 0.8 = responsive
    const float ema_alpha = 0.15f;

    if (d_last_diskio_time_ms > 0) {
        double elapsed_sec = (current_time_ms - d_last_diskio_time_ms) / 1000.0;
        if (elapsed_sec > 0.01) {  // Avoid division by very small numbers
            float instant_read_rate = (float)(current_read - d_prev_disk_read) / elapsed_sec;
            float instant_write_rate = (float)(current_write - d_prev_disk_write) / elapsed_sec;

            // Apply EMA smoothing to reduce bursty fluctuations
            d_disk_read_rate = ema_alpha * instant_read_rate + (1.0f - ema_alpha) * d_disk_read_rate;
            d_disk_write_rate = ema_alpha * instant_write_rate + (1.0f - ema_alpha) * d_disk_write_rate;
        }
    }

    d_prev_disk_read = current_read;
    d_prev_disk_write = current_write;
    d_last_diskio_time_ms = current_time_ms;

    // Only update disk labels every 5 calls (~1 second) to make them readable
    static int disk_update_counter = 0;
    disk_update_counter++;
    if (disk_update_counter < 5) {
        return;  // Skip display update, but keep calculating rates above
    }
    disk_update_counter = 0;

    // Update disk READ label - integer values only
    int read_mb_per_sec = (int)(d_disk_read_rate / (1024.0 * 1024.0));
    int read_kb_per_sec = (int)(d_disk_read_rate / 1024.0);
    QString read_text;
    if (read_mb_per_sec >= 1) {
        read_text = QString("Disk R: %1 MB/s").arg(read_mb_per_sec);
    } else if (read_kb_per_sec >= 1) {
        read_text = QString("Disk R: %1 KB/s").arg(read_kb_per_sec);
    } else {
        read_text = QString("Disk R: 0");
    }
    ui->diskReadLabel->setText(read_text);
    // Color based on read rate
    if (read_mb_per_sec < 1 && read_kb_per_sec < 1) {
        ui->diskReadLabel->setStyleSheet("QLabel { color: #555; font-size: 10px; font-weight: bold; }");  // Dim when idle
    } else if (read_mb_per_sec < 10) {
        ui->diskReadLabel->setStyleSheet("QLabel { color: #6c6; font-size: 10px; font-weight: bold; }");  // Green
    } else if (read_mb_per_sec < 50) {
        ui->diskReadLabel->setStyleSheet("QLabel { color: #cc6; font-size: 10px; font-weight: bold; }");  // Yellow
    } else {
        ui->diskReadLabel->setStyleSheet("QLabel { color: #c66; font-size: 10px; font-weight: bold; }");  // Red
    }

    // Update disk WRITE label - integer values only
    int write_mb_per_sec = (int)(d_disk_write_rate / (1024.0 * 1024.0));
    int write_kb_per_sec = (int)(d_disk_write_rate / 1024.0);
    QString write_text;
    if (write_mb_per_sec >= 1) {
        write_text = QString("Disk W: %1 MB/s").arg(write_mb_per_sec);
    } else if (write_kb_per_sec >= 1) {
        write_text = QString("Disk W: %1 KB/s").arg(write_kb_per_sec);
    } else {
        write_text = QString("Disk W: 0");
    }
    ui->diskWriteLabel->setText(write_text);
    // Color based on write rate
    if (write_mb_per_sec < 1 && write_kb_per_sec < 1) {
        ui->diskWriteLabel->setStyleSheet("QLabel { color: #555; font-size: 10px; font-weight: bold; }");  // Dim when idle
    } else if (write_mb_per_sec < 10) {
        ui->diskWriteLabel->setStyleSheet("QLabel { color: #6c6; font-size: 10px; font-weight: bold; }");  // Green
    } else if (write_mb_per_sec < 50) {
        ui->diskWriteLabel->setStyleSheet("QLabel { color: #cc6; font-size: 10px; font-weight: bold; }");  // Yellow
    } else {
        ui->diskWriteLabel->setStyleSheet("QLabel { color: #c66; font-size: 10px; font-weight: bold; }");  // Red
    }
}

/**
 * @brief Update gain stages.
 * @param read_from_device If true, the gain value will be read from the device,
 *                         otherwise we set gain to the midpoint.
 *
 * This function fetches a list of available gain stages with their range
 * and sends them to the input control UI widget.
 */
void MainWindow::updateGainStages(bool read_from_device)
{
    if (!tuner_manager)
        return;

    gain_list_t gain_list;
    std::vector<std::string> gain_names = tuner_manager->get_gain_names();
    gain_t gain;

    std::vector<std::string>::iterator it;
    for (it = gain_names.begin(); it != gain_names.end(); ++it)
    {
        gain.name = *it;
        tuner_manager->get_gain_range(gain.name, &gain.start, &gain.stop, &gain.step);
        if (read_from_device)
        {
            gain.value = tuner_manager->get_gain(gain.name);
        }
        else
        {
            gain.value = (gain.start + gain.stop) / 2;
            tuner_manager->set_gain(gain.name, gain.value);
        }
        gain_list.push_back(gain);
    }

    uiDockInputCtl->setGainStages(gain_list);
    remote->setGainStages(gain_list);
}

/**
 * @brief Slot for receiving frequency change signals.
 * @param[in] freq The new frequency.
 *
 * This slot is connected to the CFreqCtrl::newFrequency() signal and is used
 * to set the SDR center frequency.
 *
 * In multi-tuner mode, this changes the SDR center frequency.
 * ALL channelized tuners stay at their absolute frequencies - their DDC offsets
 * are recalculated to compensate for the RF shift.
 */
void MainWindow::setNewFrequency(qint64 rx_freq)
{
    if (!tuner_manager) {
        return;
    }

    // Get current RF frequency to calculate the shift
    qint64 old_rf_freq = tuner_manager->get_rf_freq();
    qint64 new_rf_freq = rx_freq - d_lnb_lo;

    // Set the new SDR center frequency
    d_hw_freq = new_rf_freq;
    tuner_manager->set_rf_freq((double)new_rf_freq);

    // Update plotter center
    ui->plotter->setCenterFreq(rx_freq);
    uiDockRxOpt->setHwFreq(d_hw_freq);

    // Recalculate ALL tuners' DDC offsets so their absolute frequencies stay the same
    // Also bypass tuners that fall outside the valid bandwidth (saves CPU)
    double sample_rate = tuner_manager->get_input_rate();
    qint64 max_offset = (qint64)(sample_rate / 2.0 * 0.95);  // 95% of half sample rate

    auto all_ids = tuner_manager->get_all_channels();
    for (channel_id id : all_ids) {
        auto* tuner = tuner_manager->get_channel_impl(id);
        if (tuner) {
            // Get this tuner's current absolute frequency
            qint64 tuner_abs_freq = old_rf_freq + d_lnb_lo + (qint64)tuner->get_center_freq();
            // Calculate new offset relative to new RF center
            qint64 new_offset = tuner_abs_freq - new_rf_freq - d_lnb_lo;
            tuner->set_center_freq((double)new_offset);

            // Check if tuner is within valid bandwidth
            bool in_range = std::abs(new_offset) <= max_offset;
            bool user_enabled = tuner->is_enabled();  // Respect user's manual enable/disable

            // Set bypass state on the channel
            tuner->set_bypassed(!in_range);

            // Marker visibility based on range only (so disabled tuners are still visible)
            ui->plotter->setTunerMarkerEnabled(id, in_range);

            // Determine tuner status and audio based on range AND user enable
            TunerStatus status;
            bool is_muted = false;
            auto mute_it = channel_muted.find(id);
            if (mute_it != channel_muted.end()) {
                is_muted = mute_it->second;
            }

            if (!user_enabled) {
                status = TunerStatus::Disabled;
                tuner->set_audio_gain(0.0f);
            } else if (!in_range) {
                status = TunerStatus::Bypassed;
                tuner->set_audio_gain(0.0f);
            } else if (is_muted) {
                // Tuner in range AND user-enabled but muted
                status = TunerStatus::Running;
                tuner->set_audio_gain(0.0f);
            } else {
                // Tuner in range AND user-enabled AND not muted - restore audio gain
                status = TunerStatus::Running;
                float channel_vol = 1.0f;
                auto it = channel_volumes.find(id);
                if (it != channel_volumes.end()) {
                    channel_vol = it->second / 100.0f;
                }
                tuner->set_audio_gain(d_main_gain_linear * channel_vol);
            }

            if (tuner_list_widget) {
                tuner_list_widget->update_tuner_status(id, status);
            }

            // Marker stays at same absolute freq (just update to refresh display)
            ui->plotter->updateTunerFrequency(id, tuner_abs_freq);
        }
    }

    // Update UI elements
    ui->freqCtrl->setFrequency(rx_freq);
    uiDockBookmarks->setNewFrequency(rx_freq);
}

// Update delta and center (of marker span) when markers are updated
void MainWindow::updateDeltaAndCenter()
{
    if (d_marker_a != MARKER_OFF && d_marker_b != MARKER_OFF)
    {
        qint64 delta = d_marker_b - d_marker_a;
        qint64 center = (d_marker_a + d_marker_b) / 2;
        ui->deltaFreqLabel->setText(QString("Δ%1 kHz   ⨏%2 kHz")
                            .arg(locale().toString(delta/1.e3, 'f', 3))
                            .arg(locale().toString(center/1.e3, 'f', 3)));
    }
    else {
        ui->deltaFreqLabel->setText("");
    }
}

// Set marker via slots
void MainWindow::setMarkerA(qint64 freq)
{
    d_marker_a = freq;
    if (freq != MARKER_OFF)
    {
        ui->markerLabelA->setText(QString("%1 kHz").arg(locale().toString(freq/1.e3, 'f', 3)));
    }
    else {
        ui->markerLabelA->setText("");
    }
    ui->plotter->setMarkers(d_marker_a, d_marker_b);
    updateDeltaAndCenter();
}

void MainWindow::setMarkerB(qint64 freq)
{
    d_marker_b = freq;
    if (freq != MARKER_OFF)
    {
        ui->markerLabelB->setText(QString("%1 kHz").arg(locale().toString(freq/1.e3, 'f', 3)));
    }
    else {
        ui->markerLabelB->setText("");
    }
    ui->plotter->setMarkers(d_marker_a, d_marker_b);
    updateDeltaAndCenter();
}

// Set marker via buttons
void MainWindow::on_setMarkerButtonA_clicked()
{
    auto center_freq = d_hw_freq + (qint64)rx->get_filter_offset();
    setMarkerA(center_freq + d_lnb_lo);
}

void MainWindow::on_setMarkerButtonB_clicked()
{
    auto center_freq = d_hw_freq + (qint64)rx->get_filter_offset();
    setMarkerB(center_freq + d_lnb_lo);
}

void MainWindow::on_clearMarkerButtonA_clicked()
{
    setMarkerA(MARKER_OFF);
}

void MainWindow::on_clearMarkerButtonB_clicked()
{
    setMarkerB(MARKER_OFF);
}

/**
 * @brief Set new LNB LO frequency.
 * @param freq_mhz The new frequency in MHz.
 */
void MainWindow::setLnbLo(double freq_mhz)
{
    // calculate current RF frequency
    auto rf_freq = ui->freqCtrl->getFrequency() - d_lnb_lo;

    d_lnb_lo = qint64(freq_mhz*1e6);
    qDebug() << "New LNB LO:" << d_lnb_lo << "Hz";

    // Update ranges and show updated frequency
    updateFrequencyRange();
    ui->freqCtrl->setFrequency(d_lnb_lo + rf_freq);
    ui->plotter->setCenterFreq(d_lnb_lo + d_hw_freq);

    // update LNB LO in settings
    if (freq_mhz == 0.)
        m_settings->remove("input/lnb_lo");
    else
        m_settings->setValue("input/lnb_lo", d_lnb_lo);

    // Update status labels to show LNB LO
    updateSourceStatusLabels();
}

/** Select new antenna connector. */
void MainWindow::setAntenna(const QString& antenna)
{
    qDebug() << "New antenna selected:" << antenna;
    if (tuner_manager)
        tuner_manager->set_antenna(antenna.toStdString());
}

/**
 * @brief Set new channel filter offset.
 * @param freq_hz The new filter offset in Hz.
 */
void MainWindow::setFilterOffset(qint64 freq_hz)
{
    rx->set_filter_offset((double) freq_hz);
    ui->plotter->setFilterOffset(freq_hz);

    updateFrequencyRange();

    auto rx_freq = d_hw_freq + d_lnb_lo + freq_hz;
    ui->freqCtrl->setFrequency(rx_freq);

    if (rx->is_rds_decoder_active()) {
        rx->reset_rds_parser();
    }
}

/**
 * @brief Set a specific gain.
 * @param name The name of the gain stage to adjust.
 * @param gain The new value.
 */
void MainWindow::setGain(const QString& name, double gain)
{
    if (tuner_manager)
        tuner_manager->set_gain(name.toStdString(), gain);
}

/** Enable / disable hardware AGC. */
void MainWindow::setAutoGain(bool enabled)
{
    if (tuner_manager)
        tuner_manager->set_auto_gain(enabled);
    if (!enabled)
        uiDockInputCtl->restoreManualGains();
}

/**
 * @brief Set new frequency offset value.
 * @param ppm Frequency correction.
 *
 * The valid range is between -200 and 200.
 */
void MainWindow::setFreqCorr(double ppm)
{
    if (ppm < -200.0)
        ppm = -200.0;
    else if (ppm > 200.0)
        ppm = 200.0;

    qDebug() << __FUNCTION__ << ":" << ppm << "ppm";
    rx->set_freq_corr(ppm);
}


/** Enable/disable I/Q reversion. */
void MainWindow::setIqSwap(bool reversed)
{
    if (tuner_manager)
        tuner_manager->set_iq_swap(reversed);
}

/** Enable/disable automatic DC removal. */
void MainWindow::setDcCancel(bool enabled)
{
    if (tuner_manager)
        tuner_manager->set_dc_cancel(enabled);
}

/** Enable/disable automatic IQ balance. */
void MainWindow::setIqBalance(bool enabled)
{
    try
    {
        if (tuner_manager)
            tuner_manager->set_iq_balance(enabled);
    }
    catch (std::exception &x)
    {
        qCritical() << "Failed to set IQ balance: " << x.what();
        m_settings->remove("input/iq_balance");
        uiDockInputCtl->setIqBalance(false);
        if (enabled)
        {
            QMessageBox::warning(this, tr("Gqrx error"),
                                 tr("Failed to set IQ balance.\n"
                                    "IQ balance setting in Input Control disabled."),
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    }
}

/**
 * @brief Ignore hardware limits.
 * @param ignore_limits Whether hardware limits should be ignored or not.
 *
 * This slot is triggered when the user changes the "Ignore hardware limits"
 * option. It will update the allowed frequency range and also update the
 * current RF center frequency, which may change when we switch from ignore to
 * don't ignore.
 */
void MainWindow::setIgnoreLimits(bool ignore_limits)
{
    updateHWFrequencyRange(ignore_limits);

    // Get filter offset from active tuner
    double filter_offset = 0.0;
    if (tuner_manager) {
        auto* tuner = tuner_manager->get_channel_impl(tuner_manager->get_active_channel());
        if (tuner) {
            filter_offset = tuner->get_center_freq();
        }
    }
    auto freq = tuner_manager ? qRound64(tuner_manager->get_rf_freq()) : 0;
    ui->freqCtrl->setFrequency(d_lnb_lo + freq + (qint64)filter_offset);

    // This will ensure that if frequency is clamped and that
    // the UI is updated with the correct frequency.
    freq = ui->freqCtrl->getFrequency();
    setNewFrequency(freq);
}


/** Reset lower digits of main frequency control widget */
void MainWindow::setFreqCtrlReset(bool enabled)
{
    ui->freqCtrl->setResetLowerDigits(enabled);
    uiDockRxOpt->setResetLowerDigits(enabled);
}

/** Invert scroll wheel direction */
void MainWindow::setInvertScrolling(bool enabled)
{
    ui->freqCtrl->setInvertScrolling(enabled);
    ui->plotter->setInvertScrolling(enabled);
    uiDockRxOpt->setInvertScrolling(enabled);
    uiDockAudio->setInvertScrolling(enabled);
}

/**
 * @brief Select new demodulator.
 * @param demod New demodulator.
 */
void MainWindow::selectDemod(const QString& strModulation)
{
    int iDemodIndex;

    iDemodIndex = DockRxOpt::GetEnumForModulationString(strModulation);
    qDebug() << "selectDemod(str):" << strModulation << "-> IDX:" << iDemodIndex;

    return selectDemod(iDemodIndex);
}

/**
 * @brief Select new demodulator.
 * @param demod New demodulator index.
 *
 * This slot basically maps the index of the mode selector to receiver::demod
 * and configures the default channel filter.
 *
 */
void MainWindow::selectDemod(int mode_idx)
{
    double  cwofs = 0.0;
    int     filter_preset = uiDockRxOpt->currentFilter();
    int     flo=0, fhi=0, click_res=100;
    bool    rds_enabled = false;

    // Get active tuner - if none, just update UI without changing DSP
    ReceiverChannel* tuner = nullptr;
    if (tuner_manager) {
        channel_id active = tuner_manager->get_active_channel();
        tuner = tuner_manager->get_channel_impl(active);
    }

    // validate mode_idx
    if (mode_idx < DockRxOpt::MODE_OFF || mode_idx >= DockRxOpt::MODE_LAST)
    {
        qDebug() << "Invalid mode index:" << mode_idx;
        mode_idx = DockRxOpt::MODE_OFF;
    }
    qDebug() << "New mode index:" << mode_idx;

    uiDockRxOpt->getFilterPreset(mode_idx, filter_preset, &flo, &fhi);
    d_filter_shape = (receiver::filter_shape)uiDockRxOpt->currentFilterShape();

    if (rx) {
        rds_enabled = rx->is_rds_decoder_active();
    }
    if (rds_enabled)
        setRdsDecoder(false);
    uiDockRDS->setDisabled();

    switch (mode_idx) {

    case DockRxOpt::MODE_OFF:
        /* Spectrum analyzer only */
        if (rx && rx->is_recording_audio())
        {
            stopAudioRec();
            uiDockAudio->setAudioRecButtonState(false);
        }
        if (dec_afsk1200 != nullptr)
        {
            dec_afsk1200->close();
        }
        if (tuner) {
            tuner->set_demod(ReceiverChannel::RX_DEMOD_OFF);
            tuner->set_audio_gain(0.0f);  // Mute audio when demod is OFF
        }
        click_res = 1000;
        break;

    case DockRxOpt::MODE_RAW:
        /* Raw I/Q; max 96 ksps*/
        if (tuner)
            tuner->set_demod(ReceiverChannel::RX_DEMOD_NONE);
        ui->plotter->setDemodRanges(-40000, -200, 200, 40000, true);
        uiDockAudio->setFftRange(0,24000);
        click_res = 100;
        break;

    case DockRxOpt::MODE_AM:
        if (tuner) {
            tuner->set_demod(ReceiverChannel::RX_DEMOD_AM);
            tuner->set_am_dcr(uiDockRxOpt->currentAmDcr());
        }
        ui->plotter->setDemodRanges(-40000, -200, 200, 40000, true);
        uiDockAudio->setFftRange(0,6000);
        click_res = 100;
        break;

    case DockRxOpt::MODE_AM_SYNC:
        if (tuner) {
            tuner->set_demod(ReceiverChannel::RX_DEMOD_AMSYNC);
        }
        ui->plotter->setDemodRanges(-40000, -200, 200, 40000, true);
        uiDockAudio->setFftRange(0,6000);
        click_res = 100;
        break;

    case DockRxOpt::MODE_NFM:
        ui->plotter->setDemodRanges(-40000, -1000, 1000, 40000, true);
        uiDockAudio->setFftRange(0, 5000);
        if (tuner) {
            tuner->set_demod(ReceiverChannel::RX_DEMOD_NFM);
            tuner->set_fm_maxdev(uiDockRxOpt->currentMaxdev());
            tuner->set_fm_deemph(uiDockRxOpt->currentEmph());
        }
        click_res = 100;
        break;

    case DockRxOpt::MODE_WFM_MONO:
    case DockRxOpt::MODE_WFM_STEREO:
    case DockRxOpt::MODE_WFM_STEREO_OIRT:
        /* Broadcast FM */
        ui->plotter->setDemodRanges(-120e3, -10000, 10000, 120e3, true);
        uiDockAudio->setFftRange(0,24000);  /** FIXME: get audio rate from rx **/
        click_res = 1000;
        if (tuner) {
            if (mode_idx == DockRxOpt::MODE_WFM_MONO)
                tuner->set_demod(ReceiverChannel::RX_DEMOD_WFM_M);
            else if (mode_idx == DockRxOpt::MODE_WFM_STEREO_OIRT)
                tuner->set_demod(ReceiverChannel::RX_DEMOD_WFM_S_OIRT);
            else
                tuner->set_demod(ReceiverChannel::RX_DEMOD_WFM_S);
        }

        uiDockRDS->setEnabled();
        if (rds_enabled)
            setRdsDecoder(true);
        break;

    case DockRxOpt::MODE_LSB:
        /* LSB */
        if (tuner)
            tuner->set_demod(ReceiverChannel::RX_DEMOD_SSB);
        ui->plotter->setDemodRanges(-40000, -100, -5000, 0, false);
        uiDockAudio->setFftRange(0,3000);
        click_res = 100;
        break;

    case DockRxOpt::MODE_USB:
        /* USB */
        if (tuner)
            tuner->set_demod(ReceiverChannel::RX_DEMOD_SSB);
        ui->plotter->setDemodRanges(0, 5000, 100, 40000, false);
        uiDockAudio->setFftRange(0,3000);
        click_res = 100;
        break;

    case DockRxOpt::MODE_CWL:
        /* CW-L */
        if (tuner)
            tuner->set_demod(ReceiverChannel::RX_DEMOD_SSB);
        cwofs = -uiDockRxOpt->getCwOffset();
        ui->plotter->setDemodRanges(-5000, -100, 100, 5000, true);
        uiDockAudio->setFftRange(0,1500);
        click_res = 10;
        break;

    case DockRxOpt::MODE_CWU:
        /* CW-U */
        if (tuner)
            tuner->set_demod(ReceiverChannel::RX_DEMOD_SSB);
        cwofs = uiDockRxOpt->getCwOffset();
        ui->plotter->setDemodRanges(-5000, -100, 100, 5000, true);
        uiDockAudio->setFftRange(0,1500);
        click_res = 10;
        break;

    default:
        qDebug() << "Unsupported mode selection (can't happen!): " << mode_idx;
        flo = -5000;
        fhi = 5000;
        click_res = 100;
        break;
    }

    qDebug() << "Filter preset for mode" << mode_idx << "LO:" << flo << "HI:" << fhi;
    ui->plotter->setHiLowCutFrequencies(flo, fhi);
    ui->plotter->setClickResolution(click_res);
    ui->plotter->setFilterClickResolution(click_res);

    if (tuner) {
        // Only set filter if mode is not OFF (OFF mode has no filter)
        if (mode_idx != DockRxOpt::MODE_OFF) {
            // Convert filter_shape to transition width for ReceiverChannel
            double tw;
            double bw = std::abs(fhi - flo);
            switch (d_filter_shape) {
                case receiver::FILTER_SHAPE_SOFT:
                    tw = bw * 0.5;
                    break;
                case receiver::FILTER_SHAPE_SHARP:
                    tw = bw * 0.1;
                    break;
                case receiver::FILTER_SHAPE_NORMAL:
                default:
                    tw = bw * 0.2;
                    break;
            }
            tuner->set_filter((double)flo, (double)fhi, tw);

            // Update tuner marker filter bounds
            channel_id active_id = tuner_manager->get_active_channel();
            if (active_id >= 0) {
                ui->plotter->updateTunerFilter(active_id, flo, fhi);

                // Set max filter width based on demod mode (from WIDE preset in filter_preset_table)
                int max_filter_half_width = 10000;  // Default to NFM max
                switch (mode_idx) {
                    case DockRxOpt::MODE_OFF:     max_filter_half_width = 0;       break;
                    case DockRxOpt::MODE_RAW:     max_filter_half_width = 15000;   break;
                    case DockRxOpt::MODE_AM:
                    case DockRxOpt::MODE_AM_SYNC: max_filter_half_width = 10000;   break;
                    case DockRxOpt::MODE_LSB:
                    case DockRxOpt::MODE_USB:     max_filter_half_width = 4000;    break;
                    case DockRxOpt::MODE_CWL:
                    case DockRxOpt::MODE_CWU:     max_filter_half_width = 1000;    break;
                    case DockRxOpt::MODE_NFM:     max_filter_half_width = 10000;   break;
                    case DockRxOpt::MODE_WFM_MONO:
                    case DockRxOpt::MODE_WFM_STEREO:
                    case DockRxOpt::MODE_WFM_STEREO_OIRT:
                                                 max_filter_half_width = 100000; break;  // WFM WIDE preset ±100kHz
                    default:                     max_filter_half_width = 10000;   break;
                }
                ui->plotter->setTunerMaxFilterWidth(active_id, max_filter_half_width);
            }
        }
        tuner->set_cw_offset(cwofs);
        tuner->set_sql_level(uiDockRxOpt->currentSquelchLevel());
    }

    remote->setMode(mode_idx);
    remote->setPassband(flo, fhi);

    d_have_audio = (mode_idx != DockRxOpt::MODE_OFF);

    // Restore audio gain when switching from OFF to another mode
    if (d_have_audio && tuner && tuner_manager) {
        // Get channel volume (default 100 if not set)
        float channel_vol = 1.0f;
        channel_id active_id = tuner_manager->get_active_channel();
        auto it = channel_volumes.find(active_id);
        if (it != channel_volumes.end()) {
            channel_vol = it->second / 100.0f;
        }
        // Apply combined gain (main * channel)
        tuner->set_audio_gain(d_main_gain_linear * channel_vol);
    }

    uiDockRxOpt->setCurrentDemod(mode_idx);

    // Update tuner type in tuner list widget
    if (tuner_list_widget && tuner_manager) {
        channel_id active_id = tuner_manager->get_active_channel();
        if (active_id >= 0) {
            // Map mode_idx to ReceiverType
            ReceiverType rx_type = ReceiverType::ANALOG_NFM;  // default
            switch (mode_idx) {
                case DockRxOpt::MODE_AM:      rx_type = ReceiverType::ANALOG_AM; break;
                case DockRxOpt::MODE_AM_SYNC: rx_type = ReceiverType::ANALOG_AMSYNC; break;
                case DockRxOpt::MODE_NFM:     rx_type = ReceiverType::ANALOG_NFM; break;
                case DockRxOpt::MODE_WFM_MONO:   rx_type = ReceiverType::ANALOG_WFM_MONO; break;
                case DockRxOpt::MODE_WFM_STEREO: rx_type = ReceiverType::ANALOG_WFM_STEREO; break;
                case DockRxOpt::MODE_WFM_STEREO_OIRT: rx_type = ReceiverType::ANALOG_WFM_STEREO_OIRT; break;
                case DockRxOpt::MODE_LSB:    rx_type = ReceiverType::ANALOG_LSB; break;
                case DockRxOpt::MODE_USB:    rx_type = ReceiverType::ANALOG_USB; break;
                case DockRxOpt::MODE_CWL:    rx_type = ReceiverType::ANALOG_CW_L; break;
                case DockRxOpt::MODE_CWU:    rx_type = ReceiverType::ANALOG_CW_U; break;
                default: break;
            }

            // Find and update the TunerRowWidget
            for (auto* row : tuner_list_widget->findChildren<TunerRowWidget*>()) {
                if (row->tuner_id() == active_id) {
                    row->setReceiverType(rx_type);
                    break;
                }
            }
        }
    }
}


/**
 * @brief New FM deviation selected.
 * @param max_dev The enw FM deviation.
 */
void MainWindow::setFmMaxdev(float max_dev)
{
    qDebug() << "FM MAX_DEV: " << max_dev;

    if (tuner_manager) {
        auto* tuner = tuner_manager->get_channel_impl(tuner_manager->get_active_channel());
        if (tuner)
            tuner->set_fm_maxdev(max_dev);
    }
}


/**
 * @brief New FM de-emphasis time constant selected.
 * @param tau The new time constant
 */
void MainWindow::setFmEmph(double tau)
{
    qDebug() << "FM TAU: " << tau;

    if (tuner_manager) {
        auto* tuner = tuner_manager->get_channel_impl(tuner_manager->get_active_channel());
        if (tuner)
            tuner->set_fm_deemph(tau);
    }
}


/**
 * @brief AM DCR status changed (slot).
 * @param enabled Whether DCR is enabled or not.
 */
void MainWindow::setAmDcr(bool enabled)
{
    if (tuner_manager) {
        auto* tuner = tuner_manager->get_channel_impl(tuner_manager->get_active_channel());
        if (tuner)
            tuner->set_am_dcr(enabled);
    }
}

void MainWindow::setCwOffset(int offset)
{
    if (tuner_manager) {
        auto* tuner = tuner_manager->get_channel_impl(tuner_manager->get_active_channel());
        if (tuner)
            tuner->set_cw_offset(offset);
    }
}

/**
 * @brief AM-Sync DCR status changed (slot).
 * @param enabled Whether DCR is enabled or not.
 */
void MainWindow::setAmSyncDcr(bool enabled)
{
    rx->set_amsync_dcr(enabled);
}

/**
 * @brief New AM-Sync PLL BW selected.
 * @param pll_bw The new PLL BW.
 */
void MainWindow::setAmSyncPllBw(float pll_bw)
{
    qDebug() << "AM-Sync PLL BW: " << pll_bw;

    /* receiver will check range */
    rx->set_amsync_pll_bw(pll_bw);
}

/**
 * @brief Audio gain changed.
 * @param value The new audio gain in dB.
 */
void MainWindow::setAudioGain(float value)
{
    // Convert dB to linear gain and store
    d_main_gain_linear = std::pow(10.0f, value / 20.0f);

    // Apply combined gain (main * channel) to enabled, non-bypassed, non-muted tuners only
    if (tuner_manager) {
        auto all_channels = tuner_manager->get_all_channels();
        for (auto id : all_channels) {
            auto* tuner = tuner_manager->get_channel_impl(id);
            if (tuner) {
                // Check mute state
                bool is_muted = false;
                auto mute_it = channel_muted.find(id);
                if (mute_it != channel_muted.end()) {
                    is_muted = mute_it->second;
                }

                bool enabled = tuner->is_enabled();
                bool bypassed = tuner->is_bypassed();

                // Only apply gain if tuner is enabled, not bypassed, and not muted
                if (enabled && !bypassed && !is_muted) {
                    // Get channel volume (default 100 if not set)
                    float channel_vol = 1.0f;
                    auto it = channel_volumes.find(id);
                    if (it != channel_volumes.end()) {
                        channel_vol = it->second / 100.0f;
                    }
                    float final_gain = d_main_gain_linear * channel_vol;
                    tuner->set_audio_gain(final_gain);
                }
                // Disabled, bypassed, or muted tuners stay muted (gain already set to 0)
            }
        }
    }

    // Also set on legacy receiver for backwards compatibility
    rx->set_af_gain(value);
}

/** Set AGC ON/OFF. */
void MainWindow::setAgcOn(bool agc_on)
{
    rx->set_agc_on(agc_on);
}

/** AGC hang ON/OFF. */
void MainWindow::setAgcHang(bool use_hang)
{
    rx->set_agc_hang(use_hang);
}

/** AGC threshold changed. */
void MainWindow::setAgcThreshold(int threshold)
{
    rx->set_agc_threshold(threshold);
}

/** AGC slope factor changed. */
void MainWindow::setAgcSlope(int factor)
{
    rx->set_agc_slope(factor);
}

/** AGC manual gain changed. */
void MainWindow::setAgcGain(int gain)
{
    rx->set_agc_manual_gain(gain);
}

/** AGC decay changed. */
void MainWindow::setAgcDecay(int msec)
{
    rx->set_agc_decay(msec);
}

/**
 * @brief Noise blanker configuration changed.
 * @param nb1 Noise blanker 1 ON/OFF.
 * @param nb2 Noise blanker 2 ON/OFF.
 * @param threshold Noise blanker threshold.
 */
void MainWindow::setNoiseBlanker(int nbid, bool on, float threshold)
{
    qDebug() << "Noise blanker NB:" << nbid << " ON:" << on << "THLD:"
             << threshold;

    rx->set_nb_on(nbid, on);
    rx->set_nb_threshold(nbid, threshold);
}

/**
 * @brief Squelch level changed.
 * @param level_db The new squelch level in dBFS.
 */
void MainWindow::setSqlLevel(double level_db)
{
    rx->set_sql_level(level_db);
    // Note: sMeter removed - signal levels shown in tuner manager rows
}

/**
 * @brief Squelch level auto clicked.
 * @return The new squelch level.
 */
double MainWindow::setSqlLevelAuto()
{
    float signal_level = -100.0f;

    // Get signal level from active tuner
    if (tuner_manager) {
        channel_id active_id = tuner_manager->get_active_channel();
        auto* tuner = tuner_manager->get_channel_impl(active_id);
        if (tuner) {
            signal_level = tuner->get_signal_level();
        }
    }

    double level = (double)signal_level + 3.0;

    if (level > -10.0) { // avoid 0 dBFS
        level = uiDockRxOpt->getSqlLevel();
    }

    setSqlLevel(level);
    return level;
}

/** Signal strength meter timeout. */
void MainWindow::meterTimeout()
{
    float level = -100.0f;

    // Get signal level from active tuner
    if (tuner_manager) {
        auto* tuner = tuner_manager->get_channel_impl(tuner_manager->get_active_channel());
        if (tuner) {
            level = tuner->get_signal_level();
        }

        // Update RSSI indicators for all tuners
        if (tuner_list_widget) {
            for (int ch_id : tuner_manager->get_all_channels()) {
                auto* channel = tuner_manager->get_channel_impl(ch_id);
                if (channel) {
                    // Only show signal for enabled, non-bypassed channels
                    float ch_level;
                    if (channel->is_enabled() && !channel->is_bypassed()) {
                        ch_level = channel->get_signal_level();
                    } else {
                        ch_level = -150.0f;  // No signal for disabled/bypassed
                    }
                    tuner_list_widget->update_tuner_rssi(ch_id, ch_level);

                    // Check squelch state and notify recorder if recording
                    if (channel->is_new_audio_recording() || channel->is_recording_iq()) {
                        double sql_level = channel->get_squelch_level();
                        bool squelch_open = (ch_level > sql_level);
                        channel->notify_squelch_open(squelch_open);
                    }

                    // Update recording duration indicators
                    if (channel->is_recording_iq() || channel->is_new_audio_recording()) {
                        double iq_duration = channel->get_iq_recording_duration();
                        double audio_duration = channel->get_audio_recording_duration();
                        tuner_list_widget->update_tuner_recording_info(ch_id, audio_duration, iq_duration);
                    }
                }
            }
        }
    }

    // Note: sMeter removed - signal levels shown in tuner manager rows
    remote->setSignalLevel(level);

    // Update left-side stats (CPU, FFT rate, DSP status, tuners)
    updateLeftStats();
}

/** Baseband FFT plot timeout. */
void MainWindow::iqFftTimeout()
{
    if (!tuner_manager)
        return;

    const unsigned int fftsize = tuner_manager->iq_fft_size();

    if (fftsize == 0)
    {
        /* nothing to do, wait until next activation. */
        return;
    }

    // Track the frame rate and warn if not keeping up. Since the interval is ms, the timer can
    // not be set exactly to all rates.
    const quint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const float expected_rate = 1000.0f / (float)iq_fft_timer->interval();
    const float last_fft_rate = 1000.0f / (float)(now_ms - d_last_fft_ms);
    const float alpha = std::pow(expected_rate, -0.75f);
    if (d_avg_fft_rate == 0.0f)
        d_avg_fft_rate = expected_rate;
    else
        d_avg_fft_rate = (1.0f - alpha) * d_avg_fft_rate + alpha * last_fft_rate;

    const bool drop = d_avg_fft_rate < expected_rate * 0.95f;
    if (drop != d_frame_drop) {
        if (drop) {
            uiDockFft->setActualFrameRate(d_avg_fft_rate, true);
        }
        else {
            uiDockFft->setActualFrameRate(d_avg_fft_rate, false);
        }
        d_frame_drop = drop;
    }
    d_last_fft_ms = now_ms;

    if (tuner_manager->get_iq_fft_data(d_iqFftData.data()) >= 0)
        ui->plotter->setNewFftData(d_iqFftData.data(), fftsize);
}

/** Audio FFT plot timeout. */
void MainWindow::audioFftTimeout()
{
    const unsigned int fftsize = rx->audio_fft_size();

    if (fftsize == 0)
    {
        /* nothing to do, wait until next activation. */
        qDebug() << "No audio FFT data.";
        return;
    }

    if (!d_have_audio || !uiDockAudio->isVisible())
        return;

    if (rx->get_audio_fft_data(d_audioFftData.data()) >= 0)
        uiDockAudio->setNewFftData(d_audioFftData.data(), fftsize);
}

/** RDS message display timeout. */
void MainWindow::rdsTimeout()
{
    std::string buffer;
    int num;

    rx->get_rds_data(buffer, num);
    while(num!=-1) {
        uiDockRDS->updateRDS(QString::fromStdString(buffer), num);
        rx->get_rds_data(buffer, num);
    }
}

/**
 * @brief Start audio recorder.
 * @param filename The file name into which audio should be recorded.
 */
void MainWindow::startAudioRec(const QString& filename)
{
    if (!d_have_audio)
    {
        QMessageBox msg_box;
        msg_box.setIcon(QMessageBox::Critical);
        msg_box.setText(tr("Recording audio requires a demodulator.\n"
                           "Currently, demodulation is switched off "
                           "(Mode->Demod Off)."));
        msg_box.exec();
        uiDockAudio->setAudioRecButtonState(false);
    }
    else if (rx->start_audio_recording(filename.toStdString()))
    {
        ui->statusBar->showMessage(tr("Error starting audio recorder"));

        /* reset state of record button */
        uiDockAudio->setAudioRecButtonState(false);
    }
    else
    {
        ui->statusBar->showMessage(tr("Recording audio to %1").arg(filename));
    }
}

/** Stop audio recorder. */
void MainWindow::stopAudioRec()
{
    if (rx->stop_audio_recording())
    {
        /* okay, this one would be weird if it really happened */
        ui->statusBar->showMessage(tr("Error stopping audio recorder"));

        uiDockAudio->setAudioRecButtonState(true);
    }
    else
    {
        ui->statusBar->showMessage(tr("Audio recorder stopped"), 5000);
    }
}


/** Start playback of audio file. */
void MainWindow::startAudioPlayback(const QString& filename)
{
    if (rx->start_audio_playback(filename.toStdString()))
    {
        ui->statusBar->showMessage(tr("Error trying to play %1").arg(filename));

        /* reset state of record button */
        uiDockAudio->setAudioPlayButtonState(false);
    }
    else
    {
        ui->statusBar->showMessage(tr("Playing %1").arg(filename));
    }
}

/** Stop playback of audio file. */
void MainWindow::stopAudioPlayback()
{
    if (rx->stop_audio_playback())
    {
        /* okay, this one would be weird if it really happened */
        ui->statusBar->showMessage(tr("Error stopping audio playback"));

        uiDockAudio->setAudioPlayButtonState(true);
    }
    else
    {
        ui->statusBar->showMessage(tr("Audio playback stopped"), 5000);
    }
}

/** Start streaming audio over UDP. */
void MainWindow::startAudioStream(const QString& udp_host, int udp_port, bool stereo)
{
    rx->start_udp_streaming(udp_host.toStdString(), udp_port, stereo);
}

/** Stop streaming audio over UDP. */
void MainWindow::stopAudioStreaming()
{
    rx->stop_udp_streaming();
}

/** Start I/Q recording. */
void MainWindow::startIqRecording(const QString& recdir, const QString& format)
{
    qDebug() << __func__;
    // generate file name using date, time, rf freq in kHz and BW in Hz
    // gqrx_iq_yyyymmdd_hhmmss_freq_bw_fc.raw
    auto freq = tuner_manager ? qRound64(tuner_manager->get_rf_freq()) : 0LL;
    auto sr = tuner_manager ? qRound64(tuner_manager->get_input_rate()) : 0LL;
    auto dec = tuner_manager ? (quint32)(tuner_manager->get_input_decim()) : 1;
    auto currentDate = QDateTime::currentDateTimeUtc();
    auto filenameTemplate = currentDate.toString("%1/gqrx_yyyyMMdd_hhmmss_%2_%3_fc.%4").arg(recdir).arg(freq).arg(sr/dec);
    bool sigmf = (format == "SigMF");
    auto lastRec = filenameTemplate.arg(sigmf ? "sigmf-data" : "raw");

    QFile metaFile(filenameTemplate.arg("sigmf-meta"));
    bool ok = true;
    if (sigmf) {
        auto meta = QJsonDocument { QJsonObject {
            {"global", QJsonObject {
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
                {"core:datatype", "cf32_be"},
#else
                {"core:datatype", "cf32_le"},
#endif
                {"core:sample_rate", sr/dec},
                {"core:version", "1.0.0"},
                {"core:recorder", "Gqrx " VERSION},
                {"core:hw", QString("OsmoSDR: ") + m_settings->value("input/device", "").toString()},
            }}, {"captures", QJsonArray {
                QJsonObject {
                    {"core:sample_start", 0},
                    {"core:frequency", freq},
                    {"core:datetime", currentDate.toString(Qt::ISODateWithMs)},
                },
            }}, {"annotations", QJsonArray {}},
        }}.toJson();

        if (!metaFile.open(QIODevice::WriteOnly) || metaFile.write(meta) != meta.size()) {
            ok = false;
        }
    }

    // start recorder; fails if recording already in progress
    // Use tuner_manager if available (multi-tuner mode), otherwise fall back to rx
    bool recording_failed = false;
    if (tuner_manager) {
        if (tuner_manager->start_iq_recording(lastRec.toStdString()) != TunerManager::STATUS_OK) {
            recording_failed = true;
        }
    } else {
        if (rx->start_iq_recording(lastRec.toStdString())) {
            recording_failed = true;
        }
    }

    if (!ok || recording_failed)
    {
        // remove metadata file if we managed to open it
        if (sigmf && metaFile.isOpen())
            metaFile.remove();

        // reset action status
        ui->statusBar->showMessage(tr("Error starting I/Q recoder"));

        // show an error message to user
        QMessageBox msg_box;
        msg_box.setIcon(QMessageBox::Critical);
        msg_box.setText(tr("There was an error starting the I/Q recorder.\n"
                           "Check write permissions for the selected location."));
        msg_box.exec();

    }
    else
    {
        ui->statusBar->showMessage(tr("Recording I/Q data to: %1").arg(lastRec),
                                   5000);
    }
}

/** Stop current I/Q recording. */
void MainWindow::stopIqRecording()
{
    qDebug() << __func__;

    // Use tuner_manager if available (multi-tuner mode), otherwise fall back to rx
    bool stop_failed = false;
    if (tuner_manager && tuner_manager->is_recording_iq()) {
        if (tuner_manager->stop_iq_recording() != TunerManager::STATUS_OK) {
            stop_failed = true;
        }
    } else {
        if (rx->stop_iq_recording()) {
            stop_failed = true;
        }
    }

    if (stop_failed) {
        ui->statusBar->showMessage(tr("Error stopping I/Q recoder"));
    } else {
        ui->statusBar->showMessage(tr("I/Q data recoding stopped"), 5000);
    }
}

void MainWindow::startIqPlayback(const QString& filename, float samprate, qint64 center_freq)
{
    if (ui->actionDSP->isChecked())
    {
        // suspend DSP while we reload settings
        on_actionDSP_triggered(false);
    }

    storeSession();

    auto sri = (int)samprate;
    auto cf  = center_freq;
    QString escapedFilename = receiver::escape_filename(filename.toStdString()).c_str();
    auto devstr = QString("file=%1,rate=%2,freq=%3,throttle=true,repeat=false")
            .arg(escapedFilename).arg(sri).arg(cf);

    qDebug() << __func__ << ":" << devstr;

    if (tuner_manager) {
        tuner_manager->set_input_device(devstr.toStdString());
    }
    updateHWFrequencyRange(false);

    // sample rate
    double actual_rate = 0.0;
    if (tuner_manager) {
        tuner_manager->set_input_rate((double)samprate);
        actual_rate = tuner_manager->get_input_rate();
    }
    qDebug() << "Requested sample rate:" << samprate;
    qDebug() << "Actual sample rate   :" << QString("%1")
                .arg(actual_rate, 0, 'f', 6);

    uiDockRxOpt->setFilterOffsetRange((qint64)(actual_rate));
    ui->plotter->setSampleRate(actual_rate);
    ui->plotter->setSpanFreq((quint32)actual_rate);

    // Set center frequency on tuner_manager and frequency display
    if (tuner_manager) {
        tuner_manager->set_rf_freq((double)center_freq);
    }
    ui->freqCtrl->setFrequency(center_freq);
    ui->plotter->setCenterFreq(center_freq);

    remote->setBandwidth(actual_rate);
    updateSourceStatusLabels();

    // FIXME: would be nice with good/bad status
    ui->statusBar->showMessage(tr("Playing %1").arg(filename));

    on_actionDSP_triggered(true);
}

void MainWindow::stopIqPlayback()
{
    if (ui->actionDSP->isChecked())
    {
        // suspend DSP while we reload settings
        on_actionDSP_triggered(false);
    }

    ui->statusBar->showMessage(tr("I/Q playback stopped"), 5000);

    // restore original input device
    auto indev = m_settings->value("input/device", "").toString();
    if (tuner_manager) {
        tuner_manager->set_input_device(indev.toStdString());
    }

    // restore sample rate
    bool conv_ok;
    auto sr = m_settings->value("input/sample_rate", 0).toInt(&conv_ok);
    if (conv_ok && (sr > 0) && tuner_manager)
    {
        tuner_manager->set_input_rate(sr);
        auto actual_rate = tuner_manager->get_input_rate();
        qDebug() << "Requested sample rate:" << sr;
        qDebug() << "Actual sample rate   :" << QString("%1")
                    .arg(actual_rate, 0, 'f', 6);

        uiDockRxOpt->setFilterOffsetRange((qint64)(actual_rate));
        ui->plotter->setSampleRate(actual_rate);
        ui->plotter->setSpanFreq((quint32)actual_rate);
        remote->setBandwidth(sr);
        updateSourceStatusLabels();

        // not needed as long as we are not recording in iq_tool
        //iq_tool->setSampleRate(sr);
    }

    // restore frequency, gain, etc...
    uiDockInputCtl->readSettings(m_settings);
    bool centerOK = false;
    bool offsetOK = false;
    qint64 oldCenter = m_settings->value("input/frequency", 0).toLongLong(&centerOK);
    qint64 oldOffset = m_settings->value("receiver/offset", 0).toLongLong(&offsetOK);
    if (centerOK && offsetOK)
    {
        on_plotter_newDemodFreq(oldCenter, oldOffset);
    }

    if (ui->actionDSP->isChecked())
    {
        // restsart DSP
        on_actionDSP_triggered(true);
    }
}


/**
 * Go to a specific offset in the IQ file.
 * @param seek_pos The byte offset from the beginning of the file.
 */
void MainWindow::seekIqFile(qint64 seek_pos)
{

    // Use tuner_manager if available, otherwise fall back to rx
    if (tuner_manager) {
        tuner_manager->seek_iq_file((long)seek_pos);
    } else {
        rx->seek_iq_file((long)seek_pos);
    }

}

/** FFT size has changed. */
void MainWindow::setIqFftSize(int size)
{
    qDebug() << "Changing baseband FFT size to" << size;
    d_iqFftData.resize(size);
    d_iqFftData.shrink_to_fit();
    if (tuner_manager)
    {
        tuner_manager->set_iq_fft_size(size);
    }
}

/** Baseband FFT rate has changed. */
void MainWindow::setIqFftRate(int fps)
{
    int interval;

    d_fps = fps;

    if (fps == 0)
    {
        interval = 36e7; // 100 hours
        ui->plotter->setRunningState(false);
    }
    else
    {
        interval = 1000 / fps;

        ui->plotter->setFftRate(fps);
        if (iq_fft_timer->isActive())
        {
            ui->plotter->setRunningState(true);
        }
    }

    // Limit to 500 fps
    if (interval > 1 && iq_fft_timer->isActive())
    {
        iq_fft_timer->setInterval(interval);
    }

    uiDockFft->setWfResolution(ui->plotter->getWfTimeRes());

    // Invalidate average frame rate
    d_avg_fft_rate = 0.0;
}

void MainWindow::setIqFftWindow(int type)
{
    d_fftWindowType = type;
    if (tuner_manager)
        tuner_manager->set_iq_fft_window(d_fftWindowType, d_fftNormalizeEnergy);
}

void MainWindow::plotScaleChanged(int type, bool perHz)
{
    // PLOT_SCALE_DBFS (0) always uses amplitude normalization.

    // PLOT_SCALE_DBV (1) requires energy normalization for /sqrt(Hz) (1), but
    // not for RBW (0).

    // PLOT_SCALE_DBM (2) requires energy normalization of FFT window whether
    // or not perHz is specified.

    d_fftNormalizeEnergy = (type == 2) || (type == 1 && perHz);
    if (tuner_manager)
        tuner_manager->set_iq_fft_window(d_fftWindowType, d_fftNormalizeEnergy);
}

/** Waterfall time span has changed. */
void MainWindow::setWfTimeSpan(quint64 span_ms)
{
    // set new time span, then send back new resolution to be shown by GUI label
    ui->plotter->setWaterfallSpan(span_ms);
    uiDockFft->setWfResolution(ui->plotter->getWfTimeRes());
}

void MainWindow::setWfSize()
{
    uiDockFft->setWfResolution(ui->plotter->getWfTimeRes());
}

/**
 * @brief Vertical split between waterfall and pandapter changed.
 * @param pct_pand The percentage of the waterfall.
 */
void MainWindow::setIqFftSplit(int pct_wf)
{
    if ((pct_wf >= 0) && (pct_wf <= 100))
        ui->plotter->setPercent2DScreen(pct_wf);
}

/** Audio FFT rate has changed. */
void MainWindow::setAudioFftRate(int fps)
{
    auto interval = 1000 / fps;

    if (interval < 10)
        return;

    if (audio_fft_timer->isActive())
        audio_fft_timer->setInterval(interval);
}

/** Set FFT plot color. */
void MainWindow::setFftColor(const QColor& color)
{
    ui->plotter->setFftPlotColor(color);
    uiDockAudio->setFftColor(color);
}

/** Enable/disable filling the aread below the FFT plot. */
void MainWindow::enableFftFill(bool enable)
{
    ui->plotter->enableFftFill(enable);
    uiDockAudio->setFftFill(enable);
}

/**
 * @brief Start/Stop DSP processing.
 * @param checked Flag indicating whether DSP processing should be ON or OFF.
 *
 * This slot is executed when the actionDSP is toggled by the user. This can
 * either be via the menu bar or the "power on" button in the main toolbar or
 * by remote control.
 */
void MainWindow::on_actionDSP_triggered(bool checked)
{
    remote->setReceiverStatus(checked);

    if (checked)
    {
        /* Start TunerManager (owns SDR source, FFT, and all tuners) */
        if (tuner_manager) {
            auto status = tuner_manager->start();
            if (status != TunerManager::STATUS_OK) {
                qWarning() << "TunerManager failed to start, status:" << status;
            }

            // Restore tuners from saved state, or create default tuner on first run
            int existing_count = tuner_manager->get_active_channel_count();
            if (existing_count == 0) {
                int saved_count = 0;
                int active_index = 0;

                if (m_settings && m_settings->contains("tuner/count")) {
                    saved_count = m_settings->value("tuner/count", 0).toInt();
                    active_index = m_settings->value("tuner/active_index", 0).toInt();
                } else {
                    // First run - don't auto-create tuners, let user add them manually
                    saved_count = 0;
                }

                // Create and restore each tuner
                std::vector<channel_id> created_ids;
                for (int i = 0; i < saved_count; i++) {
                    QString prefix = QString("tuners/%1/").arg(i);

                    // Get saved receiver type (preferred) or fall back to demod conversion
                    ReceiverType rx_type = ReceiverType::ANALOG_NFM;  // Default
                    if (m_settings && m_settings->contains(prefix + "receiver_type")) {
                        rx_type = static_cast<ReceiverType>(m_settings->value(prefix + "receiver_type").toInt());
                    } else if (m_settings) {
                        // Legacy: convert from demod value
                        int demod_int = m_settings->value(prefix + "demod", (int)ReceiverChannel::RX_DEMOD_NFM).toInt();
                        switch (demod_int) {
                            case ReceiverChannel::RX_DEMOD_AM:
                                rx_type = ReceiverType::ANALOG_AM;
                                break;
                            case ReceiverChannel::RX_DEMOD_AMSYNC:
                                rx_type = ReceiverType::ANALOG_AMSYNC;
                                break;
                            case ReceiverChannel::RX_DEMOD_WFM_M:
                                rx_type = ReceiverType::ANALOG_WFM_MONO;
                                break;
                            case ReceiverChannel::RX_DEMOD_WFM_S:
                                rx_type = ReceiverType::ANALOG_WFM_STEREO;
                                break;
                            case ReceiverChannel::RX_DEMOD_WFM_S_OIRT:
                                rx_type = ReceiverType::ANALOG_WFM_STEREO_OIRT;
                                break;
                            case ReceiverChannel::RX_DEMOD_SSB:
                                rx_type = ReceiverType::ANALOG_USB;  // Default SSB to USB
                                break;
                            default:
                                rx_type = ReceiverType::ANALOG_NFM;
                                break;
                        }
                    }

                    channel_id new_id = tuner_manager->create_channel(ChannelType::MANUAL, rx_type);
                    if (new_id >= 0) {
                        created_ids.push_back(new_id);
                        ReceiverChannel* tuner = tuner_manager->get_channel_impl(new_id);
                        if (tuner && m_settings) {
                            // Restore tuner settings
                            QString name = m_settings->value(prefix + "name", QString("Tuner %1").arg(new_id)).toString();
                            double freq_offset = m_settings->value(prefix + "freq_offset", 0.0).toDouble();
                            double squelch = m_settings->value(prefix + "squelch", -150.0).toDouble();
                            int volume = m_settings->value(prefix + "volume", 18).toInt();  // Default -15dB
                            bool muted = m_settings->value(prefix + "muted", false).toBool();
                            bool enabled = m_settings->value(prefix + "enabled", true).toBool();

                            tuner->set_channel_name(name.toStdString());
                            tuner->set_center_freq(freq_offset);
                            // Note: demod mode is set via rx_type when channel is created
                            tuner->set_sql_level(squelch);

                            // Populate MainWindow maps for volume/mute tracking
                            channel_volumes[new_id] = volume;
                            channel_muted[new_id] = muted;

                            // Restore enabled state and audio gain (respecting mute)
                            tuner->set_enabled(enabled);
                            if (enabled && !muted) {
                                float channel_vol = volume / 100.0f;
                                tuner->set_audio_gain(d_main_gain_linear * channel_vol);
                            } else {
                                tuner->set_audio_gain(0.0f);  // Mute disabled or muted tuners
                            }
                        }
                    }
                }

                // Set active tuner
                if (!created_ids.empty()) {
                    int idx = (active_index >= 0 && active_index < (int)created_ids.size()) ? active_index : 0;
                    tuner_manager->set_active_channel(created_ids[idx]);
                }
            }

            // Create markers for all tuners
            std::vector<channel_id> tuner_ids = tuner_manager->get_all_channels();
            for (size_t i = 0; i < tuner_ids.size(); i++) {
                channel_id tuner_id = tuner_ids[i];
                ReceiverChannel* tuner = tuner_manager->get_channel_impl(tuner_id);
                if (tuner) {
                    QString prefix = QString("tuners/%1/").arg(i);

                    // Calculate marker frequency from RF center + tuner offset
                    qint64 tuner_freq = tuner_manager->get_rf_freq() + d_lnb_lo + (qint64)tuner->get_center_freq();

                    // Restore color and visibility from settings
                    QColor marker_color = ui->plotter->getDefaultTunerColor(tuner_id);
                    bool marker_visible = true;
                    if (m_settings) {
                        QString color_str = m_settings->value(prefix + "color", "").toString();
                        if (!color_str.isEmpty()) {
                            marker_color = QColor(color_str);
                        }
                        marker_visible = m_settings->value(prefix + "visible", true).toBool();
                    }

                    // Get receiver type from channel (was set during channel creation)
                    ReceiverType rx_type = tuner->get_backend_type();

                    // Get default filter bounds based on receiver type
                    int default_filter_low = -5000;
                    int default_filter_high = 5000;
                    switch (rx_type) {
                        case ReceiverType::ANALOG_WFM_MONO:
                        case ReceiverType::ANALOG_WFM_STEREO:
                        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
                            default_filter_low = -80000;
                            default_filter_high = 80000;
                            break;
                        case ReceiverType::ANALOG_USB:
                            default_filter_low = 0;
                            default_filter_high = 3000;
                            break;
                        case ReceiverType::ANALOG_LSB:
                            default_filter_low = -3000;
                            default_filter_high = 0;
                            break;
                        case ReceiverType::ANALOG_CW_U:
                            default_filter_low = -250;
                            default_filter_high = 750;
                            break;
                        case ReceiverType::ANALOG_CW_L:
                            default_filter_low = -750;
                            default_filter_high = 250;
                            break;
                        default:
                            break;  // NFM/AM: -5000/5000
                    }

                    // Get saved filter bounds (with type-appropriate defaults)
                    int filter_low = m_settings ? m_settings->value(prefix + "filter_low", default_filter_low).toInt() : default_filter_low;
                    int filter_high = m_settings ? m_settings->value(prefix + "filter_high", default_filter_high).toInt() : default_filter_high;

                    // Determine max filter half width based on receiver type
                    int max_filter_half_width = 10000;  // Default to NFM
                    switch (rx_type) {
                        case ReceiverType::ANALOG_OFF:
                            max_filter_half_width = 0;
                            break;
                        case ReceiverType::ANALOG_RAW:
                            max_filter_half_width = 15000;
                            break;
                        case ReceiverType::ANALOG_AM:
                        case ReceiverType::ANALOG_AMSYNC:
                        case ReceiverType::ANALOG_NFM:
                            max_filter_half_width = 10000;
                            break;
                        case ReceiverType::ANALOG_WFM_MONO:
                        case ReceiverType::ANALOG_WFM_STEREO:
                        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
                            max_filter_half_width = 100000;  // WFM WIDE preset ±100kHz
                            break;
                        case ReceiverType::ANALOG_USB:
                        case ReceiverType::ANALOG_LSB:
                            max_filter_half_width = 4000;
                            break;
                        case ReceiverType::ANALOG_CW_U:
                        case ReceiverType::ANALOG_CW_L:
                            max_filter_half_width = 1000;
                            break;
                        default:
                            max_filter_half_width = 10000;
                            break;
                    }

                    MultiTunerPlotter::TunerMarker marker;
                    marker.tuner_id = tuner_id;
                    marker.name = QString::fromStdString(tuner->get_channel_name());
                    marker.frequency = tuner_freq;
                    marker.filter_low = filter_low;
                    marker.filter_high = filter_high;
                    marker.max_filter_half_width = max_filter_half_width;
                    marker.enabled = marker_visible;
                    marker.active = (tuner_id == tuner_manager->get_active_channel());
                    marker.color = marker_color;
                    marker.filter_color = marker_color;
                    ui->plotter->setTunerMarker(tuner_id, marker);

                    // Settings are now handled by TunerRowWidget - no per-tuner dock needed
                }
            }
            if (!tuner_ids.empty()) {
                ui->plotter->setMultiTunerEnabled(true);
                ui->plotter->setActiveTuner(tuner_manager->get_active_channel());
            }

            // Refresh tuner list to show current state and sync colors
            if (tuner_list_widget) {
                tuner_list_widget->refresh_tuner_list();

                // Sync color and filter width to tuner list widgets
                for (size_t i = 0; i < tuner_ids.size(); i++) {
                    channel_id tuner_id = tuner_ids[i];
                    QString prefix = QString("tuners/%1/").arg(i);
                    if (m_settings) {
                        QString color_str = m_settings->value(prefix + "color", "").toString();

                        if (!color_str.isEmpty()) {
                            QColor color(color_str);
                            // Update tuner_colors map in TunerList
                            tuner_list_widget->update_tuner_color(tuner_id, color);
                        }

                        // Sync filter width - get type-appropriate defaults
                        int default_filter_low = -5000;
                        int default_filter_high = 5000;
                        ReceiverChannel* ch = tuner_manager->get_channel_impl(tuner_id);
                        if (ch) {
                            ReceiverType rt = ch->get_backend_type();
                            switch (rt) {
                                case ReceiverType::ANALOG_WFM_MONO:
                                case ReceiverType::ANALOG_WFM_STEREO:
                                case ReceiverType::ANALOG_WFM_STEREO_OIRT:
                                    default_filter_low = -80000;
                                    default_filter_high = 80000;
                                    break;
                                case ReceiverType::ANALOG_USB:
                                    default_filter_low = 0;
                                    default_filter_high = 3000;
                                    break;
                                case ReceiverType::ANALOG_LSB:
                                    default_filter_low = -3000;
                                    default_filter_high = 0;
                                    break;
                                case ReceiverType::ANALOG_CW_U:
                                    default_filter_low = -250;
                                    default_filter_high = 750;
                                    break;
                                case ReceiverType::ANALOG_CW_L:
                                    default_filter_low = -750;
                                    default_filter_high = 250;
                                    break;
                                default:
                                    break;
                            }
                        }
                        int filter_low = m_settings->value(prefix + "filter_low", default_filter_low).toInt();
                        int filter_high = m_settings->value(prefix + "filter_high", default_filter_high).toInt();
                        tuner_list_widget->update_tuner_filter_width(tuner_id, filter_low, filter_high);
                    }
                }
            }
        }

        /* Update bypass states and sync tuner list */
        if (tuner_manager) {
            tuner_manager->update_channel_bypass_states();
        }
        if (tuner_list_widget) {
            tuner_list_widget->refresh_tuner_list();
            tuner_list_widget->set_all_tuners_running(true);

            // Sync volume/mute state from UI to MainWindow maps
            // This ensures saved settings are applied when DSP starts
            for (channel_id ch_id : tuner_manager->get_all_channels()) {
                int vol = tuner_list_widget->getTunerVolume(ch_id);
                bool muted = tuner_list_widget->getTunerMuted(ch_id);
                channel_volumes[ch_id] = vol;
                channel_muted[ch_id] = muted;


                // Apply the volume/mute to the actual channel
                ReceiverChannel* channel = tuner_manager->get_channel_impl(ch_id);
                if (channel && channel->is_enabled()) {
                    if (muted) {
                        channel->set_audio_gain(0.0f);
                    } else {
                        float channel_vol = vol / 100.0f;
                        channel->set_audio_gain(d_main_gain_linear * channel_vol);
                    }
                }
            }
        }

        /* start GUI timers */
        meter_timer->start(100);

        if (uiDockFft->fftRate())
        {
            iq_fft_timer->start(1000/uiDockFft->fftRate());
            ui->plotter->setRunningState(true);
        }
        else
        {
            iq_fft_timer->start(36e7); // 100 hours
            ui->plotter->setRunningState(false);
        }

        audio_fft_timer->start(40);

        /* update menu text and button tooltip */
        ui->actionDSP->setToolTip(tr("Stop DSP processing"));
        ui->actionDSP->setText(tr("Stop DSP"));
    }
    else
    {
        /* stop GUI timers */
        meter_timer->stop();
        iq_fft_timer->stop();
        audio_fft_timer->stop();
        rds_timer->stop();

        /* Stop TunerManager */
        if (tuner_manager) {
            tuner_manager->stop();
        }

        /* Set all tuners to stopped state */
        if (tuner_list_widget) {
            tuner_list_widget->set_all_tuners_running(false);
        }

        /* update menu text and button tooltip */
        ui->actionDSP->setToolTip(tr("Start DSP processing"));
        ui->actionDSP->setText(tr("Start DSP"));

        ui->plotter->setRunningState(false);
    }

    ui->actionDSP->setChecked(checked); //for remote control

}

/**
 * @brief Action: I/O device configurator triggered.
 *
 * This slot is activated when the user selects "I/O Devices" in the
 * menu. It activates the I/O configurator and if the user closes the
 * configurator using the OK button, the new configuration is read and
 * sent to the receiver.
 */
int MainWindow::on_actionIoConfig_triggered()
{
    qDebug() << "Configure I/O devices.";

    auto *ioconf = new CIoConfig(m_settings, devList);
    auto confres = ioconf->exec();

    if (confres == QDialog::Accepted)
    {
        bool dsp_running = ui->actionDSP->isChecked();

        if (dsp_running)
        {
            // suspend DSP while we reload settings
            on_actionDSP_triggered(false);
        }

        // Refresh LNB LO in dock widget, otherwise changes will be lost
        uiDockInputCtl->readLnbLoFromSettings(m_settings);
        storeSession();
        loadConfig(m_settings->fileName(), false, false);

        if (dsp_running)
        {
            // restsart DSP
            on_actionDSP_triggered(true);
        }
    }

    delete ioconf;

    return confres;
}


/** Run first time configurator. */
int MainWindow::firstTimeConfig()
{
    qDebug() << __func__;

    auto *ioconf = new CIoConfig(m_settings, devList);
    auto confres = ioconf->exec();

    if (confres == QDialog::Accepted)
        loadConfig(m_settings->fileName(), false, false);

    delete ioconf;

    return confres;
}


/** Load configuration activated by user. */
void MainWindow::on_actionLoadSettings_triggered()
{
    auto cfgfile = QFileDialog::getOpenFileName(this, tr("Load settings"),
                                           m_last_dir.isEmpty() ? m_cfg_dir : m_last_dir,
                                           tr("Settings (*.conf)"));

    qDebug() << "File to open:" << cfgfile;

    if (cfgfile.isEmpty())
        return;

    if (!cfgfile.endsWith(".conf", Qt::CaseSensitive))
        cfgfile.append(".conf");

    loadConfig(cfgfile, cfgfile != m_settings->fileName(), cfgfile != m_settings->fileName());

    // store last dir
    QFileInfo fi(cfgfile);
    if (m_cfg_dir != fi.absolutePath())
        m_last_dir = fi.absolutePath();
}

/** Save configuration activated by user. */
void MainWindow::on_actionSaveSettings_triggered()
{
    auto cfgfile = QFileDialog::getSaveFileName(this, tr("Save settings"),
                                           m_last_dir.isEmpty() ? m_cfg_dir : m_last_dir,
                                           tr("Settings (*.conf)"));

    qDebug() << "File to save:" << cfgfile;

    if (cfgfile.isEmpty())
        return;

    if (!cfgfile.endsWith(".conf", Qt::CaseSensitive))
        cfgfile.append(".conf");

    storeSession();
    saveConfig(cfgfile);

    // store last dir
    QFileInfo fi(cfgfile);
    if (m_cfg_dir != fi.absolutePath())
        m_last_dir = fi.absolutePath();
}

/** Show I/Q player. */
void MainWindow::on_actionIqTool_triggered()
{
    iq_tool->show();
}


/* CPlotter::NewDemodFreq() is emitted when clicking on empty space (not on a tuner marker) */
void MainWindow::on_plotter_newDemodFreq(qint64 freq, qint64 delta)
{
    Q_UNUSED(freq);
    Q_UNUSED(delta);

    // In TunerManager mode, ignore all clicks on empty space
    // Tuners can only be moved by dragging their markers directly
    // To tune to a frequency, user must add a tuner first, then drag it
    if (tuner_manager) {
        return;
    }
}

/* MultiTunerPlotter::tuneToFrequency() is emitted when dragging a tuner marker */
void MainWindow::onTunerDragged(int tuner_id, qint64 freq)
{
    if (!tuner_manager)
        return;

    auto* tuner = tuner_manager->get_channel_impl(tuner_id);
    if (!tuner)
        return;

    // Calculate DDC offset from display frequency
    // Display freq = RF + LNB + offset
    // So offset = Display freq - RF - LNB
    qint64 rf_freq = tuner_manager->get_rf_freq();
    qint64 delta = freq - rf_freq - d_lnb_lo;

    // Set the tuner's DDC offset (this is the coarse tuning)
    tuner->set_center_freq((double)delta);

    // Don't update main freqCtrl - it shows SDR center frequency, not tuner frequency
    // Tuner frequency is shown in the tuner list widget

    // Update frequency display in tuner list widget and re-sort by frequency
    if (tuner_list_widget) {
        tuner_list_widget->update_tuner_frequency(tuner_id, freq);
    }

    // The marker position is already updated by MultiTunerPlotter during drag
}

/* MultiTunerPlotter::filterResized() is emitted when filter edge is dragged */
void MainWindow::onFilterResized(int tuner_id, int filter_low, int filter_high)
{
    if (!tuner_manager)
        return;

    auto* tuner = tuner_manager->get_channel_impl(tuner_id);
    if (!tuner)
        return;


    // Apply the new filter to the receiver channel
    double tw = std::abs(filter_high - filter_low) * 0.2;  // 20% transition width
    tuner->set_filter((double)filter_low, (double)filter_high, tw);

    // Update tuner list widget bandwidth display
    if (tuner_list_widget) {
        tuner_list_widget->update_tuner_filter_width(tuner_id, filter_low, filter_high);
    }

    // Update UI if this is the active tuner
    if (tuner_id == tuner_manager->get_active_channel()) {
        ui->plotter->setHiLowCutFrequencies(filter_low, filter_high);
        uiDockRxOpt->setFilterParam(filter_low, filter_high);
    }
}

/* MultiTunerPlotter::panSdrFrequency() is emitted when shift+dragging tuner */
void MainWindow::onPanSdrFrequency(int dragged_tuner_id, qint64 new_freq)
{
    if (!tuner_manager)
        return;

    // new_freq is the display frequency (includes LNB offset)
    // Calculate hardware frequency
    qint64 new_hw_freq = new_freq - d_lnb_lo;
    qint64 old_hw_freq = d_hw_freq;

    // Recalculate DDC offsets for all tuners EXCEPT the dragged one
    // - Dragged tuner: stays at same screen position (DDC offset unchanged, absolute freq changes)
    // - Other tuners: keep same absolute frequency (DDC offset recalculated, they move on screen)
    auto all_ids = tuner_manager->get_all_channels();
    for (channel_id id : all_ids) {
        if (id == dragged_tuner_id)
            continue;  // Skip the dragged tuner - its DDC offset stays the same

        auto* tuner = tuner_manager->get_channel_impl(id);
        if (tuner) {
            // Get current absolute frequency
            qint64 abs_freq = old_hw_freq + d_lnb_lo + (qint64)tuner->get_center_freq();

            // Calculate new DDC offset to maintain same absolute frequency
            double new_offset = (double)(abs_freq - new_hw_freq - d_lnb_lo);
            tuner->set_center_freq(new_offset);
        }
    }

    // Update the dragged tuner's absolute frequency in the plotter marker
    // (its DDC offset is unchanged, so its absolute freq = new_hw_freq + LNB + old_offset)
    auto* dragged = tuner_manager->get_channel_impl(dragged_tuner_id);
    if (dragged) {
        qint64 new_abs_freq = new_hw_freq + d_lnb_lo + (qint64)dragged->get_center_freq();
        ui->plotter->updateTunerFrequency(dragged_tuner_id, new_abs_freq);
        if (tuner_list_widget) {
            tuner_list_widget->update_tuner_frequency(dragged_tuner_id, new_abs_freq);
        }
    }

    // Update SDR hardware frequency
    d_hw_freq = new_hw_freq;
    tuner_manager->set_rf_freq((double)new_hw_freq);

    // Update plotter center frequency
    ui->plotter->setCenterFreq(new_freq);

    // Update UI elements
    ui->freqCtrl->setFrequency(new_freq);
    uiDockRxOpt->setHwFreq(d_hw_freq);
}

/* CPlotter::NewfilterFreq() is emitted or bookmark activated */
void MainWindow::on_plotter_newFilterFreq(int low, int high)
{   /* parameter correctness will be checked in receiver class */
    receiver::status retcode = rx->set_filter((double) low, (double) high, d_filter_shape);

    /* Update filter range of plotter, in case this slot is triggered by
     * switching to a bookmark */
    ui->plotter->setHiLowCutFrequencies(low, high);

    if (retcode == receiver::STATUS_OK)
        uiDockRxOpt->setFilterParam(low, high);

    // Update active tuner marker filter bounds
    if (tuner_manager) {
        channel_id active_id = tuner_manager->get_active_channel();
        if (active_id >= 0) {
            ui->plotter->updateTunerFilter(active_id, low, high);
        }
    }
}

/** Full screen button or menu item toggled. */
void MainWindow::on_actionFullScreen_triggered(bool checked)
{
    if (checked)
    {
        ui->statusBar->hide();
        showFullScreen();
    }
    else
    {
        ui->statusBar->show();
        showNormal();
    }
}

/** Remote control button (or menu item) toggled. */
void MainWindow::on_actionRemoteControl_triggered(bool checked)
{
    if (checked)
        remote->start_server();
    else
        remote->stop_server();
}

/** Remote control configuration button (or menu item) clicked. */
void MainWindow::on_actionRemoteConfig_triggered()
{
    auto *rcs = new RemoteControlSettings();

    rcs->setPort(remote->getPort());
    rcs->setHosts(remote->getHosts());

    if (rcs->exec() == QDialog::Accepted)
    {
        remote->setPort(rcs->getPort());
        remote->setHosts(rcs->getHosts());
    }

    delete rcs;
}


#define DATA_BUFFER_SIZE 48000

/**
 * AFSK1200 decoder action triggered.
 *
 * This slot is called when the user activates the AFSK1200
 * action. It will create an AFSK1200 decoder window and start
 * and start pushing data from the receiver to it.
 */
void MainWindow::on_actionAFSK1200_triggered()
{
    if (!d_have_audio)
    {
        QMessageBox msg_box;
        msg_box.setIcon(QMessageBox::Critical);
        msg_box.setText(tr("AFSK1200 decoder requires a demodulator.\n"
                           "Currently, demodulation is switched off "
                           "(Mode->Demod Off)."));
        msg_box.exec();
        return;
    }
    if (dec_afsk1200 != nullptr)
    {
        qDebug() << "AFSK1200 decoder already active.";
        dec_afsk1200->raise();
    }
    else
    {
        qDebug() << "Starting AFSK1200 decoder.";

        /* start sample sniffer */
        if (rx->start_sniffer(22050, DATA_BUFFER_SIZE) == receiver::STATUS_OK)
        {
            dec_afsk1200 = new Afsk1200Win(this);
            connect(dec_afsk1200, SIGNAL(windowClosed()), this, SLOT(afsk1200win_closed()));
            dec_afsk1200->setAttribute(Qt::WA_DeleteOnClose);
            dec_afsk1200->show();

            dec_timer->start(100);
        }
        else
            QMessageBox::warning(this, tr("Gqrx error"),
                                 tr("Error starting sample sniffer.\n"
                                    "Close all data decoders and try again."),
                                 QMessageBox::Ok, QMessageBox::Ok);
    }
}


/**
 * Destroy AFSK1200 decoder window got closed.
 *
 * This slot is connected to the windowClosed() signal of the AFSK1200 decoder
 * object. We need this to properly destroy the object, stop timeout and clean
 * up whatever need to be cleaned up.
 */
void MainWindow::afsk1200win_closed()
{
    /* stop cyclic processing */
    dec_timer->stop();
    rx->stop_sniffer();

    dec_afsk1200 = nullptr;
}

/** Show DXC Options. */
void MainWindow::on_actionDX_Cluster_triggered()
{
    dxc_options->show();
}

/**
 * Cyclic processing for acquiring samples from receiver and processing them
 * with data decoders (see dec_* objects)
 */
void MainWindow::decoderTimeout()
{
    float buffer[DATA_BUFFER_SIZE];
    unsigned int num;

    rx->get_sniffer_data(&buffer[0], num);
    if (dec_afsk1200)
        dec_afsk1200->process_samples(&buffer[0], num);
}

void MainWindow::setRdsDecoder(bool checked)
{
    if (checked)
    {
        qDebug() << "Starting RDS decoder.";
        uiDockRDS->showEnabled();
        rx->start_rds_decoder();
        rx->reset_rds_parser();
        rds_timer->start(250);
    }
    else
    {
        qDebug() << "Stopping RDS decoder.";
        uiDockRDS->showDisabled();
        rx->stop_rds_decoder();
        rds_timer->stop();
    }
    remote->setRDSstatus(checked);
}

void MainWindow::onBookmarkActivated(qint64 freq, const QString& demod, int bandwidth)
{
    setNewFrequency(freq);
    selectDemod(demod);

    /* Check if filter is symmetric or not by checking the presets */
    auto mode = uiDockRxOpt->currentDemod();
    auto preset = uiDockRxOpt->currentFilter();

    int lo, hi;
    uiDockRxOpt->getFilterPreset(mode, preset, &lo, &hi);

    if(lo + hi == 0)
    {
        lo = -bandwidth / 2;
        hi =  bandwidth / 2;
    }
    else if(lo >= 0 && hi >= 0)
    {
        hi = lo + bandwidth;
    }
    else if(lo <= 0 && hi <= 0)
    {
        lo = hi - bandwidth;
    }

    on_plotter_newFilterFreq(lo, hi);
}

void MainWindow::setPassband(int bandwidth)
{
    /* Check if filter is symmetric or not by checking the presets */
    auto mode = uiDockRxOpt->currentDemod();
    auto preset = uiDockRxOpt->currentFilter();

    int lo, hi;
    uiDockRxOpt->getFilterPreset(mode, preset, &lo, &hi);

    if(lo + hi == 0)
    {
        lo = -bandwidth / 2;
        hi =  bandwidth / 2;
    }
    else if(lo >= 0 && hi >= 0)
    {
        hi = lo + bandwidth;
    }
    else if(lo <= 0 && hi <= 0)
    {
        lo = hi - bandwidth;
    }

    remote->setPassband(lo, hi);

    on_plotter_newFilterFreq(lo, hi);
}

/** Launch Gqrx google group website. */
void MainWindow::on_actionUserGroup_triggered()
{
    auto res = QDesktopServices::openUrl(QUrl("https://groups.google.com/g/gqrx",
                                              QUrl::TolerantMode));
    if (!res)
        QMessageBox::warning(this, tr("Error"),
                             tr("Failed to open website:\n"
                                "https://groups.google.com/g/gqrx"),
                             QMessageBox::Close);
}

/**
 * Show ftxt in a dialog window.
 */
void MainWindow::on_actionNews_triggered()
{
    showSimpleTextFile(":/textfiles/news.txt", tr("Release news"));
}

/**
 * Show remote-contol.txt in a dialog window.
 */
void MainWindow::on_actionRemoteProtocol_triggered()
{
    showSimpleTextFile(":/textfiles/remote-control.txt",
                       tr("Remote control protocol"));
}

/**
 * Show kbd-shortcuts.txt in a dialog window.
 */
void MainWindow::on_actionKbdShortcuts_triggered()
{
    showSimpleTextFile(":/textfiles/kbd-shortcuts.txt",
                       tr("Keyboard shortcuts"));
}

/**
 * Show simple text file in a window.
 */
void MainWindow::showSimpleTextFile(const QString &resource_path,
                                    const QString &window_title)
{
    QResource resource(resource_path);
    QFile news(resource.absoluteFilePath());

    if (!news.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "Unable to open file: " << news.fileName() <<
                    " because of error " << news.errorString();

        return;
    }

    QTextStream in(&news);
    auto content = in.readAll();
    news.close();

    auto *browser = new QTextBrowser();
    browser->setLineWrapMode(QTextEdit::NoWrap);
    browser->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    browser->append(content);
    browser->adjustSize();

    // scroll to the beginning
    auto cursor = browser->textCursor();
    cursor.setPosition(0);
    browser->setTextCursor(cursor);


    auto *layout = new QVBoxLayout();
    layout->addWidget(browser);

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(window_title);
    dialog->setLayout(layout);
    dialog->resize(800, 400);
    dialog->exec();

    delete dialog;
    // browser and layout deleted automatically
}

/**
 * @brief Slot for handling loadConfig signals
 * @param cfgfile
 */
void MainWindow::loadConfigSlot(const QString &cfgfile)
{
    loadConfig(cfgfile, cfgfile != m_settings->fileName(), cfgfile != m_settings->fileName());
}

/**
 * @brief Action: About gqrx.
 *
 * This slot is called when the user activates the
 * Help|About menu item (or Gqrx|About on Mac)
 */
void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, tr("About Gqrx"),
        tr("<p>This is Gqrx %1</p>"
           "<p>Copyright (C) 2011-2024 Alexandru Csete & contributors.</p>"
           "<p>Gqrx is a software defined radio (SDR) receiver powered by "
           "<a href='https://www.gnuradio.org/'>GNU Radio</a> and the Qt toolkit. "
           "<p>Gqrx uses the <a href='https://osmocom.org/projects/gr-osmosdr/wiki/GrOsmoSDR'>GrOsmoSDR</a> "
           "input source block and works with any input device supported by it, including "
           "Funcube Dongle, RTL-SDR, Airspy, HackRF, RFSpace, BladeRF and USRP receivers."
           "</p>"
           "<p>You can download the latest version from the "
           "<a href='https://gqrx.dk/'>Gqrx website</a>."
           "</p>"
           "<p>"
           "Gqrx is licensed under the <a href='https://www.gnu.org/licenses/gpl-3.0.html'>GNU General Public License</a>."
           "</p>").arg(VERSION));
}

/**
 * @brief Action: About Qt
 *
 * This slot is called when the user activates the
 * Help|About Qt menu item (or Gqrx|About Qt on Mac)
 */
void MainWindow::on_actionAboutQt_triggered()
{
    QMessageBox::aboutQt(this, tr("About Qt"));
}

void MainWindow::on_actionAddBookmark_triggered()
{
    bool ok=false;
    QString name;
    QStringList tags;

    // Create and show the Dialog for a new Bookmark.
    // Write the result into variable 'name'.
    {
        QDialog dialog(this);
        dialog.setWindowTitle("New bookmark");

        auto* LabelAndTextfieldName = new QGroupBox(&dialog);
        auto* label1 = new QLabel("Bookmark name:", LabelAndTextfieldName);
        auto* textfield = new QLineEdit(LabelAndTextfieldName);
        auto *layout = new QHBoxLayout;
        layout->addWidget(label1);
        layout->addWidget(textfield);
        LabelAndTextfieldName->setLayout(layout);

        auto* buttonCreateTag = new QPushButton("Create new Tag", &dialog);

        auto* taglist = new BookmarksTagList(&dialog, false);
        taglist->updateTags();
        taglist->DeselectAll();

        auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                              | QDialogButtonBox::Cancel);
        connect(buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
        connect(buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));
        connect(buttonCreateTag, SIGNAL(clicked()), taglist, SLOT(AddNewTag()));

        auto *mainLayout = new QVBoxLayout(&dialog);
        mainLayout->addWidget(LabelAndTextfieldName);
        mainLayout->addWidget(buttonCreateTag);
        mainLayout->addWidget(taglist);
        mainLayout->addWidget(buttonBox);

        ok = dialog.exec();
        if (ok)
        {
            name = textfield->text();
            tags = taglist->getSelectedTags();
            qDebug() << "Tags: " << tags;
        }
        else
        {
            name.clear();
            tags.clear();
        }
    }

    // Add new Bookmark to Bookmarks.
    if(ok)
    {
        int i;

        BookmarkInfo info;
        info.frequency = ui->freqCtrl->getFrequency();
        info.bandwidth = ui->plotter->getFilterBw();
        info.modulation = uiDockRxOpt->currentDemodAsString();
        info.name=name;
        info.tags.clear();
        if (tags.empty())
            info.tags.append(Bookmarks::Get().findOrAddTag(""));


        for (i = 0; i < tags.size(); ++i)
            info.tags.append(Bookmarks::Get().findOrAddTag(tags[i]));

        Bookmarks::Get().add(info);
        uiDockBookmarks->updateTags();
    }
}

void MainWindow::updateClusterSpots()
{
    ui->plotter->updateOverlay();
}

void MainWindow::frequencyFocusShortcut()
{
    ui->freqCtrl->setFrequencyFocus();
}

void MainWindow::rxOffsetZeroShortcut()
{
    uiDockRxOpt->setFilterOffset(0);
}

void MainWindow::enableMarkers(bool enabled)
{
    d_show_markers = enabled;
    ui->markerFrame->setVisible(d_show_markers);
}

void MainWindow::toggleMarkers()
{
    enableMarkers(!d_show_markers);
    uiDockFft->setMarkersEnabled(d_show_markers);
}

void MainWindow::onTunerRemoved(int tuner_id)
{
    if (!tuner_manager) {
        return;
    }

    // Remove marker from plotter first (before destroy_channel)
    ui->plotter->removeTunerMarker(tuner_id);

    // Use IChannelManager API
    tuner_manager->destroy_channel(tuner_id);

    // Refresh the tuner list UI
    if (tuner_list_widget) {
        tuner_list_widget->refresh_tuner_list();
    }

    // Select first available tuner if needed, or disable multi-tuner mode
    auto all_channels = tuner_manager->get_all_channels();
    if (!all_channels.empty()) {
        tuner_manager->set_active_channel(all_channels[0]);
        ui->plotter->setActiveTuner(all_channels[0]);
    } else {
        // No tuners left - disable multi-tuner display
        ui->plotter->setMultiTunerEnabled(false);
    }
}

void MainWindow::addTuner()
{
    // Default to NFM
    addTunerWithType(ReceiverType::ANALOG_NFM);
}

void MainWindow::addTunerWithType(ReceiverType type)
{
    if (!tuner_manager) {
        QMessageBox::warning(this, tr("Error"), tr("No tuner manager available"));
        return;
    }

    // Get input rate from tuner manager (now owns SDR)
    double input_rate = tuner_manager->get_input_rate();
    unsigned int input_decim = tuner_manager->get_input_decim();

    if (input_rate <= 0) {
        QMessageBox::warning(this, tr("Error"),
            tr("SDR not connected or invalid sample rate.\nInput rate: %1").arg(input_rate));
        return;
    }

    tuner_manager->set_input_rate(input_rate);
    tuner_manager->set_input_decim(input_decim);

    // Use new IChannelManager API
    channel_id new_tuner_id = tuner_manager->create_channel(ChannelType::MANUAL, type);
    if (new_tuner_id >= 0) {
        tuner_manager->set_active_channel(new_tuner_id);

        // Set tuner's DDC offset to center of visible FFT display
        auto* tuner = tuner_manager->get_channel_impl(new_tuner_id);
        if (tuner) {
            // Get the center of the currently visible FFT display
            // plotter->getCenterFreq() = display center (includes LNB offset)
            // plotter->getFftCenterFreq() = offset from center when zoomed
            qint64 visible_center = ui->plotter->getCenterFreq() + ui->plotter->getFftCenterFreq();
            qint64 rf_freq = tuner_manager->get_rf_freq();

            // DDC offset = visible center - (rf + lnb)
            double ddc_offset = (double)(visible_center - rf_freq - d_lnb_lo);
            tuner->set_center_freq(ddc_offset);

            // Set initial audio gain (main gain * default channel volume at -15dB)
            float default_vol = 0.18f;  // -15dB = 10^(-15/20) ≈ 0.178
            tuner->set_audio_gain(d_main_gain_linear * default_vol);

            // Add to channel volumes map at default 18%
            channel_volumes[new_tuner_id] = 18;
        }

        // Update frequency display to show tuner frequency (visible center)
        qint64 tuner_freq = ui->plotter->getCenterFreq() + ui->plotter->getFftCenterFreq();
        ui->freqCtrl->setFrequency(tuner_freq);

        // Determine default filter width and max width based on type
        int filter_low = -6250;   // Default NFM
        int filter_high = 6250;
        int max_filter_half_width = 10000;  // Default NFM max

        switch (type) {
        case ReceiverType::ANALOG_NFM:
            filter_low = -6250;
            filter_high = 6250;
            max_filter_half_width = 10000;
            break;
        case ReceiverType::ANALOG_AM:
        case ReceiverType::ANALOG_AMSYNC:
            filter_low = -5000;
            filter_high = 5000;
            max_filter_half_width = 10000;
            break;
        case ReceiverType::ANALOG_WFM_MONO:
        case ReceiverType::ANALOG_WFM_STEREO:
        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
            filter_low = -80000;
            filter_high = 80000;
            max_filter_half_width = 100000;
            break;
        case ReceiverType::ANALOG_USB:
            filter_low = 0;
            filter_high = 3000;
            max_filter_half_width = 4000;
            break;
        case ReceiverType::ANALOG_LSB:
            filter_low = -3000;
            filter_high = 0;
            max_filter_half_width = 4000;
            break;
        case ReceiverType::ANALOG_CW_U:
        case ReceiverType::ANALOG_CW_L:
            filter_low = -250;
            filter_high = 250;
            max_filter_half_width = 1000;
            break;
        default:
            break;
        }

        // Add marker to plotter for the new tuner at center frequency
        MultiTunerPlotter::TunerMarker marker;
        marker.tuner_id = new_tuner_id;
        marker.name = QString("Tuner %1").arg(new_tuner_id);
        marker.frequency = tuner_freq;
        marker.filter_low = filter_low;
        marker.filter_high = filter_high;
        marker.max_filter_half_width = max_filter_half_width;
        marker.enabled = true;
        marker.active = true;
        marker.color = ui->plotter->getDefaultTunerColor(new_tuner_id);
        ui->plotter->setTunerMarker(new_tuner_id, marker);
        ui->plotter->setActiveTuner(new_tuner_id);
        ui->plotter->setMultiTunerEnabled(true);


        // Apply the type-based filter settings to the plotter marker
        ui->plotter->updateTunerFilter(new_tuner_id, filter_low, filter_high);

        // Settings are now handled by TunerRowWidget - no per-tuner dock needed

        // Refresh the tuner list UI and sync filter width
        if (tuner_list_widget) {
            tuner_list_widget->refresh_tuner_list();
            tuner_list_widget->update_tuner_filter_width(new_tuner_id, filter_low, filter_high);
        }
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Failed to add tuner"));
    }
}

void MainWindow::removeTuner()
{
    if (!tuner_manager) {
        return;
    }

    // Use new IChannelManager API
    channel_id active_tuner = tuner_manager->get_active_channel();

    if (active_tuner < 0) {
        QMessageBox::information(this, tr("Cannot Remove"),
                                tr("No tuner selected."));
        return;
    }

    int ret = QMessageBox::question(this, tr("Remove Tuner"),
                                   QString("Remove tuner %1?").arg(active_tuner),
                                   QMessageBox::Yes | QMessageBox::No);

    if (ret == QMessageBox::Yes) {

        // Remove marker from plotter first (before destroy_channel)
        ui->plotter->removeTunerMarker(active_tuner);

        // Use new IChannelManager API to destroy channel
        tuner_manager->destroy_channel(active_tuner);

        // Update tuner list display
        if (tuner_list_widget) {
            tuner_list_widget->refresh_tuner_list();
        }

        // Disable multi-tuner display if only one tuner left
        if (tuner_manager->get_active_channel_count() <= 1) {
            ui->plotter->setMultiTunerEnabled(false);
        }
    }
}

// Per-tuner docks removed - settings now consolidated in TunerRowWidget

void MainWindow::onTunerEnabledChanged(int tuner_id, bool enabled)
{
    if (tuner_manager) {
        ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
        if (channel) {
            channel->set_enabled(enabled);

            // Update status in tuner list
            if (enabled) {
                // Restore the user's volume setting, respecting mute state
                auto mute_it = channel_muted.find(tuner_id);
                bool muted = (mute_it != channel_muted.end()) ? mute_it->second : false;
                auto vol_it = channel_volumes.find(tuner_id);
                float channel_vol = (vol_it != channel_volumes.end()) ? vol_it->second / 100.0f : 1.0f;

                if (!muted) {
                    float final_gain = d_main_gain_linear * channel_vol;
                    channel->set_audio_gain(final_gain);
                } else {
                    channel->set_audio_gain(0.0f);
                }

                // Determine correct status based on DSP state and bypass
                TunerStatus status;
                bool running = tuner_manager->is_running();
                bool bypassed = channel->is_bypassed();
                if (!running) {
                    status = TunerStatus::Stopped;
                } else if (bypassed) {
                    status = TunerStatus::Bypassed;
                } else {
                    status = TunerStatus::Running;
                }
                tuner_list_widget->update_tuner_status(tuner_id, status);
            }
            // When disabling, the TunerRowWidget already sets status to Disabled
        }
    }
}

void MainWindow::onTunerNameChanged(int tuner_id, const QString& name)
{

    // Update the marker name on the plotter
    ui->plotter->setTunerMarkerName(tuner_id, name);
}

void MainWindow::onTunerTypeChanged(int tuner_id, ReceiverType type)
{

    if (!tuner_manager) {
        qWarning() << "onTunerTypeChanged: No tuner manager available";
        return;
    }

    ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel) {
        qWarning() << "onTunerTypeChanged: Channel" << tuner_id << "not found";
        return;
    }

    // Get rates needed for backend creation
    double quad_rate = channel->get_quad_rate();
    int audio_rate = 48000;  // Standard audio rate

    // Create new backend of the requested type
    auto backend = ReceiverBackendFactory::create(type, quad_rate, audio_rate);
    if (!backend) {
        qWarning() << "onTunerTypeChanged: Failed to create backend for type" << static_cast<int>(type);
        return;
    }

    // Switch to the new backend
    channel->set_backend(std::move(backend));

    // Update filter width based on mode
    int filter_low = -6250;   // Default NFM
    int filter_high = 6250;

    switch (type) {
    case ReceiverType::ANALOG_NFM:
        filter_low = -6250;
        filter_high = 6250;
        break;
    case ReceiverType::ANALOG_AM:
    case ReceiverType::ANALOG_AMSYNC:
        filter_low = -5000;
        filter_high = 5000;
        break;
    case ReceiverType::ANALOG_WFM_MONO:
    case ReceiverType::ANALOG_WFM_STEREO:
    case ReceiverType::ANALOG_WFM_STEREO_OIRT:
        filter_low = -80000;
        filter_high = 80000;
        break;
    case ReceiverType::ANALOG_USB:
        filter_low = 0;
        filter_high = 3000;
        break;
    case ReceiverType::ANALOG_LSB:
        filter_low = -3000;
        filter_high = 0;
        break;
    case ReceiverType::ANALOG_CW_U:
        filter_low = -100;
        filter_high = 900;
        break;
    case ReceiverType::ANALOG_CW_L:
        filter_low = -900;
        filter_high = 100;
        break;
    default:
        break;
    }

    channel->set_filter_width(filter_low, filter_high);

    // Update plotter marker with new filter width
    ui->plotter->updateTunerFilter(tuner_id, filter_low, filter_high);

}

void MainWindow::onTunerColorChanged(int tuner_id, const QColor& color)
{

    // Update the marker color on the plotter
    ui->plotter->setTunerMarkerColor(tuner_id, color);
}

void MainWindow::onTunerAlphaChanged(int tuner_id, int alpha)
{

    // Update the marker alpha on the plotter
    ui->plotter->setTunerMarkerAlpha(tuner_id, alpha);
}

void MainWindow::onTunerVolumeChanged(int tuner_id, int volume)
{

    // Store the channel volume
    channel_volumes[tuner_id] = volume;

    if (!tuner_manager)
    {
        return;
    }

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
    {
        return;
    }

    bool enabled = channel->is_enabled();
    bool bypassed = channel->is_bypassed();

    // Only apply gain if tuner is enabled, not bypassed, and not muted
    if (enabled && !bypassed) {
        // Check if channel is muted
        auto it = channel_muted.find(tuner_id);
        bool muted = (it != channel_muted.end()) ? it->second : false;
        if (!muted) {
            // Apply combined gain (main * channel)
            float channel_vol = volume / 100.0f;
            float final_gain = d_main_gain_linear * channel_vol;
            channel->set_audio_gain(final_gain);
        }
    }
    // Disabled or bypassed tuners stay muted
}

void MainWindow::onTunerMuteToggled(int tuner_id, bool muted)
{

    // Store the mute state
    channel_muted[tuner_id] = muted;

    if (!tuner_manager)
    {
        return;
    }

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
    {
        return;
    }

    if (muted) {
        // Mute - set gain to 0
        channel->set_audio_gain(0.0f);
    } else {
        bool enabled = channel->is_enabled();
        bool bypassed = channel->is_bypassed();
        // Unmute - restore volume if enabled and not bypassed
        if (enabled && !bypassed) {
            auto it = channel_volumes.find(tuner_id);
            float channel_vol = (it != channel_volumes.end()) ? it->second / 100.0f : 1.0f;
            float final_gain = d_main_gain_linear * channel_vol;
            channel->set_audio_gain(final_gain);
        }
    }
}

void MainWindow::onTunerRecordingToggled(int tuner_id, bool recording)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    if (recording) {
        // Get global and per-tuner recording config
        const auto& config = tuner_manager->getRecordingConfig();
        const auto tunerConfig = tuner_manager->getTunerRecordingConfig(tuner_id);

        // Build base filename params
        FilenameParams params;
        params.timestamp = QDateTime::currentDateTime();
        params.frequency_hz = channel->get_filter_offset() + tuner_manager->get_rf_freq();
        // Get actual mode from channel
        ReceiverChannel::rx_demod demod = channel->get_demod();
        switch (demod) {
            case ReceiverChannel::RX_DEMOD_OFF: params.mode = "Off"; break;
            case ReceiverChannel::RX_DEMOD_AM: params.mode = "AM"; break;
            case ReceiverChannel::RX_DEMOD_AMSYNC: params.mode = "AM-Sync"; break;
            case ReceiverChannel::RX_DEMOD_NFM: params.mode = "NFM"; break;
            case ReceiverChannel::RX_DEMOD_WFM_M: params.mode = "WFM-Mono"; break;
            case ReceiverChannel::RX_DEMOD_WFM_S: params.mode = "WFM-Stereo"; break;
            case ReceiverChannel::RX_DEMOD_WFM_S_OIRT: params.mode = "WFM-OIRT"; break;
            case ReceiverChannel::RX_DEMOD_SSB: params.mode = "SSB"; break;
            case ReceiverChannel::RX_DEMOD_NONE: params.mode = "Raw"; break;
            default: params.mode = "Unknown"; break;
        }
        params.tuner_id = tuner_id;
        params.tuner_name = QString::fromStdString(channel->get_channel_name());
        if (params.tuner_name.isEmpty()) {
            params.tuner_name = QString("Tuner%1").arg(tuner_id);
        }

        // Ensure folder exists
        FilenameTemplate::ensureFolder(config.recording_folder);

        bool anyStarted = false;

        // Start IQ recording if enabled
        if (tunerConfig.record_iq) {
            params.type = "iq";
            // Get sample rate based on tap point
            if (config.iq_tap_point == IqTapPoint::AFTER_FILTER) {
                params.sample_rate = channel->get_backend() ? channel->get_backend()->get_filtered_iq_rate() : 96000;
            } else {
                params.sample_rate = channel->get_quad_rate();
            }

            QString iqPath = FilenameTemplate::buildPath(
                config.recording_folder,
                config.filename_template,
                params,
                ""
            );

            // Apply IQ settings
            channel->set_iq_tap_point(config.iq_tap_point);
            channel->set_iq_recording_format(config.iq_format);
            channel->set_iq_recording_center_freq(params.frequency_hz);
            channel->set_sigmf_config(config.sigmf);
            channel->set_iq_recording_mode(tunerConfig.iq_mode);
            channel->set_iq_split_minutes(config.auto_split_minutes);
            channel->set_iq_pre_buffer_ms(config.squelch_config.pre_buffer_ms);

            if (channel->start_iq_recording(iqPath) == ReceiverChannel::STATUS_OK) {
                anyStarted = true;
            } else {
                qWarning() << "Failed to start IQ recording for tuner" << tuner_id;
            }
        }

        // Start audio recording if enabled
        if (tunerConfig.record_audio) {
            params.type = "audio";
            params.sample_rate = 48000;

            QString audioPath = FilenameTemplate::buildPath(
                config.recording_folder,
                config.filename_template,
                params,
                ""
            );

            channel->set_audio_recording_format(config.audio_format);
            channel->set_audio_recording_wav_format(config.wav_sample_format);
            channel->set_audio_recording_mode(tunerConfig.audio_mode);
            channel->set_audio_squelch_config(
                tunerConfig.use_custom_squelch_config ? tunerConfig.squelch_config : config.squelch_config
            );
            channel->set_audio_split_minutes(config.auto_split_minutes);

            if (channel->start_new_audio_recording(audioPath) == ReceiverChannel::STATUS_OK) {
                anyStarted = true;
            } else {
                qWarning() << "Failed to start audio recording for tuner" << tuner_id;
            }
        }

        // Update UI if nothing started
        if (!anyStarted && tuner_list_widget) {
            tuner_list_widget->update_tuner_recording(tuner_id, false);
        }
    } else {
        // Stop all recording
        channel->stop_iq_recording();
        channel->stop_new_audio_recording();
    }
}

void MainWindow::onTunerCenterRequested(int tuner_id)
{

    if (!tuner_manager || !tuner_list_widget)
        return;

    // Get tuner status and frequency from the UI widget (works even before channels exist)
    TunerStatus row_status = tuner_list_widget->getTunerStatus(tuner_id);
    qint64 tuner_freq = tuner_list_widget->getTunerFrequency(tuner_id);

    // Check if tuner is out of SDR range (bypassed or disabled due to being out of range)
    ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
    bool is_out_of_range = (row_status == TunerStatus::Bypassed) ||
                           (row_status == TunerStatus::Disabled) ||
                           (channel && channel->is_bypassed());

    if (is_out_of_range && tuner_freq > 0) {

        // Retune SDR to the tuner's frequency
        setNewFrequency(tuner_freq);

        // Re-fetch channel after retune
        channel = tuner_manager->get_channel_impl(tuner_id);
    }

    // If still no channel, we can't center
    if (!channel)
        return;

    // Get tuner offset and center the FFT view on it (no zoom change)
    qint64 fft_offset = (qint64)channel->get_freq_offset();
    ui->plotter->setFftCenterFreq(fft_offset);
}

void MainWindow::onTunerZoomRequested(int tuner_id)
{

    if (!tuner_manager || !tuner_list_widget)
        return;

    // Get tuner info
    TunerStatus row_status = tuner_list_widget->getTunerStatus(tuner_id);
    qint64 tuner_freq = tuner_list_widget->getTunerFrequency(tuner_id);

    // Check if tuner is out of range and retune if needed
    ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
    bool is_out_of_range = (row_status == TunerStatus::Bypassed) ||
                           (row_status == TunerStatus::Disabled) ||
                           (channel && channel->is_bypassed());

    if (is_out_of_range && tuner_freq > 0) {
        setNewFrequency(tuner_freq);
        channel = tuner_manager->get_channel_impl(tuner_id);
    }

    if (!channel)
        return;

    // Use a default channel width (10 kHz) for zoom calculation
    // This works well for most narrowband modes
    int channel_width = 10000;

    // Get current span and sample rate from plotter
    qint64 current_span = ui->plotter->getSpan();
    qint64 sample_rate = (qint64)ui->plotter->getSampleRate();

    // Define zoom levels as span widths (in Hz)
    // Channel fit (2x channel width), then progressively wider views
    QVector<qint64> zoom_spans;
    zoom_spans << qMax(channel_width * 2, 20000);   // Fit channel (min 20 kHz)
    zoom_spans << 50000;                             // 50 kHz
    zoom_spans << 100000;                            // 100 kHz
    zoom_spans << 250000;                            // 250 kHz
    zoom_spans << 500000;                            // 500 kHz
    zoom_spans << 1000000;                           // 1 MHz
    zoom_spans << sample_rate;                       // Full bandwidth

    // Find closest zoom level and go to next
    int next_idx = 0;
    for (int i = 0; i < zoom_spans.size(); i++) {
        if (current_span <= zoom_spans[i] * 1.1) {  // Within 10% of this level
            next_idx = (i + 1) % zoom_spans.size();  // Cycle to next
            break;
        }
        next_idx = 0;  // If current span > all levels, start from beginning
    }

    qint64 new_span = zoom_spans[next_idx];

    // Center on channel and set new span
    qint64 fft_offset = (qint64)channel->get_freq_offset();
    ui->plotter->setFftCenterFreq(fft_offset);
    ui->plotter->setSpanFreq(new_span);
}

void MainWindow::onTunerFrequencyChanged(int tuner_id, qint64 freq)
{

    if (!tuner_manager)
        return;

    ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // Calculate new offset from absolute frequency
    // freq is in display coordinates (includes LNB offset)
    // rf_freq is hardware frequency
    // offset = display_freq - hardware_rf - lnb_lo
    qint64 rf_freq = (qint64)tuner_manager->get_rf_freq();
    qint64 new_offset = freq - rf_freq - d_lnb_lo;

    // Update channel offset
    channel->set_freq_offset(new_offset);

    // Calculate if tuner is within valid bandwidth and update bypass state
    double sample_rate = tuner_manager->get_input_rate();
    qint64 max_offset = (qint64)(sample_rate / 2.0 * 0.95);  // 95% of half sample rate
    bool in_range = std::abs(new_offset) <= max_offset;
    channel->set_bypassed(!in_range);

    // Update the FFT marker visibility
    ui->plotter->updateTunerFrequency(tuner_id, freq);
    ui->plotter->setTunerMarkerEnabled(tuner_id, in_range);

    // Update bypass status in tuner list and handle audio
    if (tuner_manager->is_running()) {
        TunerStatus status;
        if (!channel->is_enabled()) {
            status = TunerStatus::Disabled;
            channel->set_audio_gain(0.0f);
        } else if (!in_range) {
            status = TunerStatus::Bypassed;
            channel->set_audio_gain(0.0f);  // Mute when bypassed
        } else {
            status = TunerStatus::Running;
            // Restore audio if not muted
            auto mute_it = channel_muted.find(tuner_id);
            bool is_muted = (mute_it != channel_muted.end() && mute_it->second);
            if (!is_muted) {
                auto vol_it = channel_volumes.find(tuner_id);
                // channel_volumes stores integer percentage (0-100), convert to float (0.0-1.0)
                float vol = (vol_it != channel_volumes.end()) ? vol_it->second / 100.0f : 0.18f;
                channel->set_audio_gain(vol);
            }
        }
        if (tuner_list_widget) {
            tuner_list_widget->update_tuner_status(tuner_id, status);
        }
    }

    // Don't update main freqCtrl - it shows SDR center frequency, not tuner frequency
}

void MainWindow::onTunerFilterWidthChanged(int tuner_id, int filter_low, int filter_high)
{

    if (!tuner_manager)
        return;

    ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // Set filter on the channel
    channel->set_filter((double)filter_low, (double)filter_high, receiver::FILTER_SHAPE_NORMAL);

    // Update the FFT marker filter display
    ui->plotter->updateTunerFilter(tuner_id, filter_low, filter_high);
}

/**
 * @brief Tuner squelch level changed.
 * @param tuner_id The tuner ID.
 * @param level_db The new squelch level in dBFS.
 */
void MainWindow::onTunerSquelchChanged(int tuner_id, double level_db)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_sql_level(level_db);
}

/**
 * @brief Tuner auto squelch requested.
 * @param tuner_id The tuner ID.
 */
void MainWindow::onTunerAutoSquelchRequested(int tuner_id)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // Get current signal level and add 3dB headroom
    float signal_level = channel->get_signal_level();
    double level = (double)signal_level + 3.0;

    // Avoid setting to 0 dBFS
    if (level > -10.0) {
        level = channel->get_sql_level();
    }

    channel->set_sql_level(level);

    // Update the UI widget
    auto it = tuner_list_widget->findChildren<TunerRowWidget*>();
    for (auto* row : it) {
        if (row->tuner_id() == tuner_id) {
            row->setSquelch(level);
            break;
        }
    }
}

/**
 * @brief Tuner filter preset changed.
 * @param tuner_id The tuner ID.
 * @param preset The filter preset (0=Wide, 1=Normal, 2=Narrow, 3=User).
 */
void MainWindow::onTunerFilterPresetChanged(int tuner_id, int preset)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // Filter preset table for NFM mode (most common for tuners)
    // Format: {lo, hi} in Hz relative to carrier
    static const int nfm_filter_presets[3][2] = {
        {-10000, 10000},  // Wide
        {-5000, 5000},    // Normal
        {-2500, 2500}     // Narrow
    };

    if (preset >= 0 && preset < 3) {
        int lo = nfm_filter_presets[preset][0];
        int hi = nfm_filter_presets[preset][1];
        channel->set_filter(lo, hi, 500.0);  // 500Hz transition width

        // Update FFT plotter with new filter width
        ui->plotter->updateTunerFilter(tuner_id, lo, hi);
    }
}

/**
 * @brief Tuner AGC preset changed.
 * @param tuner_id The tuner ID.
 * @param preset The AGC preset (0=Fast, 1=Medium, 2=Slow, 3=User, 4=Off).
 */
void MainWindow::onTunerAgcPresetChanged(int tuner_id, int preset)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // AGC preset values
    // Preset: 0=Fast, 1=Medium, 2=Slow, 3=User, 4=Off
    switch (preset) {
    case 0:  // Fast
        channel->set_agc_on(true);
        channel->set_agc_decay(100);
        channel->set_agc_slope(0);
        break;
    case 1:  // Medium
        channel->set_agc_on(true);
        channel->set_agc_decay(500);
        channel->set_agc_slope(0);
        break;
    case 2:  // Slow
        channel->set_agc_on(true);
        channel->set_agc_decay(2000);
        channel->set_agc_slope(0);
        break;
    case 3:  // User - keep current settings, just ensure AGC is on
        channel->set_agc_on(true);
        break;
    case 4:  // Off
        channel->set_agc_on(false);
        break;
    }
}

/**
 * @brief Tuner noise blanker state changed.
 * @param tuner_id The tuner ID.
 * @param state The NB state (0=Off, 1=NB1, 2=NB2, 3=Both).
 */
void MainWindow::onTunerNbStateChanged(int tuner_id, int state)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // NB state: 0=Off, 1=NB1, 2=NB2, 3=Both
    bool nb1_on = (state == 1) || (state == 3);
    bool nb2_on = (state == 2) || (state == 3);

    channel->set_nb_on(1, nb1_on);
    channel->set_nb_on(2, nb2_on);
}

/**
 * @brief Tuner filter shape changed.
 * @param tuner_id The tuner ID.
 * @param shape The filter shape (0=Soft, 1=Normal, 2=Sharp).
 */
void MainWindow::onTunerFilterShapeChanged(int tuner_id, int shape)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    // Filter shape is applied through the backend
    // The IReceiverBackend doesn't have a direct set_filter_shape method,
    // so we need to re-apply the filter with different transition widths:
    // Soft: wide transition (1000Hz), Normal: medium (500Hz), Sharp: narrow (100Hz)
    // For now, just log - full implementation would require storing current filter values
}

/**
 * @brief Tuner NB1 threshold changed.
 * @param tuner_id The tuner ID.
 * @param threshold The threshold (0.0 to 1.0).
 */
void MainWindow::onTunerNb1ThresholdChanged(int tuner_id, float threshold)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_nb_threshold(1, threshold);
}

/**
 * @brief Tuner NB2 threshold changed.
 * @param tuner_id The tuner ID.
 * @param threshold The threshold (0.0 to 1.0).
 */
void MainWindow::onTunerNb2ThresholdChanged(int tuner_id, float threshold)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_nb_threshold(2, threshold);
}

/**
 * @brief Tuner AGC hang setting changed.
 * @param tuner_id The tuner ID.
 * @param use_hang Whether to use hang mode.
 */
void MainWindow::onTunerAgcHangChanged(int tuner_id, bool use_hang)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_agc_hang(use_hang);
}

/**
 * @brief Tuner AGC threshold changed.
 * @param tuner_id The tuner ID.
 * @param threshold The threshold in dB.
 */
void MainWindow::onTunerAgcThresholdChanged(int tuner_id, int threshold)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_agc_threshold(threshold);
}

/**
 * @brief Tuner AGC decay changed.
 * @param tuner_id The tuner ID.
 * @param decay_ms The decay time in milliseconds.
 */
void MainWindow::onTunerAgcDecayChanged(int tuner_id, int decay_ms)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_agc_decay(decay_ms);
}

/**
 * @brief Tuner AGC gain changed.
 * @param tuner_id The tuner ID.
 * @param gain The manual gain value.
 */
void MainWindow::onTunerAgcGainChanged(int tuner_id, int gain)
{

    if (!tuner_manager)
        return;

    auto* channel = tuner_manager->get_channel_impl(tuner_id);
    if (!channel)
        return;

    channel->set_agc_manual_gain(gain);
}
