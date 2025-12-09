/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * RadioReference.com import dock widget
 */
#ifndef RRIMPORTDIALOG_H
#define RRIMPORTDIALOG_H

#include <QDockWidget>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QSettings>
#include "radioreference.h"

class DockRRImport : public QDockWidget
{
    Q_OBJECT

public:
    explicit DockRRImport(QWidget *parent = nullptr);
    ~DockRRImport();

    void setCredentials(const QString &username, const QString &password, const QString &appKey);
    void saveSettings(QSettings *settings);
    void readSettings(QSettings *settings);
    void setFrequencyFilter(qint64 freqHz);

signals:
    void frequenciesImported(int count);

private slots:
    void onStateSelected(int index);
    void onCountySelected(int index);
    void onMetroSelected(int index);
    void onSearchClicked();
    void onImportClicked();
    void onSelectAllClicked();
    void onDeselectAllClicked();
    void onSaveCredentialsClicked();

    void onStateListReceived(const QList<RRState> &states);
    void onCountyListReceived(const QList<RRCounty> &counties);
    void onMetroListReceived(const QList<RRMetro> &metros);
    void onFrequenciesReceived(const QList<RRFrequency> &frequencies);
    void onError(const QString &message);

private:
    void setupUi();
    void populateFrequencyTable(const QList<RRFrequency> &frequencies);
    QString modeToGqrx(const QString &rrMode);

    RadioReference *m_api;
    QList<RRFrequency> m_frequencies;
    QString m_username;
    QString m_password;
    QString m_appKey;

    // UI elements - credentials
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_appKeyEdit;
    QPushButton *m_saveCredentialsBtn;

    // UI elements - location
    QComboBox *m_stateCombo;
    QComboBox *m_countyCombo;
    QComboBox *m_metroCombo;
    QLineEdit *m_freqFilter;
    QPushButton *m_searchBtn;
    QTableWidget *m_freqTable;
    QPushButton *m_selectAllBtn;
    QPushButton *m_deselectAllBtn;
    QPushButton *m_importBtn;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QCheckBox *m_useMetroCheck;
};

#endif // RRIMPORTDIALOG_H
