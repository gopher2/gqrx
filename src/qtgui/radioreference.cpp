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
#include <QByteArray>
#include "radioreference.h"

const QString RadioReference::API_ENDPOINT = "https://api.radioreference.com/soap2/";

RadioReference::RadioReference(QObject *parent)
    : QObject(parent)
    , m_currentCountyId(0)
    , m_totalSubcats(0)
{
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &RadioReference::onNetworkReply);

    // Hardcode app key only - username/password set via setCredentials()
    m_appKey = QString::fromUtf8(QByteArray::fromBase64("NjAxYWIzM2UtZDVlMy0xMWYwLWJiMzItMGVmOTc0MzNiNWY5"));
}

RadioReference::~RadioReference()
{
}

void RadioReference::setCredentials(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
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
        "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
        "xmlns:tns=\"http://api.radioreference.com/soap2\">"
        "<soap:Body>"
        "<tns:%1>"
        "%2"
        "%3"
        "</tns:%1>"
        "</soap:Body>"
        "</soap:Envelope>"
    ).arg(method, body, buildAuthInfo());
}

void RadioReference::sendRequest(const QString &soapEnvelope, const QString &requestType)
{
    if (!hasCredentials()) {
        emit error("RadioReference credentials not configured");
        return;
    }

    qDebug() << "[radioreference.cpp:sendRequest()] Sending request type:" << requestType;

    QNetworkRequest request;
    request.setUrl(QUrl(API_ENDPOINT));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml;charset=UTF-8");
    request.setRawHeader("User-Agent", "gqrx-sdr");

    QNetworkReply *reply = m_networkManager->post(request, soapEnvelope.toUtf8());
    reply->setProperty("requestType", requestType);
}

void RadioReference::getStateList()
{
    // Use getCountryInfo with US country ID (1) to get list of states
    QString body = "<coid>1</coid>";
    QString envelope = buildSoapEnvelope("getCountryInfo", body);
    sendRequest(envelope, "stateList");
}

void RadioReference::getCountyList(int stateId)
{
    // Use getStateInfo to get counties for a state
    QString body = QString("<stid>%1</stid>").arg(stateId);
    QString envelope = buildSoapEnvelope("getStateInfo", body);
    sendRequest(envelope, "countyList");
}

void RadioReference::getMetroList(int stateId)
{
    // Use getMetroArea to get metro areas for a state
    QString body = QString("<stid>%1</stid>").arg(stateId);
    QString envelope = buildSoapEnvelope("getMetroArea", body);
    sendRequest(envelope, "metroList");
}

void RadioReference::searchCountyFrequencies(int countyId, double frequencyMhz)
{
    if (frequencyMhz > 0) {
        // Search for specific frequency
        qDebug() << "[radioreference.cpp:searchCountyFrequencies()] Searching for frequency" << frequencyMhz << "MHz in county" << countyId;
        QString body = QString("<ctid>%1</ctid><freq>%2</freq>").arg(countyId).arg(frequencyMhz, 0, 'f', 4);
        QString envelope = buildSoapEnvelope("searchCountyFreq", body);
        sendRequest(envelope, "frequencies");
    } else {
        // Get all frequencies via getCountyInfo -> getSubcatFreqs for each subcategory
        qDebug() << "[radioreference.cpp:searchCountyFrequencies()] Fetching all frequencies for county" << countyId;
        m_currentCountyId = countyId;
        m_pendingSubcatIds.clear();
        m_aggregatedFrequencies.clear();
        QString body = QString("<ctid>%1</ctid>").arg(countyId);
        QString envelope = buildSoapEnvelope("getCountyInfo", body);
        sendRequest(envelope, "countyInfoForFreqs");
    }
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
    QString requestType = reply->property("requestType").toString();
    qDebug() << "[radioreference.cpp:onNetworkReply()] Received reply for:" << requestType;

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[radioreference.cpp:onNetworkReply()] Network error:" << reply->errorString();
        emit error(QString("Network error: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QString xml = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    qDebug() << "[radioreference.cpp:onNetworkReply()] Response length:" << xml.length();

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
    if (requestType == "stateList") {
        parseStateList(xml);
    } else if (requestType == "countyList") {
        parseCountyList(xml);
    } else if (requestType == "metroList") {
        parseMetroList(xml);
    } else if (requestType == "frequencies") {
        parseFrequencies(xml, false);
    } else if (requestType == "lookup") {
        parseFrequencies(xml, true);
    } else if (requestType == "userData") {
        parseUserData(xml);
    } else if (requestType == "countyInfoForFreqs") {
        parseCountyInfoForFreqs(xml);
    } else if (requestType == "subcatFreqs") {
        parseSubcatFreqs(xml);
    }
}

void RadioReference::parseStateList(const QString &xml)
{
    QList<RRState> states;
    QXmlStreamReader reader(xml);
    bool inStateList = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("stateList")) {
            inStateList = true;
        } else if (reader.isEndElement() && reader.name() == QLatin1String("stateList")) {
            inStateList = false;
        } else if (inStateList && reader.isStartElement() && reader.name() == QLatin1String("item")) {
            RRState state;
            while (!(reader.isEndElement() && reader.name() == QLatin1String("item"))) {
                reader.readNext();
                if (reader.isStartElement()) {
                    QString name = reader.name().toString();
                    QString value = reader.readElementText();
                    if (name == "stid") state.id = value.toInt();
                    else if (name == "stateName") state.name = value;
                    else if (name == "stateCode") state.abbrev = value;
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
    bool inCountyList = false;

    qDebug() << "[radioreference.cpp:parseCountyList()] Parsing county list, xml length:" << xml.length();

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("countyList")) {
            inCountyList = true;
            qDebug() << "[radioreference.cpp:parseCountyList()] Found countyList element";
        } else if (reader.isEndElement() && reader.name() == QLatin1String("countyList")) {
            inCountyList = false;
        } else if (inCountyList && reader.isStartElement() && reader.name() == QLatin1String("item")) {
            RRCounty county;
            while (!(reader.isEndElement() && reader.name() == QLatin1String("item"))) {
                reader.readNext();
                if (reader.isStartElement()) {
                    QString name = reader.name().toString();
                    QString value = reader.readElementText();
                    if (name == "ctid") county.id = value.toInt();
                    else if (name == "countyName") county.name = value;
                }
            }
            if (county.id > 0) {
                counties.append(county);
            }
        }
    }

    qDebug() << "[radioreference.cpp:parseCountyList()] Parsed" << counties.size() << "counties";
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

void RadioReference::parseCountyInfoForFreqs(const QString &xml)
{
    qDebug() << "[radioreference.cpp:parseCountyInfoForFreqs()] Parsing county info for subcategory IDs";

    QXmlStreamReader reader(xml);
    m_subcatNames.clear();
    int currentScid = 0;
    QString currentScName;
    bool inSubcats = false;
    bool inSubcatItem = false;
    int depth = 0;
    int subcatsDepth = 0;    // Track depth where subcats was found
    int subcatItemDepth = 0; // Track depth where current subcategory item was found

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.hasError()) {
            qDebug() << "[radioreference.cpp:parseCountyInfoForFreqs()] XML Error:" << reader.errorString();
            break;
        }

        if (reader.isStartElement()) {
            QString name = reader.name().toString();
            depth++;

            if (name == "subcats") {
                inSubcats = true;
                subcatsDepth = depth;
            } else if (inSubcats && name == "item" && depth == subcatsDepth + 1) {
                // Only process items directly inside subcats (not nested deeper)
                inSubcatItem = true;
                subcatItemDepth = depth;
                currentScid = 0;
                currentScName.clear();
            } else if (inSubcatItem && name == "scid" && depth == subcatItemDepth + 1) {
                currentScid = reader.readElementText().toInt();
                depth--;  // readElementText consumes the end element
            } else if (inSubcatItem && name == "scName" && depth == subcatItemDepth + 1) {
                currentScName = reader.readElementText();
                depth--;  // readElementText consumes the end element
            }
        } else if (reader.isEndElement()) {
            QString name = reader.name().toString();

            if (name == "subcats" && depth == subcatsDepth) {
                inSubcats = false;
            } else if (name == "item" && inSubcatItem && depth == subcatItemDepth) {
                // End of subcategory item - save it
                if (currentScid > 0) {
                    m_pendingSubcatIds.append(currentScid);
                    m_subcatNames[currentScid] = currentScName;
                }
                inSubcatItem = false;
            }
            depth--;
        }
    }

    m_totalSubcats = m_pendingSubcatIds.size();
    qDebug() << "[radioreference.cpp:parseCountyInfoForFreqs()] Found" << m_totalSubcats << "subcategories";

    if (m_pendingSubcatIds.isEmpty()) {
        qDebug() << "[radioreference.cpp:parseCountyInfoForFreqs()] No subcategories found, emitting empty list";
        emit frequenciesReceived(QList<RRFrequency>());
    } else {
        fetchNextSubcat();
    }
}

void RadioReference::fetchNextSubcat()
{
    if (m_pendingSubcatIds.isEmpty()) {
        qDebug() << "[radioreference.cpp:fetchNextSubcat()] All subcategories fetched, emitting" << m_aggregatedFrequencies.size() << "frequencies";
        emit frequenciesReceived(m_aggregatedFrequencies);
        m_aggregatedFrequencies.clear();
        m_subcatNames.clear();
        return;
    }

    int scid = m_pendingSubcatIds.takeFirst();
    m_currentSubcatName = m_subcatNames.value(scid);

    int current = m_totalSubcats - m_pendingSubcatIds.size();
    emit fetchProgress(current, m_totalSubcats, m_aggregatedFrequencies.size(), m_currentSubcatName);
    qDebug() << "[radioreference.cpp:fetchNextSubcat()] Fetching subcategory" << scid << "(" << m_currentSubcatName << ")," << m_pendingSubcatIds.size() << "remaining";

    QString body = QString("<scid>%1</scid>").arg(scid);
    QString envelope = buildSoapEnvelope("getSubcatFreqs", body);
    sendRequest(envelope, "subcatFreqs");
}

void RadioReference::parseSubcatFreqs(const QString &xml)
{
    qDebug() << "[radioreference.cpp:parseSubcatFreqs()] Parsing subcategory frequencies";

    QXmlStreamReader reader(xml);
    int count = 0;
    RRFrequency freq;
    freq.county_id = m_currentCountyId;
    bool inFreqItem = false;
    int depth = 0;
    int freqItemDepth = 0;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            QString name = reader.name().toString();
            depth++;

            if (name == "item" && !inFreqItem) {
                // Start of a frequency item
                inFreqItem = true;
                freqItemDepth = depth;
                freq = RRFrequency();
                freq.county_id = m_currentCountyId;
                freq.tag = m_currentSubcatName;  // Set category from subcategory name
            } else if (inFreqItem && depth == freqItemDepth + 1) {
                // Direct child of frequency item - only read leaf elements we care about
                if (name == "out") {
                    freq.frequency = reader.readElementText().toDouble() * 1e6;
                    depth--;
                }
                else if (name == "in") {
                    freq.input_freq = reader.readElementText().toDouble() * 1e6;
                    depth--;
                }
                else if (name == "callsign") {
                    freq.callsign = reader.readElementText();
                    depth--;
                }
                else if (name == "alpha") {
                    freq.alpha_tag = reader.readElementText();
                    depth--;
                }
                else if (name == "descr") {
                    freq.description = reader.readElementText();
                    depth--;
                }
                else if (name == "mode") {
                    QString modeNum = reader.readElementText();
                    // Convert mode number to text
                    int m = modeNum.toInt();
                    switch (m) {
                        case 1: freq.mode = "FM"; break;
                        case 2: freq.mode = "AM"; break;
                        case 3: freq.mode = "USB"; break;
                        case 4: freq.mode = "LSB"; break;
                        case 5: freq.mode = "P25"; break;
                        case 6: freq.mode = "DMR"; break;
                        case 7: freq.mode = "NXDN"; break;
                        case 8: freq.mode = "D-STAR"; break;
                        case 9: freq.mode = "Fusion"; break;
                        default: freq.mode = modeNum; break;
                    }
                    depth--;
                }
                else if (name == "tone") {
                    freq.tone = reader.readElementText();
                    depth--;
                }
                else if (name == "colorCode") {
                    freq.color_code = reader.readElementText();
                    depth--;
                }
                else if (name == "tg") {
                    freq.talkgroup = reader.readElementText();
                    depth--;
                }
                else if (name == "slot") {
                    freq.slot = reader.readElementText();
                    depth--;
                }
                else if (name == "class") {
                    QString cls = reader.readElementText();
                    // Expand class abbreviations
                    if (cls == "RM") freq.service_class = "Repeater";
                    else if (cls == "BM") freq.service_class = "Base/Mobile";
                    else if (cls == "B") freq.service_class = "Base";
                    else if (cls == "M") freq.service_class = "Mobile";
                    else freq.service_class = cls;
                    depth--;
                }
                else if (name == "enc") {
                    freq.encrypted = reader.readElementText().toInt() != 0;
                    depth--;
                }
                // Skip container elements like <tags> - don't call readElementText on them
            }
        } else if (reader.isEndElement()) {
            QString name = reader.name().toString();

            if (name == "item" && inFreqItem && depth == freqItemDepth) {
                inFreqItem = false;
                if (freq.frequency > 0) {
                    m_aggregatedFrequencies.append(freq);
                    count++;
                }
            }
            depth--;
        }
    }

    qDebug() << "[radioreference.cpp:parseSubcatFreqs()] Added" << count << "frequencies, total now" << m_aggregatedFrequencies.size();

    // Fetch next subcategory
    fetchNextSubcat();
}
