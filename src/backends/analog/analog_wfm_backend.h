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
#ifndef ANALOG_WFM_BACKEND_H
#define ANALOG_WFM_BACKEND_H

#include "interfaces/i_receiver_backend.h"
#include "receivers/wfmrx.h"

/**
 * @brief Analog wideband FM receiver backend.
 *
 * Wraps the existing wfmrx class (mono, stereo, OIRT) to implement IReceiverBackend.
 * This allows the existing receiver implementation to be used with the new
 * pluggable backend architecture.
 */
class AnalogWfmBackend : public IReceiverBackend {
public:
    /**
     * @brief Construct a wideband FM analog backend.
     * @param quad_rate Input sample rate (after DDC)
     * @param audio_rate Output audio sample rate
     * @param mode Initial demodulation mode (MONO, STEREO, or OIRT)
     */
    AnalogWfmBackend(float quad_rate, float audio_rate, ReceiverType mode = ReceiverType::ANALOG_WFM_MONO);
    virtual ~AnalogWfmBackend();

    // ─── IReceiverBackend Implementation ───

    void connect(gr::top_block_sptr tb, gr::basic_block_sptr source) override;
    void disconnect() override;
    gr::basic_block_sptr get_input() override;

    // IQ output for recording
    gr::basic_block_sptr get_filtered_iq_output() override;
    int get_filtered_iq_port() const override { return wfmrx::IQ_TAP_PORT; }
    double get_filtered_iq_rate() const override;

    int audio_output_count() const override { return 1; }
    gr::basic_block_sptr audio_output(int index) override;

    ReceiverType type() const override { return m_mode; }
    QString type_name() const override;

    // ─── WFM-Specific Methods ───

    /**
     * @brief Set the demodulation mode.
     * @param mode New mode (must be WFM type)
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
     * @brief Get signal level.
     * @return Signal level in dBFS
     */
    float get_signal_level() const override;

    // ─── IReceiverBackend Overrides ───

    // Noise blanker - not available for WFM
    bool has_nb() const override { return false; }

    // Squelch
    bool has_sql() const override { return true; }
    void set_sql_level(double level_db) override;
    void set_sql_alpha(double alpha) override;

    // AGC - not available for WFM
    bool has_agc() const override { return false; }

    // FM
    void set_fm_maxdev(float hz) override;
    void set_fm_deemph(double tau) override;

    // Sample rate update
    void set_quad_rate(double rate) override;

    // ─── RDS Functions ───

    /**
     * @brief Get RDS data from the decoder.
     * @param outbuff Output buffer for RDS data
     * @param num Number of bytes read
     */
    void get_rds_data(std::string &outbuff, int &num);

    /**
     * @brief Start the RDS decoder.
     */
    void start_rds_decoder();

    /**
     * @brief Stop the RDS decoder.
     */
    void stop_rds_decoder();

    /**
     * @brief Reset the RDS parser.
     */
    void reset_rds_parser();

    /**
     * @brief Check if RDS decoder is active.
     * @return true if RDS decoder is running
     */
    bool is_rds_decoder_active();

private:
    wfmrx_sptr m_wfmrx;
    gr::top_block_sptr m_tb;
    gr::basic_block_sptr m_source;
    ReceiverType m_mode;
    float m_quad_rate;
    bool m_connected;

    /**
     * @brief Convert ReceiverType to wfmrx demod enum.
     */
    static wfmrx::wfmrx_demod receiver_type_to_wfmrx_demod(ReceiverType type);
};

#endif // ANALOG_WFM_BACKEND_H
