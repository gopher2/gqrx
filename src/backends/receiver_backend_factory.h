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
#ifndef RECEIVER_BACKEND_FACTORY_H
#define RECEIVER_BACKEND_FACTORY_H

#include "interfaces/i_receiver_backend.h"
#include <vector>

/**
 * @brief Factory for creating receiver backends.
 *
 * Creates backend instances by type. Compile-time optional features
 * (DMR, P25, etc.) are controlled via CMake options.
 */
class ReceiverBackendFactory {
public:
    /**
     * @brief Create a receiver backend.
     * @param type The receiver type to create
     * @param quad_rate Input sample rate (after DDC)
     * @param audio_rate Output audio sample rate
     * @return New backend instance, or nullptr if type not supported
     */
    static IReceiverBackend_ptr create(
        ReceiverType type,
        double quad_rate,
        double audio_rate
    );

    /**
     * @brief Get list of available receiver types.
     * @return Vector of supported ReceiverType values
     */
    static std::vector<ReceiverType> get_available_types();

    /**
     * @brief Get display name for a receiver type.
     * @param type The receiver type
     * @return Human-readable name
     */
    static QString get_type_name(ReceiverType type);

    /**
     * @brief Get icon name for a receiver type.
     * @param type The receiver type
     * @return Icon resource name
     */
    static QString get_type_icon(ReceiverType type);

    /**
     * @brief Check if a receiver type is analog.
     * @param type The receiver type
     */
    static bool is_analog(ReceiverType type);

    /**
     * @brief Check if a receiver type is available (compiled in).
     * @param type The receiver type
     */
    static bool is_available(ReceiverType type);
};

#endif // RECEIVER_BACKEND_FACTORY_H
