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

#include "receiver_channel.h"
#include "backends/receiver_backend_factory.h"
#include "backends/analog/analog_nbrx_backend.h"
#include "backends/analog/analog_wfm_backend.h"
#include <gnuradio/blocks/null_sink.h>
#include <gnuradio/blocks/null_source.h>
#include <gnuradio/blocks/multiply_const.h>
#include <algorithm>

// Target quadrature rate for narrowband (same as receiver.cpp)
// nbrx will resample from this to its internal 96kHz rate
#define TARGET_QUAD_RATE 1e6

ReceiverChannel::ReceiverChannel(int channel_id,
                                double input_rate,
                                const std::string& audio_device,
                                gr::top_block_sptr parent_tb,
                                bool use_internal_audio_sink)
    : d_channel_id(channel_id)
    , d_channel_name("Channel " + std::to_string(channel_id))
    , d_audio_device(audio_device)
    , d_channel_type(ChannelType::MANUAL)
    , d_connected(false)
    , d_enabled(true)
    , d_bypassed(false)
    , d_recording_wav(false)
    , d_sniffer_active(false)
    , d_use_internal_audio_sink(use_internal_audio_sink)
    , d_input_rate(input_rate)
    , d_quad_rate(192000.0) // Default quad rate
    , d_audio_rate(48000.0) // Default audio rate
    , d_center_freq(0.0) // Tuner offset from RF center (0 = tuned to center)
    , d_filter_offset(0.0)
    , d_cw_offset(0.0)
    , d_sql_level(-150.0)
    , d_audio_playing(false)
    , d_demod(RX_DEMOD_NFM)
    , d_parent_tb(parent_tb)
    , d_iq_recording_active(false)
    , d_new_audio_recording_active(false)
    , d_iq_tap_point(IqTapPoint::AFTER_FILTER)  // Default to narrowband (96 kHz)
    , d_iq_recording_source_port(0)
    , d_audio_muted(false)
{
    create_chain();
}

ReceiverChannel::~ReceiverChannel()
{
    disconnect();
}

void ReceiverChannel::create_chain()
{
    try {
        // Decimate to ~1MHz, let nbrx resample to 96kHz
        unsigned int decim = std::max(1u, (unsigned int)(d_input_rate / TARGET_QUAD_RATE));
        d_quad_rate = d_input_rate / decim;

        d_ddc = make_downconverter_cc(decim, 0.0, d_input_rate);

        d_audio_gain0 = gr::blocks::multiply_const_ff::make(0.1f);
        d_audio_gain1 = gr::blocks::multiply_const_ff::make(0.1f);

        d_wav_gain0 = gr::blocks::multiply_const_ff::make(1.0f);
        d_wav_gain1 = gr::blocks::multiply_const_ff::make(1.0f);

        d_audio_fft = make_rx_fft_f(1024, d_audio_rate);

        if (d_use_internal_audio_sink) {
            std::string audio_dev = d_audio_device.empty() ? "" : d_audio_device;

#ifdef WITH_PULSEAUDIO
            d_audio_snk = make_pa_sink(audio_dev, d_audio_rate, "GQRX Channel " + std::to_string(d_channel_id), "Audio");
#elif WITH_PORTAUDIO
            d_audio_snk = make_portaudio_sink(audio_dev, d_audio_rate, "GQRX", "Channel " + std::to_string(d_channel_id));
#else
            d_audio_snk = gr::audio::sink::make(d_audio_rate, audio_dev);
#endif
        }

        d_audio_udp_sink = make_udp_sink_f();
        d_sniffer = make_sniffer_f();

        d_audio_null_sink0 = gr::blocks::null_sink::make(sizeof(float));
        d_audio_null_sink1 = gr::blocks::null_sink::make(sizeof(float));

        d_backend_audio_null_sink0 = gr::blocks::null_sink::make(sizeof(float));
        d_backend_audio_null_sink1 = gr::blocks::null_sink::make(sizeof(float));

        d_audio_null_src0 = gr::blocks::null_source::make(sizeof(float));
        d_audio_null_src1 = gr::blocks::null_source::make(sizeof(float));

        d_ddc_null_sink = gr::blocks::null_sink::make(sizeof(gr_complex));
        d_ddc_null_source = gr::blocks::null_source::make(sizeof(gr_complex));

    } catch (std::exception&) {
        throw;
    }
}

void ReceiverChannel::connect_to_source(gr::basic_block_sptr source, int source_port)
{
    if (!d_parent_tb) {
        return;
    }

    if (d_connected) {
        disconnect();
    }

    // Lock flowgraph if running (try-catch pattern)
    bool was_locked = false;
    try {
        d_parent_tb->lock();
        was_locked = true;
    } catch (...) {
        was_locked = false;
    }

    try {
        d_parent_tb->connect(source, source_port, d_ddc, 0);
        d_parent_tb->connect(d_ddc, 0, d_ddc_null_sink, 0);

        // Placeholder connections until backend is set
        d_parent_tb->connect(d_audio_null_src0, 0, d_audio_gain0, 0);
        d_parent_tb->connect(d_audio_null_src1, 0, d_audio_gain1, 0);

        d_parent_tb->connect(d_audio_gain0, 0, d_audio_null_sink0, 0);
        d_parent_tb->connect(d_audio_gain1, 0, d_audio_null_sink1, 0);

        d_parent_tb->connect(d_audio_gain0, 0, d_audio_fft, 0);
        d_parent_tb->connect(d_audio_gain0, 0, d_sniffer, 0);

        if (was_locked) {
            d_parent_tb->unlock();
        }
        d_connected = true;
        d_source = source;
        d_source_port = source_port;

    } catch (std::exception&) {
        if (was_locked) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
        throw;
    }
}

void ReceiverChannel::disconnect()
{
    if (!d_connected || !d_parent_tb) {
        return;
    }

    bool was_locked = false;
    try {
        d_parent_tb->lock();
        was_locked = true;
    } catch (...) {
        was_locked = false;
    }

    try {
        disconnect_impl();

        if (was_locked) {
            d_parent_tb->unlock();
        }
    } catch (std::exception& e) {
        if (was_locked) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
    }
}

void ReceiverChannel::disconnect_locked()
{
    if (!d_connected || !d_parent_tb) {
        return;
    }

    try {
        disconnect_impl();
    } catch (std::exception&) {
        // May already be disconnected
    }
}

void ReceiverChannel::disconnect_impl()
{
    if (d_ddc) {
        if ((d_bypassed || !d_enabled) && d_ddc_null_source) {
            try { d_parent_tb->disconnect(d_ddc_null_source, 0, d_ddc, 0); } catch (...) {}
        } else if (d_source) {
            try { d_parent_tb->disconnect(d_source, d_source_port, d_ddc, 0); } catch (...) {}
        }
    }

    if (d_ddc && d_ddc_null_sink) {
        try { d_parent_tb->disconnect(d_ddc, 0, d_ddc_null_sink, 0); } catch (...) {}
    }

    if (d_backend) {
        auto audio_out = d_backend->audio_output(0);
        if (audio_out) {
            if (d_audio_gain0 && d_audio_gain1) {
                try {
                    d_parent_tb->disconnect(audio_out, 0, d_audio_gain0, 0);
                    d_parent_tb->disconnect(audio_out, 1, d_audio_gain1, 0);
                } catch (...) {}
            }
            if (d_backend_audio_null_sink0 && d_backend_audio_null_sink1) {
                try {
                    d_parent_tb->disconnect(audio_out, 0, d_backend_audio_null_sink0, 0);
                    d_parent_tb->disconnect(audio_out, 1, d_backend_audio_null_sink1, 0);
                } catch (...) {}
            }
        }
        d_backend->disconnect();
    }

    if (d_audio_null_src0 && d_audio_gain0) {
        try { d_parent_tb->disconnect(d_audio_null_src0, 0, d_audio_gain0, 0); } catch (...) {}
        try { d_parent_tb->disconnect(d_audio_null_src1, 0, d_audio_gain1, 0); } catch (...) {}
    }

    if (d_audio_gain0 && d_audio_null_sink0) {
        try {
            d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_null_sink0, 0);
            d_parent_tb->disconnect(d_audio_gain1, 0, d_audio_null_sink1, 0);
        } catch (...) {}
    }

    if (d_audio_gain0 && d_audio_snk && d_audio_playing && d_use_internal_audio_sink) {
        try {
            d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_snk, 0);
            d_parent_tb->disconnect(d_audio_gain1, 0, d_audio_snk, 1);
        } catch (...) {}
    }

    if (d_audio_gain0 && d_audio_fft) {
        try { d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_fft, 0); } catch (...) {}
    }
    if (d_audio_gain0 && d_sniffer) {
        try { d_parent_tb->disconnect(d_audio_gain0, 0, d_sniffer, 0); } catch (...) {}
    }

    d_connected = false;
    d_audio_playing = false;
    d_source.reset();
    d_source_port = 0;
}

ReceiverChannel::status ReceiverChannel::set_center_freq(double offset_hz)
{
    d_center_freq = offset_hz;

    if (d_ddc) {
        d_ddc->set_center_freq(d_center_freq);
    }

    return STATUS_OK;
}

ReceiverChannel::status ReceiverChannel::set_demod(rx_demod demod)
{
    d_demod = demod;

    if (demod == RX_DEMOD_OFF) {
        if (d_parent_tb && d_backend && d_connected && !d_audio_muted) {
            d_parent_tb->lock();
            try {
                auto audio_out = d_backend->audio_output(0);
                if (audio_out && d_audio_gain0 && d_audio_gain1) {
                    try {
                        d_parent_tb->disconnect(audio_out, 0, d_audio_gain0, 0);
                        d_parent_tb->disconnect(audio_out, 1, d_audio_gain1, 0);
                    } catch (...) {}

                    // Route to null sinks (GNU Radio requires all ports connected)
                    if (d_backend_audio_null_sink0 && d_backend_audio_null_sink1) {
                        d_parent_tb->connect(audio_out, 0, d_backend_audio_null_sink0, 0);
                        d_parent_tb->connect(audio_out, 1, d_backend_audio_null_sink1, 0);
                    }
                }
                if (d_audio_null_src0 && d_audio_gain0) {
                    d_parent_tb->connect(d_audio_null_src0, 0, d_audio_gain0, 0);
                    d_parent_tb->connect(d_audio_null_src1, 0, d_audio_gain1, 0);
                }
                d_audio_muted = true;
            } catch (std::exception&) {
            }
            d_parent_tb->unlock();
        }
        return STATUS_OK;
    }

    // Switching from OFF - reconnect backend audio
    if (d_audio_muted && d_parent_tb && d_backend && d_connected) {
        d_parent_tb->lock();
        try {
            if (d_audio_null_src0 && d_audio_gain0) {
                try {
                    d_parent_tb->disconnect(d_audio_null_src0, 0, d_audio_gain0, 0);
                    d_parent_tb->disconnect(d_audio_null_src1, 0, d_audio_gain1, 0);
                } catch (...) {}
            }

            auto audio_out = d_backend->audio_output(0);
            if (audio_out && d_backend_audio_null_sink0 && d_backend_audio_null_sink1) {
                try {
                    d_parent_tb->disconnect(audio_out, 0, d_backend_audio_null_sink0, 0);
                    d_parent_tb->disconnect(audio_out, 1, d_backend_audio_null_sink1, 0);
                } catch (...) {}
            }

            if (audio_out && d_audio_gain0 && d_audio_gain1) {
                d_parent_tb->connect(audio_out, 0, d_audio_gain0, 0);
                d_parent_tb->connect(audio_out, 1, d_audio_gain1, 0);
            }
            d_audio_muted = false;
        } catch (std::exception&) {
        }
        d_parent_tb->unlock();
    }

    ReceiverType new_type = demod_to_receiver_type(demod);

    if (d_backend) {
        ReceiverType current_type = d_backend->type();
        bool current_is_wfm = (current_type == ReceiverType::ANALOG_WFM_MONO ||
                               current_type == ReceiverType::ANALOG_WFM_STEREO ||
                               current_type == ReceiverType::ANALOG_WFM_STEREO_OIRT);
        bool new_is_wfm = (new_type == ReceiverType::ANALOG_WFM_MONO ||
                           new_type == ReceiverType::ANALOG_WFM_STEREO ||
                           new_type == ReceiverType::ANALOG_WFM_STEREO_OIRT);

        // Swap backend types if crossing nbrx <-> wfmrx boundary
        if (current_is_wfm != new_is_wfm) {
            auto new_backend = ReceiverBackendFactory::create(new_type, d_quad_rate, d_audio_rate);
            if (new_backend) {
                set_backend(std::move(new_backend));
            }
        } else {
            if (current_is_wfm) {
                auto* wfm_backend = dynamic_cast<AnalogWfmBackend*>(d_backend.get());
                if (wfm_backend) {
                    wfm_backend->set_mode(new_type);
                }
            } else {
                auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
                if (nbrx_backend) {
                    nbrx_backend->set_mode(new_type);
                }
            }
        }
    } else {
        auto new_backend = ReceiverBackendFactory::create(new_type, d_quad_rate, d_audio_rate);
        if (new_backend) {
            set_backend(std::move(new_backend));
        }
    }

    return STATUS_OK;
}

ReceiverType ReceiverChannel::demod_to_receiver_type(rx_demod demod)
{
    switch (demod) {
        case RX_DEMOD_OFF:
            return ReceiverType::ANALOG_OFF;
        case RX_DEMOD_NONE:
            return ReceiverType::ANALOG_RAW;
        case RX_DEMOD_AM:
            return ReceiverType::ANALOG_AM;
        case RX_DEMOD_NFM:
            return ReceiverType::ANALOG_NFM;
        case RX_DEMOD_WFM_M:
            return ReceiverType::ANALOG_WFM_MONO;
        case RX_DEMOD_WFM_S:
            return ReceiverType::ANALOG_WFM_STEREO;
        case RX_DEMOD_WFM_S_OIRT:
            return ReceiverType::ANALOG_WFM_STEREO_OIRT;
        case RX_DEMOD_SSB:
            return ReceiverType::ANALOG_SSB;
        case RX_DEMOD_AMSYNC:
            return ReceiverType::ANALOG_AMSYNC;
        default:
            return ReceiverType::ANALOG_NFM;
    }
}

ReceiverChannel::rx_demod ReceiverChannel::receiver_type_to_demod(ReceiverType type)
{
    switch (type) {
        case ReceiverType::ANALOG_OFF:
            return RX_DEMOD_OFF;
        case ReceiverType::ANALOG_RAW:
            return RX_DEMOD_NONE;
        case ReceiverType::ANALOG_AM:
            return RX_DEMOD_AM;
        case ReceiverType::ANALOG_NFM:
            return RX_DEMOD_NFM;
        case ReceiverType::ANALOG_WFM_MONO:
            return RX_DEMOD_WFM_M;
        case ReceiverType::ANALOG_WFM_STEREO:
            return RX_DEMOD_WFM_S;
        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
            return RX_DEMOD_WFM_S_OIRT;
        case ReceiverType::ANALOG_SSB:
        case ReceiverType::ANALOG_USB:
        case ReceiverType::ANALOG_LSB:
        case ReceiverType::ANALOG_CW_L:
        case ReceiverType::ANALOG_CW_U:
            return RX_DEMOD_SSB;
        case ReceiverType::ANALOG_AMSYNC:
            return RX_DEMOD_AMSYNC;
        default:
            return RX_DEMOD_NFM;
    }
}

void ReceiverChannel::set_audio_gain(float gain)
{
    if (d_audio_gain0) {
        d_audio_gain0->set_k(gain);
    }
    if (d_audio_gain1) {
        d_audio_gain1->set_k(gain);
    }
}

ReceiverChannel::status ReceiverChannel::start_audio_recording(const std::string& filename)
{
    if (d_recording_wav) {
        return STATUS_ERROR;
    }

    try {
#if GNURADIO_VERSION < 0x030900
        d_wav_sink = gr::blocks::wavfile_sink::make(filename.c_str(),
                                                   2, // stereo
                                                   d_audio_rate,
                                                   16); // 16-bit
#else
        d_wav_sink = gr::blocks::wavfile_sink::make(filename.c_str(),
                                                   2, // stereo
                                                   d_audio_rate,
                                                   gr::blocks::FORMAT_WAV,
                                                   gr::blocks::FORMAT_PCM_16);
#endif

        if (d_parent_tb && d_connected) {
            d_parent_tb->lock();

            d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_null_sink0, 0);
            d_parent_tb->disconnect(d_audio_gain1, 0, d_audio_null_sink1, 0);

            d_parent_tb->connect(d_audio_gain0, 0, d_wav_gain0, 0);
            d_parent_tb->connect(d_audio_gain1, 0, d_wav_gain1, 0);
            d_parent_tb->connect(d_wav_gain0, 0, d_wav_sink, 0);
            d_parent_tb->connect(d_wav_gain1, 0, d_wav_sink, 1);

            d_parent_tb->unlock();
        }

        d_recording_wav = true;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            d_parent_tb->unlock();
        }
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::stop_audio_recording()
{
    if (!d_recording_wav) {
        return STATUS_ERROR;
    }

    try {
        if (d_parent_tb && d_connected) {
            d_parent_tb->lock();

            d_parent_tb->disconnect(d_audio_gain0, 0, d_wav_gain0, 0);
            d_parent_tb->disconnect(d_audio_gain1, 0, d_wav_gain1, 0);
            d_parent_tb->disconnect(d_wav_gain0, 0, d_wav_sink, 0);
            d_parent_tb->disconnect(d_wav_gain1, 0, d_wav_sink, 1);

            d_parent_tb->connect(d_audio_gain0, 0, d_audio_null_sink0, 0);
            d_parent_tb->connect(d_audio_gain1, 0, d_audio_null_sink1, 0);

            d_parent_tb->unlock();
        }

        d_wav_sink.reset();
        d_recording_wav = false;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            d_parent_tb->unlock();
        }
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::start_udp_streaming(const std::string& host, int port, bool stereo)
{
    if (!d_parent_tb || !d_connected) {
        return STATUS_ERROR;
    }

    try {
        d_audio_udp_sink->start_streaming(host, port, stereo);

        d_parent_tb->lock();

        d_parent_tb->connect(d_audio_gain0, 0, d_audio_udp_sink, 0);
        if (stereo) {
            d_parent_tb->connect(d_audio_gain1, 0, d_audio_udp_sink, 1);
        }

        d_parent_tb->unlock();
        return STATUS_OK;

    } catch (std::exception& e) {
        d_parent_tb->unlock();
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::stop_udp_streaming()
{
    if (!d_parent_tb || !d_connected) {
        return STATUS_ERROR;
    }

    try {
        d_audio_udp_sink->stop_streaming();

        d_parent_tb->lock();

        d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_udp_sink, 0);
        d_parent_tb->disconnect(d_audio_gain1, 0, d_audio_udp_sink, 1);

        d_parent_tb->unlock();
        return STATUS_OK;

    } catch (std::exception& e) {
        d_parent_tb->unlock();
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::start_sniffer(unsigned int samplerate, int buffsize)
{
    if (d_sniffer_active) {
        return STATUS_ERROR;
    }

    if (buffsize > 0) {
        d_sniffer->set_buffer_size(buffsize);
    }

    d_sniffer_active = true;
    return STATUS_OK;
}

ReceiverChannel::status ReceiverChannel::stop_sniffer()
{
    if (!d_sniffer_active) {
        return STATUS_ERROR;
    }

    d_sniffer_active = false;
    return STATUS_OK;
}

void ReceiverChannel::get_sniffer_data(float* buffer, unsigned int& buffer_size)
{
    if (!d_sniffer_active || !d_sniffer) {
        buffer_size = 0;
        return;
    }

    d_sniffer->get_samples(buffer, buffer_size);
}

void ReceiverChannel::get_audio_fft_data(std::vector<float>& fftData, unsigned int& fftSize)
{
    if (!d_audio_fft) {
        fftSize = 0;
        return;
    }

    fftSize = d_audio_fft->fft_size();
    fftData.resize(fftSize);
    d_audio_fft->get_fft_data(fftData.data());
}

void ReceiverChannel::set_audio_device(const std::string& device)
{
    d_audio_device = device;

    // Recreate audio sink if we're connected
    if (d_connected) {
        try {
#ifdef WITH_PULSEAUDIO
            d_audio_snk = make_pa_sink(device, d_audio_rate, "GQRX", "Channel " + std::to_string(d_channel_id));
#elif WITH_PORTAUDIO
            d_audio_snk = make_portaudio_sink(device, d_audio_rate, "GQRX", "Channel " + std::to_string(d_channel_id));
#else
            d_audio_snk = gr::audio::sink::make(d_audio_rate, device);
#endif
        } catch (std::exception&) {
            // Audio device creation may fail - continue without audio
        }
    }
}

void ReceiverChannel::set_enabled(bool enabled)
{
    if (d_enabled == enabled) {
        return;  // No change
    }

    d_enabled = enabled;

    if (d_connected && d_parent_tb && d_ddc && d_source) {
        bool was_locked = false;
        try {
            d_parent_tb->lock();
            was_locked = true;
        } catch (...) {}

        try {
            if (!enabled) {
                d_parent_tb->disconnect(d_source, d_source_port, d_ddc, 0);
                d_parent_tb->connect(d_ddc_null_source, 0, d_ddc, 0);
                set_audio_gain(0.0f);
            } else {
                d_parent_tb->disconnect(d_ddc_null_source, 0, d_ddc, 0);
                d_parent_tb->connect(d_source, d_source_port, d_ddc, 0);
            }
        } catch (std::exception&) {
        }

        if (was_locked) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
    } else if (!enabled) {
        set_audio_gain(0.0f);
    }
}

void ReceiverChannel::set_bypassed(bool bypassed)
{
    if (d_bypassed == bypassed) {
        return;  // No change
    }

    d_bypassed = bypassed;

    if (d_connected && d_parent_tb && d_ddc && d_source && d_enabled) {
        bool was_locked = false;
        try {
            d_parent_tb->lock();
            was_locked = true;
        } catch (...) {}

        try {
            if (bypassed) {
                d_parent_tb->disconnect(d_source, d_source_port, d_ddc, 0);
                d_parent_tb->connect(d_ddc_null_source, 0, d_ddc, 0);
                set_audio_gain(0.0f);
            } else {
                d_parent_tb->disconnect(d_ddc_null_source, 0, d_ddc, 0);
                d_parent_tb->connect(d_source, d_source_port, d_ddc, 0);
            }
        } catch (std::exception&) {
        }

        if (was_locked) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
    } else if (bypassed) {
        set_audio_gain(0.0f);
    }
}

void ReceiverChannel::set_cw_offset(double offset)
{
    d_cw_offset = offset;
    if (d_backend) {
        // CW offset is handled by the backend's internal receiver
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_cw_offset(offset);
        }
    }
}

ReceiverChannel::status ReceiverChannel::set_filter_offset(double offset)
{
    d_filter_offset = offset;
    if (d_ddc) {
        d_ddc->set_center_freq(d_center_freq + offset);
    }
    return STATUS_OK;
}


void ReceiverChannel::set_agc_on(bool on)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_agc_on(on);
        }
    }
}

bool ReceiverChannel::get_agc_on() const
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<const AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            return nbrx_backend->get_agc_on();
        }
    }
    return false;
}

void ReceiverChannel::set_agc_hang(bool use_hang)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_agc_hang(use_hang);
        }
    }
}

void ReceiverChannel::set_agc_threshold(int threshold)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_agc_threshold(threshold);
        }
    }
}

void ReceiverChannel::set_agc_slope(int slope)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_agc_slope(slope);
        }
    }
}

void ReceiverChannel::set_agc_decay(int decay_ms)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_agc_decay(decay_ms);
        }
    }
}

void ReceiverChannel::set_agc_manual_gain(int gain)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_agc_manual_gain(gain);
        }
    }
}

void ReceiverChannel::set_nb_on(int nbid, bool on)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_nb_on(nbid, on);
        }
    }
}

void ReceiverChannel::set_nb_threshold(int nbid, float threshold)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_nb_threshold(nbid, threshold);
        }
    }
}

float ReceiverChannel::get_audio_gain() const
{
    if (d_audio_gain0) {
        return d_audio_gain0->k();
    }
    return 0.0f;
}

void ReceiverChannel::set_audio_mute(bool mute)
{
    if (mute) {
        set_audio_gain(0.0f);
    } else {
        set_audio_gain(0.1f); // Default gain when unmuting
    }
    return;
}

bool ReceiverChannel::get_audio_mute() const
{
    return get_audio_gain() == 0.0f;
}

double ReceiverChannel::get_center_freq() const
{
    return d_center_freq;
}

ReceiverChannel::rx_demod ReceiverChannel::get_demod() const
{
    return d_demod;
}

ReceiverChannel::status ReceiverChannel::set_filter(double low, double high, double tw)
{
    if (d_backend) {
        d_backend->set_filter(low, high, tw);
    }
    return STATUS_OK;
}

bool ReceiverChannel::is_rds_decoder_active() const
{
    // For now, return false - RDS support can be added later
    return false;
}

void ReceiverChannel::set_sql_level(double level_db)
{
    d_sql_level = level_db;
    if (d_backend) {
        d_backend->set_sql_level(level_db);
    }
}

double ReceiverChannel::get_sql_level() const
{
    return d_sql_level;
}

void ReceiverChannel::set_fm_maxdev(float maxdev_hz)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_fm_maxdev(maxdev_hz);
        }
    }
}

void ReceiverChannel::set_fm_deemph(double tau)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_fm_deemph(tau);
        }
    }
}

void ReceiverChannel::set_am_dcr(bool enabled)
{
    if (d_backend) {
        auto* nbrx_backend = dynamic_cast<AnalogNbrxBackend*>(d_backend.get());
        if (nbrx_backend) {
            nbrx_backend->set_am_dcr(enabled);
        }
    }
}

ReceiverChannel::status ReceiverChannel::start_audio_playback()
{
    if (d_audio_playing) {
        return STATUS_OK; // Already playing
    }

    if (!d_parent_tb || !d_connected) {
        return STATUS_ERROR;
    }

    // If using external mixer, just mark as playing - mixer will handle connections
    if (!d_use_internal_audio_sink) {
        d_audio_playing = true;
        return STATUS_OK;
    }

    try {
        d_parent_tb->lock();

        d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_null_sink0, 0);
        d_parent_tb->disconnect(d_audio_gain1, 0, d_audio_null_sink1, 0);

        d_parent_tb->connect(d_audio_gain0, 0, d_audio_snk, 0);
        d_parent_tb->connect(d_audio_gain1, 0, d_audio_snk, 1);

        d_parent_tb->unlock();

        d_audio_playing = true;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            d_parent_tb->unlock();
        }
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::stop_audio_playback()
{
    if (!d_audio_playing) {
        return STATUS_OK; // Already stopped
    }

    // If using external mixer, just mark as stopped - mixer will handle connections
    if (!d_use_internal_audio_sink) {
        d_audio_playing = false;
        return STATUS_OK;
    }

    if (!d_parent_tb || !d_connected) {
        return STATUS_ERROR;
    }

    try {
        d_parent_tb->lock();

        d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_snk, 0);
        d_parent_tb->disconnect(d_audio_gain1, 0, d_audio_snk, 1);

        d_parent_tb->connect(d_audio_gain0, 0, d_audio_null_sink0, 0);
        d_parent_tb->connect(d_audio_gain1, 0, d_audio_null_sink1, 0);

        d_parent_tb->unlock();

        d_audio_playing = false;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            d_parent_tb->unlock();
        }
        return STATUS_ERROR;
    }
}

void ReceiverChannel::set_use_internal_audio_sink(bool use_internal)
{
    d_use_internal_audio_sink = use_internal;
}

// ═══════════════════════════════════════════════════════════════════════════
// IReceiverChannel Interface Implementation
// ═══════════════════════════════════════════════════════════════════════════

void ReceiverChannel::set_freq_offset(double hz)
{
    set_center_freq(hz);
}

double ReceiverChannel::get_freq_offset() const
{
    return d_center_freq;
}

void ReceiverChannel::set_filter_width(double low_hz, double high_hz)
{
    // Use default transition width based on filter span
    double tw = std::max(100.0, (high_hz - low_hz) * 0.1);
    set_filter(low_hz, high_hz, tw);
}

float ReceiverChannel::get_signal_level() const
{
    if (d_backend) {
        return d_backend->get_signal_level();
    }
    return -100.0f;
}

void ReceiverChannel::set_squelch_level(double db)
{
    set_sql_level(db);
}

double ReceiverChannel::get_squelch_level() const
{
    return get_sql_level();
}

void ReceiverChannel::set_backend(IReceiverBackend_ptr backend)
{
    // If not connected to flowgraph yet, just store the backend
    if (!d_parent_tb || !d_connected) {
        d_backend = std::move(backend);
        // Sync d_demod with backend type
        if (d_backend) {
            rx_demod new_demod = receiver_type_to_demod(d_backend->type());
            d_demod = new_demod;
        }
        return;
    }

    // Only lock if flowgraph is running (try-catch pattern)
    bool was_locked = false;
    try {
        d_parent_tb->lock();
        was_locked = true;
    } catch (...) {
        // Flowgraph not running - that's OK
        was_locked = false;
    }

    try {
        if (d_backend) {
            // Scope ensures old_audio_out is destroyed before disconnect()
            {
                auto old_audio_out = d_backend->audio_output(0);
                if (old_audio_out && d_audio_gain0 && d_audio_gain1) {
                    try {
                        d_parent_tb->disconnect(old_audio_out, 0, d_audio_gain0, 0);
                        d_parent_tb->disconnect(old_audio_out, 1, d_audio_gain1, 0);
                    } catch (...) {}
                }
            }
            d_backend->disconnect();
            d_backend.reset();
        } else {
            if (d_ddc && d_ddc_null_sink) {
                try { d_parent_tb->disconnect(d_ddc, 0, d_ddc_null_sink, 0); } catch (...) {}
            }
            if (d_audio_null_src0 && d_audio_gain0) {
                try { d_parent_tb->disconnect(d_audio_null_src0, 0, d_audio_gain0, 0); } catch (...) {}
                try { d_parent_tb->disconnect(d_audio_null_src1, 0, d_audio_gain1, 0); } catch (...) {}
            }
        }

        d_backend = std::move(backend);

        if (d_backend) {
            rx_demod new_demod = receiver_type_to_demod(d_backend->type());
            d_demod = new_demod;
        }

        if (d_backend) {
            d_backend->connect(d_parent_tb, d_ddc);

            // Ensure null sources disconnected before connecting backend audio
            if (d_audio_null_src0 && d_audio_gain0) {
                try { d_parent_tb->disconnect(d_audio_null_src0, 0, d_audio_gain0, 0); } catch (...) {}
                try { d_parent_tb->disconnect(d_audio_null_src1, 0, d_audio_gain1, 0); } catch (...) {}
            }

            auto audio_out = d_backend->audio_output(0);
            if (audio_out) {
                d_parent_tb->connect(audio_out, 0, d_audio_gain0, 0);
                d_parent_tb->connect(audio_out, 1, d_audio_gain1, 0);
            }
        } else {
            if (d_ddc && d_ddc_null_sink) {
                d_parent_tb->connect(d_ddc, 0, d_ddc_null_sink, 0);
            }
            if (d_audio_null_src0 && d_audio_gain0) {
                d_parent_tb->connect(d_audio_null_src0, 0, d_audio_gain0, 0);
                d_parent_tb->connect(d_audio_null_src1, 0, d_audio_gain1, 0);
            }
        }

        if (was_locked) {
            d_parent_tb->unlock();
        }

    } catch (std::exception& e) {
        if (was_locked) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
        throw;
    }
}

void ReceiverChannel::connect_to_source_locked(gr::basic_block_sptr source, int source_port)
{
    // Version that assumes flowgraph is ALREADY LOCKED by caller

    if (!d_parent_tb) {
        return;
    }

    // Store source reference even if bypassed (needed for un-bypass later)
    d_source = source;
    d_source_port = source_port;

    // Connect DDC input - respect bypass state
    if (d_bypassed && d_ddc_null_source) {
        // Bypassed: connect null source to DDC (saves CPU)
        d_parent_tb->connect(d_ddc_null_source, 0, d_ddc, 0);
    } else {
        // Normal: connect real source to DDC
        d_parent_tb->connect(source, source_port, d_ddc, 0);
    }

    // Connect placeholder blocks
    d_parent_tb->connect(d_ddc, 0, d_ddc_null_sink, 0);
    d_parent_tb->connect(d_audio_null_src0, 0, d_audio_gain0, 0);
    d_parent_tb->connect(d_audio_null_src1, 0, d_audio_gain1, 0);
    d_parent_tb->connect(d_audio_gain0, 0, d_audio_null_sink0, 0);
    d_parent_tb->connect(d_audio_gain1, 0, d_audio_null_sink1, 0);

    d_parent_tb->connect(d_audio_gain0, 0, d_audio_fft, 0);
    d_parent_tb->connect(d_audio_gain0, 0, d_sniffer, 0);

    d_connected = true;
}

void ReceiverChannel::set_backend_locked(IReceiverBackend_ptr backend)
{
    if (!d_parent_tb || !d_connected) {
        d_backend = std::move(backend);
        if (d_backend) {
            rx_demod new_demod = receiver_type_to_demod(d_backend->type());
            d_demod = new_demod;
        }
        return;
    }

    if (d_backend) {
        auto old_audio_out = d_backend->audio_output(0);
        if (old_audio_out && d_audio_gain0 && d_audio_gain1) {
            try {
                d_parent_tb->disconnect(old_audio_out, 0, d_audio_gain0, 0);
                d_parent_tb->disconnect(old_audio_out, 1, d_audio_gain1, 0);
            } catch (std::exception&) {
            }
        }
        d_backend->disconnect();
        d_backend.reset();
    } else {
        if (d_ddc && d_ddc_null_sink) {
            try { d_parent_tb->disconnect(d_ddc, 0, d_ddc_null_sink, 0); } catch (...) {}
        }
        if (d_audio_null_src0 && d_audio_gain0) {
            try { d_parent_tb->disconnect(d_audio_null_src0, 0, d_audio_gain0, 0); } catch (...) {}
            try { d_parent_tb->disconnect(d_audio_null_src1, 0, d_audio_gain1, 0); } catch (...) {}
        }
    }

    d_backend = std::move(backend);

    if (d_backend) {
        rx_demod new_demod = receiver_type_to_demod(d_backend->type());
        d_demod = new_demod;
    }

    if (d_backend) {
        d_backend->connect(d_parent_tb, d_ddc);
        auto audio_out = d_backend->audio_output(0);
        if (audio_out) {
            d_parent_tb->connect(audio_out, 0, d_audio_gain0, 0);
            d_parent_tb->connect(audio_out, 1, d_audio_gain1, 0);
        }
    } else {
        if (d_ddc && d_ddc_null_sink) {
            d_parent_tb->connect(d_ddc, 0, d_ddc_null_sink, 0);
        }
        if (d_audio_null_src0 && d_audio_gain0) {
            d_parent_tb->connect(d_audio_null_src0, 0, d_audio_gain0, 0);
            d_parent_tb->connect(d_audio_null_src1, 0, d_audio_gain1, 0);
        }
    }
}

void ReceiverChannel::set_input_rate_locked(double rate)
{
    if (std::abs(rate - d_input_rate) < 1.0) {
        return;
    }

    d_input_rate = rate;

    unsigned int decim = std::max(1u, (unsigned int)(d_input_rate / TARGET_QUAD_RATE));
    d_quad_rate = d_input_rate / decim;

    // Recreate DDC and backend when input rate changes
    d_ddc.reset();
    d_ddc = make_downconverter_cc(decim, d_center_freq, d_input_rate);

    if (d_backend) {
        if (d_connected) {
            auto old_audio_out = d_backend->audio_output(0);
            if (old_audio_out && d_audio_gain0 && d_audio_gain1) {
                try {
                    d_parent_tb->disconnect(old_audio_out, 0, d_audio_gain0, 0);
                    d_parent_tb->disconnect(old_audio_out, 1, d_audio_gain1, 0);
                } catch (std::exception&) {
                }
            }
        }

        d_backend->set_quad_rate(d_quad_rate);
    }
}

void ReceiverChannel::reconnect_backend_locked()
{
    if (!d_parent_tb || !d_connected || !d_backend) {
        return;
    }

    try {
        if (d_ddc && d_ddc_null_sink) {
            try { d_parent_tb->disconnect(d_ddc, 0, d_ddc_null_sink, 0); } catch (std::exception&) {}
        }
        if (d_audio_null_src0 && d_audio_gain0) {
            try {
                d_parent_tb->disconnect(d_audio_null_src0, 0, d_audio_gain0, 0);
                d_parent_tb->disconnect(d_audio_null_src1, 0, d_audio_gain1, 0);
            } catch (std::exception&) {}
        }

        d_backend->connect(d_parent_tb, d_ddc);

        auto audio_out = d_backend->audio_output(0);
        if (audio_out) {
            d_parent_tb->connect(audio_out, 0, d_audio_gain0, 0);
            d_parent_tb->connect(audio_out, 1, d_audio_gain1, 0);
        }

        d_audio_muted = d_bypassed;

    } catch (std::exception&) {
    }
}

ReceiverType ReceiverChannel::get_backend_type() const
{
    if (d_backend) {
        return d_backend->type();
    }

    // Map legacy demod to ReceiverType
    switch (d_demod) {
        case RX_DEMOD_OFF:
            return ReceiverType::ANALOG_OFF;
        case RX_DEMOD_NONE:
            return ReceiverType::ANALOG_RAW;
        case RX_DEMOD_AM:
            return ReceiverType::ANALOG_AM;
        case RX_DEMOD_NFM:
            return ReceiverType::ANALOG_NFM;
        case RX_DEMOD_WFM_M:
            return ReceiverType::ANALOG_WFM_MONO;
        case RX_DEMOD_WFM_S:
            return ReceiverType::ANALOG_WFM_STEREO;
        case RX_DEMOD_WFM_S_OIRT:
            return ReceiverType::ANALOG_WFM_STEREO_OIRT;
        case RX_DEMOD_SSB:
            return ReceiverType::ANALOG_USB;  // Default to USB
        case RX_DEMOD_AMSYNC:
            return ReceiverType::ANALOG_AMSYNC;
        default:
            return ReceiverType::ANALOG_NFM;
    }
}

void ReceiverChannel::set_muted(bool muted)
{
    d_audio_muted = muted;
    set_audio_mute(muted);
}

bool ReceiverChannel::is_muted() const
{
    return d_audio_muted;
}

// ═══════════════════════════════════════════════════════════════════════════
// New Recording System Implementation
// ═══════════════════════════════════════════════════════════════════════════

ReceiverChannel::status ReceiverChannel::start_iq_recording(const QString& filepath)
{
    if (d_iq_recording_active) {
        return STATUS_ERROR;
    }

    if (!d_parent_tb || !d_connected || !d_ddc) {
        return STATUS_ERROR;
    }

    try {
        // Determine IQ source and sample rate based on tap point
        gr::basic_block_sptr iq_source;
        double iq_sample_rate;

        // Determine the output port for the IQ source
        int iq_source_port = 0;

        switch (d_iq_tap_point) {
            case IqTapPoint::AFTER_FILTER:
                // Tap from backend's filtered IQ output (~96 kHz for nbrx, ~240 kHz for wfm)
                if (d_backend && d_backend->get_filtered_iq_output()) {
                    iq_source = d_backend->get_filtered_iq_output();
                    iq_source_port = d_backend->get_filtered_iq_port();  // Port 2 for hierarchical blocks
                    iq_sample_rate = d_backend->get_filtered_iq_rate();
                } else {
                    // Fallback to DDC if backend doesn't support filtered IQ
                    iq_source = d_ddc;
                    iq_source_port = 0;
                    iq_sample_rate = d_quad_rate;
                }
                break;

            case IqTapPoint::AFTER_DDC:
            default:
                // Tap from DDC output (~1 MHz)
                iq_source = d_ddc;
                iq_source_port = 0;
                iq_sample_rate = d_quad_rate;
                break;
        }

        // Create IQ recorder with correct sample rate
        if (!d_iq_recorder) {
            d_iq_recorder = make_iq_recorder(iq_sample_rate, 0.0);
        } else {
            d_iq_recorder->set_sample_rate(iq_sample_rate);
        }

        d_parent_tb->lock();

        // Connect IQ source to IQ recorder (using the correct output port)
        d_parent_tb->connect(iq_source, iq_source_port, d_iq_recorder, 0);
        d_iq_recording_source = iq_source;  // Remember for disconnect
        d_iq_recording_source_port = iq_source_port;  // Remember port for disconnect

        d_parent_tb->unlock();

        // Start recording
        if (!d_iq_recorder->start_recording(filepath)) {
            // Failed to start - disconnect
            d_parent_tb->lock();
            d_parent_tb->disconnect(d_iq_recording_source, d_iq_recording_source_port, d_iq_recorder, 0);
            d_parent_tb->unlock();
            d_iq_recording_source.reset();
            d_iq_recording_source_port = 0;
            return STATUS_ERROR;
        }

        d_iq_recording_active = true;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
        d_iq_recording_source.reset();
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::stop_iq_recording()
{
    if (!d_iq_recording_active || !d_iq_recorder) {
        return STATUS_OK;
    }

    try {
        // Stop recording first
        d_iq_recorder->stop_recording();

        if (d_parent_tb && d_iq_recording_source) {
            d_parent_tb->lock();
            d_parent_tb->disconnect(d_iq_recording_source, d_iq_recording_source_port, d_iq_recorder, 0);
            d_parent_tb->unlock();
        }

        d_iq_recording_source.reset();
        d_iq_recording_source_port = 0;
        d_iq_recording_active = false;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
        d_iq_recording_source.reset();
        d_iq_recording_source_port = 0;
        d_iq_recording_active = false;
        return STATUS_ERROR;
    }
}

bool ReceiverChannel::is_recording_iq() const
{
    return d_iq_recording_active && d_iq_recorder && d_iq_recorder->is_recording();
}

void ReceiverChannel::set_iq_recording_format(IqFileFormat format)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(d_quad_rate, 0.0);
    }
    d_iq_recorder->set_format(format);
}

void ReceiverChannel::set_iq_recording_sample_rate(double rate)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(rate, 0.0);
    } else {
        d_iq_recorder->set_sample_rate(rate);
    }
}

void ReceiverChannel::set_iq_recording_center_freq(double freq)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(d_quad_rate, freq);
    } else {
        d_iq_recorder->set_center_freq(freq);
    }
}

void ReceiverChannel::set_iq_tap_point(IqTapPoint tap_point)
{
    if (d_iq_recording_active) {
        return;
    }
    d_iq_tap_point = tap_point;
}

void ReceiverChannel::set_sigmf_config(const SigMFConfig& config)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(d_quad_rate, 0.0);
    }
    d_iq_recorder->set_sigmf_config(config);
}

void ReceiverChannel::set_iq_recording_mode(RecordingMode mode)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(d_quad_rate, 0.0);
    }
    d_iq_recorder->set_recording_mode(mode);
}

void ReceiverChannel::set_iq_split_minutes(int minutes)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(d_quad_rate, 0.0);
    }
    d_iq_recorder->set_split_minutes(minutes);
}

void ReceiverChannel::set_iq_pre_buffer_ms(int ms)
{
    if (!d_iq_recorder) {
        d_iq_recorder = make_iq_recorder(d_quad_rate, 0.0);
    }
    d_iq_recorder->set_pre_buffer_ms(ms);
}

uint64_t ReceiverChannel::get_iq_samples_recorded() const
{
    if (d_iq_recorder) {
        return d_iq_recorder->get_samples_recorded();
    }
    return 0;
}

double ReceiverChannel::get_iq_recording_duration() const
{
    if (d_iq_recorder) {
        return d_iq_recorder->get_duration();
    }
    return 0.0;
}

// Audio Recording with squelch modes

ReceiverChannel::status ReceiverChannel::start_new_audio_recording(const QString& filepath)
{
    if (d_new_audio_recording_active) {
        return STATUS_ERROR;
    }

    if (!d_parent_tb || !d_connected || !d_audio_gain0) {
        return STATUS_ERROR;
    }

    try {
        // Create audio recorder if not exists
        if (!d_audio_recorder) {
            d_audio_recorder = make_audio_recorder(static_cast<int>(d_audio_rate));
        }

        d_parent_tb->lock();

        // Connect audio gain output to audio recorder
        d_parent_tb->connect(d_audio_gain0, 0, d_audio_recorder, 0);

        d_parent_tb->unlock();

        // Start recording (or arm for squelch mode)
        if (!d_audio_recorder->start_recording(filepath)) {
            // Failed to start - disconnect
            d_parent_tb->lock();
            d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_recorder, 0);
            d_parent_tb->unlock();
            return STATUS_ERROR;
        }

        d_new_audio_recording_active = true;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
        return STATUS_ERROR;
    }
}

ReceiverChannel::status ReceiverChannel::stop_new_audio_recording()
{
    if (!d_new_audio_recording_active || !d_audio_recorder) {
        return STATUS_OK;
    }

    try {
        // Stop recording first
        d_audio_recorder->stop_recording();

        if (d_parent_tb && d_audio_gain0) {
            d_parent_tb->lock();
            d_parent_tb->disconnect(d_audio_gain0, 0, d_audio_recorder, 0);
            d_parent_tb->unlock();
        }

        d_new_audio_recording_active = false;
        return STATUS_OK;

    } catch (std::exception& e) {
        if (d_parent_tb) {
            try { d_parent_tb->unlock(); } catch (...) {}
        }
        d_new_audio_recording_active = false;
        return STATUS_ERROR;
    }
}

bool ReceiverChannel::is_new_audio_recording() const
{
    return d_new_audio_recording_active && d_audio_recorder && d_audio_recorder->is_recording();
}

void ReceiverChannel::set_audio_recording_format(AudioFileFormat format)
{
    if (!d_audio_recorder) {
        d_audio_recorder = make_audio_recorder(static_cast<int>(d_audio_rate));
    }
    d_audio_recorder->set_format(format);
}

void ReceiverChannel::set_audio_recording_wav_format(WavSampleFormat format)
{
    if (!d_audio_recorder) {
        d_audio_recorder = make_audio_recorder(static_cast<int>(d_audio_rate));
    }
    d_audio_recorder->set_wav_format(format);
}

void ReceiverChannel::set_audio_recording_mode(RecordingMode mode)
{
    if (!d_audio_recorder) {
        d_audio_recorder = make_audio_recorder(static_cast<int>(d_audio_rate));
    }
    d_audio_recorder->set_mode(mode);
}

void ReceiverChannel::set_audio_squelch_config(const SquelchRecordingConfig& config)
{
    if (!d_audio_recorder) {
        d_audio_recorder = make_audio_recorder(static_cast<int>(d_audio_rate));
    }
    d_audio_recorder->set_squelch_config(config);
}

void ReceiverChannel::set_audio_split_minutes(int minutes)
{
    if (!d_audio_recorder) {
        d_audio_recorder = make_audio_recorder(static_cast<int>(d_audio_rate));
    }
    d_audio_recorder->set_split_minutes(minutes);
}

void ReceiverChannel::notify_squelch_open(bool open)
{
    if (d_audio_recorder) {
        d_audio_recorder->set_squelch_open(open);
    }
    if (d_iq_recorder) {
        d_iq_recorder->set_squelch_open(open);
    }
}

uint64_t ReceiverChannel::get_audio_samples_recorded() const
{
    if (d_audio_recorder) {
        return d_audio_recorder->get_samples_recorded();
    }
    return 0;
}

double ReceiverChannel::get_audio_recording_duration() const
{
    if (d_audio_recorder) {
        return d_audio_recorder->get_duration();
    }
    return 0.0;
}

int ReceiverChannel::get_audio_call_count() const
{
    if (d_audio_recorder) {
        return d_audio_recorder->get_call_count();
    }
    return 0;
}
