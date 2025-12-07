/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2014 Alexandru Csete OZ9AEC.
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
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPalette>
#include <QProcess>
#include <QStorageInfo>
#include <QString>
#include <QStringList>
#include <QScrollBar>
#include <QUrl>

#include <math.h>

#include "iq_tool.h"
#include "ui_iq_tool.h"


CIqTool::CIqTool(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::CIqTool),
    m_fileModel(new IqFileModel(this)),
    m_proxyModel(new QSortFilterProxyModel(this)),
    m_delegate(new IqFileDelegate(this))
{
    ui->setupUi(this);

    is_recording = false;
    is_playing = false;
    bytes_per_sample = 8;
    sample_rate = 192000;
    rec_len = 0;
    center_freq = 1e8;

    recdir = new QDir(QDir::homePath(), "*.raw");
    recdir->setNameFilters(recdir->nameFilters() << "*.sigmf-data");

    error_palette = new QPalette();
    error_palette->setColor(QPalette::Text, Qt::red);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timeoutFunction()));

    // QDockWidget doesn't have closeEvent/showEvent like QDialog,
    // so we use visibilityChanged instead.
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            refreshDir();
            refreshTimeWidgets();
            updateDiskSpace();
            timer->start(1000);
        } else {
            timer->stop();
        }
    });

    setupTableView();
}

CIqTool::~CIqTool()
{
    timer->stop();
    delete timer;
    delete ui;
    delete recdir;
    delete error_palette;

}

/*! \brief Set new sample rate. */
void CIqTool::setSampleRate(qint64 sr)
{
    sample_rate = sr;

    if (!current_file.isEmpty())
    {
        // Get duration of selected recording and update label
        QFileInfo info(*recdir, current_file);
        rec_len = (int)(info.size() / (sample_rate * bytes_per_sample));
        refreshTimeWidgets();
    }
}


/*! \brief Slot activated when the user selects a file. */
void CIqTool::onSelectionChanged()
{
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if (selection.isEmpty())
    {
        current_file.clear();
        rec_len = 0;
        refreshTimeWidgets();
        return;
    }

    QModelIndex proxyIndex = selection.first();
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const IqFileInfo *info = m_fileModel->getFileInfo(sourceIndex.row());

    if (info)
    {
        current_file = info->filename;

        // Use metadata from model if available, otherwise parse filename
        if (info->sampleRate > 0)
            sample_rate = info->sampleRate;
        else
            parseFileName(current_file);

        if (info->centerFreq > 0)
            center_freq = info->centerFreq;

        rec_len = static_cast<int>(info->duration);
    }
    else
    {
        current_file.clear();
        rec_len = 0;
    }

    refreshTimeWidgets();
}

/*! \brief Setup the table view with model and delegate */
void CIqTool::setupTableView()
{
    m_proxyModel->setSourceModel(m_fileModel);
    m_proxyModel->setSortRole(Qt::UserRole);

    ui->tableView->setModel(m_proxyModel);
    ui->tableView->setItemDelegate(m_delegate);

    // Configure header
    QHeaderView *header = ui->tableView->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(true);

    connect(header, &QHeaderView::customContextMenuRequested,
            this, &CIqTool::onHeaderContextMenu);

    // Configure file context menu
    ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableView, &QTableView::customContextMenuRequested,
            this, &CIqTool::onFileContextMenu);

    // Connect selection changes
    connect(ui->tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &CIqTool::onSelectionChanged);

    updateFilenameColumnIndex();
}

/*! \brief Update delegate's filename column index based on visible columns */
void CIqTool::updateFilenameColumnIndex()
{
    // Find which visible column corresponds to filename
    int visibleCol = 0;
    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
    {
        if (static_cast<IqFileColumn>(i) == IqFileColumn::Filename)
        {
            if (m_fileModel->isColumnVisible(IqFileColumn::Filename))
                m_delegate->setFilenameColumn(visibleCol);
            else
                m_delegate->setFilenameColumn(-1); // Filename column not visible
            break;
        }
        if (m_fileModel->isColumnVisible(static_cast<IqFileColumn>(i)))
            ++visibleCol;
    }
}

/*! \brief Show context menu for column visibility */
void CIqTool::onHeaderContextMenu(const QPoint &pos)
{
    QMenu menu(this);

    for (int i = 0; i < static_cast<int>(IqFileColumn::COUNT); ++i)
    {
        IqFileColumn col = static_cast<IqFileColumn>(i);
        QAction *action = menu.addAction(IqFileModel::columnName(col));
        action->setCheckable(true);
        action->setChecked(m_fileModel->isColumnVisible(col));
        action->setData(i);

        connect(action, &QAction::toggled, this, [this, col](bool checked) {
            m_fileModel->setColumnVisible(col, checked);
            updateFilenameColumnIndex();
        });
    }

    menu.exec(ui->tableView->horizontalHeader()->mapToGlobal(pos));
}

/*! \brief Show context menu for file operations */
void CIqTool::onFileContextMenu(const QPoint &pos)
{
    QModelIndex index = ui->tableView->indexAt(pos);
    if (!index.isValid())
        return;

    // Select the row under cursor if not already selected
    ui->tableView->selectRow(index.row());

    QMenu menu(this);

    QAction *openAction = menu.addAction(tr("Show in Finder"));
    connect(openAction, &QAction::triggered, this, &CIqTool::onOpenInFinder);

    QAction *editAction = menu.addAction(tr("Edit Comment..."));
    connect(editAction, &QAction::triggered, this, &CIqTool::onEditFile);

    menu.addSeparator();

    QAction *deleteAction = menu.addAction(tr("Delete"));
    connect(deleteAction, &QAction::triggered, this, &CIqTool::onDeleteFile);

    menu.exec(ui->tableView->viewport()->mapToGlobal(pos));
}

/*! \brief Open the selected file's location in Finder */
void CIqTool::onOpenInFinder()
{
    if (current_file.isEmpty())
        return;

    QString filePath = recdir->absoluteFilePath(current_file);

#ifdef Q_OS_MAC
    QProcess::startDetached("open", QStringList() << "-R" << filePath);
#elif defined(Q_OS_WIN)
    QProcess::startDetached("explorer", QStringList() << "/select," << QDir::toNativeSeparators(filePath));
#else
    // Linux - open containing folder
    QDesktopServices::openUrl(QUrl::fromLocalFile(recdir->absolutePath()));
#endif
}

/*! \brief Edit the selected file's comment/description */
void CIqTool::onEditFile()
{
    if (current_file.isEmpty())
        return;

    // Get current file info from model
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if (selection.isEmpty())
        return;

    QModelIndex proxyIndex = selection.first();
    QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
    const IqFileInfo *info = m_fileModel->getFileInfo(sourceIndex.row());
    if (!info)
        return;

    QString currentComment = info->comment;
    if (currentComment == "n/a")
        currentComment.clear();

    bool ok;
    QString newComment = QInputDialog::getText(this, tr("Edit Comment"),
                                                tr("Comment:"), QLineEdit::Normal,
                                                currentComment, &ok);
    if (!ok)
        return;

    QString filePath = recdir->absoluteFilePath(current_file);

    if (info->isSigMF)
    {
        // Edit SigMF metadata file
        QString metaPath = filePath;
        metaPath.replace(".sigmf-data", ".sigmf-meta");

        QFile metaFile(metaPath);
        if (!metaFile.open(QIODevice::ReadOnly))
        {
            QMessageBox::warning(this, tr("Edit Failed"),
                                 tr("Could not open metadata file."));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();

        if (doc.isNull() || !doc.isObject())
        {
            QMessageBox::warning(this, tr("Edit Failed"),
                                 tr("Invalid metadata file format."));
            return;
        }

        QJsonObject root = doc.object();
        QJsonObject global = root["global"].toObject();
        global["core:description"] = newComment;
        root["global"] = global;

        if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, tr("Edit Failed"),
                                 tr("Could not write metadata file."));
            return;
        }

        metaFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        metaFile.close();
    }
    else
    {
        // For raw files, create/update a JSON sidecar file
        QString metaPath = filePath + ".json";

        QJsonObject root;

        // Try to read existing sidecar
        QFile metaFile(metaPath);
        if (metaFile.open(QIODevice::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
            metaFile.close();
            if (doc.isObject())
                root = doc.object();
        }

        root["comment"] = newComment;

        if (!metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            QMessageBox::warning(this, tr("Edit Failed"),
                                 tr("Could not write metadata file."));
            return;
        }

        metaFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        metaFile.close();
    }

    // Refresh to show updated comment
    refreshDir();
}

/*! \brief Delete the selected file with confirmation */
void CIqTool::onDeleteFile()
{
    if (current_file.isEmpty())
        return;

    QString filePath = recdir->absoluteFilePath(current_file);

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Delete File"),
                                  tr("Are you sure you want to delete '%1'?").arg(current_file),
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        QFile file(filePath);
        if (file.remove())
        {
            // Also delete associated metadata file if it exists
            QString metaPath = filePath;
            if (metaPath.endsWith(".sigmf-data"))
            {
                metaPath.replace(".sigmf-data", ".sigmf-meta");
                QFile::remove(metaPath);
            }
            else if (metaPath.endsWith(".raw"))
            {
                // Check for .json sidecar
                metaPath.replace(".raw", ".json");
                QFile::remove(metaPath);
            }

            current_file.clear();
            refreshDir();
        }
        else
        {
            QMessageBox::warning(this, tr("Delete Failed"),
                                 tr("Could not delete '%1'.").arg(current_file));
        }
    }
}

/*! \brief Start/stop playback */
void CIqTool::on_playButton_clicked(bool checked)
{
    is_playing = checked;

    if (checked)
    {
        if (current_file.isEmpty())
        {
            QMessageBox msg_box;
            msg_box.setIcon(QMessageBox::Critical);
            if (m_proxyModel->rowCount() == 0)
            {
                msg_box.setText(tr("There are no I/Q files in the current directory."));
            }
            else
            {
                msg_box.setText(tr("Please select a file to play."));
            }
            msg_box.exec();

            ui->playButton->setChecked(false); // will not trig clicked()
        }
        else
        {
            ui->tableView->setEnabled(false);
            ui->recButton->setEnabled(false);
            emit startPlayback(recdir->absoluteFilePath(current_file),
                               (float)sample_rate, center_freq);
        }
    }
    else
    {
        emit stopPlayback();
        ui->tableView->setEnabled(true);
        ui->recButton->setEnabled(true);
        ui->slider->setValue(0);
    }
}

/*! \brief Cancel playback.
 *
 * This slot can be activated to cancel an ongoing playback.
 *
 * This slot should be used to signal that a playback could not be started.
 */
void CIqTool::cancelPlayback()
{
    ui->playButton->setChecked(false);
    ui->tableView->setEnabled(true);
    ui->recButton->setEnabled(true);
    is_playing = false;
}


/*! \brief Slider value (seek position) has changed. */
void CIqTool::on_slider_valueChanged(int value)
{
    refreshTimeWidgets();

    qint64 seek_pos = (qint64)(value)*sample_rate;
    emit seek(seek_pos);
}

/*! \brief Start/stop recording */
void CIqTool::on_recButton_clicked(bool checked)
{
    is_recording = checked;

    if (checked)
    {
        ui->playButton->setEnabled(false);
        emit startRecording(recdir->path(), ui->formatCombo->currentText());

        refreshDir();
        // Select last row (newest file)
        int lastRow = m_proxyModel->rowCount() - 1;
        if (lastRow >= 0)
            ui->tableView->selectRow(lastRow);
    }
    else
    {
        ui->playButton->setEnabled(true);
        emit stopRecording();
    }
}

/*! Public slot to start IQ recording by external events (e.g. remote control).
 *
 * If a recording is already in progress we ignore the event.
 */
void CIqTool::startIqRecorder(void)
{
    if (ui->recButton->isChecked())
    {
        qDebug() << __func__ << "An IQ recording is already in progress";
        return;
    }

    // emulate a button click
    ui->recButton->click();
}

/*! Public slot to stop IQ recording by external events (e.g. remote control).
 *
 * The event is ignored if no recording is in progress.
 */
void CIqTool::stopIqRecorder(void)
{
    if (ui->recButton->isChecked())
        ui->recButton->click(); // emulate a button click
    else
        qDebug() << __func__ << "No IQ recording in progress";
}

/*! \brief Cancel a recording.
 *
 * This slot can be activated to cancel an ongoing recording. Cancelling an
 * ongoing recording will stop the recording and delete the recorded file, if
 * any.
 *
 * This slot should be used to signal that a recording could not be started.
 */
void CIqTool::cancelRecording()
{
    ui->recButton->setChecked(false);
    ui->playButton->setEnabled(true);
    is_recording = false;
}

void CIqTool::saveSettings(QSettings *settings)
{
    if (!settings)
        return;

    // Location of baseband recordings
    QString dir = recdir->path();
    if (dir != QDir::homePath())
        settings->setValue("baseband/rec_dir", dir);
    else
        settings->remove("baseband/rec_dir");

    QString format = ui->formatCombo->currentText();
    if (format != "SigMF")
        settings->setValue("baseband/rec_format", format);
    else
        settings->remove("baseband/rec_format");

    // Column visibility settings
    m_fileModel->saveSettings(settings);
}

void CIqTool::readSettings(QSettings *settings)
{
    if (!settings)
        return;

    // Location of baseband recordings
    QString dir = settings->value("baseband/rec_dir", QDir::homePath()).toString();
    ui->recDirEdit->setText(dir);

    // Format of baseband recordings
    QString format = settings->value("baseband/rec_format", "SigMF").toString();
    ui->formatCombo->setCurrentText(format);

    // Column visibility settings
    m_fileModel->readSettings(settings);
    updateFilenameColumnIndex();
}


/*! \brief Slot called when the recordings directory has changed either
 *         because of user input or programmatically.
 */
void CIqTool::on_recDirEdit_textChanged(const QString &dir)
{
    if (recdir->exists(dir))
    {
        ui->recDirEdit->setPalette(QPalette());  // Clear custom color
        recdir->setPath(dir);
        recdir->cd(dir);
        updateDiskSpace();
        //emit newRecDirSelected(dir);
    }
    else
    {
        ui->recDirEdit->setPalette(*error_palette);  // indicate error
    }
}

/*! \brief Slot called when the user clicks on the "Select" button. */
void CIqTool::on_recDirButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select a directory"),
                                                    ui->recDirEdit->text(),
                                                    QFileDialog::ShowDirsOnly |
                                                    QFileDialog::DontResolveSymlinks);

    if (!dir.isNull())
        ui->recDirEdit->setText(dir);
}

void CIqTool::timeoutFunction(void)
{
    refreshDir();
    updateDiskSpace();

    if (is_playing)
    {
        // advance slider with one second
        int val = ui->slider->value();
        if (val < ui->slider->maximum())
        {
            ui->slider->blockSignals(true);
            ui->slider->setValue(val+1);
            ui->slider->blockSignals(false);
            refreshTimeWidgets();
        }
    }
    if (is_recording)
        refreshTimeWidgets();
}

/*! \brief Refresh list of files in current working directory. */
void CIqTool::refreshDir()
{
    // Save current selection
    QString selectedFile = current_file;

    // Update model's directory and refresh
    m_fileModel->setDirectory(*recdir);

    // Auto-size columns on first load (except filename which gets fixed width)
    if (!m_columnsAutoSized && m_proxyModel->rowCount() > 0)
    {
        // Set filename column to fixed width
        ui->tableView->setColumnWidth(0, 200);

        // Auto-size all other columns to fit content
        int colCount = m_proxyModel->columnCount();
        for (int col = 1; col < colCount; ++col)
            ui->tableView->resizeColumnToContents(col);

        m_columnsAutoSized = true;
    }

    // Restore selection if possible
    if (!selectedFile.isEmpty())
    {
        for (int i = 0; i < m_proxyModel->rowCount(); ++i)
        {
            QModelIndex proxyIndex = m_proxyModel->index(i, 0);
            QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
            const IqFileInfo *info = m_fileModel->getFileInfo(sourceIndex.row());
            if (info && info->filename == selectedFile)
            {
                ui->tableView->selectRow(i);
                break;
            }
        }
    }

    if (is_recording)
    {
        // update rec_len; if the file being recorded is the one selected
        // in the list, the length will update periodically
        QFileInfo info(*recdir, current_file);
        rec_len = (int)(info.size() / (sample_rate * bytes_per_sample));
    }
}

/*! \brief Refresh time labels and slider position
 *
 * \note Safe for recordings > 24 hours
 */
void CIqTool::refreshTimeWidgets(void)
{
    // Ensure rec_len is non-negative to avoid slider assertion
    if (rec_len < 0)
        rec_len = 0;
    ui->slider->setMaximum(rec_len);

    // duration
    int len = rec_len;
    int lh, lm, ls;
    lh = len / 3600;
    len = len % 3600;
    lm = len / 60;
    ls = len % 60;

    // current position
    int pos = ui->slider->value();
    int ph, pm, ps;
    ph = pos / 3600;
    pos = pos % 3600;
    pm = pos / 60;
    ps = pos % 60;

    ui->timeLabel->setText(QString("%1:%2:%3 / %4:%5:%6")
                           .arg(ph, 2, 10, QChar('0'))
                           .arg(pm, 2, 10, QChar('0'))
                           .arg(ps, 2, 10, QChar('0'))
                           .arg(lh, 2, 10, QChar('0'))
                           .arg(lm, 2, 10, QChar('0'))
                           .arg(ls, 2, 10, QChar('0')));
}

/*! \brief Update free disk space label */
void CIqTool::updateDiskSpace(void)
{
    QStorageInfo storage(recdir->absolutePath());
    if (storage.isValid())
    {
        qint64 freeBytes = storage.bytesAvailable();
        QString freeStr;

        if (freeBytes >= 1099511627776LL) // 1 TB
            freeStr = QString("%1 TB").arg(freeBytes / 1099511627776.0, 0, 'f', 1);
        else if (freeBytes >= 1073741824LL) // 1 GB
            freeStr = QString("%1 GB").arg(freeBytes / 1073741824.0, 0, 'f', 1);
        else if (freeBytes >= 1048576LL) // 1 MB
            freeStr = QString("%1 MB").arg(freeBytes / 1048576.0, 0, 'f', 0);
        else
            freeStr = QString("%1 KB").arg(freeBytes / 1024.0, 0, 'f', 0);

        ui->diskSpaceLabel->setText(tr("Free: %1").arg(freeStr));
    }
    else
    {
        ui->diskSpaceLabel->setText(tr("Free: --"));
    }
}

/*! \brief Extract sample rate and offset frequency from file name */
void CIqTool::parseFileName(const QString &filename)
{
    bool   sr_ok;
    qint64 sr;
    bool   center_ok;
    qint64 center;

    QStringList list = filename.split('_');

    if (list.size() < 5)
        return;

    // gqrx_yymmdd_hhmmss_freq_samprate_fc.raw
    sr = list.at(4).toLongLong(&sr_ok);
    center = list.at(3).toLongLong(&center_ok);

    if (sr_ok)
        sample_rate = sr;
    if (center_ok)
        center_freq = center;
}
