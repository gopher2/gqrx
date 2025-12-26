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

#include "tuner_manager.h"
#include "backends/receiver_backend_factory.h"
#include <gnuradio/blocks/null_sink.h>
#include <QSettings>
#include <algorithm>

TunerManager::TunerManager()
    : d_running(false)
    , d_hw_mode(HW_MODE_SINGLE_SHARED)
    , d_input_rate(2048000.0)
    , d_input_decim(1)
    , d_rf_freq(100000000.0)
    , d_iq_swap(false)
    , d_dc_cancel(false)
    , d_iq_balance(false)
    , d_fft_size(4096)
    , d_audio_rate(48000)
    , d_audio_mixer_connected(false)
    , d_mixer_tuner_count(0)
    , d_next_tuner_id(0)
    , d_active_tuner(-1)
    , d_max_channels(0)  // 0 = unlimited
    , d_global_recording_active(false)
    , d_recording_iq(false)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    create_flowgraph();

    // Load recording settings from persistent storage
    QSettings settings;
    d_recording_config.load(settings);
}

TunerManager::~TunerManager()
{
    stop();
    destroy_flowgraph();
}

void TunerManager::set_shared_flowgraph(gr::top_block_sptr tb, gr::basic_block_sptr iq_source)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_shared_tb = tb;
    d_shared_iq_source = iq_source;

    // If we have existing tuners, reconnect them to the shared source and start audio
    for (auto& tuner : d_tuners) {
        if (tuner && d_shared_tb && d_shared_iq_source) {
            tuner->connect_to_source(d_shared_iq_source, 0);
            tuner->start_audio_playback();
        }
    }
}

void TunerManager::create_flowgraph()
{
    d_top_block = gr::make_top_block("tuner_manager");

    // Initialize hardware devices vector
    d_hardware_devices.clear();
    d_device_strings.clear();

    // Initialize primary source
    d_src.reset();
    d_input_device.clear();

    // Initialize correction blocks
    d_dc_corr.reset();
    d_iq_swap_block.reset();
    d_input_decim_block.reset();
    d_iq_fft.reset();

    d_copy_blocks.clear();
    d_tuners.clear();
    d_tuner_id_to_index.clear();

    // Initialize FFT and IQ processing blocks
    d_iq_swap_block = make_iq_swap_cc(false);
    d_dc_corr = make_dc_corr_cc(d_input_rate / d_input_decim, 1.0);
    d_iq_fft = make_rx_fft_c(d_fft_size > 0 ? d_fft_size : 4096, d_input_rate / d_input_decim, gr::fft::window::WIN_HANN);

    // Create input decimator if needed
    if (d_input_decim >= 2) {
        try {
            d_input_decim_block = make_fir_decim_cc(d_input_decim);
        } catch (std::range_error& e) {
            d_input_decim = 1;
        }
    }

    // Create shared audio mixer
    create_audio_mixer();
}

void TunerManager::create_audio_mixer()
{
    d_audio_sink = make_portaudio_sink("", d_audio_rate, "GQRX", "Multi-Tuner Audio");

    d_audio_null_left = gr::blocks::null_sink::make(sizeof(float));
    d_audio_null_right = gr::blocks::null_sink::make(sizeof(float));

    d_audio_mixer_connected = false;
    d_mixer_tuner_count = 0;
}

void TunerManager::rebuild_audio_mixer()
{
    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;
    if (!tb) {
        return;
    }

    try {
        tb->lock();

        if (d_audio_mixer_connected && d_audio_mixer_left && d_audio_mixer_right) {
            try {
                tb->disconnect(d_audio_mixer_left, 0, d_audio_sink, 0);
                tb->disconnect(d_audio_mixer_right, 0, d_audio_sink, 1);
            } catch (...) {}

            for (size_t i = 0; i < d_mixer_tuner_count && i < d_tuners.size(); i++) {
                auto tuner = d_tuners[i];
                auto left_out = tuner->get_audio_output_left();
                auto right_out = tuner->get_audio_output_right();
                if (left_out && right_out) {
                    try {
                        tb->disconnect(left_out, 0, d_audio_mixer_left, i);
                        tb->disconnect(right_out, 0, d_audio_mixer_right, i);
                    } catch (...) {}
                }
            }
            d_audio_mixer_connected = false;
            d_mixer_tuner_count = 0;
        }

        size_t num_tuners = d_tuners.size();
        if (num_tuners == 0) {
            d_audio_mixer_left.reset();
            d_audio_mixer_right.reset();
            tb->unlock();
            return;
        }

        d_audio_mixer_left = gr::blocks::add_ff::make();
        d_audio_mixer_right = gr::blocks::add_ff::make();

        for (size_t i = 0; i < num_tuners; i++) {
            auto tuner = d_tuners[i];
            auto left_out = tuner->get_audio_output_left();
            auto right_out = tuner->get_audio_output_right();

            if (left_out && right_out) {
                try {
                    tb->disconnect(left_out, 0, tuner->get_audio_null_sink_left(), 0);
                } catch (...) {}
                try {
                    tb->disconnect(right_out, 0, tuner->get_audio_null_sink_right(), 0);
                } catch (...) {}

                tb->connect(left_out, 0, d_audio_mixer_left, i);
                tb->connect(right_out, 0, d_audio_mixer_right, i);
            }
        }

        tb->connect(d_audio_mixer_left, 0, d_audio_sink, 0);
        tb->connect(d_audio_mixer_right, 0, d_audio_sink, 1);

        d_audio_mixer_connected = true;
        d_mixer_tuner_count = num_tuners;

        tb->unlock();

    } catch (std::exception& e) {
        if (tb) tb->unlock();
    }
}

void TunerManager::rebuild_audio_mixer_locked(gr::top_block_sptr tb)
{
    if (!tb) {
        return;
    }

    try {
        if (d_audio_mixer_connected && d_audio_mixer_left && d_audio_mixer_right) {
            try {
                tb->disconnect(d_audio_mixer_left, 0, d_audio_sink, 0);
                tb->disconnect(d_audio_mixer_right, 0, d_audio_sink, 1);
            } catch (...) {}

            for (size_t i = 0; i < d_mixer_tuner_count && i < d_tuners.size(); i++) {
                auto tuner = d_tuners[i];
                auto left_out = tuner->get_audio_output_left();
                auto right_out = tuner->get_audio_output_right();
                if (left_out && right_out) {
                    try {
                        tb->disconnect(left_out, 0, d_audio_mixer_left, i);
                        tb->disconnect(right_out, 0, d_audio_mixer_right, i);
                    } catch (...) {}
                }
            }
            d_audio_mixer_connected = false;
            d_mixer_tuner_count = 0;
        }

        size_t num_tuners = d_tuners.size();
        if (num_tuners == 0) {
            d_audio_mixer_left.reset();
            d_audio_mixer_right.reset();
            return;
        }

        d_audio_mixer_left = gr::blocks::add_ff::make();
        d_audio_mixer_right = gr::blocks::add_ff::make();

        for (size_t i = 0; i < num_tuners; i++) {
            auto tuner = d_tuners[i];
            auto left_out = tuner->get_audio_output_left();
            auto right_out = tuner->get_audio_output_right();

            if (left_out && right_out) {
                try {
                    tb->disconnect(left_out, 0, tuner->get_audio_null_sink_left(), 0);
                } catch (std::exception&) {
                }
                try {
                    tb->disconnect(right_out, 0, tuner->get_audio_null_sink_right(), 0);
                } catch (...) {
                }

                try {
                    tb->connect(left_out, 0, d_audio_mixer_left, i);
                    tb->connect(right_out, 0, d_audio_mixer_right, i);
                } catch (std::exception&) {
                }
            }
        }

        try {
            tb->connect(d_audio_mixer_left, 0, d_audio_sink, 0);
            tb->connect(d_audio_mixer_right, 0, d_audio_sink, 1);
        } catch (std::exception&) {
        }

        d_audio_mixer_connected = true;
        d_mixer_tuner_count = num_tuners;

    } catch (std::exception&) {
    }
}

void TunerManager::connect_tuner_to_mixer(ReceiverChannel_sptr tuner)
{
    rebuild_audio_mixer();
}

void TunerManager::disconnect_tuner_from_mixer(ReceiverChannel_sptr tuner)
{
    rebuild_audio_mixer();
}

void TunerManager::connect_signal_chain()
{
    if (!d_src || !d_top_block || !d_iq_swap_block) {
        return;
    }

    gr::basic_block_sptr current_block = d_src;

    if (d_input_decim >= 2 && d_input_decim_block) {
        d_top_block->connect(current_block, 0, d_input_decim_block, 0);
        current_block = d_input_decim_block;
    }

    d_top_block->connect(current_block, 0, d_iq_swap_block, 0);
    current_block = d_iq_swap_block;

    if (d_dc_cancel && d_dc_corr) {
        d_top_block->connect(current_block, 0, d_dc_corr, 0);
        current_block = d_dc_corr;
    }

    if (d_iq_fft) {
        d_top_block->connect(current_block, 0, d_iq_fft, 0);
    }

    d_shared_iq_source = current_block;
}

void TunerManager::destroy_flowgraph()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_running) {
        stop();
    }

    d_tuners.clear();
    d_tuner_id_to_index.clear();
    d_copy_blocks.clear();
    d_hardware_devices.clear();
    d_device_strings.clear();

    d_top_block.reset();
}

TunerManager::status TunerManager::start()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_running) {
        return STATUS_OK;
    }

    // Need at least one hardware device (primary d_src or legacy d_hardware_devices)
    if (!d_src && d_hardware_devices.empty()) {
        return STATUS_NO_DEVICE;
    }

    try {
        d_top_block->start();
        d_running = true;
        return STATUS_OK;
    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

TunerManager::status TunerManager::stop()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (!d_running) {
        return STATUS_OK;
    }

    try {
        d_top_block->stop();
        d_top_block->wait();
        d_running = false;
        return STATUS_OK;
    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

TunerManager::status TunerManager::set_hardware_mode(hw_mode mode)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_running) {
        return STATUS_ERROR; // Can't change mode while running
    }

    d_hw_mode = mode;
    return STATUS_OK;
}

TunerManager::status TunerManager::add_hardware_device(const std::string& device_string)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_running) {
        return STATUS_ERROR; // Can't add devices while running
    }

    try {
        osmosdr::source::sptr src = osmosdr::source::make(device_string);
        if (!src) {
            return STATUS_NO_DEVICE;
        }

        d_hardware_devices.push_back(src);
        d_device_strings.push_back(device_string);

        // Set default parameters for new device
        int device_index = d_hardware_devices.size() - 1;
        initialize_hardware_device(device_index, device_string);

        return STATUS_OK;
    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

TunerManager::status TunerManager::remove_hardware_device(int device_index)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_running) {
        return STATUS_ERROR;
    }

    if (device_index < 0 || device_index >= static_cast<int>(d_hardware_devices.size())) {
        return STATUS_INVALID_INDEX;
    }

    cleanup_hardware_device(device_index);
    d_hardware_devices.erase(d_hardware_devices.begin() + device_index);
    d_device_strings.erase(d_device_strings.begin() + device_index);

    return STATUS_OK;
}

std::vector<std::string> TunerManager::get_hardware_devices() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_device_strings;
}

int TunerManager::get_hardware_device_count() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return static_cast<int>(d_hardware_devices.size());
}

TunerManager::status TunerManager::set_input_device(const std::string& device_string)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    bool was_running = d_running;

    // Stop flowgraph if running
    if (d_running && d_top_block) {
        d_top_block->stop();
        d_top_block->wait();
        d_running = false;
    }

    // Disconnect tuners from shared source first
    if (d_top_block && d_shared_iq_source) {
        d_top_block->lock();
        for (auto& tuner : d_tuners) {
            if (tuner) {
                try {
                    tuner->disconnect_locked();
                } catch (...) {
                    // May not be connected
                }
            }
        }
        d_top_block->unlock();
    }

    // Disconnect entire signal chain if exists
    if (d_top_block) {
        try {
            // Disconnect FFT (end of chain)
            if (d_iq_fft) {
                if (d_dc_cancel && d_dc_corr) {
                    d_top_block->disconnect(d_dc_corr, 0, d_iq_fft, 0);
                } else if (d_iq_swap_block) {
                    d_top_block->disconnect(d_iq_swap_block, 0, d_iq_fft, 0);
                }
            }
            // Disconnect DC correction
            if (d_dc_cancel && d_dc_corr && d_iq_swap_block) {
                d_top_block->disconnect(d_iq_swap_block, 0, d_dc_corr, 0);
            }
            // Disconnect source chain
            if (d_src) {
                if (d_input_decim >= 2 && d_input_decim_block) {
                    d_top_block->disconnect(d_src, 0, d_input_decim_block, 0);
                    d_top_block->disconnect(d_input_decim_block, 0, d_iq_swap_block, 0);
                } else if (d_iq_swap_block) {
                    d_top_block->disconnect(d_src, 0, d_iq_swap_block, 0);
                }
            }
        } catch (...) {
            // May not be connected yet
        }
    }

    d_input_device = device_string;
    d_src.reset();

    try {
        // Create SDR source
        d_src = osmosdr::source::make(device_string);
        if (!d_src) {
            return STATUS_ERROR;
        }

        // Get the actual sample rate from the device
        if (d_src->get_sample_rate() != 0) {
            d_input_rate = d_src->get_sample_rate();
        }

        // Connect source to the signal chain
        connect_signal_chain();

        // Reconnect all tuners to the new shared IQ source
        if (d_shared_iq_source) {
            d_top_block->lock();
            for (auto& tuner : d_tuners) {
                if (tuner) {
                    try {
                        // 0. Update tuner's DDC rate BEFORE reconnecting
                        //    This ensures DDC is configured for new rate before being connected
                        tuner->set_input_rate_locked(d_input_rate);
                        // 1. Connect source to DDC (sets up placeholder connections)
                        tuner->connect_to_source_locked(d_shared_iq_source, 0);
                        // 2. Reconnect existing backend (replaces placeholders with real audio)
                        tuner->reconnect_backend_locked();
                    } catch (std::exception&) {
                        // Individual tuner reconnection may fail - continue with others
                    }
                }
            }
            // 3. Rebuild audio mixer to reconnect all tuner outputs
            rebuild_audio_mixer_locked(d_top_block);
            d_top_block->unlock();
        }

        // Restart if was running
        if (was_running && d_top_block) {
            d_top_block->start();
            d_running = true;
        }

        return STATUS_OK;

    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

TunerManager::status TunerManager::set_input_rate(double rate)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_input_rate = rate;
    // No shared input decimation - each tuner has full bandwidth access
    // Each tuner's DDC handles its own multi-stage decimation
    d_input_decim = 1;

    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;

    // Remember if we were running so we can restart at the end
    bool was_running = d_running;

    // WORKAROUND: Modifying hier_block2 structures while the flowgraph exists corrupts
    // GNU Radio's connection state. We must stop the flowgraph when changing rates.
    if (tb && d_running) {
        try {
            tb->stop();
            tb->wait();
        } catch (...) {
            // Flowgraph might not be running
        }
        d_running = false;
    }

    // Lock flowgraph for modifications
    if (tb) {
        tb->lock();
    }

    // Disconnect all tuners first (like set_input_device does)
    for (auto& tuner : d_tuners) {
        if (tuner) {
            tuner->disconnect_locked();
        }
    }

    // Set rate on primary source if available
    if (d_src) {
        try {
            d_src->set_sample_rate(rate);
        } catch (const std::exception& e) {
            if (tb) {
                tb->unlock();
            }
            return STATUS_ERROR;
        }
    }

    // Update all hardware devices (for multi-device mode)
    for (auto& device : d_hardware_devices) {
        try {
            device->set_sample_rate(rate);
        } catch (const std::exception& e) {
            if (tb) {
                tb->unlock();
            }
            return STATUS_ERROR;
        }
    }

    // Update and reconnect all tuners (like set_input_device does)
    gr::basic_block_sptr iq_source = d_shared_iq_source ? d_shared_iq_source : d_src;

    for (auto& tuner : d_tuners) {
        if (tuner) {
            try {
                // 0. Update tuner's DDC rate
                tuner->set_input_rate_locked(rate);
                // 1. Connect source to DDC (sets up placeholder connections)
                tuner->connect_to_source_locked(iq_source, 0);
                // 2. Reconnect existing backend (replaces placeholders with real audio)
                tuner->reconnect_backend_locked();
            } catch (std::exception&) {
                // Individual tuner connection may fail - continue with others
            }
        }
    }
    // Rebuild audio mixer to reconnect all tuner outputs
    if (!d_tuners.empty()) {
        rebuild_audio_mixer_locked(tb);
    }

    if (tb) {
        tb->unlock();
    }

    // Only restart if it was running before
    if (tb && was_running) {
        tb->start();
        d_running = true;
    }

    return STATUS_OK;
}

TunerManager::status TunerManager::set_input_decim(unsigned int decim)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_input_decim = decim;
    return STATUS_OK;
}

TunerManager::status TunerManager::set_rf_freq(double freq_hz)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_rf_freq = freq_hz;

    status result = STATUS_OK;

    // Use primary source first
    if (d_src) {
        try {
            d_src->set_center_freq(freq_hz);
        } catch (const std::exception& e) {
            result = STATUS_ERROR;
        }
    }
    // Fallback to legacy hardware devices
    else if (d_hw_mode == HW_MODE_SINGLE_SHARED && !d_hardware_devices.empty()) {
        try {
            d_hardware_devices[0]->set_center_freq(freq_hz);
        } catch (const std::exception& e) {
            result = STATUS_ERROR;
        }
    }

    // Update bypass states for all channels based on new RF frequency
    update_channel_bypass_states_unlocked();

    return result;
}

TunerManager::status TunerManager::set_antenna(const std::string& antenna)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    // Use primary source first
    if (d_src) {
        try {
            d_src->set_antenna(antenna);
            return STATUS_OK;
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    // Fallback to legacy devices
    for (auto& device : d_hardware_devices) {
        try {
            device->set_antenna(antenna);
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    return STATUS_OK;
}

std::string TunerManager::get_antenna() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_src) {
        try {
            return d_src->get_antenna();
        } catch (const std::exception& e) {
            return "";
        }
    }

    if (d_hardware_devices.empty()) {
        return "";
    }

    try {
        return d_hardware_devices[0]->get_antenna();
    } catch (const std::exception& e) {
        return "";
    }
}

std::vector<std::string> TunerManager::get_antennas() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_src) {
        try {
            return d_src->get_antennas();
        } catch (const std::exception& e) {
            return {};
        }
    }

    if (d_hardware_devices.empty()) {
        return {};
    }

    try {
        return d_hardware_devices[0]->get_antennas();
    } catch (const std::exception& e) {
        return {};
    }
}

std::vector<std::string> TunerManager::get_gain_names() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_src) {
        try {
            return d_src->get_gain_names();
        } catch (const std::exception& e) {
            return {};
        }
    }

    if (d_hardware_devices.empty()) {
        return {};
    }

    try {
        return d_hardware_devices[0]->get_gain_names();
    } catch (const std::exception& e) {
        return {};
    }
}

TunerManager::status TunerManager::get_gain_range(const std::string& name, double* start, double* stop, double* step) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_src) {
        try {
            osmosdr::gain_range_t range = d_src->get_gain_range(name);
            if (start) *start = range.start();
            if (stop) *stop = range.stop();
            if (step) *step = range.step();
            return STATUS_OK;
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    if (d_hardware_devices.empty()) {
        return STATUS_NO_DEVICE;
    }

    try {
        osmosdr::gain_range_t range = d_hardware_devices[0]->get_gain_range(name);
        if (start) *start = range.start();
        if (stop) *stop = range.stop();
        if (step) *step = range.step();
        return STATUS_OK;
    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

TunerManager::status TunerManager::set_gain(const std::string& name, double gain)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    // Use primary source first
    if (d_src) {
        try {
            d_src->set_gain(gain, name);
            return STATUS_OK;
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    // Fallback to legacy devices
    for (auto& device : d_hardware_devices) {
        try {
            device->set_gain(gain, name);
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    return STATUS_OK;
}

double TunerManager::get_gain(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_src) {
        try {
            return d_src->get_gain(name);
        } catch (const std::exception& e) {
            return 0.0;
        }
    }

    if (d_hardware_devices.empty()) {
        return 0.0;
    }

    try {
        return d_hardware_devices[0]->get_gain(name);
    } catch (const std::exception& e) {
        return 0.0;
    }
}

TunerManager::status TunerManager::set_auto_gain(bool automatic)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    // Use primary source first
    if (d_src) {
        try {
            d_src->set_gain_mode(automatic);
            return STATUS_OK;
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    // Fallback to legacy devices
    for (auto& device : d_hardware_devices) {
        try {
            device->set_gain_mode(automatic);
        } catch (const std::exception& e) {
            return STATUS_ERROR;
        }
    }

    return STATUS_OK;
}

bool TunerManager::get_auto_gain() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_src) {
        try {
            return d_src->get_gain_mode();
        } catch (const std::exception& e) {
            return false;
        }
    }

    if (d_hardware_devices.empty()) {
        return false;
    }

    try {
        return d_hardware_devices[0]->get_gain_mode();
    } catch (const std::exception& e) {
        return false;
    }
}

void TunerManager::set_iq_swap(bool swapped)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_iq_swap = swapped;
}

void TunerManager::set_dc_cancel(bool enable)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_dc_cancel = enable;
}

void TunerManager::set_iq_balance(bool enable)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_iq_balance = enable;
}

bool TunerManager::can_tune_to_freq(double freq_hz) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_hw_mode == HW_MODE_MULTIPLE_DEDICATED) {
        return true; // Each tuner has its own hardware
    }

    double bandwidth = get_bandwidth();
    double min_freq = d_rf_freq - bandwidth / 2.0;
    double max_freq = d_rf_freq + bandwidth / 2.0;

    return (freq_hz >= min_freq && freq_hz <= max_freq);
}

double TunerManager::get_min_tunable_freq() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_hw_mode == HW_MODE_MULTIPLE_DEDICATED) {
        // For dedicated mode, return hardware limits
        return 0.0; // This would need to be queried from hardware
    }

    double bandwidth = get_bandwidth();
    return d_rf_freq - bandwidth / 2.0;
}

double TunerManager::get_max_tunable_freq() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_hw_mode == HW_MODE_MULTIPLE_DEDICATED) {
        // For dedicated mode, return hardware limits
        return 6000000000.0; // 6 GHz - typical upper limit
    }

    double bandwidth = get_bandwidth();
    return d_rf_freq + bandwidth / 2.0;
}

double TunerManager::get_bandwidth() const
{
    return d_input_rate; // Instantaneous bandwidth
}

void TunerManager::update_channel_bypass_states()
{
    std::lock_guard<std::mutex> lock(d_mutex);
    update_channel_bypass_states_unlocked();
}

// Internal version that assumes d_mutex is already held
void TunerManager::update_channel_bypass_states_unlocked()
{
    if (d_hw_mode == HW_MODE_MULTIPLE_DEDICATED) {
        // Each tuner has its own hardware, no bypass needed
        for (auto& tuner : d_tuners) {
            if (tuner) {
                tuner->set_bypassed(false);
            }
        }
        return;
    }

    double bandwidth = get_bandwidth();
    double min_freq = d_rf_freq - bandwidth / 2.0;
    double max_freq = d_rf_freq + bandwidth / 2.0;

    for (auto& tuner : d_tuners) {
        if (!tuner) continue;

        double channel_freq = d_rf_freq + tuner->get_freq_offset();

        // Check if channel center frequency is within SDR bandwidth
        bool should_bypass = (channel_freq < min_freq || channel_freq > max_freq);
        tuner->set_bypassed(should_bypass);
    }
}

void TunerManager::get_fft_data(std::vector<float>& fftData, unsigned int& fftSize)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_iq_fft) {
        fftSize = d_iq_fft->fft_size();
        fftData.resize(fftSize);
        d_iq_fft->get_fft_data(fftData.data());
    } else {
        fftSize = d_fft_size;
        fftData.resize(fftSize, 0.0f);
    }
}

int TunerManager::get_iq_fft_data(float* fftData)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_iq_fft) {
        d_iq_fft->get_fft_data(fftData);
        return d_iq_fft->fft_size();
    }
    return 0;
}

unsigned int TunerManager::iq_fft_size() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_iq_fft ? d_iq_fft->fft_size() : d_fft_size;
}

void TunerManager::set_iq_fft_size(unsigned int fft_size)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_fft_size = fft_size;
    if (d_iq_fft) {
        d_iq_fft->set_fft_size(fft_size);
    }
}

void TunerManager::set_fft_size(unsigned int fft_size)
{
    set_iq_fft_size(fft_size);
}

unsigned int TunerManager::get_fft_size() const
{
    return iq_fft_size();
}

void TunerManager::set_fft_window_type(int window_type)
{
    // This method kept for compatibility but set_iq_fft_window should be used
    // as rx_fft_c requires both window_type and normalize_energy together
}

void TunerManager::set_fft_normalize_energy(bool normalize)
{
    // This method kept for compatibility but set_iq_fft_window should be used
    // as rx_fft_c requires both window_type and normalize_energy together
}

void TunerManager::set_iq_fft_window(int window_type, bool normalize_energy)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    if (d_iq_fft)
        d_iq_fft->set_window_type(window_type, normalize_energy);
}

TunerManager::SystemConfig TunerManager::save_configuration() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    SystemConfig config;
    config.hardware_mode = d_hw_mode;
    config.hardware_devices = d_device_strings;
    config.input_rate = d_input_rate;
    config.input_decim = d_input_decim;
    config.rf_freq = d_rf_freq;
    config.auto_gain = get_auto_gain();
    config.iq_swap = d_iq_swap;
    config.dc_cancel = d_dc_cancel;
    config.iq_balance = d_iq_balance;
    config.fft_size = d_fft_size;
    config.active_tuner = d_active_tuner;

    // Save tuner configurations
    for (const auto& tuner : d_tuners) {
        TunerConfig tuner_config;
        tuner_config.tuner_id = tuner->get_channel_id();
        tuner_config.name = tuner->get_channel_name();
        tuner_config.center_freq = tuner->get_center_freq();
        tuner_config.demod = tuner->get_demod();
        tuner_config.enabled = tuner->is_enabled();

        config.tuners.push_back(tuner_config);
    }

    return config;
}

TunerManager::status TunerManager::load_configuration(const SystemConfig& config)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    try {
        // Stop if running
        bool was_running = d_running;
        if (was_running) {
            stop();
        }

        // Clear existing configuration
        d_tuners.clear();
        d_tuner_id_to_index.clear();
        d_hardware_devices.clear();
        d_device_strings.clear();

        // Set system configuration
        d_hw_mode = config.hardware_mode;
        d_input_rate = config.input_rate;
        d_input_decim = config.input_decim;
        d_rf_freq = config.rf_freq;
        d_iq_swap = config.iq_swap;
        d_dc_cancel = config.dc_cancel;
        d_iq_balance = config.iq_balance;
        d_fft_size = config.fft_size;
        d_active_tuner = config.active_tuner;

        // Add hardware devices
        for (const auto& device_string : config.hardware_devices) {
            if (add_hardware_device(device_string) != STATUS_OK) {
                return STATUS_ERROR;
            }
        }

        // Add tuners using IChannelManager API
        for (const auto& tuner_config : config.tuners) {
            channel_id new_id = create_channel(ChannelType::MANUAL, ReceiverType::ANALOG_NFM);
            if (new_id < 0) {
                return STATUS_ERROR;
            }

            // Configure the tuner
            ReceiverChannel* tuner = get_channel_impl(new_id);
            if (tuner) {
                tuner->set_center_freq(tuner_config.center_freq);
                tuner->set_demod(tuner_config.demod);
                tuner->set_enabled(tuner_config.enabled);
            }
        }

        // Restart if it was running
        if (was_running) {
            start();
        }

        return STATUS_OK;
    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

// Remote control methods
TunerManager::status TunerManager::rigctl_set_freq(int tuner_index, double freq_hz)
{
    ReceiverChannel* tuner = get_channel_impl(tuner_index);
    if (!tuner) {
        return STATUS_INVALID_INDEX;
    }

    status result = (tuner->set_center_freq(freq_hz) == ReceiverChannel::STATUS_OK) ? STATUS_OK : STATUS_ERROR;
    return result;
}

TunerManager::status TunerManager::rigctl_get_freq(int tuner_index, double& freq_hz)
{
    ReceiverChannel* tuner = get_channel_impl(tuner_index);
    if (!tuner) {
        return STATUS_INVALID_INDEX;
    }

    freq_hz = tuner->get_center_freq();
    return STATUS_OK;
}

TunerManager::status TunerManager::rigctl_set_mode(int tuner_index, const std::string& mode)
{
    ReceiverChannel* tuner = get_channel_impl(tuner_index);
    if (!tuner) {
        return STATUS_INVALID_INDEX;
    }

    // Convert string mode to enum
    ReceiverChannel::rx_demod demod;
    if (mode == "FM") demod = ReceiverChannel::RX_DEMOD_NFM;
    else if (mode == "AM") demod = ReceiverChannel::RX_DEMOD_AM;
    else if (mode == "SSB") demod = ReceiverChannel::RX_DEMOD_SSB;
    else if (mode == "NFM") demod = ReceiverChannel::RX_DEMOD_NFM;
    else if (mode == "WFM") demod = ReceiverChannel::RX_DEMOD_WFM_M;
    else {
        return STATUS_ERROR;
    }

    status result = (tuner->set_demod(demod) == ReceiverChannel::STATUS_OK) ? STATUS_OK : STATUS_ERROR;
    return result;
}

TunerManager::status TunerManager::rigctl_get_mode(int tuner_index, std::string& mode)
{
    ReceiverChannel* tuner = get_channel_impl(tuner_index);
    if (!tuner) {
        return STATUS_INVALID_INDEX;
    }

    // Convert enum to string
    ReceiverChannel::rx_demod demod = tuner->get_demod();
    switch (demod) {
        case ReceiverChannel::RX_DEMOD_NFM: mode = "FM"; break;
        case ReceiverChannel::RX_DEMOD_AM: mode = "AM"; break;
        case ReceiverChannel::RX_DEMOD_SSB: mode = "SSB"; break;
        case ReceiverChannel::RX_DEMOD_WFM_M: mode = "WFM"; break;
        default: mode = "UNKNOWN"; break;
    }

    return STATUS_OK;
}

std::vector<TunerManager::TunerStats> TunerManager::get_tuner_statistics() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    std::vector<TunerStats> stats;
    for (const auto& tuner : d_tuners) {
        TunerStats stat;
        stat.tuner_id = tuner->get_channel_id();
        stat.connected = tuner->is_connected();
        stat.enabled = tuner->is_enabled();
        stat.center_freq = tuner->get_center_freq();
        stat.signal_level = tuner->get_signal_level();
        stat.demod = tuner->get_demod();
        stat.recording_audio = tuner->is_recording_audio();
        stat.sniffer_active = tuner->is_sniffer_active();
        stat.rds_active = tuner->is_rds_decoder_active();

        stats.push_back(stat);
    }

    return stats;
}

// Private methods
void TunerManager::connect_shared_hardware()
{
    if (d_hardware_devices.empty()) {
        return;
    }

    // Connect primary hardware device to correction blocks and FFT
    osmosdr::source::sptr primary_src = d_hardware_devices[0];
    (void)primary_src;  // Used by FFT and correction block connections
}

void TunerManager::connect_dedicated_hardware()
{
    // Each tuner gets its own hardware device - connections handled by tuner setup
}

void TunerManager::update_fft_connections()
{
    // FFT connections are managed by MainWindow/receiver
}

int TunerManager::find_next_tuner_id()
{
    int next_id = d_next_tuner_id++;
    return next_id;
}

TunerManager::status TunerManager::initialize_hardware_device(int device_index, const std::string& device_string)
{
    if (device_index >= static_cast<int>(d_hardware_devices.size())) {
        return STATUS_INVALID_INDEX;
    }

    try {
        osmosdr::source::sptr device = d_hardware_devices[device_index];
        device->set_sample_rate(d_input_rate);
        device->set_center_freq(d_rf_freq);
        device->set_freq_corr(0);

        return STATUS_OK;
    } catch (const std::exception& e) {
        return STATUS_ERROR;
    }
}

void TunerManager::cleanup_hardware_device(int device_index)
{
    // Cleanup any device-specific resources
}

// ═══════════════════════════════════════════════════════════════════════════
// IChannelManager Interface Implementation
// ═══════════════════════════════════════════════════════════════════════════

channel_id TunerManager::create_channel(ChannelType type, ReceiverType backend_type)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    // Check resource limits
    if (d_max_channels > 0 && static_cast<int>(d_tuners.size()) >= d_max_channels) {
        return -1;
    }

    // Get the top block to use
    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;
    if (!tb) {
        return -1;
    }

    // Create the channel
    // Note: ReceiverChannel receives the INPUT rate and calculates its own DDC decimation
    int tuner_id = d_next_tuner_id++;
    double input_rate = get_quad_rate();  // This is the rate BEFORE ReceiverChannel's DDC

    // Pass false for use_internal_audio_sink since TunerManager has shared audio mixer
    ReceiverChannel_sptr tuner = std::make_shared<ReceiverChannel>(
        tuner_id, input_rate, "", tb, false);

    // Create the backend BEFORE connecting (doesn't need flowgraph lock)
    // IMPORTANT: Use the ACTUAL quad_rate from the tuner, not the input rate!
    // ReceiverChannel decimates the input to ~1MHz, so the backend sees that rate.
    double actual_quad_rate = tuner->get_quad_rate();
    auto backend = ReceiverBackendFactory::create(backend_type, actual_quad_rate, d_audio_rate);

    // Add to collection before connecting
    d_tuners.push_back(tuner);
    d_tuner_id_to_index[tuner_id] = d_tuners.size() - 1;

    // Set as active if first tuner
    if (d_active_tuner < 0) {
        d_active_tuner = tuner_id;
    }

    // Batch all flowgraph operations under a single lock for speed
    if (tb && d_shared_iq_source) {
        tb->lock();
        try {
            // Connect to IQ source
            tuner->connect_to_source_locked(d_shared_iq_source, 0);

            // Set backend (connects to DDC output)
            if (backend) {
                tuner->set_backend_locked(std::move(backend));
            }

            // Rebuild audio mixer to include new channel
            rebuild_audio_mixer_locked(tb);

        } catch (std::exception&) {
            // Channel creation failed - tuner may be partially initialized
        }
        tb->unlock();
    } else if (backend) {
        // No flowgraph yet, just store the backend
        tuner->set_backend(std::move(backend));
    }

    // Load per-tuner recording config from settings
    {
        QSettings settings;
        TunerRecordingConfig rec_config;
        rec_config.load(settings, tuner_id);
        d_tuner_recording_configs[tuner_id] = rec_config;
    }

    // Fire callback
    if (d_on_channel_created) {
        d_on_channel_created(tuner_id);
    }

    return tuner_id;
}

void TunerManager::destroy_channel(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return;
    }

    int index = it->second;
    if (index < 0 || index >= static_cast<int>(d_tuners.size())) {
        return;
    }

    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;
    ReceiverChannel_sptr tuner = d_tuners[index];

    // Stop and disconnect
    if (tb && tuner) {
        // Always lock the flowgraph for modifications
        tb->lock();

        // Disconnect all tuners from mixer BEFORE erasing
        if (d_audio_mixer_connected) {
            for (size_t i = 0; i < d_tuners.size(); i++) {
                if (d_tuners[i]) {
                    try {
                        // Left channel
                        tb->disconnect(d_tuners[i]->get_audio_output_left(), 0, d_audio_mixer_left, i);
                        // Right channel
                        tb->disconnect(d_tuners[i]->get_audio_output_right(), 0, d_audio_mixer_right, i);
                    } catch (...) {}
                }
            }
            // Disconnect mixer from sink
            try {
                tb->disconnect(d_audio_mixer_left, 0, d_audio_sink, 0);
                tb->disconnect(d_audio_mixer_right, 0, d_audio_sink, 1);
            } catch (...) {}
            d_audio_mixer_connected = false;
            d_mixer_tuner_count = 0;
        }

        // Use disconnect_locked() to avoid double-lock deadlock!
        // The flowgraph is already locked above.
        tuner->disconnect_locked();

        // Remove from collection BEFORE rebuilding mixer
        d_tuners.erase(d_tuners.begin() + index);
        d_tuner_id_to_index.erase(it);

        // Rebuild index map
        d_tuner_id_to_index.clear();
        for (size_t i = 0; i < d_tuners.size(); i++) {
            if (d_tuners[i]) {
                d_tuner_id_to_index[d_tuners[i]->get_channel_id()] = i;
            }
        }

        // Rebuild mixer with remaining channels INSIDE the lock
        // This ensures the flowgraph is valid when we unlock
        rebuild_audio_mixer_locked(tb);

        tb->unlock();
    } else {
        // No flowgraph - just remove from collection
        d_tuners.erase(d_tuners.begin() + index);
        d_tuner_id_to_index.erase(it);

        // Rebuild index map
        d_tuner_id_to_index.clear();
        for (size_t i = 0; i < d_tuners.size(); i++) {
            if (d_tuners[i]) {
                d_tuner_id_to_index[d_tuners[i]->get_channel_id()] = i;
            }
        }
    }

    // Update active tuner if needed
    if (d_active_tuner == id) {
        d_active_tuner = d_tuners.empty() ? -1 : d_tuners[0]->get_channel_id();
        if (d_on_active_channel_changed && d_active_tuner >= 0) {
            d_on_active_channel_changed(d_active_tuner);
        }
    }

    // Fire callback
    if (d_on_channel_destroyed) {
        d_on_channel_destroyed(id);
    }
}

ReceiverChannel* TunerManager::get_channel_impl(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return nullptr;
    }

    int index = it->second;
    if (index < 0 || index >= static_cast<int>(d_tuners.size())) {
        return nullptr;
    }

    return d_tuners[index].get();
}

IReceiverChannel* TunerManager::get_channel(channel_id id)
{
    // ReceiverChannel now inherits from IReceiverChannel
    return get_channel_impl(id);
}

std::vector<channel_id> TunerManager::get_all_channels() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    std::vector<channel_id> result;
    result.reserve(d_tuners.size());

    for (const auto& tuner : d_tuners) {
        if (tuner) {
            result.push_back(tuner->get_channel_id());
        }
    }

    return result;
}

std::vector<channel_id> TunerManager::get_channels_by_type(ChannelType type) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    std::vector<channel_id> result;

    for (const auto& tuner : d_tuners) {
        if (tuner && tuner->get_channel_type() == type) {
            result.push_back(tuner->get_channel_id());
        }
    }

    return result;
}

int TunerManager::get_max_channels() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_max_channels;
}

int TunerManager::get_active_channel_count() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return static_cast<int>(d_tuners.size());
}

bool TunerManager::can_create_channel() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_max_channels == 0 || static_cast<int>(d_tuners.size()) < d_max_channels;
}

void TunerManager::set_max_channels(int max_channels)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_max_channels = max_channels;
}

gr::basic_block_sptr TunerManager::get_iq_source()
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_shared_iq_source;
}

gr::top_block_sptr TunerManager::get_flowgraph()
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_shared_tb ? d_shared_tb : d_top_block;
}

double TunerManager::get_center_freq() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_rf_freq;
}

double TunerManager::get_sample_rate() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_input_rate;
}

// Note: get_bandwidth() is already declared in the header but with a different
// implementation context (frequency planning). The IChannelManager version
// returns effective bandwidth = sample_rate / decimation.
// We'll use the existing implementation which already does this.

void TunerManager::connect_to_mixer(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return;
    }

    // Channel is automatically connected to mixer when created
    // This method is for re-connecting after manual disconnect
    rebuild_audio_mixer();
}

void TunerManager::disconnect_from_mixer(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return;
    }

    // For now, muting the channel is equivalent to disconnecting from mixer
    // A full disconnect would require rebuilding the mixer without this channel
    int index = it->second;
    if (index >= 0 && index < static_cast<int>(d_tuners.size()) && d_tuners[index]) {
        d_tuners[index]->set_audio_gain(0.0f);
    }
}

bool TunerManager::is_connected_to_mixer(channel_id id) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return false;
    }

    // Channel is connected if mixer is connected and channel exists
    return d_audio_mixer_connected && it->second < static_cast<int>(d_tuners.size());
}

void TunerManager::on_channel_created(std::function<void(channel_id)> callback)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_on_channel_created = callback;
}

void TunerManager::on_channel_destroyed(std::function<void(channel_id)> callback)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_on_channel_destroyed = callback;
}

void TunerManager::on_active_channel_changed(std::function<void(channel_id)> callback)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_on_active_channel_changed = callback;
}

channel_id TunerManager::get_active_channel() const
{
    std::lock_guard<std::mutex> lock(d_mutex);
    return d_active_tuner;
}

void TunerManager::set_active_channel(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    // Verify channel exists
    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return;
    }

    if (d_active_tuner != id) {
        d_active_tuner = id;

        if (d_on_active_channel_changed) {
            d_on_active_channel_changed(id);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Recording Management
// ═══════════════════════════════════════════════════════════════════════════

void TunerManager::setRecordingConfig(const RecordingConfig& config)
{
    {
        std::lock_guard<std::mutex> lock(d_mutex);
        d_recording_config = config;
    }

    // Auto-save to persist the change
    QSettings settings;
    config.save(settings);
}

TunerRecordingConfig TunerManager::getTunerRecordingConfig(channel_id id) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_recording_configs.find(id);
    if (it != d_tuner_recording_configs.end()) {
        return it->second;
    }

    // Return default config
    return TunerRecordingConfig();
}

void TunerManager::setTunerRecordingConfig(channel_id id, const TunerRecordingConfig& config)
{
    {
        std::lock_guard<std::mutex> lock(d_mutex);
        d_tuner_recording_configs[id] = config;
    }

    // Auto-save to persist the change
    QSettings settings;
    config.save(settings, id);
}

void TunerManager::startAllRecording()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_global_recording_active = true;

    for (auto& tuner : d_tuners) {
        if (!tuner) continue;

        channel_id id = tuner->get_channel_id();
        auto config_it = d_tuner_recording_configs.find(id);

        // Only start if tuner has recording enabled (audio or IQ)
        bool should_record = true;  // Default: record audio
        if (config_it != d_tuner_recording_configs.end()) {
            should_record = config_it->second.record_audio || config_it->second.record_iq;
        }

        if (should_record) {

            if (d_on_recording_state_changed) {
                d_on_recording_state_changed(id, true);
            }
        }
    }

    if (d_on_global_recording_state_changed) {
        d_on_global_recording_state_changed(true);
    }

}

void TunerManager::stopAllRecording()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_global_recording_active = false;

    for (auto& tuner : d_tuners) {
        if (!tuner) continue;

        channel_id id = tuner->get_channel_id();

        if (d_on_recording_state_changed) {
            d_on_recording_state_changed(id, false);
        }
    }

    if (d_on_global_recording_state_changed) {
        d_on_global_recording_state_changed(false);
    }

}

void TunerManager::startTunerRecording(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return;
    }

    ReceiverChannel_sptr tuner = d_tuners[it->second];
    if (!tuner) {
        return;
    }

    if (d_on_recording_state_changed) {
        d_on_recording_state_changed(id, true);
    }

}

void TunerManager::stopTunerRecording(channel_id id)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return;
    }

    ReceiverChannel_sptr tuner = d_tuners[it->second];
    if (!tuner) {
        return;
    }

    if (d_on_recording_state_changed) {
        d_on_recording_state_changed(id, false);
    }

}

bool TunerManager::isTunerRecording(channel_id id) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    auto it = d_tuner_id_to_index.find(id);
    if (it == d_tuner_id_to_index.end()) {
        return false;
    }

    ReceiverChannel_sptr tuner = d_tuners[it->second];
    if (!tuner) return false;

    return tuner->is_recording_audio() || tuner->is_recording_iq();
}

bool TunerManager::isAnyRecording() const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    for (auto& tuner : d_tuners) {
        if (!tuner) continue;

        if (tuner->is_recording_audio() || tuner->is_recording_iq()) return true;
    }

    return d_global_recording_active;
}

void TunerManager::saveRecordingSettings(QSettings& settings) const
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_recording_config.save(settings);

    // Save per-tuner configs
    for (const auto& pair : d_tuner_recording_configs) {
        pair.second.save(settings, pair.first);
    }
}

void TunerManager::loadRecordingSettings(QSettings& settings)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    d_recording_config.load(settings);

    // Load per-tuner configs for existing tuners
    for (auto& tuner : d_tuners) {
        if (!tuner) continue;
        channel_id id = tuner->get_channel_id();
        TunerRecordingConfig config;
        config.load(settings, id);
        d_tuner_recording_configs[id] = config;
    }
}


// ═══════════════════════════════════════════════════════════════════
// Global IQ Recording
// ═══════════════════════════════════════════════════════════════════

TunerManager::status TunerManager::start_iq_recording(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_recording_iq) {
        return STATUS_ERROR;
    }

    // Use shared flowgraph if available, otherwise own flowgraph
    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;
    // Use shared IQ source if available, otherwise decim block or raw source
    gr::basic_block_sptr iq_source = d_shared_iq_source ? d_shared_iq_source :
                                     (d_input_decim_block ? std::dynamic_pointer_cast<gr::basic_block>(d_input_decim_block) :
                                      std::dynamic_pointer_cast<gr::basic_block>(d_src));


    if (!tb || !iq_source) {
        return STATUS_ERROR;
    }

    try {
        d_iq_sink = gr::blocks::file_sink::make(sizeof(gr_complex), filename.c_str(), true);
    } catch (std::runtime_error &e) {
        return STATUS_ERROR;
    }

    tb->lock();

    tb->connect(iq_source, 0, d_iq_sink, 0);

    d_recording_iq = true;
    tb->unlock();

    return STATUS_OK;
}

TunerManager::status TunerManager::stop_iq_recording()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (!d_recording_iq) {
        return STATUS_ERROR;
    }

    // Use shared flowgraph if available, otherwise own flowgraph
    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;
    // Use shared IQ source if available, otherwise decim block or raw source
    gr::basic_block_sptr iq_source = d_shared_iq_source ? d_shared_iq_source :
                                     (d_input_decim_block ? std::dynamic_pointer_cast<gr::basic_block>(d_input_decim_block) :
                                      std::dynamic_pointer_cast<gr::basic_block>(d_src));


    if (!tb || !iq_source) {
        return STATUS_ERROR;
    }

    tb->lock();
    d_iq_sink->close();

    tb->disconnect(iq_source, 0, d_iq_sink, 0);

    tb->unlock();
    d_iq_sink.reset();
    d_recording_iq = false;

    return STATUS_OK;
}

TunerManager::status TunerManager::seek_iq_file(long pos)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    gr::top_block_sptr tb = d_shared_tb ? d_shared_tb : d_top_block;

    if (!tb) {
        return STATUS_ERROR;
    }

    if (!d_src) {
        return STATUS_ERROR;
    }

    tb->lock();

    bool success = d_src->seek(pos, SEEK_SET);

    tb->unlock();

    if (success) {
        return STATUS_OK;
    } else {
        return STATUS_ERROR;
    }
}
