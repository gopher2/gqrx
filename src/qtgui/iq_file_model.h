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
#ifndef IQ_FILE_MODEL_H
#define IQ_FILE_MODEL_H

#include <QAbstractTableModel>
#include <QDateTime>
#include <QDir>
#include <QList>
#include <QSettings>
#include <QString>

struct IqFileInfo
{
    QString   filename;
    QString   fullPath;
    QDateTime dateTime;
    qint64    centerFreq;
    qint64    sampleRate;
    qint64    fileSize;
    double    duration;
    QString   comment;
    bool      isSigMF;

    IqFileInfo()
        : centerFreq(0)
        , sampleRate(0)
        , fileSize(0)
        , duration(0.0)
        , isSigMF(false)
    {}
};

enum class IqFileColumn
{
    Filename = 0,
    DateTime,
    Frequency,
    SampleRate,
    Duration,
    Size,
    Comment,
    COUNT
};

class IqFileModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit IqFileModel(QObject *parent = nullptr);
    ~IqFileModel() override;

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Custom methods
    void setDirectory(const QDir &dir);
    void refresh();
    QString getFilePath(int row) const;
    const IqFileInfo* getFileInfo(int row) const;

    // Column visibility
    bool isColumnVisible(IqFileColumn col) const;
    void setColumnVisible(IqFileColumn col, bool visible);
    QList<IqFileColumn> visibleColumns() const;

    // Settings persistence
    void saveSettings(QSettings *settings);
    void readSettings(QSettings *settings);

    // Static helpers
    static QString columnName(IqFileColumn col);
    static int columnCount();

private:
    void parseFilenameMetadata(IqFileInfo &info);
    void parseSigMFMetadata(IqFileInfo &info);
    QString formatFrequency(qint64 freq) const;
    QString formatSampleRate(qint64 rate) const;
    QString formatDuration(double seconds) const;
    QString formatSize(qint64 bytes) const;
    int visibleColumnToActual(int visibleCol) const;

    QDir                m_dir;
    QList<IqFileInfo>   m_files;
    bool                m_columnVisible[static_cast<int>(IqFileColumn::COUNT)];
};

#endif // IQ_FILE_MODEL_H
