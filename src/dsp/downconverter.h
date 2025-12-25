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
#pragma once

#include <gnuradio/blocks/rotator_cc.h>
#include <gnuradio/filter/freq_xlating_fir_filter.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/hier_block2.h>

class downconverter_cc;

#if GNURADIO_VERSION < 0x030900
typedef boost::shared_ptr<downconverter_cc> downconverter_cc_sptr;
#else
typedef std::shared_ptr<downconverter_cc> downconverter_cc_sptr;
#endif
downconverter_cc_sptr make_downconverter_cc(unsigned int decim, double center_freq, double samp_rate);

class downconverter_cc : public gr::hier_block2
{
    friend downconverter_cc_sptr make_downconverter_cc(unsigned int decim, double center_freq, double samp_rate);

public:
    downconverter_cc(unsigned int decim, double center_freq, double samp_rate);
    ~downconverter_cc();
    void set_decim_and_samp_rate(unsigned int decim, double samp_rate);
    void set_center_freq(double center_freq);

    /** Check if changing to new decim/samp_rate would change internal structure.
     *  Used to determine if DDC needs to be disconnected before modification. */
    bool will_structure_change(unsigned int new_decim, double new_samp_rate) const;

private:
    unsigned int d_decim;
    unsigned int d_decim1;  // First stage decimation
    unsigned int d_decim2;  // Second stage decimation (1 if single stage)
    double d_center_freq;
    double d_samp_rate;
    std::vector<float> d_proto_taps;

    void connect_all();
    void update_proto_taps();
    void update_phase_inc();
    void calculate_decimation_stages();

    gr::filter::freq_xlating_fir_filter_ccf::sptr filt;
    gr::filter::fir_filter_ccf::sptr filt2;  // Second stage decimator
    gr::blocks::rotator_cc::sptr rot;
};
