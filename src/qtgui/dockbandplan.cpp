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
#include <QMessageBox>
#include <QColorDialog>
#include <QHeaderView>
#include "dockbandplan.h"
#include "ui_dockbandplan.h"
#include "bandplan.h"

DockBandplan::DockBandplan(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::DockBandplan)
{
    ui->setupUi(this);

    m_tableModel = new BandPlanTableModel(this);

    ui->tableView->setModel(m_tableModel);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->horizontalHeader()->setStretchLastSection(false);
    ui->tableView->horizontalHeader()->setSectionResizeMode(BandPlanTableModel::COL_NAME, QHeaderView::Stretch);
    ui->tableView->verticalHeader()->setVisible(true);
    ui->tableView->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);

    // Set column widths
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_VISIBLE, 40);
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_NAME, 150);
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_MIN_FREQ, 100);
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_MAX_FREQ, 100);
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_MODULATION, 80);
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_STEP, 60);
    ui->tableView->setColumnWidth(BandPlanTableModel::COL_COLOR, 40);

    // Context menu
    m_contextMenu = new QMenu(this);
    QAction* gotoAction = new QAction("Go to Center of Band", this);
    m_contextMenu->addAction(gotoAction);
    connect(gotoAction, &QAction::triggered, this, &DockBandplan::gotoCenterOfBand);
    m_contextMenu->addSeparator();
    QAction* deleteAction = new QAction("Delete Band", this);
    m_contextMenu->addAction(deleteAction);
    connect(deleteAction, &QAction::triggered, this, &DockBandplan::deleteSelectedBand);

    ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableView, &QTableView::customContextMenuRequested,
            this, &DockBandplan::showContextMenu);

    connect(ui->tableView, &QTableView::clicked,
            this, &DockBandplan::cellClicked);
    connect(ui->tableView, &QTableView::doubleClicked,
            this, &DockBandplan::doubleClicked);

    connect(&BandPlan::Get(), &BandPlan::BandPlanChanged,
            m_tableModel, &BandPlanTableModel::update);

    m_tableModel->update();
}

DockBandplan::~DockBandplan()
{
    delete ui;
}

void DockBandplan::updateBands()
{
    m_tableModel->update();
}

void DockBandplan::selectBand(int index)
{
    if (index >= 0 && index < m_tableModel->rowCount())
    {
        QModelIndex modelIndex = m_tableModel->index(index, 0);
        ui->tableView->setCurrentIndex(modelIndex);
        ui->tableView->scrollTo(modelIndex);
        // Make sure the dock is visible
        if (!isVisible())
            show();
        raise();
    }
}

void DockBandplan::on_addButton_clicked()
{
    BandInfo newBand;
    newBand.minFrequency = 0;
    newBand.maxFrequency = 1000000;
    newBand.name = "New Band";
    newBand.modulation = "FM";
    newBand.step = 5000;
    newBand.color = QColor("#808080");

    BandPlan::Get().addBand(newBand);
    BandPlan::Get().save();
}

void DockBandplan::on_removeButton_clicked()
{
    deleteSelectedBand();
}

void DockBandplan::doubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    int row = index.row();
    if (row >= 0 && row < BandPlan::Get().size())
    {
        BandInfo& band = BandPlan::Get().getBand(row);
        emit newFrequency(band.minFrequency);
    }
}

void DockBandplan::showContextMenu(const QPoint &pos)
{
    m_contextMenu->popup(ui->tableView->viewport()->mapToGlobal(pos));
}

void DockBandplan::deleteSelectedBand()
{
    QModelIndexList selected = ui->tableView->selectionModel()->selectedRows();

    if (selected.empty())
        return;

    if (QMessageBox::question(this, "Delete Band",
            "Really delete this band?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
    {
        int row = selected.first().row();
        BandPlan::Get().removeBand(row);
        BandPlan::Get().save();
    }
}

void DockBandplan::gotoCenterOfBand()
{
    QModelIndexList selected = ui->tableView->selectionModel()->selectedRows();

    if (selected.empty())
        return;

    int row = selected.first().row();
    if (row >= 0 && row < BandPlan::Get().size())
    {
        BandInfo& band = BandPlan::Get().getBand(row);
        qint64 centerFreq = (band.minFrequency + band.maxFrequency) / 2;
        emit newFrequency(centerFreq);
    }
}

void DockBandplan::cellClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    // Only handle clicks on the color column
    if (index.column() != BandPlanTableModel::COL_COLOR)
        return;

    int row = index.row();
    if (row < 0 || row >= BandPlan::Get().size())
        return;

    BandInfo& band = BandPlan::Get().getBand(row);
    QColor newColor = QColorDialog::getColor(band.color, this, "Select Band Color");

    if (newColor.isValid())
    {
        m_tableModel->setData(index, newColor, Qt::EditRole);
    }
}
