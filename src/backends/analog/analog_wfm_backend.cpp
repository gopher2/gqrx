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
#include "analog_wfm_backend.h"

AnalogWfmBackend::AnalogWfmBackend(float quad_rate, float audio_rate, ReceiverType mode)
    : m_mode(mode)
    , m_quad_rate(quad_rate)
    , m_connected(false)
{
    m_wfmrx = make_wfmrx(quad_rate, audio_rate);
    m_wfmrx->set_demod(static_cast<int>(receiver_type_to_wfmrx_demod(mode)));
}

AnalogWfmBackend::~AnalogWfmBackend()
{
    if (m_connected) {
        disconnect();
    }
}

void AnalogWfmBackend::connect(gr::top_block_sptr tb, gr::basic_block_sptr source)
{
    if (m_connected) {
        disconnect();
    }

    m_tb = tb;
    m_source = source;

    tb->connect(source, 0, m_wfmrx, 0);
    m_connected = true;
    m_wfmrx->start();
}

void AnalogWfmBackend::disconnect()
{
    if (!m_connected || !m_tb || !m_source) {
        return;
    }

    m_wfmrx->stop();

    try {
        m_tb->disconnect(m_source, 0, m_wfmrx, 0);
    } catch (std::exception&) {
    }

    m_connected = false;
    m_source.reset();
}

gr::basic_block_sptr AnalogWfmBackend::get_input()
{
    return m_wfmrx;
}

gr::basic_block_sptr AnalogWfmBackend::audio_output(int index)
{
    if (index != 0) {
        return nullptr;
    }
    return m_wfmrx;
}

gr::basic_block_sptr AnalogWfmBackend::get_filtered_iq_output()
{
    if (m_wfmrx) {
        return m_wfmrx->get_filter_output();
    }
    return nullptr;
}

double AnalogWfmBackend::get_filtered_iq_rate() const
{
    return wfmrx::get_filter_rate();
}

QString AnalogWfmBackend::type_name() const
{
    switch (m_mode) {
        case ReceiverType::ANALOG_WFM_MONO:        return "Wide FM (Mono)";
        case ReceiverType::ANALOG_WFM_STEREO:      return "Wide FM (Stereo)";
        case ReceiverType::ANALOG_WFM_STEREO_OIRT: return "Wide FM (OIRT)";
        default:                                   return "Wide FM";
    }
}

void AnalogWfmBackend::set_mode(ReceiverType mode)
{
    m_mode = mode;
    m_wfmrx->set_demod(static_cast<int>(receiver_type_to_wfmrx_demod(mode)));
}

void AnalogWfmBackend::set_filter(double low, double high, double transition)
{
    m_wfmrx->set_filter(low, high, transition);
}

float AnalogWfmBackend::get_signal_level() const
{
    if (m_wfmrx) {
        return m_wfmrx->get_signal_level();
    }
    return -100.0f;
}

void AnalogWfmBackend::set_sql_level(double level_db)
{
    m_wfmrx->set_sql_level(level_db);
}

void AnalogWfmBackend::set_sql_alpha(double alpha)
{
    m_wfmrx->set_sql_alpha(alpha);
}

void AnalogWfmBackend::set_fm_maxdev(float hz)
{
    m_wfmrx->set_fm_maxdev(hz);
}

void AnalogWfmBackend::set_fm_deemph(double tau)
{
    m_wfmrx->set_fm_deemph(tau);
}

void AnalogWfmBackend::set_quad_rate(double rate)
{
    m_quad_rate = rate;
    if (m_wfmrx) {
        m_wfmrx->set_quad_rate(rate);
    }
}

void AnalogWfmBackend::get_rds_data(std::string &outbuff, int &num)
{
    m_wfmrx->get_rds_data(outbuff, num);
}

void AnalogWfmBackend::start_rds_decoder()
{
    m_wfmrx->start_rds_decoder();
}

void AnalogWfmBackend::stop_rds_decoder()
{
    m_wfmrx->stop_rds_decoder();
}

void AnalogWfmBackend::reset_rds_parser()
{
    m_wfmrx->reset_rds_parser();
}

bool AnalogWfmBackend::is_rds_decoder_active()
{
    return m_wfmrx->is_rds_decoder_active();
}

wfmrx::wfmrx_demod AnalogWfmBackend::receiver_type_to_wfmrx_demod(ReceiverType type)
{
    switch (type) {
        case ReceiverType::ANALOG_WFM_MONO:
            return wfmrx::WFMRX_DEMOD_MONO;
        case ReceiverType::ANALOG_WFM_STEREO:
            return wfmrx::WFMRX_DEMOD_STEREO;
        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
            return wfmrx::WFMRX_DEMOD_STEREO_UKW;
        default:
            return wfmrx::WFMRX_DEMOD_MONO;
    }
}
