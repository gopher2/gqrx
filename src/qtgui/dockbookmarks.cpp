/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2013 Christian Lindner DL2VCL, Stefano Leucci.
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
#include <cmath>
#include <cstdlib>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include "bookmarks.h"
#include "bookmarkstaglist.h"
#include "dockbookmarks.h"
#include "dockrxopt.h"
#include "qtcolorpicker.h"
#include "ui_dockbookmarks.h"

DockBookmarks::DockBookmarks(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::DockBookmarks)
{
    ui->setupUi(this);

    bookmarksTableModel = new BookmarksTableModel();
    m_networkManager = new QNetworkAccessManager(this);
    m_positionSource = nullptr;
    m_latitude = 0;
    m_longitude = 0;
    m_radiusMiles = 50;
    m_hasLocation = false;

    // Frequency List
    ui->tableViewFrequencyList->setModel(bookmarksTableModel);
    ui->tableViewFrequencyList->setColumnWidth(BookmarksTableModel::COL_NAME,
    ui->tableViewFrequencyList->columnWidth(BookmarksTableModel::COL_NAME) * 2);
    ui->tableViewFrequencyList->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewFrequencyList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableViewFrequencyList->installEventFilter(this);

    // Demod Selection in Frequency List Table.
    ComboBoxDelegateModulation* delegateModulation = new ComboBoxDelegateModulation(this);
    ui->tableViewFrequencyList->setItemDelegateForColumn(2, delegateModulation);

    // Bookmarks Context menu
    contextmenu = new QMenu(this);
    // MenuItem Delete
    {
        QAction* action = new QAction("Delete Bookmark", this);
        contextmenu->addAction(action);
        connect(action, SIGNAL(triggered()), this, SLOT(DeleteSelectedBookmark()));
    }
    // MenuItem Add
    {
        actionAddBookmark = new QAction("Add Bookmark", this);
        contextmenu->addAction(actionAddBookmark);
    }
    // MenuItem Add Tuner
    {
        QAction* action = new QAction("Add Tuner", this);
        contextmenu->addAction(action);
        connect(action, SIGNAL(triggered()), this, SLOT(AddTunerFromSelectedBookmark()));
    }
    ui->tableViewFrequencyList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableViewFrequencyList, SIGNAL(customContextMenuRequested(const QPoint&)),
        this, SLOT(ShowContextMenu(const QPoint&)));

    // Update GUI
    Bookmarks::Get().load();
    bookmarksTableModel->update();

    m_currentFrequency = 0;
    m_updating = false;

    // TagList
    updateTags();

    // Tag list buttons
    connect(ui->btnTagsAll, &QPushButton::clicked,
            ui->tableWidgetTagList, &BookmarksTagList::SelectAll);
    connect(ui->btnTagsNone, &QPushButton::clicked,
            ui->tableWidgetTagList, &BookmarksTagList::DeselectAll);

    connect(ui->tableViewFrequencyList, SIGNAL(activated(const QModelIndex &)),
            this, SLOT(activated(const QModelIndex &)));
    connect(ui->tableViewFrequencyList, SIGNAL(doubleClicked(const QModelIndex &)),
            this, SLOT(doubleClicked(const QModelIndex &)));
    connect(bookmarksTableModel, SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &)),
            this, SLOT(onDataChanged(const QModelIndex &, const QModelIndex &)));
    connect(&Bookmarks::Get(), SIGNAL(TagListChanged()),
            ui->tableWidgetTagList, SLOT(updateTags()));
    connect(&Bookmarks::Get(), SIGNAL(BookmarksChanged()),
            bookmarksTableModel, SLOT(update()));
}

DockBookmarks::~DockBookmarks()
{
    delete ui;
    delete bookmarksTableModel;
}

void DockBookmarks::activated(const QModelIndex & index)
{
    BookmarkInfo *info = bookmarksTableModel->getBookmarkAtRow(index.row());
    emit newBookmarkActivated(info->frequency, info->modulation, info->bandwidth);
}

void DockBookmarks::setNewFrequency(qint64 rx_freq)
{
    ui->tableViewFrequencyList->clearSelection();
    const int iRowCount = bookmarksTableModel->rowCount();
    for (int row = 0; row < iRowCount; ++row)
    {
        BookmarkInfo& info = *(bookmarksTableModel->getBookmarkAtRow(row));
        if (std::abs(rx_freq - info.frequency) <= ((info.bandwidth / 2 ) + 1))
        {
            ui->tableViewFrequencyList->selectRow(row);
            ui->tableViewFrequencyList->scrollTo(ui->tableViewFrequencyList->currentIndex(), QAbstractItemView::EnsureVisible );
            break;
        }
    }
    m_currentFrequency = rx_freq;
}

void DockBookmarks::updateTags()
{
    m_updating = true;
    ui->tableWidgetTagList->updateTags();
    m_updating = false;
}

void DockBookmarks::updateBookmarks()
{
    bookmarksTableModel->update();
}

//Data has been edited
void DockBookmarks::onDataChanged(const QModelIndex&, const QModelIndex &)
{
    updateTags();
    Bookmarks::Get().save();
}

void DockBookmarks::on_tableWidgetTagList_itemChanged(QTableWidgetItem *item)
{
    // we only want to react on changed by the user, not changes by the program itself.
    if(ui->tableWidgetTagList->m_bUpdating) return;

    int col = item->column();
    if (col != 1)
        return;

    QString strText = item->text();
    Bookmarks::Get().setTagChecked(strText, (item->checkState() == Qt::Checked));
}

bool DockBookmarks::eventFilter(QObject* object, QEvent* event)
{
    // Since Key_Delete can be (is) used as a global shortcut, override the
    // shortcut. Accepting a ShortcutOverride causes the event to be delivered
    // again, but as a KeyPress.
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
    {
        QKeyEvent* pKeyEvent = static_cast<QKeyEvent *>(event);
        if (pKeyEvent->key() == Qt::Key_Delete)
        {
            if (event->type() == QEvent::ShortcutOverride) {
                event->accept();
            }
            else {
                return DeleteSelectedBookmark();
            }
        }
    }
    return QWidget::eventFilter(object, event);
}

bool DockBookmarks::DeleteSelectedBookmark()
{
    QModelIndexList selected = ui->tableViewFrequencyList->selectionModel()->selectedRows();

    if (selected.empty())
    {
        return true;
    }

    if (QMessageBox::question(this, "Delete bookmark", "Really delete?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
    {
        int iIndex = bookmarksTableModel->GetBookmarksIndexForRow(selected.first().row());
        Bookmarks::Get().remove(iIndex);
        bookmarksTableModel->update();
    }
    return true;
}

void DockBookmarks::AddTunerFromSelectedBookmark()
{
    QModelIndexList selected = ui->tableViewFrequencyList->selectionModel()->selectedRows();

    if (selected.empty())
    {
        return;
    }

    int iIndex = bookmarksTableModel->GetBookmarksIndexForRow(selected.first().row());
    BookmarkInfo bookmark = Bookmarks::Get().getBookmark(iIndex);

    emit addTunerRequested(bookmark.frequency, bookmark.modulation, bookmark.name);
}

void DockBookmarks::ShowContextMenu(const QPoint& pos)
{
    contextmenu->popup(ui->tableViewFrequencyList->viewport()->mapToGlobal(pos));
}

void DockBookmarks::doubleClicked(const QModelIndex & index)
{
    if(index.column() == BookmarksTableModel::COL_TAGS)
    {
        changeBookmarkTags(index.row(), index.column());
    }
}

ComboBoxDelegateModulation::ComboBoxDelegateModulation(QObject *parent)
:QItemDelegate(parent)
{
}

QWidget *ComboBoxDelegateModulation::createEditor(QWidget *parent, const QStyleOptionViewItem &/* option */, const QModelIndex &index) const
{
    QComboBox* comboBox = new QComboBox(parent);
    for (int i = 0; i < DockRxOpt::ModulationStrings.size(); ++i)
    {
        comboBox->addItem(DockRxOpt::ModulationStrings[i]);
    }
    setEditorData(comboBox, index);
    return comboBox;
}

void ComboBoxDelegateModulation::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    QComboBox *comboBox = static_cast<QComboBox*>(editor);
    QString value = index.model()->data(index, Qt::EditRole).toString();
    int iModulation = DockRxOpt::GetEnumForModulationString(value);
    comboBox->setCurrentIndex(iModulation);
}

void ComboBoxDelegateModulation::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    QComboBox *comboBox = static_cast<QComboBox*>(editor);
    model->setData(index, comboBox->currentText(), Qt::EditRole);
}

void DockBookmarks::changeBookmarkTags(int row, int /*column*/)
{
    bool ok = false;
    QStringList tags;

    int iIdx = bookmarksTableModel->GetBookmarksIndexForRow(row);
    BookmarkInfo& bmi = Bookmarks::Get().getBookmark(iIdx);

    // Create and show the Dialog for a new Bookmark.
    // Write the result into variable 'tags'.
    {
        QDialog dialog(this);
        dialog.setWindowTitle("Change Bookmark Tags");

        BookmarksTagList* taglist = new BookmarksTagList(&dialog, false);
        taglist->updateTags();
        taglist->setSelectedTags(bmi.tags);
        taglist->DeleteTag(TagInfo::strUntagged);

        QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                              | QDialogButtonBox::Cancel);
        connect(buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
        connect(buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

        QVBoxLayout *mainLayout = new QVBoxLayout(&dialog);
        mainLayout->addWidget(taglist);
        mainLayout->addWidget(buttonBox);

        ok = dialog.exec();
        if (ok)
        {
            tags = taglist->getSelectedTags();

            // Change Tags of Bookmark
            bmi.tags.clear();
            if (tags.size() == 0)
            {
                bmi.tags.append(Bookmarks::Get().findOrAddTag("")); // "Untagged"
            }
            for (int i = 0; i < tags.size(); ++i)
            {
                bmi.tags.append(Bookmarks::Get().findOrAddTag(tags[i]));
            }
            Bookmarks::Get().save();
        }
    }
}

void DockBookmarks::on_btnUpdateAmsat_clicked()
{
    ui->btnUpdateAmsat->setEnabled(false);
    ui->btnUpdateAmsat->setText("...");

    QUrl url("https://raw.githubusercontent.com/palewire/amateur-satellite-database/main/data/amsat-active-frequencies.json");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "gqrx");
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onAmsatDataReceived(reply);
    });
}

void DockBookmarks::onAmsatDataReceived(QNetworkReply* reply)
{
    ui->btnUpdateAmsat->setEnabled(true);
    ui->btnUpdateAmsat->setText("AMSAT");

    if (reply->error() != QNetworkReply::NoError)
    {
        QMessageBox::warning(this, "AMSAT Update Failed",
            QString("Failed to fetch satellite data:\n%1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        QMessageBox::warning(this, "AMSAT Update Failed",
            QString("Failed to parse satellite data:\n%1").arg(parseError.errorString()));
        return;
    }

    if (!doc.isArray())
    {
        QMessageBox::warning(this, "AMSAT Update Failed", "Unexpected data format");
        return;
    }

    // Remove existing AMSAT bookmarks (clear & refresh)
    const QString amsatTagName = "AMSAT Satellites";
    for (int i = Bookmarks::Get().size() - 1; i >= 0; i--)
    {
        BookmarkInfo& bm = Bookmarks::Get().getBookmark(i);
        for (const auto& tag : bm.tags)
        {
            if (tag->name == amsatTagName)
            {
                Bookmarks::Get().remove(i);
                break;
            }
        }
    }

    // Create or get the AMSAT tag
    TagInfo::sptr amsatTag = Bookmarks::Get().findOrAddTag(amsatTagName);
    amsatTag->color = QColor("#4a90d9");  // Blue color for satellites

    // Extract base name from satellite family (e.g., "TEVEL-2" -> "TEVEL", "FEES-2" -> "FEES")
    auto getBaseName = [](const QString& name) -> QString {
        // Match patterns like "NAME-N", "NAME N", "NAME_N" where N is a number
        QRegularExpression re("^(.+?)[-_ ]?\\d+$");
        QRegularExpressionMatch match = re.match(name);
        if (match.hasMatch())
            return match.captured(1);
        return name;
    };

    // Structure to group satellites by frequency
    struct FreqEntry {
        QStringList rawNames;      // Original satellite names
        QString modulation;
        qint64 bandwidth;
        QString type;              // DL, BCN, UL
    };
    QMap<qint64, FreqEntry> freqMap;

    auto parseFrequency = [](const QString& freqStr) -> qint64 {
        if (freqStr.isEmpty() || freqStr == "null")
            return 0;
        // Handle frequency ranges like "145.850-145.950"
        QString freq = freqStr.split("-").first().trimmed();
        // Convert MHz to Hz
        bool ok;
        double mhz = freq.toDouble(&ok);
        if (ok && mhz > 0)
            return static_cast<qint64>(mhz * 1000000.0);
        return 0;
    };

    QJsonArray satellites = doc.array();

    for (const QJsonValue& satValue : satellites)
    {
        QJsonObject sat = satValue.toObject();
        QString name = sat["name"].toString();
        QString downlink = sat["downlink"].toString();
        QString beacon = sat["beacon"].toString();
        QString uplink = sat["uplink"].toString();
        QString mode = sat["mode"].toString();

        // Process downlink frequencies
        if (!downlink.isEmpty() && downlink != "null")
        {
            qint64 freq = parseFrequency(downlink);
            if (freq > 0)
            {
                if (freqMap.contains(freq))
                    freqMap[freq].rawNames.append(name);
                else
                    freqMap[freq] = {QStringList{name}, mapSatelliteModeToModulation(mode), 12500, "DL"};
            }
        }

        // Process beacon frequencies
        if (!beacon.isEmpty() && beacon != "null")
        {
            qint64 freq = parseFrequency(beacon);
            if (freq > 0)
            {
                QString bcnName = name + " BCN";
                if (freqMap.contains(freq))
                    freqMap[freq].rawNames.append(bcnName);
                else
                    freqMap[freq] = {QStringList{bcnName}, "CW-U", 500, "BCN"};
            }
        }

        // Process uplink frequencies
        if (!uplink.isEmpty() && uplink != "null")
        {
            qint64 freq = parseFrequency(uplink);
            if (freq > 0)
            {
                QString ulName = name + " UL";
                if (freqMap.contains(freq))
                    freqMap[freq].rawNames.append(ulName);
                else
                    freqMap[freq] = {QStringList{ulName}, mapSatelliteModeToModulation(mode), 12500, "UL"};
            }
        }
    }

    // Create bookmarks from grouped frequencies
    int addedCount = 0;
    for (auto it = freqMap.begin(); it != freqMap.end(); ++it)
    {
        BookmarkInfo info;
        info.frequency = it.key();

        // Group names by family and create condensed display
        QStringList& rawNames = it.value().rawNames;
        rawNames.removeDuplicates();

        // Count occurrences of each base name
        QMap<QString, int> baseNameCounts;
        for (const QString& name : rawNames)
        {
            QString baseName = getBaseName(name);
            baseNameCounts[baseName]++;
        }

        // Build display name with counts for families
        QStringList displayParts;
        QSet<QString> processedBases;
        for (const QString& name : rawNames)
        {
            QString baseName = getBaseName(name);
            if (processedBases.contains(baseName))
                continue;
            processedBases.insert(baseName);

            int count = baseNameCounts[baseName];
            if (count > 1)
                displayParts.append(QString("%1 (x%2)").arg(baseName).arg(count));
            else
                displayParts.append(name);
        }

        // Limit display length
        if (displayParts.size() <= 2)
            info.name = displayParts.join(", ");
        else
            info.name = displayParts.mid(0, 2).join(", ") + QString(" +%1").arg(displayParts.size() - 2);

        info.modulation = it.value().modulation;
        info.bandwidth = it.value().bandwidth;
        info.tags.append(amsatTag);
        Bookmarks::Get().add(info);
        addedCount++;
    }

    Bookmarks::Get().save();
    bookmarksTableModel->update();
    updateTags();

    QMessageBox::information(this, "AMSAT Update Complete",
        QString("Added %1 frequency bookmarks from %2 satellite entries.")
            .arg(addedCount).arg(satellites.size()));
}

QString DockBookmarks::mapSatelliteModeToModulation(const QString& mode)
{
    QString modeLower = mode.toLower();

    // SSB modes (typically used for linear transponders)
    if (modeLower.contains("ssb") || modeLower == "v" || modeLower == "u" ||
        modeLower == "l" || modeLower == "s" || modeLower == "a" || modeLower == "b" ||
        modeLower == "j" || modeLower == "t")
        return "USB";

    // FM modes
    if (modeLower.contains("fm") || modeLower.contains("nbfm"))
        return "Narrow FM";

    // CW modes
    if (modeLower.contains("cw"))
        return "CW-U";

    // Digital modes - use USB for audio-based digital
    if (modeLower.contains("bpsk") || modeLower.contains("afsk") ||
        modeLower.contains("fsk") || modeLower.contains("rtty") ||
        modeLower.contains("sstv"))
        return "USB";

    // Packet/data modes - typically FM
    if (modeLower.contains("packet") || modeLower.contains("aprs") ||
        modeLower.contains("ax.25") || modeLower.contains("9600") ||
        modeLower.contains("1200"))
        return "Narrow FM";

    // GMSK and similar
    if (modeLower.contains("gmsk") || modeLower.contains("gfsk"))
        return "Narrow FM";

    // LoRa
    if (modeLower.contains("lora"))
        return "Narrow FM";

    // Default to USB for unknown modes
    return "USB";
}

void DockBookmarks::on_btnGetLocation_clicked()
{
    ui->btnGetLocation->setText("...");
    ui->btnGetLocation->setEnabled(false);
    startLocationRequest();
}

void DockBookmarks::startLocationRequest()
{
    if (!m_positionSource)
    {
        m_positionSource = QGeoPositionInfoSource::createDefaultSource(this);
        if (m_positionSource)
        {
            connect(m_positionSource, &QGeoPositionInfoSource::positionUpdated,
                    this, &DockBookmarks::onPositionUpdated);
            connect(m_positionSource, &QGeoPositionInfoSource::errorOccurred,
                    this, &DockBookmarks::onPositionError);
        }
    }

    if (m_positionSource)
    {
        m_positionSource->requestUpdate(10000);  // 10 second timeout
    }
    else
    {
        // No GPS available, try IP geolocation
        tryIpGeolocationWithOptions();
    }
}

void DockBookmarks::tryIpGeolocationWithOptions()
{
    // Note: ip-api.com free tier only supports HTTP
    QNetworkRequest request(QUrl("http://ip-api.com/json/?fields=status,lat,lon,city,regionName"));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        ui->btnGetLocation->setText("📍");
        ui->btnGetLocation->setEnabled(true);

        bool ipSuccess = false;
        QString cityName;

        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject obj = doc.object();
            if (obj["status"].toString() == "success") {
                m_latitude = obj["lat"].toDouble();
                m_longitude = obj["lon"].toDouble();
                cityName = QString("%1, %2").arg(obj["city"].toString(), obj["regionName"].toString());
                m_hasLocation = true;
                ipSuccess = true;
            }
        }

        // Show dialog with options
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Set Location");
        if (ipSuccess) {
            msgBox.setText(QString("IP-based location detected:\n%1\n(%2, %3)")
                .arg(cityName)
                .arg(m_latitude, 0, 'f', 4)
                .arg(m_longitude, 0, 'f', 4));
        } else {
            msgBox.setText("Could not detect location from IP.");
        }
        msgBox.setInformativeText("Choose location method:");

        QPushButton *useIpBtn = nullptr;
        if (ipSuccess) {
            useIpBtn = msgBox.addButton("Use IP Location", QMessageBox::AcceptRole);
        }
        QPushButton *zipBtn = msgBox.addButton("Enter ZIP Code", QMessageBox::ActionRole);
        QPushButton *manualBtn = msgBox.addButton("Enter Lat/Lon", QMessageBox::ActionRole);
        msgBox.addButton(QMessageBox::Cancel);

        msgBox.exec();

        if (msgBox.clickedButton() == useIpBtn) {
            ui->btnGetLocation->setToolTip(
                QString("Location: %1 (%2, %3)").arg(cityName).arg(m_latitude, 0, 'f', 4).arg(m_longitude, 0, 'f', 4));
            ui->btnGetLocation->setText(QString("📍 %1").arg(cityName));
        } else if (msgBox.clickedButton() == zipBtn) {
            bool ok;
            QString zip = QInputDialog::getText(this, "Enter ZIP Code",
                "Enter US ZIP code:", QLineEdit::Normal, "", &ok);
            if (ok && !zip.isEmpty()) {
                QNetworkRequest zipReq(QUrl(QString("https://api.zippopotam.us/us/%1").arg(zip.trimmed())));
                QNetworkReply *zipReply = m_networkManager->get(zipReq);
                connect(zipReply, &QNetworkReply::finished, this, [this, zipReply]() {
                    zipReply->deleteLater();
                    if (zipReply->error() == QNetworkReply::NoError) {
                        QJsonDocument doc = QJsonDocument::fromJson(zipReply->readAll());
                        QJsonObject obj = doc.object();
                        QJsonArray places = obj["places"].toArray();
                        if (!places.isEmpty()) {
                            QJsonObject place = places[0].toObject();
                            m_latitude = place["latitude"].toString().toDouble();
                            m_longitude = place["longitude"].toString().toDouble();
                            QString placeName = QString("%1, %2")
                                .arg(place["place name"].toString())
                                .arg(place["state abbreviation"].toString());
                            m_hasLocation = true;
                            ui->btnGetLocation->setToolTip(
                                QString("Location: %1 (%2, %3)").arg(placeName).arg(m_latitude, 0, 'f', 4).arg(m_longitude, 0, 'f', 4));
                            ui->btnGetLocation->setText(QString("📍 %1").arg(placeName));
                            return;
                        }
                    }
                    QMessageBox::warning(this, "ZIP Lookup Failed", "Could not find location for that ZIP code.");
                });
            }
        } else if (msgBox.clickedButton() == manualBtn) {
            bool ok;
            QString input = QInputDialog::getText(this, "Enter Coordinates",
                "Enter lat, lon (e.g., 41.8781, -87.6298):",
                QLineEdit::Normal, "", &ok);
            if (ok && !input.isEmpty()) {
                QStringList parts = input.split(",");
                if (parts.size() == 2) {
                    double lat = parts[0].trimmed().toDouble();
                    double lon = parts[1].trimmed().toDouble();
                    // Validate lat/lon ranges
                    if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180) {
                        m_latitude = lat;
                        m_longitude = lon;
                        m_hasLocation = true;
                        ui->btnGetLocation->setToolTip(
                            QString("Location: %1, %2").arg(m_latitude, 0, 'f', 4).arg(m_longitude, 0, 'f', 4));
                        ui->btnGetLocation->setText(QString("📍 %1, %2").arg(m_latitude, 0, 'f', 2).arg(m_longitude, 0, 'f', 2));
                    } else {
                        QMessageBox::warning(this, "Invalid Coordinates",
                            "Latitude must be -90 to 90, longitude must be -180 to 180.");
                    }
                }
            }
        }
    });
}

void DockBookmarks::onPositionUpdated(const QGeoPositionInfo &info)
{
    if (info.isValid())
    {
        ui->btnGetLocation->setText("📍");
        ui->btnGetLocation->setEnabled(true);

        m_latitude = info.coordinate().latitude();
        m_longitude = info.coordinate().longitude();
        m_hasLocation = true;
        ui->btnGetLocation->setToolTip(
            QString("Location: %1, %2").arg(m_latitude, 0, 'f', 4).arg(m_longitude, 0, 'f', 4));
        ui->btnGetLocation->setText(QString("📍 %1, %2").arg(m_latitude, 0, 'f', 2).arg(m_longitude, 0, 'f', 2));
        QMessageBox::information(this, "Location Updated",
            QString("GPS Location: %1, %2").arg(m_latitude, 0, 'f', 4).arg(m_longitude, 0, 'f', 4));
    }
    else
    {
        tryIpGeolocationWithOptions();
    }
}

void DockBookmarks::onPositionError(QGeoPositionInfoSource::Error /*error*/)
{
    tryIpGeolocationWithOptions();
}

double DockBookmarks::haversineDistance(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 3958.8;  // Earth's radius in miles
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat/2) * sin(dLat/2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dLon/2) * sin(dLon/2);
    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

void DockBookmarks::on_btnImportRR_clicked()
{
    emit openRadioReferenceRequested();
}

void DockBookmarks::on_btnImportFM_clicked()
{
    if (!m_hasLocation)
    {
        QMessageBox::warning(this, "Location Required",
            "Please set your location first using the location button.");
        return;
    }

    bool ok;
    int radius = QInputDialog::getInt(this, "Import FM Stations",
        "Enter search radius in miles:", 10, 1, 9999, 1, &ok);
    if (!ok)
        return;

    m_radiusMiles = radius;
    ui->btnImportFM->setEnabled(false);
    ui->btnImportFM->setText("...");

    QUrl url("https://transition.fcc.gov/fcc-bin/fmq?state=&list=4");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 gqrx-sdr");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFccDataReceived(reply, true);
    });
}

void DockBookmarks::on_btnImportTV_clicked()
{
    if (!m_hasLocation)
    {
        QMessageBox::warning(this, "Location Required",
            "Please set your location first using the location button.");
        return;
    }

    bool ok;
    int radius = QInputDialog::getInt(this, "Import TV Stations",
        "Enter search radius in miles:", 10, 1, 9999, 1, &ok);
    if (!ok)
        return;

    m_radiusMiles = radius;
    ui->btnImportTV->setEnabled(false);
    ui->btnImportTV->setText("...");

    QUrl url("https://transition.fcc.gov/fcc-bin/tvq?state=&list=4");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 gqrx-sdr");

    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onFccDataReceived(reply, false);
    });
}

void DockBookmarks::onFccDataReceived(QNetworkReply* reply, bool isFM)
{
    if (isFM) {
        ui->btnImportFM->setEnabled(true);
        ui->btnImportFM->setText("Import FM");
    } else {
        ui->btnImportTV->setEnabled(true);
        ui->btnImportTV->setText("Import TV");
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        QMessageBox::warning(this, isFM ? "FM Import Failed" : "TV Import Failed",
            QString("Failed to fetch data:\n%1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (data.isEmpty()) {
        QMessageBox::warning(this, "Import Failed", "Received empty response from FCC server.");
        return;
    }

    QString tagName = isFM ? "FM Stations" : "TV Stations";

    // Remove existing bookmarks with this tag
    for (int i = Bookmarks::Get().size() - 1; i >= 0; i--)
    {
        BookmarkInfo& bm = Bookmarks::Get().getBookmark(i);
        for (const auto& tag : bm.tags)
        {
            if (tag->name == tagName)
            {
                Bookmarks::Get().remove(i);
                break;
            }
        }
    }

    TagInfo::sptr stationTag = Bookmarks::Get().findOrAddTag(tagName);
    stationTag->color = isFM ? QColor("#e57373") : QColor("#64b5f6");

    QString text = QString::fromUtf8(data);
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);

    // First pass: collect stations and keep only closest per frequency
    struct StationInfo {
        QString callsign;
        qint64 frequency;
        double distance;
    };
    QMap<qint64, StationInfo> closestByFreq;

    for (const QString& line : lines)
    {
        if (!line.startsWith('|'))
            continue;

        QStringList fields = line.split('|');

        if (fields.size() < 27)
            continue;

        // Only import Licensed stations
        QString status = fields[9].trimmed();
        if (status != "LIC")
            continue;

        QString callsign = fields[1].trimmed();
        QString freqStr = fields[2].trimmed().replace(" MHz", "");

        double freq = freqStr.toDouble();

        // For TV, field 2 is "-" and field 4 is channel number
        if (freq < 1 && !isFM && fields.size() > 4)
        {
            int channel = fields[4].trimmed().toInt();
            // Convert TV channel to center frequency (MHz)
            // VHF-Lo: Ch 2-6 (54-88 MHz, 6 MHz spacing)
            // VHF-Hi: Ch 7-13 (174-216 MHz, 6 MHz spacing)
            // UHF: Ch 14-36 (470-608 MHz, 6 MHz spacing)
            if (channel >= 2 && channel <= 6)
                freq = 57 + (channel - 2) * 6;  // Ch2=57, Ch3=63, etc.
            else if (channel >= 7 && channel <= 13)
                freq = 177 + (channel - 7) * 6;  // Ch7=177, Ch8=183, etc.
            else if (channel >= 14 && channel <= 36)
                freq = 473 + (channel - 14) * 6;  // Ch14=473, etc.
        }

        if (freq < 1)
            continue;

        // Parse lat/lon from fixed field positions
        double lat = 0, lon = 0;
        QString latDir = fields[19].trimmed();
        QString lonDir = fields[23].trimmed();

        if ((latDir == "N" || latDir == "S") && (lonDir == "W" || lonDir == "E"))
        {
            int latDeg = fields[20].trimmed().toInt();
            int latMin = fields[21].trimmed().toInt();
            double latSec = fields[22].trimmed().toDouble();
            lat = latDeg + latMin/60.0 + latSec/3600.0;
            if (latDir == "S") lat = -lat;

            int lonDeg = fields[24].trimmed().toInt();
            int lonMin = fields[25].trimmed().toInt();
            double lonSec = fields[26].trimmed().toDouble();
            lon = lonDeg + lonMin/60.0 + lonSec/3600.0;
            if (lonDir == "W") lon = -lon;
        }

        if (lat == 0 && lon == 0)
            continue;

        double distance = haversineDistance(m_latitude, m_longitude, lat, lon);
        if (distance > m_radiusMiles)
            continue;

        qint64 freqHz = (qint64)(freq * 1e6);

        // Keep only the closest station for each frequency
        if (!closestByFreq.contains(freqHz) || distance < closestByFreq[freqHz].distance)
        {
            StationInfo info;
            info.callsign = callsign;
            info.frequency = freqHz;
            info.distance = distance;
            closestByFreq[freqHz] = info;
        }
    }

    // Second pass: add the closest stations as bookmarks
    int addedCount = 0;
    for (const StationInfo& station : closestByFreq)
    {
        BookmarkInfo info;
        info.frequency = station.frequency;
        info.name = station.callsign;
        info.modulation = isFM ? "WFM (stereo)" : "WFM (mono)";
        info.bandwidth = isFM ? 150000 : 6000000;
        info.tags.append(stationTag);
        Bookmarks::Get().add(info);
        addedCount++;
    }

    Bookmarks::Get().save();
    bookmarksTableModel->update();
    updateTags();

    QMessageBox::information(this, isFM ? "FM Import Complete" : "TV Import Complete",
        QString("Added %1 stations within %2 miles of your location.")
            .arg(addedCount).arg(m_radiusMiles));
}
