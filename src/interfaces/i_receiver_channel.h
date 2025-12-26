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
#ifndef I_RECEIVER_CHANNEL_H
#define I_RECEIVER_CHANNEL_H

#include "i_receiver_backend.h"
#include <QString>
#include <memory>

/**
 * @brief Channel type identifier for lifecycle management.
 */
using channel_id = int;

/**
 * @brief Channel type enumeration.
 */
enum class ChannelType {
    MANUAL,              ///< User-created, persistent until user deletes
};

/**
 * @brief Interface for a single receiver channel.
 *
 * A receiver channel is a virtual receiver that can be tuned to a frequency
 * offset from the SDR center frequency. It contains a pluggable backend
 * for demodulation (analog or digital).
 *
 */
class IReceiverChannel {
public:
    virtual ~IReceiverChannel() = default;

    // ─── Identity ───

    /**
     * @brief Get the unique channel ID.
     */
    virtual channel_id get_id() const = 0;

    /**
     * @brief Get the channel type.
     */
    virtual ChannelType get_channel_type() const = 0;

    /**
     * @brief Get the channel name (user-configurable).
     */
    virtual QString get_name() const = 0;

    /**
     * @brief Set the channel name.
     */
    virtual void set_name(const QString& name) = 0;

    // ─── Tuning (Common Front-End) ───

    /**
     * @brief Set the frequency offset from RF center.
     * @param hz Offset in Hz (positive = above center, negative = below)
     */
    virtual void set_freq_offset(double hz) = 0;

    /**
     * @brief Get the current frequency offset.
     */
    virtual double get_freq_offset() const = 0;

    /**
     * @brief Set the filter width.
     * @param low_hz Lower cutoff relative to carrier (negative for LSB)
     * @param high_hz Upper cutoff relative to carrier
     */
    virtual void set_filter_width(double low_hz, double high_hz) = 0;

    /**
     * @brief Get the current signal level.
     * @return Signal level in dBFS
     */
    virtual float get_signal_level() const = 0;

    /**
     * @brief Set the squelch level.
     * @param db Squelch threshold in dB
     */
    virtual void set_squelch_level(double db) = 0;

    /**
     * @brief Get the squelch level.
     */
    virtual double get_squelch_level() const = 0;

    // ─── Backend (Pluggable Demodulator) ───

    /**
     * @brief Set the receiver backend.
     * @param backend The new backend (ownership transferred)
     */
    virtual void set_backend(IReceiverBackend_ptr backend) = 0;

    /**
     * @brief Get the current backend.
     * @return Pointer to backend, or nullptr if none set
     */
    virtual IReceiverBackend* get_backend() = 0;

    /**
     * @brief Get the backend type.
     */
    virtual ReceiverType get_backend_type() const = 0;

    // ─── Audio ───

    /**
     * @brief Set the audio gain.
     * @param gain Linear gain (1.0 = unity)
     */
    virtual void set_audio_gain(float gain) = 0;

    /**
     * @brief Get the audio gain.
     */
    virtual float get_audio_gain() const = 0;

    /**
     * @brief Set the mute state.
     */
    virtual void set_muted(bool muted) = 0;

    /**
     * @brief Check if muted.
     */
    virtual bool is_muted() const = 0;

    // ─── State ───

    /**
     * @brief Enable or disable the channel.
     * @param enabled True to enable DSP processing
     */
    virtual void set_enabled(bool enabled) = 0;

    /**
     * @brief Check if channel is enabled.
     */
    virtual bool is_enabled() const = 0;
};

using IReceiverChannel_ptr = std::shared_ptr<IReceiverChannel>;

#endif // I_RECEIVER_CHANNEL_H
