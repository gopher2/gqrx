/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2025 David Kierzkowski.
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
#include "bandplantablemodel.h"
#include <QBrush>
#include <QLocale>

BandPlanTableModel::BandPlanTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int BandPlanTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return BandPlan::Get().size();
}

int BandPlanTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 7;
}

QVariant BandPlanTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
        case COL_VISIBLE:
            return QString("Show");
        case COL_NAME:
            return QString("Name");
        case COL_MIN_FREQ:
            return QString("Min Freq");
        case COL_MAX_FREQ:
            return QString("Max Freq");
        case COL_MODULATION:
            return QString("Modulation");
        case COL_STEP:
            return QString("Step");
        case COL_COLOR:
            return QString("Color");
        }
    }
    return QVariant();
}

QVariant BandPlanTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    int row = index.row();
    int col = index.column();

    if (row >= BandPlan::Get().size())
        return QVariant();

    BandInfo& band = BandPlan::Get().getBand(row);

    if (role == Qt::CheckStateRole && col == COL_VISIBLE)
    {
        return band.visible ? Qt::Checked : Qt::Unchecked;
    }
    else if (role == Qt::DisplayRole)
    {
        switch (col)
        {
        case COL_NAME:
            return band.name;
        case COL_MIN_FREQ:
            return QString::number(band.minFrequency);
        case COL_MAX_FREQ:
            return QString::number(band.maxFrequency);
        case COL_MODULATION:
            return band.modulation;
        case COL_STEP:
            return QString::number(band.step);
        case COL_COLOR:
            return band.color.name();
        }
    }
    else if (role == Qt::EditRole)
    {
        switch (col)
        {
        case COL_NAME:
            return band.name;
        case COL_MIN_FREQ:
            return QString::number(band.minFrequency);
        case COL_MAX_FREQ:
            return QString::number(band.maxFrequency);
        case COL_MODULATION:
            return band.modulation;
        case COL_STEP:
            return QString::number(band.step);
        case COL_COLOR:
            return band.color.name();
        }
    }
    else if (role == Qt::BackgroundRole && col == COL_COLOR)
    {
        return QBrush(band.color);
    }
    else if (role == Qt::ForegroundRole && col == COL_COLOR)
    {
        // Calculate luminance and use white or black text for contrast
        int luminance = (band.color.red() * 299 + band.color.green() * 587 + band.color.blue() * 114) / 1000;
        return QBrush(luminance > 128 ? Qt::black : Qt::white);
    }

    return QVariant();
}

bool BandPlanTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    int row = index.row();
    int col = index.column();

    if (row >= BandPlan::Get().size())
        return false;

    BandInfo band = BandPlan::Get().getBand(row);

    if (role == Qt::CheckStateRole && col == COL_VISIBLE)
    {
        band.visible = (value.toInt() == Qt::Checked);
        BandPlan::Get().updateBand(row, band);
        BandPlan::Get().save();
        emit dataChanged(index, index);
        return true;
    }

    if (role != Qt::EditRole)
        return false;

    QString strValue = value.toString();

    switch (col)
    {
    case COL_NAME:
        band.name = strValue;
        break;
    case COL_MIN_FREQ:
        band.minFrequency = strValue.toLongLong();
        break;
    case COL_MAX_FREQ:
        band.maxFrequency = strValue.toLongLong();
        break;
    case COL_MODULATION:
        band.modulation = strValue;
        break;
    case COL_STEP:
        band.step = strValue.toLongLong();
        break;
    case COL_COLOR:
        band.color = QColor(strValue);
        break;
    default:
        return false;
    }

    BandPlan::Get().updateBand(row, band);
    BandPlan::Get().save();

    emit dataChanged(index, index);
    return true;
}

Qt::ItemFlags BandPlanTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    if (index.column() == COL_VISIBLE)
        return Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void BandPlanTableModel::update()
{
    beginResetModel();
    endResetModel();
}
