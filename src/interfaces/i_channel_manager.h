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
#ifndef I_CHANNEL_MANAGER_H
#define I_CHANNEL_MANAGER_H

#include "i_receiver_channel.h"
#include "i_receiver_backend.h"
#include <gnuradio/top_block.h>
#include <gnuradio/basic_block.h>
#include <vector>
#include <functional>

/**
 * @brief Interface for channel lifecycle management.
 *
 * The channel manager handles:
 * - Creating and destroying channels
 * - Resource management (max channels based on CPU/bandwidth)
 * - Access to shared IQ source
 * - Audio mixer connections
 *
 * Trunking controllers receive a pointer to this interface to spawn
 * voice channels dynamically.
 */
class IChannelManager {
public:
    virtual ~IChannelManager() = default;

    // ─── Channel Lifecycle ───

    /**
     * @brief Create a new channel.
     * @param type The channel type
     * @param backend_type The receiver backend type
     * @return The new channel's ID, or -1 on failure
     */
    virtual channel_id create_channel(ChannelType type, ReceiverType backend_type) = 0;

    /**
     * @brief Destroy a channel.
     * @param id The channel ID to destroy
     */
    virtual void destroy_channel(channel_id id) = 0;

    /**
     * @brief Get a channel by ID.
     * @param id The channel ID
     * @return Pointer to channel, or nullptr if not found
     */
    virtual IReceiverChannel* get_channel(channel_id id) = 0;

    /**
     * @brief Get all channel IDs.
     */
    virtual std::vector<channel_id> get_all_channels() const = 0;

    /**
     * @brief Get channels by type.
     * @param type The channel type to filter by
     */
    virtual std::vector<channel_id> get_channels_by_type(ChannelType type) const = 0;

    // ─── Resource Management ───

    /**
     * @brief Get the maximum number of channels allowed.
     */
    virtual int get_max_channels() const = 0;

    /**
     * @brief Get the current number of active channels.
     */
    virtual int get_active_channel_count() const = 0;

    /**
     * @brief Check if a new channel can be created.
     * @return True if resources are available
     */
    virtual bool can_create_channel() const = 0;

    /**
     * @brief Set the maximum number of channels.
     * @param max_channels Maximum channels (0 = unlimited)
     */
    virtual void set_max_channels(int max_channels) = 0;

    // ─── IQ Source Access ───

    /**
     * @brief Get the shared IQ source block.
     * @return The block providing IQ samples to all channels
     */
    virtual gr::basic_block_sptr get_iq_source() = 0;

    /**
     * @brief Get the GNU Radio flowgraph.
     */
    virtual gr::top_block_sptr get_flowgraph() = 0;

    /**
     * @brief Get the current SDR center frequency.
     */
    virtual double get_center_freq() const = 0;

    /**
     * @brief Get the input sample rate.
     */
    virtual double get_sample_rate() const = 0;

    /**
     * @brief Get the effective bandwidth (sample_rate / decimation).
     */
    virtual double get_bandwidth() const = 0;

    // ─── Audio Mixer ───

    /**
     * @brief Connect a channel to the audio mixer.
     * @param id The channel ID
     *
     * This routes the channel's audio output to the shared speaker output.
     */
    virtual void connect_to_mixer(channel_id id) = 0;

    /**
     * @brief Disconnect a channel from the audio mixer.
     * @param id The channel ID
     */
    virtual void disconnect_from_mixer(channel_id id) = 0;

    /**
     * @brief Check if a channel is connected to the mixer.
     * @param id The channel ID
     */
    virtual bool is_connected_to_mixer(channel_id id) const = 0;

    // ─── Callbacks ───

    /**
     * @brief Set callback for channel creation.
     */
    virtual void on_channel_created(std::function<void(channel_id)> callback) = 0;

    /**
     * @brief Set callback for channel destruction.
     */
    virtual void on_channel_destroyed(std::function<void(channel_id)> callback) = 0;

    /**
     * @brief Set callback for active channel change.
     */
    virtual void on_active_channel_changed(std::function<void(channel_id)> callback) = 0;

    // ─── Active Channel ───

    /**
     * @brief Get the currently active (selected) channel.
     * @return Active channel ID, or -1 if none
     */
    virtual channel_id get_active_channel() const = 0;

    /**
     * @brief Set the active (selected) channel.
     * @param id The channel ID to make active
     */
    virtual void set_active_channel(channel_id id) = 0;
};

#endif // I_CHANNEL_MANAGER_H
