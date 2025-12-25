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
#ifndef ANALOG_NBRX_BACKEND_H
#define ANALOG_NBRX_BACKEND_H

#include "interfaces/i_receiver_backend.h"
#include "receivers/nbrx.h"

/**
 * @brief Analog narrowband receiver backend.
 *
 * Wraps the existing nbrx class (AM, NFM, SSB, CW) to implement IReceiverBackend.
 * This allows the existing receiver implementation to be used with the new
 * pluggable backend architecture.
 */
class AnalogNbrxBackend : public IReceiverBackend {
public:
    /**
     * @brief Construct a narrowband analog backend.
     * @param quad_rate Input sample rate (after DDC)
     * @param audio_rate Output audio sample rate
     * @param mode Initial demodulation mode
     */
    AnalogNbrxBackend(float quad_rate, float audio_rate, ReceiverType mode = ReceiverType::ANALOG_NFM);
    virtual ~AnalogNbrxBackend();

    // ─── IReceiverBackend Implementation ───

    void connect(gr::top_block_sptr tb, gr::basic_block_sptr source) override;
    void disconnect() override;
    gr::basic_block_sptr get_input() override;

    // IQ output for recording
    gr::basic_block_sptr get_filtered_iq_output() override;
    int get_filtered_iq_port() const override { return nbrx::IQ_TAP_PORT; }
    double get_filtered_iq_rate() const override;

    int audio_output_count() const override { return 1; }
    gr::basic_block_sptr audio_output(int index) override;

    ReceiverType type() const override { return m_mode; }
    QString type_name() const override;

    // ─── Analog-Specific Methods ───

    /**
     * @brief Set the demodulation mode.
     * @param mode New mode (must be analog type)
     */
    void set_mode(ReceiverType mode);

    /**
     * @brief Set filter bandwidth.
     * @param low Low cutoff in Hz (relative to carrier)
     * @param high High cutoff in Hz (relative to carrier)
     * @param transition Transition width in Hz
     */
    void set_filter(double low, double high, double transition = 500.0) override;

    /**
     * @brief Set CW offset.
     * @param offset Offset in Hz
     */
    void set_cw_offset(double offset);

    /**
     * @brief Get signal level.
     * @return Signal level in dBFS
     */
    float get_signal_level() const override;

    // ─── IReceiverBackend Overrides ───

    // Noise blanker
    bool has_nb() const override { return true; }
    void set_nb_on(int nbid, bool on) override;
    void set_nb_threshold(int nbid, float threshold) override;

    // Squelch
    bool has_sql() const override { return true; }
    void set_sql_level(double level_db) override;
    void set_sql_alpha(double alpha) override;

    // AGC
    bool has_agc() const override { return true; }
    void set_agc_on(bool enabled) override;
    bool get_agc_on() const override;
    void set_agc_hang(bool use_hang) override;
    void set_agc_threshold(int threshold) override;
    void set_agc_slope(int slope) override;
    void set_agc_decay(int decay_ms) override;
    void set_agc_manual_gain(int gain) override;

    // FM
    void set_fm_maxdev(float hz) override;
    void set_fm_deemph(double tau) override;

    // AM
    void set_am_dcr(bool enabled) override;

    // AM-Sync
    void set_amsync_dcr(bool enabled) override;
    void set_amsync_pll_bw(float pll_bw) override;

    // Sample rate update
    void set_quad_rate(double rate) override;

private:
    nbrx_sptr m_nbrx;
    gr::top_block_sptr m_tb;
    gr::basic_block_sptr m_source;
    ReceiverType m_mode;
    float m_quad_rate;
    float m_audio_rate;
    bool m_connected;
    bool m_agc_on;  // Track AGC state
    bool m_demod_pending;  // Demod needs to be set after connect

    /**
     * @brief Convert ReceiverType to nbrx demod enum.
     */
    static nbrx::nbrx_demod receiver_type_to_nbrx_demod(ReceiverType type);

    /**
     * @brief Convert nbrx demod to ReceiverType.
     */
    static ReceiverType nbrx_demod_to_receiver_type(nbrx::nbrx_demod demod);
};

#endif // ANALOG_NBRX_BACKEND_H
