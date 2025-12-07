/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2011-2013 Alexandru Csete OZ9AEC.
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
#include <math.h>
#include <volk/volk.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/gr_complex.h>
#include <gnuradio/fft/fft.h>
#include "dsp/rx_fft.h"
#include <algorithm>


rx_fft_c_sptr make_rx_fft_c (unsigned int fftsize, double quad_rate,
                             int wintype, bool normalize_energy)
{
    return gnuradio::get_initial_sptr(new rx_fft_c (fftsize, quad_rate,
                                                   wintype, normalize_energy));
}

/*! \brief Create receiver FFT object.
 *  \param fftsize The FFT size.
 *  \param wintype The window type (see gr::fft::window::win_type).
 *  \param normalize_energy Normalize window for energy instead of amplitude.
 *
 */
rx_fft_c::rx_fft_c(unsigned int fftsize, double quad_rate, int wintype, bool normalize_energy)
    : gr::sync_block ("rx_fft_c",
          gr::io_signature::make(1, 1, sizeof(gr_complex)),
          gr::io_signature::make(0, 0, 0)),
      d_fftsize(fftsize),
      d_startup_samples(0),
      d_quadrate(quad_rate),
      d_wintype(-1),
      d_normalize_energy(false)
{

    /* create FFT object */
#if GNURADIO_VERSION < 0x030900
    d_fft = new gr::fft::fft_complex(d_fftsize, true);
#else
    d_fft = new gr::fft::fft_complex_fwd(d_fftsize);
#endif

    /* allocate circular buffer */
#if GNURADIO_VERSION < 0x031000
    d_writer = gr::make_buffer(MAX_FFT_SIZE * 2, sizeof(gr_complex));
#else
    d_writer = gr::make_buffer(MAX_FFT_SIZE * 2, sizeof(gr_complex), 1, 1);
#endif
    d_reader = gr::buffer_add_reader(d_writer, 0);

    memset(d_writer->write_pointer(), 0, sizeof(gr_complex) * MAX_FFT_SIZE);
    d_writer->update_write_pointer(MAX_FFT_SIZE);

    /* create FFT window */
    set_window_type(wintype, normalize_energy);

    d_lasttime = std::chrono::steady_clock::now();
}

rx_fft_c::~rx_fft_c()
{
    delete d_fft;
}

/*! \brief Receiver FFT work method.
 *  \param noutput_items
 *  \param input_items
 *  \param output_items
 *
 * In normal mode: throws incoming samples into circular buffer.
 * In spectrogram mode: computes FFT for each block and accumulates into spectrogram.
 */
int rx_fft_c::work(int noutput_items,
                   gr_vector_const_void_star &input_items,
                   gr_vector_void_star &output_items)
{
    const gr_complex *in = (const gr_complex*)input_items[0];
    (void) output_items;

    if (d_spectrogram_mode && d_spectrogram_completed_rows < d_spectrogram_rows)
    {
        /* Spectrogram mode: buffer samples and compute multiple FFTs per row (MAX) */
        std::lock_guard<std::mutex> lock(d_in_mutex);

        /* Add incoming samples to buffer */
        d_spectrogram_sample_buffer.insert(d_spectrogram_sample_buffer.end(),
                                           in, in + noutput_items);

        unsigned int rows_before = d_spectrogram_completed_rows;

        /* Process FFTs from buffer */
        while (d_spectrogram_sample_buffer.size() >= d_fftsize &&
               d_spectrogram_completed_rows < d_spectrogram_rows)
        {
            /* Copy samples to FFT input buffer with window */
            gr_complex *dst = d_fft->get_inbuf();
            if (d_window.size())
            {
                volk_32fc_32f_multiply_32fc(dst, d_spectrogram_sample_buffer.data(),
                                            &d_window[0], d_fftsize);
            }
            else
            {
                memcpy(dst, d_spectrogram_sample_buffer.data(),
                       sizeof(gr_complex) * d_fftsize);
            }

            /* Consume fftsize samples (no overlap for speed) */
            d_spectrogram_sample_buffer.erase(d_spectrogram_sample_buffer.begin(),
                                              d_spectrogram_sample_buffer.begin() + d_fftsize);
            d_spectrogram_row_samples += d_fftsize;

            /* Compute FFT */
            d_fft->execute();
            const std::complex<float> *fftOut = d_fft->get_outbuf();
            d_total_ffts_computed++;

            /* Convert to magnitude^2 and MAX into row accumulator */
            for (unsigned int i = 0; i < d_fftsize/2; ++i)
            {
                float val = static_cast<float>(std::norm(fftOut[i + d_fftsize/2]));
                if (val > d_spectrogram_row_max[i])
                    d_spectrogram_row_max[i] = val;
                /* Also update global max-hold */
                if (val > d_global_maxhold[i])
                    d_global_maxhold[i] = val;
            }
            for (unsigned int i = d_fftsize/2; i < d_fftsize; ++i)
            {
                float val = static_cast<float>(std::norm(fftOut[i - d_fftsize/2]));
                if (val > d_spectrogram_row_max[i])
                    d_spectrogram_row_max[i] = val;
                if (val > d_global_maxhold[i])
                    d_global_maxhold[i] = val;
            }

            /* Check if we've consumed enough samples for this row */
            if (d_spectrogram_row_samples >= d_spectrogram_step_size)
            {
                /* Finalize row: copy MAX accumulator to row data */
                std::copy(d_spectrogram_row_max.begin(), d_spectrogram_row_max.end(),
                          d_spectrogram_data[d_spectrogram_completed_rows].begin());

                /* Reset for next row */
                std::fill(d_spectrogram_row_max.begin(), d_spectrogram_row_max.end(), 0.0f);
                d_spectrogram_row_samples = 0;
                d_spectrogram_completed_rows++;
            }
        }

        /* Debug output when rows complete */
        if (d_spectrogram_completed_rows > rows_before)
        {
            static unsigned int last_reported = 0;
            if (d_spectrogram_completed_rows - last_reported >= 100 ||
                d_spectrogram_completed_rows >= d_spectrogram_rows)
            {
                std::cout << "DEBUG rx_fft: Spectrogram progress "
                          << d_spectrogram_completed_rows << "/" << d_spectrogram_rows
                          << " rows, " << d_total_ffts_computed << " FFTs"
                          << " buffer=" << d_spectrogram_sample_buffer.size() << std::endl;
                last_reported = d_spectrogram_completed_rows;
            }
        }

        return noutput_items;
    }

    /* Normal mode: just throw new samples into the buffer */
    int items_to_copy = std::min(noutput_items, (int)d_writer->bufsize());
    if (items_to_copy < noutput_items)
        in += (noutput_items - items_to_copy);

    {
        std::lock_guard<std::mutex> lock(d_in_mutex);

        if (d_writer->space_available() < items_to_copy)
            d_reader->update_read_pointer(items_to_copy - d_writer->space_available());
        memcpy(d_writer->write_pointer(), in, sizeof(gr_complex) * items_to_copy);
        d_writer->update_write_pointer(items_to_copy);

        if (d_startup_samples < d_writer->bufsize())
            d_startup_samples += items_to_copy;
    }

    return noutput_items;
}

/*! \brief Get FFT data.
 *  \param fftPoints Buffer to copy FFT data
 *  \param fftSize Current FFT size (output).
 *
 * In spectrogram mode, returns the global max-hold data instead of computing
 * a new FFT from the circular buffer.
 */
int rx_fft_c::get_fft_data(float* fftPoints)
{
    /* In spectrogram mode, return global max-hold */
    if (d_spectrogram_mode && d_global_maxhold.size() == d_fftsize)
    {
        std::lock_guard<std::mutex> lock(d_in_mutex);
        memcpy(fftPoints, d_global_maxhold.data(), sizeof(float) * d_fftsize);
        return 0;
    }

    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = now - d_lasttime;
    diff = std::min(diff, std::chrono::duration<double>(d_writer->bufsize() / d_quadrate));
    d_lasttime = now;

    {
        std::lock_guard<std::mutex> lock(d_in_mutex);

        d_reader->update_read_pointer(std::min((int)(diff.count() * d_quadrate * 1.001), d_reader->items_available() - MAX_FFT_SIZE));

        if (d_startup_samples < d_reader->items_available() - (MAX_FFT_SIZE - d_fftsize))
            return -1;

        apply_window(d_fftsize);
    }

    /* compute FFT */
    d_fft->execute();

    const std::complex<float> *fftOut = d_fft->get_outbuf();

    // Shifted mag^2(FFT)
    for (unsigned int i = 0; i < d_fftsize/2; ++i)
        fftPoints[i] = static_cast<float>(std::norm(fftOut[i + d_fftsize/2]));
    for (unsigned int i = d_fftsize/2; i < d_fftsize; ++i)
        fftPoints[i] = static_cast<float>(std::norm(fftOut[i - d_fftsize/2]));

    return 0;
}

/*! \brief Compute FFT on the available input data.
 *  \param data_in The data to compute FFT on.
 *  \param size The size of data_in.
 *
 * Note that this function does not lock the mutex since the caller, get_fft_data()
 * has already locked it.
 */
void rx_fft_c::apply_window(unsigned int size)
{
    /* apply window, if any */
    gr_complex * p = (gr_complex *)d_reader->read_pointer();
    p += (MAX_FFT_SIZE - d_fftsize);
    if (d_window.size())
    {
        gr_complex *dst = d_fft->get_inbuf();
        volk_32fc_32f_multiply_32fc(dst, p, &d_window[0], size);
    }
    else
    {
        memcpy(d_fft->get_inbuf(), p, sizeof(gr_complex)*size);
    }
}

/*! \brief Set new FFT size. */
void rx_fft_c::set_fft_size(unsigned int fftsize)
{
    if (fftsize != d_fftsize)
    {
        d_fftsize = fftsize;

        /* reset FFT object (also reset FFTW plan) */
        delete d_fft;
#if GNURADIO_VERSION < 0x030900
        d_fft = new gr::fft::fft_complex(d_fftsize, true);
#else
        d_fft = new gr::fft::fft_complex_fwd(d_fftsize);
#endif

        update_window();
    }
}

/*! \brief Set new quadrature rate. */
void rx_fft_c::set_quad_rate(double quad_rate)
{
    d_quadrate = quad_rate;
}

/*! \brief Set new window type. */
void rx_fft_c::set_window_type(int wintype, bool normalize_energy)
{
    if ((wintype < gr::fft::window::WIN_HAMMING) || wintype > gr::fft::window::WIN_FLATTOP)
    {
        wintype = gr::fft::window::WIN_HAMMING;
    }

    if (wintype != d_wintype || normalize_energy != d_normalize_energy)
    {
        d_wintype = wintype;
        d_normalize_energy = normalize_energy;
        update_window();
    }
}

void rx_fft_c::update_window()
{
    float factor;

    d_window.clear();
    d_window = gr::fft::window::build((gr::fft::window::win_type)d_wintype, d_fftsize, 6.76);
    d_window.resize(d_fftsize);

    // Normalize using average of window for amplitude, or RMS for energy
    float sum = 0.0;
    for (auto v : d_window)
        sum += d_normalize_energy ? v * v : v;
    factor = sum / (float)d_fftsize;
    if (d_normalize_energy)
        factor = std::sqrt(factor);
    volk_32f_s32f_normalize(d_window.data(), factor, d_fftsize);
}

/*! \brief Enable or disable spectrogram mode for fast playback.
 *  \param enabled Whether to enable spectrogram mode
 *  \param rows Number of rows in the spectrogram (waterfall height)
 *  \param time_span_sec Total time span of the spectrogram in seconds
 */
void rx_fft_c::set_spectrogram_mode(bool enabled, unsigned int rows, double time_span_sec)
{
    std::lock_guard<std::mutex> lock(d_in_mutex);

    if (enabled && rows > 0 && time_span_sec > 0 && d_quadrate > 0)
    {
        d_spectrogram_mode = true;
        d_spectrogram_rows = rows;

        /* Calculate step size - how many samples to advance between FFTs */
        /* This determines the overlap: step_size < fftsize means overlap */
        double total_samples = time_span_sec * d_quadrate;
        d_spectrogram_step_size = std::max(1u, (unsigned int)(total_samples / rows));

        unsigned int ffts_per_row = d_spectrogram_step_size / d_fftsize;
        std::cout << "DEBUG rx_fft: Spectrogram mode enabled - rows=" << rows
                  << " time_span=" << time_span_sec << "s"
                  << " d_quadrate=" << d_quadrate
                  << " d_fftsize=" << d_fftsize
                  << " step_size=" << d_spectrogram_step_size
                  << " ffts_per_row=" << ffts_per_row << std::endl;

        /* Allocate buffers */
        d_spectrogram_data.resize(rows);
        for (auto& row : d_spectrogram_data)
            row.resize(d_fftsize, 0.0f);

        d_global_maxhold.resize(d_fftsize, 0.0f);
        d_spectrogram_row_max.resize(d_fftsize, 0.0f);  // MAX accumulator for current row
        d_spectrogram_sample_buffer.clear();
        d_spectrogram_sample_buffer.reserve(d_fftsize * 2);  // Reserve space for buffering

        /* Reset counters */
        d_spectrogram_completed_rows = 0;
        d_spectrogram_row_samples = 0;
        d_total_ffts_computed = 0;
    }
    else
    {
        std::cout << "DEBUG rx_fft: Spectrogram mode disabled" << std::endl;
        d_spectrogram_mode = false;
        /* Reset startup samples so normal FFT waits for new data */
        d_startup_samples = 0;
    }
}

/*! \brief Reset spectrogram buffers without changing mode. */
void rx_fft_c::reset_spectrogram()
{
    std::lock_guard<std::mutex> lock(d_in_mutex);

    for (auto& row : d_spectrogram_data)
        std::fill(row.begin(), row.end(), 0.0f);

    std::fill(d_global_maxhold.begin(), d_global_maxhold.end(), 0.0f);
    d_spectrogram_sample_buffer.clear();

    d_spectrogram_completed_rows = 0;
    d_total_ffts_computed = 0;
}

/*! \brief Finalize spectrogram by processing all remaining buffered samples.
 *  Call this when the file source reaches EOF to ensure all data is processed.
 */
void rx_fft_c::finalize_spectrogram()
{
    if (!d_spectrogram_mode || d_spectrogram_completed_rows >= d_spectrogram_rows)
        return;

    std::lock_guard<std::mutex> lock(d_in_mutex);

    unsigned int ffts_processed = 0;

    /* Process any remaining complete FFTs in the buffer */
    while (d_spectrogram_sample_buffer.size() >= d_fftsize &&
           d_spectrogram_completed_rows < d_spectrogram_rows)
    {
        /* Copy samples to FFT input buffer with window */
        gr_complex *dst = d_fft->get_inbuf();
        if (d_window.size())
        {
            volk_32fc_32f_multiply_32fc(dst, d_spectrogram_sample_buffer.data(),
                                        &d_window[0], d_fftsize);
        }
        else
        {
            memcpy(dst, d_spectrogram_sample_buffer.data(),
                   sizeof(gr_complex) * d_fftsize);
        }

        /* Consume fftsize samples */
        d_spectrogram_sample_buffer.erase(d_spectrogram_sample_buffer.begin(),
                                          d_spectrogram_sample_buffer.begin() + d_fftsize);
        d_spectrogram_row_samples += d_fftsize;

        /* Compute FFT */
        d_fft->execute();
        const std::complex<float> *fftOut = d_fft->get_outbuf();
        d_total_ffts_computed++;
        ffts_processed++;

        /* Convert to magnitude^2 and MAX into row accumulator */
        for (unsigned int i = 0; i < d_fftsize/2; ++i)
        {
            float val = static_cast<float>(std::norm(fftOut[i + d_fftsize/2]));
            if (val > d_spectrogram_row_max[i])
                d_spectrogram_row_max[i] = val;
            if (val > d_global_maxhold[i])
                d_global_maxhold[i] = val;
        }
        for (unsigned int i = d_fftsize/2; i < d_fftsize; ++i)
        {
            float val = static_cast<float>(std::norm(fftOut[i - d_fftsize/2]));
            if (val > d_spectrogram_row_max[i])
                d_spectrogram_row_max[i] = val;
            if (val > d_global_maxhold[i])
                d_global_maxhold[i] = val;
        }

        /* Check if we've consumed enough samples for this row */
        if (d_spectrogram_row_samples >= d_spectrogram_step_size)
        {
            /* Finalize row */
            std::copy(d_spectrogram_row_max.begin(), d_spectrogram_row_max.end(),
                      d_spectrogram_data[d_spectrogram_completed_rows].begin());
            std::fill(d_spectrogram_row_max.begin(), d_spectrogram_row_max.end(), 0.0f);
            d_spectrogram_row_samples = 0;
            d_spectrogram_completed_rows++;
        }
    }

    /* If there's a partial row with some FFTs computed, finalize it anyway */
    bool has_partial_data = false;
    for (unsigned int i = 0; i < d_fftsize && !has_partial_data; ++i)
    {
        if (d_spectrogram_row_max[i] > 0.0f)
            has_partial_data = true;
    }

    if (has_partial_data && d_spectrogram_completed_rows < d_spectrogram_rows)
    {
        std::copy(d_spectrogram_row_max.begin(), d_spectrogram_row_max.end(),
                  d_spectrogram_data[d_spectrogram_completed_rows].begin());
        std::fill(d_spectrogram_row_max.begin(), d_spectrogram_row_max.end(), 0.0f);
        d_spectrogram_row_samples = 0;
        d_spectrogram_completed_rows++;
    }

    std::cout << "DEBUG rx_fft: finalize_spectrogram processed " << ffts_processed
              << " additional FFTs, completed_rows=" << d_spectrogram_completed_rows
              << "/" << d_spectrogram_rows << std::endl;
}

/*! \brief Get a completed spectrogram row.
 *  \param row Row index to retrieve
 *  \param data Buffer to copy row data (must be fftsize floats)
 *  \return 0 on success, -1 if row not yet completed
 */
int rx_fft_c::get_spectrogram_row(unsigned int row, float* data)
{
    std::lock_guard<std::mutex> lock(d_in_mutex);

    if (row >= d_spectrogram_completed_rows)
        return -1;

    memcpy(data, d_spectrogram_data[row].data(), sizeof(float) * d_fftsize);
    return 0;
}

/*! \brief Get global max-hold data (max across entire file).
 *  \param data Buffer to copy max-hold data (must be fftsize floats)
 *  \return 0 on success
 */
int rx_fft_c::get_global_maxhold(float* data)
{
    std::lock_guard<std::mutex> lock(d_in_mutex);
    memcpy(data, d_global_maxhold.data(), sizeof(float) * d_fftsize);
    return 0;
}


/**   rx_fft_f     **/

rx_fft_f_sptr make_rx_fft_f(unsigned int fftsize, double audio_rate,
                            int wintype, bool normalize_energy)
{
    return gnuradio::get_initial_sptr(new rx_fft_f (fftsize, audio_rate,
                                                    wintype, normalize_energy));
}

/*! \brief Create receiver FFT object.
 *  \param fftsize The FFT size.
 *  \param wintype The window type (see gr::fft::window::win_type).
 *  \param normalize_energy Normalize window for energy instead of amplitude.
 *
 */
rx_fft_f::rx_fft_f(unsigned int fftsize, double audio_rate,
                   int wintype, bool normalize_energy)
    : gr::sync_block ("rx_fft_f",
          gr::io_signature::make(1, 1, sizeof(float)),
          gr::io_signature::make(0, 0, 0)),
      d_fftsize(fftsize),
      d_startup_samples(0),
      d_audiorate(audio_rate),
      d_wintype(-1),
      d_normalize_energy(false)
{

    /* create FFT object */
#if GNURADIO_VERSION < 0x030900
    d_fft = new gr::fft::fft_complex(d_fftsize, true);
#else
    d_fft = new gr::fft::fft_complex_fwd(d_fftsize);
#endif

    /* allocate circular buffer */
#if GNURADIO_VERSION < 0x031000
    d_writer = gr::make_buffer(AUDIO_BUFFER_SIZE, sizeof(float));
#else
    d_writer = gr::make_buffer(AUDIO_BUFFER_SIZE, sizeof(float), 1, 1);
#endif
    d_reader = gr::buffer_add_reader(d_writer, 0);

    memset(d_writer->write_pointer(), 0, sizeof(gr_complex) * d_fftsize);
    d_writer->update_write_pointer(d_fftsize);

    /* create FFT window */
    set_window_type(wintype, d_normalize_energy);

    d_lasttime = std::chrono::steady_clock::now();
}

rx_fft_f::~rx_fft_f()
{
    delete d_fft;
}

/*! \brief Audio FFT work method.
 *  \param noutput_items
 *  \param input_items
 *  \param output_items
 *
 * This method does nothing except throwing the incoming samples into the
 * circular buffer.
 * FFT is only executed when the GUI asks for new FFT data via get_fft_data().
 */
int rx_fft_f::work(int noutput_items,
                   gr_vector_const_void_star &input_items,
                   gr_vector_void_star &output_items)
{
    const float *in = (const float*)input_items[0];
    (void) output_items;

    /* just throw new samples into the buffer */
    int items_to_copy = std::min(noutput_items, (int)d_writer->bufsize());
    if (items_to_copy < noutput_items)
        in += (noutput_items - items_to_copy);

    {
        std::lock_guard<std::mutex> lock(d_in_mutex);

        if (d_writer->space_available() < items_to_copy)
            d_reader->update_read_pointer(items_to_copy - d_writer->space_available());
        memcpy(d_writer->write_pointer(), in, sizeof(float) * items_to_copy);
        d_writer->update_write_pointer(items_to_copy);

        if (d_startup_samples < d_writer->bufsize())
            d_startup_samples += items_to_copy;
    }

    return noutput_items;
}

/*! \brief Get FFT data.
 *  \param fftPoints Buffer to copy FFT data
 *  \param fftSize Current FFT size (output).
 */
int rx_fft_f::get_fft_data(float* fftPoints)
{
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = now - d_lasttime;
    diff = std::min(diff, std::chrono::duration<double>(d_writer->bufsize() / d_audiorate));
    d_lasttime = now;

    /* perform FFT */
    {
        {
            std::lock_guard<std::mutex> lock(d_in_mutex);

            d_reader->update_read_pointer(std::min((unsigned int)(diff.count() * d_audiorate * 1.001), d_reader->items_available() - d_fftsize));

            if ((int)d_startup_samples < d_reader->items_available())
                return -1;

            apply_window(d_fftsize);
        }

        /* compute FFT */
        d_fft->execute();

        const std::complex<float> *fftOut = d_fft->get_outbuf();

        // Shifted mag^2(FFT)
        for (unsigned int i = 0; i < d_fftsize/2; ++i)
            fftPoints[i] = static_cast<float>(std::norm(fftOut[i + d_fftsize/2]));
        for (unsigned int i = d_fftsize/2; i < d_fftsize; ++i)
            fftPoints[i] = static_cast<float>(std::norm(fftOut[i - d_fftsize/2]));
    }

    return 0;
}

/*! \brief Compute FFT on the available input data.
 *  \param data_in The data to compute FFT on.
 *  \param size The size of data_in.
 *
 * Note that this function does not lock the mutex since the caller, get_fft_data()
 * has already locked it.
 */
void rx_fft_f::apply_window(unsigned int size)
{
    gr_complex *dst = d_fft->get_inbuf();
    float * p = (float *)d_reader->read_pointer();
    /* apply window, and convert to complex */
    if (d_window.size())
    {
        for (unsigned int i = 0; i < size; i++)
            dst[i] = p[i] * d_window[i];
    }
    else
    {
        for (unsigned int i = 0; i < size; i++)
            dst[i] = p[i];
    }
}


/*! \brief Set new FFT size. */
void rx_fft_f::set_fft_size(unsigned int fftsize)
{
    if (fftsize != d_fftsize)
    {
        d_fftsize = fftsize;

        /* reset FFT object (also reset FFTW plan) */
        delete d_fft;
#if GNURADIO_VERSION < 0x030900
        d_fft = new gr::fft::fft_complex(d_fftsize, true);
#else
        d_fft = new gr::fft::fft_complex_fwd(d_fftsize);
#endif

        update_window();
    }
}

/*! \brief Set new window type. */
void rx_fft_f::set_window_type(int wintype, bool normalize_energy)
{
    if ((wintype < gr::fft::window::WIN_HAMMING) || wintype > gr::fft::window::WIN_FLATTOP)
    {
        wintype = gr::fft::window::WIN_HAMMING;
    }

    if (wintype != d_wintype || normalize_energy != d_normalize_energy)
    {
        d_wintype = wintype;
        d_normalize_energy = normalize_energy;
        update_window();
    }
}

void rx_fft_f::update_window()
{
    float factor;

    d_window.clear();
    d_window = gr::fft::window::build((gr::fft::window::win_type)d_wintype, d_fftsize, 6.76);
    d_window.resize(d_fftsize);

    // Normalize using average of window for amplitude, or RMS for energy
    float sum = 0.0;
    for (auto v : d_window)
        sum += d_normalize_energy ? v * v : v;
    factor = sum / (float)d_fftsize;
    if (d_normalize_energy)
        factor = std::sqrt(factor);
    volk_32f_s32f_normalize(d_window.data(), factor, d_fftsize);
}
