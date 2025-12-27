/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * RadioReference.com API client for frequency lookups and imports.
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
#ifndef RADIOREFERENCE_H
#define RADIOREFERENCE_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>

/* Frequency result from RadioReference */
struct RRFrequency
{
    double   frequency = 0;      // Frequency in Hz (output/TX)
    double   input_freq = 0;     // Input frequency for repeaters
    QString  callsign;           // Callsign
    QString  alpha_tag;          // Short tag/identifier
    QString  description;        // Description/name
    QString  mode;               // Modulation mode (FM, NFM, AM, etc.)
    QString  tone;               // PL/CTCSS tone
    QString  color_code;         // DMR color code
    QString  talkgroup;          // DMR talkgroup
    QString  slot;               // DMR slot
    QString  service_class;      // Service class (RM=Repeater, BM=Base/Mobile, etc.)
    QString  tag;                // Category tag (Police, Fire, EMS, etc.)
    QString  agency;             // Agency name
    int      county_id = 0;
    int      state_id = 0;
    bool     encrypted = false;  // Encrypted flag
};

/* State info */
struct RRState
{
    int     id = 0;
    QString name;
    QString abbrev;
};

/* County info */
struct RRCounty
{
    int     id = 0;
    QString name;
};

/* Metro area info */
struct RRMetro
{
    int     id = 0;
    QString name;
};

class RadioReference : public QObject
{
    Q_OBJECT

public:
    explicit RadioReference(QObject *parent = nullptr);
    ~RadioReference();

    /* Set credentials (app key is built-in) */
    void setCredentials(const QString &username, const QString &password);
    bool hasCredentials() const;

    /* Get location lists */
    void getStateList();
    void getCountyList(int stateId);
    void getMetroList(int stateId);

    /* Search frequencies by location */
    void searchCountyFrequencies(int countyId, double frequencyMhz = 0.0);
    void searchStateFrequencies(int stateId, double frequencyMhz = 0.0);
    void searchMetroFrequencies(int metroId, double frequencyMhz = 0.0);

    /* Get frequencies by tag (category) */
    void getCountyFreqsByTag(int countyId, int tagId = 0);

    /* Lookup frequency (for reverse lookup feature) */
    void lookupFrequency(double frequencyMhz, int countyId = 0, int stateId = 0);

    /* Get user subscription status */
    void getUserData();

signals:
    void stateListReceived(const QList<RRState> &states);
    void countyListReceived(const QList<RRCounty> &counties);
    void metroListReceived(const QList<RRMetro> &metros);
    void frequenciesReceived(const QList<RRFrequency> &frequencies);
    void lookupResult(const QList<RRFrequency> &matches);
    void userDataReceived(bool isPremium, const QString &expiresDate);
    void fetchProgress(int current, int total, int freqsSoFar, const QString &categoryName);
    void error(const QString &message);

private slots:
    void onNetworkReply(QNetworkReply *reply);

private:
    QString buildSoapEnvelope(const QString &method, const QString &body);
    QString buildAuthInfo();
    void sendRequest(const QString &soapEnvelope, const QString &requestType);

    void parseStateList(const QString &xml);
    void parseCountyList(const QString &xml);
    void parseMetroList(const QString &xml);
    void parseFrequencies(const QString &xml, bool isLookup = false);
    void parseUserData(const QString &xml);
    void parseCountyInfoForFreqs(const QString &xml);
    void parseSubcatFreqs(const QString &xml);
    void fetchNextSubcat();

    QNetworkAccessManager *m_networkManager;
    QString m_username;
    QString m_password;
    QString m_appKey;

    // For multi-request frequency fetching
    QList<int> m_pendingSubcatIds;
    QMap<int, QString> m_subcatNames;  // scid -> subcategory name
    QString m_currentSubcatName;
    QList<RRFrequency> m_aggregatedFrequencies;
    int m_currentCountyId;
    int m_totalSubcats;

    static const QString API_ENDPOINT;
};

#endif // RADIOREFERENCE_H
