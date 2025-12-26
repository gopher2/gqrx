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
#include "audio_recorder.h"
#include <gnuradio/io_signature.h>

audio_recorder_sptr make_audio_recorder(int sample_rate)
{
    return gnuradio::get_initial_sptr(new audio_recorder(sample_rate));
}

audio_recorder::audio_recorder(int sample_rate)
    : gr::sync_block("audio_recorder",
                     gr::io_signature::make(1, 1, sizeof(float)),
                     gr::io_signature::make(0, 0, 0))
    , d_format(AudioFileFormat::WAV)
    , d_wav_format(WavSampleFormat::PCM_16)
    , d_stereo(false)
    , d_sample_rate(sample_rate)
    , d_mode(RecordingMode::CONSTANT)
    , d_split_minutes(0)
    , d_frequency(0)
    , d_armed(false)
    , d_state(RecordingState::IDLE)
    , d_squelch_open(false)
    , d_samples_recorded(0)
    , d_file_size(0)
    , d_samples_in_current_file(0)
    , d_call_count(0)
    , d_prebuffer_size(0)
    , d_post_buffer_samples_remaining(0)
    , d_chunk_has_audio(false)
    , d_chunk_duration_samples(0)
{
    d_squelch_config.pre_buffer_ms = 500;
    d_squelch_config.post_buffer_ms = 1000;
    d_squelch_config.min_duration_ms = 200;
    d_squelch_config.chunk_duration_minutes = 5;

    d_prebuffer_size = (d_squelch_config.pre_buffer_ms * d_sample_rate) / 1000;
}

audio_recorder::~audio_recorder()
{
    stop_recording();
}

void audio_recorder::set_stereo(bool stereo)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    if (d_armed) {
        return;
    }
    d_stereo = stereo;

    if (stereo) {
        set_input_signature(gr::io_signature::make(2, 2, sizeof(float)));
    } else {
        set_input_signature(gr::io_signature::make(1, 1, sizeof(float)));
    }
}

void audio_recorder::set_sample_rate(int rate)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_sample_rate = rate;
    d_prebuffer_size = (d_squelch_config.pre_buffer_ms * d_sample_rate) / 1000;
    d_chunk_duration_samples = d_squelch_config.chunk_duration_minutes * 60 * d_sample_rate;
}

void audio_recorder::set_squelch_config(const SquelchRecordingConfig& config)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_squelch_config = config;
    d_prebuffer_size = (config.pre_buffer_ms * d_sample_rate) / 1000;
    d_chunk_duration_samples = config.chunk_duration_minutes * 60 * d_sample_rate;
}

QString audio_recorder::get_extension() const
{
    switch (d_format) {
        case AudioFileFormat::WAV:  return ".wav";
        case AudioFileFormat::FLAC: return ".flac";
        case AudioFileFormat::OGG:  return ".ogg";
    }
    return ".wav";
}

bool audio_recorder::start_recording(const QString& base_filepath)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_armed) {
        return false;
    }

    d_base_filepath = base_filepath;
    d_call_count = 0;
    d_samples_recorded = 0;
    d_armed = true;
    d_recording_start_time = QDateTime::currentDateTime();
    d_prebuffer.clear();

    switch (d_mode) {
        case RecordingMode::CONSTANT:
            if (!open_new_file()) {
                d_armed = false;
                return false;
            }
            d_state = RecordingState::RECORDING;
            break;

        case RecordingMode::SQUELCH_PER_CALL:
            d_state = RecordingState::IDLE;
            break;

        case RecordingMode::SQUELCH_CHUNKS:
            d_chunk_start_time = QDateTime::currentDateTime();
            d_chunk_has_audio = false;
            d_samples_in_current_file = 0;
            d_state = RecordingState::IDLE;
            break;
    }

    return true;
}

void audio_recorder::stop_recording()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (!d_armed) {
        return;
    }

    close_current_file();

    d_armed = false;
    d_state = RecordingState::IDLE;
    d_prebuffer.clear();
}

bool audio_recorder::open_new_file()
{
    if (d_mode == RecordingMode::SQUELCH_PER_CALL || d_split_minutes > 0) {
        d_current_filepath = QString("%1_%2%3")
            .arg(d_base_filepath)
            .arg(d_call_count, 3, 10, QChar('0'))
            .arg(get_extension());
    } else {
        d_current_filepath = d_base_filepath + get_extension();
    }

    d_file.open(d_current_filepath.toStdString(), std::ios::binary | std::ios::trunc);
    if (!d_file.is_open()) {
        return false;
    }

    d_file_start_time = QDateTime::currentDateTime();
    d_samples_in_current_file = 0;
    d_file_size = 0;

    if (d_format == AudioFileFormat::WAV) {
        write_wav_header();
    }

    if (d_on_file_started) {
        d_on_file_started(d_current_filepath);
    }

    return true;
}

void audio_recorder::close_current_file()
{
    if (!d_file.is_open()) {
        return;
    }

    if (d_format == AudioFileFormat::WAV) {
        finalize_wav_header();
    }

    d_file.close();

    double duration = static_cast<double>(d_samples_in_current_file) / d_sample_rate;

    if (d_on_file_completed) {
        d_on_file_completed(d_current_filepath, duration);
    }
}

void audio_recorder::write_wav_header()
{
    int num_channels = d_stereo ? 2 : 1;
    int bits_per_sample;
    uint16_t audio_format;

    switch (d_wav_format) {
        case WavSampleFormat::PCM_16:
            bits_per_sample = 16;
            audio_format = 1;
            break;
        case WavSampleFormat::PCM_32:
            bits_per_sample = 32;
            audio_format = 1;
            break;
        case WavSampleFormat::FLOAT_32:
            bits_per_sample = 32;
            audio_format = 3;
            break;
        default:
            bits_per_sample = 16;
            audio_format = 1;
    }

    int byte_rate = d_sample_rate * num_channels * (bits_per_sample / 8);
    int block_align = num_channels * (bits_per_sample / 8);

    d_file.write("RIFF", 4);
    uint32_t file_size = 0;
    d_file.write(reinterpret_cast<char*>(&file_size), 4);
    d_file.write("WAVE", 4);

    d_file.write("fmt ", 4);
    uint32_t fmt_size = 16;
    d_file.write(reinterpret_cast<char*>(&fmt_size), 4);
    d_file.write(reinterpret_cast<char*>(&audio_format), 2);
    uint16_t num_ch = num_channels;
    d_file.write(reinterpret_cast<char*>(&num_ch), 2);
    uint32_t sr = d_sample_rate;
    d_file.write(reinterpret_cast<char*>(&sr), 4);
    uint32_t br = byte_rate;
    d_file.write(reinterpret_cast<char*>(&br), 4);
    uint16_t ba = block_align;
    d_file.write(reinterpret_cast<char*>(&ba), 2);
    uint16_t bps = bits_per_sample;
    d_file.write(reinterpret_cast<char*>(&bps), 2);

    d_file.write("data", 4);
    uint32_t data_size = 0;
    d_file.write(reinterpret_cast<char*>(&data_size), 4);

    d_file_size = 44;
}

void audio_recorder::finalize_wav_header()
{
    if (!d_file.is_open()) {
        return;
    }

    int bits_per_sample;
    switch (d_wav_format) {
        case WavSampleFormat::PCM_16:
            bits_per_sample = 16;
            break;
        case WavSampleFormat::PCM_32:
        case WavSampleFormat::FLOAT_32:
            bits_per_sample = 32;
            break;
        default:
            bits_per_sample = 16;
    }

    int num_channels = d_stereo ? 2 : 1;
    uint32_t data_size = d_samples_in_current_file * num_channels * (bits_per_sample / 8);
    uint32_t file_size = data_size + 36;

    d_file.seekp(4);
    d_file.write(reinterpret_cast<char*>(&file_size), 4);

    d_file.seekp(40);
    d_file.write(reinterpret_cast<char*>(&data_size), 4);
}

double audio_recorder::get_duration() const
{
    if (d_sample_rate > 0) {
        return static_cast<double>(d_samples_recorded) / d_sample_rate;
    }
    return 0.0;
}

void audio_recorder::set_squelch_open(bool open)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_squelch_open == open) {
        return;
    }

    d_squelch_open = open;

    if (!d_armed || d_mode == RecordingMode::CONSTANT) {
        return;
    }

    if (open) {
        switch (d_state) {
            case RecordingState::IDLE:
                transition_to_recording();
                break;
            case RecordingState::POST_BUFFER:
                d_state = RecordingState::RECORDING;
                break;
            case RecordingState::CHUNK_WAITING:
                if (!d_file.is_open()) {
                    open_new_file();
                    flush_prebuffer_to_file();
                }
                d_state = RecordingState::RECORDING;
                d_chunk_has_audio = true;
                break;
            default:
                break;
        }
    } else {
        if (d_state == RecordingState::RECORDING) {
            transition_to_post_buffer();
        }
    }
}

void audio_recorder::transition_to_recording()
{
    if (d_mode == RecordingMode::SQUELCH_PER_CALL || !d_file.is_open()) {
        if (d_mode == RecordingMode::SQUELCH_PER_CALL) {
            d_call_count++;
        }
        if (!open_new_file()) {
            return;
        }
    }

    flush_prebuffer_to_file();

    d_state = RecordingState::RECORDING;
    d_chunk_has_audio = true;
}

void audio_recorder::transition_to_post_buffer()
{
    d_post_buffer_samples_remaining = (d_squelch_config.post_buffer_ms * d_sample_rate) / 1000;
    d_state = RecordingState::POST_BUFFER;
}

void audio_recorder::transition_to_idle()
{
    if (d_mode == RecordingMode::SQUELCH_PER_CALL) {
        close_current_file();
    }

    d_state = RecordingState::IDLE;
}

void audio_recorder::add_to_prebuffer(const float* data, int count)
{
    for (int i = 0; i < count; i++) {
        if (d_prebuffer.size() >= d_prebuffer_size) {
            d_prebuffer.pop_front();
        }
        d_prebuffer.push_back(data[i]);
    }
}

void audio_recorder::flush_prebuffer_to_file()
{
    if (!d_file.is_open() || d_prebuffer.empty()) {
        return;
    }

    std::vector<float> buffer(d_prebuffer.begin(), d_prebuffer.end());

    switch (d_wav_format) {
        case WavSampleFormat::PCM_16:
            for (float sample : buffer) {
                int16_t val = static_cast<int16_t>(std::clamp(sample * 32767.0f, -32768.0f, 32767.0f));
                d_file.write(reinterpret_cast<char*>(&val), sizeof(int16_t));
                d_file_size += sizeof(int16_t);
            }
            break;

        case WavSampleFormat::PCM_32:
            for (float sample : buffer) {
                int32_t val = static_cast<int32_t>(std::clamp(sample * 2147483647.0f, -2147483648.0f, 2147483647.0f));
                d_file.write(reinterpret_cast<char*>(&val), sizeof(int32_t));
                d_file_size += sizeof(int32_t);
            }
            break;

        case WavSampleFormat::FLOAT_32:
            d_file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(float));
            d_file_size += buffer.size() * sizeof(float);
            break;
    }

    d_samples_in_current_file += buffer.size();
    d_samples_recorded += buffer.size();
}

int audio_recorder::work(int noutput_items,
                         gr_vector_const_void_star& input_items,
                         gr_vector_void_star& /* output_items */)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (!d_armed) {
        return noutput_items;
    }

    const float* in = static_cast<const float*>(input_items[0]);

    switch (d_mode) {
        case RecordingMode::CONSTANT:
            handle_constant_mode(in, noutput_items);
            break;
        case RecordingMode::SQUELCH_PER_CALL:
            handle_squelch_per_call(in, noutput_items);
            break;
        case RecordingMode::SQUELCH_CHUNKS:
            handle_squelch_chunks(in, noutput_items);
            break;
    }

    return noutput_items;
}

void audio_recorder::handle_constant_mode(const float* in, int count)
{
    if (!d_file.is_open()) {
        return;
    }

    switch (d_wav_format) {
        case WavSampleFormat::PCM_16:
            for (int i = 0; i < count; i++) {
                int16_t val = static_cast<int16_t>(std::clamp(in[i] * 32767.0f, -32768.0f, 32767.0f));
                d_file.write(reinterpret_cast<char*>(&val), sizeof(int16_t));
            }
            d_file_size += count * sizeof(int16_t);
            break;

        case WavSampleFormat::PCM_32:
            for (int i = 0; i < count; i++) {
                int32_t val = static_cast<int32_t>(std::clamp(in[i] * 2147483647.0f, -2147483648.0f, 2147483647.0f));
                d_file.write(reinterpret_cast<char*>(&val), sizeof(int32_t));
            }
            d_file_size += count * sizeof(int32_t);
            break;

        case WavSampleFormat::FLOAT_32:
            d_file.write(reinterpret_cast<const char*>(in), count * sizeof(float));
            d_file_size += count * sizeof(float);
            break;
    }

    d_samples_in_current_file += count;
    d_samples_recorded += count;

    if (d_split_minutes > 0 && d_sample_rate > 0) {
        uint64_t split_samples = static_cast<uint64_t>(d_split_minutes) * 60 * static_cast<uint64_t>(d_sample_rate);
        if (d_samples_in_current_file >= split_samples) {
            rotate_file();
        }
    }
}

void audio_recorder::rotate_file()
{
    close_current_file();
    d_call_count++;
    if (!open_new_file()) {
        d_armed = false;
        d_state = RecordingState::IDLE;
    }
}

void audio_recorder::handle_squelch_per_call(const float* in, int count)
{
    switch (d_state) {
        case RecordingState::IDLE:
            add_to_prebuffer(in, count);
            break;

        case RecordingState::RECORDING:
            handle_constant_mode(in, count);
            break;

        case RecordingState::POST_BUFFER:
            handle_constant_mode(in, count);
            d_post_buffer_samples_remaining -= count;
            if (d_post_buffer_samples_remaining <= 0) {
                transition_to_idle();
            }
            break;

        default:
            break;
    }
}

void audio_recorder::handle_squelch_chunks(const float* in, int count)
{
    int chunk_seconds = d_squelch_config.chunk_duration_minutes * 60;
    int elapsed = d_chunk_start_time.secsTo(QDateTime::currentDateTime());

    if (elapsed >= chunk_seconds) {
        if (d_chunk_has_audio && d_file.is_open()) {
            close_current_file();
        } else if (d_file.is_open()) {
            d_file.close();
            QFile::remove(d_current_filepath);
        }

        d_chunk_start_time = QDateTime::currentDateTime();
        d_chunk_has_audio = false;
        d_samples_in_current_file = 0;
        d_state = RecordingState::IDLE;
    }

    switch (d_state) {
        case RecordingState::IDLE:
            add_to_prebuffer(in, count);
            break;

        case RecordingState::RECORDING:
            handle_constant_mode(in, count);
            break;

        case RecordingState::POST_BUFFER:
            handle_constant_mode(in, count);
            d_post_buffer_samples_remaining -= count;
            if (d_post_buffer_samples_remaining <= 0) {
                d_state = RecordingState::IDLE;
            }
            break;

        default:
            break;
    }
}
