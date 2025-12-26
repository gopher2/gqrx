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
#ifndef TUNER_MANAGER_H
#define TUNER_MANAGER_H

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <mutex>

#include <gnuradio/top_block.h>
#include <gnuradio/blocks/copy.h>
#include <gnuradio/blocks/add_blk.h>
#include <gnuradio/blocks/null_sink.h>
#include <gnuradio/blocks/file_sink.h>
#include <osmosdr/source.h>

#include "receiver_channel.h"
#include "recording_config.h"
#include "interfaces/i_channel_manager.h"
#include "interfaces/i_receiver_backend.h"
#include "portaudio/portaudio_sink.h"
#include "dsp/correct_iq_cc.h"
#include "dsp/rx_fft.h"
#include "dsp/filter/fir_decim.h"


// Forward declarations for GNU Radio blocks
class dc_corr_cc;
class iq_swap_cc;
class fir_decim_cc;
class rx_fft_c;

typedef std::shared_ptr<dc_corr_cc> dc_corr_cc_sptr;
typedef std::shared_ptr<iq_swap_cc> iq_swap_cc_sptr;
typedef std::shared_ptr<fir_decim_cc> fir_decim_cc_sptr;
typedef std::shared_ptr<rx_fft_c> rx_fft_c_sptr;

/**
 * @brief Multi-tuner manager class.
 * @ingroup DSP
 *
 * This class manages multiple receiver channels and handles both
 * single hardware with multiple virtual receivers and multiple
 * hardware devices with dedicated receivers.
 *
 * Implements IChannelManager interface for pluggable backend architecture.
 */
class TunerManager : public IChannelManager
{
public:
    /** Hardware operation modes */
    enum hw_mode {
        HW_MODE_SINGLE_SHARED,     /*!< Single hardware, multiple virtual receivers */
        HW_MODE_MULTIPLE_DEDICATED /*!< Multiple hardware devices, one per tuner */
    };

    /** Status codes */
    enum status {
        STATUS_OK = 0,
        STATUS_ERROR = 1,
        STATUS_NO_DEVICE = 2,
        STATUS_INVALID_INDEX = 3
    };

    TunerManager();
    ~TunerManager();

    /** Set the shared flowgraph from the main receiver.
     *  This allows tuners to connect to the existing SDR signal chain.
     *  @param tb The GNU Radio top block from main receiver
     *  @param iq_source The block providing IQ-corrected signal (after iq_swap)
     */
    void set_shared_flowgraph(gr::top_block_sptr tb, gr::basic_block_sptr iq_source);

    // System control
    status start();
    status stop();
    bool is_running() const { return d_running; }

    // Hardware management
    status set_hardware_mode(hw_mode mode);

    status add_hardware_device(const std::string& device_string);
    status remove_hardware_device(int device_index);
    std::vector<std::string> get_hardware_devices() const;
    int get_hardware_device_count() const;

    // Primary hardware control (for shared mode)
    status set_input_device(const std::string& device_string);
    std::string get_input_device() const { return d_input_device; }

    status set_input_rate(double rate);
    double get_input_rate() const { return d_input_rate; }

    status set_input_decim(unsigned int decim);
    unsigned int get_input_decim() const { return d_input_decim; }

    double get_quad_rate() const { return d_input_rate / d_input_decim; }

    status set_rf_freq(double freq_hz);
    double get_rf_freq() const { return d_rf_freq; }

    status set_antenna(const std::string& antenna);
    std::string get_antenna() const;
    std::vector<std::string> get_antennas() const;

    // Gain control
    std::vector<std::string> get_gain_names() const;
    status get_gain_range(const std::string& name, double* start, double* stop, double* step) const;
    status set_gain(const std::string& name, double gain);
    double get_gain(const std::string& name) const;
    status set_auto_gain(bool automatic);
    bool get_auto_gain() const;

    // IQ corrections (applied to primary hardware)
    void set_iq_swap(bool swapped);
    bool get_iq_swap() const { return d_iq_swap; }

    void set_dc_cancel(bool enable);
    bool get_dc_cancel() const { return d_dc_cancel; }

    void set_iq_balance(bool enable);
    bool get_iq_balance() const { return d_iq_balance; }

    // FFT/Spectrum (shared)
    void get_fft_data(std::vector<float>& fftData, unsigned int& fftSize);
    int get_iq_fft_data(float* fftData);
    unsigned int iq_fft_size() const;
    void set_iq_fft_size(unsigned int fft_size);
    void set_fft_size(unsigned int fft_size);
    unsigned int get_fft_size() const;
    void set_fft_window_type(int window_type);
    void set_fft_normalize_energy(bool normalize);
    void set_iq_fft_window(int window_type, bool normalize_energy);

    // Frequency planning (for shared mode)
    bool can_tune_to_freq(double freq_hz) const;
    double get_min_tunable_freq() const;
    double get_max_tunable_freq() const;
    // get_bandwidth() defined in IChannelManager section below

    // Channel bypass management
    void update_channel_bypass_states();  // Check all channels and update bypass based on frequency range

    // Configuration persistence
    struct TunerConfig {
        int tuner_id;
        std::string name;
        double center_freq;
        ReceiverChannel::rx_demod demod;
        double filter_low;
        double filter_high;
        double filter_tw;
        double filter_offset;
        float audio_gain;
        bool audio_mute;
        std::string audio_device;
        bool agc_on;
        double sql_level;
        bool enabled;
        std::string hardware_device; // For dedicated mode
    };

    struct SystemConfig {
        hw_mode hardware_mode;
        std::vector<std::string> hardware_devices;
        double input_rate;
        unsigned int input_decim;
        double rf_freq;
        std::string antenna;
        std::map<std::string, double> gains;
        bool auto_gain;
        bool iq_swap;
        bool dc_cancel;
        bool iq_balance;
        unsigned int fft_size;
        int active_tuner;
        std::vector<TunerConfig> tuners;
    };

    SystemConfig save_configuration() const;
    status load_configuration(const SystemConfig& config);

    // Remote control support
    status rigctl_set_freq(int tuner_index, double freq_hz);
    status rigctl_get_freq(int tuner_index, double& freq_hz);
    status rigctl_set_mode(int tuner_index, const std::string& mode);
    status rigctl_get_mode(int tuner_index, std::string& mode);

    // ═══════════════════════════════════════════════════════════════════
    // IChannelManager Interface Implementation
    // ═══════════════════════════════════════════════════════════════════

    // Channel Lifecycle
    channel_id create_channel(ChannelType type, ReceiverType backend_type) override;
    void destroy_channel(channel_id id) override;
    ReceiverChannel* get_channel_impl(channel_id id);
    IReceiverChannel* get_channel(channel_id id) override;
    std::vector<channel_id> get_all_channels() const override;
    std::vector<channel_id> get_channels_by_type(ChannelType type) const override;

    // Resource Management
    int get_max_channels() const override;
    int get_active_channel_count() const override;
    bool can_create_channel() const override;
    void set_max_channels(int max_channels) override;

    // IQ Source Access
    gr::basic_block_sptr get_iq_source() override;
    gr::top_block_sptr get_flowgraph() override;
    double get_center_freq() const override;
    double get_sample_rate() const override;
    double get_bandwidth() const override;

    // Audio Mixer (IChannelManager)
    void connect_to_mixer(channel_id id) override;
    void disconnect_from_mixer(channel_id id) override;
    bool is_connected_to_mixer(channel_id id) const override;

    // Callbacks
    void on_channel_created(std::function<void(channel_id)> callback) override;
    void on_channel_destroyed(std::function<void(channel_id)> callback) override;
    void on_active_channel_changed(std::function<void(channel_id)> callback) override;

    // Active Channel (IChannelManager)
    channel_id get_active_channel() const override;
    void set_active_channel(channel_id id) override;

    // ═══════════════════════════════════════════════════════════════════

    // Statistics and monitoring
    struct TunerStats {
        int tuner_id;
        bool connected;
        bool enabled;
        double center_freq;
        float signal_level;
        ReceiverChannel::rx_demod demod;
        bool recording_audio;
        bool recording_iq;
        bool sniffer_active;
        bool rds_active;
    };

    std::vector<TunerStats> get_tuner_statistics() const;

    // ═══════════════════════════════════════════════════════════════════
    // Recording Management
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Get global recording configuration.
     */
    const RecordingConfig& getRecordingConfig() const { return d_recording_config; }

    /**
     * @brief Set global recording configuration.
     */
    void setRecordingConfig(const RecordingConfig& config);

    /**
     * @brief Get per-tuner recording configuration.
     */
    TunerRecordingConfig getTunerRecordingConfig(channel_id id) const;

    /**
     * @brief Set per-tuner recording configuration.
     */
    void setTunerRecordingConfig(channel_id id, const TunerRecordingConfig& config);

    /**
     * @brief Start recording on all tuners that have recording enabled.
     */
    void startAllRecording();

    /**
     * @brief Stop recording on all tuners.
     */
    void stopAllRecording();

    /**
     * @brief Start recording on a specific tuner.
     */
    void startTunerRecording(channel_id id);

    /**
     * @brief Stop recording on a specific tuner.
     */
    void stopTunerRecording(channel_id id);

    /**
     * @brief Check if a tuner is currently recording.
     */
    bool isTunerRecording(channel_id id) const;

    /**
     * @brief Check if any tuner is currently recording.
     */
    bool isAnyRecording() const;

    // Global IQ recording (full bandwidth from SDR source)
    status start_iq_recording(const std::string& filename);
    status stop_iq_recording();
    bool is_recording_iq() const { return d_recording_iq; }

    // IQ file playback seeking
    status seek_iq_file(long pos);

    /**
     * @brief Register callback for recording state changes.
     */
    void onRecordingStateChanged(std::function<void(channel_id, bool)> callback) {
        d_on_recording_state_changed = callback;
    }

    /**
     * @brief Register callback for global recording state changes.
     */
    void onGlobalRecordingStateChanged(std::function<void(bool)> callback) {
        d_on_global_recording_state_changed = callback;
    }

    /**
     * @brief Save recording configuration to QSettings.
     */
    void saveRecordingSettings(QSettings& settings) const;

    /**
     * @brief Load recording configuration from QSettings.
     */
    void loadRecordingSettings(QSettings& settings);

private:
    void create_flowgraph();
    void destroy_flowgraph();
    void connect_signal_chain();
    void connect_shared_hardware();
    void connect_dedicated_hardware();
    void update_fft_connections();
    int find_next_tuner_id();
    void update_channel_bypass_states_unlocked();  // Internal version, assumes d_mutex held

    // Shared audio mixer management
    void create_audio_mixer();
    void connect_tuner_to_mixer(ReceiverChannel_sptr tuner);
    void disconnect_tuner_from_mixer(ReceiverChannel_sptr tuner);
    void rebuild_audio_mixer();
    void rebuild_audio_mixer_locked(gr::top_block_sptr tb);  // Called when flowgraph already locked

    // Hardware management
    status initialize_hardware_device(int device_index, const std::string& device_string);
    void cleanup_hardware_device(int device_index);

    // Thread safety
    mutable std::mutex d_mutex;

private:
    // System state
    bool d_running;
    hw_mode d_hw_mode;

    // Hardware
    std::vector<osmosdr::source::sptr> d_hardware_devices;
    std::vector<std::string> d_device_strings;
    std::string d_input_device;  // Primary input device string
    osmosdr::source::sptr d_src; // Primary SDR source
    double d_input_rate;
    unsigned int d_input_decim;
    double d_rf_freq;
    bool d_iq_swap;
    bool d_dc_cancel;
    bool d_iq_balance;

    // GNU Radio flowgraph (own, used when not sharing)
    gr::top_block_sptr d_top_block;

    // Shared flowgraph from main receiver (used when set)
    gr::top_block_sptr d_shared_tb;
    gr::basic_block_sptr d_shared_iq_source;

    // IQ correction blocks (for shared hardware)
    dc_corr_cc_sptr d_dc_corr;
    iq_swap_cc_sptr d_iq_swap_block;
    fir_decim_cc_sptr d_input_decim_block;

    // FFT for spectrum display (shared)
    rx_fft_c_sptr d_iq_fft;
    unsigned int d_fft_size;

    // Copy blocks for distribution (shared mode)
    std::vector<gr::blocks::copy::sptr> d_copy_blocks;

    // Shared audio mixer - combines all tuner outputs into single audio stream
    gr::blocks::add_ff::sptr d_audio_mixer_left;
    gr::blocks::add_ff::sptr d_audio_mixer_right;
    gr::blocks::null_sink::sptr d_audio_null_left;  // Placeholder when no tuners
    gr::blocks::null_sink::sptr d_audio_null_right;
    portaudio_sink_sptr d_audio_sink;
    int d_audio_rate;
    bool d_audio_mixer_connected;
    size_t d_mixer_tuner_count;  // How many tuners are connected to current mixer

    // Tuner management
    std::vector<ReceiverChannel_sptr> d_tuners;
    std::map<int, int> d_tuner_id_to_index; // tuner_id -> vector index
    int d_next_tuner_id;
    int d_active_tuner;

    // IChannelManager state
    int d_max_channels;  // 0 = unlimited
    std::function<void(channel_id)> d_on_channel_created;
    std::function<void(channel_id)> d_on_channel_destroyed;
    std::function<void(channel_id)> d_on_active_channel_changed;

    // Recording management
    RecordingConfig d_recording_config;
    std::map<channel_id, TunerRecordingConfig> d_tuner_recording_configs;
    bool d_global_recording_active;
    std::function<void(channel_id, bool)> d_on_recording_state_changed;
    std::function<void(bool)> d_on_global_recording_state_changed;

    // Global IQ recording
    bool d_recording_iq;
    gr::blocks::file_sink::sptr d_iq_sink;
};

#endif // TUNER_MANAGER_H
