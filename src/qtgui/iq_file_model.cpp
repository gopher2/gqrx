/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2014 Alexandru Csete OZ9AEC.
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
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

#include "iq_file_model.h"

static const int BYTES_PER_SAMPLE = 8; // Complex float32

IqFileModel::IqFileModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // All columns visible by default
    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
        m_columnVisible[i] = true;
}

IqFileModel::~IqFileModel()
{
}

int IqFileModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_files.count();
}

int IqFileModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    int count = 0;
    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
        if (m_columnVisible[i])
            ++count;
    return count;
}

int IqFileModel::visibleColumnToActual(int visibleCol) const
{
    int count = -1;
    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
    {
        if (m_columnVisible[i])
            ++count;
        if (count == visibleCol)
            return i;
    }
    return -1;
}

QVariant IqFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_files.count())
        return QVariant();

    const IqFileInfo &info = m_files.at(index.row());
    int actualCol = visibleColumnToActual(index.column());

    if (role == Qt::DisplayRole)
    {
        switch (static_cast<IqFileColumn>(actualCol))
        {
        case IqFileColumn::Filename:
            return info.filename;
        case IqFileColumn::DateTime:
            return info.dateTime.isValid() ? info.dateTime.toString("yyyy-MM-dd hh:mm:ss") : "n/a";
        case IqFileColumn::Frequency:
            return info.centerFreq > 0 ? formatFrequency(info.centerFreq) : "n/a";
        case IqFileColumn::SampleRate:
            return info.sampleRate > 0 ? formatSampleRate(info.sampleRate) : "n/a";
        case IqFileColumn::Duration:
            return info.duration > 0 ? formatDuration(info.duration) : "n/a";
        case IqFileColumn::Size:
            return formatSize(info.fileSize);
        case IqFileColumn::Comment:
            return info.comment.isEmpty() ? "n/a" : info.comment;
        default:
            break;
        }
    }
    else if (role == Qt::UserRole)
    {
        // Return raw data for sorting
        switch (static_cast<IqFileColumn>(actualCol))
        {
        case IqFileColumn::Filename:
            return info.filename;
        case IqFileColumn::DateTime:
            return info.dateTime;
        case IqFileColumn::Frequency:
            return info.centerFreq;
        case IqFileColumn::SampleRate:
            return info.sampleRate;
        case IqFileColumn::Duration:
            return info.duration;
        case IqFileColumn::Size:
            return info.fileSize;
        case IqFileColumn::Comment:
            return info.comment;
        default:
            break;
        }
    }
    else if (role == Qt::TextAlignmentRole)
    {
        switch (static_cast<IqFileColumn>(actualCol))
        {
        case IqFileColumn::Frequency:
        case IqFileColumn::SampleRate:
        case IqFileColumn::Duration:
        case IqFileColumn::Size:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant IqFileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    int actualCol = visibleColumnToActual(section);
    return columnName(static_cast<IqFileColumn>(actualCol));
}

Qt::ItemFlags IqFileModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void IqFileModel::setDirectory(const QDir &dir)
{
    m_dir = dir;
    m_dir.setNameFilters(QStringList() << "*.raw" << "*.sigmf-data");
    refresh();
}

void IqFileModel::refresh()
{
    beginResetModel();
    m_files.clear();

    QStringList files = m_dir.entryList(QDir::Files, QDir::Name);
    for (const QString &filename : files)
    {
        IqFileInfo info;
        info.filename = filename;
        info.fullPath = m_dir.absoluteFilePath(filename);
        info.isSigMF = filename.endsWith(".sigmf-data");

        QFileInfo fileInfo(info.fullPath);
        info.fileSize = fileInfo.size();

        if (info.isSigMF)
            parseSigMFMetadata(info);
        else
            parseFilenameMetadata(info);

        // Calculate duration from file size and sample rate
        if (info.sampleRate > 0)
            info.duration = static_cast<double>(info.fileSize) / (info.sampleRate * BYTES_PER_SAMPLE);

        m_files.append(info);
    }

    endResetModel();
}

QString IqFileModel::getFilePath(int row) const
{
    if (row < 0 || row >= m_files.count())
        return QString();
    return m_files.at(row).fullPath;
}

const IqFileInfo* IqFileModel::getFileInfo(int row) const
{
    if (row < 0 || row >= m_files.count())
        return nullptr;
    return &m_files.at(row);
}

bool IqFileModel::isColumnVisible(IqFileColumn col) const
{
    int idx = static_cast<int>(col);
    if (idx < 0 || idx >= static_cast<int>(IqFileColumn::COUNT))
        return false;
    return m_columnVisible[idx];
}

void IqFileModel::setColumnVisible(IqFileColumn col, bool visible)
{
    int idx = static_cast<int>(col);
    if (idx < 0 || idx >= static_cast<int>(IqFileColumn::COUNT))
        return;

    if (m_columnVisible[idx] != visible)
    {
        beginResetModel();
        m_columnVisible[idx] = visible;
        endResetModel();
    }
}

QList<IqFileColumn> IqFileModel::visibleColumns() const
{
    QList<IqFileColumn> result;
    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
        if (m_columnVisible[i])
            result.append(static_cast<IqFileColumn>(i));
    return result;
}

void IqFileModel::saveSettings(QSettings *settings)
{
    if (!settings)
        return;

    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
    {
        QString key = QString("iq_tool/column_%1_visible").arg(i);
        settings->setValue(key, m_columnVisible[i]);
    }
}

void IqFileModel::readSettings(QSettings *settings)
{
    if (!settings)
        return;

    beginResetModel();
    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
    {
        QString key = QString("iq_tool/column_%1_visible").arg(i);
        m_columnVisible[i] = settings->value(key, true).toBool();
    }
    endResetModel();
}

QString IqFileModel::columnName(IqFileColumn col)
{
    switch (col)
    {
    case IqFileColumn::Filename:   return tr("Filename");
    case IqFileColumn::DateTime:   return tr("Date/Time");
    case IqFileColumn::Frequency:  return tr("Frequency");
    case IqFileColumn::SampleRate: return tr("Sample Rate");
    case IqFileColumn::Duration:   return tr("Duration");
    case IqFileColumn::Size:       return tr("Size");
    case IqFileColumn::Comment:    return tr("Comment");
    default:                       return QString();
    }
}

int IqFileModel::columnCount()
{
    return static_cast<int>(IqFileColumn::COUNT);
}

void IqFileModel::parseFilenameMetadata(IqFileInfo &info)
{
    // Format: gqrx_YYYYMMDD_HHMMSS_FREQ_SAMPLERATE_fc.raw
    QStringList parts = info.filename.split('_');

    if (parts.size() >= 5)
    {
        // Parse date/time
        if (parts.size() >= 3)
        {
            QString dateStr = parts.at(1);
            QString timeStr = parts.at(2);
            if (dateStr.length() == 8 && timeStr.length() == 6)
            {
                info.dateTime = QDateTime::fromString(dateStr + timeStr, "yyyyMMddhhmmss");
            }
        }

        // Parse center frequency
        if (parts.size() >= 4)
        {
            bool ok;
            qint64 freq = parts.at(3).toLongLong(&ok);
            if (ok)
                info.centerFreq = freq;
        }

        // Parse sample rate
        if (parts.size() >= 5)
        {
            bool ok;
            qint64 rate = parts.at(4).toLongLong(&ok);
            if (ok)
                info.sampleRate = rate;
        }
    }

    // Check for JSON sidecar file with comment
    QString metaPath = info.fullPath + ".json";
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();
        if (doc.isObject())
        {
            QJsonObject root = doc.object();
            if (root.contains("comment"))
                info.comment = root["comment"].toString();
        }
    }
}

void IqFileModel::parseSigMFMetadata(IqFileInfo &info)
{
    // Look for corresponding .sigmf-meta file
    QString metaPath = info.fullPath;
    metaPath.replace(".sigmf-data", ".sigmf-meta");

    QFile metaFile(metaPath);
    if (!metaFile.open(QIODevice::ReadOnly))
    {
        parseFilenameMetadata(info);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
    metaFile.close();

    if (doc.isNull() || !doc.isObject())
    {
        parseFilenameMetadata(info);
        return;
    }

    QJsonObject root = doc.object();

    // Parse global metadata
    QJsonObject global = root["global"].toObject();
    if (global.contains("core:sample_rate"))
        info.sampleRate = static_cast<qint64>(global["core:sample_rate"].toDouble());
    if (global.contains("core:description"))
        info.comment = global["core:description"].toString();

    // Parse captures array for frequency and datetime
    QJsonArray captures = root["captures"].toArray();
    if (!captures.isEmpty())
    {
        QJsonObject capture = captures.first().toObject();
        if (capture.contains("core:frequency"))
            info.centerFreq = static_cast<qint64>(capture["core:frequency"].toDouble());
        if (capture.contains("core:datetime"))
        {
            QString dtStr = capture["core:datetime"].toString();
            info.dateTime = QDateTime::fromString(dtStr, Qt::ISODate);
        }
    }
}

QString IqFileModel::formatFrequency(qint64 freq) const
{
    double freqMHz = freq / 1e6;
    return QString("%1 MHz").arg(freqMHz, 0, 'f', 6);
}

QString IqFileModel::formatSampleRate(qint64 rate) const
{
    if (rate >= 1000000)
        return QString("%1 MS/s").arg(rate / 1e6, 0, 'f', 2);
    else if (rate >= 1000)
        return QString("%1 kS/s").arg(rate / 1e3, 0, 'f', 2);
    else
        return QString("%1 S/s").arg(rate);
}

QString IqFileModel::formatDuration(double seconds) const
{
    int totalSecs = static_cast<int>(seconds);
    int hours = totalSecs / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;

    if (hours > 0)
        return QString("%1:%2:%3").arg(hours).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    else
        return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

QString IqFileModel::formatSize(qint64 bytes) const
{
    if (bytes >= 1073741824) // 1 GB
        return QString("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 2);
    else if (bytes >= 1048576) // 1 MB
        return QString("%1 MB").arg(bytes / 1048576.0, 0, 'f', 2);
    else if (bytes >= 1024) // 1 KB
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    else
        return QString("%1 B").arg(bytes);
}
