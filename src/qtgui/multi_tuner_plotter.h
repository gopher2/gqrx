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
#ifndef MULTI_TUNER_PLOTTER_H
#define MULTI_TUNER_PLOTTER_H

#include "plotter.h"
#include <QColor>
#include <QBrush>
#include <QPen>
#include <map>

/**
 * @brief Enhanced plotter with multi-tuner marker support
 * @ingroup UI
 *
 * Extends the basic CPlotter to show markers for multiple tuners
 * with different colors and information display.
 */
class MultiTunerPlotter : public CPlotter
{
    Q_OBJECT

public:
    explicit MultiTunerPlotter(QWidget *parent = nullptr);
    ~MultiTunerPlotter() override;

    /** Tuner marker information */
    struct TunerMarker {
        int tuner_id;
        QString name;
        qint64 frequency;
        int filter_low;
        int filter_high;
        int max_filter_half_width;  // Maximum filter half-width based on demod mode
        bool enabled;
        bool active;        // Is this the active tuner?
        QColor color;
        QColor filter_color;
        int alpha;          // Filter transparency (0-255)

        TunerMarker() : tuner_id(-1), frequency(0), filter_low(-5000),
                       filter_high(5000), max_filter_half_width(10000),  // Default to NFM max (±10kHz)
                       enabled(true), active(false),
                       color(Qt::red), filter_color(Qt::red), alpha(25) {}  // ~10% transparency
    };

    /** Filter edge being dragged */
    enum FilterEdge { NoEdge, LeftEdge, RightEdge };

    /** Add or update a tuner marker */
    void setTunerMarker(int tuner_id, const TunerMarker& marker);

    /** Get a tuner marker (returns marker with tuner_id=-1 if not found) */
    TunerMarker getTunerMarker(int tuner_id) const;

    /** Remove a tuner marker */
    void removeTunerMarker(int tuner_id);

    /** Clear all tuner markers */
    void clearTunerMarkers();

    /** Set active tuner (highlights differently) */
    void setActiveTuner(int tuner_id);

    /** Enable/disable multi-tuner display */
    void setMultiTunerEnabled(bool enabled);
    bool isMultiTunerEnabled() const { return m_MultiTunerEnabled; }

    /** Enable/disable IQ playback mode (prevents frequency changes) */
    void setIqPlaybackActive(bool active) { m_IqPlaybackActive = active; }
    bool isIqPlaybackActive() const { return m_IqPlaybackActive; }

    /** Set marker display options */
    void setShowTunerNames(bool show) { m_ShowTunerNames = show; }
    void setShowTunerFilters(bool show) { m_ShowTunerFilters = show; }
    void setShowTunerFrequencies(bool show) { m_ShowTunerFrequencies = show; }

    /** Get tuner marker at position */
    int getTunerAtPosition(const QPoint& pos) const;

    /** Get list of all tuner IDs */
    std::vector<int> getTunerIds() const;

public slots:
    /** Update tuner frequency */
    void updateTunerFrequency(int tuner_id, qint64 frequency);

    /** Update tuner filter */
    void updateTunerFilter(int tuner_id, int filter_low, int filter_high);

    /** Update tuner enabled state */
    void updateTunerEnabled(int tuner_id, bool enabled);

    /** Set tuner marker enabled (visible on FFT) */
    void setTunerMarkerEnabled(int tuner_id, bool enabled);

    /** Set tuner marker name */
    void setTunerMarkerName(int tuner_id, const QString& name);

    /** Set tuner marker color */
    void setTunerMarkerColor(int tuner_id, const QColor& color);

    /** Set tuner marker alpha (filter transparency) */
    void setTunerMarkerAlpha(int tuner_id, int alpha);

    /** Set tuner marker max filter width (based on demod mode) */
    void setTunerMaxFilterWidth(int tuner_id, int max_half_width);

signals:
    /** Emitted when user clicks on tuner marker */
    void tunerMarkerClicked(int tuner_id, qint64 frequency);

    /** Emitted when user wants to tune a specific tuner to frequency */
    void tuneToFrequency(int tuner_id, qint64 frequency);

    /** Emitted when user resizes filter by dragging edges */
    void filterResized(int tuner_id, int filter_low, int filter_high);

    /** Emitted when user shift+drags to pan SDR center frequency (dragged tuner stays centered) */
    void panSdrFrequency(int dragged_tuner_id, qint64 new_sdr_freq);

    /** Emitted when user double-clicks to set new SDR center frequency */
    void newCenterFreqRequest(qint64 freq);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;

public:
    QColor getDefaultTunerColor(int tuner_id) const;

private:
    void drawTunerMarkers(QPainter& painter);
    void drawTunerMarker(QPainter& painter, const TunerMarker& marker);
    void drawTunerFilter(QPainter& painter, const TunerMarker& marker);
    void drawTunerLabel(QPainter& painter, const TunerMarker& marker, int x, int y);

    int frequencyToScreenX(qint64 frequency) const;
    qint64 screenXToFrequency(int x) const;

    void updateTooltipForTuner(int tuner_id, const QPoint& pos);

    /** Check if position is near a filter edge, returns tuner_id and edge type */
    FilterEdge getFilterEdgeAtPosition(const QPoint& pos, int& tuner_id) const;

    /** Check if position is on the bottom handle (drag icon area) of a tuner marker */
    bool isOnTunerHandle(const QPoint& pos, int tuner_id) const;

private:
    std::map<int, TunerMarker> m_TunerMarkers;
    int m_ActiveTuner;
    bool m_MultiTunerEnabled;
    bool m_IqPlaybackActive;
    bool m_ShowTunerNames;
    bool m_ShowTunerFilters;
    bool m_ShowTunerFrequencies;

    // Mouse interaction state
    int m_DraggedTuner;
    bool m_IsDraggingTuner;
    bool m_DraggedViaHandle;  // True if drag started via bottom handle (enables zoom)
    QPoint m_LastMousePos;

    // Filter edge dragging state
    int m_DraggedFilterTuner;
    FilterEdge m_DraggedFilterEdge;

    // Default colors for tuners
    static const QColor DEFAULT_TUNER_COLORS[];
    static const int NUM_DEFAULT_COLORS;

    // Marker display settings
    static const int MARKER_HEIGHT = 20;
    static const int LABEL_OFFSET_Y = -25;
};

#endif // MULTI_TUNER_PLOTTER_H
