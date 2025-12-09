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
#include <QNetworkAccessManager>
#include <QNetworkReply>

/* Frequency result from RadioReference */
struct RRFrequency
{
    double   frequency;      // Frequency in Hz
    QString  description;    // Description/name
    QString  mode;           // Modulation mode (FM, NFM, AM, etc.)
    QString  tone;           // PL/CTCSS tone
    QString  alpha_tag;      // Short tag/identifier
    QString  tag;            // Category tag (Police, Fire, EMS, etc.)
    QString  agency;         // Agency name
    int      county_id;
    int      state_id;
};

/* State info */
struct RRState
{
    int     id;
    QString name;
    QString abbrev;
};

/* County info */
struct RRCounty
{
    int     id;
    int     state_id;
    QString name;
};

/* Metro area info */
struct RRMetro
{
    int     id;
    QString name;
};

class RadioReference : public QObject
{
    Q_OBJECT

public:
    explicit RadioReference(QObject *parent = nullptr);
    ~RadioReference();

    /* Set credentials */
    void setCredentials(const QString &username, const QString &password, const QString &appKey);
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

    QNetworkAccessManager *m_networkManager;
    QString m_username;
    QString m_password;
    QString m_appKey;
    QString m_pendingRequestType;

    static const QString API_ENDPOINT;
};

#endif // RADIOREFERENCE_H
