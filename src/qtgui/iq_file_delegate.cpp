/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2025 David Kierzkowski K9DPD.
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
#include <QPainter>
#include <QApplication>

#include "iq_file_delegate.h"

IqFileDelegate::IqFileDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
    , m_filenameColumn(0)
{
}

IqFileDelegate::~IqFileDelegate()
{
}

void IqFileDelegate::setFilenameColumn(int column)
{
    m_filenameColumn = column;
}

void IqFileDelegate::paint(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const QModelIndex &index) const
{
    // Only apply middle-elide to filename column
    if (index.column() != m_filenameColumn)
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // Get the text
    QString text = index.data(Qt::DisplayRole).toString();

    // Initialize style options
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Calculate available width (account for margins)
    int margin = QApplication::style()->pixelMetric(QStyle::PM_FocusFrameHMargin, &opt) + 1;
    int availableWidth = opt.rect.width() - 2 * margin;

    // Get font metrics
    QFontMetrics fm(opt.font);

    // Check if text fits
    QString displayText;
    if (fm.horizontalAdvance(text) <= availableWidth)
    {
        displayText = text;
    }
    else
    {
        displayText = elideMiddle(text, fm, availableWidth);
    }

    // Draw background (for selection highlighting)
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

    // Draw the text
    QRect textRect = opt.rect.adjusted(margin, 0, -margin, 0);
    painter->save();
    if (opt.state & QStyle::State_Selected)
        painter->setPen(opt.palette.highlightedText().color());
    else
        painter->setPen(opt.palette.text().color());
    painter->setFont(opt.font);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, displayText);
    painter->restore();
}

QString IqFileDelegate::elideMiddle(const QString &text, const QFontMetrics &fm, int width) const
{
    static const QString ellipsis = "...";
    int ellipsisWidth = fm.horizontalAdvance(ellipsis);

    if (fm.horizontalAdvance(text) <= width)
        return text;

    // Find file extension
    int extPos = text.lastIndexOf('.');
    QString baseName, extension;

    if (extPos > 0)
    {
        baseName = text.left(extPos);
        extension = text.mid(extPos);
    }
    else
    {
        baseName = text;
        extension = QString();
    }

    int extWidth = fm.horizontalAdvance(extension);
    int availableForBase = width - ellipsisWidth - extWidth;

    if (availableForBase <= 0)
    {
        // Not enough room even for basic elision, just truncate
        return fm.elidedText(text, Qt::ElideMiddle, width);
    }

    // Split available space between start and end of basename
    int startWidth = availableForBase / 2;
    int endWidth = availableForBase - startWidth;

    // Find how many characters fit from start
    QString startPart;
    for (int i = 1; i <= baseName.length(); ++i)
    {
        QString candidate = baseName.left(i);
        if (fm.horizontalAdvance(candidate) > startWidth)
            break;
        startPart = candidate;
    }

    // Find how many characters fit from end
    QString endPart;
    for (int i = 1; i <= baseName.length(); ++i)
    {
        QString candidate = baseName.right(i);
        if (fm.horizontalAdvance(candidate) > endWidth)
            break;
        endPart = candidate;
    }

    return startPart + ellipsis + endPart + extension;
}
