/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
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
#ifndef TUNER_LIST_H
#define TUNER_LIST_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QSlider>
#include <QMenu>
#include <QWidgetAction>
#include <QToolButton>
#include <QToolBar>
#include <QPainter>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <map>

#include "interfaces/i_receiver_backend.h"
#include "applications/gqrx/recording_config.h"
#include "freqctrl.h"

// Forward declarations
class TunerManager;
class QSettings;

/*!
 * \brief Tuner status states
 */
enum class TunerStatus {
    Disabled,   // User explicitly disabled
    Stopped,    // DSP not running
    Bypassed,   // Out of SDR frequency range
    Running     // Active and receiving
};

/*!
 * \brief Custom RSSI meter widget with draggable squelch indicator and dBFS scale
 *
 * Displays signal strength as a colored bar with tick marks for dBFS scale.
 * The squelch threshold is shown as a vertical line that can be dragged.
 */
class RssiMeterWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RssiMeterWidget(QWidget *parent = nullptr);

    void setRssi(float level_db);
    float rssi() const { return m_rssi; }

    void setSquelch(double level_db);
    double squelch() const { return m_squelch; }

    void setRange(float min_db, float max_db);

signals:
    void squelchChanged(double level_db);
    void squelchDragStarted();
    void squelchDragFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    float dbToX(float db) const;
    float xToDb(int x) const;
    QColor colorForLevel(float db) const;

    float m_rssi;          // Current RSSI level in dB
    double m_squelch;      // Squelch threshold in dB
    float m_min_db;        // Display range minimum
    float m_max_db;        // Display range maximum
    bool m_dragging;       // True when squelch is being dragged
    int m_drag_offset;     // Mouse offset from squelch marker center
};

/*!
 * \brief A single token chip widget for the TokenEditor
 */
class TokenChip : public QFrame
{
    Q_OBJECT

public:
    explicit TokenChip(const QString& token, QWidget *parent = nullptr);
    QString token() const { return m_token; }

signals:
    void removeRequested();
    void dragStarted(TokenChip* chip, QPoint globalPos);
    void dragMoved(TokenChip* chip, QPoint globalPos);
    void dragFinished(TokenChip* chip, QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_token;
    QPoint m_press_pos;
    bool m_dragging;
};

/*!
 * \brief Token-based pattern editor with draggable chips
 *
 * Displays filename pattern tokens as visual chips that can be:
 * - Dragged to reorder
 * - Dragged out to remove
 * - Added via the variable buttons below
 */
class TokenEditor : public QFrame
{
    Q_OBJECT

public:
    explicit TokenEditor(QWidget *parent = nullptr);

    void setPattern(const QString& pattern);
    QString pattern() const;

    void addToken(const QString& token);
    void clear();

signals:
    void patternChanged(const QString& pattern);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onChipRemoved();
    void onChipDragStarted(TokenChip* chip, QPoint globalPos);
    void onChipDragMoved(TokenChip* chip, QPoint globalPos);
    void onChipDragFinished(TokenChip* chip, QPoint globalPos);

private:
    void rebuildLayout();
    int findInsertIndex(const QPoint& localPos);

    QList<TokenChip*> m_chips;
    QHBoxLayout* m_layout;
    TokenChip* m_dragged_chip;
    int m_drag_original_index;
};

/*!
 * \brief Widget representing a single tuner row in the list
 */
class TunerRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TunerRowWidget(int tuner_id, const QString& name, ReceiverType type = ReceiverType::ANALOG_NFM, const QColor& color = Qt::cyan, QWidget *parent = nullptr);

    int tuner_id() const { return m_tuner_id; }
    ReceiverType receiverType() const { return m_receiver_type; }
    void setReceiverType(ReceiverType type);
    QString name() const;
    void setName(const QString& name);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void setColor(const QColor& color);
    QColor color() const { return m_color; }
    void setAlpha(int alpha);
    int alpha() const { return m_alpha; }
    void setVolume(int volume);
    int volume() const { return m_volume; }
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }
    void setFrequency(qint64 freq_hz);
    qint64 frequency() const { return m_frequency; }
    void setRunning(bool running);  // Legacy - use setStatus for more detail
    bool isRunning() const { return m_status == TunerStatus::Running; }
    void setStatus(TunerStatus status);
    TunerStatus status() const { return m_status; }

    // Filter bandwidth (for display and setting)
    void setFilterWidth(int filter_low, int filter_high);
    int filterLow() const { return m_filter_low; }
    int filterHigh() const { return m_filter_high; }

    // New settings controls
    void setSquelch(double level_db);
    double squelch() const { return m_squelch; }
    void setFilterPreset(int preset);  // 0=Wide, 1=Normal, 2=Narrow, 3=User
    int filterPreset() const { return m_filter_preset; }
    void setAgcPreset(int preset);  // 0=Fast, 1=Medium, 2=Slow, 3=User, 4=Off
    int agcPreset() const { return m_agc_preset; }
    void setNbState(int state);  // 0=Off, 1=NB1, 2=NB2, 3=Both
    int nbState() const { return m_nb_state; }
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }

    // Recording state
    void setRecording(bool recording);
    bool isRecording() const { return m_recording; }
    void setRecordingIq(bool recording);
    bool isRecordingIq() const { return m_recording_iq; }
    void updateRecordingInfo(double audio_duration, double iq_duration);
    void setRecordingConfig(bool record_iq, bool record_audio, RecordingMode iq_mode, RecordingMode audio_mode);

    // RSSI level indicator
    void setRssi(float level_db);
    float rssi() const { return m_rssi; }

    // Check if this is an analog tuner type
    bool isAnalogType() const;

    // Force layout update for given width (called by TunerList on resize)
    void updateLayoutForWidth(int width);

signals:
    void receiverTypeChanged(int tuner_id, ReceiverType type);
    void nameChanged(int tuner_id, const QString& name);
    void enableToggled(int tuner_id, bool enabled);
    void colorChanged(int tuner_id, const QColor& color);
    void alphaChanged(int tuner_id, int alpha);
    void volumeChanged(int tuner_id, int volume);
    void muteToggled(int tuner_id, bool muted);
    void centerRequested(int tuner_id);
    void zoomRequested(int tuner_id);
    void closeRequested(int tuner_id);
    void frequencyChanged(int tuner_id, qint64 freq);
    void filterWidthChanged(int tuner_id, int filter_low, int filter_high);
    // New settings signals
    void squelchChanged(int tuner_id, double level_db);
    void autoSquelchRequested(int tuner_id);
    void filterPresetChanged(int tuner_id, int preset);
    void agcPresetChanged(int tuner_id, int preset);
    void nbStateChanged(int tuner_id, int state);
    void expandedChanged(int tuner_id, bool expanded);
    // Expanded section signals
    void filterShapeChanged(int tuner_id, int shape);
    void nb1ThresholdChanged(int tuner_id, float threshold);
    void nb2ThresholdChanged(int tuner_id, float threshold);
    void agcHangChanged(int tuner_id, bool use_hang);
    void agcThresholdChanged(int tuner_id, int threshold);
    void agcDecayChanged(int tuner_id, int decay_ms);
    void agcGainChanged(int tuner_id, int gain);
    // Recording signals
    void recordingToggled(int tuner_id, bool recording);
    void recordingIqToggled(int tuner_id, bool recording);
    void tunerRecordingConfigChanged(int tuner_id, bool record_iq, bool record_audio, RecordingMode iq_mode, RecordingMode audio_mode);

private slots:
    void onNameEditFinished();
    void onTypeSelected(QAction* action);
    void onBandwidthSelected(QAction* action);
    void onEnableClicked();
    void onColorClicked();
    void onVolumeChanged(int value);
    void onMuteClicked();
    void onCenterClicked();
    void onZoomClicked();
    void onBookmarkClicked();
    void onCloseClicked();
    void onFrequencyChanged(qint64 freq);
    // New settings slots
    void onSquelchCycleClicked();
    void onSquelchDragged(double level_db);  // From RssiMeterWidget
    void onFilterPresetSelected(QAction* action);
    void onAgcPresetSelected(QAction* action);
    void onNbClicked();
    void onGearClicked();
    // Expanded section slots
    void onFilterShapeSelected(QAction* action);
    void onNb1ThresholdChanged(int value);
    void onNb2ThresholdChanged(int value);
    void onAgcHangToggled(bool checked);
    void onAgcThresholdChanged(int value);
    void onAgcDecayChanged(int value);
    void onAgcGainChanged(int value);
    // Recording slots
    void onRecordClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QString receiverTypeName(ReceiverType type) const;
    void updateAnalogControlsVisibility();
    void updateRowLayout(int width);

    int m_tuner_id;
    ReceiverType m_receiver_type;
    bool m_enabled;
    TunerStatus m_status;
    QColor m_color;
    int m_alpha;
    qint64 m_frequency;
    int m_volume;
    bool m_muted;
    int m_filter_low;      // Hz, negative for lower sideband
    int m_filter_high;     // Hz, positive for upper sideband
    // New settings state
    double m_squelch;      // dB, -150 to 0
    int m_squelch_mode;    // 0=next click does Auto, 1=next click does Reset
    int m_filter_preset;   // 0=Wide, 1=Normal, 2=Narrow, 3=User
    int m_agc_preset;      // 0=Fast, 1=Medium, 2=Slow, 3=User, 4=Off
    int m_nb_state;        // 0=Off, 1=NB1, 2=NB2, 3=Both
    bool m_expanded;       // Gear expansion state
    float m_rssi;          // Current RSSI level in dB
    bool m_recording;      // Audio recording state
    bool m_recording_iq;   // IQ recording state
    // Recording config (persisted)
    bool m_config_record_iq;    // Enable IQ recording for this tuner
    bool m_config_record_audio; // Enable audio recording for this tuner
    RecordingMode m_config_iq_mode;
    RecordingMode m_config_audio_mode;

    // Row 1 widgets (info)
    QPushButton* m_color_btn;
    QLineEdit* m_name_edit;
    QPushButton* m_type_btn;
    QMenu* m_type_menu;
    QPushButton* m_bandwidth_btn;
    QMenu* m_bandwidth_menu;
    CFreqCtrl* m_freq_ctrl;
    QLabel* m_status_label;

    // Row 2 widgets (controls)
    QPushButton* m_squelch_btn;  // Cycles Auto/Reset
    QPushButton* m_filter_btn;   // Only for analog
    QMenu* m_filter_menu;
    QPushButton* m_agc_btn;      // Only for analog
    QMenu* m_agc_menu;
    QPushButton* m_nb_btn;       // Only for analog
    QPushButton* m_volume_btn;
    QSlider* m_volume_slider;    // In popup menu
    QLabel* m_volume_label;      // Shows volume % in popup
    QPushButton* m_mute_btn;
    QPushButton* m_record_btn;
    QLabel* m_recording_indicator;  // Shows recording duration
    QPushButton* m_gear_btn;
    QPushButton* m_enable_btn;
    QPushButton* m_bookmark_btn;
    QPushButton* m_center_btn;
    QPushButton* m_zoom_btn;
    QPushButton* m_close_btn;

    // Expanded settings section (Row 3+)
    QWidget* m_expanded_section;
    QVBoxLayout* m_main_layout;

    // Expanded section widgets
    QPushButton* m_filter_shape_btn;
    QSlider* m_nb1_slider;
    QSlider* m_nb2_slider;
    QPushButton* m_agc_hang_btn;
    QSlider* m_agc_threshold_slider;
    QSlider* m_agc_decay_slider;
    QSlider* m_agc_gain_slider;

    // RSSI indicator bar with draggable squelch marker and dBFS scale
    RssiMeterWidget* m_rssi_meter;  // Custom widget with drag support

    // Responsive layout - Row 1 (info)
    QWidget* m_row1_container;    // Container for row1 controls
    QWidget* m_row1a_widget;      // Left: color, name
    QWidget* m_row1b_widget;      // Right: type, bandwidth, freq, status
    QLayout* m_row1_layout;       // Current layout (horizontal or vertical)

    // Responsive layout - Row 2 (controls)
    QWidget* m_row2_container;    // Container for row2 controls
    QWidget* m_row2a_widget;      // Left controls: Sq, Filter, AGC, NB, V
    QWidget* m_row2b_widget;      // Right controls: ⚙, E, C, X
    QLayout* m_row2_layout;       // Current layout (horizontal or vertical)
    bool m_is_stacked_layout;
    int m_last_layout_width;
};

/*!
 * \brief Tuner list widget for managing multiple tuners
 */
class TunerList : public QWidget
{
    Q_OBJECT

public:
    explicit TunerList(QWidget *parent = nullptr);
    ~TunerList();

    void set_tuner_manager(TunerManager* manager);

signals:
    void tuner_add_requested();  // Default (NFM)
    void tuner_add_requested_with_type(ReceiverType type);  // Specific type
    void tuner_remove_requested(int tuner_id);
    void tuner_type_changed(int tuner_id, ReceiverType type);  // Receiver mode changed
    void tuner_enabled_changed(int tuner_id, bool enabled);
    void tuner_name_changed(int tuner_id, const QString& name);
    void tuner_color_changed(int tuner_id, const QColor& color);
    void tuner_alpha_changed(int tuner_id, int alpha);
    void tuner_volume_changed(int tuner_id, int volume);
    void tuner_mute_toggled(int tuner_id, bool muted);
    void tuner_center_requested(int tuner_id);
    void tuner_zoom_requested(int tuner_id);
    void tuner_frequency_changed(int tuner_id, qint64 freq);
    void tuner_filter_width_changed(int tuner_id, int filter_low, int filter_high);
    // New settings signals
    void tuner_squelch_changed(int tuner_id, double level_db);
    void tuner_auto_squelch_requested(int tuner_id);
    void tuner_filter_preset_changed(int tuner_id, int preset);
    void tuner_agc_preset_changed(int tuner_id, int preset);
    void tuner_nb_state_changed(int tuner_id, int state);
    // Expanded section signals
    void tuner_filter_shape_changed(int tuner_id, int shape);
    void tuner_nb1_threshold_changed(int tuner_id, float threshold);
    void tuner_nb2_threshold_changed(int tuner_id, float threshold);
    void tuner_agc_hang_changed(int tuner_id, bool use_hang);
    void tuner_agc_threshold_changed(int tuner_id, int threshold);
    void tuner_agc_decay_changed(int tuner_id, int decay_ms);
    void tuner_agc_gain_changed(int tuner_id, int gain);
    // Recording signals
    void tuner_recording_toggled(int tuner_id, bool recording);
    void tuner_recording_iq_toggled(int tuner_id, bool recording);
    void recording_config_changed(const RecordingConfig& config);
    void tuner_recording_config_changed(int tuner_id, bool record_iq, bool record_audio, RecordingMode iq_mode, RecordingMode audio_mode);
    void global_recording_toggled(bool recording);

private slots:
    void onGlobalRecordClicked();
    void on_add_button_clicked();
    void onAddTunerType(QAction* action);
    void onTunerTypeChanged(int tuner_id, ReceiverType type);
    void onTunerNameChanged(int tuner_id, const QString& name);
    void onTunerEnabledToggled(int tuner_id, bool enabled);
    void onTunerColorChanged(int tuner_id, const QColor& color);
    void onTunerAlphaChanged(int tuner_id, int alpha);
    void onTunerVolumeChanged(int tuner_id, int volume);
    void onTunerMuteToggled(int tuner_id, bool muted);
    void onTunerCenterRequested(int tuner_id);
    void onTunerZoomRequested(int tuner_id);
    void onTunerCloseRequested(int tuner_id);
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
    // Recording slots
    void onTunerRecordingToggled(int tuner_id, bool recording);
    void onTunerRecordingIqToggled(int tuner_id, bool recording);
    void onTunerRecordingConfigChanged(int tuner_id, bool record_iq, bool record_audio, RecordingMode iq_mode, RecordingMode audio_mode);
    // Master settings
    void onSettingsClicked();
    void onOpenFolderClicked();

private:
    void updateGlobalRecordButtonState();

public:
    // Getters for tuner info (works even before channels are created)
    qint64 getTunerFrequency(int tuner_id) const;
    TunerStatus getTunerStatus(int tuner_id) const;
    int getTunerVolume(int tuner_id) const;
    bool getTunerMuted(int tuner_id) const;

public slots:
    void refresh_tuner_list();
    void update_tuner_color(int tuner_id, const QColor& color);
    void update_tuner_running(int tuner_id, bool running);  // Legacy - use update_tuner_status
    void update_tuner_status(int tuner_id, TunerStatus status);
    void update_tuner_frequency(int tuner_id, qint64 freq);  // Update freq display and re-sort
    void update_tuner_filter_width(int tuner_id, int filter_low, int filter_high);  // Update filter width display
    void update_tuner_rssi(int tuner_id, float level_db);  // Update RSSI indicator
    void update_tuner_recording(int tuner_id, bool recording);  // Update recording state
    void update_tuner_recording_iq(int tuner_id, bool recording);  // Update IQ recording state
    void update_tuner_recording_info(int tuner_id, double audio_duration, double iq_duration);  // Update recording duration
    void set_all_tuners_running(bool dsp_running);  // Updates all statuses based on DSP state
    void load_from_settings(QSettings* settings);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void clearTunerRows();
    void setupAddMenu();
    void sortTunerRows();  // Re-sort rows by frequency
    void updateAllRowLayouts(int width);  // Propagate width to all rows
    void updateStatusLabel();  // Update "Tuners: x/y" display
    void connectRowSignals(TunerRowWidget* row);  // Connect all signals for a tuner row

    QString receiverTypeName(ReceiverType type) const;

    int m_last_width;
    QVBoxLayout* main_layout;
    QToolBar* button_toolbar;
    QVBoxLayout* tuner_rows_layout;
    QWidget* tuner_rows_container;
    QScrollArea* scroll_area;
    QLabel* status_label;
    QToolButton* add_button;
    QToolButton* add_type_button;
    QMenu* add_menu;
    QToolButton* settings_button;
    QToolButton* global_record_button;
    QToolButton* open_folder_button;
    bool m_global_recording;
    TunerManager* tuner_manager;
    std::map<int, TunerRowWidget*> tuner_rows;
    std::map<int, QColor> tuner_colors;  // Persist colors across refreshes
    std::map<int, int> tuner_volumes;    // Persist volumes across refreshes
    std::map<int, bool> tuner_muted;     // Persist mute state across refreshes
    std::map<int, int> tuner_filter_low;   // Persist filter low across refreshes
    std::map<int, int> tuner_filter_high;  // Persist filter high across refreshes
    ReceiverType last_tuner_type;  // Remember last selected type
};

#endif // TUNER_LIST_H
