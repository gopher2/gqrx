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
#include "analog_nbrx_backend.h"

AnalogNbrxBackend::AnalogNbrxBackend(float quad_rate, float audio_rate, ReceiverType mode)
    : m_mode(mode)
    , m_quad_rate(quad_rate)
    , m_audio_rate(audio_rate)
    , m_connected(false)
    , m_agc_on(true)
    , m_demod_pending(false)
{
    m_nbrx = make_nbrx(quad_rate, audio_rate);
    m_nbrx->set_demod(receiver_type_to_nbrx_demod(mode));
}

AnalogNbrxBackend::~AnalogNbrxBackend()
{
    if (m_connected) {
        disconnect();
    }
}

void AnalogNbrxBackend::connect(gr::top_block_sptr tb, gr::basic_block_sptr source)
{
    if (m_connected) {
        disconnect();
    }

    m_tb = tb;
    m_source = source;

    tb->connect(source, 0, m_nbrx, 0);
    m_connected = true;

    if (m_demod_pending) {
        m_nbrx->set_demod(receiver_type_to_nbrx_demod(m_mode));
        m_demod_pending = false;
    }

    m_nbrx->start();
}

void AnalogNbrxBackend::disconnect()
{
    if (!m_connected || !m_tb || !m_source) {
        return;
    }

    m_nbrx->stop();

    try {
        m_tb->disconnect(m_source, 0, m_nbrx, 0);
    } catch (std::exception&) {
    }

    m_connected = false;
    m_source.reset();
    m_tb.reset();
}

gr::basic_block_sptr AnalogNbrxBackend::get_input()
{
    return m_nbrx;
}

gr::basic_block_sptr AnalogNbrxBackend::audio_output(int index)
{
    if (index != 0) {
        return nullptr;
    }
    // nbrx has two audio outputs (0 = left, 1 = right) from its hier_block2
    // Return the nbrx itself - caller connects to output ports 0 and 1
    return m_nbrx;
}

gr::basic_block_sptr AnalogNbrxBackend::get_filtered_iq_output()
{
    if (m_nbrx) {
        return m_nbrx->get_filter_output();
    }
    return nullptr;
}

double AnalogNbrxBackend::get_filtered_iq_rate() const
{
    return nbrx::get_filter_rate();  // 96000 Hz
}

QString AnalogNbrxBackend::type_name() const
{
    switch (m_mode) {
        case ReceiverType::ANALOG_OFF:    return "Off";
        case ReceiverType::ANALOG_RAW:    return "Raw I/Q";
        case ReceiverType::ANALOG_AM:     return "AM";
        case ReceiverType::ANALOG_NFM:    return "Narrow FM";
        case ReceiverType::ANALOG_SSB:    return "SSB";
        case ReceiverType::ANALOG_USB:    return "USB";
        case ReceiverType::ANALOG_LSB:    return "LSB";
        case ReceiverType::ANALOG_CW_L:   return "CW-L";
        case ReceiverType::ANALOG_CW_U:   return "CW-U";
        case ReceiverType::ANALOG_AMSYNC: return "AM-Sync";
        default:                          return "Unknown";
    }
}

void AnalogNbrxBackend::set_mode(ReceiverType mode)
{
    m_mode = mode;
    m_nbrx->set_demod(receiver_type_to_nbrx_demod(mode));
}

void AnalogNbrxBackend::set_filter(double low, double high, double transition)
{
    m_nbrx->set_filter(low, high, transition);
}

void AnalogNbrxBackend::set_cw_offset(double offset)
{
    m_nbrx->set_cw_offset(offset);
}

float AnalogNbrxBackend::get_signal_level() const
{
    if (m_nbrx) {
        return m_nbrx->get_signal_level();
    }
    return -100.0f;
}

// Noise blanker
void AnalogNbrxBackend::set_nb_on(int nbid, bool on)
{
    m_nbrx->set_nb_on(nbid, on);
}

void AnalogNbrxBackend::set_nb_threshold(int nbid, float threshold)
{
    m_nbrx->set_nb_threshold(nbid, threshold);
}

// Squelch
void AnalogNbrxBackend::set_sql_level(double level_db)
{
    m_nbrx->set_sql_level(level_db);
}

void AnalogNbrxBackend::set_sql_alpha(double alpha)
{
    m_nbrx->set_sql_alpha(alpha);
}

// AGC
void AnalogNbrxBackend::set_agc_on(bool enabled)
{
    m_agc_on = enabled;
    m_nbrx->set_agc_on(enabled);
}

bool AnalogNbrxBackend::get_agc_on() const
{
    return m_agc_on;
}

void AnalogNbrxBackend::set_agc_hang(bool use_hang)
{
    m_nbrx->set_agc_hang(use_hang);
}

void AnalogNbrxBackend::set_agc_threshold(int threshold)
{
    m_nbrx->set_agc_threshold(threshold);
}

void AnalogNbrxBackend::set_agc_slope(int slope)
{
    m_nbrx->set_agc_slope(slope);
}

void AnalogNbrxBackend::set_agc_decay(int decay_ms)
{
    m_nbrx->set_agc_decay(decay_ms);
}

void AnalogNbrxBackend::set_agc_manual_gain(int gain)
{
    m_nbrx->set_agc_manual_gain(gain);
}

// FM
void AnalogNbrxBackend::set_fm_maxdev(float hz)
{
    m_nbrx->set_fm_maxdev(hz);
}

void AnalogNbrxBackend::set_fm_deemph(double tau)
{
    m_nbrx->set_fm_deemph(tau);
}

// AM
void AnalogNbrxBackend::set_am_dcr(bool enabled)
{
    m_nbrx->set_am_dcr(enabled);
}

// AM-Sync
void AnalogNbrxBackend::set_amsync_dcr(bool enabled)
{
    m_nbrx->set_amsync_dcr(enabled);
}

void AnalogNbrxBackend::set_amsync_pll_bw(float pll_bw)
{
    m_nbrx->set_amsync_pll_bw(pll_bw);
}

void AnalogNbrxBackend::set_quad_rate(double rate)
{
    m_quad_rate = rate;

    // Recreate nbrx block with new rate (caller must have disconnected first)
    m_connected = false;
    m_source.reset();
    m_nbrx.reset();
    m_nbrx = make_nbrx(m_quad_rate, m_audio_rate);

    m_demod_pending = true;
}

// Static conversion functions
nbrx::nbrx_demod AnalogNbrxBackend::receiver_type_to_nbrx_demod(ReceiverType type)
{
    switch (type) {
        case ReceiverType::ANALOG_RAW:
        case ReceiverType::ANALOG_OFF:
            return nbrx::NBRX_DEMOD_NONE;
        case ReceiverType::ANALOG_AM:
            return nbrx::NBRX_DEMOD_AM;
        case ReceiverType::ANALOG_NFM:
            return nbrx::NBRX_DEMOD_FM;
        case ReceiverType::ANALOG_SSB:
        case ReceiverType::ANALOG_USB:
        case ReceiverType::ANALOG_LSB:
        case ReceiverType::ANALOG_CW_L:
        case ReceiverType::ANALOG_CW_U:
            return nbrx::NBRX_DEMOD_SSB;
        case ReceiverType::ANALOG_AMSYNC:
            return nbrx::NBRX_DEMOD_AMSYNC;
        default:
            return nbrx::NBRX_DEMOD_FM;
    }
}

ReceiverType AnalogNbrxBackend::nbrx_demod_to_receiver_type(nbrx::nbrx_demod demod)
{
    switch (demod) {
        case nbrx::NBRX_DEMOD_NONE:
            return ReceiverType::ANALOG_RAW;
        case nbrx::NBRX_DEMOD_AM:
            return ReceiverType::ANALOG_AM;
        case nbrx::NBRX_DEMOD_FM:
            return ReceiverType::ANALOG_NFM;
        case nbrx::NBRX_DEMOD_SSB:
            return ReceiverType::ANALOG_SSB;
        case nbrx::NBRX_DEMOD_AMSYNC:
            return ReceiverType::ANALOG_AMSYNC;
        default:
            return ReceiverType::ANALOG_NFM;
    }
}
