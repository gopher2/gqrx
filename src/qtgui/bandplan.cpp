/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2020 Dallas Epperson.
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
#include <Qt>
#include <QFile>
#include <QResource>
#include <QStringList>
#include <QTextStream>
#include <QString>
#include <QSet>
#include <algorithm>
#include <iostream>
#include "bandplan.h"

BandPlan* BandPlan::m_pThis = 0;

BandPlan::BandPlan()
{

}

void BandPlan::create()
{
    m_pThis = new BandPlan;
}

BandPlan& BandPlan::Get()
{
    return *m_pThis;
}

void BandPlan::setConfigDir(const QString& cfg_dir)
{
    m_bandPlanFile = cfg_dir + "/bandplan.csv";
    std::cout << "BandPlanFile is " << m_bandPlanFile.toStdString() << std::endl;

    if (!QFile::exists(m_bandPlanFile))
    {
        QResource resource(":/textfiles/bandplan.csv");
        QFile::copy(resource.absoluteFilePath(), m_bandPlanFile);
        QFile::setPermissions(m_bandPlanFile, QFile::permissions(m_bandPlanFile) | QFile::WriteOwner);
    }
}

bool BandPlan::load()
{
    QFile file(m_bandPlanFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    m_BandInfoList.clear();

    while (!file.atEnd())
    {
        QString line = QString::fromUtf8(file.readLine().trimmed());
        if(line.isEmpty() || line.startsWith("#"))
            continue;

        QStringList strings = line.split(",");

        if (strings.count() < 6) {
            std::cout << "BandPlan: Ignoring Line:" << std::endl;
            std::cout << "  " << line.toStdString() << std::endl;
        } else {
            BandInfo info;
            info.minFrequency = strings[0].toLongLong();
            info.maxFrequency = strings[1].toLongLong();
            info.modulation   = strings[2].trimmed();
            info.step         = strings[3].toInt();
            info.color        = QColor(strings[4].trimmed());
            info.name         = strings[5].trimmed();
            // Read visible state if present; defaults to true for old files
            if (strings.count() >= 7)
                info.visible = (strings[6].trimmed() == "1");

            m_BandInfoList.append(info);
        }
    }
    file.close();

    emit BandPlanChanged();
    return true;
}

QList<BandInfo> BandPlan::getBandsInRange(qint64 low, qint64 high)
{
    QList<BandInfo> found;
    for (int i = 0; i < m_BandInfoList.size(); i++) {
        if(m_BandInfoList[i].maxFrequency < low) continue;
        if(m_BandInfoList[i].minFrequency > high) continue;
        found.append(m_BandInfoList[i]);
    }
    return found;
}

QList<QPair<int, BandInfo>> BandPlan::getBandsInRangeWithIndex(qint64 low, qint64 high)
{
    QList<QPair<int, BandInfo>> found;
    for (int i = 0; i < m_BandInfoList.size(); i++) {
        if(m_BandInfoList[i].maxFrequency < low) continue;
        if(m_BandInfoList[i].minFrequency > high) continue;
        found.append(qMakePair(i, m_BandInfoList[i]));
    }
    return found;
}

QList<BandInfo> BandPlan::getBandsEncompassing(qint64 freq)
{
    QList<BandInfo> found;
    for (int i = 0; i < m_BandInfoList.size(); i++) {
        if(m_BandInfoList[i].maxFrequency < freq) continue;
        if(m_BandInfoList[i].minFrequency > freq) continue;
        found.append(m_BandInfoList[i]);
    }
    return found;
}

bool BandPlan::save()
{
    QFile file(m_bandPlanFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << "# Band Plan for Gqrx\n";
    out << "# minFrequency,maxFrequency,modulation,step,color,name,visible\n";

    for (int i = 0; i < m_BandInfoList.size(); i++)
    {
        const BandInfo& info = m_BandInfoList[i];
        out << info.minFrequency << ","
            << info.maxFrequency << ","
            << info.modulation << ","
            << info.step << ","
            << info.color.name() << ","
            << info.name << ","
            << (info.visible ? "1" : "0") << "\n";
    }

    file.close();
    return true;
}

void BandPlan::addBand(const BandInfo& band)
{
    m_BandInfoList.append(band);
    std::sort(m_BandInfoList.begin(), m_BandInfoList.end());
    emit BandPlanChanged();
}

void BandPlan::removeBand(int index)
{
    if (index >= 0 && index < m_BandInfoList.size())
    {
        m_BandInfoList.removeAt(index);
        emit BandPlanChanged();
    }
}

void BandPlan::updateBand(int index, const BandInfo& band)
{
    if (index >= 0 && index < m_BandInfoList.size())
    {
        m_BandInfoList[index] = band;
        std::sort(m_BandInfoList.begin(), m_BandInfoList.end());
        emit BandPlanChanged();
    }
}
