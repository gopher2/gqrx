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
#include "multi_tuner_plotter.h"
#include "bookmarks.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QFontMetrics>
#include <algorithm>

// Default colors for tuners (cycle through these)
const QColor MultiTunerPlotter::DEFAULT_TUNER_COLORS[] = {
    QColor(255, 0, 0),      // Red
    QColor(0, 255, 0),      // Green
    QColor(0, 0, 255),      // Blue
    QColor(255, 255, 0),    // Yellow
    QColor(255, 0, 255),    // Magenta
    QColor(0, 255, 255),    // Cyan
    QColor(255, 128, 0),    // Orange
    QColor(128, 0, 255),    // Purple
    QColor(255, 128, 128),  // Light red
    QColor(128, 255, 128),  // Light green
    QColor(128, 128, 255),  // Light blue
    QColor(192, 192, 192)   // Light gray
};

const int MultiTunerPlotter::NUM_DEFAULT_COLORS = sizeof(DEFAULT_TUNER_COLORS) / sizeof(QColor);

MultiTunerPlotter::MultiTunerPlotter(QWidget *parent)
    : CPlotter(parent)
    , m_ActiveTuner(-1)
    , m_MultiTunerEnabled(true)
    , m_IqPlaybackActive(false)
    , m_ShowTunerNames(false)
    , m_ShowTunerFilters(true)
    , m_ShowTunerFrequencies(false)
    , m_DraggedTuner(-1)
    , m_IsDraggingTuner(false)
    , m_DraggedViaHandle(false)
    , m_DraggedFilterTuner(-1)
    , m_DraggedFilterEdge(NoEdge)
{

    // Enable mouse tracking for better interaction
    setMouseTracking(true);

    // Disable the default filter box - we use tuner markers instead
    setFilterBoxEnabled(false);

}

MultiTunerPlotter::~MultiTunerPlotter()
{
}

void MultiTunerPlotter::setTunerMarker(int tuner_id, const TunerMarker& marker)
{


    TunerMarker new_marker = marker;
    new_marker.tuner_id = tuner_id;

    // Assign default color if not specified
    if (marker.color == QColor()) {
        new_marker.color = getDefaultTunerColor(tuner_id);
    }
    // Always set filter_color to match marker color
    new_marker.filter_color = new_marker.color;

    m_TunerMarkers[tuner_id] = new_marker;
    update(); // Trigger repaint
}

MultiTunerPlotter::TunerMarker MultiTunerPlotter::getTunerMarker(int tuner_id) const
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        return it->second;
    }
    return TunerMarker();  // Returns marker with tuner_id=-1
}

void MultiTunerPlotter::removeTunerMarker(int tuner_id)
{

    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        m_TunerMarkers.erase(it);

        // Clear active tuner if it was this one
        if (m_ActiveTuner == tuner_id) {
            m_ActiveTuner = -1;
        }

        update();
    }
}

void MultiTunerPlotter::clearTunerMarkers()
{
    m_TunerMarkers.clear();
    m_ActiveTuner = -1;
    update();
}

void MultiTunerPlotter::setActiveTuner(int tuner_id)
{
    if (m_ActiveTuner != tuner_id) {
        m_ActiveTuner = tuner_id;

        // Update active flag in all markers
        for (auto& pair : m_TunerMarkers) {
            pair.second.active = (pair.first == tuner_id);
        }

        update();
    }
}

void MultiTunerPlotter::setMultiTunerEnabled(bool enabled)
{
    if (m_MultiTunerEnabled != enabled) {
        m_MultiTunerEnabled = enabled;
        // Always keep filter box disabled in multi-tuner plotter
        // (tuner markers show filters, not the legacy single filter box)
        setFilterBoxEnabled(false);
        update();
    }
}

int MultiTunerPlotter::getTunerAtPosition(const QPoint& pos) const
{
    if (!m_MultiTunerEnabled) return -1;

    // Dimensions must match drawTunerMarker
    const int TOTAL_HEIGHT = 20;
    const int TAPER_HEIGHT = 5;
    const int BASE_WIDTH = 28;
    int fft_height = getFftHeight();
    int tip_y = fft_height - TOTAL_HEIGHT - 2;
    int base_top_y = tip_y + TAPER_HEIGHT;
    int base_bottom_y = fft_height - 2;
    int half_base = BASE_WIDTH / 2;

    for (const auto& pair : m_TunerMarkers) {
        const TunerMarker& marker = pair.second;
        if (!marker.enabled) continue;

        int center_x = frequencyToScreenX(marker.frequency);

        // Check if in the squared base area (left half = drag)
        if (pos.y() >= base_top_y && pos.y() <= base_bottom_y) {
            if (pos.x() >= center_x - half_base && pos.x() <= center_x) {
                return pair.first;
            }
        }

        // Check if in the taper area (left half = drag)
        if (pos.y() >= tip_y && pos.y() < base_top_y) {
            float t = static_cast<float>(pos.y() - tip_y) / static_cast<float>(base_top_y - tip_y);
            int half_width_at_y = static_cast<int>(t * half_base);
            if (pos.x() >= center_x - half_width_at_y && pos.x() <= center_x) {
                return pair.first;
            }
        }

        // Also allow dragging from the vertical center line above marker
        if (pos.y() < tip_y && abs(pos.x() - center_x) <= 5) {
            return pair.first;
        }
    }

    return -1;
}

// Check if position is on the bottom handle area (where the drag icon is)
bool MultiTunerPlotter::isOnTunerHandle(const QPoint& pos, int tuner_id) const
{
    if (!m_MultiTunerEnabled) return false;

    auto it = m_TunerMarkers.find(tuner_id);
    if (it == m_TunerMarkers.end() || !it->second.enabled) return false;

    // Dimensions must match drawTunerMarker
    const int TOTAL_HEIGHT = 20;
    const int TAPER_HEIGHT = 5;
    const int BASE_WIDTH = 28;
    int fft_height = getFftHeight();
    int base_top_y = fft_height - TOTAL_HEIGHT - 2 + TAPER_HEIGHT;
    int base_bottom_y = fft_height - 2;
    int half_base = BASE_WIDTH / 2;

    int center_x = frequencyToScreenX(it->second.frequency);

    // Handle is in the left half of the base area only (not the taper or line above)
    if (pos.y() >= base_top_y && pos.y() <= base_bottom_y) {
        if (pos.x() >= center_x - half_base && pos.x() <= center_x) {
            return true;
        }
    }

    return false;
}

MultiTunerPlotter::FilterEdge MultiTunerPlotter::getFilterEdgeAtPosition(const QPoint& pos, int& tuner_id) const
{
    if (!m_MultiTunerEnabled) {
        tuner_id = -1;
        return NoEdge;
    }

    // Dimensions must match drawTunerMarker
    const int TOTAL_HEIGHT = 20;
    const int TAPER_HEIGHT = 5;
    const int BASE_WIDTH = 28;
    int fft_height = getFftHeight();
    int tip_y = fft_height - TOTAL_HEIGHT - 2;
    int base_top_y = tip_y + TAPER_HEIGHT;
    int base_bottom_y = fft_height - 2;
    int half_base = BASE_WIDTH / 2;

    // Edge detection tolerance in pixels
    const int EDGE_TOLERANCE = 6;

    for (const auto& pair : m_TunerMarkers) {
        const TunerMarker& marker = pair.second;
        if (!marker.enabled) continue;

        int center_x = frequencyToScreenX(marker.frequency);

        // Check if we're in the marker base or taper area first
        bool in_marker_base = (pos.y() >= base_top_y && pos.y() <= base_bottom_y &&
                               pos.x() >= center_x - half_base && pos.x() <= center_x + half_base);
        bool in_marker_taper = false;
        if (pos.y() >= tip_y && pos.y() < base_top_y) {
            float t = static_cast<float>(pos.y() - tip_y) / static_cast<float>(base_top_y - tip_y);
            int half_width_at_y = static_cast<int>(t * half_base);
            in_marker_taper = (pos.x() >= center_x - half_width_at_y && pos.x() <= center_x + half_width_at_y);
        }

        // If in marker area, only the right half triggers resize (left half is for dragging)
        if (in_marker_base) {
            if (pos.x() > center_x) {
                tuner_id = pair.first;
                return RightEdge;
            }
            // Left half of marker base = drag handle, not a filter edge
            continue;
        }

        if (in_marker_taper) {
            if (pos.x() > center_x) {
                tuner_id = pair.first;
                return RightEdge;
            }
            // Left half of marker taper = drag handle, not a filter edge
            continue;
        }

        // Only check filter edges if NOT in the marker area
        if (m_ShowTunerFilters && pos.y() >= 0 && pos.y() < fft_height) {
            qint64 filter_low_freq = marker.frequency + marker.filter_low;
            qint64 filter_high_freq = marker.frequency + marker.filter_high;
            int filter_left_x = frequencyToScreenX(filter_low_freq);
            int filter_right_x = frequencyToScreenX(filter_high_freq);

            // Check left edge of filter
            if (std::abs(pos.x() - filter_left_x) <= EDGE_TOLERANCE) {
                tuner_id = pair.first;
                return LeftEdge;
            }

            // Check right edge of filter
            if (std::abs(pos.x() - filter_right_x) <= EDGE_TOLERANCE) {
                tuner_id = pair.first;
                return RightEdge;
            }
        }
    }

    tuner_id = -1;
    return NoEdge;
}

std::vector<int> MultiTunerPlotter::getTunerIds() const
{
    std::vector<int> ids;
    for (const auto& pair : m_TunerMarkers) {
        ids.push_back(pair.first);
    }
    return ids;
}

void MultiTunerPlotter::updateTunerFrequency(int tuner_id, qint64 frequency)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.frequency = frequency;
        update();
    }
}

void MultiTunerPlotter::updateTunerFilter(int tuner_id, int filter_low, int filter_high)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.filter_low = filter_low;
        it->second.filter_high = filter_high;
        update();
    }
}

void MultiTunerPlotter::updateTunerEnabled(int tuner_id, bool enabled)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.enabled = enabled;
        update();
    }
}

void MultiTunerPlotter::setTunerMarkerEnabled(int tuner_id, bool enabled)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.enabled = enabled;
        update();
    }
}

void MultiTunerPlotter::setTunerMarkerName(int tuner_id, const QString& name)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.name = name;
        update();
    }
}

void MultiTunerPlotter::setTunerMarkerColor(int tuner_id, const QColor& color)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.color = color;
        it->second.filter_color = color;
        update();
    }
}

void MultiTunerPlotter::setTunerMarkerAlpha(int tuner_id, int alpha)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.alpha = qBound(0, alpha, 255);
        update();
    }
}

void MultiTunerPlotter::setTunerMaxFilterWidth(int tuner_id, int max_half_width)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it != m_TunerMarkers.end()) {
        it->second.max_filter_half_width = max_half_width;
    }
}

void MultiTunerPlotter::paintEvent(QPaintEvent *event)
{
    // Call parent implementation first
    CPlotter::paintEvent(event);

    // Draw tuner markers on top
    if (m_MultiTunerEnabled && !m_TunerMarkers.empty()) {
        QPainter painter(this);
        drawTunerMarkers(painter);
    }

    // Draw exclusion zone indicator when dragging via handle
    if (m_DraggedViaHandle && m_IsDraggingTuner) {
        QPainter painter(this);
        drawDragExclusionZone(painter);
    }
}

void MultiTunerPlotter::drawDragExclusionZone(QPainter& painter)
{
    int total_height = height();
    int fft_height = getFftHeight();
    int exclusion_zone = total_height / 15;  // ~7% of total height (larger zone)

    int zone_top = fft_height - exclusion_zone;
    int zone_bottom = fft_height + exclusion_zone;

    // Fill the exclusion zone with semi-transparent yellow tint (visible on dark and light)
    painter.fillRect(0, zone_top, width(), zone_bottom - zone_top,
                     QColor(255, 255, 0, 50));  // Yellow, ~20% opacity

    // Draw boundary lines in contrasting color
    QPen pen(QColor(255, 200, 0, 180));  // Orange-yellow
    pen.setStyle(Qt::SolidLine);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.drawLine(0, zone_top, width(), zone_top);
    painter.drawLine(0, zone_bottom, width(), zone_bottom);

    // Draw zoom direction indicators on both sides
    QFont labelFont("Arial", 12, QFont::Bold);
    painter.setFont(labelFont);
    QFontMetrics fm(labelFont);

    QString zoomInText = "↑ Zoom In";
    QString zoomOutText = "↓ Zoom Out";
    int zoomInWidth = fm.horizontalAdvance(zoomInText);
    int zoomOutWidth = fm.horizontalAdvance(zoomOutText);
    int textHeight = fm.height();
    int padding = 4;

    // Left side - Zoom In (above zone)
    QRect leftInRect(10, zone_top - textHeight - padding * 2, zoomInWidth + padding * 2, textHeight + padding);
    painter.fillRect(leftInRect, QColor(0, 0, 0, 160));
    painter.setPen(QColor(255, 255, 255, 240));
    painter.drawText(leftInRect.x() + padding, zone_top - padding - 2, zoomInText);

    // Left side - Zoom Out (below zone)
    QRect leftOutRect(10, zone_bottom + padding, zoomOutWidth + padding * 2, textHeight + padding);
    painter.fillRect(leftOutRect, QColor(0, 0, 0, 160));
    painter.drawText(leftOutRect.x() + padding, zone_bottom + textHeight + padding - 2, zoomOutText);

    // Right side - Zoom In (above zone)
    QRect rightInRect(width() - zoomInWidth - padding * 2 - 10, zone_top - textHeight - padding * 2, zoomInWidth + padding * 2, textHeight + padding);
    painter.fillRect(rightInRect, QColor(0, 0, 0, 160));
    painter.drawText(rightInRect.x() + padding, zone_top - padding - 2, zoomInText);

    // Right side - Zoom Out (below zone)
    QRect rightOutRect(width() - zoomOutWidth - padding * 2 - 10, zone_bottom + padding, zoomOutWidth + padding * 2, textHeight + padding);
    painter.fillRect(rightOutRect, QColor(0, 0, 0, 160));
    painter.drawText(rightOutRect.x() + padding, zone_bottom + textHeight + padding - 2, zoomOutText);
}

void MultiTunerPlotter::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_MultiTunerEnabled) {
        // First check for filter edge click (higher priority)
        int edge_tuner_id;
        FilterEdge edge = getFilterEdgeAtPosition(event->pos(), edge_tuner_id);
        if (edge != NoEdge) {
            // Don't allow filter resize if tuner is locked
            if (isTunerLocked(edge_tuner_id)) {
                return;
            }
            m_DraggedFilterTuner = edge_tuner_id;
            m_DraggedFilterEdge = edge;
            m_LastMousePos = event->pos();
            setCursor(Qt::SizeHorCursor);
            return;
        }

        // Then check for tuner center click
        int tuner_id = getTunerAtPosition(event->pos());
        if (tuner_id >= 0) {
            // Don't allow dragging if tuner is locked
            if (isTunerLocked(tuner_id)) {
                // Still emit click event for selection purposes
                emit tunerMarkerClicked(tuner_id, screenXToFrequency(event->pos().x()));
                return;
            }

            // Start tuner dragging
            m_DraggedTuner = tuner_id;
            m_IsDraggingTuner = true;
            m_DraggedViaHandle = isOnTunerHandle(event->pos(), tuner_id);
            m_LastMousePos = event->pos();

            emit tunerMarkerClicked(tuner_id, screenXToFrequency(event->pos().x()));
            return;
        }
    }

    // Intercept right-click - let contextMenuEvent handle it (always allow adding tuners)
    if (event->button() == Qt::RightButton) {
        return;  // Don't pass to parent, contextMenuEvent will show menu
    }

    // If not handled by tuner logic, pass to parent
    CPlotter::mousePressEvent(event);
}

void MultiTunerPlotter::mouseMoveEvent(QMouseEvent *event)
{
    // Handle filter edge dragging (symmetric width resize - both sides adjust equally)
    if ((m_DraggedFilterEdge == RightEdge || m_DraggedFilterEdge == LeftEdge) && m_DraggedFilterTuner >= 0) {
        auto it = m_TunerMarkers.find(m_DraggedFilterTuner);
        if (it != m_TunerMarkers.end()) {
            qint64 mouse_freq = screenXToFrequency(event->pos().x());
            qint64 center_freq = it->second.frequency;
            qint64 offset = mouse_freq - center_freq;

            // Prevent dragging past center: right edge stays right, left edge stays left
            if (m_DraggedFilterEdge == RightEdge && offset < 0) {
                offset = 100;  // Minimum positive offset
            } else if (m_DraggedFilterEdge == LeftEdge && offset > 0) {
                offset = -100;  // Minimum negative offset
            }

            // Calculate half-width based on distance from center to mouse
            int half_width = static_cast<int>(std::abs(offset));

            // Minimum half-width of 100 Hz
            half_width = std::max(half_width, 100);

            // Clamp to maximum filter half-width for this demod mode (no hard cap)
            int max_hw = it->second.max_filter_half_width;
            if (max_hw > 0 && half_width > max_hw) {
                half_width = max_hw;
            }

            // Apply symmetrically to both sides of center
            it->second.filter_low = -half_width;
            it->second.filter_high = half_width;


            update();
            emit filterResized(m_DraggedFilterTuner, it->second.filter_low, it->second.filter_high);
        }
        m_LastMousePos = event->pos();
        return;
    }

    // Handle tuner center dragging
    if (m_IsDraggingTuner && m_DraggedTuner >= 0) {
        auto it = m_TunerMarkers.find(m_DraggedTuner);
        if (it != m_TunerMarkers.end()) {
            bool shift_held = (event->modifiers() & Qt::ShiftModifier);

            if (shift_held) {
                // Shift+drag: Pan SDR center frequency (tuner stays at same screen position)
                // Calculate how much the mouse moved in pixels, convert to frequency delta
                int px_delta = event->pos().x() - m_LastMousePos.x();

                qint64 span = getSpan();
                int plot_width = width();
                if (plot_width <= 0) plot_width = 1;

                double hz_per_pixel = static_cast<double>(span) / static_cast<double>(plot_width);
                qint64 freq_delta = static_cast<qint64>(px_delta * hz_per_pixel);

                if (freq_delta != 0) {
                    // Move SDR center - negate delta for "grab and drag" feel
                    // (drag right = see lower frequencies = SDR freq decreases)
                    qint64 current_sdr_freq = getCenterFreq();
                    qint64 new_sdr_freq = current_sdr_freq - freq_delta;
                    emit panSdrFrequency(m_DraggedTuner, new_sdr_freq);
                }
            } else {
                // Normal drag: Update only the dragged tuner's frequency
                qint64 new_freq = screenXToFrequency(event->pos().x());
                it->second.frequency = new_freq;

                // Emit signal for frequency change
                emit tuneToFrequency(m_DraggedTuner, new_freq);
            }

            // Handle vertical movement for zooming (only when dragging via handle, not with shift)
            // Zoom level based on absolute Y position:
            // - Top of FFT (y=0) = maximum zoom in
            // - Bottom of waterfall (y=height) = maximum zoom out
            if (m_DraggedViaHandle) {
                int total_height = height();
                int fft_height = getFftHeight();
                int mouse_y = event->pos().y();
                int exclusion_zone = total_height / 15;  // ~7% of total height

                // Define exclusion zone around FFT/Waterfall divider
                int zone_top = fft_height - exclusion_zone;
                int zone_bottom = fft_height + exclusion_zone;

                // Only zoom if outside the exclusion zone
                if (mouse_y < zone_top || mouse_y > zone_bottom) {
                    // Calculate y_ratio mapped to usable zones:
                    // FFT area: y=0 -> ratio=0, y=zone_top -> ratio=0.5
                    // Waterfall area: y=zone_bottom -> ratio=0.5, y=height -> ratio=1.0
                    float y_ratio;
                    if (mouse_y < zone_top) {
                        // In FFT area: map [0, zone_top] to [0, 0.5]
                        y_ratio = 0.5f * static_cast<float>(mouse_y) / static_cast<float>(zone_top);
                    } else {
                        // In Waterfall area: map [zone_bottom, height] to [0.5, 1.0]
                        y_ratio = 0.5f + 0.5f * static_cast<float>(mouse_y - zone_bottom) / static_cast<float>(total_height - zone_bottom);
                    }
                    y_ratio = qBound(0.0f, y_ratio, 1.0f);

                    // Map y_ratio to zoom level:
                    // ratio=0 (top of FFT) -> max zoom in (100000x)
                    // ratio=1 (bottom of waterfall) -> min zoom (1x, full span)
                    // Use exponential scale for smooth zooming
                    float max_zoom = 100000.0f;  // Maximum zoom factor (matches FFT settings slider)
                    float min_zoom = 1.0f;       // Minimum zoom (full span)

                    // Exponential interpolation: zoom = max_zoom^(1-y_ratio) * min_zoom^y_ratio
                    float target_zoom = std::pow(max_zoom, 1.0f - y_ratio) * std::pow(min_zoom, y_ratio);

                    // Get current zoom level
                    float current_zoom = static_cast<float>(getSampleRate()) / static_cast<float>(getSpan());

                    // Directional constraint:
                    // - FFT area (above): only zoom IN (target > current)
                    // - Waterfall area (below): only zoom OUT (target < current)
                    bool allow_zoom = false;
                    if (mouse_y < zone_top && target_zoom > current_zoom) {
                        // In FFT area, only allow zooming in
                        allow_zoom = true;
                    } else if (mouse_y > zone_bottom && target_zoom < current_zoom) {
                        // In Waterfall area, only allow zooming out
                        allow_zoom = true;
                    }

                    if (allow_zoom) {
                        // Calculate zoom factor to reach target
                        float zoom_factor = current_zoom / target_zoom;
                        zoom_factor = qBound(0.7f, zoom_factor, 1.43f);  // Limit per-frame change for smoothness

                        if (std::abs(zoom_factor - 1.0f) > 0.001f) {
                            // Convert logical pixels to physical pixels for zoomStepX
                            int physical_x = qRound(event->pos().x() * devicePixelRatioF());
                            zoomStepX(zoom_factor, physical_x);
                        }
                    }
                }
            }

            update();
        }

        m_LastMousePos = event->pos();
        return;  // Don't pass to parent - we handled the drag
    }

    if (m_MultiTunerEnabled) {
        // Update cursor based on what's under the mouse
        int edge_tuner_id;
        FilterEdge edge = getFilterEdgeAtPosition(event->pos(), edge_tuner_id);
        if (edge != NoEdge) {
            setCursor(Qt::SizeHorCursor);
        } else {
            int tuner_id = getTunerAtPosition(event->pos());
            if (tuner_id >= 0) {
                setCursor(Qt::SizeAllCursor);
                updateTooltipForTuner(tuner_id, event->pos());
            } else {
                setCursor(Qt::ArrowCursor);
            }
        }
    }

    // Call parent implementation
    CPlotter::mouseMoveEvent(event);
}

void MultiTunerPlotter::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_DraggedFilterEdge != NoEdge) {
            m_DraggedFilterEdge = NoEdge;
            m_DraggedFilterTuner = -1;
            setCursor(Qt::ArrowCursor);
            return;
        }

        if (m_IsDraggingTuner) {
            m_IsDraggingTuner = false;
            m_DraggedTuner = -1;
            m_DraggedViaHandle = false;
            setCursor(Qt::ArrowCursor);
            return;
        }
    }

    // Call parent implementation
    CPlotter::mouseReleaseEvent(event);
}

void MultiTunerPlotter::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Don't allow frequency changes during IQ playback
        if (m_IqPlaybackActive)
            return;

        // Check if clicking on a peak circle - let parent handle pinned labels
        if (isPeakDetectActive() && getNearestPeak(event->pos()) != -1) {
            CPlotter::mouseDoubleClickEvent(event);
            return;
        }

        // Double-click sets new SDR center frequency
        qint64 freq = screenXToFrequency(event->pos().x());
        emit newCenterFreqRequest(freq);
        return;
    }

    // Call parent implementation
    CPlotter::mouseDoubleClickEvent(event);
}

void MultiTunerPlotter::wheelEvent(QWheelEvent *event)
{
    // Check if wheel event is over a tuner marker
    if (m_MultiTunerEnabled) {
        int tuner_id = getTunerAtPosition(event->position().toPoint());
        if (tuner_id >= 0) {
            // Don't allow wheel tuning if tuner is locked
            if (isTunerLocked(tuner_id)) {
                return;
            }
            // Fine-tune the tuner frequency with mouse wheel
            auto it = m_TunerMarkers.find(tuner_id);
            if (it != m_TunerMarkers.end()) {
                qint64 freq_step = 1000; // 1 kHz steps
                if (event->modifiers() & Qt::ControlModifier) {
                    freq_step = 100; // Fine tuning: 100 Hz
                } else if (event->modifiers() & Qt::ShiftModifier) {
                    freq_step = 10000; // Coarse tuning: 10 kHz
                }

                qint64 old_freq = it->second.frequency;
                qint64 new_freq = old_freq;
                if (event->angleDelta().y() > 0) {
                    new_freq += freq_step;
                } else {
                    new_freq -= freq_step;
                }


                it->second.frequency = new_freq;
                update();

                emit tuneToFrequency(tuner_id, new_freq);
                return;
            }
        }
    }

    // Call parent implementation
    CPlotter::wheelEvent(event);
}

void MultiTunerPlotter::leaveEvent(QEvent *event)
{
    QToolTip::hideText();
    CPlotter::leaveEvent(event);
}

void MultiTunerPlotter::contextMenuEvent(QContextMenuEvent *event)
{
    // Always allow adding tuners via context menu (even when no tuners exist yet)
    // Get click position in DPR-scaled coordinates (m_Taglist uses DPR-scaled coords)
    QPointF ppos = event->pos() * m_DPR;

    // Get frequency at click position
    qint64 freq = screenXToFrequency(event->pos().x());

    // Check if clicking on a bookmark tag label (using m_Taglist from parent)
    BookmarkInfo clickedBookmark;
    bool onBookmark = false;
    qint64 tagFreq = 0;

    // m_Taglist contains pairs of (QRectF tag_rect, qint64 frequency)
    for (const auto& tag : m_Taglist) {
        if (tag.first.contains(ppos)) {
            tagFreq = tag.second;
            onBookmark = true;
            break;
        }
    }

    // If clicked on a tag, find the corresponding bookmark info
    bool bookmarkFound = false;
    if (onBookmark) {
        Bookmarks& bookmarks = Bookmarks::Get();
        for (int i = 0; i < bookmarks.size(); ++i) {
            const BookmarkInfo& bm = bookmarks.getBookmark(i);
            if (bm.frequency == tagFreq) {
                clickedBookmark = bm;
                bookmarkFound = true;
                break;
            }
        }
    }

    QMenu menu(this);
    QAction *addTunerAction = nullptr;
    QAction *addTunerFromBookmarkAction = nullptr;

    if (bookmarkFound) {
        // Show bookmark-specific option
        addTunerFromBookmarkAction = menu.addAction(
            QString("Add Tuner for '%1' (%2)")
                .arg(clickedBookmark.name)
                .arg(clickedBookmark.modulation));
        addTunerFromBookmarkAction->setData(clickedBookmark.frequency);
    } else {
        // Format frequency for display
        QString freqStr;
        if (freq >= 1000000000)
            freqStr = QString("%1 GHz").arg((double)freq / 1e9, 0, 'f', 6);
        else if (freq >= 1000000)
            freqStr = QString("%1 MHz").arg((double)freq / 1e6, 0, 'f', 3);
        else if (freq >= 1000)
            freqStr = QString("%1 kHz").arg((double)freq / 1e3, 0, 'f', 1);
        else
            freqStr = QString("%1 Hz").arg(freq);

        addTunerAction = menu.addAction(QString("Add Tuner at %1").arg(freqStr));
        addTunerAction->setData(freq);
    }

    QAction *selectedAction = menu.exec(event->globalPos());

    if (selectedAction == addTunerAction && addTunerAction) {
        emit addTunerRequested(freq);
    } else if (selectedAction == addTunerFromBookmarkAction && addTunerFromBookmarkAction) {
        emit addTunerFromBookmarkRequested(clickedBookmark.frequency, clickedBookmark.modulation, clickedBookmark.name);
    }
}

void MultiTunerPlotter::drawTunerMarkers(QPainter& painter)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw in order: inactive tuners first, then active tuner on top
    for (int pass = 0; pass < 2; pass++) {
        bool draw_active = (pass == 1);

        for (const auto& pair : m_TunerMarkers) {
            const TunerMarker& marker = pair.second;

            if (!marker.enabled) continue;
            if (marker.active != draw_active) continue;

            // Draw filter overlay first (behind marker)
            if (m_ShowTunerFilters) {
                drawTunerFilter(painter, marker);
            }

            // Draw marker line
            drawTunerMarker(painter, marker);
        }
    }
}

void MultiTunerPlotter::drawTunerMarker(QPainter& painter, const TunerMarker& marker)
{
    int center_x = frequencyToScreenX(marker.frequency);

    // Calculate FFT area bounds (below any overlapping bookmark at this x)
    int plot_top = getBookmarkTagsBottomAtX(center_x - 1, center_x + 1);
    int plot_bottom = getFftHeight();

    // Marker dimensions - short taper then squared base with handle icons
    const int TOTAL_HEIGHT = 20;       // Total height of marker
    const int TAPER_HEIGHT = 5;        // Short transition from point to base
    const int BASE_WIDTH = 28;         // Width of squared base

    const int TIP_Y = plot_bottom - TOTAL_HEIGHT - 2;
    const int BASE_TOP_Y = TIP_Y + TAPER_HEIGHT;
    const int BASE_BOTTOM_Y = plot_bottom - 2;
    int half_base = BASE_WIDTH / 2;

    // Choose colors based on whether tuner is active
    QColor base_color = marker.color;
    if (!marker.active) {
        base_color.setAlpha(180);
    }

    // Draw center vertical line (the frequency marker) - from top down to tip
    if (center_x >= 0 && center_x < width()) {
        QPen line_pen(base_color, marker.active ? 2 : 1);
        painter.setPen(line_pen);
        painter.drawLine(center_x, plot_top, center_x, TIP_Y);
    }

    // Draw the marker shape - short taper then squared base
    QPolygon shape;
    shape << QPoint(center_x, TIP_Y)                              // Tip (top center)
          << QPoint(center_x + half_base, BASE_TOP_Y)             // Right shoulder
          << QPoint(center_x + half_base, BASE_BOTTOM_Y)          // Bottom right
          << QPoint(center_x - half_base, BASE_BOTTOM_Y)          // Bottom left
          << QPoint(center_x - half_base, BASE_TOP_Y)             // Left shoulder
          << QPoint(center_x, TIP_Y);                             // Back to tip

    // Draw filled shape (no border)
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(base_color));
    painter.drawPolygon(shape);

    // Calculate handle centers
    int left_handle_cx = center_x - half_base / 2;
    int right_handle_cx = center_x + half_base / 2;
    int handle_cy = (BASE_TOP_Y + BASE_BOTTOM_Y) / 2;

    // Choose icon color based on marker brightness (use dark on bright colors)
    int luminance = (299 * base_color.red() + 587 * base_color.green() + 114 * base_color.blue()) / 1000;
    QColor icon_color = (luminance > 140) ? QColor(40, 40, 40) : Qt::white;

    // Draw drag icon on LEFT handle (simple circle)
    painter.setPen(QPen(icon_color, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPoint(left_handle_cx, handle_cy), 4, 4);

    // Draw resize icon on RIGHT handle (vertical line spanning the base)
    painter.setPen(QPen(icon_color, 1.5));
    painter.drawLine(right_handle_cx, BASE_TOP_Y + 2, right_handle_cx, BASE_BOTTOM_Y - 2);

    // Draw tuner label if enabled
    if (m_ShowTunerNames || m_ShowTunerFrequencies) {
        drawTunerLabel(painter, marker, center_x, plot_top + LABEL_OFFSET_Y);
    }
}

void MultiTunerPlotter::drawTunerFilter(QPainter& painter, const TunerMarker& marker)
{
    qint64 filter_low_freq = marker.frequency + marker.filter_low;
    qint64 filter_high_freq = marker.frequency + marker.filter_high;

    int filter_left = frequencyToScreenX(filter_low_freq);
    int filter_right = frequencyToScreenX(filter_high_freq);

    // Clamp to visible area
    int clamped_left = std::max(0, filter_left);
    int clamped_right = std::min(width() - 1, filter_right);

    if (clamped_right <= clamped_left) return;

    // Calculate FFT area bounds (below any overlapping bookmarks)
    int plot_top = getBookmarkTagsBottomAtX(clamped_left, clamped_right);
    int fft_height = getFftHeight() - plot_top;

    // Draw semi-transparent filter overlay
    QColor filter_color = marker.filter_color;
    filter_color.setAlpha(marker.alpha);

    painter.fillRect(clamped_left, plot_top, clamped_right - clamped_left,
                    fft_height, QBrush(filter_color));
}

void MultiTunerPlotter::drawTunerLabel(QPainter& painter, const TunerMarker& marker, int x, int y)
{
    QString label_text;

    if (m_ShowTunerNames && !marker.name.isEmpty()) {
        label_text = marker.name;
    } else {
        label_text = QString("T%1").arg(marker.tuner_id);
    }

    if (m_ShowTunerFrequencies) {
        if (!label_text.isEmpty()) label_text += "\n";

        // Format frequency in appropriate units
        double freq_mhz = marker.frequency / 1000000.0;
        if (freq_mhz >= 1000.0) {
            label_text += QString("%1 GHz").arg(freq_mhz / 1000.0, 0, 'f', 3);
        } else if (freq_mhz >= 1.0) {
            label_text += QString("%1 MHz").arg(freq_mhz, 0, 'f', 3);
        } else {
            label_text += QString("%1 kHz").arg(marker.frequency / 1000.0, 0, 'f', 1);
        }
    }

    if (label_text.isEmpty()) return;

    // Set font and calculate text size
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect text_rect = fm.boundingRect(QRect(), Qt::AlignCenter | Qt::TextWordWrap, label_text);

    // Position text box
    int text_x = x - text_rect.width() / 2;
    int text_y = y - text_rect.height();

    // Adjust if text would go off screen
    if (text_x < 0) text_x = 0;
    if (text_x + text_rect.width() >= width()) text_x = width() - text_rect.width() - 1;
    if (text_y < 0) text_y = 20; // Move below marker instead

    // Draw text background
    QRect bg_rect(text_x - 2, text_y - 2, text_rect.width() + 4, text_rect.height() + 4);
    painter.fillRect(bg_rect, QBrush(QColor(0, 0, 0, 180)));

    // Draw text
    painter.setPen(QPen(Qt::white));
    painter.drawText(QRect(text_x, text_y, text_rect.width(), text_rect.height()),
                    Qt::AlignCenter | Qt::TextWordWrap, label_text);
}

QColor MultiTunerPlotter::getDefaultTunerColor(int tuner_id) const
{
    return DEFAULT_TUNER_COLORS[tuner_id % NUM_DEFAULT_COLORS];
}

int MultiTunerPlotter::frequencyToScreenX(qint64 frequency) const
{
    // xFromFreq returns DPR-scaled coordinates, convert to logical pixels
    qreal dpr = devicePixelRatioF();
    return qRound(xFromFreq(frequency) / dpr);
}

qint64 MultiTunerPlotter::screenXToFrequency(int x) const
{
    // freqFromX expects DPR-scaled coordinates, convert from logical pixels
    qreal dpr = devicePixelRatioF();
    return freqFromX(qRound(x * dpr));
}

void MultiTunerPlotter::updateTooltipForTuner(int tuner_id, const QPoint& pos)
{
    auto it = m_TunerMarkers.find(tuner_id);
    if (it == m_TunerMarkers.end()) return;

    const TunerMarker& marker = it->second;

    QString tooltip = QString("Tuner %1").arg(tuner_id);
    if (!marker.name.isEmpty()) {
        tooltip = marker.name;
    }

    // Add frequency information
    double freq_mhz = marker.frequency / 1000000.0;
    tooltip += QString("\nFreq: %1 MHz").arg(freq_mhz, 0, 'f', 3);

    // Add filter information
    tooltip += QString("\nFilter: %1 to %2 Hz")
               .arg(marker.filter_low)
               .arg(marker.filter_high);

    // Add status
    tooltip += QString("\nStatus: %1").arg(marker.enabled ? "Enabled" : "Disabled");
    if (marker.active) {
        tooltip += " (Active)";
    }

    QToolTip::showText(mapToGlobal(pos), tooltip, this);
}
