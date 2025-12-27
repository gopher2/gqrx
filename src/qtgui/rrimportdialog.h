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
#include <QTabWidget>
#include "radioreference.h"

class DockRRImport : public QDockWidget
{
    Q_OBJECT

public:
    explicit DockRRImport(QWidget *parent = nullptr);
    ~DockRRImport();

    void saveSettings(QSettings *settings);
    void readSettings(QSettings *settings);

signals:
    void frequenciesImported(int count);

private slots:
    void onStateSelected(int index);
    void onCountySelected(int index);
    void onMetroSelected(int index);
    void onDownloadClicked();
    void onImportClicked();
    void onSelectAllClicked();
    void onDeselectAllClicked();

    void onStateListReceived(const QList<RRState> &states);
    void onCountyListReceived(const QList<RRCounty> &counties);
    void onMetroListReceived(const QList<RRMetro> &metros);
    void onFrequenciesReceived(const QList<RRFrequency> &frequencies);
    void onFetchProgress(int current, int total, int freqsSoFar, const QString &categoryName);
    void onError(const QString &message);
    void onCategoryFilterChanged(int index);

private:
    void setupUi();
    void populateFrequencyTable(const QList<RRFrequency> &frequencies);
    void filterTable();
    QString modeToGqrx(const QString &rrMode);
    void updateDownloadButton();

    // Credential encryption (key derived from machine ID - no stored key needed)
    QString encryptCredential(const QString &plaintext);
    QString decryptCredential(const QString &ciphertext);

    RadioReference *m_api;
    QList<RRFrequency> m_frequencies;
    QString m_username;
    QString m_password;
    bool m_userInitiated = false;  // Track if action was user-initiated (for error popups)

    // UI elements
    QTabWidget *m_tabWidget;

    // UI elements - credentials
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;

    // UI elements - location
    QComboBox *m_stateCombo;
    QComboBox *m_countyCombo;
    QComboBox *m_metroCombo;
    QPushButton *m_downloadBtn;
    QComboBox *m_categoryFilter;
    QTableWidget *m_freqTable;
    QPushButton *m_selectAllBtn;
    QPushButton *m_deselectAllBtn;
    QPushButton *m_importBtn;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QCheckBox *m_useMetroCheck;
};

#endif // RRIMPORTDIALOG_H
