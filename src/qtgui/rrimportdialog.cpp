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
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QSet>
#include <QCryptographicHash>
#include <QSysInfo>
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
    connect(m_api, &RadioReference::fetchProgress, this, &DockRRImport::onFetchProgress);
    connect(m_api, &RadioReference::error, this, &DockRRImport::onError);

    setupUi();
    setWindowTitle("RadioReference");
    setObjectName("DockRRImport");

    // Float by default - user can dock if desired
    setFloating(true);
    resize(800, 500);

    // When floating, make it a regular window so it doesn't disappear when main window loses focus
    connect(this, &QDockWidget::topLevelChanged, this, [this](bool floating) {
        if (floating) {
            setWindowFlags(Qt::Window);
            show();  // Need to show() after changing window flags
        }
    });
}

DockRRImport::~DockRRImport()
{
}

void DockRRImport::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Tab widget for Credentials and Location
    m_tabWidget = new QTabWidget();
    m_tabWidget->setMaximumHeight(120);

    // --- Credentials tab ---
    QWidget *credTab = new QWidget();
    QVBoxLayout *credMainLayout = new QVBoxLayout(credTab);
    credMainLayout->setContentsMargins(8, 8, 8, 8);
    credMainLayout->setSpacing(6);

    QHBoxLayout *userRow = new QHBoxLayout();
    userRow->addWidget(new QLabel("Username:"));
    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText("RadioReference username");
    m_usernameEdit->setMinimumWidth(200);
    userRow->addWidget(m_usernameEdit);
    userRow->addStretch();
    credMainLayout->addLayout(userRow);

    QHBoxLayout *passRow = new QHBoxLayout();
    passRow->addWidget(new QLabel("Password:"));
    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Password");
    m_passwordEdit->setMinimumWidth(200);
    passRow->addWidget(m_passwordEdit);
    passRow->addStretch();
    credMainLayout->addLayout(passRow);

    credMainLayout->addStretch();

    // --- Location tab ---
    QWidget *locTab = new QWidget();
    QVBoxLayout *locMainLayout = new QVBoxLayout(locTab);
    locMainLayout->setContentsMargins(8, 8, 8, 8);
    locMainLayout->setSpacing(6);

    // Row 1: All location dropdowns
    QHBoxLayout *locRow = new QHBoxLayout();

    locRow->addWidget(new QLabel("State:"));
    m_stateCombo = new QComboBox();
    m_stateCombo->setMaxVisibleItems(20);
    m_stateCombo->addItem("Select a state...", 0);
    locRow->addWidget(m_stateCombo);

    locRow->addWidget(new QLabel("County:"));
    m_countyCombo = new QComboBox();
    m_countyCombo->setMaxVisibleItems(20);
    m_countyCombo->addItem("Select state first...", 0);
    m_countyCombo->setEnabled(false);
    locRow->addWidget(m_countyCombo);

    m_metroCombo = new QComboBox();
    m_metroCombo->setMaxVisibleItems(20);
    m_metroCombo->addItem("Select state first...", 0);
    m_metroCombo->setEnabled(false);
    m_metroCombo->setVisible(false);
    locRow->addWidget(m_metroCombo);

    m_useMetroCheck = new QCheckBox("Metro");
    m_useMetroCheck->setToolTip("Use Metro Area instead of County");
    locRow->addWidget(m_useMetroCheck);

    locRow->addStretch();
    locMainLayout->addLayout(locRow);

    // Row 2: Action buttons and category filter
    QHBoxLayout *buttonRow = new QHBoxLayout();
    m_downloadBtn = new QPushButton("Download");
    m_downloadBtn->setEnabled(false);
    m_selectAllBtn = new QPushButton("Select All");
    m_deselectAllBtn = new QPushButton("Deselect All");
    m_importBtn = new QPushButton("Import Selected");
    m_importBtn->setEnabled(false);
    buttonRow->addWidget(m_downloadBtn);
    buttonRow->addWidget(m_selectAllBtn);
    buttonRow->addWidget(m_deselectAllBtn);
    buttonRow->addWidget(m_importBtn);
    buttonRow->addSpacing(20);
    buttonRow->addWidget(new QLabel("Category:"));
    m_categoryFilter = new QComboBox();
    m_categoryFilter->setMaxVisibleItems(20);
    m_categoryFilter->setMinimumWidth(150);
    m_categoryFilter->addItem("All Categories", "");
    buttonRow->addWidget(m_categoryFilter);
    buttonRow->addStretch();
    locMainLayout->addLayout(buttonRow);

    locMainLayout->addStretch();

    // Location tab first
    m_tabWidget->addTab(locTab, "Location");
    m_tabWidget->addTab(credTab, "Credentials");

    mainLayout->addWidget(m_tabWidget);

    // Frequency table
    m_freqTable = new QTableWidget();
    m_freqTable->setColumnCount(12);
    m_freqTable->setHorizontalHeaderLabels({
        "",           // 0: checkbox
        "Frequency",  // 1
        "Input",      // 2: repeater input
        "Callsign",   // 3
        "Alpha",      // 4: alpha tag
        "Description",// 5
        "Mode",       // 6
        "Tone/CC",    // 7: tone or color code
        "TG/Slot",    // 8: talkgroup/slot
        "Type",       // 9: service class
        "Enc",        // 10: encrypted
        "Category"    // 11: tag/category
    });
    m_freqTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_freqTable->horizontalHeader()->setStretchLastSection(true);
    m_freqTable->setColumnWidth(0, 30);   // checkbox
    m_freqTable->setColumnWidth(1, 85);   // frequency
    m_freqTable->setColumnWidth(2, 85);   // input
    m_freqTable->setColumnWidth(3, 70);   // callsign
    m_freqTable->setColumnWidth(4, 90);   // alpha
    m_freqTable->setColumnWidth(5, 200);  // description
    m_freqTable->setColumnWidth(6, 50);   // mode
    m_freqTable->setColumnWidth(7, 70);   // tone/cc
    m_freqTable->setColumnWidth(8, 60);   // tg/slot
    m_freqTable->setColumnWidth(9, 70);   // type
    m_freqTable->setColumnWidth(10, 35);  // enc
    m_freqTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_freqTable->setAlternatingRowColors(true);
    m_freqTable->setSortingEnabled(true);
    mainLayout->addWidget(m_freqTable);

    // Status and progress
    QHBoxLayout *statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Select location and click Download");
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setRange(0, 0); // Indeterminate
    m_progressBar->setMaximumWidth(150);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_progressBar);
    mainLayout->addLayout(statusLayout);

    // Connections
    connect(m_stateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onStateSelected);
    connect(m_countyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onCountySelected);
    connect(m_metroCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onMetroSelected);
    connect(m_downloadBtn, &QPushButton::clicked, this, &DockRRImport::onDownloadClicked);
    connect(m_importBtn, &QPushButton::clicked, this, &DockRRImport::onImportClicked);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &DockRRImport::onSelectAllClicked);
    connect(m_deselectAllBtn, &QPushButton::clicked, this, &DockRRImport::onDeselectAllClicked);

    connect(m_useMetroCheck, &QCheckBox::toggled, [this](bool checked) {
        m_countyCombo->setVisible(!checked);
        m_metroCombo->setVisible(checked);
        updateDownloadButton();
    });

    // Update download button when credentials change
    connect(m_usernameEdit, &QLineEdit::textChanged, this, &DockRRImport::updateDownloadButton);
    connect(m_passwordEdit, &QLineEdit::textChanged, this, &DockRRImport::updateDownloadButton);

    // Category filter
    connect(m_categoryFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DockRRImport::onCategoryFilterChanged);
}

void DockRRImport::onStateSelected(int index)
{
    int stateId = m_stateCombo->itemData(index).toInt();
    if (stateId > 0) {
        m_userInitiated = true;  // User selected a state
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
        m_downloadBtn->setEnabled(false);
    }
}

void DockRRImport::onCountySelected(int index)
{
    Q_UNUSED(index);
    updateDownloadButton();
}

void DockRRImport::onMetroSelected(int index)
{
    Q_UNUSED(index);
    updateDownloadButton();
}

void DockRRImport::updateDownloadButton()
{
    bool hasCreds = !m_usernameEdit->text().isEmpty() && !m_passwordEdit->text().isEmpty();
    bool hasLocation = false;

    if (m_useMetroCheck->isChecked()) {
        hasLocation = m_metroCombo->currentData().toInt() > 0;
    } else {
        hasLocation = m_countyCombo->currentData().toInt() > 0;
    }

    m_downloadBtn->setEnabled(hasCreds && hasLocation);
}

void DockRRImport::onDownloadClicked()
{
    m_userInitiated = true;  // User clicked Download

    // Set credentials from UI
    m_username = m_usernameEdit->text();
    m_password = m_passwordEdit->text();
    m_api->setCredentials(m_username, m_password);

    if (!m_api->hasCredentials()) {
        m_statusLabel->setText("Please enter username and password in Credentials tab");
        return;
    }

    m_statusLabel->setText("Downloading frequencies...");
    m_progressBar->setVisible(true);
    m_freqTable->setRowCount(0);
    m_frequencies.clear();

    if (m_useMetroCheck->isChecked()) {
        int metroId = m_metroCombo->currentData().toInt();
        m_api->searchMetroFrequencies(metroId);
    } else {
        int countyId = m_countyCombo->currentData().toInt();
        m_api->searchCountyFrequencies(countyId);
    }
}

void DockRRImport::onImportClicked()
{
    // Count selected frequencies
    int selectedCount = 0;
    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        QTableWidgetItem *checkItem = m_freqTable->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            selectedCount++;
        }
    }

    if (selectedCount == 0) {
        m_statusLabel->setText("No frequencies selected");
        return;
    }

    // Get existing tags for the combo box
    QStringList tagNames;
    tagNames << "(No tag)" << "(Use RR category)";
    QList<TagInfo::sptr> existingTags = Bookmarks::Get().getTagList();
    for (const TagInfo::sptr &tag : existingTags) {
        if (!tag->name.isEmpty()) {
            tagNames << tag->name;
        }
    }

    // Ask user which tag to use
    bool ok;
    QString selectedTag = QInputDialog::getItem(this,
        "Import Tag",
        QString("Select tag for %1 frequencies:").arg(selectedCount),
        tagNames, 1, true, &ok);

    if (!ok) {
        return; // User cancelled
    }

    // Determine tag to use
    TagInfo::sptr importTag;
    bool useRRCategory = (selectedTag == "(Use RR category)");
    bool noTag = (selectedTag == "(No tag)");

    if (!useRRCategory && !noTag) {
        // User selected or entered a specific tag
        importTag = Bookmarks::Get().findOrAddTag(selectedTag);
    }

    int imported = 0;
    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        QTableWidgetItem *checkItem = m_freqTable->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            int originalIndex = checkItem->data(Qt::UserRole).toInt();
            const RRFrequency &freq = m_frequencies[originalIndex];

            BookmarkInfo info;
            info.frequency = (qint64)freq.frequency;
            info.name = freq.description.isEmpty() ? freq.alpha_tag : freq.description;
            info.modulation = modeToGqrx(freq.mode);
            info.bandwidth = 12500; // Default NFM bandwidth

            // Add tag
            if (useRRCategory && !freq.tag.isEmpty()) {
                TagInfo::sptr tag = Bookmarks::Get().findOrAddTag(freq.tag);
                info.tags.append(tag);
            } else if (importTag) {
                info.tags.append(importTag);
            }

            Bookmarks::Get().add(info);
            imported++;
        }
    }

    if (imported > 0) {
        Bookmarks::Get().save();
        m_statusLabel->setText(QString("Imported %1 frequencies").arg(imported));
        emit frequenciesImported(imported);
    }
}

void DockRRImport::onSelectAllClicked()
{
    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        if (m_freqTable->isRowHidden(row))
            continue;
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

    // Populate category filter with unique categories
    m_categoryFilter->blockSignals(true);
    m_categoryFilter->clear();
    m_categoryFilter->addItem("All Categories", "");

    QSet<QString> categories;
    for (const RRFrequency &freq : frequencies) {
        if (!freq.tag.isEmpty()) {
            categories.insert(freq.tag);
        }
    }

    QStringList sortedCategories = categories.values();
    sortedCategories.sort();
    for (const QString &cat : sortedCategories) {
        m_categoryFilter->addItem(cat, cat);
    }
    m_categoryFilter->blockSignals(false);
}

void DockRRImport::onFetchProgress(int current, int total, int freqsSoFar, const QString &categoryName)
{
    m_statusLabel->setText(QString("Fetching %1 (%2/%3, %4 freqs)")
                           .arg(categoryName).arg(current).arg(total).arg(freqsSoFar));
}

void DockRRImport::onError(const QString &message)
{
    m_progressBar->setVisible(false);
    m_statusLabel->setText(message);

    // Only show modal popup for user-initiated actions, not auto-connect on startup
    if (m_userInitiated) {
        QMessageBox::warning(this, "RadioReference Error", message);
    }
    m_userInitiated = false;
}

void DockRRImport::onCategoryFilterChanged(int index)
{
    Q_UNUSED(index);
    filterTable();
}

void DockRRImport::filterTable()
{
    QString selectedCategory = m_categoryFilter->currentData().toString();
    int visibleCount = 0;

    for (int row = 0; row < m_freqTable->rowCount(); row++) {
        bool show = true;
        if (!selectedCategory.isEmpty()) {
            QTableWidgetItem *catItem = m_freqTable->item(row, 11); // Category column
            show = catItem && catItem->text() == selectedCategory;
        }
        m_freqTable->setRowHidden(row, !show);
        if (show) visibleCount++;
    }

    // Update Select All button text
    if (selectedCategory.isEmpty()) {
        m_selectAllBtn->setText("Select All");
    } else {
        m_selectAllBtn->setText(QString("Select All Shown (%1)").arg(visibleCount));
    }

    m_statusLabel->setText(QString("Showing %1 of %2 frequencies")
                           .arg(visibleCount).arg(m_frequencies.size()));
}

void DockRRImport::populateFrequencyTable(const QList<RRFrequency> &frequencies)
{
    m_freqTable->setSortingEnabled(false); // Disable during population
    m_freqTable->setRowCount(frequencies.size());

    for (int i = 0; i < frequencies.size(); i++) {
        const RRFrequency &freq = frequencies[i];

        // Helper to create read-only item
        auto makeItem = [](const QString &text) {
            QTableWidgetItem *item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };

        // 0: Checkbox - store original index in UserRole for retrieval after sorting
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(Qt::UserRole, i);  // Store original index
        m_freqTable->setItem(i, 0, checkItem);

        // 1: Frequency (MHz) - 6 decimal places for 1 Hz precision
        QTableWidgetItem *freqItem = makeItem(QString::number(freq.frequency / 1e6, 'f', 6));
        freqItem->setData(Qt::UserRole, freq.frequency); // Store Hz for sorting
        freqItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_freqTable->setItem(i, 1, freqItem);

        // 2: Input frequency (MHz) - for repeaters
        if (freq.input_freq > 0) {
            QTableWidgetItem *inputItem = makeItem(QString::number(freq.input_freq / 1e6, 'f', 6));
            inputItem->setData(Qt::UserRole, freq.input_freq);
            inputItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_freqTable->setItem(i, 2, inputItem);
        } else {
            m_freqTable->setItem(i, 2, makeItem(""));
        }

        // 3: Callsign
        m_freqTable->setItem(i, 3, makeItem(freq.callsign));

        // 4: Alpha tag
        m_freqTable->setItem(i, 4, makeItem(freq.alpha_tag));

        // 5: Description
        m_freqTable->setItem(i, 5, makeItem(freq.description));

        // 6: Mode
        m_freqTable->setItem(i, 6, makeItem(freq.mode));

        // 7: Tone/CC - show tone for analog, color code for DMR
        QString toneCC = freq.tone;
        if (!freq.color_code.isEmpty()) {
            toneCC = "CC:" + freq.color_code;
        }
        m_freqTable->setItem(i, 7, makeItem(toneCC));

        // 8: TG/Slot - talkgroup and slot for digital
        QString tgSlot;
        if (!freq.talkgroup.isEmpty()) {
            tgSlot = freq.talkgroup;
            if (!freq.slot.isEmpty()) {
                tgSlot += "/" + freq.slot;
            }
        }
        m_freqTable->setItem(i, 8, makeItem(tgSlot));

        // 9: Service type
        m_freqTable->setItem(i, 9, makeItem(freq.service_class));

        // 10: Encrypted
        m_freqTable->setItem(i, 10, makeItem(freq.encrypted ? "Y" : ""));

        // 11: Category/tag
        m_freqTable->setItem(i, 11, makeItem(freq.tag));
    }

    m_freqTable->setSortingEnabled(true);
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
    else if (mode == "P25" || mode == "DMR" || mode == "NXDN" || mode == "DSTAR" || mode == "D-STAR" || mode == "FUSION")
        return "Narrow FM"; // Digital modes use FM
    else
        return "Narrow FM"; // Default
}

void DockRRImport::saveSettings(QSettings *settings)
{
    if (!settings)
        return;

    settings->beginGroup("radioreference");

    // Store credentials encrypted (key derived from machine ID)
    settings->setValue("username_enc", encryptCredential(m_usernameEdit->text()));
    settings->setValue("password_enc", encryptCredential(m_passwordEdit->text()));

    // Remove old plaintext keys if they exist
    settings->remove("username");
    settings->remove("password");

    settings->endGroup();
}

void DockRRImport::readSettings(QSettings *settings)
{
    if (!settings)
        return;

    settings->beginGroup("radioreference");

    // Try encrypted credentials first
    QString encUser = settings->value("username_enc", "").toString();
    QString encPass = settings->value("password_enc", "").toString();

    if (!encUser.isEmpty() || !encPass.isEmpty()) {
        // Decrypt stored credentials
        m_username = decryptCredential(encUser);
        m_password = decryptCredential(encPass);
    } else {
        // Fallback: migrate from old plaintext storage
        m_username = settings->value("username", "").toString();
        m_password = settings->value("password", "").toString();
    }

    m_usernameEdit->setText(m_username);
    m_passwordEdit->setText(m_password);

    settings->endGroup();

    // Auto-connect if credentials are available
    if (!m_username.isEmpty() && !m_password.isEmpty()) {
        m_api->setCredentials(m_username, m_password);
        if (m_api->hasCredentials()) {
            m_statusLabel->setText("Loading states...");
            m_progressBar->setVisible(true);
            m_api->getStateList();
        }
    }
}

QString DockRRImport::encryptCredential(const QString &plaintext)
{
    if (plaintext.isEmpty())
        return QString();

    // Derive key from machine-specific data (no key storage needed)
    // The "salt" is disguised as a version string in case anyone looks at the code
    QByteArray machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) {
        // Fallback for systems without machine ID
        machineId = QSysInfo::prettyProductName().toUtf8() +
                    QSysInfo::currentCpuArchitecture().toUtf8();
    }

    // Salt looks like an innocuous version/build identifier
    const QByteArray salt = "gqrx-rr-v2.17.5-build-20251227";

    // Generate key using SHA-256
    QByteArray keyMaterial = machineId + salt;
    QByteArray key = QCryptographicHash::hash(keyMaterial, QCryptographicHash::Sha256);

    // XOR encrypt
    QByteArray plainBytes = plaintext.toUtf8();
    QByteArray cipher;
    cipher.resize(plainBytes.size());

    for (int i = 0; i < plainBytes.size(); ++i) {
        cipher[i] = plainBytes[i] ^ key[i % key.size()];
    }

    // Return as base64
    return QString::fromLatin1(cipher.toBase64());
}

QString DockRRImport::decryptCredential(const QString &ciphertext)
{
    if (ciphertext.isEmpty())
        return QString();

    // Derive same key from machine-specific data
    QByteArray machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) {
        machineId = QSysInfo::prettyProductName().toUtf8() +
                    QSysInfo::currentCpuArchitecture().toUtf8();
    }

    const QByteArray salt = "gqrx-rr-v2.17.5-build-20251227";

    QByteArray keyMaterial = machineId + salt;
    QByteArray key = QCryptographicHash::hash(keyMaterial, QCryptographicHash::Sha256);

    // Decode base64 and XOR decrypt (XOR is symmetric)
    QByteArray cipher = QByteArray::fromBase64(ciphertext.toLatin1());
    QByteArray plain;
    plain.resize(cipher.size());

    for (int i = 0; i < cipher.size(); ++i) {
        plain[i] = cipher[i] ^ key[i % key.size()];
    }

    return QString::fromUtf8(plain);
}
