/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2020 Clayton Smith VE3IRR.
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
#include <algorithm>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/io_signature.h>

#include "downconverter.h"

#define LPF_CUTOFF 120e3

downconverter_cc_sptr make_downconverter_cc(unsigned int decim, double center_freq, double samp_rate)
{
    return gnuradio::get_initial_sptr(new downconverter_cc(decim, center_freq, samp_rate));
}

downconverter_cc::downconverter_cc(unsigned int decim, double center_freq, double samp_rate)
    : gr::hier_block2("downconverter_cc",
          gr::io_signature::make(1, 1, sizeof(gr_complex)),
          gr::io_signature::make(1, 1, sizeof(gr_complex))),
      d_decim(decim),
      d_decim1(1),
      d_decim2(1),
      d_center_freq(center_freq),
      d_samp_rate(samp_rate)
{
    calculate_decimation_stages();
    connect_all();
    update_proto_taps();
    update_phase_inc();
}

downconverter_cc::~downconverter_cc()
{
}

void downconverter_cc::calculate_decimation_stages()
{
    // For high decimation ratios, use two-stage decimation to keep filter taps manageable
    if (d_decim <= 8) {
        d_decim1 = d_decim;
        d_decim2 = 1;
    } else {
        // Two-stage: first stage <= 8
        d_decim1 = 8;
        while (d_decim1 > 1 && (d_decim % d_decim1) != 0) {
            d_decim1--;
        }
        if (d_decim1 < 2) d_decim1 = 2;

        d_decim2 = d_decim / d_decim1;

        // If second stage too large, find better factorization
        if (d_decim2 > 16) {
            for (unsigned int d1 = 8; d1 >= 2; d1--) {
                if (d_decim % d1 == 0) {
                    unsigned int d2 = d_decim / d1;
                    if (d2 <= 16) {
                        d_decim1 = d1;
                        d_decim2 = d2;
                        break;
                    }
                }
            }
        }
    }
}

void downconverter_cc::set_decim_and_samp_rate(unsigned int decim, double samp_rate)
{
    if (decim == d_decim && std::abs(samp_rate - d_samp_rate) < 1.0) {
        return;
    }

    unsigned int old_decim1 = d_decim1;
    unsigned int old_decim2 = d_decim2;
    bool was_rotator_mode = (d_decim <= 1);

    d_samp_rate = samp_rate;
    d_decim = decim;

    calculate_decimation_stages();

    bool is_rotator_mode = (d_decim <= 1);
    bool structure_changed = (d_decim1 != old_decim1 || d_decim2 != old_decim2 ||
                              was_rotator_mode != is_rotator_mode);

    if (structure_changed) {
        // Disconnect old internal blocks explicitly
        if (was_rotator_mode && rot) {
            disconnect(self(), 0, rot, 0);
            disconnect(rot, 0, self(), 0);
            rot.reset();
        } else if (!was_rotator_mode) {
            if (filt) {
                if (old_decim2 > 1 && filt2) {
                    disconnect(self(), 0, filt, 0);
                    disconnect(filt, 0, filt2, 0);
                    disconnect(filt2, 0, self(), 0);
                    filt2.reset();
                } else {
                    disconnect(self(), 0, filt, 0);
                    disconnect(filt, 0, self(), 0);
                }
                filt.reset();
            }
        }

        connect_all();
    }

    update_proto_taps();
    update_phase_inc();
}

void downconverter_cc::set_center_freq(double center_freq)
{
    d_center_freq = center_freq;
    update_phase_inc();
}

bool downconverter_cc::will_structure_change(unsigned int new_decim, double new_samp_rate) const
{
    if (new_decim == d_decim && std::abs(new_samp_rate - d_samp_rate) < 1.0) {
        return false;
    }

    unsigned int new_decim1, new_decim2;
    if (new_decim <= 8) {
        new_decim1 = new_decim;
        new_decim2 = 1;
    } else {
        new_decim1 = 8;
        while (new_decim1 > 1 && (new_decim % new_decim1) != 0) {
            new_decim1--;
        }
        if (new_decim1 < 2) new_decim1 = 2;
        new_decim2 = new_decim / new_decim1;
        if (new_decim2 > 16) {
            for (unsigned int d1 = 8; d1 >= 2; d1--) {
                if (new_decim % d1 == 0) {
                    unsigned int d2 = new_decim / d1;
                    if (d2 <= 16) {
                        new_decim1 = d1;
                        new_decim2 = d2;
                        break;
                    }
                }
            }
        }
    }

    bool was_rotator_mode = (d_decim <= 1);
    bool is_rotator_mode = (new_decim <= 1);

    return (was_rotator_mode != is_rotator_mode ||
            new_decim1 != d_decim1 || new_decim2 != d_decim2);
}

void downconverter_cc::connect_all()
{
    if (d_decim > 1)
    {
        filt = gr::filter::freq_xlating_fir_filter_ccf::make(d_decim1, {1}, 0.0, d_samp_rate);
        filt->set_min_output_buffer(65536);

        if (d_decim2 > 1) {
            filt2 = gr::filter::fir_filter_ccf::make(d_decim2, {1});
            filt2->set_min_output_buffer(65536);

            connect(self(), 0, filt, 0);
            connect(filt, 0, filt2, 0);
            connect(filt2, 0, self(), 0);
        } else {
            connect(self(), 0, filt, 0);
            connect(filt, 0, self(), 0);
        }
    }
    else
    {
        rot = gr::blocks::rotator_cc::make(0.0);
        connect(self(), 0, rot, 0);
        connect(rot, 0, self(), 0);
    }
}

void downconverter_cc::update_proto_taps()
{
    if (d_decim > 1)
    {
        double stage1_out_rate = d_samp_rate / d_decim1;
        double final_out_rate = d_samp_rate / d_decim;

        double cutoff1 = std::min(LPF_CUTOFF, stage1_out_rate * 0.4);
        double trans_width1 = std::max(stage1_out_rate * 0.1, d_samp_rate / 500.0);

        if (cutoff1 + trans_width1 > stage1_out_rate * 0.45) {
            cutoff1 = stage1_out_rate * 0.35;
        }

        auto taps1 = gr::filter::firdes::low_pass(1.0, d_samp_rate, cutoff1, trans_width1,
#if GNURADIO_VERSION < 0x030900
            gr::filter::firdes::WIN_HAMMING
#else
            gr::fft::window::WIN_HAMMING
#endif
        );

        while (taps1.size() > 500 && trans_width1 < d_samp_rate * 0.2) {
            trans_width1 *= 1.5;
            taps1 = gr::filter::firdes::low_pass(1.0, d_samp_rate, cutoff1, trans_width1,
#if GNURADIO_VERSION < 0x030900
                gr::filter::firdes::WIN_HAMMING
#else
                gr::fft::window::WIN_HAMMING
#endif
            );
        }

        filt->set_taps(taps1);

        if (d_decim2 > 1 && filt2) {
            double cutoff2 = std::min(LPF_CUTOFF, final_out_rate * 0.4);
            double trans_width2 = std::max(final_out_rate * 0.1, stage1_out_rate / 100.0);

            if (cutoff2 + trans_width2 > final_out_rate * 0.45) {
                cutoff2 = final_out_rate * 0.35;
            }

            auto taps2 = gr::filter::firdes::low_pass(1.0, stage1_out_rate, cutoff2, trans_width2,
#if GNURADIO_VERSION < 0x030900
                gr::filter::firdes::WIN_HAMMING
#else
                gr::fft::window::WIN_HAMMING
#endif
            );

            filt2->set_taps(taps2);
        }
    }
}

void downconverter_cc::update_phase_inc()
{
    if (d_decim > 1)
        filt->set_center_freq(d_center_freq);
    else
        rot->set_phase_inc(-2.0 * M_PI * d_center_freq / d_samp_rate);
}
