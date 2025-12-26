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
#ifndef RECEIVER_CHANNEL_H
#define RECEIVER_CHANNEL_H

#include <gnuradio/blocks/file_sink.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/blocks/null_sink.h>
#include <gnuradio/blocks/null_source.h>
#include <gnuradio/blocks/wavfile_sink.h>
#include <gnuradio/top_block.h>
#include <string>
#include <memory>

#include "dsp/downconverter.h"
#include "dsp/rx_filter.h"
#include "dsp/rx_meter.h"
#include "dsp/rx_agc_xx.h"
#include "dsp/iq_recorder.h"
#include "dsp/audio_recorder.h"
#include "recording_config.h"
#include "dsp/rx_demod_fm.h"
#include "dsp/rx_demod_am.h"
#include "dsp/rx_fft.h"
#include "dsp/sniffer_f.h"
#include "dsp/resampler_xx.h"
#include "interfaces/udp_sink_f.h"
#include "interfaces/i_receiver_channel.h"

#ifdef WITH_PULSEAUDIO
#include "pulseaudio/pa_sink.h"
#elif WITH_PORTAUDIO
#include "portaudio/portaudio_sink.h"
#else
#include <gnuradio/audio/sink.h>
#endif

/**
 * @brief Single receiver channel class.
 * @ingroup DSP
 *
 * This class encapsulates a single receiver channel that can be connected
 * to a shared IQ source. Each channel has its own demodulator, audio output,
 * and control settings.
 *
 * Implements IReceiverChannel interface for pluggable backend architecture.
 */
class ReceiverChannel : public IReceiverChannel
{
public:
    /** Flag used to indicate success or failure of an operation */
    enum status {
        STATUS_OK    = 0, /*!< Operation was successful. */
        STATUS_ERROR = 1  /*!< There was an error. */
    };

    /** Available demodulators (same as main receiver) */
    enum rx_demod {
        RX_DEMOD_OFF   = 0,  /*!< No receiver. */
        RX_DEMOD_NONE  = 1,  /*!< No demod. Raw I/Q to audio. */
        RX_DEMOD_AM    = 2,  /*!< Amplitude modulation. */
        RX_DEMOD_NFM   = 3,  /*!< Frequency modulation. */
        RX_DEMOD_WFM_M = 4,  /*!< Frequency modulation (wide, mono). */
        RX_DEMOD_WFM_S = 5,  /*!< Frequency modulation (wide, stereo). */
        RX_DEMOD_WFM_S_OIRT = 6,  /*!< Frequency modulation (wide, stereo oirt). */
        RX_DEMOD_SSB   = 7,  /*!< Single Side Band. */
        RX_DEMOD_AMSYNC = 8  /*!< Amplitude modulation (synchronous demod). */
    };

    /** Filter shape options */
    enum filter_shape {
        FILTER_SHAPE_SOFT = 0,
        FILTER_SHAPE_NORMAL = 1,
        FILTER_SHAPE_SHARP = 2
    };

    ReceiverChannel(int channel_id,
                   double input_rate,
                   const std::string& audio_device = "",
                   gr::top_block_sptr parent_tb = nullptr,
                   bool use_internal_audio_sink = true);
    ~ReceiverChannel() override;

    // Connection management
    void connect_to_source(gr::basic_block_sptr source, int source_port = 0);
    void connect_to_source_locked(gr::basic_block_sptr source, int source_port = 0);  // Assumes flowgraph locked
    void disconnect();
    void disconnect_locked();  // Assumes flowgraph locked - use in destroy_channel
    bool is_connected() const { return d_connected; }

    // ═══════════════════════════════════════════════════════════════════
    // IReceiverChannel Interface Implementation
    // ═══════════════════════════════════════════════════════════════════

    // Identity
    channel_id get_id() const override { return d_channel_id; }
    ChannelType get_channel_type() const override { return d_channel_type; }
    QString get_name() const override { return QString::fromStdString(d_channel_name); }
    void set_name(const QString& name) override { d_channel_name = name.toStdString(); }

    // Tuning
    void set_freq_offset(double hz) override;
    double get_freq_offset() const override;
    void set_filter_width(double low_hz, double high_hz) override;
    float get_signal_level() const override;
    void set_squelch_level(double db) override;
    double get_squelch_level() const override;

    // Backend (Pluggable Demodulator)
    void set_backend(IReceiverBackend_ptr backend) override;
    void set_backend_locked(IReceiverBackend_ptr backend);  // Assumes flowgraph locked
    void reconnect_backend_locked();  // Reconnect existing backend after source reconnect
    void set_input_rate_locked(double rate);  // Update input rate (reconfigures DDC)
    IReceiverBackend* get_backend() override { return d_backend.get(); }
    ReceiverType get_backend_type() const override;

    // Audio (IReceiverChannel)
    void set_audio_gain(float gain) override;
    float get_audio_gain() const override;
    void set_muted(bool muted) override;
    bool is_muted() const override;

    // State (IReceiverChannel)
    void set_enabled(bool enabled) override;
    bool is_enabled() const override { return d_enabled; }

    // Bypass mode - disconnects backend from DDC to save CPU when out of range
    void set_bypassed(bool bypassed);
    bool is_bypassed() const { return d_bypassed; }

    // ═══════════════════════════════════════════════════════════════════
    // Legacy/Extended Methods (not in IReceiverChannel)
    // ═══════════════════════════════════════════════════════════════════

    // Channel identification (legacy)
    int get_channel_id() const { return d_channel_id; }
    void set_channel_name(const std::string& name) { d_channel_name = name; }
    std::string get_channel_name() const { return d_channel_name; }

    // Get the actual quad rate (after DDC decimation)
    double get_quad_rate() const { return d_quad_rate; }

    // Tuner offset from RF center (NOT absolute frequency!)
    // Use: tuner_offset = desired_freq - rf_center_freq
    status set_center_freq(double offset_hz);
    double get_center_freq() const;  // Returns offset, not absolute freq

    // Filter control
    status set_filter(double low, double high, double tw);
    status set_filter_offset(double offset_hz);
    double get_filter_offset() const { return d_filter_offset; }

    // Demodulation
    status set_demod(rx_demod demod);
    rx_demod get_demod() const;

    // Audio control - set_audio_gain/get_audio_gain moved to IReceiverChannel section
    void set_audio_mute(bool mute);  // Legacy - use set_muted() instead
    bool get_audio_mute() const;     // Legacy - use is_muted() instead
    void set_audio_device(const std::string& device);

    // Audio playback control
    status set_af_gain(float gain);
    status start_audio_playback();
    status stop_audio_playback();

    // Signal level - see IReceiverChannel::get_signal_level() const override

    // AGC
    void set_agc_on(bool agc_on);
    bool get_agc_on() const;
    void set_agc_hang(bool use_hang);
    void set_agc_threshold(int threshold);
    void set_agc_slope(int slope);
    void set_agc_decay(int decay_ms);
    void set_agc_manual_gain(int gain);

    // Squelch
    void set_sql_level(double level_db);
    double get_sql_level() const;
    void set_sql_alpha(double alpha);

    // Noise blanker
    void set_nb_on(int nbid, bool on);
    void set_nb_threshold(int nbid, float threshold);

    // FM parameters
    void set_fm_maxdev(float maxdev_hz);
    void set_fm_deemph(double tau);

    // AM parameters
    void set_am_dcr(bool enabled);

    // CW parameters
    void set_cw_offset(double offset);

    // Legacy Recording (WAV only)
    status start_audio_recording(const std::string& filename);
    status stop_audio_recording();
    bool is_recording_audio() const { return d_recording_wav; }

    // =========================================================================
    // New Recording System (IQ and Audio with squelch modes)
    // =========================================================================

    // IQ Recording
    status start_iq_recording(const QString& filepath);
    status stop_iq_recording();
    bool is_recording_iq() const;
    void set_iq_recording_format(IqFileFormat format);
    void set_iq_recording_sample_rate(double rate);
    void set_iq_recording_center_freq(double freq);
    void set_iq_tap_point(IqTapPoint tap_point);
    void set_sigmf_config(const SigMFConfig& config);
    void set_iq_recording_mode(RecordingMode mode);
    void set_iq_split_minutes(int minutes);
    void set_iq_pre_buffer_ms(int ms);
    uint64_t get_iq_samples_recorded() const;
    double get_iq_recording_duration() const;

    // Audio Recording (with squelch modes)
    status start_new_audio_recording(const QString& filepath);
    status stop_new_audio_recording();
    bool is_new_audio_recording() const;
    void set_audio_recording_format(AudioFileFormat format);
    void set_audio_recording_wav_format(WavSampleFormat format);
    void set_audio_recording_mode(RecordingMode mode);
    void set_audio_squelch_config(const SquelchRecordingConfig& config);
    void set_audio_split_minutes(int minutes);
    void notify_squelch_open(bool open);  // Call when squelch state changes
    uint64_t get_audio_samples_recorded() const;
    double get_audio_recording_duration() const;
    int get_audio_call_count() const;

    // UDP streaming
    status start_udp_streaming(const std::string& host, int port, bool stereo);
    status stop_udp_streaming();

    // Data sniffer
    status start_sniffer(unsigned int samplerate, int buffsize);
    status stop_sniffer();
    void get_sniffer_data(float* outbuff, unsigned int& num);
    bool is_sniffer_active() const { return d_sniffer_active; }

    // Audio FFT
    void get_audio_fft_data(std::vector<float>& fftData, unsigned int& fftSize);

    // RDS (for FM channels)
    void get_rds_data(std::string& outbuff, int& num);
    void start_rds_decoder();
    void stop_rds_decoder();
    bool is_rds_decoder_active() const;
    void reset_rds_parser();

    // Enable/disable channel - see IReceiverChannel section for set_enabled/is_enabled

    // Audio output blocks for external mixer connection
    gr::basic_block_sptr get_audio_output_left() const { return d_audio_gain0; }
    gr::basic_block_sptr get_audio_output_right() const { return d_audio_gain1; }
    gr::basic_block_sptr get_audio_null_sink_left() const { return d_audio_null_sink0; }
    gr::basic_block_sptr get_audio_null_sink_right() const { return d_audio_null_sink1; }

    // Control whether to use internal audio sink (default) or external mixer
    void set_use_internal_audio_sink(bool use_internal);

private:
    void create_chain();
    void connect_chain();
    void disconnect_chain();
    void disconnect_impl();  // Common disconnection logic used by disconnect() and disconnect_locked()
    void update_demod_chain();

    /** @brief Convert rx_demod enum to ReceiverType for backend handling */
    static ReceiverType demod_to_receiver_type(rx_demod demod);

    /** @brief Convert ReceiverType to rx_demod enum for state tracking */
    static rx_demod receiver_type_to_demod(ReceiverType type);

private:
    // Channel identification
    int d_channel_id;
    std::string d_channel_name;
    std::string d_audio_device;

    // Channel type and ownership
    ChannelType d_channel_type;

    // State
    bool d_connected;
    bool d_enabled;
    bool d_bypassed;  // True when tuner is out of SDR bandwidth range
    bool d_recording_wav;
    bool d_sniffer_active;
    bool d_use_internal_audio_sink;  // If false, use external mixer

    // Signal parameters
    double d_input_rate;
    double d_quad_rate;
    double d_audio_rate;
    double d_center_freq;
    double d_filter_offset;
    double d_cw_offset;
    double d_sql_level;
    bool d_audio_playing;
    rx_demod d_demod;

    // GNU Radio components
    gr::top_block_sptr d_parent_tb;
    downconverter_cc_sptr d_ddc;
    rx_fft_f_sptr d_audio_fft;

    // Audio chain
    gr::blocks::multiply_const_ff::sptr d_audio_gain0;
    gr::blocks::multiply_const_ff::sptr d_audio_gain1;
    gr::blocks::multiply_const_ff::sptr d_wav_gain0;
    gr::blocks::multiply_const_ff::sptr d_wav_gain1;

    // Recording/streaming
    gr::blocks::wavfile_sink::sptr d_wav_sink;
    udp_sink_f_sptr d_audio_udp_sink;
    sniffer_f_sptr d_sniffer;
    resampler_ff_sptr d_sniffer_rr;

    // New recording system
    iq_recorder_sptr d_iq_recorder;
    audio_recorder_sptr d_audio_recorder;
    bool d_iq_recording_active;
    bool d_new_audio_recording_active;
    IqTapPoint d_iq_tap_point;  // Where to tap IQ for recording
    gr::basic_block_sptr d_iq_recording_source;  // The block we're recording from
    int d_iq_recording_source_port;  // The output port we're recording from

    // Pluggable backend (IReceiverChannel)
    IReceiverBackend_ptr d_backend;
    bool d_audio_muted;

    // Null sink for DDC output (placeholder until backend connected)
    gr::blocks::null_sink::sptr d_ddc_null_sink;

    // Null source for DDC input (used when channel is disabled to save CPU)
    gr::blocks::null_source::sptr d_ddc_null_source;

    // Null sources for audio gain inputs (placeholder until backend provides audio)
    gr::blocks::null_source::sptr d_audio_null_src0;
    gr::blocks::null_source::sptr d_audio_null_src1;

    // Audio output
#ifdef WITH_PULSEAUDIO
    pa_sink_sptr d_audio_snk;

    // Null sinks for disconnected audio
    gr::blocks::null_sink::sptr d_audio_null_sink0;
    gr::blocks::null_sink::sptr d_audio_null_sink1;

    // Null sinks for backend audio when muted
    gr::blocks::null_sink::sptr d_backend_audio_null_sink0;
    gr::blocks::null_sink::sptr d_backend_audio_null_sink1;

    // Source connection tracking
    gr::basic_block_sptr d_source;
    int d_source_port;
#elif WITH_PORTAUDIO
    portaudio_sink_sptr d_audio_snk;

    // Null sinks for disconnected audio
    gr::blocks::null_sink::sptr d_audio_null_sink0;
    gr::blocks::null_sink::sptr d_audio_null_sink1;

    // Null sinks for backend audio when muted
    gr::blocks::null_sink::sptr d_backend_audio_null_sink0;
    gr::blocks::null_sink::sptr d_backend_audio_null_sink1;

    // Source connection tracking
    gr::basic_block_sptr d_source;
    int d_source_port;
#else
    gr::audio::sink::sptr d_audio_snk;

    // Null sinks for disconnected audio
    gr::blocks::null_sink::sptr d_audio_null_sink0;
    gr::blocks::null_sink::sptr d_audio_null_sink1;

    // Null sinks for backend audio when muted
    gr::blocks::null_sink::sptr d_backend_audio_null_sink0;
    gr::blocks::null_sink::sptr d_backend_audio_null_sink1;

    // Source connection tracking
    gr::basic_block_sptr d_source;
    int d_source_port;
#endif
};

typedef std::shared_ptr<ReceiverChannel> ReceiverChannel_sptr;

#endif // RECEIVER_CHANNEL_H
