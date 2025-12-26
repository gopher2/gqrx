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
#ifndef IQ_FILE_DELEGATE_H
#define IQ_FILE_DELEGATE_H

#include <QStyledItemDelegate>

class IqFileDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit IqFileDelegate(QObject *parent = nullptr);
    ~IqFileDelegate() override;

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    void setFilenameColumn(int column);
    int filenameColumn() const { return m_filenameColumn; }

private:
    QString elideMiddle(const QString &text, const QFontMetrics &fm, int width) const;

    int m_filenameColumn;
};

#endif // IQ_FILE_DELEGATE_H
