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
#ifndef DOCKBANDPLAN_H
#define DOCKBANDPLAN_H

#include <QDockWidget>
#include <QMenu>
#include "bandplantablemodel.h"

namespace Ui {
    class DockBandplan;
}

class DockBandplan : public QDockWidget
{
    Q_OBJECT

public:
    explicit DockBandplan(QWidget *parent = nullptr);
    ~DockBandplan();

signals:
    void newFrequency(qint64 freq);

public slots:
    void updateBands();
    void selectBand(int index);

private slots:
    void on_addButton_clicked();
    void on_removeButton_clicked();
    void doubleClicked(const QModelIndex &index);
    void showContextMenu(const QPoint &pos);
    void deleteSelectedBand();
    void gotoCenterOfBand();

private:
    Ui::DockBandplan *ui;
    BandPlanTableModel *m_tableModel;
    QMenu *m_contextMenu;
};

#endif // DOCKBANDPLAN_H
