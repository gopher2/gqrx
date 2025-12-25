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
#ifndef RECORDING_CONFIG_H
#define RECORDING_CONFIG_H

#include <QString>
#include <QDir>
#include <QSettings>

/**
 * @brief IQ file format options.
 */
enum class IqFileFormat {
    RAW_CF32,       ///< Raw complex float32 (.raw, .cf32)
    RAW_CS16,       ///< Raw complex int16 (.cs16)
    SIGMF,          ///< SigMF format (.sigmf-data + .sigmf-meta)
    WAV_IQ          ///< 2-channel WAV (I/Q as stereo)
};

/**
 * @brief Audio file format options.
 */
enum class AudioFileFormat {
    WAV,            ///< Uncompressed WAV
    FLAC,           ///< Lossless FLAC compression
    OGG             ///< Lossy OGG Vorbis
};

/**
 * @brief WAV sample format options.
 */
enum class WavSampleFormat {
    PCM_16,         ///< 16-bit signed PCM
    PCM_32,         ///< 32-bit signed PCM
    FLOAT_32        ///< 32-bit float
};

/**
 * @brief Recording trigger mode.
 */
enum class RecordingMode {
    CONSTANT,           ///< Record continuously
    SQUELCH_PER_CALL,   ///< New file per squelch break (transmission)
    SQUELCH_CHUNKS      ///< Time-based chunks, only audio when squelch open
};

/**
 * @brief IQ recording tap point.
 */
enum class IqTapPoint {
    BEFORE_DDC,     ///< Full SDR bandwidth (20+ MHz)
    AFTER_DDC,      ///< Channel bandwidth (~1 MHz)
    AFTER_FILTER    ///< Baseband (~96 kHz)
};

/**
 * @brief SigMF metadata defaults.
 */
struct SigMFConfig {
    QString author;
    QString description;
    QString license = "CC0";
    QString hw;             ///< Hardware description (auto-filled from SDR device)

    void save(QSettings& settings) const {
        settings.setValue("sigmf/author", author);
        settings.setValue("sigmf/description", description);
        settings.setValue("sigmf/license", license);
        settings.setValue("sigmf/hw", hw);
    }

    void load(QSettings& settings) {
        author = settings.value("sigmf/author", "").toString();
        description = settings.value("sigmf/description", "SDR Recording").toString();
        license = settings.value("sigmf/license", "CC0").toString();
        hw = settings.value("sigmf/hw", "").toString();
    }
};

/**
 * @brief Squelch-triggered recording settings.
 */
struct SquelchRecordingConfig {
    int pre_buffer_ms = 500;        ///< Buffer before squelch opens
    int post_buffer_ms = 1000;      ///< Continue after squelch closes
    int min_duration_ms = 200;      ///< Ignore transmissions shorter than this
    int chunk_duration_minutes = 5; ///< For SQUELCH_CHUNKS mode

    void save(QSettings& settings, const QString& prefix) const {
        settings.setValue(prefix + "/pre_buffer_ms", pre_buffer_ms);
        settings.setValue(prefix + "/post_buffer_ms", post_buffer_ms);
        settings.setValue(prefix + "/min_duration_ms", min_duration_ms);
        settings.setValue(prefix + "/chunk_duration_minutes", chunk_duration_minutes);
    }

    void load(QSettings& settings, const QString& prefix) {
        pre_buffer_ms = settings.value(prefix + "/pre_buffer_ms", 500).toInt();
        post_buffer_ms = settings.value(prefix + "/post_buffer_ms", 1000).toInt();
        min_duration_ms = settings.value(prefix + "/min_duration_ms", 200).toInt();
        chunk_duration_minutes = settings.value(prefix + "/chunk_duration_minutes", 5).toInt();
    }
};

/**
 * @brief Global recording configuration (TunerManager level).
 */
struct RecordingConfig {
    // Folder settings
    QString recording_folder;       ///< Base folder for all recordings

    // IQ recording defaults
    IqFileFormat iq_format = IqFileFormat::SIGMF;
    IqTapPoint iq_tap_point = IqTapPoint::AFTER_DDC;  // After DDC (~1 MHz), avoids hierarchy conflict

    // Audio recording defaults
    AudioFileFormat audio_format = AudioFileFormat::WAV;
    WavSampleFormat wav_sample_format = WavSampleFormat::PCM_16;
    bool audio_stereo = false;      ///< Mono by default

    // Filename template
    // Variables: {date}, {time}, {datetime}, {freq}, {freq_mhz}, {mode},
    //            {tuner}, {tuner_name}, {type}, {srate}
    QString filename_template = "{datetime}_T{tuner}_{freq_mhz}MHz_{mode}_{type}";

    // SigMF metadata
    SigMFConfig sigmf;

    // File splitting
    int auto_split_minutes = 0;     ///< 0 = no split, else split at this interval (minutes)

    // Default squelch recording settings
    SquelchRecordingConfig squelch_config;

    /**
     * @brief Get the default recording folder.
     */
    static QString getDefaultFolder() {
        return QDir::homePath() + "/SDR_Recordings";
    }

    /**
     * @brief Save configuration to QSettings.
     */
    void save(QSettings& settings) const {
        settings.beginGroup("Recording");
        settings.setValue("folder", recording_folder);
        settings.setValue("iq_format", static_cast<int>(iq_format));
        settings.setValue("iq_tap_point", static_cast<int>(iq_tap_point));
        settings.setValue("audio_format", static_cast<int>(audio_format));
        settings.setValue("wav_sample_format", static_cast<int>(wav_sample_format));
        settings.setValue("audio_stereo", audio_stereo);
        settings.setValue("filename_template", filename_template);
        settings.setValue("auto_split_minutes", auto_split_minutes);
        sigmf.save(settings);
        squelch_config.save(settings, "squelch");
        settings.endGroup();
    }

    /**
     * @brief Load configuration from QSettings.
     */
    void load(QSettings& settings) {
        settings.beginGroup("Recording");
        recording_folder = settings.value("folder", getDefaultFolder()).toString();
        iq_format = static_cast<IqFileFormat>(
            settings.value("iq_format", static_cast<int>(IqFileFormat::SIGMF)).toInt());
        iq_tap_point = static_cast<IqTapPoint>(
            settings.value("iq_tap_point", static_cast<int>(IqTapPoint::AFTER_DDC)).toInt());
        audio_format = static_cast<AudioFileFormat>(
            settings.value("audio_format", static_cast<int>(AudioFileFormat::WAV)).toInt());
        wav_sample_format = static_cast<WavSampleFormat>(
            settings.value("wav_sample_format", static_cast<int>(WavSampleFormat::PCM_16)).toInt());
        audio_stereo = settings.value("audio_stereo", false).toBool();
        filename_template = settings.value("filename_template",
            "{datetime}_T{tuner}_{freq_mhz}MHz_{mode}_{type}").toString();
        auto_split_minutes = settings.value("auto_split_minutes", 0).toInt();
        sigmf.load(settings);
        squelch_config.load(settings, "squelch");
        settings.endGroup();
    }
};

/**
 * @brief Per-tuner recording configuration.
 */
struct TunerRecordingConfig {
    bool record_iq = false;             ///< Enable IQ recording for this tuner
    bool record_audio = true;           ///< Enable audio recording for this tuner

    RecordingMode iq_mode = RecordingMode::CONSTANT;
    RecordingMode audio_mode = RecordingMode::CONSTANT;  // Default to CONSTANT until squelch wired up

    // Per-tuner squelch settings (can override global)
    bool use_custom_squelch_config = false;
    SquelchRecordingConfig squelch_config;

    // Per-tuner filename override
    bool use_custom_filename = false;
    QString custom_filename_template;

    /**
     * @brief Save configuration to QSettings.
     */
    void save(QSettings& settings, int tuner_id) const {
        QString prefix = QString("Tuner%1/Recording").arg(tuner_id);
        settings.beginGroup(prefix);
        settings.setValue("record_iq", record_iq);
        settings.setValue("record_audio", record_audio);
        settings.setValue("iq_mode", static_cast<int>(iq_mode));
        settings.setValue("audio_mode", static_cast<int>(audio_mode));
        settings.setValue("use_custom_squelch_config", use_custom_squelch_config);
        settings.setValue("use_custom_filename", use_custom_filename);
        settings.setValue("custom_filename_template", custom_filename_template);
        if (use_custom_squelch_config) {
            squelch_config.save(settings, "custom_squelch");
        }
        settings.endGroup();
    }

    /**
     * @brief Load configuration from QSettings.
     */
    void load(QSettings& settings, int tuner_id) {
        QString prefix = QString("Tuner%1/Recording").arg(tuner_id);
        settings.beginGroup(prefix);
        record_iq = settings.value("record_iq", false).toBool();
        record_audio = settings.value("record_audio", true).toBool();
        iq_mode = static_cast<RecordingMode>(
            settings.value("iq_mode", static_cast<int>(RecordingMode::CONSTANT)).toInt());
        audio_mode = static_cast<RecordingMode>(
            settings.value("audio_mode", static_cast<int>(RecordingMode::CONSTANT)).toInt());
        use_custom_squelch_config = settings.value("use_custom_squelch_config", false).toBool();
        use_custom_filename = settings.value("use_custom_filename", false).toBool();
        custom_filename_template = settings.value("custom_filename_template", "").toString();
        if (use_custom_squelch_config) {
            squelch_config.load(settings, "custom_squelch");
        }
        settings.endGroup();
    }
};

#endif // RECORDING_CONFIG_H
