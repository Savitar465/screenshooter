#include "ZipWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace qaflow {

namespace {

constexpr quint32 kLocalHeader = 0x04034b50;
constexpr quint32 kCentralHeader = 0x02014b50;
constexpr quint32 kEndOfCentral = 0x06054b50;
constexpr quint16 kVersion = 20;        // 2.0: lo que pide una entrada sin comprimir
constexpr quint16 kUtf8Names = 0x0800;  // los nombres de las partes van en UTF-8
constexpr quint16 kStored = 0;          // método «store»: sin comprimir

void appendLE(QByteArray& out, quint16 value) {
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void appendLE(QByteArray& out, quint32 value) {
    for (int i = 0; i < 4; ++i) out.append(char((value >> (8 * i)) & 0xff));
}

} // namespace

quint32 ZipWriter::crc32(const QByteArray& data) {
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    quint32 crc = 0xffffffffu;
    for (const char byte : data) crc = table[(crc ^ quint8(byte)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

ZipWriter::ZipWriter(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file = std::make_unique<QSaveFile>(path);
    if (!m_file->open(QIODevice::WriteOnly)) {
        m_error = m_file->errorString();
        m_file.reset();
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QDate date = now.date();
    const QTime time = now.time();
    m_dosDate = quint16(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
    m_dosTime = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
}

ZipWriter::~ZipWriter() = default;

bool ZipWriter::isOpen() const { return m_file != nullptr && !m_closed; }

bool ZipWriter::write(const QByteArray& bytes) {
    if (m_file->write(bytes) == bytes.size()) {
        m_offset += quint32(bytes.size());
        return true;
    }
    m_error = m_file->errorString();
    return false;
}

bool ZipWriter::add(const QString& name, const QByteArray& data) {
    if (!isOpen()) {
        if (m_error.isEmpty()) m_error = QCoreApplication::translate("infrastructure", "el fichero ya está cerrado");
        return false;
    }
    Entry entry;
    entry.name = name.toUtf8();
    entry.crc = crc32(data);
    entry.size = quint32(data.size());
    entry.offset = m_offset;

    QByteArray header;
    appendLE(header, kLocalHeader);
    appendLE(header, kVersion);
    appendLE(header, kUtf8Names);
    appendLE(header, kStored);
    appendLE(header, m_dosTime);
    appendLE(header, m_dosDate);
    appendLE(header, entry.crc);
    appendLE(header, entry.size);   // comprimido y sin comprimir son lo mismo
    appendLE(header, entry.size);
    appendLE(header, quint16(entry.name.size()));
    appendLE(header, quint16(0));   // sin campo «extra»
    header.append(entry.name);
    if (!write(header) || !write(data)) return false;
    m_entries << entry;
    return true;
}

bool ZipWriter::close() {
    if (m_closed) return m_error.isEmpty();
    if (!m_file) return false;
    m_closed = true;

    const quint32 directoryStart = m_offset;
    QByteArray directory;
    for (const auto& entry : m_entries) {
        appendLE(directory, kCentralHeader);
        appendLE(directory, kVersion);   // creado por
        appendLE(directory, kVersion);   // necesario para extraer
        appendLE(directory, kUtf8Names);
        appendLE(directory, kStored);
        appendLE(directory, m_dosTime);
        appendLE(directory, m_dosDate);
        appendLE(directory, entry.crc);
        appendLE(directory, entry.size);
        appendLE(directory, entry.size);
        appendLE(directory, quint16(entry.name.size()));
        appendLE(directory, quint16(0));   // extra
        appendLE(directory, quint16(0));   // comentario
        appendLE(directory, quint16(0));   // disco
        appendLE(directory, quint16(0));   // atributos internos
        appendLE(directory, quint32(0));   // atributos externos
        appendLE(directory, entry.offset);
        directory.append(entry.name);
    }
    if (!write(directory)) {
        m_file->cancelWriting();
        return false;
    }

    QByteArray end;
    appendLE(end, kEndOfCentral);
    appendLE(end, quint16(0));   // disco
    appendLE(end, quint16(0));   // disco del directorio
    appendLE(end, quint16(m_entries.size()));
    appendLE(end, quint16(m_entries.size()));
    appendLE(end, quint32(directory.size()));
    appendLE(end, directoryStart);
    appendLE(end, quint16(0));   // comentario
    if (!write(end)) {
        m_file->cancelWriting();
        return false;
    }
    if (m_file->commit()) return true;
    m_error = m_file->errorString();
    return false;
}

} // namespace qaflow
