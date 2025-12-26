/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2011 Alexandru Csete OZ9AEC.
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
#include <volk/volk.h>
#include <gnuradio/io_signature.h>
#include <dsp/rx_meter.h>


rx_meter_c_sptr make_rx_meter_c(double quad_rate)
{
    return gnuradio::get_initial_sptr(new rx_meter_c(quad_rate));
}

rx_meter_c::rx_meter_c(double quad_rate)
    : gr::sync_block ("rx_meter_c",
          gr::io_signature::make(1, 1, sizeof(gr_complex)),
          gr::io_signature::make(0, 0, 0)),
      d_quadrate(quad_rate),
      d_avgsize(quad_rate * 0.100)
{

    /* allocate circular buffer */
#if GNURADIO_VERSION < 0x031000
    d_writer = gr::make_buffer(d_avgsize + d_quadrate, sizeof(gr_complex));
#else
    d_writer = gr::make_buffer(d_avgsize + d_quadrate, sizeof(gr_complex), 1, 1);
#endif

    d_reader = gr::buffer_add_reader(d_writer, 0);

    d_lasttime = std::chrono::steady_clock::now();
}

rx_meter_c::~rx_meter_c()
{
}


int rx_meter_c::work(int noutput_items,
                     gr_vector_const_void_star &input_items,
                     gr_vector_void_star &output_items)
{
    std::lock_guard<std::mutex> lock(d_mutex);

    const gr_complex *in = (const gr_complex *) input_items[0];
    (void) output_items; // unused

    int items_to_copy = std::min(noutput_items, (int)d_writer->bufsize());

    if (items_to_copy < noutput_items) {
        in += (noutput_items - items_to_copy);
    }

    if (d_writer->space_available() < items_to_copy) {
        int read_pointer_advance = items_to_copy - d_writer->space_available();
        d_reader->update_read_pointer(read_pointer_advance);
    }

    memcpy(d_writer->write_pointer(), in, sizeof(gr_complex) * items_to_copy);
    d_writer->update_write_pointer(items_to_copy);

    return noutput_items;
}


float rx_meter_c::get_level_db()
{
    std::lock_guard<std::mutex> lock(d_mutex);

    unsigned int items_available = d_reader->items_available();

    if (items_available < d_avgsize) {
        return -200.0f;  // No data yet - return very low level, not 0 dB
    }

    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = now - d_lasttime;
    d_lasttime = now;

    unsigned int expected_items = (unsigned int)(diff.count() * d_quadrate * 1.001);
    unsigned int max_advance = items_available - d_avgsize;
    unsigned int read_advance = std::min(expected_items, max_advance);
    d_reader->update_read_pointer(read_advance);

    float sum = 0;
    volk_32f_x2_dot_prod_32f(&sum, (float *)d_reader->read_pointer(), (float *)d_reader->read_pointer(), d_avgsize * 2);

    float power = sum / (float)(d_avgsize);
    float level_db = 10.f * log10f(power + 1.0e-20f);

    return level_db;
}
