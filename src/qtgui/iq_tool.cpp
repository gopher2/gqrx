/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2014 Alexandru Csete OZ9AEC.
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
#include <QMessageBox>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QScrollBar>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

#include <math.h>

#include "iq_tool.h"
#include "ui_iq_tool.h"


CIqTool::CIqTool(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CIqTool)
{

    ui->setupUi(this);

    is_recording = false;
    is_playing = false;
    bytes_per_sample = 8;
    sample_rate = 192000;
    rec_len = 0;
    center_freq = 1e8;


    //ui->recDirEdit->setText(QDir::currentPath());

    recdir = new QDir(QDir::homePath(), "*.raw");
    recdir->setNameFilters(recdir->nameFilters() << "*.sigmf-data");

    error_palette = new QPalette();
    error_palette->setColor(QPalette::Text, Qt::red);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(timeoutFunction()));
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
void CIqTool::on_listWidget_currentTextChanged(const QString &currentText)
{

    current_file = currentText;
    QFileInfo info(*recdir, current_file);


    parseFileName(currentText);
    rec_len = (int)(info.size() / (sample_rate * bytes_per_sample));


    // Get duration of selected recording and update label
    refreshTimeWidgets();
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
            if (ui->listWidget->count() == 0)
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
            QString filePath = recdir->absoluteFilePath(current_file);
            ui->listWidget->setEnabled(false);
            ui->recButton->setEnabled(false);
            emit startPlayback(filePath, (float)sample_rate, center_freq);
        }
    }
    else
    {
        emit stopPlayback();
        ui->listWidget->setEnabled(true);
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
    ui->listWidget->setEnabled(true);
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
        int rowCount = ui->listWidget->count();
        ui->listWidget->setCurrentRow(rowCount-1);
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
    {
        ui->recButton->click(); // emulate a button click
    }
    else
    {
        qDebug() << __func__ << "No IQ recording in progress";
    }
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

/*! \brief Catch window close events.
 *
 * This method is called when the user closes the audio options dialog
 * window using the window close icon. We catch the event and hide the
 * dialog but keep it around for later use.
 */
void CIqTool::closeEvent(QCloseEvent *event)
{
    timer->stop();
    hide();
    event->ignore();
}

/*! \brief Catch window show events. */
void CIqTool::showEvent(QShowEvent * event)
{
    Q_UNUSED(event);
    refreshDir();
    refreshTimeWidgets();
    timer->start(1000);
}


void CIqTool::saveSettings(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    // Location of baseband recordings
    QString dir = recdir->path();
    if (dir != QDir::homePath())
    {
        settings->setValue("baseband/rec_dir", dir);
    }
    else
    {
        settings->remove("baseband/rec_dir");
    }

    QString format = ui->formatCombo->currentText();
    if (format != "Raw")
    {
        settings->setValue("baseband/rec_format", format);
    }
    else
    {
        settings->remove("baseband/rec_format");
    }
}

void CIqTool::readSettings(QSettings *settings)
{
    if (!settings)
    {
        return;
    }

    // Location of baseband recordings
    QString dir = settings->value("baseband/rec_dir", QDir::homePath()).toString();
    ui->recDirEdit->setText(dir);

    // Format of baseband recordings
    QString format = settings->value("baseband/rec_format", "Raw").toString();
    ui->formatCombo->setCurrentText(format);
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
    {
        ui->recDirEdit->setText(dir);
    }
}

void CIqTool::timeoutFunction(void)
{
    refreshDir();

    if (is_playing)
    {
        // advance slider with one second
        int val = ui->slider->value();
        int max = ui->slider->maximum();
        if (val < max)
        {
            ui->slider->blockSignals(true);
            ui->slider->setValue(val+1);
            ui->slider->blockSignals(false);
            refreshTimeWidgets();
        }
    }
    if (is_recording)
    {
        refreshTimeWidgets();
    }
}

/*! \brief Refresh list of files in current working directory. */
void CIqTool::refreshDir()
{
    int selection = ui->listWidget->currentRow();
    QScrollBar * sc = ui->listWidget->verticalScrollBar();
    int lastScroll = sc->sliderPosition();


    recdir->refresh();
    QStringList files = recdir->entryList();


    ui->listWidget->blockSignals(true);
    ui->listWidget->clear();
    ui->listWidget->insertItems(0, files);
    ui->listWidget->setCurrentRow(selection);
    sc->setSliderPosition(lastScroll);
    ui->listWidget->blockSignals(false);


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

    QString timeText = QString("%1:%2:%3 / %4:%5:%6")
                           .arg(ph, 2, 10, QChar('0'))
                           .arg(pm, 2, 10, QChar('0'))
                           .arg(ps, 2, 10, QChar('0'))
                           .arg(lh, 2, 10, QChar('0'))
                           .arg(lm, 2, 10, QChar('0'))
                           .arg(ls, 2, 10, QChar('0'));


    ui->timeLabel->setText(timeText);
}


/*! \brief Extract sample rate and center frequency from file.
 *
 * For SigMF files (.sigmf-data), reads from the .sigmf-meta JSON sidecar.
 * For legacy files, parses from filename: gqrx_yymmdd_hhmmss_freq_samprate_fc.raw
 * For new format files, tries to parse freq from filename pattern like "145.5MHz"
 */
void CIqTool::parseFileName(const QString &filename)
{

    // Check for SigMF metadata file
    if (filename.endsWith(".sigmf-data", Qt::CaseInsensitive))
    {
        QString metaPath = recdir->absoluteFilePath(filename);
        metaPath.replace(".sigmf-data", ".sigmf-meta", Qt::CaseInsensitive);

        QFile metaFile(metaPath);
        if (metaFile.open(QIODevice::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
            metaFile.close();

            if (!doc.isNull() && doc.isObject())
            {
                QJsonObject root = doc.object();

                // Read sample rate from global
                if (root.contains("global"))
                {
                    QJsonObject global = root["global"].toObject();
                    if (global.contains("core:sample_rate"))
                    {
                        sample_rate = static_cast<qint64>(global["core:sample_rate"].toDouble());
                    }
                }

                // Read center frequency from captures
                if (root.contains("captures"))
                {
                    QJsonArray captures = root["captures"].toArray();
                    if (!captures.isEmpty())
                    {
                        QJsonObject capture = captures[0].toObject();
                        if (capture.contains("core:frequency"))
                        {
                            center_freq = static_cast<qint64>(capture["core:frequency"].toDouble());
                        }
                    }
                }
                return;  // Successfully read metadata
            }
        }
    }

    // Try new format: look for frequency pattern like "145.5MHz" or "1234567Hz"
    QRegularExpression freqMhzPattern("([0-9.]+)MHz", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression freqHzPattern("_([0-9]+)Hz", QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = freqMhzPattern.match(filename);
    if (match.hasMatch())
    {
        bool ok;
        double freqMhz = match.captured(1).toDouble(&ok);
        if (ok)
        {
            center_freq = static_cast<qint64>(freqMhz * 1e6);
        }
    }
    else
    {
        match = freqHzPattern.match(filename);
        if (match.hasMatch())
        {
            bool ok;
            center_freq = match.captured(1).toLongLong(&ok);
            if (ok)
            {
            }
        }
    }

    // Legacy format: gqrx_yymmdd_hhmmss_freq_samprate_fc.raw
    QStringList list = filename.split('_');
    if (list.size() >= 5)
    {
        bool sr_ok, center_ok;
        qint64 sr = list.at(4).toLongLong(&sr_ok);
        qint64 center = list.at(3).toLongLong(&center_ok);


        if (sr_ok)
        {
            sample_rate = sr;
        }

        if (center_ok && center > 1000)  // Sanity check - should be Hz not MHz
        {
            center_freq = center;
        }
    }

}
