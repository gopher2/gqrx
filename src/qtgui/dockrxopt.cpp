/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2011-2013 Alexandru Csete OZ9AEC.
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
#include <QDebug>
#include <QVariant>
#include <QShortcut>
#include <iostream>
#include "dockrxopt.h"
#include "ui_dockrxopt.h"
#include "applications/gqrx/tuner_manager.h"
#include "applications/gqrx/receiver_channel.h"


QStringList DockRxOpt::ModulationStrings;

// Lookup table for conversion from old settings
static const int old2new[] = {
    DockRxOpt::MODE_OFF,
    DockRxOpt::MODE_RAW,
    DockRxOpt::MODE_AM,
    DockRxOpt::MODE_NFM,
    DockRxOpt::MODE_WFM_MONO,
    DockRxOpt::MODE_WFM_STEREO,
    DockRxOpt::MODE_LSB,
    DockRxOpt::MODE_USB,
    DockRxOpt::MODE_CWL,
    DockRxOpt::MODE_CWU,
    DockRxOpt::MODE_WFM_STEREO_OIRT,
    DockRxOpt::MODE_AM_SYNC
};

// Filter preset table per mode, preset and lo/hi
static const int filter_preset_table[DockRxOpt::MODE_LAST][3][2] =
{   //     WIDE             NORMAL            NARROW
    {{      0,      0}, {     0,     0}, {     0,     0}},  // MODE_OFF
    {{ -15000,  15000}, { -5000,  5000}, { -1000,  1000}},  // MODE_RAW
    {{ -10000,  10000}, { -5000,  5000}, { -2500,  2500}},  // MODE_AM
    {{ -10000,  10000}, { -5000,  5000}, { -2500,  2500}},  // MODE_AMSYNC
    {{  -4000,   -100}, { -2800,  -100}, { -2400,  -300}},  // MODE_LSB
    {{    100,   4000}, {   100,  2800}, {   300,  2400}},  // MODE_USB
    {{  -1000,   1000}, {  -250,   250}, {  -100,   100}},  // MODE_CWL
    {{  -1000,   1000}, {  -250,   250}, {  -100,   100}},  // MODE_CWU
    {{ -10000,  10000}, { -5000,  5000}, { -2500,  2500}},  // MODE_NFM
    {{-100000, 100000}, {-80000, 80000}, {-60000, 60000}},  // MODE_WFM_MONO
    {{-100000, 100000}, {-80000, 80000}, {-60000, 60000}},  // MODE_WFM_STEREO
    {{-100000, 100000}, {-80000, 80000}, {-60000, 60000}}   // MODE_WFM_STEREO_OIRT
};

DockRxOpt::DockRxOpt(qint64 filterOffsetRange, QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::DockRxOpt),
    agc_is_on(true),
    hw_freq_hz(144500000),
    d_tuner_manager(nullptr),
    d_tuner_id(-1),
    d_updating_from_tuner(false)
{

    ui->setupUi(this);

    if (ModulationStrings.size() == 0)
    {
        // Keep in sync with rxopt_mode_idx and filter_preset_table
        ModulationStrings.append("Demod Off");
        ModulationStrings.append("Raw I/Q");
        ModulationStrings.append("AM");
        ModulationStrings.append("AM-Sync");
        ModulationStrings.append("LSB");
        ModulationStrings.append("USB");
        ModulationStrings.append("CW-L");
        ModulationStrings.append("CW-U");
        ModulationStrings.append("Narrow FM");
        ModulationStrings.append("WFM (mono)");
        ModulationStrings.append("WFM (stereo)");
        ModulationStrings.append("WFM (oirt)");
    }
    ui->modeSelector->addItems(ModulationStrings);

    // use same slot for filteCombo and filterShapeCombo
    connect(ui->filterShapeCombo, SIGNAL(activated(int)), this, SLOT(on_filterCombo_activated(int)));

    // demodulator options dialog
    demodOpt = new CDemodOptions(this);
    demodOpt->setCurrentPage(CDemodOptions::PAGE_FM_OPT);
    connect(demodOpt, SIGNAL(fmMaxdevSelected(float)), this, SLOT(demodOpt_fmMaxdevSelected(float)));
    connect(demodOpt, SIGNAL(fmEmphSelected(double)), this, SLOT(demodOpt_fmEmphSelected(double)));
    connect(demodOpt, SIGNAL(amDcrToggled(bool)), this, SLOT(demodOpt_amDcrToggled(bool)));
    connect(demodOpt, SIGNAL(cwOffsetChanged(int)), this, SLOT(demodOpt_cwOffsetChanged(int)));
    connect(demodOpt, SIGNAL(amSyncDcrToggled(bool)), this, SLOT(demodOpt_amSyncDcrToggled(bool)));
    connect(demodOpt, SIGNAL(amSyncPllBwSelected(float)), this, SLOT(demodOpt_amSyncPllBwSelected(float)));

    // AGC options dialog
    agcOpt = new CAgcOptions(this);
    connect(agcOpt, SIGNAL(gainChanged(int)), this, SLOT(agcOpt_gainChanged(int)));
    connect(agcOpt, SIGNAL(thresholdChanged(int)), this, SLOT(agcOpt_thresholdChanged(int)));
    connect(agcOpt, SIGNAL(decayChanged(int)), this, SLOT(agcOpt_decayChanged(int)));
    connect(agcOpt, SIGNAL(slopeChanged(int)), this, SLOT(agcOpt_slopeChanged(int)));
    connect(agcOpt, SIGNAL(hangChanged(bool)), this, SLOT(agcOpt_hangToggled(bool)));

    // Noise blanker options
    nbOpt = new CNbOptions(this);
    connect(nbOpt, SIGNAL(thresholdChanged(int,double)), this, SLOT(nbOpt_thresholdChanged(int,double)));

    /* mode setting shortcuts */
    QShortcut *mode_off_shortcut = new QShortcut(QKeySequence(Qt::Key_Exclam), this);
    QShortcut *mode_raw_shortcut = new QShortcut(QKeySequence(Qt::Key_I), this);
    QShortcut *mode_am_shortcut = new QShortcut(QKeySequence(Qt::Key_A), this);
    QShortcut *mode_nfm_shortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    QShortcut *mode_wfm_mono_shortcut = new QShortcut(QKeySequence(Qt::Key_W), this);
    QShortcut *mode_wfm_stereo_shortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_W), this);
    QShortcut *mode_lsb_shortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    QShortcut *mode_usb_shortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S), this);
    QShortcut *mode_cwl_shortcut = new QShortcut(QKeySequence(Qt::Key_C), this);
    QShortcut *mode_cwu_shortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_C), this);
    QShortcut *mode_wfm_oirt_shortcut = new QShortcut(QKeySequence(Qt::Key_O), this);
    QShortcut *mode_am_sync_shortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A), this);

    QObject::connect(mode_off_shortcut, &QShortcut::activated, this, &DockRxOpt::modeOffShortcut);
    QObject::connect(mode_raw_shortcut, &QShortcut::activated, this, &DockRxOpt::modeRawShortcut);
    QObject::connect(mode_am_shortcut, &QShortcut::activated, this, &DockRxOpt::modeAMShortcut);
    QObject::connect(mode_nfm_shortcut, &QShortcut::activated, this, &DockRxOpt::modeNFMShortcut);
    QObject::connect(mode_wfm_mono_shortcut, &QShortcut::activated, this, &DockRxOpt::modeWFMmonoShortcut);
    QObject::connect(mode_wfm_stereo_shortcut, &QShortcut::activated, this, &DockRxOpt::modeWFMstereoShortcut);
    QObject::connect(mode_lsb_shortcut, &QShortcut::activated, this, &DockRxOpt::modeLSBShortcut);
    QObject::connect(mode_usb_shortcut, &QShortcut::activated, this, &DockRxOpt::modeUSBShortcut);
    QObject::connect(mode_cwl_shortcut, &QShortcut::activated, this, &DockRxOpt::modeCWLShortcut);
    QObject::connect(mode_cwu_shortcut, &QShortcut::activated, this, &DockRxOpt::modeCWUShortcut);
    QObject::connect(mode_wfm_oirt_shortcut, &QShortcut::activated, this, &DockRxOpt::modeWFMoirtShortcut);
    QObject::connect(mode_am_sync_shortcut, &QShortcut::activated, this, &DockRxOpt::modeAMsyncShortcut);

    /* squelch shortcuts */
    QShortcut *squelch_reset_shortcut = new QShortcut(QKeySequence(Qt::Key_QuoteLeft), this);
    QShortcut *squelch_auto_shortcut = new QShortcut(QKeySequence(Qt::Key_AsciiTilde), this);

    QObject::connect(squelch_reset_shortcut, &QShortcut::activated, this, &DockRxOpt::on_resetSquelchButton_clicked);
    QObject::connect(squelch_auto_shortcut, &QShortcut::activated, this, &DockRxOpt::on_autoSquelchButton_clicked);

    /* filter width shortcuts */
    QShortcut *filter_narrow_shortcut = new QShortcut(QKeySequence(Qt::Key_Less), this);
    QShortcut *filter_normal_shortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    QShortcut *filter_wide_shortcut = new QShortcut(QKeySequence(Qt::Key_Greater), this);

    QObject::connect(filter_narrow_shortcut, &QShortcut::activated, this, &DockRxOpt::filterNarrowShortcut);
    QObject::connect(filter_normal_shortcut, &QShortcut::activated, this, &DockRxOpt::filterNormalShortcut);
    QObject::connect(filter_wide_shortcut, &QShortcut::activated, this, &DockRxOpt::filterWideShortcut);
}

DockRxOpt::~DockRxOpt()
{
    delete ui;
    delete demodOpt;
    delete agcOpt;
    delete nbOpt;
}

/**
 * @brief Set value of channel filter offset selector.
 * @param freq_hz The frequency in Hz
 */
void DockRxOpt::setFilterOffset(qint64 freq_hz)
{
    // Filter offset display removed - shown in tuner list instead
    Q_UNUSED(freq_hz);
}

/**
 * @brief Set filter offset range.
 * @param range_hz The new range in Hz.
 */
void DockRxOpt::setFilterOffsetRange(qint64 range_hz)
{
    // Filter offset display removed - shown in tuner list instead
    Q_UNUSED(range_hz);
}

/**
 * @brief Set new RF frequency
 * @param freq_hz The frequency in Hz
 *
 * RF frequency is the frequency to which the device device is tuned to
 * The actual RX frequency is the sum of the RF frequency and the filter
 * offset.
 */
void DockRxOpt::setHwFreq(qint64 freq_hz)
{
    // Hardware freq display removed - shown above FFT instead
    hw_freq_hz = freq_hz;
}

/** Update RX frequency label. */
void DockRxOpt::updateHwFreq()
{
    // Hardware freq display removed - shown above FFT instead
}

/**
 * Get filter index from filter LO / HI values.
 * @param lo The filter low cut frequency.
 * @param hi The filter high cut frequency.
 *
 * Given filter low and high cut frequencies, this function checks whether the
 * filter settings correspond to one of the presets in filter_preset_table and
 * returns the corresponding index to ui->filterCombo;
 */
unsigned int DockRxOpt::filterIdxFromLoHi(int lo, int hi) const
{
    int mode_index = ui->modeSelector->currentIndex();

    if (lo == filter_preset_table[mode_index][FILTER_PRESET_WIDE][0] &&
        hi == filter_preset_table[mode_index][FILTER_PRESET_WIDE][1])
    {
        return FILTER_PRESET_WIDE;
    }
    else if (lo == filter_preset_table[mode_index][FILTER_PRESET_NORMAL][0] &&
             hi == filter_preset_table[mode_index][FILTER_PRESET_NORMAL][1])
    {
        return FILTER_PRESET_NORMAL;
    }
    else if (lo == filter_preset_table[mode_index][FILTER_PRESET_NARROW][0] &&
             hi == filter_preset_table[mode_index][FILTER_PRESET_NARROW][1])
    {
        return FILTER_PRESET_NARROW;
    }

    return FILTER_PRESET_USER;
}

/**
 * @brief Set filter parameters
 * @param lo Low cutoff frequency in Hz
 * @param hi High cutoff frequency in Hz.
 *
 * This function will automatically select the "User" preset in the
 * combo box.
 */
void DockRxOpt::setFilterParam(int lo, int hi)
{
    int filter_index = filterIdxFromLoHi(lo, hi);

    ui->filterCombo->setCurrentIndex(filter_index);
    if (filter_index == FILTER_PRESET_USER)
    {
        int width = abs(hi - lo);
        QString width_str;
        if (width >= 1000) {
            width_str = QString("Custom %1 kHz").arg(width / 1000.0, 0, 'f', 1);
        } else {
            width_str = QString("Custom %1 Hz").arg(width);
        }
        ui->filterCombo->setItemText(FILTER_PRESET_USER, width_str);
    }
}

/**
 * @brief Select new filter preset.
 * @param index Index of the new filter preset (0=wide, 1=normal, 2=narrow).
 */
void DockRxOpt::setCurrentFilter(int index)
{
    ui->filterCombo->setCurrentIndex(index);
}

/**
 * @brief Get current filter preset.
 * @param The current filter preset (0=wide, 1=normal, 2=narrow).
 */
int  DockRxOpt::currentFilter() const
{
    return ui->filterCombo->currentIndex();
}

/** Select filter shape */
void DockRxOpt::setCurrentFilterShape(int index)
{
    ui->filterShapeCombo->setCurrentIndex(index);
}

int  DockRxOpt::currentFilterShape() const
{
    return ui->filterShapeCombo->currentIndex();
}

/**
 * @brief Select new demodulator.
 * @param demod Demodulator index corresponding to receiver::demod.
 */
void DockRxOpt::setCurrentDemod(int demod)
{
    if ((demod >= MODE_OFF) && (demod < MODE_LAST))
    {
        ui->modeSelector->setCurrentIndex(demod);
        updateDemodOptPage(demod);
    }
}

/**
 * @brief Get current demodulator selection.
 * @return The current demodulator corresponding to receiver::demod.
 */
int  DockRxOpt::currentDemod() const
{
    return ui->modeSelector->currentIndex();
}

QString DockRxOpt::currentDemodAsString() const
{
    return GetStringForModulationIndex(currentDemod());
}

float DockRxOpt::currentMaxdev() const
{
    return demodOpt->getMaxDev();
}

double DockRxOpt::currentEmph() const
{
    return demodOpt->getEmph();
}

/**
 * @brief Set squelch level.
 * @param level Squelch level in dBFS
 */
void DockRxOpt::setSquelchLevel(double level)
{
    ui->sqlSpinBox->setValue(level);
}

double DockRxOpt::getSqlLevel(void) const
{
    return ui->sqlSpinBox->value();
}

/**
 * @brief Get the current squelch level
 * @returns The current squelch setting in dBFS
 */
double DockRxOpt::currentSquelchLevel() const
{
    return ui->sqlSpinBox->value();
}

bool DockRxOpt::currentAmDcr() const
{
    return demodOpt->getDcr();
}

bool DockRxOpt::currentAmsyncDcr() const
{
    return demodOpt->getSyncDcr();
}

float DockRxOpt::currentAmsyncPll() const
{
    return demodOpt->getPllBw();
}

/** Get filter lo/hi for a given mode and preset */
void DockRxOpt::getFilterPreset(int mode, int preset, int * lo, int * hi) const
{
    if (mode < 0 || mode >= MODE_LAST)
    {
        qDebug() << __func__ << ": Invalid mode:" << mode;
        mode = MODE_AM;
    }
    else if (preset < 0 || preset > 2)
    {
        qDebug() << __func__ << ": Invalid preset:" << preset;
        preset = FILTER_PRESET_NORMAL;
    }
    *lo = filter_preset_table[mode][preset][0];
    *hi = filter_preset_table[mode][preset][1];
}

int DockRxOpt::getCwOffset() const
{
    return demodOpt->getCwOffset();
}

/** Read receiver configuration from settings data. */
void DockRxOpt::readSettings(QSettings *settings)
{
    bool    conv_ok;
    int     int_val;
    double  dbl_val;

    int_val = settings->value("receiver/cwoffset", 700).toInt(&conv_ok);
    if (conv_ok)
        demodOpt->setCwOffset(int_val);

    int_val = settings->value("receiver/fm_maxdev", 5000).toInt(&conv_ok);
    if (conv_ok)
        demodOpt->setMaxDev(int_val);

    dbl_val = settings->value("receiver/fm_deemph", 75).toDouble(&conv_ok);
    if (conv_ok && dbl_val >= 0)
        demodOpt->setEmph(1.0e-6 * dbl_val); // was stored as usec

    qint64 offs = settings->value("receiver/offset", 0).toInt(&conv_ok);
    if (offs)
    {
        setFilterOffset(offs);
        emit filterOffsetChanged(offs);
    }

    dbl_val = settings->value("receiver/sql_level", 1.0).toDouble(&conv_ok);
    if (conv_ok && dbl_val < 1.0)
        ui->sqlSpinBox->setValue(dbl_val);

    // AGC settings
    int_val = settings->value("receiver/agc_threshold", -100).toInt(&conv_ok);
    if (conv_ok)
        agcOpt->setThreshold(int_val);

    int_val = settings->value("receiver/agc_decay", 500).toInt(&conv_ok);
    if (conv_ok)
    {
        agcOpt->setDecay(int_val);
        if (int_val == 100)
            ui->agcPresetCombo->setCurrentIndex(0);
        else if (int_val == 500)
            ui->agcPresetCombo->setCurrentIndex(1);
        else if (int_val == 2000)
            ui->agcPresetCombo->setCurrentIndex(2);
        else
            ui->agcPresetCombo->setCurrentIndex(3);
    }

    int_val = settings->value("receiver/agc_slope", 0).toInt(&conv_ok);
    if (conv_ok)
        agcOpt->setSlope(int_val);

    int_val = settings->value("receiver/agc_gain", 0).toInt(&conv_ok);
    if (conv_ok)
        agcOpt->setGain(int_val);

    agcOpt->setHang(settings->value("receiver/agc_usehang", false).toBool());

    if (settings->value("receiver/agc_off", false).toBool())
        ui->agcPresetCombo->setCurrentIndex(4);

    demodOpt->setDcr(settings->value("receiver/am_dcr", true).toBool());

    demodOpt->setSyncDcr(settings->value("receiver/amsync_dcr", true).toBool());

    int_val = settings->value("receiver/amsync_pllbw", 1000).toInt(&conv_ok);
    if (conv_ok)
        demodOpt->setPllBw(int_val / 1.0e6);

    int_val = MODE_AM;
    if (settings->contains("receiver/demod")) {
        // Try to read as string first (modern format, configversion >= 3)
        QString demodStr = settings->value("receiver/demod").toString();

        // Check if it's a valid modulation string (not just a number converted to string)
        if (IsModulationValid(demodStr)) {
            int_val = GetEnumForModulationString(demodStr);
        } else {
            // Fall back to old integer format (configversion < 3)
            int oldVal = settings->value("receiver/demod").toInt(&conv_ok);
            if (conv_ok && oldVal >= 0 && oldVal < 12) {
                int_val = old2new[oldVal];
            } else {
                int_val = MODE_AM;
            }
        }
    }

    // Only update the UI, don't emit demodSelected - channels have their own saved modes
    // The dock's demod is just for display/new channels, not for overwriting existing channel modes
    setCurrentDemod(int_val);
    // NOTE: Removed emit demodSelected(int_val) to prevent overwriting per-channel modes

}

/** Save receiver configuration to settings. */
void DockRxOpt::saveSettings(QSettings *settings)
{
    int     int_val;

    settings->setValue("receiver/demod", currentDemodAsString());

    int cwofs = demodOpt->getCwOffset();
    if (cwofs == 700)
        settings->remove("receiver/cwoffset");
    else
        settings->setValue("receiver/cwoffset", cwofs);

    // currently we do not need the decimal
    int_val = (int)demodOpt->getMaxDev();
    if (int_val == 5000)
        settings->remove("receiver/fm_maxdev");
    else
        settings->setValue("receiver/fm_maxdev", int_val);

    // save as usec
    int_val = (int)(1.0e6 * demodOpt->getEmph());
    if (int_val == 75)
        settings->remove("receiver/fm_deemph");
    else
        settings->setValue("receiver/fm_deemph", int_val);

    // Filter offset now managed by tuner list, remove from settings
    settings->remove("receiver/offset");

    qDebug() << __func__ << "*** FIXME_ SQL on/off";
    //int sql_lvl = double(ui->sqlSlider->value());  // note: dBFS*10 as int
    double sql_lvl = ui->sqlSpinBox->value();
    if (sql_lvl > -150.0)
        settings->setValue("receiver/sql_level", sql_lvl);
    else
        settings->remove("receiver/sql_level");

    // AGC settings
    int_val = agcOpt->threshold();
    if (int_val != -100)
        settings->setValue("receiver/agc_threshold", int_val);
    else
        settings->remove("receiver/agc_threshold");

    int_val = agcOpt->decay();
    if (int_val != 500)
        settings->setValue("receiver/agc_decay", int_val);
    else
        settings->remove("receiver/agc_decay");

    int_val = agcOpt->slope();
    if (int_val != 0)
        settings->setValue("receiver/agc_slope", int_val);
    else
        settings->remove("receiver/agc_slope");

    int_val = agcOpt->gain();
    if (int_val != 0)
        settings->setValue("receiver/agc_gain", int_val);
    else
        settings->remove("receiver/agc_gain");

    if (agcOpt->hang())
        settings->setValue("receiver/agc_usehang", true);
    else
        settings->remove("receiver/agc_usehang");

    // AGC Off
    if (ui->agcPresetCombo->currentIndex() == 4)
        settings->setValue("receiver/agc_off", true);
    else
        settings->remove("receiver/agc_off");

    if (!demodOpt->getDcr())
        settings->setValue("receiver/am_dcr", false);
    else
        settings->remove("receiver/am_dcr");

    if (!demodOpt->getSyncDcr())
        settings->setValue("receiver/amsync_dcr", false);
    else
        settings->remove("receiver/amsync_dcr");

    int_val = qRound(currentAmsyncPll() * 1.0e6f);
    if (int_val != 1000)
        settings->setValue("receiver/amsync_pllbw", int_val);
    else
        settings->remove("receiver/amsync_pllbw");
}

/** RX frequency changed through spin box - removed, freq shown in tuner list */
void DockRxOpt::on_freqSpinBox_valueChanged(double freq)
{
    Q_UNUSED(freq);
}

void DockRxOpt::setRxFreq(qint64 freq_hz)
{
    // Frequency display removed - shown in tuner list
    Q_UNUSED(freq_hz);
}

void DockRxOpt::setRxFreqRange(qint64 min_hz, qint64 max_hz)
{
    // Frequency display removed - shown in tuner list
    Q_UNUSED(min_hz);
    Q_UNUSED(max_hz);
}

void DockRxOpt::setResetLowerDigits(bool enabled)
{
    // Filter freq control removed
    Q_UNUSED(enabled);
}

void DockRxOpt::setInvertScrolling(bool enabled)
{
    // Filter freq control removed
    Q_UNUSED(enabled);
}

/**
 * @brief Channel filter offset has changed
 * @param freq The new filter offset in Hz
 *
 * This slot is activated when a new filter offset has been selected either
 * using the mouse or using the keyboard.
 */
void DockRxOpt::on_filterFreq_newFrequency(qint64 freq)
{
    if (d_updating_from_tuner)
    {
        return;
    }

    updateHwFreq();

    // Apply to active tuner if in multi-tuner mode
    if (d_tuner_manager) {
        int activeId = d_tuner_manager->get_active_channel();
        ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(activeId);
        if (tuner) {
            tuner->set_filter_offset((double)freq);
        }
    }

    emit filterOffsetChanged(freq);
}

/**
 * New filter preset selected.
 *
 * Instead of implementing a new signal, we simply emit demodSelected() since
 * demodulator and filter preset are tightly coupled.
 */
void DockRxOpt::on_filterCombo_activated(int index)
{
    Q_UNUSED(index);

    qDebug() << "New filter preset:" << ui->filterCombo->currentText();
    qDebug() << "            shape:" << ui->filterShapeCombo->currentIndex();

    int modeIndex = ui->modeSelector->currentIndex();
    emit demodSelected(modeIndex);
}

/**
 * @brief Mode selector activated.
 * @param New mode selection.
 *
 * This slot is activated when the user selects a new demodulator (mode change).
 * It is connected automatically by the UI constructor, and it emits the demodSelected()
 * signal.
 *
 * Note that the modes listed in the selector are different from those defined by
 * receiver::demod (we want to list LSB/USB separately but they have identical demods).
 */
void DockRxOpt::on_modeSelector_activated(int index)
{
    if (d_updating_from_tuner)
    {
        return;
    }

    updateDemodOptPage(index);

    // Apply to active tuner if in multi-tuner mode
    if (d_tuner_manager) {
        int activeId = d_tuner_manager->get_active_channel();
        ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(activeId);
        if (tuner) {
            // Map DockRxOpt mode index to ReceiverChannel::rx_demod
            // Note: ReceiverChannel uses RX_DEMOD_SSB for all SSB modes (LSB, USB, CW)
            ReceiverChannel::rx_demod demod = ReceiverChannel::RX_DEMOD_OFF;
            switch (index) {
                case MODE_OFF:      demod = ReceiverChannel::RX_DEMOD_OFF;
                                    break;
                case MODE_RAW:      demod = ReceiverChannel::RX_DEMOD_NONE;
                                    break;
                case MODE_AM:       demod = ReceiverChannel::RX_DEMOD_AM;
                                    break;
                case MODE_AM_SYNC:  demod = ReceiverChannel::RX_DEMOD_AMSYNC;
                                    break;
                case MODE_LSB:      demod = ReceiverChannel::RX_DEMOD_SSB;
                                    break;
                case MODE_USB:      demod = ReceiverChannel::RX_DEMOD_SSB;
                                    break;
                case MODE_CWL:      demod = ReceiverChannel::RX_DEMOD_SSB;
                                    break;
                case MODE_CWU:      demod = ReceiverChannel::RX_DEMOD_SSB;
                                    break;
                case MODE_NFM:      demod = ReceiverChannel::RX_DEMOD_NFM;
                                    break;
                case MODE_WFM_MONO: demod = ReceiverChannel::RX_DEMOD_WFM_M;
                                    break;
                case MODE_WFM_STEREO: demod = ReceiverChannel::RX_DEMOD_WFM_S;
                                    break;
                case MODE_WFM_STEREO_OIRT: demod = ReceiverChannel::RX_DEMOD_WFM_S_OIRT;
                                    break;
                default:            demod = ReceiverChannel::RX_DEMOD_NFM;
                                    break;
            }
            tuner->set_demod(demod);
        }
    }

    emit demodSelected(index);
}

void DockRxOpt::updateDemodOptPage(int demod)
{
    // update demodulator option widget
    if (demod == MODE_NFM)
    {
        demodOpt->setCurrentPage(CDemodOptions::PAGE_FM_OPT);
    }
    else if (demod == MODE_AM)
    {
        demodOpt->setCurrentPage(CDemodOptions::PAGE_AM_OPT);
    }
    else if (demod == MODE_CWL || demod == MODE_CWU)
    {
        demodOpt->setCurrentPage(CDemodOptions::PAGE_CW_OPT);
    }
    else if (demod == MODE_AM_SYNC)
    {
        demodOpt->setCurrentPage(CDemodOptions::PAGE_AMSYNC_OPT);
    }
    else
    {
        demodOpt->setCurrentPage(CDemodOptions::PAGE_NO_OPT);
    }
}

/** Show demodulator options. */
void DockRxOpt::on_modeButton_clicked()
{
    demodOpt->show();
}

/** Show AGC options. */
void DockRxOpt::on_agcButton_clicked()
{
    agcOpt->show();
}

/**
 * @brief Auto-squelch button clicked.
 *
 * This slot is called when the user clicks on the auto-squelch button.
 */
void DockRxOpt::on_autoSquelchButton_clicked()
{
    double newval = sqlAutoClicked(); // FIXME: We rely on signal only being connected to one slot
    ui->sqlSpinBox->setValue(newval);
}

void DockRxOpt::on_resetSquelchButton_clicked()
{
    ui->sqlSpinBox->setValue(-150.0);
}

/** AGC preset has changed. */
void DockRxOpt::on_agcPresetCombo_currentIndexChanged(int index)
{
    if (d_updating_from_tuner)
    {
        return;
    }

    CAgcOptions::agc_preset_e preset = (CAgcOptions::agc_preset_e) index;

    switch (preset)
    {
    case CAgcOptions::AGC_FAST:
    case CAgcOptions::AGC_MEDIUM:
    case CAgcOptions::AGC_SLOW:
    case CAgcOptions::AGC_USER:
        if (!agc_is_on)
        {
            // Apply to active tuner if in multi-tuner mode
            if (d_tuner_manager) {
                int activeId = d_tuner_manager->get_active_channel();
                ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(activeId);
                if (tuner) {
                    tuner->set_agc_on(true);
                }
            }
            emit agcToggled(true);
            agc_is_on = true;
        }
        agcOpt->setPreset(preset);
        break;

    case CAgcOptions::AGC_OFF:
        if (agc_is_on)
        {
            // Apply to active tuner if in multi-tuner mode
            if (d_tuner_manager) {
                int activeId = d_tuner_manager->get_active_channel();
                ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(activeId);
                if (tuner) {
                    tuner->set_agc_on(false);
                }
            }
            emit agcToggled(false);
            agc_is_on = false;
        }
        agcOpt->setPreset(preset);
        break;

    default:
        qDebug() << "Invalid AGC preset:" << index;
    }
}

void DockRxOpt::agcOpt_hangToggled(bool checked)
{
    emit agcHangToggled(checked);
}

/**
 * @brief AGC threshold ("knee") changed.
 * @param value The new AGC threshold in dB.
 */
void DockRxOpt::agcOpt_thresholdChanged(int value)
{
    emit agcThresholdChanged(value);
}

/**
 * @brief AGC slope factor changed.
 * @param value The new slope factor in dB.
 */
void DockRxOpt::agcOpt_slopeChanged(int value)
{
    emit agcSlopeChanged(value);
}

/**
 * @brief AGC decay changed.
 * @param value The new decay rate in ms (tbc).
 */
void DockRxOpt::agcOpt_decayChanged(int value)
{
    emit agcDecayChanged(value);
}

/**
 * @brief AGC manual gain changed.
 * @param gain The new gain in dB.
 */
void DockRxOpt::agcOpt_gainChanged(int gain)
{
    emit agcGainChanged(gain);
}

/**
 * @brief Squelch level change.
 * @param value The new squelch level in dB.
 */
void DockRxOpt::on_sqlSpinBox_valueChanged(double value)
{
    if (d_updating_from_tuner)
    {
        return;
    }

    // Apply to active tuner if in multi-tuner mode
    if (d_tuner_manager) {
        int activeId = d_tuner_manager->get_active_channel();
        ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(activeId);
        if (tuner) {
            tuner->set_sql_level(value);
        }
    }

    emit sqlLevelChanged(value);
}

/**
 * @brief FM deviation changed by user.
 * @param max_dev The new deviation in Hz.
 */
void DockRxOpt::demodOpt_fmMaxdevSelected(float max_dev)
{
    emit fmMaxdevSelected(max_dev);
}

/**
 * @brief FM de-emphasis changed by user.
 * @param tau The new time constant in uS.
 */
void DockRxOpt::demodOpt_fmEmphSelected(double tau)
{
    emit fmEmphSelected(tau);
}

/**
 * @brief AM DC removal toggled by user.
 * @param enabled Whether DCR is enabled or not.
 */
void DockRxOpt::demodOpt_amDcrToggled(bool enabled)
{
    emit amDcrToggled(enabled);
}

void DockRxOpt::demodOpt_cwOffsetChanged(int offset)
{
    emit cwOffsetChanged(offset);
}

/**
 * @brief AM-Sync DC removal toggled by user.
 * @param enabled Whether DCR is enabled or not.
 */
void DockRxOpt::demodOpt_amSyncDcrToggled(bool enabled)
{
    emit amSyncDcrToggled(enabled);
}

/**
 * @brief AM-Sync PLL BW changed by user.
 * @param pll_bw The new PLL BW.
 */
void DockRxOpt::demodOpt_amSyncPllBwSelected(float pll_bw)
{
    emit amSyncPllBwSelected(pll_bw);
}

/** Noise blanker 1 button has been toggled. */
void DockRxOpt::on_nb1Button_toggled(bool checked)
{
    float threshold = (float) nbOpt->nbThreshold(1);
    emit noiseBlankerChanged(1, checked, threshold);
}

/** Noise blanker 2 button has been toggled. */
void DockRxOpt::on_nb2Button_toggled(bool checked)
{
    float threshold = (float) nbOpt->nbThreshold(2);
    emit noiseBlankerChanged(2, checked, threshold);
}

/** Noise blanker threshold has been changed. */
void DockRxOpt::nbOpt_thresholdChanged(int nbid, double value)
{
    if (nbid == 1)
    {
        emit noiseBlankerChanged(nbid, ui->nb1Button->isChecked(), (float) value);
    }
    else
    {
        emit noiseBlankerChanged(nbid, ui->nb2Button->isChecked(), (float) value);
    }
}

void DockRxOpt::on_nbOptButton_clicked()
{
    nbOpt->show();
}

int DockRxOpt::GetEnumForModulationString(QString param)
{
    int iModulation = -1;
    for(int i=0; i<DockRxOpt::ModulationStrings.size(); ++i)
    {
        QString& strModulation = DockRxOpt::ModulationStrings[i];
        if(param.compare(strModulation, Qt::CaseInsensitive)==0)
        {
            iModulation = i;
            break;
        }
    }
    if(iModulation == -1)
    {
        std::cout << "Modulation '" << param.toStdString() << "' is unknown." << std::endl;
        iModulation = MODE_OFF;
    }
    return iModulation;
}

bool DockRxOpt::IsModulationValid(QString strModulation)
{
    return DockRxOpt::ModulationStrings.contains(strModulation, Qt::CaseInsensitive);
}

QString DockRxOpt::GetStringForModulationIndex(int iModulationIndex)
{
    return ModulationStrings[iModulationIndex];
}

void DockRxOpt::modeOffShortcut() {
    on_modeSelector_activated(MODE_OFF);
}

void DockRxOpt::modeRawShortcut() {
    on_modeSelector_activated(MODE_RAW);
}

void DockRxOpt::modeAMShortcut() {
    on_modeSelector_activated(MODE_AM);
}

void DockRxOpt::modeNFMShortcut() {
    on_modeSelector_activated(MODE_NFM);
}

void DockRxOpt::modeWFMmonoShortcut() {
    on_modeSelector_activated(MODE_WFM_MONO);
}

void DockRxOpt::modeWFMstereoShortcut() {
    on_modeSelector_activated(MODE_WFM_STEREO);
}

void DockRxOpt::modeLSBShortcut() {
    on_modeSelector_activated(MODE_LSB);
}

void DockRxOpt::modeUSBShortcut() {
    on_modeSelector_activated(MODE_USB);
}

void DockRxOpt::modeCWLShortcut() {
    on_modeSelector_activated(MODE_CWL);
}

void DockRxOpt::modeCWUShortcut() {
    on_modeSelector_activated(MODE_CWU);
}

void DockRxOpt::modeWFMoirtShortcut() {
    on_modeSelector_activated(MODE_WFM_STEREO_OIRT);
}

void DockRxOpt::modeAMsyncShortcut() {
    on_modeSelector_activated(MODE_AM_SYNC);
}

void DockRxOpt::filterNarrowShortcut() {
    setCurrentFilter(FILTER_PRESET_NARROW);
    on_filterCombo_activated(FILTER_PRESET_NARROW);
}

void DockRxOpt::filterNormalShortcut() {
    setCurrentFilter(FILTER_PRESET_NORMAL);
    on_filterCombo_activated(FILTER_PRESET_NORMAL);
}

void DockRxOpt::filterWideShortcut() {
    setCurrentFilter(FILTER_PRESET_WIDE);
    on_filterCombo_activated(FILTER_PRESET_WIDE);
}

// Multi-tuner support methods

void DockRxOpt::setTunerManager(TunerManager* manager)
{
    d_tuner_manager = manager;
}

void DockRxOpt::setTunerId(int tuner_id)
{
    d_tuner_id = tuner_id;

    // Update UI from this specific tuner
    if (d_tuner_manager && tuner_id >= 0) {
        ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(tuner_id);
        if (tuner) {
            updateUiFromTuner(tuner);
        }
    }
}

void DockRxOpt::setTunerColor(const QColor& color)
{
    // Set the color indicator using the UI label
    if (ui->tunerColorLabel) {
        ui->tunerColorLabel->setFixedSize(16, 16);
        ui->tunerColorLabel->setStyleSheet(QString(
            "background-color: %1; border: 1px solid gray; border-radius: 2px;"
        ).arg(color.name()));
    }
}

void DockRxOpt::onActiveTunerChanged(int tuner_id)
{
    if (!d_tuner_manager)
    {
        return;
    }

    ReceiverChannel* tuner = d_tuner_manager->get_channel_impl(tuner_id);
    if (tuner) {
        updateUiFromTuner(tuner);
        // Update the dock title to show which tuner is active
        QString title = QString("Receiver Options - Tuner %1").arg(tuner_id);
        setWindowTitle(title);
    }
}

void DockRxOpt::updateUiFromTuner(ReceiverChannel* tuner)
{
    if (!tuner)
    {
        return;
    }

    // Set flag to prevent signal feedback loops
    d_updating_from_tuner = true;

    // Filter offset and hardware frequency display removed - shown elsewhere

    // Update demodulation mode
    // Map ReceiverChannel::rx_demod to DockRxOpt mode index
    // Note: ReceiverChannel uses RX_DEMOD_SSB for all SSB modes, so we default to USB
    int demod = tuner->get_demod();
    int modeIdx = MODE_OFF;
    switch (demod) {
        case ReceiverChannel::RX_DEMOD_OFF:
            modeIdx = MODE_OFF;
            break;
        case ReceiverChannel::RX_DEMOD_NONE:
            modeIdx = MODE_RAW;
            break;
        case ReceiverChannel::RX_DEMOD_NFM:
            modeIdx = MODE_NFM;
            break;
        case ReceiverChannel::RX_DEMOD_AM:
            modeIdx = MODE_AM;
            break;
        case ReceiverChannel::RX_DEMOD_SSB:
            modeIdx = MODE_USB;  // Default SSB to USB
            break;
        case ReceiverChannel::RX_DEMOD_WFM_M:
            modeIdx = MODE_WFM_MONO;
            break;
        case ReceiverChannel::RX_DEMOD_WFM_S:
            modeIdx = MODE_WFM_STEREO;
            break;
        case ReceiverChannel::RX_DEMOD_WFM_S_OIRT:
            modeIdx = MODE_WFM_STEREO_OIRT;
            break;
        case ReceiverChannel::RX_DEMOD_AMSYNC:
            modeIdx = MODE_AM_SYNC;
            break;
        default:
            modeIdx = MODE_NFM;
            break;
    }
    ui->modeSelector->setCurrentIndex(modeIdx);
    updateDemodOptPage(modeIdx);

    // Update squelch level
    double sqlLevel = tuner->get_sql_level();
    ui->sqlSpinBox->setValue(sqlLevel);

    // Update AGC state
    bool agcOn = tuner->get_agc_on();
    agc_is_on = agcOn;
    ui->agcPresetCombo->setEnabled(agcOn);

    // Clear the updating flag
    d_updating_from_tuner = false;

}
