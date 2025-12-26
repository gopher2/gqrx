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
#include "iq_recorder.h"
#include <gnuradio/io_signature.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

iq_recorder_sptr make_iq_recorder(double sample_rate, double center_freq)
{
    return gnuradio::get_initial_sptr(new iq_recorder(sample_rate, center_freq));
}

iq_recorder::iq_recorder(double sample_rate, double center_freq)
    : gr::sync_block("iq_recorder",
                     gr::io_signature::make(1, 1, sizeof(gr_complex)),
                     gr::io_signature::make(0, 0, 0))
    , d_format(IqFileFormat::SIGMF)
    , d_sample_rate(sample_rate)
    , d_center_freq(center_freq)
    , d_split_minutes(0)
    , d_recording_mode(RecordingMode::CONSTANT)
    , d_pre_buffer_ms(500)
    , d_recording(false)
    , d_armed(false)
    , d_squelch_open(false)
    , d_file_number(0)
    , d_samples_recorded(0)
    , d_total_samples_recorded(0)
    , d_file_size(0)
    , d_pre_buffer_size(0)
{
}

iq_recorder::~iq_recorder()
{
    stop_recording();
}

void iq_recorder::set_sample_rate(double rate)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_sample_rate = rate;
    if (d_armed || d_recording) {
        d_pre_buffer_size = static_cast<size_t>(d_sample_rate * d_pre_buffer_ms / 1000.0);
    }
}

void iq_recorder::set_center_freq(double freq)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    d_center_freq = freq;
}

QString iq_recorder::get_extension() const
{
    switch (d_format) {
        case IqFileFormat::RAW_CF32: return ".cf32";
        case IqFileFormat::RAW_CS16: return ".cs16";
        case IqFileFormat::SIGMF:    return ".sigmf-data";
        case IqFileFormat::WAV_IQ:   return ".wav";
        default:                     return ".raw";
    }
}

bool iq_recorder::start_recording(const QString& filepath)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_recording || d_armed) {
        return false;
    }

    d_base_filepath = filepath;
    d_file_number = 0;
    d_total_samples_recorded = 0;
    d_start_time = QDateTime::currentDateTime();

    d_pre_buffer_size = static_cast<size_t>(d_sample_rate * d_pre_buffer_ms / 1000.0);
    d_pre_buffer.clear();

    if (d_recording_mode != RecordingMode::CONSTANT) {
        d_armed = true;
        d_recording = false;
        return true;
    }

    if (!open_new_file()) {
        return false;
    }

    d_recording = true;
    return true;
}

bool iq_recorder::open_new_file()
{
    QString numbered_path;
    if (d_split_minutes > 0 || d_recording_mode != RecordingMode::CONSTANT) {
        numbered_path = QString("%1_%2").arg(d_base_filepath).arg(d_file_number, 3, 10, QChar('0'));
    } else {
        numbered_path = d_base_filepath;
    }
    d_filepath = numbered_path + get_extension();

    d_file.open(d_filepath.toStdString(), std::ios::binary | std::ios::trunc);
    if (!d_file.is_open()) {
        return false;
    }

    if (d_format == IqFileFormat::WAV_IQ) {
        char header[44] = {0};
        d_file.write(header, 44);
    }

    d_file_start_time = QDateTime::currentDateTime();
    d_samples_recorded = 0;
    d_file_size = 0;
    d_file_number++;

    return true;
}

void iq_recorder::stop_recording()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (!d_recording && !d_armed) {
        return;
    }

    if (d_recording) {
        finalize_current_file();
    }

    d_recording = false;
    d_armed = false;
    d_pre_buffer.clear();
}

void iq_recorder::finalize_current_file()
{
    if (!d_file.is_open()) {
        return;
    }

    if (d_format == IqFileFormat::WAV_IQ) {
        uint32_t data_size = d_samples_recorded * sizeof(gr_complex);
        uint32_t file_size = data_size + 36;

        d_file.seekp(0);

        d_file.write("RIFF", 4);
        d_file.write(reinterpret_cast<char*>(&file_size), 4);
        d_file.write("WAVE", 4);

        d_file.write("fmt ", 4);
        uint32_t fmt_size = 16;
        d_file.write(reinterpret_cast<char*>(&fmt_size), 4);
        uint16_t audio_format = 3;  // IEEE float
        d_file.write(reinterpret_cast<char*>(&audio_format), 2);
        uint16_t num_channels = 2;
        d_file.write(reinterpret_cast<char*>(&num_channels), 2);
        uint32_t sample_rate = static_cast<uint32_t>(d_sample_rate);
        d_file.write(reinterpret_cast<char*>(&sample_rate), 4);
        uint32_t byte_rate = sample_rate * num_channels * sizeof(float);
        d_file.write(reinterpret_cast<char*>(&byte_rate), 4);
        uint16_t block_align = num_channels * sizeof(float);
        d_file.write(reinterpret_cast<char*>(&block_align), 2);
        uint16_t bits_per_sample = 32;
        d_file.write(reinterpret_cast<char*>(&bits_per_sample), 2);

        d_file.write("data", 4);
        d_file.write(reinterpret_cast<char*>(&data_size), 4);
    }

    d_file.close();

    if (d_format == IqFileFormat::SIGMF) {
        write_sigmf_metadata();
    }
}

void iq_recorder::rotate_file()
{
    finalize_current_file();
    if (!open_new_file()) {
        d_recording = false;
    }
}

void iq_recorder::set_squelch_open(bool open)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    if (d_squelch_open == open) {
        return;
    }

    d_squelch_open = open;

    if (d_recording_mode == RecordingMode::CONSTANT) {
        return;
    }

    if (open && d_armed && !d_recording) {
        if (open_new_file()) {
            d_recording = true;
            if (!d_pre_buffer.empty()) {
                std::vector<gr_complex> buffer(d_pre_buffer.begin(), d_pre_buffer.end());
                size_t pre_buffer_bytes = buffer.size() * sizeof(gr_complex);
                d_file.write(reinterpret_cast<const char*>(buffer.data()), pre_buffer_bytes);
                d_file_size += pre_buffer_bytes;
                d_samples_recorded += buffer.size();
                d_total_samples_recorded += buffer.size();
            }
        }
    } else if (!open && d_recording && d_recording_mode == RecordingMode::SQUELCH_PER_CALL) {
        finalize_current_file();
        d_recording = false;
    }
}

void iq_recorder::write_sigmf_metadata()
{
    QString meta_path = d_filepath;
    meta_path.replace(".sigmf-data", ".sigmf-meta");

    QJsonObject root;

    QJsonObject global;
    global["core:datatype"] = "cf32_le";
    global["core:sample_rate"] = d_sample_rate;
    global["core:version"] = "1.0.0";

    if (!d_sigmf_config.author.isEmpty()) {
        global["core:author"] = d_sigmf_config.author;
    }
    if (!d_sigmf_config.description.isEmpty()) {
        global["core:description"] = d_sigmf_config.description;
    }
    if (!d_sigmf_config.license.isEmpty()) {
        global["core:license"] = d_sigmf_config.license;
    }
    if (!d_sigmf_config.hw.isEmpty()) {
        global["core:hw"] = d_sigmf_config.hw;
    }

    global["core:recorder"] = "GQRX Multi-Tuner";
    root["global"] = global;

    QJsonArray captures;
    QJsonObject capture;
    capture["core:sample_start"] = 0;
    capture["core:frequency"] = d_center_freq;
    capture["core:datetime"] = d_start_time.toUTC().toString(Qt::ISODate);
    captures.append(capture);
    root["captures"] = captures;

    root["annotations"] = QJsonArray();

    QFile meta_file(meta_path);
    if (meta_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(root);
        meta_file.write(doc.toJson(QJsonDocument::Indented));
        meta_file.close();
    }
}

double iq_recorder::get_duration() const
{
    if (d_sample_rate > 0) {
        return d_samples_recorded / d_sample_rate;
    }
    return 0.0;
}

int iq_recorder::work(int noutput_items,
                      gr_vector_const_void_star& input_items,
                      gr_vector_void_star& /* output_items */)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    const gr_complex* in = static_cast<const gr_complex*>(input_items[0]);

    // Maintain pre-buffer for squelch modes
    if (d_armed && !d_recording && d_recording_mode != RecordingMode::CONSTANT) {
        for (int i = 0; i < noutput_items; i++) {
            d_pre_buffer.push_back(in[i]);
            if (d_pre_buffer.size() > d_pre_buffer_size) {
                d_pre_buffer.pop_front();
            }
        }
        return noutput_items;
    }

    if (!d_recording || !d_file.is_open()) {
        return noutput_items;
    }

    switch (d_format) {
        case IqFileFormat::RAW_CF32:
        case IqFileFormat::SIGMF:
        case IqFileFormat::WAV_IQ:
            d_file.write(reinterpret_cast<const char*>(in), noutput_items * sizeof(gr_complex));
            d_file_size += noutput_items * sizeof(gr_complex);
            break;

        case IqFileFormat::RAW_CS16:
            for (int i = 0; i < noutput_items; i++) {
                int16_t i_val = static_cast<int16_t>(std::clamp(in[i].real() * 32767.0f, -32768.0f, 32767.0f));
                int16_t q_val = static_cast<int16_t>(std::clamp(in[i].imag() * 32767.0f, -32768.0f, 32767.0f));
                d_file.write(reinterpret_cast<char*>(&i_val), sizeof(int16_t));
                d_file.write(reinterpret_cast<char*>(&q_val), sizeof(int16_t));
            }
            d_file_size += noutput_items * 2 * sizeof(int16_t);
            break;

        default:
            break;
    }

    d_samples_recorded += noutput_items;
    d_total_samples_recorded += noutput_items;

    if (d_split_minutes > 0 && d_sample_rate > 0) {
        uint64_t split_samples = static_cast<uint64_t>(d_split_minutes) * 60 * static_cast<uint64_t>(d_sample_rate);
        if (d_samples_recorded >= split_samples) {
            rotate_file();
        }
    }

    return noutput_items;
}
