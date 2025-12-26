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

#include "tuner_panel.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QStyle>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDateTime>

TunerPanel::TunerPanel(int tuner_id, QWidget *parent)
    : QWidget(parent)
    , d_tuner_id(tuner_id)
    , d_receiver(nullptr)
    , d_recording_audio(false)
    , d_recording_iq(false)
    , d_sniffer_active(false)
    , d_min_freq(0.0)
    , d_max_freq(6000000000.0)
    , d_updating_controls(false)
{

    setup_ui();
    connect_signals();

    // Set default values
    d_freq_ctrl->setFrequency(100000000); // 100 MHz
    d_demod_combo->setCurrentIndex(3); // NFM
    d_audio_gain_slider->setValue(50); // 50% audio gain
    d_squelch_slider->setValue(-150); // -150 dB squelch

    setMinimumWidth(300);
    setMaximumWidth(400);
}

TunerPanel::~TunerPanel()
{
    disconnect_signals();
}

void TunerPanel::setup_ui()
{
    d_main_layout = new QVBoxLayout(this);
    d_main_layout->setSpacing(5);
    d_main_layout->setContentsMargins(5, 5, 5, 5);

    create_frequency_controls();
    create_demod_controls();
    create_audio_controls();
    create_status_controls();
    create_action_buttons();

    // Add stretch to push everything to top
    d_main_layout->addStretch();
}

void TunerPanel::create_frequency_controls()
{
    d_tuner_group = new QGroupBox(tr("Tuner %1").arg(d_tuner_id), this);
    QVBoxLayout* tuner_layout = new QVBoxLayout(d_tuner_group);

    // Tuner name and enable checkbox
    QHBoxLayout* name_layout = new QHBoxLayout();
    d_tuner_name_label = new QLabel(tr("Channel %1").arg(d_tuner_id));
    d_enabled_checkbox = new QCheckBox(tr("Enabled"));
    d_enabled_checkbox->setChecked(true);
    name_layout->addWidget(d_tuner_name_label);
    name_layout->addStretch();
    name_layout->addWidget(d_enabled_checkbox);
    tuner_layout->addLayout(name_layout);

    // Frequency control
    d_freq_group = new QGroupBox(tr("Frequency"), this);
    QVBoxLayout* freq_layout = new QVBoxLayout(d_freq_group);

    d_freq_ctrl = new CFreqCtrl(this);
    d_freq_ctrl->setup(11, 0, 6000000000ULL, 1, FCTL_UNIT_NONE);
    d_freq_ctrl->setFrequency(100000000);
    freq_layout->addWidget(d_freq_ctrl);

    // Frequency step
    QHBoxLayout* step_layout = new QHBoxLayout();
    step_layout->addWidget(new QLabel(tr("Step:")));
    d_freq_step = new QSpinBox();
    d_freq_step->setRange(1, 100000000);
    d_freq_step->setSuffix(" Hz");
    d_freq_step->setValue(1000);
    step_layout->addWidget(d_freq_step);
    step_layout->addStretch();
    freq_layout->addLayout(step_layout);

    tuner_layout->addWidget(d_freq_group);
    d_main_layout->addWidget(d_tuner_group);
}

void TunerPanel::create_demod_controls()
{
    d_demod_group = new QGroupBox(tr("Demodulation"), this);
    QGridLayout* demod_layout = new QGridLayout(d_demod_group);

    // Demod mode selection
    demod_layout->addWidget(new QLabel(tr("Mode:")), 0, 0);
    d_demod_combo = new QComboBox();
    update_demod_combo();
    demod_layout->addWidget(d_demod_combo, 0, 1, 1, 2);

    // Filter controls
    demod_layout->addWidget(new QLabel(tr("Filter Low:")), 1, 0);
    d_filter_low = new QSpinBox();
    d_filter_low->setRange(-50000, 50000);
    d_filter_low->setSuffix(" Hz");
    d_filter_low->setValue(-5000);
    demod_layout->addWidget(d_filter_low, 1, 1);

    demod_layout->addWidget(new QLabel(tr("High:")), 1, 2);
    d_filter_high = new QSpinBox();
    d_filter_high->setRange(-50000, 50000);
    d_filter_high->setSuffix(" Hz");
    d_filter_high->setValue(5000);
    demod_layout->addWidget(d_filter_high, 1, 3);

    demod_layout->addWidget(new QLabel(tr("Offset:")), 2, 0);
    d_filter_offset = new QSpinBox();
    d_filter_offset->setRange(-25000, 25000);
    d_filter_offset->setSuffix(" Hz");
    d_filter_offset->setValue(0);
    demod_layout->addWidget(d_filter_offset, 2, 1, 1, 2);

    d_main_layout->addWidget(d_demod_group);
}

void TunerPanel::create_audio_controls()
{
    d_audio_group = new QGroupBox(tr("Audio"), this);
    QGridLayout* audio_layout = new QGridLayout(d_audio_group);

    // Audio gain
    audio_layout->addWidget(new QLabel(tr("Gain:")), 0, 0);
    d_audio_gain_slider = new QSlider(Qt::Horizontal);
    d_audio_gain_slider->setRange(0, 100);
    d_audio_gain_slider->setValue(50);
    audio_layout->addWidget(d_audio_gain_slider, 0, 1);
    d_audio_gain_label = new QLabel(tr("50%"));
    d_audio_gain_label->setMinimumWidth(40);
    d_audio_gain_label->setAlignment(Qt::AlignRight);
    audio_layout->addWidget(d_audio_gain_label, 0, 2);

    // Mute checkbox
    d_audio_mute_checkbox = new QCheckBox(tr("Mute"));
    audio_layout->addWidget(d_audio_mute_checkbox, 0, 3);

    // Audio device selection
    audio_layout->addWidget(new QLabel(tr("Device:")), 1, 0);
    d_audio_device_combo = new QComboBox();
    update_audio_device_combo();
    audio_layout->addWidget(d_audio_device_combo, 1, 1, 1, 3);

    // Processing group
    d_processing_group = new QGroupBox(tr("Processing"), this);
    QGridLayout* proc_layout = new QGridLayout(d_processing_group);

    // AGC
    d_agc_checkbox = new QCheckBox(tr("AGC"));
    d_agc_checkbox->setChecked(true);
    proc_layout->addWidget(d_agc_checkbox, 0, 0);

    // Squelch
    proc_layout->addWidget(new QLabel(tr("Squelch:")), 1, 0);
    d_squelch_slider = new QSlider(Qt::Horizontal);
    d_squelch_slider->setRange(-200, 0);
    d_squelch_slider->setValue(-150);
    proc_layout->addWidget(d_squelch_slider, 1, 1);
    d_squelch_label = new QLabel(tr("-150 dB"));
    d_squelch_label->setMinimumWidth(60);
    d_squelch_label->setAlignment(Qt::AlignRight);
    proc_layout->addWidget(d_squelch_label, 1, 2);

    d_main_layout->addWidget(d_audio_group);
    d_main_layout->addWidget(d_processing_group);
}

void TunerPanel::create_status_controls()
{
    d_status_group = new QGroupBox(tr("Status"), this);
    QGridLayout* status_layout = new QGridLayout(d_status_group);

    // Signal level
    status_layout->addWidget(new QLabel(tr("Signal:")), 0, 0);
    d_signal_level_bar = new QProgressBar();
    d_signal_level_bar->setRange(0, 100);
    d_signal_level_bar->setValue(0);
    status_layout->addWidget(d_signal_level_bar, 0, 1);
    d_signal_level_label = new QLabel(tr("-inf dB"));
    d_signal_level_label->setMinimumWidth(60);
    status_layout->addWidget(d_signal_level_label, 0, 2);

    // Status text
    d_status_label = new QLabel(tr("Ready"));
    d_status_label->setStyleSheet("QLabel { color: green; }");
    status_layout->addWidget(d_status_label, 1, 0, 1, 3);

    d_main_layout->addWidget(d_status_group);
}

void TunerPanel::create_action_buttons()
{
    d_buttons_layout = new QHBoxLayout();

    d_record_audio_btn = new QPushButton(tr("Record"));
    d_record_audio_btn->setCheckable(true);
    d_record_audio_btn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    d_buttons_layout->addWidget(d_record_audio_btn);

    d_record_iq_btn = new QPushButton(tr("I/Q"));
    d_record_iq_btn->setCheckable(true);
    d_record_iq_btn->setToolTip(tr("Record I/Q data"));
    d_buttons_layout->addWidget(d_record_iq_btn);

    d_sniffer_btn = new QPushButton(tr("Sniffer"));
    d_sniffer_btn->setCheckable(true);
    d_sniffer_btn->setToolTip(tr("Enable UDP streaming"));
    d_buttons_layout->addWidget(d_sniffer_btn);

    d_buttons_layout->addStretch();
    d_main_layout->addLayout(d_buttons_layout);
}

void TunerPanel::connect_signals()
{
    // Frequency controls
    connect(d_freq_ctrl, &CFreqCtrl::newFrequency,
            this, &TunerPanel::on_frequency_changed);
    connect(d_freq_step, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TunerPanel::on_frequency_step_changed);

    // Demod controls
    connect(d_demod_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TunerPanel::on_demod_selected);
    connect(d_filter_low, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TunerPanel::on_filter_width_changed);
    connect(d_filter_high, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TunerPanel::on_filter_width_changed);
    connect(d_filter_offset, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TunerPanel::on_filter_offset_changed);

    // Audio controls
    connect(d_audio_gain_slider, &QSlider::valueChanged,
            this, &TunerPanel::on_audio_gain_changed);
    connect(d_audio_mute_checkbox, &QCheckBox::toggled,
            this, &TunerPanel::on_audio_mute_toggled);
    connect(d_audio_device_combo, &QComboBox::currentTextChanged,
            this, &TunerPanel::on_audio_device_changed);

    // Processing controls
    connect(d_agc_checkbox, &QCheckBox::toggled,
            this, &TunerPanel::on_agc_toggled);
    connect(d_squelch_slider, &QSlider::valueChanged,
            this, &TunerPanel::on_squelch_changed);

    // Enable/disable
    connect(d_enabled_checkbox, &QCheckBox::toggled,
            this, &TunerPanel::on_enabled_toggled);

    // Action buttons
    connect(d_record_audio_btn, &QPushButton::clicked,
            this, &TunerPanel::on_record_audio_clicked);
    connect(d_record_iq_btn, &QPushButton::clicked,
            this, &TunerPanel::on_record_iq_clicked);
    connect(d_sniffer_btn, &QPushButton::toggled,
            this, &TunerPanel::on_sniffer_toggled);
}

void TunerPanel::disconnect_signals()
{
    // Disconnect all signals to avoid issues during destruction
    disconnect();
}

void TunerPanel::set_receiver_channel(ReceiverChannel* channel)
{
    d_receiver = channel;
    if (channel) {
        refresh_all_settings();
    }
}

void TunerPanel::refresh_all_settings()
{
    if (!d_receiver) {
        return;
    }

    d_updating_controls = true;

    // Update frequency
    qint64 freq = static_cast<qint64>(d_receiver->get_center_freq());
    d_freq_ctrl->setFrequency(freq);

    // Update demod
    ReceiverChannel::rx_demod demod = d_receiver->get_demod();
    QString demod_str = get_demod_string(demod);
    int demod_index = d_demod_combo->findText(demod_str);
    if (demod_index >= 0) {
        d_demod_combo->setCurrentIndex(demod_index);
    }

    // Update audio controls
    float audio_gain = d_receiver->get_audio_gain();
    d_audio_gain_slider->setValue(static_cast<int>(audio_gain * 100));
    d_audio_gain_label->setText(tr("%1%").arg(static_cast<int>(audio_gain * 100)));
    bool muted = d_receiver->get_audio_mute();
    d_audio_mute_checkbox->setChecked(muted);

    // Update processing
    bool agc_on = d_receiver->get_agc_on();
    d_agc_checkbox->setChecked(agc_on);

    // Update enabled state
    bool enabled = d_receiver->is_enabled();
    d_enabled_checkbox->setChecked(enabled);
    set_controls_enabled(enabled);

    d_updating_controls = false;
}

void TunerPanel::set_tuner_name(const QString& name)
{
    d_tuner_name_label->setText(name);
    d_tuner_group->setTitle(name);
}

void TunerPanel::set_controls_enabled(bool enabled)
{
    d_freq_group->setEnabled(enabled);
    d_demod_group->setEnabled(enabled);
    d_audio_group->setEnabled(enabled);
    d_processing_group->setEnabled(enabled);
    d_buttons_layout->parentWidget()->setEnabled(enabled);
}

void TunerPanel::set_frequency_range(double min_freq, double max_freq)
{
    d_min_freq = min_freq;
    d_max_freq = max_freq;
    d_freq_ctrl->setup(11, static_cast<quint64>(min_freq),
                      static_cast<quint64>(max_freq), 1, FCTL_UNIT_NONE);
}

void TunerPanel::update_signal_level(float level_db)
{
    // Convert dB to 0-100 scale (assuming range -120 to 0 dB)
    int level_percent = static_cast<int>((level_db + 120.0f) * 100.0f / 120.0f);
    level_percent = qBound(0, level_percent, 100);

    d_signal_level_bar->setValue(level_percent);
    d_signal_level_label->setText(tr("%1 dB").arg(level_db, 0, 'f', 1));
}

void TunerPanel::on_frequency_changed(qint64 freq)
{
    if (d_updating_controls) {
        return;
    }

    if (d_receiver) {
        d_receiver->set_center_freq(static_cast<double>(freq));
    }
    emit frequency_changed(d_tuner_id, freq);
}

void TunerPanel::on_frequency_step_changed()
{
    // d_freq_ctrl->setFreqStep /* not available */(d_freq_step->value());
}

void TunerPanel::on_demod_selected(int index)
{
    if (d_updating_controls) {
        return;
    }

    QString demod_str = d_demod_combo->itemText(index);
    ReceiverChannel::rx_demod demod = get_demod_from_string(demod_str);

    if (d_receiver) {
        d_receiver->set_demod(demod);
    }

    update_demod_settings();
    emit demod_changed(d_tuner_id, demod);
}

void TunerPanel::on_filter_width_changed()
{
    if (d_updating_controls) {
        return;
    }

    double low = d_filter_low->value();
    double high = d_filter_high->value();

    if (d_receiver) {
        d_receiver->set_filter(low, high, 1000.0); // 1kHz transition width
    }
    emit filter_changed(d_tuner_id, low, high);
}

void TunerPanel::on_filter_offset_changed()
{
    if (d_updating_controls) {
        return;
    }

    double offset = d_filter_offset->value();
    if (d_receiver) {
        d_receiver->set_filter_offset(offset);
    }
}

void TunerPanel::on_audio_gain_changed(int value)
{
    if (d_updating_controls) {
        return;
    }

    float gain = value / 100.0f;
    d_audio_gain_label->setText(tr("%1%").arg(value));

    if (d_receiver) {
        d_receiver->set_audio_gain(gain);
    }
    emit audio_gain_changed(d_tuner_id, gain);
}

void TunerPanel::on_audio_mute_toggled(bool muted)
{
    if (d_updating_controls) {
        return;
    }

    if (d_receiver) {
        d_receiver->set_audio_mute(muted);
    }
    emit audio_mute_changed(d_tuner_id, muted);
}

void TunerPanel::on_audio_device_changed(const QString& device)
{
    if (d_updating_controls) {
        return;
    }

    if (d_receiver) {
        d_receiver->set_audio_device(device.toStdString());
    }
}

void TunerPanel::on_agc_toggled(bool enabled)
{
    if (d_updating_controls) {
        return;
    }

    if (d_receiver) {
        d_receiver->set_agc_on(enabled);
    }
}

void TunerPanel::on_squelch_changed(int value)
{
    if (d_updating_controls) {
        return;
    }

    d_squelch_label->setText(tr("%1 dB").arg(value));
    // Note: Squelch implementation would go here
}

void TunerPanel::on_record_audio_clicked()
{
    if (!d_recording_audio) {
        // Start recording
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString filename = QFileDialog::getSaveFileName(
            this, tr("Save Audio Recording"),
            QString("%1/gqrx_tuner%2_%3.wav").arg(defaultPath).arg(d_tuner_id).arg(timestamp),
            tr("WAV Files (*.wav)"));

        if (!filename.isEmpty()) {
            d_recording_audio = true;
            d_record_audio_btn->setText(tr("Stop"));
            d_record_audio_btn->setStyleSheet("QPushButton { background-color: red; color: white; }");
            emit start_audio_recording(d_tuner_id, filename);
        } else {
            d_record_audio_btn->setChecked(false);
        }
    } else {
        // Stop recording
        d_recording_audio = false;
        d_record_audio_btn->setText(tr("Record"));
        d_record_audio_btn->setStyleSheet("");
        emit stop_audio_recording(d_tuner_id);
    }
}

void TunerPanel::on_record_iq_clicked()
{
    if (!d_recording_iq) {
        // Start I/Q recording
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString filename = QFileDialog::getSaveFileName(
            this, tr("Save I/Q Recording"),
            QString("%1/gqrx_iq_tuner%2_%3.raw").arg(defaultPath).arg(d_tuner_id).arg(timestamp),
            tr("Raw Files (*.raw);;All Files (*)"));

        if (!filename.isEmpty()) {
            d_recording_iq = true;
            d_record_iq_btn->setText(tr("Stop"));
            d_record_iq_btn->setStyleSheet("QPushButton { background-color: red; color: white; }");
            emit start_iq_recording(d_tuner_id, filename);
        } else {
            d_record_iq_btn->setChecked(false);
        }
    } else {
        // Stop I/Q recording
        d_recording_iq = false;
        d_record_iq_btn->setText(tr("I/Q"));
        d_record_iq_btn->setStyleSheet("");
        emit stop_iq_recording(d_tuner_id);
    }
}

void TunerPanel::on_sniffer_toggled(bool enabled)
{
    if (d_updating_controls) {
        return;
    }

    d_sniffer_active = enabled;
    if (enabled) {
        d_sniffer_btn->setText(tr("Stop"));
        d_sniffer_btn->setStyleSheet("QPushButton { background-color: orange; }");
    } else {
        d_sniffer_btn->setText(tr("Sniffer"));
        d_sniffer_btn->setStyleSheet("");
    }
}

void TunerPanel::on_enabled_toggled(bool enabled)
{
    if (d_updating_controls) {
        return;
    }

    set_controls_enabled(enabled);

    if (d_receiver) {
        d_receiver->set_enabled(enabled);
    }

    d_status_label->setText(enabled ? tr("Ready") : tr("Disabled"));
    d_status_label->setStyleSheet(enabled ? "QLabel { color: green; }" : "QLabel { color: gray; }");

    emit enabled_changed(d_tuner_id, enabled);
}

void TunerPanel::contextMenuEvent(QContextMenuEvent *event)
{
    emit context_menu_requested(d_tuner_id, event->globalPos());
}

void TunerPanel::update_demod_combo()
{
    d_demod_combo->clear();
    d_demod_combo->addItem("OFF");
    d_demod_combo->addItem("Raw");
    d_demod_combo->addItem("AM");
    d_demod_combo->addItem("NFM");
    d_demod_combo->addItem("WFM (M)");
    d_demod_combo->addItem("WFM (S)");
    d_demod_combo->addItem("WFM (OIRT)");
    d_demod_combo->addItem("SSB");
    d_demod_combo->addItem("AM-Sync");
}

void TunerPanel::update_audio_device_combo()
{
    d_audio_device_combo->clear();
    d_audio_device_combo->addItem("Default");
    d_audio_device_combo->addItem("pulse");
    // Additional audio devices would be populated here based on system
}

void TunerPanel::update_demod_settings()
{
    QString demod_str = d_demod_combo->currentText();

    // Update filter ranges based on demodulation mode
    if (demod_str == "WFM (M)" || demod_str == "WFM (S)" || demod_str == "WFM (OIRT)") {
        d_filter_low->setRange(-100000, 0);
        d_filter_high->setRange(0, 100000);
        d_filter_low->setValue(-80000);
        d_filter_high->setValue(80000);
    } else if (demod_str == "AM" || demod_str == "AM-Sync") {
        d_filter_low->setRange(-10000, 0);
        d_filter_high->setRange(0, 10000);
        d_filter_low->setValue(-5000);
        d_filter_high->setValue(5000);
    } else if (demod_str == "SSB") {
        d_filter_low->setRange(-5000, 5000);
        d_filter_high->setRange(-5000, 5000);
        d_filter_low->setValue(300);
        d_filter_high->setValue(3000);
    } else { // NFM, Raw, OFF
        d_filter_low->setRange(-25000, 0);
        d_filter_high->setRange(0, 25000);
        d_filter_low->setValue(-5000);
        d_filter_high->setValue(5000);
    }
}

QString TunerPanel::get_demod_string(ReceiverChannel::rx_demod demod) const
{
    QString result;
    switch (demod) {
        case ReceiverChannel::RX_DEMOD_OFF:
            result = "OFF";
            break;
        case ReceiverChannel::RX_DEMOD_NONE:
            result = "Raw";
            break;
        case ReceiverChannel::RX_DEMOD_AM:
            result = "AM";
            break;
        case ReceiverChannel::RX_DEMOD_NFM:
            result = "NFM";
            break;
        case ReceiverChannel::RX_DEMOD_WFM_M:
            result = "WFM (M)";
            break;
        case ReceiverChannel::RX_DEMOD_WFM_S:
            result = "WFM (S)";
            break;
        case ReceiverChannel::RX_DEMOD_WFM_S_OIRT:
            result = "WFM (OIRT)";
            break;
        case ReceiverChannel::RX_DEMOD_SSB:
            result = "SSB";
            break;
        case ReceiverChannel::RX_DEMOD_AMSYNC:
            result = "AM-Sync";
            break;
        default:
            result = "NFM";
            break;
    }
    return result;
}

ReceiverChannel::rx_demod TunerPanel::get_demod_from_string(const QString& str) const
{
    ReceiverChannel::rx_demod result;

    if (str == "OFF") {
        result = ReceiverChannel::RX_DEMOD_OFF;
    } else if (str == "Raw") {
        result = ReceiverChannel::RX_DEMOD_NONE;
    } else if (str == "AM") {
        result = ReceiverChannel::RX_DEMOD_AM;
    } else if (str == "NFM") {
        result = ReceiverChannel::RX_DEMOD_NFM;
    } else if (str == "WFM (M)") {
        result = ReceiverChannel::RX_DEMOD_WFM_M;
    } else if (str == "WFM (S)") {
        result = ReceiverChannel::RX_DEMOD_WFM_S;
    } else if (str == "WFM (OIRT)") {
        result = ReceiverChannel::RX_DEMOD_WFM_S_OIRT;
    } else if (str == "SSB") {
        result = ReceiverChannel::RX_DEMOD_SSB;
    } else if (str == "AM-Sync") {
        result = ReceiverChannel::RX_DEMOD_AMSYNC;
    } else {
        result = ReceiverChannel::RX_DEMOD_NFM;
    }

    return result;
}


void TunerPanel::update_filter_range()
{
    // Update filter range based on current demodulation mode
    if (d_receiver) {
        ReceiverChannel::rx_demod demod = d_receiver->get_demod();
        switch (demod) {
            case ReceiverChannel::RX_DEMOD_AM:
                // Set AM filter range
                d_filter_low->setRange(-10000, -100);
                d_filter_low->setValue(-5000);
                d_filter_high->setRange(100, 10000);
                d_filter_high->setValue(5000);
                break;
            case ReceiverChannel::RX_DEMOD_NFM:
                // Set FM filter range
                d_filter_low->setRange(-20000, -1000);
                d_filter_low->setValue(-12500);
                d_filter_high->setRange(1000, 20000);
                d_filter_high->setValue(12500);
                break;
            case ReceiverChannel::RX_DEMOD_WFM_M:
            case ReceiverChannel::RX_DEMOD_WFM_S:
                // Set wide FM filter range
                d_filter_low->setRange(-200000, -50000);
                d_filter_low->setValue(-100000);
                d_filter_high->setRange(50000, 200000);
                d_filter_high->setValue(100000);
                break;
            case ReceiverChannel::RX_DEMOD_SSB:
                // Set SSB filter range
                d_filter_low->setRange(-5000, -100);
                d_filter_low->setValue(-2400);
                d_filter_high->setRange(100, 5000);
                d_filter_high->setValue(2400);
                break;
            default:
                d_filter_low->setRange(-50000, -100);
                d_filter_low->setValue(-10000);
                d_filter_high->setRange(100, 50000);
                d_filter_high->setValue(10000);
                break;
        }
    }
}
