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
#ifndef IQ_RECORDER_H
#define IQ_RECORDER_H

#include <gnuradio/sync_block.h>
#include <gnuradio/blocks/file_sink.h>
#include <QString>
#include <QDateTime>
#include <memory>
#include <mutex>
#include <fstream>
#include <deque>

#include "applications/gqrx/recording_config.h"

class iq_recorder;
typedef std::shared_ptr<iq_recorder> iq_recorder_sptr;

/**
 * @brief Create an IQ recorder block.
 * @param sample_rate Sample rate of the IQ data
 * @param center_freq Center frequency in Hz
 * @return Shared pointer to new IQ recorder
 */
iq_recorder_sptr make_iq_recorder(double sample_rate, double center_freq = 0.0);

/**
 * @brief IQ Recorder block for recording complex IQ data.
 *
 * Supports multiple file formats:
 * - RAW_CF32: Raw complex float32
 * - RAW_CS16: Raw complex int16 (scaled)
 * - SIGMF: SigMF format with metadata sidecar
 * - WAV_IQ: 2-channel WAV file (I/Q as stereo)
 *
 * Usage:
 *   auto recorder = make_iq_recorder(2e6, 145.5e6);
 *   recorder->set_format(IqFileFormat::SIGMF);
 *   recorder->start_recording("/path/to/file");
 *   // ... recording happens ...
 *   recorder->stop_recording();
 */
class iq_recorder : public gr::sync_block
{
    friend iq_recorder_sptr make_iq_recorder(double sample_rate, double center_freq);

public:
    ~iq_recorder() override;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set the IQ file format.
     */
    void set_format(IqFileFormat format) { d_format = format; }

    /**
     * @brief Set the sample rate (for metadata).
     */
    void set_sample_rate(double rate);
    double get_sample_rate() const { return d_sample_rate; }

    /**
     * @brief Set the center frequency (for metadata).
     */
    void set_center_freq(double freq);
    double get_center_freq() const { return d_center_freq; }

    /**
     * @brief Set SigMF metadata configuration.
     */
    void set_sigmf_config(const SigMFConfig& config) { d_sigmf_config = config; }

    /**
     * @brief Set auto split interval in minutes (0 = no splitting).
     */
    void set_split_minutes(int minutes) { d_split_minutes = minutes; }

    /**
     * @brief Set squelch-triggered recording mode.
     */
    void set_recording_mode(RecordingMode mode) { d_recording_mode = mode; }

    /**
     * @brief Set squelch state for squelch-triggered modes.
     */
    void set_squelch_open(bool open);

    /**
     * @brief Set pre-buffer duration in milliseconds.
     */
    void set_pre_buffer_ms(int ms) { d_pre_buffer_ms = ms; }

    // =========================================================================
    // Recording Control
    // =========================================================================

    /**
     * @brief Start recording to a file.
     * @param filepath Full path to the output file (without extension)
     * @return True if recording started successfully
     */
    bool start_recording(const QString& filepath);

    /**
     * @brief Stop recording and close the file.
     */
    void stop_recording();

    /**
     * @brief Check if currently recording.
     */
    bool is_recording() const { return d_recording; }

    /**
     * @brief Get the number of samples recorded.
     */
    uint64_t get_samples_recorded() const { return d_samples_recorded; }

    /**
     * @brief Get the recording duration in seconds.
     */
    double get_duration() const;

    // =========================================================================
    // GNU Radio Interface
    // =========================================================================

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star& output_items) override;

private:
    iq_recorder(double sample_rate, double center_freq);

    void write_sigmf_metadata();
    QString get_extension() const;
    void rotate_file();
    void finalize_current_file();
    bool open_new_file();

    // Configuration
    IqFileFormat d_format;
    double d_sample_rate;
    double d_center_freq;
    SigMFConfig d_sigmf_config;
    int d_split_minutes;
    RecordingMode d_recording_mode;
    int d_pre_buffer_ms;

    // Recording state
    bool d_recording;
    bool d_armed;  // For squelch modes: armed but waiting for squelch
    bool d_squelch_open;
    QString d_filepath;
    QString d_base_filepath;  // Without extension or number
    int d_file_number;
    QDateTime d_start_time;
    QDateTime d_file_start_time;
    uint64_t d_samples_recorded;
    uint64_t d_total_samples_recorded;
    uint64_t d_file_size;

    // Pre-buffer for squelch mode
    std::deque<gr_complex> d_pre_buffer;
    size_t d_pre_buffer_size;  // Max samples in pre-buffer

    // File output
    std::ofstream d_file;
    mutable std::mutex d_mutex;
};

#endif // IQ_RECORDER_H
