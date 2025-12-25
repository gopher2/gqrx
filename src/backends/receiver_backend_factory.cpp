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
#include "receiver_backend_factory.h"
#include "analog/analog_nbrx_backend.h"
#include "analog/analog_wfm_backend.h"

IReceiverBackend_ptr ReceiverBackendFactory::create(
    ReceiverType type,
    double quad_rate,
    double audio_rate)
{
    switch (type) {
        case ReceiverType::ANALOG_OFF:
        case ReceiverType::ANALOG_RAW:
        case ReceiverType::ANALOG_AM:
        case ReceiverType::ANALOG_NFM:
        case ReceiverType::ANALOG_SSB:
        case ReceiverType::ANALOG_USB:
        case ReceiverType::ANALOG_LSB:
        case ReceiverType::ANALOG_CW_L:
        case ReceiverType::ANALOG_CW_U:
        case ReceiverType::ANALOG_AMSYNC:
            return std::make_unique<AnalogNbrxBackend>(
                static_cast<float>(quad_rate),
                static_cast<float>(audio_rate),
                type
            );

        case ReceiverType::ANALOG_WFM_MONO:
        case ReceiverType::ANALOG_WFM_STEREO:
        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
            return std::make_unique<AnalogWfmBackend>(
                static_cast<float>(quad_rate),
                static_cast<float>(audio_rate),
                type
            );

        default:
            return nullptr;
    }
}

std::vector<ReceiverType> ReceiverBackendFactory::get_available_types()
{
    std::vector<ReceiverType> types;

    types.push_back(ReceiverType::ANALOG_OFF);
    types.push_back(ReceiverType::ANALOG_RAW);
    types.push_back(ReceiverType::ANALOG_AM);
    types.push_back(ReceiverType::ANALOG_NFM);
    types.push_back(ReceiverType::ANALOG_SSB);
    types.push_back(ReceiverType::ANALOG_USB);
    types.push_back(ReceiverType::ANALOG_LSB);
    types.push_back(ReceiverType::ANALOG_CW_L);
    types.push_back(ReceiverType::ANALOG_CW_U);
    types.push_back(ReceiverType::ANALOG_AMSYNC);

    types.push_back(ReceiverType::ANALOG_WFM_MONO);
    types.push_back(ReceiverType::ANALOG_WFM_STEREO);
    types.push_back(ReceiverType::ANALOG_WFM_STEREO_OIRT);

    return types;
}

QString ReceiverBackendFactory::get_type_name(ReceiverType type)
{
    switch (type) {
        case ReceiverType::ANALOG_OFF:             return "Off";
        case ReceiverType::ANALOG_RAW:             return "Raw I/Q";
        case ReceiverType::ANALOG_AM:              return "AM";
        case ReceiverType::ANALOG_NFM:             return "Narrow FM";
        case ReceiverType::ANALOG_WFM_MONO:        return "Wide FM (Mono)";
        case ReceiverType::ANALOG_WFM_STEREO:      return "Wide FM (Stereo)";
        case ReceiverType::ANALOG_WFM_STEREO_OIRT: return "Wide FM (OIRT)";
        case ReceiverType::ANALOG_SSB:             return "SSB";
        case ReceiverType::ANALOG_USB:             return "USB";
        case ReceiverType::ANALOG_LSB:             return "LSB";
        case ReceiverType::ANALOG_CW_L:            return "CW-L";
        case ReceiverType::ANALOG_CW_U:            return "CW-U";
        case ReceiverType::ANALOG_AMSYNC:          return "AM-Sync";
        default:                                   return "Unknown";
    }
}

QString ReceiverBackendFactory::get_type_icon(ReceiverType)
{
    return ":/icons/analog.png";
}

bool ReceiverBackendFactory::is_analog(ReceiverType type)
{
    switch (type) {
        case ReceiverType::ANALOG_OFF:
        case ReceiverType::ANALOG_RAW:
        case ReceiverType::ANALOG_AM:
        case ReceiverType::ANALOG_NFM:
        case ReceiverType::ANALOG_WFM_MONO:
        case ReceiverType::ANALOG_WFM_STEREO:
        case ReceiverType::ANALOG_WFM_STEREO_OIRT:
        case ReceiverType::ANALOG_SSB:
        case ReceiverType::ANALOG_USB:
        case ReceiverType::ANALOG_LSB:
        case ReceiverType::ANALOG_CW_L:
        case ReceiverType::ANALOG_CW_U:
        case ReceiverType::ANALOG_AMSYNC:
            return true;
        default:
            return false;
    }
}

bool ReceiverBackendFactory::is_available(ReceiverType type)
{
    auto available = get_available_types();
    for (auto t : available) {
        if (t == type) return true;
    }
    return false;
}
