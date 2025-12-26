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
#ifndef TUNER_PANEL_H
#define TUNER_PANEL_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QFrame>

#include "applications/gqrx/receiver_channel.h"
#include "qtgui/freqctrl.h"

/**
 * @brief Control panel for a single tuner/receiver channel
 * @ingroup UI
 *
 * This widget provides controls for a single tuner channel including:
 * - Frequency control
 * - Demodulation mode selection
 * - Filter settings
 * - Audio controls
 * - Signal level display
 * - Recording controls
 */
class TunerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TunerPanel(int tuner_id, QWidget *parent = nullptr);
    ~TunerPanel();

    /** Set the receiver channel to control */
    void set_receiver_channel(ReceiverChannel* channel);

    /** Get tuner ID */
    int get_tuner_id() const { return d_tuner_id; }

    /** Update display from receiver state */
    void refresh_all_settings();

    /** Set tuner name/label */
    void set_tuner_name(const QString& name);

    /** Enable/disable all controls */
    void set_controls_enabled(bool enabled);

    /** Set frequency limits */
    void set_frequency_range(double min_freq, double max_freq);

    /** Update signal level display */
    void update_signal_level(float level_db);

public slots:
    /** Frequency control slots */
    void on_frequency_changed(qint64 freq);
    void on_frequency_step_changed();

    /** Demodulation control slots */
    void on_demod_selected(int index);
    void on_filter_width_changed();
    void on_filter_offset_changed();

    /** Audio control slots */
    void on_audio_gain_changed(int value);
    void on_audio_mute_toggled(bool muted);
    void on_audio_device_changed(const QString& device);

    /** AGC and squelch slots */
    void on_agc_toggled(bool enabled);
    void on_squelch_changed(int value);

    /** Recording slots */
    void on_record_audio_clicked();
    void on_record_iq_clicked();

    /** Sniffer slot */
    void on_sniffer_toggled(bool enabled);

    /** Enable/disable slot */
    void on_enabled_toggled(bool enabled);

signals:
    /** Emitted when tuner settings change */
    void frequency_changed(int tuner_id, qint64 frequency);
    void demod_changed(int tuner_id, ReceiverChannel::rx_demod demod);
    void filter_changed(int tuner_id, double low, double high);
    void audio_gain_changed(int tuner_id, float gain);
    void audio_mute_changed(int tuner_id, bool muted);
    void enabled_changed(int tuner_id, bool enabled);

    /** Emitted for actions */
    void start_audio_recording(int tuner_id, const QString& filename);
    void stop_audio_recording(int tuner_id);
    void start_iq_recording(int tuner_id, const QString& filename);
    void stop_iq_recording(int tuner_id);

    /** Context menu request */
    void context_menu_requested(int tuner_id, const QPoint& pos);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private slots:
    void update_filter_range();
    void update_demod_settings();

private:
    void setup_ui();
    void create_frequency_controls();
    void create_demod_controls();
    void create_audio_controls();
    void create_status_controls();
    void create_action_buttons();

    void connect_signals();
    void disconnect_signals();

    void update_demod_combo();
    void update_audio_device_combo();

    QString format_frequency(qint64 freq) const;
    QString get_demod_string(ReceiverChannel::rx_demod demod) const;
    ReceiverChannel::rx_demod get_demod_from_string(const QString& str) const;

private:
    int d_tuner_id;
    ReceiverChannel* d_receiver;

    // UI Layout
    QVBoxLayout* d_main_layout;
    QHBoxLayout* d_top_layout;
    QGridLayout* d_controls_layout;

    // Tuner identification
    QGroupBox* d_tuner_group;
    QLabel* d_tuner_name_label;
    QCheckBox* d_enabled_checkbox;

    // Frequency controls
    QGroupBox* d_freq_group;
    CFreqCtrl* d_freq_ctrl;
    QSpinBox* d_freq_step;

    // Demodulation controls
    QGroupBox* d_demod_group;
    QComboBox* d_demod_combo;
    QSpinBox* d_filter_low;
    QSpinBox* d_filter_high;
    QSpinBox* d_filter_offset;

    // Audio controls
    QGroupBox* d_audio_group;
    QSlider* d_audio_gain_slider;
    QLabel* d_audio_gain_label;
    QCheckBox* d_audio_mute_checkbox;
    QComboBox* d_audio_device_combo;

    // Signal processing controls
    QGroupBox* d_processing_group;
    QCheckBox* d_agc_checkbox;
    QSlider* d_squelch_slider;
    QLabel* d_squelch_label;

    // Status display
    QGroupBox* d_status_group;
    QProgressBar* d_signal_level_bar;
    QLabel* d_signal_level_label;
    QLabel* d_status_label;

    // Action buttons
    QHBoxLayout* d_buttons_layout;
    QPushButton* d_record_audio_btn;
    QPushButton* d_record_iq_btn;
    QPushButton* d_sniffer_btn;

    // State tracking
    bool d_recording_audio;
    bool d_recording_iq;
    bool d_sniffer_active;
    double d_min_freq;
    double d_max_freq;

    // Prevent recursion during updates
    bool d_updating_controls;
};

#endif // TUNER_PANEL_H
