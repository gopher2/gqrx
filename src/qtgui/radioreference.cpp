/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * RadioReference.com API client for frequency lookups and imports.
 */
#include <QDebug>
#include <QXmlStreamReader>
#include <QUrl>
#include "radioreference.h"

const QString RadioReference::API_ENDPOINT = "http://api.radioreference.com/soap2/";

RadioReference::RadioReference(QObject *parent)
    : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &RadioReference::onNetworkReply);
}

RadioReference::~RadioReference()
{
}

void RadioReference::setCredentials(const QString &username, const QString &password, const QString &appKey)
{
    m_username = username;
    m_password = password;
    m_appKey = appKey;
}

bool RadioReference::hasCredentials() const
{
    return !m_username.isEmpty() && !m_password.isEmpty() && !m_appKey.isEmpty();
}

QString RadioReference::buildAuthInfo()
{
    return QString(
        "<authInfo>"
        "  <appKey>%1</appKey>"
        "  <username>%2</username>"
        "  <password>%3</password>"
        "  <version>latest</version>"
        "  <style>rpc</style>"
        "</authInfo>"
    ).arg(m_appKey.toHtmlEscaped(),
          m_username.toHtmlEscaped(),
          m_password.toHtmlEscaped());
}

QString RadioReference::buildSoapEnvelope(const QString &method, const QString &body)
{
    return QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">"
        "<soap:Body>"
        "<%1 xmlns=\"http://api.radioreference.com\">"
        "%2"
        "%3"
        "</%1>"
        "</soap:Body>"
        "</soap:Envelope>"
    ).arg(method, buildAuthInfo(), body);
}

void RadioReference::sendRequest(const QString &soapEnvelope, const QString &requestType)
{
    if (!hasCredentials()) {
        emit error("RadioReference credentials not configured");
        return;
    }

    m_pendingRequestType = requestType;

    QNetworkRequest request;
    request.setUrl(QUrl(API_ENDPOINT));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml;charset=UTF-8");
    request.setRawHeader("User-Agent", "gqrx-sdr");

    m_networkManager->post(request, soapEnvelope.toUtf8());
}

void RadioReference::getStateList()
{
    QString envelope = buildSoapEnvelope("getStateList", "");
    sendRequest(envelope, "stateList");
}

void RadioReference::getCountyList(int stateId)
{
    QString body = QString("<stid>%1</stid>").arg(stateId);
    QString envelope = buildSoapEnvelope("getCountyList", body);
    sendRequest(envelope, "countyList");
}

void RadioReference::getMetroList(int stateId)
{
    QString body = QString("<stid>%1</stid>").arg(stateId);
    QString envelope = buildSoapEnvelope("getMetroList", body);
    sendRequest(envelope, "metroList");
}

void RadioReference::searchCountyFrequencies(int countyId, double frequencyMhz)
{
    QString body = QString("<ctid>%1</ctid>").arg(countyId);
    if (frequencyMhz > 0) {
        body += QString("<freq>%1</freq>").arg(frequencyMhz, 0, 'f', 4);
    }
    QString envelope = buildSoapEnvelope("searchCountyFreq", body);
    sendRequest(envelope, "frequencies");
}

void RadioReference::searchStateFrequencies(int stateId, double frequencyMhz)
{
    QString body = QString("<stid>%1</stid>").arg(stateId);
    if (frequencyMhz > 0) {
        body += QString("<freq>%1</freq>").arg(frequencyMhz, 0, 'f', 4);
    }
    QString envelope = buildSoapEnvelope("searchStateFreq", body);
    sendRequest(envelope, "frequencies");
}

void RadioReference::searchMetroFrequencies(int metroId, double frequencyMhz)
{
    QString body = QString("<mid>%1</mid>").arg(metroId);
    if (frequencyMhz > 0) {
        body += QString("<freq>%1</freq>").arg(frequencyMhz, 0, 'f', 4);
    }
    QString envelope = buildSoapEnvelope("searchMetroFreq", body);
    sendRequest(envelope, "frequencies");
}

void RadioReference::getCountyFreqsByTag(int countyId, int tagId)
{
    QString body = QString("<ctid>%1</ctid>").arg(countyId);
    if (tagId > 0) {
        body += QString("<tag>%1</tag>").arg(tagId);
    }
    QString envelope = buildSoapEnvelope("getCountyFreqsByTag", body);
    sendRequest(envelope, "frequencies");
}

void RadioReference::lookupFrequency(double frequencyMhz, int countyId, int stateId)
{
    // Search with frequency in the specified location
    if (countyId > 0) {
        QString body = QString("<ctid>%1</ctid><freq>%2</freq>")
            .arg(countyId)
            .arg(frequencyMhz, 0, 'f', 4);
        QString envelope = buildSoapEnvelope("searchCountyFreq", body);
        sendRequest(envelope, "lookup");
    } else if (stateId > 0) {
        QString body = QString("<stid>%1</stid><freq>%2</freq>")
            .arg(stateId)
            .arg(frequencyMhz, 0, 'f', 4);
        QString envelope = buildSoapEnvelope("searchStateFreq", body);
        sendRequest(envelope, "lookup");
    } else {
        emit error("County or State ID required for frequency lookup");
    }
}

void RadioReference::getUserData()
{
    QString envelope = buildSoapEnvelope("getUserData", "");
    sendRequest(envelope, "userData");
}

void RadioReference::onNetworkReply(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit error(QString("Network error: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QString xml = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    // Check for SOAP fault
    if (xml.contains("<faultstring>")) {
        QXmlStreamReader reader(xml);
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QLatin1String("faultstring")) {
                emit error(QString("API error: %1").arg(reader.readElementText()));
                return;
            }
        }
    }

    // Route to appropriate parser
    if (m_pendingRequestType == "stateList") {
        parseStateList(xml);
    } else if (m_pendingRequestType == "countyList") {
        parseCountyList(xml);
    } else if (m_pendingRequestType == "metroList") {
        parseMetroList(xml);
    } else if (m_pendingRequestType == "frequencies") {
        parseFrequencies(xml, false);
    } else if (m_pendingRequestType == "lookup") {
        parseFrequencies(xml, true);
    } else if (m_pendingRequestType == "userData") {
        parseUserData(xml);
    }
}

void RadioReference::parseStateList(const QString &xml)
{
    QList<RRState> states;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("item")) {
            RRState state;
            while (!(reader.isEndElement() && reader.name() == QLatin1String("item"))) {
                reader.readNext();
                if (reader.isStartElement()) {
                    QString name = reader.name().toString();
                    QString value = reader.readElementText();
                    if (name == "stid") state.id = value.toInt();
                    else if (name == "stateName") state.name = value;
                    else if (name == "stateAbbr") state.abbrev = value;
                }
            }
            if (state.id > 0) {
                states.append(state);
            }
        }
    }

    emit stateListReceived(states);
}

void RadioReference::parseCountyList(const QString &xml)
{
    QList<RRCounty> counties;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("item")) {
            RRCounty county;
            while (!(reader.isEndElement() && reader.name() == QLatin1String("item"))) {
                reader.readNext();
                if (reader.isStartElement()) {
                    QString name = reader.name().toString();
                    QString value = reader.readElementText();
                    if (name == "ctid") county.id = value.toInt();
                    else if (name == "stid") county.state_id = value.toInt();
                    else if (name == "countyName") county.name = value;
                }
            }
            if (county.id > 0) {
                counties.append(county);
            }
        }
    }

    emit countyListReceived(counties);
}

void RadioReference::parseMetroList(const QString &xml)
{
    QList<RRMetro> metros;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("item")) {
            RRMetro metro;
            while (!(reader.isEndElement() && reader.name() == QLatin1String("item"))) {
                reader.readNext();
                if (reader.isStartElement()) {
                    QString name = reader.name().toString();
                    QString value = reader.readElementText();
                    if (name == "mid") metro.id = value.toInt();
                    else if (name == "metroName") metro.name = value;
                }
            }
            if (metro.id > 0) {
                metros.append(metro);
            }
        }
    }

    emit metroListReceived(metros);
}

void RadioReference::parseFrequencies(const QString &xml, bool isLookup)
{
    QList<RRFrequency> frequencies;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("item")) {
            RRFrequency freq;
            while (!(reader.isEndElement() && reader.name() == QLatin1String("item"))) {
                reader.readNext();
                if (reader.isStartElement()) {
                    QString name = reader.name().toString();
                    QString value = reader.readElementText();
                    if (name == "freq" || name == "out") {
                        freq.frequency = value.toDouble() * 1e6; // MHz to Hz
                    }
                    else if (name == "descr") freq.description = value;
                    else if (name == "mode") freq.mode = value;
                    else if (name == "tone") freq.tone = value;
                    else if (name == "alpha") freq.alpha_tag = value;
                    else if (name == "tag" || name == "catDesc") freq.tag = value;
                    else if (name == "agency" || name == "agencyName") freq.agency = value;
                    else if (name == "ctid") freq.county_id = value.toInt();
                    else if (name == "stid") freq.state_id = value.toInt();
                }
            }
            if (freq.frequency > 0) {
                frequencies.append(freq);
            }
        }
    }

    if (isLookup) {
        emit lookupResult(frequencies);
    } else {
        emit frequenciesReceived(frequencies);
    }
}

void RadioReference::parseUserData(const QString &xml)
{
    QXmlStreamReader reader(xml);
    bool isPremium = false;
    QString expiresDate;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            QString name = reader.name().toString();
            if (name == "subExpireDate") {
                expiresDate = reader.readElementText();
                isPremium = !expiresDate.isEmpty();
            }
        }
    }

    emit userDataReceived(isPremium, expiresDate);
}
