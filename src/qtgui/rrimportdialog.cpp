/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * RadioReference.com import dock widget
 */
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include "rrimportdialog.h"
#include "bookmarks.h"

DockRRImport::DockRRImport(QWidget *parent)
    : QDockWidget(parent)
{
    m_api = new RadioReference(this);

    connect(m_api, &RadioReference::stateListReceived, this, &DockRRImport::onStateListReceived);
    connect(m_api, &RadioReference::countyListReceived, this, &DockRRImport::onCountyListReceived);
    connect(m_api, &RadioReference::metroListReceived, this, &DockRRImport::onMetroListReceived);
    connect(m_api, &RadioReference::frequenciesReceived, this, &DockRRImport::onFrequenciesReceived);
    connect(m_api, &RadioReference::error, this, &DockRRImport::onError);

    setupUi();
    setWindowTitle("RadioReference");
    setObjectName("DockRRImport");
}

DockRRImport::~DockRRImport()
{
}

void DockRRImport::setCredentials(const QString &username, const QString &password, const QString &appKey)
{
    m_api->setCredentials(username, password, appKey);

    if (m_api->hasCredentials()) {
        m_statusLabel->setText("Loading states...");
        m_progressBar->setVisible(true);
        m_api->getStateList();
    } else {
        m_statusLabel->setText("RadioReference credentials not configured. Set them in Preferences.");
    }
}

void DockRRImport::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Credentials group (collapsible)
    QGroupBox *credentialsGroup = new QGroupBox("RadioReference Credentials");
    QGridLayout *credLayout = new QGridLayout(credentialsGroup);

    credLayout->addWidget(new QLabel("Username:"), 0, 0);
    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText("RadioReference username");
    credLayout->addWidget(m_usernameEdit, 0, 1);

    credLayout->addWidget(new QLabel("Password:"), 1, 0);
    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("RadioReference password");
    credLayout->addWidget(m_passwordEdit, 1, 1);

    credLayout->addWidget(new QLabel("App Key:"), 2, 0);
    m_appKeyEdit = new QLineEdit();
    m_appKeyEdit->setPlaceholderText("API application key");
    credLayout->addWidget(m_appKeyEdit, 2, 1);

    m_saveCredentialsBtn = new QPushButton("Save && Connect");
    credLayout->addWidget(m_saveCredentialsBtn, 2, 2);

    mainLayout->addWidget(credentialsGroup);

    // Location selection group
    QGroupBox *locationGroup = new QGroupBox("Location");
    QGridLayout *locationLayout = new QGridLayout(locationGroup);

    locationLayout->addWidget(new QLabel("State:"), 0, 0);
    m_stateCombo = new QComboBox();
    m_stateCombo->addItem("Select a state...", 0);
    locationLayout->addWidget(m_stateCombo, 0, 1);

    m_useMetroCheck = new QCheckBox("Use Metro Area instead of County");
    locationLayout->addWidget(m_useMetroCheck, 0, 2);

    locationLayout->addWidget(new QLabel("County:"), 1, 0);
    m_countyCombo = new QComboBox();
    m_countyCombo->addItem("Select state first...", 0);
    m_countyCombo->setEnabled(false);
    locationLayout->addWidget(m_countyCombo, 1, 1, 1, 2);

    locationLayout->addWidget(new QLabel("Metro:"), 2, 0);
    m_metroCombo = new QComboBox();
    m_metroCombo->addItem("Select state first...", 0);
    m_metroCombo->setEnabled(false);
    m_metroCombo->setVisible(false);
    locationLayout->addWidget(m_metroCombo, 2, 1, 1, 2);

    mainLayout->addWidget(locationGroup);

    // Search controls
    QHBoxLayout *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Filter (MHz):"));
    m_freqFilter = new QLineEdit();
    m_freqFilter->setPlaceholderText("e.g., 145.0 or leave empty for all");
    m_freqFilter->setMaximumWidth(200);
    searchLayout->addWidget(m_freqFilter);
    m_searchBtn = new QPushButton("Search");
    m_searchBtn->setEnabled(false);
    searchLayout->addWidget(m_searchBtn);
    searchLayout->addStretch();
    mainLayout->addLayout(searchLayout);

    // Frequency table
    m_freqTable = new QTableWidget();
    m_freqTable->setColumnCount(7);
    m_freqTable->setHorizontalHeaderLabels({"", "Frequency", "Description", "Mode", "Tone", "Tag", "Agency"});
    m_freqTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_freqTable->setColumnWidth(0, 30);
    m_freqTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_freqTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_freqTable->setAlternatingRowColors(true);
    mainLayout->addWidget(m_freqTable);

    // Selection buttons
    QHBoxLayout *selectionLayout = new QHBoxLayout();
    m_selectAllBtn = new QPushButton("Select All");
    m_deselectAllBtn = new QPushButton("Deselect All");
    selectionLayout->addWidget(m_selectAllBtn);
    selectionLayout->addWidget(m_deselectAllBtn);
    selectionLayout->addStretch();
    mainLayout->addLayout(selectionLayout);

    // Status and progress
    QHBoxLayout *statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Configure credentials in Preferences");
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setRange(0, 0); // Indeterminate
    m_progressBar->setMaximumWidth(150);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_progressBar);
    mainLayout->addLayout(statusLayout);

    // Dialog buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    m_importBtn = new QPushButton("Import Selected");
    m_importBtn->setEnabled(false);
    buttonLayout->addWidget(m_importBtn);
    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_stateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onStateSelected);
    connect(m_countyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onCountySelected);
    connect(m_metroCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onMetroSelected);
    connect(m_searchBtn, &QPushButton::clicked, this, &DockRRImport::onSearchClicked);
    connect(m_importBtn, &QPushButton::clicked, this, &DockRRImport::onImportClicked);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &DockRRImport::onSelectAllClicked);
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &DockRRImport::onDeselectAllClicked);
    connect(m_saveCredentialsBtn, &QPushButton::clicked, this, &DockRRImport::onSaveCredentialsClicked);

    connect(m_useMetroCheck, &QCheckBox::toggled, [this](bool checked) {
        m_countyCombo->setVisible(!checked);
        m_metroCombo->setVisible(checked);
        // Re-enable search if appropriate combo has selection
        if (checked) {
            m_searchBtn->setEnabled(m_metroCombo->currentData().toInt() > 0);
        } else {
            m_searchBtn->setEnabled(m_countyCombo->currentData().toInt() > 0);
        }
    });
}

void DockRRImport::onStateSelected(int index)
{
    int stateId = m_stateCombo->itemData(index).toInt();
    if (stateId > 0) {
        m_statusLabel->setText("Loading counties...");
        m_progressBar->setVisible(true);
        m_countyCombo->clear();
        m_countyCombo->addItem("Loading...", 0);
        m_metroCombo->clear();
        m_metroCombo->addItem("Loading...", 0);
        m_api->getCountyList(stateId);
        m_api->getMetroList(stateId);
    } else {
        m_countyCombo->clear();
        m_countyCombo->addItem("Select state first...", 0);
        m_countyCombo->setEnabled(false);
        m_metroCombo->clear();
        m_metroCombo->addItem("Select state first...", 0);
        m_metroCombo->setEnabled(false);
        m_searchBtn->setEnabled(false);
    }
}

void DockRRImport::onCountySelected(int index)
{
    int countyId = m_countyCombo->itemData(index).toInt();
    m_searchBtn->setEnabled(countyId > 0 && !m_useMetroCheck->isChecked());
}

void DockRRImport::onMetroSelected(int index)
{
    int metroId = m_metroCombo->itemData(index).toInt();
    m_searchBtn->setEnabled(metroId > 0 && m_useMetroCheck->isChecked());
}

void DockRRImport::onSearchClicked()
{
    m_statusLabel->setText("Searching frequencies...");
    m_progressBar->setVisible(true);
    m_freqTable->setRowCount(0);
    m_frequencies.clear();

    double freqFilter = 0.0;
    if (!m_freqFilter->text().isEmpty()) {
        freqFilter = m_freqFilter->text().toDouble();
    }

    if (m_useMetroCheck->isChecked()) {
        int metroId = m_metroCombo->currentData().toInt();
        m_api->searchMetroFrequencies(metroId, freqFilter);
    } else {
        int countyId = m_countyCombo->currentData().toInt();
        m_api->searchCountyFrequencies(countyId, freqFilter);
    }
}

void DockRRImport::onImportClicked()
{
    int imported = 0;

    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        QTableWidgetItem *checkItem = m_freqTable->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            const RRFrequency &freq = m_frequencies[row];

            BookmarkInfo info;
            info.frequency = (qint64)freq.frequency;
            info.name = freq.description.isEmpty() ? freq.alpha_tag : freq.description;
            info.modulation = modeToGqrx(freq.mode);
            info.bandwidth = 12500; // Default NFM bandwidth

            // Add tag based on RR category
            if (!freq.tag.isEmpty()) {
                TagInfo::sptr tag = Bookmarks::Get().findOrAddTag(freq.tag);
                info.tags.append(tag);
            }

            Bookmarks::Get().add(info);
            imported++;
        }
    }

    if (imported > 0) {
        Bookmarks::Get().save();
        m_statusLabel->setText(QString("Imported %1 frequencies").arg(imported));
        emit frequenciesImported(imported);
    } else {
        m_statusLabel->setText("No frequencies selected");
    }
}

void DockRRImport::onSelectAllClicked()
{
    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        QTableWidgetItem *item = m_freqTable->item(row, 0);
        if (item) {
            item->setCheckState(Qt::Checked);
        }
    }
}

void DockRRImport::onDeselectAllClicked()
{
    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        QTableWidgetItem *item = m_freqTable->item(row, 0);
        if (item) {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

void DockRRImport::onStateListReceived(const QList<RRState> &states)
{
    m_progressBar->setVisible(false);
    m_stateCombo->clear();
    m_stateCombo->addItem("Select a state...", 0);

    for (const RRState &state : states) {
        m_stateCombo->addItem(QString("%1 (%2)").arg(state.name, state.abbrev), state.id);
    }

    m_statusLabel->setText(QString("Loaded %1 states").arg(states.size()));
}

void DockRRImport::onCountyListReceived(const QList<RRCounty> &counties)
{
    m_progressBar->setVisible(false);
    m_countyCombo->clear();
    m_countyCombo->addItem("Select a county...", 0);

    for (const RRCounty &county : counties) {
        m_countyCombo->addItem(county.name, county.id);
    }

    m_countyCombo->setEnabled(true);
    m_statusLabel->setText(QString("Loaded %1 counties").arg(counties.size()));
}

void DockRRImport::onMetroListReceived(const QList<RRMetro> &metros)
{
    m_metroCombo->clear();
    m_metroCombo->addItem("Select a metro area...", 0);

    for (const RRMetro &metro : metros) {
        m_metroCombo->addItem(metro.name, metro.id);
    }

    m_metroCombo->setEnabled(true);
}

void DockRRImport::onFrequenciesReceived(const QList<RRFrequency> &frequencies)
{
    m_progressBar->setVisible(false);
    m_frequencies = frequencies;
    populateFrequencyTable(frequencies);
    m_statusLabel->setText(QString("Found %1 frequencies").arg(frequencies.size()));
    m_importBtn->setEnabled(frequencies.size() > 0);
}

void DockRRImport::onError(const QString &message)
{
    m_progressBar->setVisible(false);
    m_statusLabel->setText(message);
    QMessageBox::warning(this, "RadioReference Error", message);
}

void DockRRImport::populateFrequencyTable(const QList<RRFrequency> &frequencies)
{
    m_freqTable->setRowCount(frequencies.size());

    for (int i = 0; i < frequencies.size(); i++) {
        const RRFrequency &freq = frequencies[i];

        // Checkbox
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        m_freqTable->setItem(i, 0, checkItem);

        // Frequency (MHz)
        QTableWidgetItem *freqItem = new QTableWidgetItem(
            QString::number(freq.frequency / 1e6, 'f', 4));
        freqItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_freqTable->setItem(i, 1, freqItem);

        // Description
        m_freqTable->setItem(i, 2, new QTableWidgetItem(freq.description));

        // Mode
        m_freqTable->setItem(i, 3, new QTableWidgetItem(freq.mode));

        // Tone
        m_freqTable->setItem(i, 4, new QTableWidgetItem(freq.tone));

        // Tag (category)
        m_freqTable->setItem(i, 5, new QTableWidgetItem(freq.tag));

        // Agency
        m_freqTable->setItem(i, 6, new QTableWidgetItem(freq.agency));
    }
}

QString DockRRImport::modeToGqrx(const QString &rrMode)
{
    // Convert RadioReference mode names to gqrx modulation strings
    QString mode = rrMode.toUpper();

    if (mode == "FM" || mode == "NFM" || mode == "FMN")
        return "Narrow FM";
    else if (mode == "AM")
        return "AM";
    else if (mode == "USB")
        return "USB";
    else if (mode == "LSB")
        return "LSB";
    else if (mode == "FMW" || mode == "WFM")
        return "WFM (mono)";
    else if (mode == "P25" || mode == "DMR" || mode == "NXDN" || mode == "DSTAR")
        return "Narrow FM"; // Digital modes use FM
    else
        return "Narrow FM"; // Default
}

void DockRRImport::onSaveCredentialsClicked()
{
    m_username = m_usernameEdit->text();
    m_password = m_passwordEdit->text();
    m_appKey = m_appKeyEdit->text();

    m_api->setCredentials(m_username, m_password, m_appKey);

    if (m_api->hasCredentials()) {
        m_statusLabel->setText("Loading states...");
        m_progressBar->setVisible(true);
        m_api->getStateList();
    } else {
        m_statusLabel->setText("Please fill in all credential fields");
    }
}

void DockRRImport::saveSettings(QSettings *settings)
{
    if (!settings)
        return;

    settings->beginGroup("radioreference");

    settings->setValue("username", m_usernameEdit->text());
    // Note: Password is stored - users should be aware of this
    settings->setValue("password", m_passwordEdit->text());
    settings->setValue("appKey", m_appKeyEdit->text());

    settings->endGroup();
}

void DockRRImport::readSettings(QSettings *settings)
{
    if (!settings)
        return;

    settings->beginGroup("radioreference");

    m_username = settings->value("username", "").toString();
    m_password = settings->value("password", "").toString();
    m_appKey = settings->value("appKey", "").toString();

    m_usernameEdit->setText(m_username);
    m_passwordEdit->setText(m_password);
    m_appKeyEdit->setText(m_appKey);

    settings->endGroup();

    // Auto-connect if credentials are available
    if (!m_username.isEmpty() && !m_password.isEmpty() && !m_appKey.isEmpty()) {
        m_api->setCredentials(m_username, m_password, m_appKey);
        if (m_api->hasCredentials()) {
            m_statusLabel->setText("Loading states...");
            m_progressBar->setVisible(true);
            m_api->getStateList();
        }
    }
}

void DockRRImport::setFrequencyFilter(qint64 freqHz)
{
    double freqMHz = freqHz / 1e6;
    m_freqFilter->setText(QString::number(freqMHz, 'f', 4));
}
