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
#ifndef I_RECEIVER_BACKEND_H
#define I_RECEIVER_BACKEND_H

#include <gnuradio/top_block.h>
#include <gnuradio/basic_block.h>
#include <QString>
#include <memory>

/**
 * @brief Receiver type enumeration.
 *
 * Defines all supported analog receiver modes.
 */
enum class ReceiverType {
    // Analog modes
    ANALOG_OFF = 0,
    ANALOG_RAW,          // Raw I/Q to audio
    ANALOG_AM,
    ANALOG_NFM,
    ANALOG_WFM_MONO,
    ANALOG_WFM_STEREO,
    ANALOG_WFM_STEREO_OIRT,
    ANALOG_SSB,
    ANALOG_USB,
    ANALOG_LSB,
    ANALOG_CW_L,
    ANALOG_CW_U,
    ANALOG_AMSYNC,
};

/**
 * @brief Interface for pluggable receiver backends.
 *
 * A receiver backend handles mode-specific DSP (demodulation, decoding).
 * Examples: NFM demodulator, DMR decoder, P25 vocoder.
 *
 * Backends are created by ReceiverBackendFactory and owned by ReceiverChannel.
 */
class IReceiverBackend {
public:
    virtual ~IReceiverBackend() = default;

    // ─── GNU Radio Integration ───

    /**
     * @brief Connect this backend to the flowgraph.
     * @param tb The GNU Radio top block
     * @param source The IQ source to connect from (typically after DDC/filter)
     */
    virtual void connect(gr::top_block_sptr tb, gr::basic_block_sptr source) = 0;

    /**
     * @brief Disconnect this backend from the flowgraph.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Get the input block where IQ samples should be fed.
     * @return The GNU Radio block to connect IQ source to
     */
    virtual gr::basic_block_sptr get_input() = 0;

    // ─── IQ Output for Recording ───

    /**
     * @brief Get the filtered IQ output block for recording.
     * @return The GNU Radio block providing filtered IQ output at ~96 kHz,
     *         or nullptr if not available.
     *
     * This taps the signal after the filter but before demodulation,
     * providing narrowband IQ suitable for recording.
     *
     * @note Connect to the port returned by get_filtered_iq_port(), not port 0.
     */
    virtual gr::basic_block_sptr get_filtered_iq_output() { return nullptr; }

    /**
     * @brief Get the output port index for the filtered IQ tap.
     * @return Port index (typically 2 for hierarchical blocks with audio on 0,1)
     */
    virtual int get_filtered_iq_port() const { return 0; }

    /**
     * @brief Get the sample rate of the filtered IQ output.
     * @return Sample rate in Hz (typically 96000)
     */
    virtual double get_filtered_iq_rate() const { return 0.0; }

    // ─── Audio Outputs ───

    /**
     * @brief Get the number of audio outputs.
     * @return 1 for analog/P25, 2 for DMR (TS1, TS2)
     */
    virtual int audio_output_count() const = 0;

    /**
     * @brief Get an audio output block.
     * @param index Output index (0 for mono/left, 1 for right/TS2)
     * @return The GNU Radio block providing audio output
     */
    virtual gr::basic_block_sptr audio_output(int index) = 0;

    // ─── Type Information ───

    /**
     * @brief Get the receiver type.
     */
    virtual ReceiverType type() const = 0;

    /**
     * @brief Get the human-readable type name.
     */
    virtual QString type_name() const = 0;

    // ─── Mode-Specific Parameters ───

    // Filter
    virtual void set_filter(double low, double high, double transition = 500.0) {}

    // Analog FM
    virtual void set_fm_maxdev(float hz) {}
    virtual void set_fm_deemph(double tau) {}

    // Analog AM
    virtual void set_am_dcr(bool enabled) {}

    // AM Sync
    virtual void set_amsync_dcr(bool enabled) {}
    virtual void set_amsync_pll_bw(float pll_bw) {}

    // AGC (analog modes)
    virtual bool has_agc() const { return false; }
    virtual void set_agc_on(bool enabled) {}
    virtual bool get_agc_on() const { return false; }
    virtual void set_agc_hang(bool use_hang) {}
    virtual void set_agc_threshold(int threshold) {}
    virtual void set_agc_slope(int slope) {}
    virtual void set_agc_decay(int decay_ms) {}
    virtual void set_agc_manual_gain(int gain) {}

    // Noise blanker
    virtual bool has_nb() const { return false; }
    virtual void set_nb_on(int nbid, bool on) {}
    virtual void set_nb_threshold(int nbid, float threshold) {}

    // Squelch
    virtual bool has_sql() const { return false; }
    virtual void set_sql_level(double level_db) {}
    virtual void set_sql_alpha(double alpha) {}

    // Signal level (for squelch auto)
    virtual float get_signal_level() const { return -100.0f; }

    // Sample rate update (called when SDR rate changes)
    virtual void set_quad_rate(double rate) {}
};

using IReceiverBackend_ptr = std::unique_ptr<IReceiverBackend>;

#endif // I_RECEIVER_BACKEND_H
