/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           https://gqrx.dk/
 *
 * Copyright 2025 David Kierzkowski K9DPD
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
#ifndef FILENAME_TEMPLATE_H
#define FILENAME_TEMPLATE_H

#include <QString>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

/**
 * @brief Parameters for filename template expansion.
 */
struct FilenameParams {
    QDateTime timestamp;        ///< Recording timestamp (default: now)
    double frequency_hz = 0;    ///< Tuner frequency in Hz
    QString mode;               ///< Demodulation mode (NFM, AM, USB, etc.)
    int tuner_id = 0;           ///< Tuner index
    QString tuner_name;         ///< Tuner display name
    QString type;               ///< Recording type: "iq" or "audio"
    double sample_rate = 0;     ///< Sample rate in Hz
    int call_number = 0;        ///< For squelch mode: transmission number

    FilenameParams() : timestamp(QDateTime::currentDateTime()) {}
};

/**
 * @brief Filename template parser for recording files.
 *
 * Supported variables:
 *   {date}       - Date as YYYYMMDD
 *   {time}       - Time as HHMMSS
 *   {datetime}   - Combined as YYYYMMDD_HHMMSS
 *   {Y}          - Year as YYYY (e.g., 2025)
 *   {m}          - Month as MM (e.g., 12)
 *   {d}          - Day as DD (e.g., 23)
 *   {H}          - Hour as HH 24-hour (e.g., 14)
 *   {M}          - Minute as MM (e.g., 30)
 *   {S}          - Second as SS (e.g., 45)
 *   {freq}       - Frequency in Hz (e.g., 145500000)
 *   {freq_mhz}   - Frequency in MHz with 3 decimals (e.g., 145.500)
 *   {freq_khz}   - Frequency in kHz with 1 decimal (e.g., 145500.0)
 *   {mode}       - Demodulation mode (NFM, AM, USB, etc.)
 *   {tuner}      - Tuner ID number
 *   {tuner_name} - Tuner display name (sanitized for filename)
 *   {channel}    - Alias for {tuner_name}
 *   {type}       - Recording type: "iq" or "audio"
 *   {srate}      - Sample rate in Hz
 *   {srate_k}    - Sample rate in kHz
 *   {call}       - Call/transmission number (for squelch mode)
 *
 * Subdirectory support:
 *   Use / in template to create subdirectories automatically.
 *   Example: "{Y}/{m}/{d}/{channel}_{freq_mhz}MHz"
 *   Creates: 2025/12/23/Channel_1_145.500MHz.wav
 */
class FilenameTemplate {
public:
    /**
     * @brief Expand a template string with the given parameters.
     * @param tmpl Template string with {variable} placeholders
     * @param params Parameters to substitute
     * @return Expanded filename (without extension)
     */
    static QString expand(const QString& tmpl, const FilenameParams& params);

    /**
     * @brief Build a full file path with folder, filename, and extension.
     * @param folder Base recording folder
     * @param tmpl Filename template
     * @param params Parameters to substitute
     * @param extension File extension (e.g., ".wav", ".sigmf-data")
     * @return Full file path
     */
    static QString buildPath(const QString& folder,
                             const QString& tmpl,
                             const FilenameParams& params,
                             const QString& extension);

    /**
     * @brief Sanitize a string for use in a filename.
     * @param str String to sanitize
     * @return Sanitized string safe for filenames
     */
    static QString sanitize(const QString& str);

    /**
     * @brief Get a preview of the expanded template.
     * @param tmpl Template string
     * @return Preview with example values
     */
    static QString preview(const QString& tmpl);

    /**
     * @brief Ensure the recording folder exists, create if needed.
     * @param folder Folder path
     * @return True if folder exists or was created
     */
    static bool ensureFolder(const QString& folder);

    /**
     * @brief Get a unique filename if the target already exists.
     * @param basePath Full path without extension
     * @param extension File extension
     * @return Unique path with _001, _002, etc. suffix if needed
     */
    static QString makeUnique(const QString& basePath, const QString& extension);
};

// ============================================================================
// Implementation (header-only for simplicity)
// ============================================================================

inline QString FilenameTemplate::expand(const QString& tmpl, const FilenameParams& params)
{
    QString result = tmpl;

    // Date/time - combined formats
    result.replace("{date}", params.timestamp.toString("yyyyMMdd"));
    result.replace("{time}", params.timestamp.toString("HHmmss"));
    result.replace("{datetime}", params.timestamp.toString("yyyyMMdd_HHmmss"));

    // Date/time - granular components for folder structures
    result.replace("{Y}", params.timestamp.toString("yyyy"));
    result.replace("{m}", params.timestamp.toString("MM"));
    result.replace("{d}", params.timestamp.toString("dd"));
    result.replace("{H}", params.timestamp.toString("HH"));
    result.replace("{M}", params.timestamp.toString("mm"));
    result.replace("{S}", params.timestamp.toString("ss"));

    // Frequency
    result.replace("{freq}", QString::number(static_cast<qint64>(params.frequency_hz)));
    result.replace("{freq_mhz}", QString::number(params.frequency_hz / 1e6, 'f', 3));
    result.replace("{freq_khz}", QString::number(params.frequency_hz / 1e3, 'f', 1));

    // Mode and type
    result.replace("{mode}", sanitize(params.mode));
    result.replace("{type}", params.type);

    // Tuner
    result.replace("{tuner}", QString::number(params.tuner_id));
    result.replace("{tuner_name}", sanitize(params.tuner_name));
    result.replace("{channel}", sanitize(params.tuner_name));  // Alias

    // Sample rate
    result.replace("{srate}", QString::number(static_cast<qint64>(params.sample_rate)));
    result.replace("{srate_k}", QString::number(params.sample_rate / 1e3, 'f', 0));

    // Call number
    result.replace("{call}", QString::number(params.call_number));

    return result;
}

inline QString FilenameTemplate::buildPath(const QString& folder,
                                           const QString& tmpl,
                                           const FilenameParams& params,
                                           const QString& extension)
{
    QString expanded = expand(tmpl, params);

    // Handle path separators in template (create subdirectories)
    // Normalize backslashes to forward slashes
    expanded.replace("\\", "/");

    QString fullPath;
    if (expanded.contains("/")) {
        // Template contains subdirectories
        // Split into path components
        QStringList parts = expanded.split("/", Qt::SkipEmptyParts);
        QString filename = parts.takeLast();  // Last part is the filename

        // Build subdirectory path
        QString subdir = parts.join("/");
        QString fullFolder = QDir(folder).filePath(subdir);

        // Ensure subdirectory exists
        ensureFolder(fullFolder);

        fullPath = QDir(fullFolder).filePath(filename + extension);
    } else {
        // No subdirectories, simple case
        fullPath = QDir(folder).filePath(expanded + extension);
    }

    return fullPath;
}

inline QString FilenameTemplate::sanitize(const QString& str)
{
    QString result = str;
    // Replace problematic characters with underscore
    result.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
    // Replace spaces with underscore
    result.replace(" ", "_");
    // Remove leading/trailing underscores
    result = result.trimmed();
    while (result.startsWith("_")) result.remove(0, 1);
    while (result.endsWith("_")) result.chop(1);
    return result;
}

inline QString FilenameTemplate::preview(const QString& tmpl)
{
    FilenameParams params;
    params.timestamp = QDateTime::currentDateTime();
    params.frequency_hz = 145500000;
    params.mode = "NFM";
    params.tuner_id = 0;
    params.tuner_name = "Channel 1";
    params.type = "audio";
    params.sample_rate = 48000;
    params.call_number = 1;

    QString expanded = expand(tmpl, params);

    // Show with ellipsis prefix if template has subdirectories
    if (expanded.contains("/")) {
        return ".../" + expanded + ".wav";
    }
    return expanded + ".wav";
}

inline bool FilenameTemplate::ensureFolder(const QString& folder)
{
    QDir dir(folder);
    if (!dir.exists()) {
        return dir.mkpath(".");
    }
    return true;
}

inline QString FilenameTemplate::makeUnique(const QString& basePath, const QString& extension)
{
    QString path = basePath + extension;
    if (!QFile::exists(path)) {
        return path;
    }

    // File exists, add suffix
    for (int i = 1; i < 10000; i++) {
        path = QString("%1_%2%3").arg(basePath).arg(i, 3, 10, QChar('0')).arg(extension);
        if (!QFile::exists(path)) {
            return path;
        }
    }

    // Fallback: use timestamp in ms
    path = QString("%1_%2%3").arg(basePath)
               .arg(QDateTime::currentMSecsSinceEpoch())
               .arg(extension);
    return path;
}

#endif // FILENAME_TEMPLATE_H
