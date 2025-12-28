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
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &DockBookmarks::onAmsatDataReceived);

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
    ui->btnUpdateAmsat->setText("Fetching...");

    QUrl url("https://raw.githubusercontent.com/palewire/amateur-satellite-database/main/data/amsat-active-frequencies.json");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "gqrx");
    m_networkManager->get(request);
}

void DockBookmarks::onAmsatDataReceived(QNetworkReply* reply)
{
    ui->btnUpdateAmsat->setEnabled(true);
    ui->btnUpdateAmsat->setText("Update AMSAT Satellites");

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
