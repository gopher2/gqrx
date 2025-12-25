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
#include "tuner_list.h"
#include "bookmarks.h"
#include "applications/gqrx/tuner_manager.h"
#include "applications/gqrx/receiver_channel.h"
#include "applications/gqrx/filename_template.h"
#include <QMessageBox>
#include <QSettings>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <algorithm>

// ============================================================================
// RssiMeterWidget - Custom RSSI meter with draggable squelch
// ============================================================================

RssiMeterWidget::RssiMeterWidget(QWidget *parent)
    : QWidget(parent)
    , m_rssi(-100.0f)
    , m_squelch(-150.0)
    , m_min_db(-100.0f)
    , m_max_db(-20.0f)
    , m_dragging(false)
    , m_drag_offset(0)
{
    setMinimumHeight(16);
    setMaximumHeight(20);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
}

void RssiMeterWidget::setRssi(float level_db)
{
    m_rssi = level_db;
    update();
}

void RssiMeterWidget::setSquelch(double level_db)
{
    m_squelch = qBound(static_cast<double>(m_min_db) - 50.0, level_db, static_cast<double>(m_max_db));
    update();
}

void RssiMeterWidget::setRange(float min_db, float max_db)
{
    m_min_db = min_db;
    m_max_db = max_db;
    update();
}

float RssiMeterWidget::dbToX(float db) const
{
    // Account for reserved space on right for value display
    int value_width = 32;
    int bar_width = width() - value_width - 4;
    float pct = (db - m_min_db) / (m_max_db - m_min_db);
    pct = qBound(0.0f, pct, 1.0f);
    return pct * bar_width;
}

float RssiMeterWidget::xToDb(int x) const
{
    // Account for reserved space on right for value display
    int value_width = 32;
    int bar_width = width() - value_width - 4;
    float pct = static_cast<float>(x) / static_cast<float>(bar_width);
    pct = qBound(0.0f, pct, 1.0f);
    return m_min_db + pct * (m_max_db - m_min_db);
}

QColor RssiMeterWidget::colorForLevel(float db) const
{
    if (db < -90.0f)
        return QColor(0x33, 0x33, 0x33);  // Very weak: dark gray
    else if (db < -80.0f)
        return QColor(0x60, 0x00, 0x00);  // Weak: dark red
    else if (db < -70.0f)
        return QColor(0xc0, 0x00, 0x00);  // Low: red
    else if (db < -60.0f)
        return QColor(0xf8, 0x80, 0x00);  // Moderate: orange
    else if (db < -50.0f)
        return QColor(0xcc, 0xc0, 0x00);  // Good: yellow
    else if (db < -40.0f)
        return QColor(0x8c, 0xc0, 0x00);  // Strong: yellow-green
    else
        return QColor(0x00, 0xc0, 0x00);  // Very strong: bright green
}

void RssiMeterWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();

    // Reserve space on right for current value display
    int value_width = 32;  // Space for "-100" text
    int bar_width = w - value_width - 4;  // Leave gap before value

    int bar_height = 6;
    int bar_y = h - bar_height - 2;  // Leave room for squelch handle above

    // Background for meter bar
    painter.fillRect(0, bar_y, bar_width, bar_height, QColor(0x22, 0x22, 0x22));

    // Draw RSSI level bar
    if (m_rssi > m_min_db) {
        float pct = (m_rssi - m_min_db) / (m_max_db - m_min_db);
        pct = qBound(0.0f, pct, 1.0f);
        int level_x = static_cast<int>(pct * bar_width);
        QColor level_color = colorForLevel(m_rssi);
        painter.fillRect(0, bar_y, level_x, bar_height, level_color);
    }

    // Draw scale labels (no tick marks)
    painter.setPen(QColor(0x55, 0x55, 0x55));
    QFont font = painter.font();
    font.setPixelSize(8);
    painter.setFont(font);

    // Labels every 20 dB
    for (int db = static_cast<int>(m_min_db); db <= static_cast<int>(m_max_db); db += 20) {
        float pct = (static_cast<float>(db) - m_min_db) / (m_max_db - m_min_db);
        int x = static_cast<int>(pct * bar_width);

        // Draw label above bar
        QString label = QString::number(db);
        int label_w = painter.fontMetrics().horizontalAdvance(label);
        int label_x = x - label_w / 2;
        // Clamp label to bar bounds
        label_x = qBound(0, label_x, bar_width - label_w);
        painter.drawText(label_x, 8, label);
    }

    // Draw squelch threshold marker (triangle/arrow pointing down)
    float sq_pct = (static_cast<float>(m_squelch) - m_min_db) / (m_max_db - m_min_db);
    sq_pct = qBound(0.0f, sq_pct, 1.0f);
    float sq_x = sq_pct * bar_width;

    if (m_squelch > static_cast<double>(m_min_db) - 40.0) {  // Only show if not at "off" position
        painter.setPen(Qt::NoPen);

        // Change color if dragging
        QColor sq_color = m_dragging ? QColor(0xff, 0xff, 0x00) : QColor(0xff, 0xff, 0xff);
        painter.setBrush(sq_color);

        // Draw triangle pointing down into the bar
        QPolygon triangle;
        int tx = static_cast<int>(sq_x);
        triangle << QPoint(tx, bar_y - 1)
                 << QPoint(tx - 4, bar_y - 6)
                 << QPoint(tx + 4, bar_y - 6);
        painter.drawPolygon(triangle);

        // Draw vertical line through bar
        painter.setPen(QPen(sq_color, 1));
        painter.drawLine(tx, bar_y, tx, bar_y + bar_height);
    }

    // Draw current dBFS value on the right (separate from scale)
    painter.setPen(QColor(0xaa, 0xaa, 0xaa));
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    QString rssi_text = QString::number(static_cast<int>(m_rssi));
    int text_w = painter.fontMetrics().horizontalAdvance(rssi_text);
    // Center vertically in the widget, right-aligned
    int text_x = w - text_w - 2;
    int text_y = h / 2 + 4;
    painter.drawText(text_x, text_y, rssi_text);
}

void RssiMeterWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if click is near the squelch marker
        float sq_x = dbToX(static_cast<float>(m_squelch));
        int click_x = event->pos().x();

        if (qAbs(click_x - sq_x) < 10) {
            // Start dragging
            m_dragging = true;
            m_drag_offset = click_x - static_cast<int>(sq_x);
            setCursor(Qt::ClosedHandCursor);
            emit squelchDragStarted();
            update();
        } else {
            // Click elsewhere - move squelch to that position
            double new_squelch = static_cast<double>(xToDb(click_x));
            new_squelch = qBound(static_cast<double>(m_min_db), new_squelch, static_cast<double>(m_max_db));
            m_squelch = new_squelch;
            emit squelchChanged(m_squelch);
            update();
        }
    }
}

void RssiMeterWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        int new_x = event->pos().x() - m_drag_offset;
        double new_squelch = static_cast<double>(xToDb(new_x));
        new_squelch = qBound(static_cast<double>(m_min_db), new_squelch, static_cast<double>(m_max_db));
        m_squelch = new_squelch;
        emit squelchChanged(m_squelch);
        update();
    } else {
        // Update cursor based on hover position
        float sq_x = dbToX(static_cast<float>(m_squelch));
        int hover_x = event->pos().x();
        if (qAbs(hover_x - sq_x) < 10) {
            setCursor(Qt::OpenHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void RssiMeterWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        emit squelchDragFinished();
        update();
    }
}

// ============================================================================
// TokenChip - A draggable token chip
// ============================================================================

TokenChip::TokenChip(const QString& token, QWidget *parent)
    : QFrame(parent)
    , m_token(token)
    , m_dragging(false)
{
    setFrameStyle(QFrame::StyledPanel | QFrame::Raised);
    setStyleSheet(
        "TokenChip {"
        "  background-color: #3a5a8a;"
        "  border: 1px solid #5a7aaa;"
        "  border-radius: 10px;"
        "  padding: 4px 8px;"
        "  margin: 2px;"
        "}"
        "TokenChip:hover { background-color: #4a6a9a; }"
    );
    setCursor(Qt::OpenHandCursor);
    setToolTip("Drag to reorder, drag out to remove");
}

void TokenChip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_press_pos = event->pos();
        m_dragging = false;
        setCursor(Qt::ClosedHandCursor);
    }
    QFrame::mousePressEvent(event);
}

void TokenChip::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;

    if (!m_dragging) {
        if ((event->pos() - m_press_pos).manhattanLength() >= 5) {
            m_dragging = true;
            emit dragStarted(this, event->globalPosition().toPoint());
        }
    } else {
        emit dragMoved(this, event->globalPosition().toPoint());
    }
}

void TokenChip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setCursor(Qt::OpenHandCursor);
        if (m_dragging) {
            m_dragging = false;
            emit dragFinished(this, event->globalPosition().toPoint());
        }
    }
    QFrame::mouseReleaseEvent(event);
}

void TokenChip::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw token text
    QFont font = painter.font();
    font.setFamily("monospace");
    font.setPointSize(10);
    painter.setFont(font);
    painter.setPen(Qt::white);

    QRect textRect = rect().adjusted(8, 4, -8, -4);
    painter.drawText(textRect, Qt::AlignCenter, m_token);
}

// ============================================================================
// TokenEditor - Pattern editor with draggable token chips
// ============================================================================

TokenEditor::TokenEditor(QWidget *parent)
    : QFrame(parent)
    , m_dragged_chip(nullptr)
    , m_drag_original_index(-1)
{
    setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    setStyleSheet(
        "TokenEditor {"
        "  background-color: #1a1a1a;"
        "  border: 1px solid #444;"
        "  border-radius: 4px;"
        "  min-height: 40px;"
        "}"
    );

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 4, 6, 4);
    m_layout->setSpacing(2);
    m_layout->addStretch();
}

void TokenEditor::setPattern(const QString& pattern)
{
    clear();

    // Parse pattern into tokens
    // Tokens are either {something} or literal characters
    QString current;
    bool in_token = false;

    for (int i = 0; i < pattern.length(); i++) {
        QChar c = pattern[i];
        if (c == '{') {
            // Save any accumulated literal text
            if (!current.isEmpty()) {
                addToken(current);
                current.clear();
            }
            in_token = true;
            current = "{";
        } else if (c == '}' && in_token) {
            current += '}';
            addToken(current);
            current.clear();
            in_token = false;
        } else if (c == '/' && !in_token) {
            // Slash is a special token for subdirectory
            if (!current.isEmpty()) {
                addToken(current);
                current.clear();
            }
            addToken("/");
        } else if (c == '_' && !in_token) {
            // Underscore is a special token
            if (!current.isEmpty()) {
                addToken(current);
                current.clear();
            }
            addToken("_");
        } else {
            current += c;
        }
    }

    // Add any remaining text
    if (!current.isEmpty()) {
        addToken(current);
    }
}

QString TokenEditor::pattern() const
{
    QString result;
    for (TokenChip* chip : m_chips) {
        result += chip->token();
    }
    return result;
}

void TokenEditor::addToken(const QString& token)
{
    TokenChip* chip = new TokenChip(token, this);

    // Size the chip to fit its content
    QFontMetrics fm(QFont("monospace", 10));
    int width = fm.horizontalAdvance(token) + 20;
    chip->setFixedSize(width, 26);

    connect(chip, &TokenChip::removeRequested, this, &TokenEditor::onChipRemoved);
    connect(chip, &TokenChip::dragStarted, this, &TokenEditor::onChipDragStarted);
    connect(chip, &TokenChip::dragMoved, this, &TokenEditor::onChipDragMoved);
    connect(chip, &TokenChip::dragFinished, this, &TokenEditor::onChipDragFinished);

    m_chips.append(chip);
    rebuildLayout();
    emit patternChanged(pattern());
}

void TokenEditor::clear()
{
    for (TokenChip* chip : m_chips) {
        chip->deleteLater();
    }
    m_chips.clear();
    rebuildLayout();
}

void TokenEditor::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    rebuildLayout();
}

void TokenEditor::onChipRemoved()
{
    TokenChip* chip = qobject_cast<TokenChip*>(sender());
    if (chip && m_chips.contains(chip)) {
        m_chips.removeOne(chip);
        chip->deleteLater();
        rebuildLayout();
        emit patternChanged(pattern());
    }
}

void TokenEditor::rebuildLayout()
{
    // Remove all items from layout
    while (m_layout->count() > 0) {
        m_layout->takeAt(0);
    }

    // Re-add chips
    for (TokenChip* chip : m_chips) {
        m_layout->addWidget(chip);
    }

    m_layout->addStretch();
}

void TokenEditor::onChipDragStarted(TokenChip* chip, QPoint globalPos)
{
    Q_UNUSED(globalPos);
    m_dragged_chip = chip;
    m_drag_original_index = m_chips.indexOf(chip);
    chip->raise();  // Bring to front during drag
}

void TokenEditor::onChipDragMoved(TokenChip* chip, QPoint globalPos)
{
    if (!m_dragged_chip || m_dragged_chip != chip)
        return;

    // Convert global position to local
    QPoint localPos = mapFromGlobal(globalPos);

    // Find where to insert
    int newIndex = findInsertIndex(localPos);
    int currentIndex = m_chips.indexOf(chip);

    // Move chip in list if position changed
    if (newIndex != currentIndex && newIndex != currentIndex + 1) {
        m_chips.removeAt(currentIndex);
        if (newIndex > currentIndex) newIndex--;
        m_chips.insert(newIndex, chip);
        rebuildLayout();
    }
}

void TokenEditor::onChipDragFinished(TokenChip* chip, QPoint globalPos)
{
    if (!m_dragged_chip || m_dragged_chip != chip)
        return;

    // Check if dropped outside the editor
    QPoint localPos = mapFromGlobal(globalPos);
    if (!rect().contains(localPos)) {
        // Dropped outside - remove the chip
        m_chips.removeOne(chip);
        chip->deleteLater();
        rebuildLayout();
        emit patternChanged(pattern());
    } else if (m_chips.indexOf(chip) != m_drag_original_index) {
        // Position changed - emit pattern changed
        emit patternChanged(pattern());
    }

    m_dragged_chip = nullptr;
    m_drag_original_index = -1;
}

int TokenEditor::findInsertIndex(const QPoint& localPos)
{
    int x = localPos.x();
    for (int i = 0; i < m_chips.size(); i++) {
        QWidget* chip = m_chips[i];
        int chipCenter = chip->geometry().center().x();
        if (x < chipCenter) {
            return i;
        }
    }
    return m_chips.size();
}

// ============================================================================
// TunerRowWidget
// ============================================================================

TunerRowWidget::TunerRowWidget(int tuner_id, const QString& name, ReceiverType type, const QColor& color, QWidget *parent)
    : QWidget(parent)
    , m_tuner_id(tuner_id)
    , m_receiver_type(type)
    , m_enabled(true)
    , m_status(TunerStatus::Stopped)
    , m_color(color)
    , m_alpha(25)  // Default ~10%
    , m_frequency(0)
    , m_volume(18)  // Default ~-15dB (10^(-15/20) * 100 = 17.8%)
    , m_muted(false)
    , m_filter_low(-5000)   // Default NFM bandwidth
    , m_filter_high(5000)
    , m_squelch(-150.0)  // Default squelch off
    , m_squelch_mode(0)   // Start with Auto mode
    , m_filter_preset(1)  // Normal
    , m_agc_preset(1)     // Medium
    , m_nb_state(0)       // Off
    , m_expanded(false)
    , m_rssi(-100.0f)     // Default low RSSI
    , m_recording(false)
    , m_recording_iq(false)
    , m_config_record_iq(false)
    , m_config_record_audio(true)
    , m_config_iq_mode(RecordingMode::CONSTANT)
    , m_config_audio_mode(RecordingMode::CONSTANT)
    , m_expanded_section(nullptr)
    , m_rssi_meter(nullptr)
    , m_row1_container(nullptr)
    , m_row1a_widget(nullptr)
    , m_row1b_widget(nullptr)
    , m_row1_layout(nullptr)
    , m_row2_container(nullptr)
    , m_row2a_widget(nullptr)
    , m_row2b_widget(nullptr)
    , m_row2_layout(nullptr)
    , m_is_stacked_layout(false)
    , m_last_layout_width(0)
{

    // Enable stylesheet support for custom widget
    setAttribute(Qt::WA_StyledBackground, true);

    // Main vertical layout for 2+ rows
    m_main_layout = new QVBoxLayout(this);
    m_main_layout->setContentsMargins(4, 2, 4, 2);
    m_main_layout->setSpacing(2);

    // === Row 1: Info (responsive - stacks when narrow) ===
    // Row 1a: color, name, type, bandwidth, status (buttons stay together)
    // Row 1b: freq (frequency display wraps to own row when narrow)

    m_row1_container = new QWidget(this);

    // Create Row 1a widget (buttons/controls)
    m_row1a_widget = new QWidget(m_row1_container);
    auto* row1a_layout = new QHBoxLayout(m_row1a_widget);
    row1a_layout->setContentsMargins(0, 0, 0, 0);
    row1a_layout->setSpacing(4);

    // Color button - shows current color
    m_color_btn = new QPushButton(this);
    m_color_btn->setFixedSize(20, 20);
    m_color_btn->setToolTip("Click to change tuner color");
    m_color_btn->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid gray; border-radius: 3px; }").arg(color.name()));
    connect(m_color_btn, &QPushButton::clicked, this, &TunerRowWidget::onColorClicked);
    row1a_layout->addWidget(m_color_btn);

    // Editable name - click to edit
    m_name_edit = new QLineEdit(name, this);
    m_name_edit->setFrame(false);
    m_name_edit->setStyleSheet("QLineEdit { background: transparent; }");
    m_name_edit->setMinimumWidth(40);
    connect(m_name_edit, &QLineEdit::editingFinished, this, &TunerRowWidget::onNameEditFinished);
    row1a_layout->addWidget(m_name_edit, 1);

    // Type button with dropdown menu to change mode
    m_type_btn = new QPushButton(receiverTypeName(type), this);
    m_type_btn->setStyleSheet("QPushButton { color: #888; font-weight: bold; border: none; text-align: left; padding: 4px 6px; } QPushButton:hover { background: #444; } QPushButton::menu-indicator { width: 0; height: 0; }");
    m_type_btn->setToolTip("Click to change receiver mode");

    // Create type selection menu
    m_type_menu = new QMenu(this);

    // Analog submenu
    QMenu* analogMenu = m_type_menu->addMenu("Analog");
    analogMenu->addAction("NFM")->setData(static_cast<int>(ReceiverType::ANALOG_NFM));
    analogMenu->addAction("AM")->setData(static_cast<int>(ReceiverType::ANALOG_AM));
    analogMenu->addAction("AM Sync")->setData(static_cast<int>(ReceiverType::ANALOG_AMSYNC));
    analogMenu->addSeparator();
    analogMenu->addAction("WFM Mono")->setData(static_cast<int>(ReceiverType::ANALOG_WFM_MONO));
    analogMenu->addAction("WFM Stereo")->setData(static_cast<int>(ReceiverType::ANALOG_WFM_STEREO));
    analogMenu->addSeparator();
    analogMenu->addAction("USB")->setData(static_cast<int>(ReceiverType::ANALOG_USB));
    analogMenu->addAction("LSB")->setData(static_cast<int>(ReceiverType::ANALOG_LSB));
    analogMenu->addAction("CW-U")->setData(static_cast<int>(ReceiverType::ANALOG_CW_U));
    analogMenu->addAction("CW-L")->setData(static_cast<int>(ReceiverType::ANALOG_CW_L));
    connect(analogMenu, &QMenu::triggered, this, &TunerRowWidget::onTypeSelected);

    m_type_btn->setMenu(m_type_menu);
    row1a_layout->addWidget(m_type_btn);

    // Bandwidth button - shows current filter width, click to change
    m_bandwidth_btn = new QPushButton("10k", this);
    m_bandwidth_btn->setStyleSheet("QPushButton { color: #888; font-size: 10px; border: none; padding: 2px 4px; } QPushButton:hover { background: #444; } QPushButton::menu-indicator { width: 0; height: 0; }");
    m_bandwidth_btn->setToolTip("Filter bandwidth - click to change");
    m_bandwidth_btn->setFixedHeight(20);

    // Create bandwidth selection menu with common values
    m_bandwidth_menu = new QMenu(this);
    // Menu shows total bandwidth, data stores half-bandwidth (for symmetric filters: -half to +half)

    // NFM/AM bandwidths
    m_bandwidth_menu->addAction("5 kHz")->setData(2500);
    m_bandwidth_menu->addAction("6.25 kHz")->setData(3125);
    m_bandwidth_menu->addAction("10 kHz")->setData(5000);
    m_bandwidth_menu->addAction("12.5 kHz")->setData(6250);
    m_bandwidth_menu->addAction("15 kHz")->setData(7500);
    m_bandwidth_menu->addAction("20 kHz")->setData(10000);
    m_bandwidth_menu->addAction("25 kHz")->setData(12500);
    m_bandwidth_menu->addSeparator();
    // WFM bandwidths
    m_bandwidth_menu->addAction("80 kHz")->setData(40000);
    m_bandwidth_menu->addAction("100 kHz")->setData(50000);
    m_bandwidth_menu->addAction("160 kHz")->setData(80000);
    m_bandwidth_menu->addAction("200 kHz")->setData(100000);
    m_bandwidth_menu->addSeparator();
    m_bandwidth_menu->addAction("Custom...")->setData(-1);  // Special marker for custom input
    connect(m_bandwidth_menu, &QMenu::triggered, this, &TunerRowWidget::onBandwidthSelected);
    m_bandwidth_btn->setMenu(m_bandwidth_menu);
    row1a_layout->addWidget(m_bandwidth_btn);

    // Status label (stopped/running) - stays with buttons
    m_status_label = new QLabel("stopped", this);
    m_status_label->setStyleSheet("QLabel { color: #f88; font-size: 10px; }");
    m_status_label->setFixedWidth(50);
    m_status_label->setAlignment(Qt::AlignCenter);
    row1a_layout->addWidget(m_status_label);

    // Create Row 1b widget (frequency display - wraps when narrow)
    m_row1b_widget = new QWidget(m_row1_container);
    auto* row1b_layout = new QHBoxLayout(m_row1b_widget);
    row1b_layout->setContentsMargins(0, 0, 0, 0);
    row1b_layout->setSpacing(0);

    // Frequency control - clickable digits for fine tuning
    m_freq_ctrl = new CFreqCtrl(this);
    m_freq_ctrl->setup(10, 0, 9999999999LL, 1, FCTL_UNIT_HZ);
    m_freq_ctrl->setDigitColor(QColor("#ccc"));
    m_freq_ctrl->setBgColor(QColor("#2a2a2a"));
    m_freq_ctrl->setHighlightColor(QColor("#555"));
    m_freq_ctrl->setFixedHeight(20);
    m_freq_ctrl->setMinimumWidth(120);
    m_freq_ctrl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_freq_ctrl, &CFreqCtrl::newFrequency, this, &TunerRowWidget::onFrequencyChanged);
    row1b_layout->addWidget(m_freq_ctrl);

    // Start with combined (single row) layout for Row 1
    m_row1_layout = new QHBoxLayout(m_row1_container);
    m_row1_layout->setContentsMargins(0, 0, 0, 0);
    m_row1_layout->setSpacing(4);
    m_row1_layout->addWidget(m_row1a_widget);
    m_row1_layout->addWidget(m_row1b_widget);

    m_main_layout->addWidget(m_row1_container);

    // === Row 2: Controls (responsive - single row or two rows) ===
    // Row 2a: [Sq] [Filter▾] [AGC▾] [NB] [V]
    // Row 2b: [⚙] [E] [C] [X]

    // Create container for all Row 2 controls
    m_row2_container = new QWidget(this);

    // Create container widget for Row 2a (left controls)
    m_row2a_widget = new QWidget(m_row2_container);
    auto* row2a_layout = new QHBoxLayout(m_row2a_widget);
    row2a_layout->setContentsMargins(0, 0, 0, 0);
    row2a_layout->setSpacing(2);

    // Squelch cycle button (Auto ↔ Reset)
    m_squelch_btn = new QPushButton("SQ", this);
    m_squelch_btn->setFixedSize(28, 24);
    m_squelch_btn->setToolTip("Squelch: Click to cycle Auto → Reset (off)");
    connect(m_squelch_btn, &QPushButton::clicked, this, &TunerRowWidget::onSquelchCycleClicked);
    row2a_layout->addWidget(m_squelch_btn);

    // Filter preset button (analog only)
    m_filter_btn = new QPushButton("N", this);  // N = Normal (default)
    m_filter_btn->setFixedSize(28, 24);
    m_filter_btn->setToolTip("Filter bandwidth preset");
    m_filter_btn->setStyleSheet("QPushButton::menu-indicator { width: 0; height: 0; }");

    m_filter_menu = new QMenu(this);
    QAction* filterWide = m_filter_menu->addAction("Wide");
    filterWide->setData(0);
    QAction* filterNormal = m_filter_menu->addAction("Normal");
    filterNormal->setData(1);
    QAction* filterNarrow = m_filter_menu->addAction("Narrow");
    filterNarrow->setData(2);
    QAction* filterUser = m_filter_menu->addAction("User");
    filterUser->setData(3);
    connect(m_filter_menu, &QMenu::triggered, this, &TunerRowWidget::onFilterPresetSelected);
    m_filter_btn->setMenu(m_filter_menu);
    row2a_layout->addWidget(m_filter_btn);

    // AGC preset button (analog only)
    m_agc_btn = new QPushButton("AGC", this);
    m_agc_btn->setFixedSize(36, 24);
    m_agc_btn->setToolTip("AGC preset");
    m_agc_btn->setStyleSheet("QPushButton::menu-indicator { width: 0; height: 0; }");

    m_agc_menu = new QMenu(this);
    QAction* agcFast = m_agc_menu->addAction("Fast");
    agcFast->setData(0);
    QAction* agcMedium = m_agc_menu->addAction("Medium");
    agcMedium->setData(1);
    QAction* agcSlow = m_agc_menu->addAction("Slow");
    agcSlow->setData(2);
    QAction* agcUser = m_agc_menu->addAction("User");
    agcUser->setData(3);
    m_agc_menu->addSeparator();
    QAction* agcOff = m_agc_menu->addAction("Off");
    agcOff->setData(4);
    connect(m_agc_menu, &QMenu::triggered, this, &TunerRowWidget::onAgcPresetSelected);
    m_agc_btn->setMenu(m_agc_menu);
    row2a_layout->addWidget(m_agc_btn);

    // Noise Blanker toggle button (analog only)
    m_nb_btn = new QPushButton("NB", this);
    m_nb_btn->setFixedSize(36, 24);
    m_nb_btn->setToolTip("Noise Blanker: Off → NB1 → NB2 → Both");
    m_nb_btn->setCheckable(true);
    connect(m_nb_btn, &QPushButton::clicked, this, &TunerRowWidget::onNbClicked);
    row2a_layout->addWidget(m_nb_btn);

    // Volume button with popup slider
    m_volume_btn = new QPushButton("V", this);
    m_volume_btn->setFixedSize(24, 24);
    m_volume_btn->setToolTip("Channel volume");
    m_volume_btn->setStyleSheet("QPushButton::menu-indicator { width: 0; height: 0; }");

    QMenu* volumeMenu = new QMenu(this);
    QWidgetAction* volumeSliderAction = new QWidgetAction(volumeMenu);
    QWidget* volumeSliderWidget = new QWidget(volumeMenu);
    QVBoxLayout* volumeSliderLayout = new QVBoxLayout(volumeSliderWidget);
    volumeSliderLayout->setContentsMargins(8, 8, 8, 8);

    // Volume label showing percentage
    m_volume_label = new QLabel(QString("%1%").arg(m_volume), volumeSliderWidget);
    m_volume_label->setAlignment(Qt::AlignCenter);
    m_volume_label->setMinimumWidth(50);
    volumeSliderLayout->addWidget(m_volume_label);

    m_volume_slider = new QSlider(Qt::Horizontal, volumeSliderWidget);
    m_volume_slider->setFixedWidth(120);
    m_volume_slider->setRange(0, 150);  // Allow +3.5dB boost above unity
    m_volume_slider->setValue(m_volume);
    connect(m_volume_slider, &QSlider::valueChanged, this, &TunerRowWidget::onVolumeChanged);
    volumeSliderLayout->addWidget(m_volume_slider);

    volumeSliderWidget->setLayout(volumeSliderLayout);
    volumeSliderAction->setDefaultWidget(volumeSliderWidget);
    volumeMenu->addAction(volumeSliderAction);
    m_volume_btn->setMenu(volumeMenu);
    row2a_layout->addWidget(m_volume_btn);

    // Mute button
    m_mute_btn = new QPushButton("M", this);
    m_mute_btn->setFixedSize(24, 24);
    m_mute_btn->setToolTip("Mute/Unmute channel");
    m_mute_btn->setCheckable(true);
    connect(m_mute_btn, &QPushButton::clicked, this, &TunerRowWidget::onMuteClicked);
    row2a_layout->addWidget(m_mute_btn);

    // Record button
    m_record_btn = new QPushButton("●", this);
    m_record_btn->setFixedSize(24, 24);
    m_record_btn->setToolTip("Start/Stop recording");
    m_record_btn->setCheckable(true);
    m_record_btn->setStyleSheet("QPushButton { font-size: 11px; }");
    connect(m_record_btn, &QPushButton::clicked, this, &TunerRowWidget::onRecordClicked);
    row2a_layout->addWidget(m_record_btn);

    // Recording indicator (duration display)
    m_recording_indicator = new QLabel(this);
    m_recording_indicator->setStyleSheet("QLabel { color: #cc0000; font-size: 10px; font-weight: bold; }");
    m_recording_indicator->setFixedWidth(55);
    m_recording_indicator->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_recording_indicator->hide();  // Hidden until recording starts
    row2a_layout->addWidget(m_recording_indicator);

    row2a_layout->addStretch();

    // Create container widget for Row 2b (right controls)
    m_row2b_widget = new QWidget(m_row2_container);
    auto* row2b_layout = new QHBoxLayout(m_row2b_widget);
    row2b_layout->setContentsMargins(0, 0, 0, 0);
    row2b_layout->setSpacing(2);

    row2b_layout->addStretch();

    // Gear button (opens modal settings dialog)
    m_gear_btn = new QPushButton("⚙", this);
    m_gear_btn->setFixedSize(24, 24);
    m_gear_btn->setToolTip("Advanced settings");
    m_gear_btn->setStyleSheet("QPushButton { font-size: 13px; }");
    connect(m_gear_btn, &QPushButton::clicked, this, &TunerRowWidget::onGearClicked);
    row2b_layout->addWidget(m_gear_btn);

    // Enable/disable toggle button
    m_enable_btn = new QPushButton(this);
    m_enable_btn->setFixedSize(24, 24);
    m_enable_btn->setCheckable(true);
    m_enable_btn->setChecked(true);
    m_enable_btn->setText("E");
    m_enable_btn->setToolTip("Enable/disable tuner (E=enabled, D=disabled)");
    connect(m_enable_btn, &QPushButton::clicked, this, &TunerRowWidget::onEnableClicked);
    row2b_layout->addWidget(m_enable_btn);

    // Bookmark button
    m_bookmark_btn = new QPushButton("B", this);
    m_bookmark_btn->setFixedSize(24, 24);
    m_bookmark_btn->setToolTip("Add/update bookmark for this frequency");
    connect(m_bookmark_btn, &QPushButton::clicked, this, &TunerRowWidget::onBookmarkClicked);
    row2b_layout->addWidget(m_bookmark_btn);

    // Center button (center FFT on channel without changing zoom)
    m_center_btn = new QPushButton("C", this);
    m_center_btn->setFixedSize(24, 24);
    m_center_btn->setToolTip("Center FFT on this tuner");
    connect(m_center_btn, &QPushButton::clicked, this, &TunerRowWidget::onCenterClicked);
    row2b_layout->addWidget(m_center_btn);

    // Zoom button (cycle through zoom levels centered on channel)
    m_zoom_btn = new QPushButton("Z", this);
    m_zoom_btn->setFixedSize(24, 24);
    m_zoom_btn->setToolTip("Cycle zoom levels centered on this tuner");
    connect(m_zoom_btn, &QPushButton::clicked, this, &TunerRowWidget::onZoomClicked);
    row2b_layout->addWidget(m_zoom_btn);

    // Close button
    m_close_btn = new QPushButton("X", this);
    m_close_btn->setFixedSize(24, 24);
    m_close_btn->setToolTip("Remove tuner");
    connect(m_close_btn, &QPushButton::clicked, this, &TunerRowWidget::onCloseClicked);
    row2b_layout->addWidget(m_close_btn);

    // Start with combined (single row) layout
    m_row2_layout = new QHBoxLayout(m_row2_container);
    m_row2_layout->setContentsMargins(0, 0, 0, 0);
    m_row2_layout->setSpacing(4);
    m_row2_layout->addWidget(m_row2a_widget);
    m_row2_layout->addWidget(m_row2b_widget);
    m_is_stacked_layout = false;

    m_main_layout->addWidget(m_row2_container);

    // === RSSI Meter with draggable squelch and dBFS scale ===
    m_rssi_meter = new RssiMeterWidget(this);
    m_rssi_meter->setRange(-100.0f, -20.0f);
    m_rssi_meter->setSquelch(m_squelch);
    connect(m_rssi_meter, &RssiMeterWidget::squelchChanged, this, &TunerRowWidget::onSquelchDragged);
    m_main_layout->addWidget(m_rssi_meter);

    // === Expanded Section (hidden by default) ===
    m_expanded_section = new QWidget(this);
    m_expanded_section->setVisible(false);
    auto* expanded_layout = new QVBoxLayout(m_expanded_section);
    expanded_layout->setContentsMargins(20, 4, 4, 4);  // Indent from left
    expanded_layout->setSpacing(4);

    // Row 3: Filter shape | NB1 threshold | NB2 threshold
    auto* row3_layout = new QHBoxLayout();
    row3_layout->setSpacing(8);

    // Filter shape selector
    auto* filter_shape_label = new QLabel("Shape:", m_expanded_section);
    filter_shape_label->setStyleSheet("QLabel { color: #888; }");
    row3_layout->addWidget(filter_shape_label);

    m_filter_shape_btn = new QPushButton("Normal", m_expanded_section);
    m_filter_shape_btn->setFixedWidth(60);
    m_filter_shape_btn->setToolTip("Filter roll-off shape");
    m_filter_shape_btn->setStyleSheet("QPushButton::menu-indicator { width: 0; height: 0; }");

    QMenu* shapeMenu = new QMenu(m_expanded_section);
    QAction* shapeSoft = shapeMenu->addAction("Soft");
    shapeSoft->setData(0);
    QAction* shapeNormal = shapeMenu->addAction("Normal");
    shapeNormal->setData(1);
    QAction* shapeSharp = shapeMenu->addAction("Sharp");
    shapeSharp->setData(2);
    connect(shapeMenu, &QMenu::triggered, this, &TunerRowWidget::onFilterShapeSelected);
    m_filter_shape_btn->setMenu(shapeMenu);
    row3_layout->addWidget(m_filter_shape_btn);

    row3_layout->addSpacing(12);

    // NB1 threshold
    auto* nb1_label = new QLabel("NB1:", m_expanded_section);
    nb1_label->setStyleSheet("QLabel { color: #888; }");
    row3_layout->addWidget(nb1_label);

    m_nb1_slider = new QSlider(Qt::Horizontal, m_expanded_section);
    m_nb1_slider->setFixedWidth(80);
    m_nb1_slider->setRange(0, 100);
    m_nb1_slider->setValue(50);
    m_nb1_slider->setToolTip("NB1 threshold (0-100)");
    connect(m_nb1_slider, &QSlider::valueChanged, this, &TunerRowWidget::onNb1ThresholdChanged);
    row3_layout->addWidget(m_nb1_slider);

    // NB2 threshold
    auto* nb2_label = new QLabel("NB2:", m_expanded_section);
    nb2_label->setStyleSheet("QLabel { color: #888; }");
    row3_layout->addWidget(nb2_label);

    m_nb2_slider = new QSlider(Qt::Horizontal, m_expanded_section);
    m_nb2_slider->setFixedWidth(80);
    m_nb2_slider->setRange(0, 100);
    m_nb2_slider->setValue(50);
    m_nb2_slider->setToolTip("NB2 threshold (0-100)");
    connect(m_nb2_slider, &QSlider::valueChanged, this, &TunerRowWidget::onNb2ThresholdChanged);
    row3_layout->addWidget(m_nb2_slider);

    row3_layout->addStretch();
    expanded_layout->addLayout(row3_layout);

    // Row 4: AGC settings
    auto* row4_layout = new QHBoxLayout();
    row4_layout->setSpacing(8);

    auto* agc_label = new QLabel("AGC:", m_expanded_section);
    agc_label->setStyleSheet("QLabel { color: #888; }");
    row4_layout->addWidget(agc_label);

    // Hang checkbox
    m_agc_hang_btn = new QPushButton("Hang", m_expanded_section);
    m_agc_hang_btn->setFixedWidth(50);
    m_agc_hang_btn->setCheckable(true);
    m_agc_hang_btn->setToolTip("AGC hang mode");
    connect(m_agc_hang_btn, &QPushButton::toggled, this, &TunerRowWidget::onAgcHangToggled);
    row4_layout->addWidget(m_agc_hang_btn);

    // Threshold
    auto* thresh_label = new QLabel("Thr:", m_expanded_section);
    thresh_label->setStyleSheet("QLabel { color: #888; }");
    row4_layout->addWidget(thresh_label);

    m_agc_threshold_slider = new QSlider(Qt::Horizontal, m_expanded_section);
    m_agc_threshold_slider->setFixedWidth(60);
    m_agc_threshold_slider->setRange(-160, 0);
    m_agc_threshold_slider->setValue(-100);
    m_agc_threshold_slider->setToolTip("AGC threshold");
    connect(m_agc_threshold_slider, &QSlider::valueChanged, this, &TunerRowWidget::onAgcThresholdChanged);
    row4_layout->addWidget(m_agc_threshold_slider);

    // Decay
    auto* decay_label = new QLabel("Dec:", m_expanded_section);
    decay_label->setStyleSheet("QLabel { color: #888; }");
    row4_layout->addWidget(decay_label);

    m_agc_decay_slider = new QSlider(Qt::Horizontal, m_expanded_section);
    m_agc_decay_slider->setFixedWidth(60);
    m_agc_decay_slider->setRange(10, 5000);
    m_agc_decay_slider->setValue(500);
    m_agc_decay_slider->setToolTip("AGC decay (ms)");
    connect(m_agc_decay_slider, &QSlider::valueChanged, this, &TunerRowWidget::onAgcDecayChanged);
    row4_layout->addWidget(m_agc_decay_slider);

    // Gain
    auto* gain_label = new QLabel("Gain:", m_expanded_section);
    gain_label->setStyleSheet("QLabel { color: #888; }");
    row4_layout->addWidget(gain_label);

    m_agc_gain_slider = new QSlider(Qt::Horizontal, m_expanded_section);
    m_agc_gain_slider->setFixedWidth(60);
    m_agc_gain_slider->setRange(0, 100);
    m_agc_gain_slider->setValue(80);
    m_agc_gain_slider->setToolTip("AGC manual gain");
    connect(m_agc_gain_slider, &QSlider::valueChanged, this, &TunerRowWidget::onAgcGainChanged);
    row4_layout->addWidget(m_agc_gain_slider);

    row4_layout->addStretch();
    expanded_layout->addLayout(row4_layout);

    m_expanded_section->setLayout(expanded_layout);
    m_main_layout->addWidget(m_expanded_section);

    // Update visibility of analog-specific controls
    updateAnalogControlsVisibility();

    // Apply default bandwidth based on receiver type
    setReceiverType(type);

    setLayout(m_main_layout);
    setAutoFillBackground(true);
}

QString TunerRowWidget::name() const
{
    return m_name_edit->text();
}

void TunerRowWidget::setName(const QString& name)
{
    m_name_edit->setText(name);
}

void TunerRowWidget::setEnabled(bool enabled)
{
    m_enabled = enabled;
    m_enable_btn->setChecked(enabled);
    m_enable_btn->setText(enabled ? "E" : "D");

    // Update status when disabled - when enabled, caller should call setStatus with proper state
    if (!enabled) {
        setStatus(TunerStatus::Disabled);
    }
}

void TunerRowWidget::onNameEditFinished()
{

    emit nameChanged(m_tuner_id, m_name_edit->text());
}

void TunerRowWidget::onTypeSelected(QAction* action)
{
    if (!action) {
        return;
    }

    bool ok;
    int typeInt = action->data().toInt(&ok);
    if (!ok) {
        return;
    }

    ReceiverType newType = static_cast<ReceiverType>(typeInt);
    if (newType == m_receiver_type) {
        return;  // No change
    }

    setReceiverType(newType);
    emit receiverTypeChanged(m_tuner_id, newType);
}

void TunerRowWidget::onBandwidthSelected(QAction* action)
{
    if (!action) {
        return;
    }

    bool ok;
    int half_bw = action->data().toInt(&ok);
    if (!ok) {
        return;
    }

    // Handle custom input
    if (half_bw == -1) {
        // Show input dialog for custom bandwidth
        int current_bw = m_filter_high - m_filter_low;
        double current_khz = current_bw / 1000.0;

        double new_khz = QInputDialog::getDouble(this, "Custom Bandwidth",
            "Enter filter bandwidth (kHz):", current_khz, 0.1, 500.0, 2, &ok);

        if (!ok) {
            return;
        }

        half_bw = static_cast<int>(new_khz * 500);  // half of kHz * 1000
    }

    // For symmetric filters (NFM, AM), set low = -half_bw, high = +half_bw
    int new_low = -half_bw;
    int new_high = half_bw;

    if (new_low == m_filter_low && new_high == m_filter_high) {
        return;  // No change
    }

    m_filter_low = new_low;
    m_filter_high = new_high;

    // Update button text
    QString bw_text;
    int total_bw = m_filter_high - m_filter_low;
    if (total_bw >= 1000) {
        bw_text = QString("%1k").arg(total_bw / 1000.0, 0, 'g', 3);
    } else {
        bw_text = QString("%1").arg(total_bw);
    }
    m_bandwidth_btn->setText(bw_text);

    emit filterWidthChanged(m_tuner_id, m_filter_low, m_filter_high);
}

void TunerRowWidget::onEnableClicked()
{
    m_enabled = m_enable_btn->isChecked();
    m_enable_btn->setText(m_enabled ? "E" : "D");

    // Update status immediately for visual feedback
    if (!m_enabled) {
        setStatus(TunerStatus::Disabled);
    }
    // When enabling, status will be updated by MainWindow after checking DSP/bypass state

    emit enableToggled(m_tuner_id, m_enabled);
}

void TunerRowWidget::onColorClicked()
{
    QColor newColor = QColorDialog::getColor(m_color, this, tr("Select Tuner Color"));
    if (newColor.isValid() && newColor != m_color) {
        setColor(newColor);
        emit colorChanged(m_tuner_id, newColor);
    }
}

void TunerRowWidget::setColor(const QColor& color)
{
    m_color = color;
    m_color_btn->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid gray; border-radius: 3px; }").arg(color.name()));
}

void TunerRowWidget::setReceiverType(ReceiverType type)
{

    m_receiver_type = type;
    m_type_btn->setText(receiverTypeName(type));
    updateAnalogControlsVisibility();

    // Set default bandwidth based on receiver type
    int default_half_bw = 5000;  // Default 10kHz for NFM
    switch (type) {
    case ReceiverType::ANALOG_NFM:
        default_half_bw = 5000;   // 10 kHz
        break;
    case ReceiverType::ANALOG_AM:
    case ReceiverType::ANALOG_AMSYNC:
        default_half_bw = 5000;   // 10 kHz
        break;
    case ReceiverType::ANALOG_WFM_MONO:
    case ReceiverType::ANALOG_WFM_STEREO:
    case ReceiverType::ANALOG_WFM_STEREO_OIRT:
        default_half_bw = 100000; // 200 kHz
        break;
    case ReceiverType::ANALOG_USB:
    case ReceiverType::ANALOG_LSB:
        default_half_bw = 1500;   // 3 kHz
        break;
    case ReceiverType::ANALOG_CW_U:
    case ReceiverType::ANALOG_CW_L:
        default_half_bw = 250;    // 500 Hz
        break;
    default:
        default_half_bw = 5000;   // 10 kHz fallback
        break;
    }
    setFilterWidth(-default_half_bw, default_half_bw);
}

QString TunerRowWidget::receiverTypeName(ReceiverType type) const
{
    switch (type) {
    case ReceiverType::ANALOG_NFM:          return "NFM";
    case ReceiverType::ANALOG_AM:           return "AM";
    case ReceiverType::ANALOG_AMSYNC:       return "AM Sync";
    case ReceiverType::ANALOG_WFM_MONO:     return "WFM";
    case ReceiverType::ANALOG_WFM_STEREO:   return "WFM St";
    case ReceiverType::ANALOG_WFM_STEREO_OIRT: return "OIRT";
    case ReceiverType::ANALOG_USB:          return "USB";
    case ReceiverType::ANALOG_LSB:          return "LSB";
    case ReceiverType::ANALOG_CW_U:         return "CW-U";
    case ReceiverType::ANALOG_CW_L:         return "CW-L";
    default:                                return "?";
    }
}

void TunerRowWidget::setAlpha(int alpha)
{
    m_alpha = qBound(0, alpha, 255);
    // Alpha slider removed - alpha can still be changed via code
}

void TunerRowWidget::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    m_volume_slider->setValue(m_volume);
}

void TunerRowWidget::setFrequency(qint64 freq_hz)
{
    m_frequency = freq_hz;
    // Block signals to avoid emitting frequencyChanged when setting programmatically
    m_freq_ctrl->blockSignals(true);
    m_freq_ctrl->setFrequency(freq_hz);
    m_freq_ctrl->blockSignals(false);
}

void TunerRowWidget::setFilterWidth(int filter_low, int filter_high)
{
    m_filter_low = filter_low;
    m_filter_high = filter_high;

    // Update button text to show total bandwidth
    QString bw_text;
    int total_bw = m_filter_high - m_filter_low;
    if (total_bw >= 1000) {
        bw_text = QString("%1k").arg(total_bw / 1000.0, 0, 'g', 3);
    } else {
        bw_text = QString("%1").arg(total_bw);
    }
    m_bandwidth_btn->setText(bw_text);
}

void TunerRowWidget::setRssi(float level_db)
{
    m_rssi = level_db;
    if (m_rssi_meter) {
        m_rssi_meter->setRssi(level_db);
    }
}

void TunerRowWidget::setRunning(bool running)
{
    // Legacy method - calls setStatus with Running or Stopped
    setStatus(running ? TunerStatus::Running : TunerStatus::Stopped);
}

void TunerRowWidget::setStatus(TunerStatus status)
{
    m_status = status;
    switch (status) {
    case TunerStatus::Disabled:
        m_status_label->setText("disabled");
        m_status_label->setStyleSheet("QLabel { color: #888; font-size: 10px; }");  // Gray
        m_status_label->setToolTip("Tuner disabled by user - click E to enable");
        break;
    case TunerStatus::Stopped:
        m_status_label->setText("stopped");
        m_status_label->setStyleSheet("QLabel { color: #f88; font-size: 10px; }");  // Red
        m_status_label->setToolTip("DSP not running - click Play to start");
        break;
    case TunerStatus::Bypassed:
        m_status_label->setText("bypassed");
        m_status_label->setStyleSheet("QLabel { color: #fa0; font-size: 10px; }");  // Orange
        m_status_label->setToolTip("Tuner frequency outside SDR bandwidth - tune SDR or move tuner");
        break;
    case TunerStatus::Running:
        m_status_label->setText("running");
        m_status_label->setStyleSheet("QLabel { color: #8f8; font-size: 10px; }");  // Green
        m_status_label->setToolTip("Tuner active and receiving");
        break;
    }
}

void TunerRowWidget::onVolumeChanged(int value)
{
    m_volume = value;
    // Update label with percentage (highlight if boosted above 100%)
    if (m_volume_label) {
        QString text = QString("%1%").arg(m_volume);
        if (m_volume > 100) {
            m_volume_label->setStyleSheet("QLabel { color: #f80; font-weight: bold; }");
        } else {
            m_volume_label->setStyleSheet("");
        }
        m_volume_label->setText(text);
    }
    emit volumeChanged(m_tuner_id, m_volume);
}

void TunerRowWidget::onMuteClicked()
{
    m_muted = m_mute_btn->isChecked();
    // Update button appearance
    m_mute_btn->setStyleSheet(m_muted ? "QPushButton { background-color: #a00; color: white; }" : "");
    emit muteToggled(m_tuner_id, m_muted);
}

void TunerRowWidget::setMuted(bool muted)
{
    m_muted = muted;
    m_mute_btn->setChecked(muted);
    m_mute_btn->setStyleSheet(muted ? "QPushButton { background-color: #a00; color: white; }" : "");
}

void TunerRowWidget::onRecordClicked()
{
    m_recording = m_record_btn->isChecked();
    // Update button appearance (preserve font-size)
    m_record_btn->setStyleSheet(m_recording
        ? "QPushButton { font-size: 11px; background-color: #a00; color: white; }"
        : "QPushButton { font-size: 11px; }");
    emit recordingToggled(m_tuner_id, m_recording);
}

void TunerRowWidget::setRecording(bool recording)
{
    m_recording = recording;
    m_record_btn->setChecked(recording);
    m_record_btn->setStyleSheet(recording
        ? "QPushButton { font-size: 11px; background-color: #a00; color: white; }"
        : "QPushButton { font-size: 11px; }");

    // Show/hide recording indicator
    if (recording || m_recording_iq) {
        m_recording_indicator->show();
        m_recording_indicator->setText("0:00");
    } else {
        m_recording_indicator->hide();
    }
}

void TunerRowWidget::setRecordingIq(bool recording)
{
    m_recording_iq = recording;

    // Show/hide recording indicator
    if (recording || m_recording) {
        m_recording_indicator->show();
        m_recording_indicator->setText("0:00");
    } else {
        m_recording_indicator->hide();
    }
}

void TunerRowWidget::updateRecordingInfo(double audio_duration, double iq_duration)
{
    if (!m_recording && !m_recording_iq) {
        m_recording_indicator->hide();
        return;
    }

    // Use the longer duration for display
    double duration = std::max(audio_duration, iq_duration);
    int total_seconds = static_cast<int>(duration);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;

    QString text;
    if (hours > 0) {
        text = QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    } else {
        text = QString("%1:%2")
            .arg(minutes)
            .arg(seconds, 2, 10, QChar('0'));
    }

    // Add IQ indicator if recording IQ
    if (m_recording_iq) {
        text += " IQ";
    }

    m_recording_indicator->setText(text);
    m_recording_indicator->show();
}

void TunerRowWidget::setRecordingConfig(bool record_iq, bool record_audio, RecordingMode iq_mode, RecordingMode audio_mode)
{
    m_config_record_iq = record_iq;
    m_config_record_audio = record_audio;
    m_config_iq_mode = iq_mode;
    m_config_audio_mode = audio_mode;
}

void TunerRowWidget::onCenterClicked()
{
    emit centerRequested(m_tuner_id);
}

void TunerRowWidget::onZoomClicked()
{
    emit zoomRequested(m_tuner_id);
}

void TunerRowWidget::onBookmarkClicked()
{


    // Check if a bookmark already exists at this exact frequency
    Bookmarks& bookmarks = Bookmarks::Get();
    int existing_index = -1;
    for (int i = 0; i < bookmarks.size(); i++) {
        if (bookmarks.getBookmark(i).frequency == m_frequency) {
            existing_index = i;
            break;
        }
    }

    if (existing_index >= 0) {
        // Bookmark exists - offer to update it

        BookmarkInfo& existing = bookmarks.getBookmark(existing_index);

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Update Bookmark");
        msgBox.setText(QString("A bookmark already exists at %1 MHz:\n\"%2\"")
                       .arg(m_frequency / 1e6, 0, 'f', 6)
                       .arg(existing.name));
        msgBox.setInformativeText("Do you want to update it with current tuner settings?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);

        if (msgBox.exec() == QMessageBox::Yes) {
            // Update the bookmark with current settings
            existing.bandwidth = std::abs(m_filter_high - m_filter_low);
            existing.modulation = m_type_btn->text();
            // Keep the existing name and tags
            bookmarks.save();
        }
    } else {
        // Create new bookmark - show tag selection dialog

        QDialog dialog(this);
        dialog.setWindowTitle("Add Bookmark");
        dialog.setMinimumWidth(300);

        QVBoxLayout* layout = new QVBoxLayout(&dialog);

        // Name input
        QFormLayout* formLayout = new QFormLayout();
        QLineEdit* nameEdit = new QLineEdit(&dialog);
        nameEdit->setText(m_name_edit->text());
        formLayout->addRow("Name:", nameEdit);

        // Frequency display (read-only)
        QLabel* freqLabel = new QLabel(QString("%1 MHz").arg(m_frequency / 1e6, 0, 'f', 6), &dialog);
        formLayout->addRow("Frequency:", freqLabel);

        // Modulation display
        QLabel* modLabel = new QLabel(m_type_btn->text(), &dialog);
        formLayout->addRow("Mode:", modLabel);

        // Bandwidth display
        qint64 bw = std::abs(m_filter_high - m_filter_low);
        QLabel* bwLabel = new QLabel(QString("%1 kHz").arg(bw / 1000.0, 0, 'f', 1), &dialog);
        formLayout->addRow("Bandwidth:", bwLabel);

        layout->addLayout(formLayout);

        // Tag selection
        QGroupBox* tagGroup = new QGroupBox("Tags", &dialog);
        QVBoxLayout* tagLayout = new QVBoxLayout(tagGroup);

        QList<TagInfo::sptr> allTags = bookmarks.getTagList();
        QList<QCheckBox*> tagCheckboxes;

        for (const auto& tag : allTags) {
            QCheckBox* cb = new QCheckBox(tag->name, tagGroup);
            cb->setChecked(false);
            tagCheckboxes.append(cb);
            tagLayout->addWidget(cb);
        }

        // Option to add new tag
        QHBoxLayout* newTagLayout = new QHBoxLayout();
        QLineEdit* newTagEdit = new QLineEdit(&dialog);
        newTagEdit->setPlaceholderText("New tag name...");
        QPushButton* addTagBtn = new QPushButton("+", &dialog);
        addTagBtn->setFixedWidth(30);
        newTagLayout->addWidget(newTagEdit);
        newTagLayout->addWidget(addTagBtn);
        tagLayout->addLayout(newTagLayout);

        layout->addWidget(tagGroup);

        // Connect add tag button
        connect(addTagBtn, &QPushButton::clicked, [&]() {
            QString newTagName = newTagEdit->text().trimmed();
            if (!newTagName.isEmpty()) {
                QCheckBox* cb = new QCheckBox(newTagName, tagGroup);
                cb->setChecked(true);
                tagCheckboxes.append(cb);
                tagLayout->insertWidget(tagLayout->count() - 1, cb);
                newTagEdit->clear();
            }
        });

        // Dialog buttons
        QDialogButtonBox* buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttonBox);

        if (dialog.exec() == QDialog::Accepted) {
            // Create the bookmark
            BookmarkInfo newBookmark;
            newBookmark.frequency = m_frequency;
            newBookmark.name = nameEdit->text();
            newBookmark.modulation = m_type_btn->text();
            newBookmark.bandwidth = std::abs(m_filter_high - m_filter_low);

            // Add selected tags
            for (int i = 0; i < tagCheckboxes.size(); i++) {
                if (tagCheckboxes[i]->isChecked()) {
                    QString tagName = tagCheckboxes[i]->text();
                    TagInfo::sptr tag = bookmarks.findOrAddTag(tagName);
                    newBookmark.tags.append(tag);
                }
            }

            // If no tags selected, add "Untagged"
            if (newBookmark.tags.isEmpty()) {
                TagInfo::sptr untagged = bookmarks.findOrAddTag(TagInfo::strUntagged);
                newBookmark.tags.append(untagged);
            }

            bookmarks.add(newBookmark);
            bookmarks.save();

        }
    }
}

void TunerRowWidget::onCloseClicked()
{
    emit closeRequested(m_tuner_id);
}

void TunerRowWidget::onFrequencyChanged(qint64 freq)
{
    m_frequency = freq;
    emit frequencyChanged(m_tuner_id, freq);
}

// === New Settings Methods ===

bool TunerRowWidget::isAnalogType() const
{
    switch (m_receiver_type) {
    case ReceiverType::ANALOG_OFF:
    case ReceiverType::ANALOG_RAW:
    case ReceiverType::ANALOG_AM:
    case ReceiverType::ANALOG_AMSYNC:
    case ReceiverType::ANALOG_NFM:
    case ReceiverType::ANALOG_WFM_MONO:
    case ReceiverType::ANALOG_WFM_STEREO:
    case ReceiverType::ANALOG_WFM_STEREO_OIRT:
    case ReceiverType::ANALOG_LSB:
    case ReceiverType::ANALOG_USB:
    case ReceiverType::ANALOG_CW_L:
    case ReceiverType::ANALOG_CW_U:
        return true;
    default:
        return false;
    }
}

void TunerRowWidget::updateAnalogControlsVisibility()
{
    bool analog = isAnalogType();
    m_filter_btn->setVisible(analog);
    m_agc_btn->setVisible(analog);
    m_nb_btn->setVisible(analog);
}

void TunerRowWidget::setSquelch(double level_db)
{
    m_squelch = qBound(-150.0, level_db, 0.0);
    // Update squelch mode based on level: if reset to -150, next click should be Auto
    m_squelch_mode = (m_squelch <= -150.0) ? 0 : 1;
    m_squelch_btn->setToolTip(m_squelch_mode == 0 ? "Squelch: Click for Auto" : "Squelch: Click to Reset (off)");

    // Update squelch marker on RSSI meter
    if (m_rssi_meter) {
        m_rssi_meter->setSquelch(m_squelch);
    }
}

void TunerRowWidget::setFilterPreset(int preset)
{
    m_filter_preset = qBound(0, preset, 3);
    static const char* labels[] = {"W", "N", "Na", "U"};
    m_filter_btn->setText(labels[m_filter_preset]);
}

void TunerRowWidget::setAgcPreset(int preset)
{
    m_agc_preset = qBound(0, preset, 4);
    static const char* labels[] = {"F", "M", "S", "U", "O"};
    m_agc_btn->setText(labels[m_agc_preset]);
}

void TunerRowWidget::setNbState(int state)
{
    m_nb_state = qBound(0, state, 3);
    m_nb_btn->setChecked(m_nb_state > 0);

    // Update button text to show current state
    switch (m_nb_state) {
    case 0: m_nb_btn->setText("NB"); break;
    case 1: m_nb_btn->setText("NB1"); break;
    case 2: m_nb_btn->setText("NB2"); break;
    case 3: m_nb_btn->setText("NB+"); break;
    }
}

void TunerRowWidget::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_gear_btn->setChecked(expanded);
    if (m_expanded_section) {
        m_expanded_section->setVisible(expanded);
    }
}

void TunerRowWidget::onSquelchCycleClicked()
{
    // Cycle: Auto (set based on signal) → Reset (off at -150 dB)
    if (m_squelch_mode == 0) {
        // Do Auto squelch
        emit autoSquelchRequested(m_tuner_id);
        m_squelch_mode = 1;
        m_squelch_btn->setText("Sq");
        m_squelch_btn->setToolTip("Squelch: Click to Reset (off)");
    } else {
        // Reset squelch to off (-150 dB)
        m_squelch = -150.0;
        emit squelchChanged(m_tuner_id, m_squelch);
        m_squelch_mode = 0;
        m_squelch_btn->setText("Sq");
        m_squelch_btn->setToolTip("Squelch: Click for Auto");
        // Update meter display
        if (m_rssi_meter) {
            m_rssi_meter->setSquelch(m_squelch);
        }
    }
}

void TunerRowWidget::onSquelchDragged(double level_db)
{
    // Called when user drags the squelch marker on the RSSI meter
    m_squelch = qBound(-150.0, level_db, 0.0);
    m_squelch_mode = (m_squelch <= -150.0) ? 0 : 1;
    m_squelch_btn->setToolTip(m_squelch_mode == 0 ? "Squelch: Click for Auto" : "Squelch: Click to Reset (off)");
    emit squelchChanged(m_tuner_id, m_squelch);
}

void TunerRowWidget::onFilterPresetSelected(QAction* action)
{
    if (!action) return;

    bool ok;
    int preset = action->data().toInt(&ok);
    if (!ok) return;

    setFilterPreset(preset);
    emit filterPresetChanged(m_tuner_id, preset);
}

void TunerRowWidget::onAgcPresetSelected(QAction* action)
{
    if (!action) return;

    bool ok;
    int preset = action->data().toInt(&ok);
    if (!ok) return;

    setAgcPreset(preset);
    emit agcPresetChanged(m_tuner_id, preset);
}

void TunerRowWidget::onNbClicked()
{
    // Cycle through: Off (0) -> NB1 (1) -> NB2 (2) -> Both (3) -> Off (0)
    m_nb_state = (m_nb_state + 1) % 4;
    setNbState(m_nb_state);
    emit nbStateChanged(m_tuner_id, m_nb_state);
}

void TunerRowWidget::onGearClicked()
{

    // Create modal settings dialog
    QDialog dialog(this);
    dialog.setWindowTitle(QString("Settings - %1").arg(m_name_edit->text()));
    dialog.setModal(true);
    dialog.setMinimumWidth(350);

    auto* mainLayout = new QVBoxLayout(&dialog);

    // Filter section
    auto* filterGroup = new QGroupBox("Filter", &dialog);
    auto* filterLayout = new QFormLayout(filterGroup);

    auto* filterShapeCombo = new QComboBox(&dialog);
    filterShapeCombo->addItem("Soft", 0);
    filterShapeCombo->addItem("Normal", 1);
    filterShapeCombo->addItem("Sharp", 2);
    filterShapeCombo->setCurrentIndex(1);  // Default Normal
    filterLayout->addRow("Shape:", filterShapeCombo);

    mainLayout->addWidget(filterGroup);

    // Noise Blanker section (analog only)
    if (isAnalogType()) {
        auto* nbGroup = new QGroupBox("Noise Blanker", &dialog);
        auto* nbLayout = new QFormLayout(nbGroup);

        auto* nb1Slider = new QSlider(Qt::Horizontal, &dialog);
        nb1Slider->setRange(0, 100);
        nb1Slider->setValue(m_nb1_slider ? m_nb1_slider->value() : 50);
        nbLayout->addRow("NB1 Threshold:", nb1Slider);

        auto* nb2Slider = new QSlider(Qt::Horizontal, &dialog);
        nb2Slider->setRange(0, 100);
        nb2Slider->setValue(m_nb2_slider ? m_nb2_slider->value() : 50);
        nbLayout->addRow("NB2 Threshold:", nb2Slider);

        mainLayout->addWidget(nbGroup);

        // Connect NB sliders
        connect(nb1Slider, &QSlider::valueChanged, this, &TunerRowWidget::onNb1ThresholdChanged);
        connect(nb2Slider, &QSlider::valueChanged, this, &TunerRowWidget::onNb2ThresholdChanged);
    }

    // AGC section (analog only)
    if (isAnalogType()) {
        auto* agcGroup = new QGroupBox("AGC", &dialog);
        auto* agcLayout = new QFormLayout(agcGroup);

        auto* hangCheck = new QCheckBox(&dialog);
        hangCheck->setChecked(m_agc_hang_btn ? m_agc_hang_btn->isChecked() : false);
        agcLayout->addRow("Hang:", hangCheck);

        auto* threshSlider = new QSlider(Qt::Horizontal, &dialog);
        threshSlider->setRange(-120, 0);
        threshSlider->setValue(m_agc_threshold_slider ? m_agc_threshold_slider->value() : -100);
        agcLayout->addRow("Threshold:", threshSlider);

        auto* decaySlider = new QSlider(Qt::Horizontal, &dialog);
        decaySlider->setRange(20, 5000);
        decaySlider->setValue(m_agc_decay_slider ? m_agc_decay_slider->value() : 500);
        agcLayout->addRow("Decay (ms):", decaySlider);

        auto* gainSlider = new QSlider(Qt::Horizontal, &dialog);
        gainSlider->setRange(0, 100);
        gainSlider->setValue(m_agc_gain_slider ? m_agc_gain_slider->value() : 80);
        agcLayout->addRow("Manual Gain:", gainSlider);

        mainLayout->addWidget(agcGroup);

        // Connect AGC controls
        connect(hangCheck, &QCheckBox::toggled, this, &TunerRowWidget::onAgcHangToggled);
        connect(threshSlider, &QSlider::valueChanged, this, &TunerRowWidget::onAgcThresholdChanged);
        connect(decaySlider, &QSlider::valueChanged, this, &TunerRowWidget::onAgcDecayChanged);
        connect(gainSlider, &QSlider::valueChanged, this, &TunerRowWidget::onAgcGainChanged);
    }

    // Connect filter shape
    connect(filterShapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        emit filterShapeChanged(m_tuner_id, index);
    });

    // Recording section
    auto* recGroup = new QGroupBox("Recording", &dialog);
    auto* recLayout = new QFormLayout(recGroup);

    auto* recordIqCheck = new QCheckBox(&dialog);
    recordIqCheck->setChecked(m_config_record_iq);
    recordIqCheck->setToolTip("Enable IQ recording for this tuner");
    recLayout->addRow("Record IQ:", recordIqCheck);

    auto* recordAudioCheck = new QCheckBox(&dialog);
    recordAudioCheck->setChecked(m_config_record_audio);
    recordAudioCheck->setToolTip("Enable audio recording for this tuner");
    recLayout->addRow("Record Audio:", recordAudioCheck);

    auto* iqModeCombo = new QComboBox(&dialog);
    iqModeCombo->addItem("Constant", static_cast<int>(RecordingMode::CONSTANT));
    iqModeCombo->addItem("Per Call (squelch)", static_cast<int>(RecordingMode::SQUELCH_PER_CALL));
    iqModeCombo->addItem("Chunks (no silence)", static_cast<int>(RecordingMode::SQUELCH_CHUNKS));
    iqModeCombo->setCurrentIndex(iqModeCombo->findData(static_cast<int>(m_config_iq_mode)));
    iqModeCombo->setToolTip("CONSTANT: Record continuously\n"
                            "Per Call: New file per transmission\n"
                            "Chunks: Time-based files, only with activity");
    recLayout->addRow("IQ Mode:", iqModeCombo);

    auto* audioModeCombo = new QComboBox(&dialog);
    audioModeCombo->addItem("Constant", static_cast<int>(RecordingMode::CONSTANT));
    audioModeCombo->addItem("Per Call (squelch)", static_cast<int>(RecordingMode::SQUELCH_PER_CALL));
    audioModeCombo->addItem("Chunks (no silence)", static_cast<int>(RecordingMode::SQUELCH_CHUNKS));
    audioModeCombo->setCurrentIndex(audioModeCombo->findData(static_cast<int>(m_config_audio_mode)));
    audioModeCombo->setToolTip("CONSTANT: Record continuously\n"
                               "Per Call: New file per transmission\n"
                               "Chunks: Time-based files, only with activity");
    recLayout->addRow("Audio Mode:", audioModeCombo);

    mainLayout->addWidget(recGroup);

    // Close button
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Show dialog modally
    if (dialog.exec() == QDialog::Accepted) {
        // Update local config
        m_config_record_iq = recordIqCheck->isChecked();
        m_config_record_audio = recordAudioCheck->isChecked();
        m_config_iq_mode = static_cast<RecordingMode>(iqModeCombo->currentData().toInt());
        m_config_audio_mode = static_cast<RecordingMode>(audioModeCombo->currentData().toInt());

        // Emit recording config changes (this will persist to TunerManager/QSettings)
        emit tunerRecordingConfigChanged(m_tuner_id, m_config_record_iq, m_config_record_audio,
                                         m_config_iq_mode, m_config_audio_mode);
    }
}

void TunerRowWidget::onFilterShapeSelected(QAction* action)
{
    int shape = action->data().toInt();
    static const char* labels[] = {"Soft", "Normal", "Sharp"};
    if (shape >= 0 && shape <= 2) {
        m_filter_shape_btn->setText(labels[shape]);
    }
    emit filterShapeChanged(m_tuner_id, shape);
}

void TunerRowWidget::onNb1ThresholdChanged(int value)
{
    float threshold = value / 100.0f;  // Convert 0-100 to 0.0-1.0
    emit nb1ThresholdChanged(m_tuner_id, threshold);
}

void TunerRowWidget::onNb2ThresholdChanged(int value)
{
    float threshold = value / 100.0f;  // Convert 0-100 to 0.0-1.0
    emit nb2ThresholdChanged(m_tuner_id, threshold);
}

void TunerRowWidget::onAgcHangToggled(bool checked)
{
    emit agcHangChanged(m_tuner_id, checked);
}

void TunerRowWidget::onAgcThresholdChanged(int value)
{
    emit agcThresholdChanged(m_tuner_id, value);
}

void TunerRowWidget::onAgcDecayChanged(int value)
{
    emit agcDecayChanged(m_tuner_id, value);
}

void TunerRowWidget::onAgcGainChanged(int value)
{
    emit agcGainChanged(m_tuner_id, value);
}

void TunerRowWidget::resizeEvent(QResizeEvent* event)
{
    // Don't handle layout changes here - let TunerList control the layout
    // via updateLayoutForWidth(). The row's own resize events report stale
    // widths during layout transitions which causes layout thrashing.
    QWidget::resizeEvent(event);
}

void TunerRowWidget::updateLayoutForWidth(int width)
{
    // Public method to force layout update from parent
    m_last_layout_width = width;
    updateRowLayout(width);
}

void TunerRowWidget::updateRowLayout(int width)
{
    // Threshold for switching between single row and stacked layout
    // Row 1a: color(20) + name(40) + type(60) + bw(40) + status(50) + spacing = ~220px
    // Row 1b: freq(120)
    // Together they need ~350px, so stack when narrower than 320
    const int STACK_THRESHOLD = 320;

    bool should_stack = (width < STACK_THRESHOLD);


    // Only change layout if needed
    if (should_stack == m_is_stacked_layout) {
        return;
    }

    m_is_stacked_layout = should_stack;

    // === Update Row 1 layout ===
    m_row1_layout->removeWidget(m_row1a_widget);
    m_row1_layout->removeWidget(m_row1b_widget);
    delete m_row1_layout;

    if (should_stack) {
        m_row1_layout = new QVBoxLayout(m_row1_container);
        m_row1_layout->setContentsMargins(0, 0, 0, 0);
        m_row1_layout->setSpacing(2);
    } else {
        m_row1_layout = new QHBoxLayout(m_row1_container);
        m_row1_layout->setContentsMargins(0, 0, 0, 0);
        m_row1_layout->setSpacing(4);
    }
    m_row1_layout->addWidget(m_row1a_widget);
    m_row1_layout->addWidget(m_row1b_widget);

    // === Update Row 2 layout ===
    m_row2_layout->removeWidget(m_row2a_widget);
    m_row2_layout->removeWidget(m_row2b_widget);
    delete m_row2_layout;

    if (should_stack) {
        m_row2_layout = new QVBoxLayout(m_row2_container);
        m_row2_layout->setContentsMargins(0, 0, 0, 0);
        m_row2_layout->setSpacing(2);
    } else {
        m_row2_layout = new QHBoxLayout(m_row2_container);
        m_row2_layout->setContentsMargins(0, 0, 0, 0);
        m_row2_layout->setSpacing(4);
    }
    m_row2_layout->addWidget(m_row2a_widget);
    m_row2_layout->addWidget(m_row2b_widget);
}

// ============================================================================
// TunerList
// ============================================================================

TunerList::TunerList(QWidget *parent)
    : QWidget(parent)
    , m_last_width(0)
    , main_layout(nullptr)
    , tuner_rows_layout(nullptr)
    , tuner_rows_container(nullptr)
    , scroll_area(nullptr)
    , status_label(nullptr)
    , add_button(nullptr)
    , add_type_button(nullptr)
    , add_menu(nullptr)
    , settings_button(nullptr)
    , global_record_button(nullptr)
    , open_folder_button(nullptr)
    , m_global_recording(false)
    , tuner_manager(nullptr)
    , last_tuner_type(ReceiverType::ANALOG_NFM)
{

    // Set minimum width to ensure toolbar buttons are visible
    setMinimumWidth(280);

    main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(4, 4, 4, 4);
    main_layout->setSpacing(4);

    // Toolbar for buttons - wraps when narrow
    button_toolbar = new QToolBar(this);
    button_toolbar->setMovable(false);
    button_toolbar->setFloatable(false);
    button_toolbar->setIconSize(QSize(16, 16));
    button_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button_toolbar->setStyleSheet(
        "QToolBar { border: none; spacing: 4px; padding: 2px; }"
        "QToolButton {"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "  padding: 4px 6px;"
        "  background-color: #3a3a3a;"
        "  color: #eee;"
        "}"
        "QToolButton:hover { background-color: #4a4a4a; }"
        "QToolButton:pressed { background-color: #2a2a2a; }"
        "QToolButton::menu-indicator { image: none; width: 0; }"
        "QToolButton[popupMode=\"1\"]::menu-button { width: 0; border: none; }"
    );

    // Setup the dropdown menu
    setupAddMenu();

    // Common toolbar button style
    QString toolbarBtnStyle =
        "QToolButton {"
        "  font-size: 14px;"
        "  min-width: 28px;"
        "  min-height: 28px;"
        "  padding: 4px;"
        "}";

    // Main add button (on the left)
    add_button = new QToolButton(this);
    add_button->setToolTip("Click to add tuner");
    connect(add_button, &QToolButton::clicked, this, &TunerList::on_add_button_clicked);
    button_toolbar->addWidget(add_button);

    // Type selector dropdown (on the right of add button)
    add_type_button = new QToolButton(this);
    add_type_button->setToolTip("Select tuner type to add");
    add_type_button->setText("▼");
    add_type_button->setPopupMode(QToolButton::InstantPopup);
    add_type_button->setMenu(add_menu);
    button_toolbar->addWidget(add_type_button);

    // Load last used tuner type from settings
    QSettings settings;
    int savedType = settings.value("tuner/last_add_type", static_cast<int>(ReceiverType::ANALOG_NFM)).toInt();
    last_tuner_type = static_cast<ReceiverType>(savedType);
    add_button->setText(QString("+ %1").arg(receiverTypeName(last_tuner_type)));

    button_toolbar->addSeparator();

    // Master settings gear button
    settings_button = new QToolButton(this);
    settings_button->setText("⚙");
    settings_button->setToolTip("Tuner manager settings");
    connect(settings_button, &QToolButton::clicked, this, &TunerList::onSettingsClicked);
    button_toolbar->addWidget(settings_button);

    // Global record button
    global_record_button = new QToolButton(this);
    global_record_button->setText("●");
    global_record_button->setToolTip("Start/Stop recording on all enabled tuners");
    global_record_button->setCheckable(true);
    global_record_button->setStyleSheet(
        "QToolButton { color: #666666; }"
        "QToolButton:hover { color: #888888; }"
        "QToolButton:checked { color: #cc0000; }"
        "QToolButton:checked:hover { color: #ff0000; }"
    );
    connect(global_record_button, &QToolButton::clicked, this, &TunerList::onGlobalRecordClicked);
    button_toolbar->addWidget(global_record_button);

    // Open folder button
    open_folder_button = new QToolButton(this);
    open_folder_button->setText("📂");
    open_folder_button->setToolTip("Open recording folder");
    connect(open_folder_button, &QToolButton::clicked, this, &TunerList::onOpenFolderClicked);
    button_toolbar->addWidget(open_folder_button);

    button_toolbar->addSeparator();

    // Status label - shows "Tuners: x/y" (running/total)
    status_label = new QLabel("0/0", this);
    status_label->setToolTip("Running tuners / Total tuners");
    button_toolbar->addWidget(status_label);

    main_layout->addWidget(button_toolbar);

    // Scroll area for tuner rows
    scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    tuner_rows_container = new QWidget();
    tuner_rows_layout = new QVBoxLayout(tuner_rows_container);
    tuner_rows_layout->setContentsMargins(0, 0, 0, 0);
    tuner_rows_layout->setSpacing(2);
    tuner_rows_layout->addStretch();

    scroll_area->setWidget(tuner_rows_container);
    main_layout->addWidget(scroll_area, 1);

    setLayout(main_layout);
}

void TunerList::setupAddMenu()
{
    add_menu = new QMenu(this);

    // Analog submenu
    QMenu* analogMenu = add_menu->addMenu("Analog");

    QAction* actNfm = analogMenu->addAction("NFM (Narrowband FM)");
    actNfm->setData(static_cast<int>(ReceiverType::ANALOG_NFM));

    QAction* actAm = analogMenu->addAction("AM");
    actAm->setData(static_cast<int>(ReceiverType::ANALOG_AM));

    QAction* actAmSync = analogMenu->addAction("AM Sync");
    actAmSync->setData(static_cast<int>(ReceiverType::ANALOG_AMSYNC));

    analogMenu->addSeparator();

    QAction* actWfmMono = analogMenu->addAction("WFM Mono");
    actWfmMono->setData(static_cast<int>(ReceiverType::ANALOG_WFM_MONO));

    QAction* actWfmStereo = analogMenu->addAction("WFM Stereo");
    actWfmStereo->setData(static_cast<int>(ReceiverType::ANALOG_WFM_STEREO));

    analogMenu->addSeparator();

    QAction* actUsb = analogMenu->addAction("USB");
    actUsb->setData(static_cast<int>(ReceiverType::ANALOG_USB));

    QAction* actLsb = analogMenu->addAction("LSB");
    actLsb->setData(static_cast<int>(ReceiverType::ANALOG_LSB));

    QAction* actCwU = analogMenu->addAction("CW-U");
    actCwU->setData(static_cast<int>(ReceiverType::ANALOG_CW_U));

    QAction* actCwL = analogMenu->addAction("CW-L");
    actCwL->setData(static_cast<int>(ReceiverType::ANALOG_CW_L));

    // Connect menu action
    connect(add_menu, &QMenu::triggered, this, &TunerList::onAddTunerType);
}

TunerList::~TunerList()
{
    clearTunerRows();
}

void TunerList::set_tuner_manager(TunerManager* manager)
{
    tuner_manager = manager;
    refresh_tuner_list();
}

void TunerList::refresh_tuner_list()
{
    clearTunerRows();

    if (!tuner_manager) {
        status_label->setText("No tuner manager");
        return;
    }

    // Collect tuner info for sorting
    struct TunerInfo {
        channel_id id;
        qint64 frequency;
    };
    std::vector<TunerInfo> tuner_infos;

    std::vector<channel_id> ids = tuner_manager->get_all_channels();
    qint64 rf_freq = static_cast<qint64>(tuner_manager->get_rf_freq());

    for (channel_id tuner_id : ids) {
        ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
        if (channel) {
            qint64 offset = static_cast<qint64>(channel->get_freq_offset());

            tuner_infos.push_back({tuner_id, rf_freq + offset});
        }
    }

    // Sort by frequency (ascending)
    std::sort(tuner_infos.begin(), tuner_infos.end(),
              [](const TunerInfo& a, const TunerInfo& b) { return a.frequency < b.frequency; });

    // Add rows for all tuners in sorted order
    for (const TunerInfo& info : tuner_infos) {
        channel_id tuner_id = info.id;
        ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
        if (channel) {
            QString name = QString::fromStdString(channel->get_channel_name());
            if (name.isEmpty()) {
                name = QString("Tuner %1").arg(tuner_id);
            }

            // Use stored color if available, otherwise use default
            QColor tuner_color;
            auto color_it = tuner_colors.find(tuner_id);
            if (color_it != tuner_colors.end()) {
                tuner_color = color_it->second;
            } else {
                // Default colors (cycle through) - must match MultiTunerPlotter::DEFAULT_TUNER_COLORS
                static const QColor colors[] = {
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
                tuner_color = colors[tuner_id % 12];
            }

            // Get receiver type from channel
            ReceiverType type = channel->get_backend_type();

            auto* row = new TunerRowWidget(tuner_id, name, type, tuner_color, tuner_rows_container);

            // Set frequency (already computed during sort)
            row->setFrequency(info.frequency);

            // Sync enabled state from ReceiverChannel
            row->setEnabled(channel->is_enabled());

            // Set status based on enabled, DSP running, and bypass state
            TunerStatus status;
            if (!channel->is_enabled()) {
                status = TunerStatus::Disabled;
            } else if (!tuner_manager->is_running()) {
                status = TunerStatus::Stopped;
            } else if (channel->is_bypassed()) {
                status = TunerStatus::Bypassed;
            } else {
                status = TunerStatus::Running;
            }
            row->setStatus(status);

            // Restore volume/muted state from maps (like color is restored)
            auto vol_it = tuner_volumes.find(tuner_id);
            if (vol_it != tuner_volumes.end()) {
                row->setVolume(vol_it->second);
            }
            auto mute_it = tuner_muted.find(tuner_id);
            if (mute_it != tuner_muted.end()) {
                row->setMuted(mute_it->second);
            }

            // Restore filter width from maps
            auto flow_it = tuner_filter_low.find(tuner_id);
            auto fhigh_it = tuner_filter_high.find(tuner_id);
            if (flow_it != tuner_filter_low.end() && fhigh_it != tuner_filter_high.end()) {
                row->setFilterWidth(flow_it->second, fhigh_it->second);
            }

            // Restore recording config from TunerManager
            if (tuner_manager) {
                auto rec_config = tuner_manager->getTunerRecordingConfig(tuner_id);
                row->setRecordingConfig(rec_config.record_iq, rec_config.record_audio,
                                        rec_config.iq_mode, rec_config.audio_mode);
            }

            connectRowSignals(row);

            // Insert before the stretch
            tuner_rows_layout->insertWidget(tuner_rows_layout->count() - 1, row);
            tuner_rows[tuner_id] = row;

            // Apply initial layout based on current width
            row->updateLayoutForWidth(width() - 20);
        }
    }

    updateStatusLabel();
}

void TunerList::update_tuner_color(int tuner_id, const QColor& color)
{

    // Store color so it persists across refreshes
    tuner_colors[tuner_id] = color;

    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setColor(color);
    }
}

void TunerList::update_tuner_running(int tuner_id, bool running)
{
    // Legacy method - use update_tuner_status for more control
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setRunning(running);
    }
}

void TunerList::update_tuner_status(int tuner_id, TunerStatus status)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setStatus(status);
        updateStatusLabel();
    }
}

void TunerList::update_tuner_frequency(int tuner_id, qint64 freq)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setFrequency(freq);
        sortTunerRows();
    }
}

void TunerList::update_tuner_filter_width(int tuner_id, int filter_low, int filter_high)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setFilterWidth(filter_low, filter_high);
    }
}

void TunerList::update_tuner_rssi(int tuner_id, float level_db)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setRssi(level_db);
    }
}

qint64 TunerList::getTunerFrequency(int tuner_id) const
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        return it->second->frequency();
    }
    return 0;
}

TunerStatus TunerList::getTunerStatus(int tuner_id) const
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        return it->second->status();
    }
    return TunerStatus::Stopped;
}

int TunerList::getTunerVolume(int tuner_id) const
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        return it->second->volume();
    }
    return 80;  // Default volume
}

bool TunerList::getTunerMuted(int tuner_id) const
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        return it->second->isMuted();
    }
    return false;  // Default not muted
}

void TunerList::set_all_tuners_running(bool dsp_running)
{
    for (auto& pair : tuner_rows) {
        TunerStatus status;
        int tuner_id = pair.first;
        TunerRowWidget* row = pair.second;

        if (!row->isEnabled()) {
            // User explicitly disabled this tuner
            status = TunerStatus::Disabled;
        } else if (!dsp_running) {
            // DSP not running
            status = TunerStatus::Stopped;
        } else {
            // DSP running and tuner enabled - check if bypassed
            bool is_bypassed = false;

            if (tuner_manager) {
                ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
                if (channel) {
                    is_bypassed = channel->is_bypassed();
                } else {
                    // Channel doesn't exist yet - calculate if it would be bypassed
                    // based on tuner frequency vs SDR range
                    qint64 tuner_freq = row->frequency();
                    double rf_freq = tuner_manager->get_rf_freq();
                    double sample_rate = tuner_manager->get_sample_rate();
                    double half_bw = sample_rate / 2.0;

                    // Check if tuner freq is within SDR range (rf_freq +/- half_bw)
                    qint64 low_freq = static_cast<qint64>(rf_freq - half_bw);
                    qint64 high_freq = static_cast<qint64>(rf_freq + half_bw);

                    is_bypassed = (tuner_freq < low_freq || tuner_freq > high_freq);

                }
            }

            status = is_bypassed ? TunerStatus::Bypassed : TunerStatus::Running;
        }

        row->setStatus(status);
    }

    updateStatusLabel();
}

void TunerList::load_from_settings(QSettings* settings)
{
    if (!settings) {
        return;
    }

    clearTunerRows();

    int saved_count = settings->value("tuner/count", 0).toInt();

    if (saved_count == 0) {
        status_label->setText("No saved tuners");
        return;
    }

    // Get saved RF frequency to calculate absolute tuner frequencies
    qint64 saved_rf_freq = settings->value("input/frequency", 0).toLongLong();

    // Default colors (same as in refresh_tuner_list)
    static const QColor colors[] = {
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

    for (int i = 0; i < saved_count; i++) {
        QString prefix = QString("tuners/%1/").arg(i);

        // Read saved tuner info
        QString name = settings->value(prefix + "name", QString("Tuner %1").arg(i)).toString();
        double freq_offset = settings->value(prefix + "freq_offset", 0.0).toDouble();
        QString color_str = settings->value(prefix + "color", "").toString();


        // Get receiver type
        ReceiverType type = ReceiverType::ANALOG_NFM;
        if (settings->contains(prefix + "receiver_type")) {
            type = static_cast<ReceiverType>(settings->value(prefix + "receiver_type").toInt());
        }

        // Get color
        QColor tuner_color = colors[i % 12];
        if (!color_str.isEmpty()) {
            tuner_color = QColor(color_str);
        }

        // Store color in tuner_colors map so refresh_tuner_list can use it
        tuner_colors[i] = tuner_color;

        // Get enabled state
        bool enabled = settings->value(prefix + "enabled", true).toBool();

        // Use index as temporary tuner_id (will be replaced when real channels created)
        int temp_id = i;
        auto* row = new TunerRowWidget(temp_id, name, type, tuner_color, tuner_rows_container);

        // Set frequency from saved RF freq + offset
        qint64 tuner_freq = saved_rf_freq + static_cast<qint64>(freq_offset);
        row->setFrequency(tuner_freq);

        // Get default filter values based on receiver type
        int default_filter_low = -5000;
        int default_filter_high = 5000;
        switch (type) {
            case ReceiverType::ANALOG_WFM_MONO:
            case ReceiverType::ANALOG_WFM_STEREO:
            case ReceiverType::ANALOG_WFM_STEREO_OIRT:
                default_filter_low = -80000;
                default_filter_high = 80000;
                break;
            case ReceiverType::ANALOG_USB:
                default_filter_low = 0;
                default_filter_high = 3000;
                break;
            case ReceiverType::ANALOG_LSB:
                default_filter_low = -3000;
                default_filter_high = 0;
                break;
            case ReceiverType::ANALOG_CW_U:
                default_filter_low = -250;
                default_filter_high = 750;
                break;
            case ReceiverType::ANALOG_CW_L:
                default_filter_low = -750;
                default_filter_high = 250;
                break;
            default:
                break;  // NFM/AM: -5000/5000
        }

        // Load filter width from settings (with type-appropriate defaults)
        int filter_low = settings->value(prefix + "filter_low", default_filter_low).toInt();
        int filter_high = settings->value(prefix + "filter_high", default_filter_high).toInt();
        row->setFilterWidth(filter_low, filter_high);

        // Set enabled state from saved settings
        row->setEnabled(enabled);

        // Load volume and mute state (default -15dB = 18%)
        int volume = settings->value(prefix + "volume", 18).toInt();
        bool muted = settings->value(prefix + "muted", false).toBool();
        row->setVolume(volume);
        row->setMuted(muted);

        // Store in maps so refresh_tuner_list can restore them
        tuner_volumes[i] = volume;
        tuner_muted[i] = muted;
        tuner_filter_low[i] = filter_low;
        tuner_filter_high[i] = filter_high;

        // Connect signals (some won't work until real channels exist)
        connectRowSignals(row);

        // Insert before the stretch
        tuner_rows_layout->insertWidget(tuner_rows_layout->count() - 1, row);
        tuner_rows[temp_id] = row;

        // Apply initial layout based on current width
        row->updateLayoutForWidth(width() - 20);

    }

    updateStatusLabel();
}

void TunerList::clearTunerRows()
{
    for (auto& pair : tuner_rows) {
        delete pair.second;
    }
    tuner_rows.clear();
}

void TunerList::sortTunerRows()
{
    if (tuner_rows.empty()) {
        return;
    }

    // Collect rows with their frequencies
    std::vector<std::pair<qint64, TunerRowWidget*>> sorted_rows;
    for (auto& pair : tuner_rows) {
        qint64 freq = pair.second->frequency();
        sorted_rows.push_back({freq, pair.second});
    }

    // Sort by frequency (ascending)
    std::sort(sorted_rows.begin(), sorted_rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Remove all widgets from layout (except the stretch at the end)
    for (auto& pair : tuner_rows) {
        tuner_rows_layout->removeWidget(pair.second);
    }

    // Re-add in sorted order (insert before the stretch)
    for (auto& row_pair : sorted_rows) {
        tuner_rows_layout->insertWidget(tuner_rows_layout->count() - 1, row_pair.second);
    }
}

void TunerList::updateStatusLabel()
{
    int total = tuner_rows.size();
    int running = 0;

    for (const auto& pair : tuner_rows) {
        if (pair.second->status() == TunerStatus::Running) {
            running++;
        }
    }

    status_label->setText(QString("%1/%2").arg(running).arg(total));
}

void TunerList::connectRowSignals(TunerRowWidget* row)
{
    connect(row, &TunerRowWidget::receiverTypeChanged, this, &TunerList::onTunerTypeChanged);
    connect(row, &TunerRowWidget::nameChanged, this, &TunerList::onTunerNameChanged);
    connect(row, &TunerRowWidget::enableToggled, this, &TunerList::onTunerEnabledToggled);
    connect(row, &TunerRowWidget::colorChanged, this, &TunerList::onTunerColorChanged);
    connect(row, &TunerRowWidget::alphaChanged, this, &TunerList::onTunerAlphaChanged);
    connect(row, &TunerRowWidget::volumeChanged, this, &TunerList::onTunerVolumeChanged);
    connect(row, &TunerRowWidget::muteToggled, this, &TunerList::onTunerMuteToggled);
    connect(row, &TunerRowWidget::recordingToggled, this, &TunerList::onTunerRecordingToggled);
    connect(row, &TunerRowWidget::tunerRecordingConfigChanged, this, &TunerList::onTunerRecordingConfigChanged);
    connect(row, &TunerRowWidget::centerRequested, this, &TunerList::onTunerCenterRequested);
    connect(row, &TunerRowWidget::zoomRequested, this, &TunerList::onTunerZoomRequested);
    connect(row, &TunerRowWidget::closeRequested, this, &TunerList::onTunerCloseRequested);
    connect(row, &TunerRowWidget::frequencyChanged, this, &TunerList::onTunerFrequencyChanged);
    connect(row, &TunerRowWidget::filterWidthChanged, this, &TunerList::onTunerFilterWidthChanged);
    connect(row, &TunerRowWidget::squelchChanged, this, &TunerList::onTunerSquelchChanged);
    connect(row, &TunerRowWidget::autoSquelchRequested, this, &TunerList::onTunerAutoSquelchRequested);
    connect(row, &TunerRowWidget::filterPresetChanged, this, &TunerList::onTunerFilterPresetChanged);
    connect(row, &TunerRowWidget::agcPresetChanged, this, &TunerList::onTunerAgcPresetChanged);
    connect(row, &TunerRowWidget::nbStateChanged, this, &TunerList::onTunerNbStateChanged);
    connect(row, &TunerRowWidget::filterShapeChanged, this, &TunerList::onTunerFilterShapeChanged);
    connect(row, &TunerRowWidget::nb1ThresholdChanged, this, &TunerList::onTunerNb1ThresholdChanged);
    connect(row, &TunerRowWidget::nb2ThresholdChanged, this, &TunerList::onTunerNb2ThresholdChanged);
    connect(row, &TunerRowWidget::agcHangChanged, this, &TunerList::onTunerAgcHangChanged);
    connect(row, &TunerRowWidget::agcThresholdChanged, this, &TunerList::onTunerAgcThresholdChanged);
    connect(row, &TunerRowWidget::agcDecayChanged, this, &TunerList::onTunerAgcDecayChanged);
    connect(row, &TunerRowWidget::agcGainChanged, this, &TunerList::onTunerAgcGainChanged);
}

void TunerList::on_add_button_clicked()
{
    emit tuner_add_requested_with_type(last_tuner_type);
}

void TunerList::onAddTunerType(QAction* action)
{
    if (!action)
        return;

    bool ok;
    int typeInt = action->data().toInt(&ok);
    if (!ok) {
        return;
    }

    ReceiverType type = static_cast<ReceiverType>(typeInt);
    last_tuner_type = type;

    // Update button text to show selected type
    add_button->setText(QString("+ %1").arg(receiverTypeName(type)));

    // Save to settings for persistence across launches
    QSettings settings;
    settings.setValue("tuner/last_add_type", typeInt);

}

QString TunerList::receiverTypeName(ReceiverType type) const
{
    switch (type) {
    case ReceiverType::ANALOG_NFM:          return "NFM";
    case ReceiverType::ANALOG_AM:           return "AM";
    case ReceiverType::ANALOG_AMSYNC:       return "AM Sync";
    case ReceiverType::ANALOG_WFM_MONO:     return "WFM Mono";
    case ReceiverType::ANALOG_WFM_STEREO:   return "WFM Stereo";
    case ReceiverType::ANALOG_WFM_STEREO_OIRT: return "WFM OIRT";
    case ReceiverType::ANALOG_USB:          return "USB";
    case ReceiverType::ANALOG_LSB:          return "LSB";
    case ReceiverType::ANALOG_CW_U:         return "CW-U";
    case ReceiverType::ANALOG_CW_L:         return "CW-L";
    default:                                return "Tuner";
    }
}

void TunerList::onTunerNameChanged(int tuner_id, const QString& name)
{
    if (tuner_manager) {
        ReceiverChannel* channel = tuner_manager->get_channel_impl(tuner_id);
        if (channel) {
            channel->set_channel_name(name.toStdString());
        }
    }
    emit tuner_name_changed(tuner_id, name);
}

void TunerList::onTunerTypeChanged(int tuner_id, ReceiverType type)
{
    emit tuner_type_changed(tuner_id, type);
}

void TunerList::onTunerEnabledToggled(int tuner_id, bool enabled)
{

    // Status is already set by TunerRowWidget::onEnableClicked() for disable case
    // For enable case, MainWindow will set the proper status after checking DSP/bypass state

    emit tuner_enabled_changed(tuner_id, enabled);
}

void TunerList::onTunerCenterRequested(int tuner_id)
{
    emit tuner_center_requested(tuner_id);
}

void TunerList::onTunerZoomRequested(int tuner_id)
{
    emit tuner_zoom_requested(tuner_id);
}

void TunerList::onTunerCloseRequested(int tuner_id)
{
    emit tuner_remove_requested(tuner_id);
}

void TunerList::onTunerColorChanged(int tuner_id, const QColor& color)
{
    tuner_colors[tuner_id] = color;
    emit tuner_color_changed(tuner_id, color);
}

void TunerList::onTunerAlphaChanged(int tuner_id, int alpha)
{
    emit tuner_alpha_changed(tuner_id, alpha);
}

void TunerList::onTunerVolumeChanged(int tuner_id, int volume)
{
    tuner_volumes[tuner_id] = volume;
    emit tuner_volume_changed(tuner_id, volume);
}

void TunerList::onTunerMuteToggled(int tuner_id, bool muted)
{
    tuner_muted[tuner_id] = muted;
    emit tuner_mute_toggled(tuner_id, muted);
}

void TunerList::onTunerRecordingToggled(int tuner_id, bool recording)
{
    emit tuner_recording_toggled(tuner_id, recording);

    // Update global record button state based on any tuner recording
    updateGlobalRecordButtonState();
}

void TunerList::onGlobalRecordClicked()
{
    m_global_recording = global_record_button->isChecked();

    // Toggle recording on all tuner rows
    for (auto& pair : tuner_rows) {
        TunerRowWidget* row = pair.second;
        if (row) {
            // Only toggle if the tuner has recording enabled in config
            if (tuner_manager) {
                TunerRecordingConfig config = tuner_manager->getTunerRecordingConfig(pair.first);
                if (config.record_iq || config.record_audio) {
                    row->setRecording(m_global_recording);
                    emit tuner_recording_toggled(pair.first, m_global_recording);
                }
            } else {
                // No tuner manager, just toggle all
                row->setRecording(m_global_recording);
                emit tuner_recording_toggled(pair.first, m_global_recording);
            }
        }
    }

    emit global_recording_toggled(m_global_recording);
}

void TunerList::updateGlobalRecordButtonState()
{
    // Check if any tuner is recording
    bool anyRecording = false;
    for (const auto& pair : tuner_rows) {
        if (pair.second && pair.second->isRecording()) {
            anyRecording = true;
            break;
        }
    }

    // Update global button without triggering click
    global_record_button->blockSignals(true);
    global_record_button->setChecked(anyRecording);
    global_record_button->blockSignals(false);
    m_global_recording = anyRecording;
}

void TunerList::onTunerRecordingIqToggled(int tuner_id, bool recording)
{
    emit tuner_recording_iq_toggled(tuner_id, recording);
}

void TunerList::onTunerRecordingConfigChanged(int tuner_id, bool record_iq, bool record_audio, RecordingMode iq_mode, RecordingMode audio_mode)
{


    // Update TunerManager's per-tuner recording config
    if (tuner_manager) {
        TunerRecordingConfig config = tuner_manager->getTunerRecordingConfig(tuner_id);
        config.record_iq = record_iq;
        config.record_audio = record_audio;
        config.iq_mode = iq_mode;
        config.audio_mode = audio_mode;
        tuner_manager->setTunerRecordingConfig(tuner_id, config);
    }

    emit tuner_recording_config_changed(tuner_id, record_iq, record_audio, iq_mode, audio_mode);
}

void TunerList::update_tuner_recording(int tuner_id, bool recording)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setRecording(recording);
    }
}

void TunerList::update_tuner_recording_iq(int tuner_id, bool recording)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->setRecordingIq(recording);
    }
}

void TunerList::update_tuner_recording_info(int tuner_id, double audio_duration, double iq_duration)
{
    auto it = tuner_rows.find(tuner_id);
    if (it != tuner_rows.end()) {
        it->second->updateRecordingInfo(audio_duration, iq_duration);
    }
}

void TunerList::onTunerFrequencyChanged(int tuner_id, qint64 freq)
{
    emit tuner_frequency_changed(tuner_id, freq);

    // Re-sort rows by frequency
    sortTunerRows();
}

void TunerList::onTunerFilterWidthChanged(int tuner_id, int filter_low, int filter_high)
{

    tuner_filter_low[tuner_id] = filter_low;    // Persist for refresh_tuner_list
    tuner_filter_high[tuner_id] = filter_high;  // Persist for refresh_tuner_list
    emit tuner_filter_width_changed(tuner_id, filter_low, filter_high);
}

void TunerList::onTunerSquelchChanged(int tuner_id, double level_db)
{
    emit tuner_squelch_changed(tuner_id, level_db);
}

void TunerList::onTunerAutoSquelchRequested(int tuner_id)
{
    emit tuner_auto_squelch_requested(tuner_id);
}

void TunerList::onTunerFilterPresetChanged(int tuner_id, int preset)
{
    emit tuner_filter_preset_changed(tuner_id, preset);
}

void TunerList::onTunerAgcPresetChanged(int tuner_id, int preset)
{
    emit tuner_agc_preset_changed(tuner_id, preset);
}

void TunerList::onTunerNbStateChanged(int tuner_id, int state)
{
    emit tuner_nb_state_changed(tuner_id, state);
}

void TunerList::onTunerFilterShapeChanged(int tuner_id, int shape)
{
    emit tuner_filter_shape_changed(tuner_id, shape);
}

void TunerList::onTunerNb1ThresholdChanged(int tuner_id, float threshold)
{
    emit tuner_nb1_threshold_changed(tuner_id, threshold);
}

void TunerList::onTunerNb2ThresholdChanged(int tuner_id, float threshold)
{
    emit tuner_nb2_threshold_changed(tuner_id, threshold);
}

void TunerList::onTunerAgcHangChanged(int tuner_id, bool use_hang)
{
    emit tuner_agc_hang_changed(tuner_id, use_hang);
}

void TunerList::onTunerAgcThresholdChanged(int tuner_id, int threshold)
{
    emit tuner_agc_threshold_changed(tuner_id, threshold);
}

void TunerList::onTunerAgcDecayChanged(int tuner_id, int decay_ms)
{
    emit tuner_agc_decay_changed(tuner_id, decay_ms);
}

void TunerList::onTunerAgcGainChanged(int tuner_id, int gain)
{
    emit tuner_agc_gain_changed(tuner_id, gain);
}

void TunerList::onSettingsClicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Tuner Manager Settings");
    dialog.setModal(true);
    dialog.setMinimumWidth(550);
    dialog.setMinimumHeight(580);

    auto* layout = new QVBoxLayout(&dialog);

    // Create tab widget
    auto* tabWidget = new QTabWidget(&dialog);
    layout->addWidget(tabWidget);

    // Get current recording config from tuner manager
    RecordingConfig recConfig;
    if (tuner_manager) {
        recConfig = tuner_manager->getRecordingConfig();
    }

    // Load current settings
    QSettings settings;

    // =========================================================================
    // TAB 1: Recording
    // =========================================================================
    auto* recordingTab = new QWidget();
    auto* recLayout = new QFormLayout(recordingTab);
    recLayout->setSpacing(10);
    recLayout->setContentsMargins(15, 15, 15, 15);

    // Recording folder
    auto* folderLayout = new QHBoxLayout();
    auto* folderEdit = new QLineEdit(recConfig.recording_folder, &dialog);
    folderEdit->setReadOnly(true);
    folderEdit->setMinimumWidth(350);
    auto* folderBtn = new QPushButton("Browse...", &dialog);
    folderLayout->addWidget(folderEdit, 1);  // stretch factor 1
    folderLayout->addWidget(folderBtn);
    recLayout->addRow("Recording Folder:", folderLayout);

    connect(folderBtn, &QPushButton::clicked, [&dialog, folderEdit]() {
        QString dir = QFileDialog::getExistingDirectory(&dialog, "Select Recording Folder",
                                                         folderEdit->text());
        if (!dir.isEmpty()) {
            folderEdit->setText(dir);
        }
    });

    // Auto Split Interval
    auto* splitMinutesSpin = new QSpinBox(&dialog);
    splitMinutesSpin->setRange(0, 1440);
    splitMinutesSpin->setSuffix(" min");
    splitMinutesSpin->setSpecialValueText("Disabled");
    splitMinutesSpin->setValue(recConfig.auto_split_minutes);
    splitMinutesSpin->setToolTip("Automatically split recording files at this interval.");
    recLayout->addRow("Auto Split:", splitMinutesSpin);

    tabWidget->addTab(recordingTab, "Recording");

    // =========================================================================
    // TAB 2: Filename Pattern
    // =========================================================================
    auto* filenameTab = new QWidget();
    auto* fnLayout = new QVBoxLayout(filenameTab);
    fnLayout->setSpacing(10);
    fnLayout->setContentsMargins(15, 15, 15, 15);

    // Pattern text box
    auto* patternLabel = new QLabel("Filename Pattern:", &dialog);
    fnLayout->addWidget(patternLabel);

    auto* templateEdit = new QLineEdit(recConfig.filename_template, &dialog);
    templateEdit->setMinimumWidth(450);
    templateEdit->setStyleSheet("QLineEdit { font-family: monospace; padding: 6px; }");
    fnLayout->addWidget(templateEdit);

    // Template preview
    auto* previewLabel = new QLabel(&dialog);
    previewLabel->setStyleSheet("QLabel { color: #0a0; font-family: monospace; padding: 8px; background: #222; border-radius: 3px; }");
    previewLabel->setText(FilenameTemplate::preview(recConfig.filename_template));
    fnLayout->addWidget(previewLabel);

    connect(templateEdit, &QLineEdit::textChanged, [previewLabel](const QString& text) {
        previewLabel->setText(FilenameTemplate::preview(text));
    });

    fnLayout->addSpacing(10);

    // Variables - click to insert at cursor
    auto* varsLabel = new QLabel("<b>Click to insert at cursor:</b>", &dialog);
    fnLayout->addWidget(varsLabel);

    // Scrollable area for variables
    auto* scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumHeight(220);
    auto* varsWidget = new QWidget();
    auto* varsGrid = new QGridLayout(varsWidget);
    varsGrid->setSpacing(4);
    varsGrid->setContentsMargins(0, 0, 0, 0);

    struct VarInfo {
        QString var;
        QString desc;
    };
    QVector<VarInfo> variables = {
        { "{datetime}", "YYYYMMDD_HHMMSS" },
        { "{date}", "YYYYMMDD" },
        { "{time}", "HHMMSS" },
        { "{Y}", "Year (2025)" },
        { "{m}", "Month (01-12)" },
        { "{d}", "Day (01-31)" },
        { "{H}", "Hour (00-23)" },
        { "{M}", "Minute (00-59)" },
        { "{S}", "Second (00-59)" },
        { "{channel}", "Channel name" },
        { "{freq}", "Frequency in Hz" },
        { "{freq_mhz}", "Frequency in MHz (145.500)" },
        { "{freq_khz}", "Frequency in kHz" },
        { "{mode}", "Demod mode (NFM, AM, etc.)" },
        { "{tuner}", "Tuner ID number" },
        { "{type}", "'iq' or 'audio'" },
        { "{srate}", "Sample rate in Hz" },
        { "{srate_k}", "Sample rate in kHz" },
        { "{call}", "Call number" },
        { "_", "Underscore separator" },
        { "/", "Create subdirectory" }
    };

    int row = 0;
    for (const auto& v : variables) {
        auto* varBtn = new QPushButton(v.var, &dialog);
        varBtn->setStyleSheet("QPushButton { font-family: monospace; padding: 3px 8px; min-width: 90px; }");
        varBtn->setCursor(Qt::PointingHandCursor);
        varBtn->setToolTip("Click to insert at cursor position");
        connect(varBtn, &QPushButton::clicked, [templateEdit, var = v.var]() {
            templateEdit->insert(var);
            templateEdit->setFocus();
        });
        varsGrid->addWidget(varBtn, row, 0);

        auto* descLabel = new QLabel(v.desc, &dialog);
        descLabel->setStyleSheet("QLabel { color: #888; }");
        varsGrid->addWidget(descLabel, row, 1);
        row++;
    }

    scrollArea->setWidget(varsWidget);
    fnLayout->addWidget(scrollArea, 1);  // stretch factor 1

    tabWidget->addTab(filenameTab, "Filename");

    // =========================================================================
    // TAB 3: Formats
    // =========================================================================
    auto* formatsTab = new QWidget();
    auto* fmtLayout = new QFormLayout(formatsTab);
    fmtLayout->setSpacing(10);
    fmtLayout->setContentsMargins(15, 15, 15, 15);

    // IQ Format
    auto* iqFormatCombo = new QComboBox(&dialog);
    iqFormatCombo->addItem("SigMF (.sigmf-data)", static_cast<int>(IqFileFormat::SIGMF));
    iqFormatCombo->addItem("Raw CF32 (.raw)", static_cast<int>(IqFileFormat::RAW_CF32));
    iqFormatCombo->addItem("Raw CS16 (.raw)", static_cast<int>(IqFileFormat::RAW_CS16));
    iqFormatCombo->addItem("WAV IQ (.wav)", static_cast<int>(IqFileFormat::WAV_IQ));
    iqFormatCombo->setCurrentIndex(iqFormatCombo->findData(static_cast<int>(recConfig.iq_format)));
    fmtLayout->addRow("IQ Format:", iqFormatCombo);

    // IQ Tap Point
    auto* tapPointCombo = new QComboBox(&dialog);
    tapPointCombo->addItem("After Filter (~96 kHz)", static_cast<int>(IqTapPoint::AFTER_FILTER));
    tapPointCombo->addItem("After DDC (~1 MHz)", static_cast<int>(IqTapPoint::AFTER_DDC));
    tapPointCombo->setCurrentIndex(tapPointCombo->findData(static_cast<int>(recConfig.iq_tap_point)));
    tapPointCombo->setToolTip("AFTER_FILTER: Narrowband (~96 kHz)\nAFTER_DDC: Wideband (~1 MHz)");
    fmtLayout->addRow("IQ Tap Point:", tapPointCombo);

    // Audio Format
    auto* audioFormatCombo = new QComboBox(&dialog);
    audioFormatCombo->addItem("WAV", static_cast<int>(AudioFileFormat::WAV));
    audioFormatCombo->addItem("FLAC", static_cast<int>(AudioFileFormat::FLAC));
    audioFormatCombo->addItem("OGG Vorbis", static_cast<int>(AudioFileFormat::OGG));
    audioFormatCombo->setCurrentIndex(audioFormatCombo->findData(static_cast<int>(recConfig.audio_format)));
    fmtLayout->addRow("Audio Format:", audioFormatCombo);

    tabWidget->addTab(formatsTab, "Formats");

    // =========================================================================
    // TAB 4: Display
    // =========================================================================
    auto* displayTab = new QWidget();
    auto* dispLayout = new QFormLayout(displayTab);
    dispLayout->setSpacing(10);
    dispLayout->setContentsMargins(15, 15, 15, 15);

    int zoom1 = settings.value("tuner/zoom_level_1_khz", 100).toInt();
    int zoom2 = settings.value("tuner/zoom_level_2_khz", 500).toInt();
    int zoom3 = settings.value("tuner/zoom_level_3_khz", 2000).toInt();

    // Zoom 1: Close view
    auto* zoom1Spin = new QSpinBox(&dialog);
    zoom1Spin->setRange(10, 10000);
    zoom1Spin->setValue(zoom1);
    zoom1Spin->setSuffix(" kHz");
    dispLayout->addRow("Zoom 1 (Close):", zoom1Spin);

    // Zoom 2: Medium view
    auto* zoom2Spin = new QSpinBox(&dialog);
    zoom2Spin->setRange(10, 10000);
    zoom2Spin->setValue(zoom2);
    zoom2Spin->setSuffix(" kHz");
    dispLayout->addRow("Zoom 2 (Medium):", zoom2Spin);

    // Zoom 3: Wide view
    auto* zoom3Spin = new QSpinBox(&dialog);
    zoom3Spin->setRange(10, 10000);
    zoom3Spin->setValue(zoom3);
    zoom3Spin->setSuffix(" kHz");
    dispLayout->addRow("Zoom 3 (Wide):", zoom3Spin);

    auto* noteLabel = new QLabel("Zoom 4 is always full sample rate span", &dialog);
    noteLabel->setStyleSheet("color: #888;");
    dispLayout->addRow("", noteLabel);

    tabWidget->addTab(displayTab, "Display");

    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() == QDialog::Accepted) {
        // Save zoom settings
        settings.setValue("tuner/zoom_level_1_khz", zoom1Spin->value());
        settings.setValue("tuner/zoom_level_2_khz", zoom2Spin->value());
        settings.setValue("tuner/zoom_level_3_khz", zoom3Spin->value());


        // Save recording settings
        RecordingConfig newConfig;
        newConfig.recording_folder = folderEdit->text();
        newConfig.iq_format = static_cast<IqFileFormat>(iqFormatCombo->currentData().toInt());
        newConfig.iq_tap_point = static_cast<IqTapPoint>(tapPointCombo->currentData().toInt());
        newConfig.audio_format = static_cast<AudioFileFormat>(audioFormatCombo->currentData().toInt());
        newConfig.auto_split_minutes = splitMinutesSpin->value();
        newConfig.filename_template = templateEdit->text();
        // Keep other settings from existing config
        newConfig.wav_sample_format = recConfig.wav_sample_format;
        newConfig.squelch_config = recConfig.squelch_config;
        newConfig.sigmf = recConfig.sigmf;

        if (tuner_manager) {
            tuner_manager->setRecordingConfig(newConfig);
        }
        emit recording_config_changed(newConfig);

    }
}

void TunerList::onOpenFolderClicked()
{
    QString folder;
    if (tuner_manager) {
        folder = tuner_manager->getRecordingConfig().recording_folder;
    } else {
        folder = RecordingConfig::getDefaultFolder();
    }

    // Ensure folder exists
    QDir dir(folder);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Open in OS file explorer
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void TunerList::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    int new_width = event->size().width();


    // Only update if width changed significantly
    if (std::abs(new_width - m_last_width) > 10) {
        m_last_width = new_width;
        updateAllRowLayouts(new_width);
    }
}

void TunerList::updateAllRowLayouts(int width)
{
    // Account for scroll area margins/scrollbar
    int row_width = width - 20;  // Approximate adjustment for scrollbar and margins


    for (auto& pair : tuner_rows) {
        pair.second->updateLayoutForWidth(row_width);
    }
}
