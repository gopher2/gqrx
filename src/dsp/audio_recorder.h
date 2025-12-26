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
#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include <gnuradio/sync_block.h>
#include <QString>
#include <QDateTime>
#include <memory>
#include <mutex>
#include <fstream>
#include <vector>
#include <deque>
#include <functional>

#include "applications/gqrx/recording_config.h"

class audio_recorder;
typedef std::shared_ptr<audio_recorder> audio_recorder_sptr;

/**
 * @brief Create an audio recorder block.
 * @param sample_rate Audio sample rate in Hz
 * @return Shared pointer to new audio recorder
 */
audio_recorder_sptr make_audio_recorder(int sample_rate);

/**
 * @brief Recording state for squelch-triggered recording.
 */
enum class RecordingState {
    IDLE,               ///< Not recording, waiting for squelch to open
    PRE_BUFFERING,      ///< Continuously filling pre-buffer (always in IDLE)
    RECORDING,          ///< Squelch open, actively recording
    POST_BUFFER,        ///< Squelch closed, recording post-buffer
    CHUNK_WAITING       ///< Chunk mode: chunk complete, waiting for next squelch
};

/**
 * @brief Audio Recorder block with squelch-triggered modes.
 *
 * Supports multiple recording modes:
 * - CONSTANT: Record continuously
 * - SQUELCH_PER_CALL: New file per transmission with pre/post buffer
 * - SQUELCH_CHUNKS: Time-based chunks, only saves chunks with activity
 *
 * Usage:
 *   auto recorder = make_audio_recorder(48000);
 *   recorder->set_mode(RecordingMode::SQUELCH_PER_CALL);
 *   recorder->set_squelch_config(config);
 *   recorder->start_recording("/path/to/base");
 *   // ... recording happens based on squelch state ...
 *   recorder->set_squelch_open(true);  // Call from squelch detector
 *   // ... transmission ...
 *   recorder->set_squelch_open(false);
 *   recorder->stop_recording();
 */
class audio_recorder : public gr::sync_block
{
    friend audio_recorder_sptr make_audio_recorder(int sample_rate);

public:
    ~audio_recorder() override;

    // =========================================================================
    // Configuration
    // =========================================================================

    /**
     * @brief Set the audio file format.
     */
    void set_format(AudioFileFormat format) { d_format = format; }

    /**
     * @brief Set the WAV sample format.
     */
    void set_wav_format(WavSampleFormat format) { d_wav_format = format; }

    /**
     * @brief Set stereo mode (default: mono).
     */
    void set_stereo(bool stereo);

    /**
     * @brief Set the sample rate.
     */
    void set_sample_rate(int rate);
    int get_sample_rate() const { return d_sample_rate; }

    /**
     * @brief Set the recording mode.
     */
    void set_mode(RecordingMode mode) { d_mode = mode; }
    RecordingMode get_mode() const { return d_mode; }

    /**
     * @brief Set auto split interval in minutes (0 = no splitting).
     */
    void set_split_minutes(int minutes) { d_split_minutes = minutes; }

    /**
     * @brief Set squelch recording configuration.
     */
    void set_squelch_config(const SquelchRecordingConfig& config);

    /**
     * @brief Set metadata for recordings.
     */
    void set_frequency(double freq) { d_frequency = freq; }

    // =========================================================================
    // Recording Control
    // =========================================================================

    /**
     * @brief Start recording (or arm for squelch mode).
     * @param base_filepath Base path without extension (for per-call mode, will add _001, _002, etc.)
     * @return True if recording/arming started successfully
     */
    bool start_recording(const QString& base_filepath);

    /**
     * @brief Stop recording and close any open files.
     */
    void stop_recording();

    /**
     * @brief Check if recording is active or armed.
     */
    bool is_recording() const { return d_armed; }

    /**
     * @brief Get recording statistics.
     */
    uint64_t get_samples_recorded() const { return d_samples_recorded; }
    double get_duration() const;
    int get_call_count() const { return d_call_count; }

    // =========================================================================
    // Squelch Interface
    // =========================================================================

    /**
     * @brief Update squelch state (call from squelch detector).
     * @param open True if squelch is open (signal present)
     */
    void set_squelch_open(bool open);

    /**
     * @brief Get current recording state.
     */
    RecordingState get_state() const { return d_state; }

    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Set callback for new file started.
     */
    void set_on_file_started(std::function<void(const QString&)> callback) {
        d_on_file_started = callback;
    }

    /**
     * @brief Set callback for file completed.
     */
    void set_on_file_completed(std::function<void(const QString&, double)> callback) {
        d_on_file_completed = callback;
    }

    // =========================================================================
    // GNU Radio Interface
    // =========================================================================

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star& output_items) override;

private:
    audio_recorder(int sample_rate);

    // File operations
    bool open_new_file();
    void close_current_file();
    void write_wav_header();
    void finalize_wav_header();
    QString get_extension() const;

    // Pre-buffer management
    void add_to_prebuffer(const float* data, int count);
    void flush_prebuffer_to_file();

    // State machine
    void handle_constant_mode(const float* in, int count);
    void handle_squelch_per_call(const float* in, int count);
    void handle_squelch_chunks(const float* in, int count);
    void transition_to_recording();
    void transition_to_post_buffer();
    void transition_to_idle();
    void rotate_file();

    // Configuration
    AudioFileFormat d_format;
    WavSampleFormat d_wav_format;
    bool d_stereo;
    int d_sample_rate;
    RecordingMode d_mode;
    int d_split_minutes;
    SquelchRecordingConfig d_squelch_config;

    // Metadata
    double d_frequency;
    QString d_demod_mode;
    QString d_tuner_name;

    // Recording state
    bool d_armed;
    RecordingState d_state;
    bool d_squelch_open;
    QString d_base_filepath;
    QString d_current_filepath;
    QDateTime d_file_start_time;
    QDateTime d_recording_start_time;

    // Statistics
    uint64_t d_samples_recorded;
    uint64_t d_file_size;
    uint64_t d_samples_in_current_file;
    int d_call_count;

    // Pre-buffer (circular buffer)
    std::deque<float> d_prebuffer;
    size_t d_prebuffer_size;  // in samples

    // Post-buffer countdown
    int d_post_buffer_samples_remaining;

    // Chunk mode
    QDateTime d_chunk_start_time;
    bool d_chunk_has_audio;
    int d_chunk_duration_samples;

    // File output
    std::ofstream d_file;
    mutable std::mutex d_mutex;

    // Callbacks
    std::function<void(const QString&)> d_on_file_started;
    std::function<void(const QString&, double)> d_on_file_completed;
};

#endif // AUDIO_RECORDER_H
