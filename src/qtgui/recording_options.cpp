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
#include "recording_options.h"
#include "ui_recording_options.h"
#include "applications/gqrx/filename_template.h"
#include <QFileDialog>

CRecordingOptions::CRecordingOptions(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::CRecordingOptions)
{

    ui->setupUi(this);

    // Connect filename edit to preview update
    connect(ui->filenameEdit, &QLineEdit::textChanged,
            this, &CRecordingOptions::updateFilenamePreview);

    // Set default filename template
    QString defaultTemplate = "{datetime}_{freq_mhz}MHz_{mode}_{type}";
    ui->filenameEdit->setText(defaultTemplate);
    updateFilenamePreview();
}

CRecordingOptions::~CRecordingOptions()
{
    delete ui;
}

RecordingConfig CRecordingOptions::getConfig() const
{
    RecordingConfig config;

    config.recording_folder = getRecordingFolder();

    config.filename_template = getFilenameTemplate();

    config.iq_format = getIqFormat();

    config.iq_tap_point = getIqTapPoint();

    config.audio_format = getAudioFormat();

    config.wav_sample_format = getWavFormat();

    config.squelch_config = getSquelchConfig();

    config.sigmf = getSigMFConfig();

    return config;
}

void CRecordingOptions::setConfig(const RecordingConfig& config)
{

    setRecordingFolder(config.recording_folder);
    setFilenameTemplate(config.filename_template);
    setIqFormat(config.iq_format);
    setIqTapPoint(config.iq_tap_point);
    setAudioFormat(config.audio_format);
    setWavFormat(config.wav_sample_format);
    setSquelchConfig(config.squelch_config);
    setSigMFConfig(config.sigmf);
}

QString CRecordingOptions::getRecordingFolder() const
{
    QString folder = ui->recFolderEdit->text();
    return folder;
}

void CRecordingOptions::setRecordingFolder(const QString& folder)
{
    ui->recFolderEdit->setText(folder);
}

QString CRecordingOptions::getFilenameTemplate() const
{
    QString tmpl = ui->filenameEdit->text();
    return tmpl;
}

void CRecordingOptions::setFilenameTemplate(const QString& tmpl)
{
    ui->filenameEdit->setText(tmpl);
}

IqFileFormat CRecordingOptions::getIqFormat() const
{
    int index = ui->iqFormatCombo->currentIndex();

    switch (index) {
        case 0:
            return IqFileFormat::SIGMF;
        case 1:
            return IqFileFormat::RAW_CF32;
        case 2:
            return IqFileFormat::RAW_CS16;
        case 3:
            return IqFileFormat::WAV_IQ;
        default:
            return IqFileFormat::SIGMF;
    }
}

void CRecordingOptions::setIqFormat(IqFileFormat format)
{

    switch (format) {
        case IqFileFormat::SIGMF:
            ui->iqFormatCombo->setCurrentIndex(0);
            break;
        case IqFileFormat::RAW_CF32:
            ui->iqFormatCombo->setCurrentIndex(1);
            break;
        case IqFileFormat::RAW_CS16:
            ui->iqFormatCombo->setCurrentIndex(2);
            break;
        case IqFileFormat::WAV_IQ:
            ui->iqFormatCombo->setCurrentIndex(3);
            break;
    }
}

IqTapPoint CRecordingOptions::getIqTapPoint() const
{
    int index = ui->tapPointCombo->currentIndex();

    switch (index) {
        case 0:
            return IqTapPoint::AFTER_DDC;
        case 1:
            return IqTapPoint::AFTER_FILTER;
        default:
            return IqTapPoint::AFTER_DDC;
    }
}

void CRecordingOptions::setIqTapPoint(IqTapPoint tap)
{

    switch (tap) {
        case IqTapPoint::BEFORE_DDC:
            ui->tapPointCombo->setCurrentIndex(0);
            break; // Map to AFTER_DDC as fallback
        case IqTapPoint::AFTER_DDC:
            ui->tapPointCombo->setCurrentIndex(0);
            break;
        case IqTapPoint::AFTER_FILTER:
            ui->tapPointCombo->setCurrentIndex(1);
            break;
    }
}

AudioFileFormat CRecordingOptions::getAudioFormat() const
{
    int index = ui->audioFormatCombo->currentIndex();

    switch (index) {
        case 0:
            return AudioFileFormat::WAV;
        case 1:
            return AudioFileFormat::FLAC;
        case 2:
            return AudioFileFormat::OGG;
        default:
            return AudioFileFormat::WAV;
    }
}

void CRecordingOptions::setAudioFormat(AudioFileFormat format)
{

    switch (format) {
        case AudioFileFormat::WAV:
            ui->audioFormatCombo->setCurrentIndex(0);
            break;
        case AudioFileFormat::FLAC:
            ui->audioFormatCombo->setCurrentIndex(1);
            break;
        case AudioFileFormat::OGG:
            ui->audioFormatCombo->setCurrentIndex(2);
            break;
    }
}

WavSampleFormat CRecordingOptions::getWavFormat() const
{
    int index = ui->wavFormatCombo->currentIndex();

    switch (index) {
        case 0:
            return WavSampleFormat::PCM_16;
        case 1:
            return WavSampleFormat::PCM_32;
        case 2:
            return WavSampleFormat::FLOAT_32;
        default:
            return WavSampleFormat::PCM_16;
    }
}

void CRecordingOptions::setWavFormat(WavSampleFormat format)
{

    switch (format) {
        case WavSampleFormat::PCM_16:
            ui->wavFormatCombo->setCurrentIndex(0);
            break;
        case WavSampleFormat::PCM_32:
            ui->wavFormatCombo->setCurrentIndex(1);
            break;
        case WavSampleFormat::FLOAT_32:
            ui->wavFormatCombo->setCurrentIndex(2);
            break;
    }
}

SquelchRecordingConfig CRecordingOptions::getSquelchConfig() const
{
    SquelchRecordingConfig config;

    config.pre_buffer_ms = ui->preBufferSpin->value();

    config.post_buffer_ms = ui->postBufferSpin->value();

    config.min_duration_ms = ui->minDurationSpin->value();

    config.chunk_duration_minutes = ui->chunkDurationSpin->value();

    return config;
}

void CRecordingOptions::setSquelchConfig(const SquelchRecordingConfig& config)
{

    ui->preBufferSpin->setValue(config.pre_buffer_ms);
    ui->postBufferSpin->setValue(config.post_buffer_ms);
    ui->minDurationSpin->setValue(config.min_duration_ms);
    ui->chunkDurationSpin->setValue(config.chunk_duration_minutes);
}

SigMFConfig CRecordingOptions::getSigMFConfig() const
{
    SigMFConfig config;

    config.author = ui->authorEdit->text();

    config.description = ui->descriptionEdit->text();

    config.license = ui->licenseCombo->currentText();

    config.hw = ui->hardwareEdit->text();

    return config;
}

void CRecordingOptions::setSigMFConfig(const SigMFConfig& config)
{

    ui->authorEdit->setText(config.author);
    ui->descriptionEdit->setText(config.description);
    ui->licenseCombo->setCurrentText(config.license);
    ui->hardwareEdit->setText(config.hw);
}

void CRecordingOptions::on_recFolderButton_clicked()
{
    QString currentDir = ui->recFolderEdit->text();

    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Recording Folder"),
        currentDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );


    if (!dir.isEmpty()) {
        ui->recFolderEdit->setText(dir);
    }
}

void CRecordingOptions::on_filenameEdit_textChanged(const QString& text)
{
    updateFilenamePreview();
}

void CRecordingOptions::updateFilenamePreview()
{
    QString template_text = ui->filenameEdit->text();

    QString preview = FilenameTemplate::preview(template_text);

    QString full_preview = preview + ".wav";

    ui->filenamePreview->setText(full_preview);
}
