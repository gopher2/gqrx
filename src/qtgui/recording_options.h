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
#ifndef RECORDING_OPTIONS_H
#define RECORDING_OPTIONS_H

#include <QDialog>
#include <QString>
#include "applications/gqrx/recording_config.h"

namespace Ui {
    class CRecordingOptions;
}

/**
 * @brief Dialog for configuring recording options.
 *
 * Provides UI for:
 * - Recording folder and filename template
 * - IQ recording format and tap point
 * - Audio recording format and squelch settings
 * - SigMF metadata defaults
 */
class CRecordingOptions : public QDialog
{
    Q_OBJECT

public:
    explicit CRecordingOptions(QWidget *parent = nullptr);
    ~CRecordingOptions();

    // =========================================================================
    // Configuration accessors
    // =========================================================================

    /** @brief Get the full recording configuration. */
    RecordingConfig getConfig() const;

    /** @brief Set the recording configuration. */
    void setConfig(const RecordingConfig& config);

    // Individual property accessors
    QString getRecordingFolder() const;
    void setRecordingFolder(const QString& folder);

    QString getFilenameTemplate() const;
    void setFilenameTemplate(const QString& tmpl);

    IqFileFormat getIqFormat() const;
    void setIqFormat(IqFileFormat format);

    IqTapPoint getIqTapPoint() const;
    void setIqTapPoint(IqTapPoint tap);

    AudioFileFormat getAudioFormat() const;
    void setAudioFormat(AudioFileFormat format);

    WavSampleFormat getWavFormat() const;
    void setWavFormat(WavSampleFormat format);

    SquelchRecordingConfig getSquelchConfig() const;
    void setSquelchConfig(const SquelchRecordingConfig& config);

    SigMFConfig getSigMFConfig() const;
    void setSigMFConfig(const SigMFConfig& config);

signals:
    /** @brief Emitted when configuration changes are applied. */
    void configChanged(const RecordingConfig& config);

private slots:
    void on_recFolderButton_clicked();
    void on_filenameEdit_textChanged(const QString& text);
    void updateFilenamePreview();

private:
    Ui::CRecordingOptions *ui;
};

#endif // RECORDING_OPTIONS_H
