/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2011-2014 Alexandru Csete OZ9AEC.
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
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QMessageBox>
#include <QFileDialog>
#include <QSvgWidget>
#include <map>
#include <QToolButton>
#include <QMenu>
#include <QInputDialog>

#include "qtgui/dockrxopt.h"
#include "qtgui/dockaudio.h"
#include "qtgui/dockinputctl.h"
#include "qtgui/dockfft.h"
#include "qtgui/dockbookmarks.h"
#include "qtgui/dockbandplan.h"
#include "qtgui/dockrds.h"
#include "qtgui/afsk1200win.h"
#include "qtgui/iq_tool.h"
#include "qtgui/dxc_options.h"
#include "qtgui/rrimportdialog.h"

#include "applications/gqrx/recentconfig.h"
#include "applications/gqrx/remote_control.h"
#include "applications/gqrx/receiver.h"
#include "tuner_manager.h"
#include "interfaces/i_receiver_backend.h"
#include "qtgui/tuner_list.h"

namespace Ui {
    class MainWindow;  /*! The main window UI */
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString& cfgfile, bool edit_conf, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool loadConfig(const QString& cfgfile, bool check_crash, bool restore_mainwindow);
    bool saveConfig(const QString& cfgfile);
    void storeSession();

    bool configOk; /*!< Main app uses this flag to know whether we should abort or continue. */

public slots:
    void setNewFrequency(qint64 rx_freq);
    void setMarkerA(qint64 freq);
    void setMarkerB(qint64 freq);
    void enableMarkers(bool enable);

private:
    Ui::MainWindow *ui;

    QPointer<QSettings> m_settings;  /*!< Application wide settings. */
    QString             m_cfg_dir;   /*!< Default config dir, e.g. XDG_CONFIG_HOME. */
    QString             m_last_dir;
    RecentConfig       *m_recent_config; /* Menu File Recent config */

    qint64 d_lnb_lo;  /* LNB LO in Hz. */
    qint64 d_hw_freq;
    qint64 d_marker_a;
    qint64 d_marker_b;
    bool   d_show_markers;
    qint64 d_hw_freq_start{};
    qint64 d_hw_freq_stop{};

    enum receiver::filter_shape d_filter_shape;
    std::vector<float> d_iqFftData;
    float           d_fftAvg;      /*!< FFT averaging parameter set by user (not the true gain). */
    float           d_fps;
    int             d_fftWindowType;
    bool            d_fftNormalizeEnergy;

    std::vector<float> d_audioFftData;
    bool d_have_audio;  /*!< Whether we have audio (i.e. not with demod_off. */

    /* Fast IQ playback state */
    bool m_fastPlaybackActive{false};
    int  m_fastPlaybackPrevDemod{0};
    QTimer *m_fastPlaybackCheckTimer{nullptr};
    QElapsedTimer m_fastPlaybackStartTime;
    qint64 m_fastPlaybackFileSize{0};
    float m_fastPlaybackSampleRate{0};
    qint64 m_fastPlaybackRealDurationMs{0};  /*!< Real-time duration of file in ms */
    unsigned int m_fastPlaybackLastRowPushed{0};  /*!< Last spectrogram row pushed to waterfall */
    std::vector<float> m_spectrogramRowBuffer;    /*!< Buffer for reading spectrogram rows */

    /* dock widgets */
    DockRxOpt      *uiDockRxOpt;
    DockAudio      *uiDockAudio;
    DockInputCtl   *uiDockInputCtl;
    DockFft        *uiDockFft;
    DockBookmarks  *uiDockBookmarks;
    DockBandplan   *uiDockBandplan;
    DockRDS        *uiDockRDS;

    CIqTool        *iq_tool;
    DXCOptions     *dxc_options;
    DockRRImport   *uiDockRRImport;


    /* data decoders */
    Afsk1200Win    *dec_afsk1200;
    bool            dec_rds{};

    QTimer   *dec_timer;
    QTimer   *meter_timer;
    QTimer   *iq_fft_timer;
    QTimer   *audio_fft_timer;
    QTimer   *rds_timer;
    quint64  d_last_fft_ms;
    float    d_avg_fft_rate;
    bool     d_frame_drop;

    // CPU monitoring
    quint64  d_prev_cpu_user;
    quint64  d_prev_cpu_system;
    quint64  d_prev_cpu_idle;
    float    d_cpu_usage;

    // Disk I/O monitoring (actual bytes read/written by gqrx)
    quint64  d_prev_disk_read;
    quint64  d_prev_disk_write;
    quint64  d_last_diskio_time_ms;
    float    d_disk_read_rate;   // bytes per second
    float    d_disk_write_rate;  // bytes per second

    receiver *rx;

    // Multi-tuner support
    std::shared_ptr<TunerManager> tuner_manager;
    TunerList *tuner_list_widget;
    QDockWidget *uiDockTunerList;
    std::map<int, int> channel_volumes;     // Per-channel volume (0-100), default 100
    std::map<int, bool> channel_muted;      // Per-channel mute state
    float d_main_gain_linear;               // Main volume as linear gain

    // Center button zoom cycling state
    int center_zoom_state;                  // 0=original, 1=close, 2=medium, 3=wide
    int center_zoom_tuner_id;               // Which tuner we're cycling for
    quint32 center_zoom_original_span;      // Span before zooming

    RemoteControl *remote;

    std::map<QString, QVariant> devList;

    // dummy widget to enforce linking to QtSvg
    QSvgWidget      *qsvg_dummy;

    // Main frequency lock button
    QPushButton     *m_main_freq_lock_btn;

    // View presets button
    QToolButton     *m_view_presets_btn;
    QMenu           *m_view_presets_menu;

    QFont font;

private:
    void updateHWFrequencyRange(bool ignore_limits);
    void updateFrequencyRange();
    void updateDeltaAndCenter();
    void updateGainStages(bool read_from_device);
    void updateSourceStatusLabels();
    void updateLeftStats();
    float getCpuUsage();
    void showSimpleTextFile(const QString &resource_path,
                            const QString &window_title);
    /* key shortcuts */
    void frequencyFocusShortcut();
    void rxOffsetZeroShortcut();
    void toggleFreezeShortcut();
    void toggleMarkers();
    void onMainFreqLockClicked();
    /* view presets */
    void setupViewPresetsMenu();
    void rebuildViewPresetsMenu();

private slots:
    void onTunerRemoved(int tuner_id);
    void onTunerTypeChanged(int tuner_id, ReceiverType type);
    void onTunerEnabledChanged(int tuner_id, bool enabled);
    void onTunerNameChanged(int tuner_id, const QString& name);
    void onTunerColorChanged(int tuner_id, const QColor& color);
    void onTunerAlphaChanged(int tuner_id, int alpha);
    void onTunerVolumeChanged(int tuner_id, int volume);
    void onTunerMuteToggled(int tuner_id, bool muted);
    void onTunerRecordingToggled(int tuner_id, bool recording);
    void onTunerCenterRequested(int tuner_id);
    void onTunerZoomRequested(int tuner_id);
    void onTunerFrequencyChanged(int tuner_id, qint64 freq);
    void onTunerFilterWidthChanged(int tuner_id, int filter_low, int filter_high);
    // New settings slots
    void onTunerSquelchChanged(int tuner_id, double level_db);
    void onTunerAutoSquelchRequested(int tuner_id);
    void onTunerFilterPresetChanged(int tuner_id, int preset);
    void onTunerAgcPresetChanged(int tuner_id, int preset);
    void onTunerNbStateChanged(int tuner_id, int state);
    // Expanded section slots
    void onTunerFilterShapeChanged(int tuner_id, int shape);
    void onTunerNb1ThresholdChanged(int tuner_id, float threshold);
    void onTunerNb2ThresholdChanged(int tuner_id, float threshold);
    void onTunerAgcHangChanged(int tuner_id, bool use_hang);
    void onTunerAgcThresholdChanged(int tuner_id, int threshold);
    void onTunerAgcDecayChanged(int tuner_id, int decay_ms);
    void onTunerAgcGainChanged(int tuner_id, int gain);
    void addTuner();
    void addTunerWithType(ReceiverType type);
    void addTunerAtFrequency(qint64 freq);
    void addTunerAtFrequencyWithMode(qint64 freq, QString modulation, QString name = QString());
    void removeTuner();

private slots:
    /* view presets */
    void onSaveViewPreset();
    void onLoadViewPreset(QAction* action);
    void onDeleteViewPreset();
    void onClearAllViewPresets();
    /* RecentConfig */
    void loadConfigSlot(const QString &cfgfile);

    /* rf */
    void setLnbLo(double freq_mhz);
    void setAntenna(const QString& antenna);

    /* baseband receiver */
    void setFilterOffset(qint64 freq_hz);
    void setGain(const QString& name, double gain);
    void setAutoGain(bool enabled);
    void setFreqCorr(double ppm);
    void setIqSwap(bool reversed);
    void setDcCancel(bool enabled);
    void setIqBalance(bool enabled);
    void setIgnoreLimits(bool ignore_limits);
    void setFreqCtrlReset(bool enabled);
    void setInvertScrolling(bool enabled);
    void selectDemod(const QString& demod);
    void selectDemod(int index);
    void setFmMaxdev(float max_dev);
    void setFmEmph(double tau);
    void setAmDcr(bool enabled);
    void setCwOffset(int offset);
    void setAmSyncDcr(bool enabled);
    void setAmSyncPllBw(float pll_bw);
    void setAgcOn(bool agc_on);
    void setAgcHang(bool use_hang);
    void setAgcThreshold(int threshold);
    void setAgcSlope(int factor);
    void setAgcDecay(int msec);
    void setAgcGain(int gain);
    void setNoiseBlanker(int nbid, bool on, float threshold);
    void setSqlLevel(double level_db);
    double setSqlLevelAuto();
    void setAudioGain(float gain);
    void setPassband(int bandwidth);

    /* audio recording and playback */
    void startAudioRec(const QString& filename);
    void stopAudioRec();
    void startAudioPlayback(const QString& filename);
    void stopAudioPlayback();

    void startAudioStream(const QString& udp_host, int udp_port, bool stereo);
    void stopAudioStreaming();

    /* I/Q playback and recording*/
    void startIqRecording(const QString& recdir, const QString& format);
    void stopIqRecording();
    void startIqPlayback(const QString& filename, float samprate, qint64 center_freq, bool fast);
    void stopIqPlayback();
    void seekIqFile(qint64 seek_pos);

    /* FFT settings */
    void setIqFftSize(int size);
    void setIqFftRate(int fps);
    void setIqFftWindow(int type);
    void plotScaleChanged(int type, bool perHz);
    void setIqFftSplit(int pct_wf);
    void setAudioFftRate(int fps);
    void setFftColor(const QColor& color);
    void enableFftFill(bool enable);
    void setWfTimeSpan(quint64 span_ms);
    void setWfSize();

    /* FFT plot */
    void on_plotter_newDemodFreq(qint64 freq, qint64 delta);   /*! New demod freq (aka. filter offset). */
    void on_plotter_newFilterFreq(int low, int high);    /*! New filter width */
    void onTunerDragged(int tuner_id, qint64 freq);      /*! Tuner marker dragged to new frequency */
    void onFilterResized(int tuner_id, int filter_low, int filter_high);  /*! Filter edges dragged */
    void onPanSdrFrequency(int dragged_tuner_id, qint64 new_freq); /*! Shift+drag to pan SDR frequency */

    /* RDS */
    void setRdsDecoder(bool checked);

    /* Bookmarks */
    void onBookmarkActivated(qint64 freq, const QString& demod, int bandwidth);

    /* DXC Spots */
    void updateClusterSpots();

    /* menu and toolbar actions */
    void on_actionDSP_triggered(bool checked);
    int  on_actionIoConfig_triggered();
    void on_actionLoadSettings_triggered();
    void on_actionSaveSettings_triggered();
    void on_actionIqTool_triggered();
    void on_actionFullScreen_triggered(bool checked);
    void on_actionRemoteControl_triggered(bool checked);
    void on_actionRemoteConfig_triggered();
    void on_actionAFSK1200_triggered();
    void on_actionUserGroup_triggered();
    void on_actionNews_triggered();
    void on_actionRemoteProtocol_triggered();
    void on_actionKbdShortcuts_triggered();
    void on_actionAbout_triggered();
    void on_actionAboutQt_triggered();
    void on_actionAddBookmark_triggered();
    void on_actionDX_Cluster_triggered();

    /* markers*/
    void on_setMarkerButtonA_clicked();
    void on_setMarkerButtonB_clicked();
    void on_clearMarkerButtonA_clicked();
    void on_clearMarkerButtonB_clicked();

    /* window close signals */
    void afsk1200win_closed();
    int  firstTimeConfig();

    /* cyclic processing */
    void decoderTimeout();
    void meterTimeout();
    void iqFftTimeout();
    void audioFftTimeout();
    void rdsTimeout();
    void fastPlaybackCheckTimeout();
};

#endif // MAINWINDOW_H
