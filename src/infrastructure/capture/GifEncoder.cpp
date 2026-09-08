#include "GifEncoder.h"

#include <QFile>
#include <QHash>

#include <algorithm>
#include <array>
#include <vector>

namespace qaflow {

namespace {

// ---- Cuantización -----------------------------------------------------------------------------
// Histograma de 15 bits (5 bits por canal, 32768 celdas): suficiente para pantallas y muy rápido.
// Si hay 256 colores distintos o menos, la paleta es exacta; si no, median cut sobre las celdas.

struct Box {
    int rMin, rMax, gMin, gMax, bMin, bMax;
    long count;
    int volume() const { return (rMax - rMin + 1) * (gMax - gMin + 1) * (bMax - bMin + 1); }
};

constexpr int kBits = 5;
constexpr int kLevels = 1 << kBits;
constexpr int kCells = kLevels * kLevels * kLevels;

inline int cellOf(QRgb c) {
    return ((qRed(c) >> (8 - kBits)) << (2 * kBits)) | ((qGreen(c) >> (8 - kBits)) << kBits) | (qBlue(c) >> (8 - kBits));
}

void medianCut(const std::vector<long>& hist, int maxColors, std::vector<Box>* boxes) {
    Box all{kLevels - 1, 0, kLevels - 1, 0, kLevels - 1, 0, 0};
    for (int r = 0; r < kLevels; ++r)
        for (int g = 0; g < kLevels; ++g)
            for (int b = 0; b < kLevels; ++b) {
                const long n = hist[(r << (2 * kBits)) | (g << kBits) | b];
                if (!n) continue;
                all.count += n;
                all.rMin = std::min(all.rMin, r); all.rMax = std::max(all.rMax, r);
                all.gMin = std::min(all.gMin, g); all.gMax = std::max(all.gMax, g);
                all.bMin = std::min(all.bMin, b); all.bMax = std::max(all.bMax, b);
            }
    boxes->push_back(all);
    while (static_cast<int>(boxes->size()) < maxColors) {
        // Divide la caja con más píxeles (primera mitad) o con más píxeles × volumen (segunda mitad,
        // como Heckbert: evita que una caja grande y poco poblada quede sin partir). Las cajas de
        // un solo color no se parten.
        const bool byPopulation = static_cast<int>(boxes->size()) < maxColors / 2;
        auto score = [byPopulation](const Box& b) { return byPopulation ? static_cast<double>(b.count) : static_cast<double>(b.count) * b.volume(); };
        int best = -1;
        for (size_t i = 0; i < boxes->size(); ++i)
            if ((*boxes)[i].volume() > 1 && (*boxes)[i].count > 0 && (best < 0 || score((*boxes)[i]) > score((*boxes)[best]))) best = static_cast<int>(i);
        if (best < 0) break;
        Box box = (*boxes)[best];
        const int rl = box.rMax - box.rMin, gl = box.gMax - box.gMin, bl = box.bMax - box.bMin;
        const int axis = (rl >= gl && rl >= bl) ? 0 : (gl >= bl ? 1 : 2);
        // Histograma marginal a lo largo del eje elegido y corte por la mediana de píxeles.
        std::array<long, kLevels> marginal{};
        for (int r = box.rMin; r <= box.rMax; ++r)
            for (int g = box.gMin; g <= box.gMax; ++g)
                for (int b = box.bMin; b <= box.bMax; ++b)
                    marginal[axis == 0 ? r : axis == 1 ? g : b] += hist[(r << (2 * kBits)) | (g << kBits) | b];
        const int lo = axis == 0 ? box.rMin : axis == 1 ? box.gMin : box.bMin;
        const int hi = axis == 0 ? box.rMax : axis == 1 ? box.gMax : box.bMax;
        long acc = 0;
        int cut = lo;
        for (int v = lo; v < hi; ++v) {
            acc += marginal[v];
            cut = v;
            if (acc * 2 >= box.count) break;
        }
        Box a = box, b = box;
        long countA = 0;
        for (int v = lo; v <= cut; ++v) countA += marginal[v];
        if (axis == 0) { a.rMax = cut; b.rMin = cut + 1; }
        else if (axis == 1) { a.gMax = cut; b.gMin = cut + 1; }
        else { a.bMax = cut; b.bMin = cut + 1; }
        a.count = countA;
        b.count = box.count - countA;
        (*boxes)[best] = a;
        boxes->push_back(b);
    }
}

// ---- LZW -------------------------------------------------------------------------------------

class BitWriter {
public:
    explicit BitWriter(QByteArray* out) : m_out(out) {}
    void write(uint32_t code, int bits) {
        m_acc |= static_cast<uint64_t>(code) << m_nbits;
        m_nbits += bits;
        while (m_nbits >= 8) {
            m_block.append(static_cast<char>(m_acc & 0xff));
            m_acc >>= 8;
            m_nbits -= 8;
            if (m_block.size() == 255) flushBlock();
        }
    }
    void finish() {
        if (m_nbits > 0) { m_block.append(static_cast<char>(m_acc & 0xff)); m_acc = 0; m_nbits = 0; }
        if (!m_block.isEmpty()) flushBlock();
        m_out->append('\0');   // fin de los sub-bloques
    }

private:
    void flushBlock() {
        m_out->append(static_cast<char>(m_block.size()));
        m_out->append(m_block);
        m_block.clear();
    }
    QByteArray* m_out;
    QByteArray m_block;
    uint64_t m_acc = 0;
    int m_nbits = 0;
};

void putU16(QByteArray* out, int v) {
    out->append(static_cast<char>(v & 0xff));
    out->append(static_cast<char>((v >> 8) & 0xff));
}

} // namespace

// ---- GifEncoder ------------------------------------------------------------------------------

GifEncoder::GifEncoder() = default;
GifEncoder::~GifEncoder() { if (m_open) finish(); }

void GifEncoder::quantize(const QImage& image, QVector<QRgb>* palette, QByteArray* indices) {
    const QImage img = image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32
                           ? image : image.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    indices->resize(w * h);
    palette->clear();

    // 1) Colores exactos mientras quepan en 256.
    QHash<QRgb, int> exact;
    bool tooMany = false;
    for (int y = 0; y < h && !tooMany; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = row[x] | 0xff000000;
            auto it = exact.find(c);
            if (it == exact.end()) {
                if (exact.size() == 256) { tooMany = true; break; }
                it = exact.insert(c, exact.size());
                palette->append(c);
            }
            (*indices)[y * w + x] = static_cast<char>(it.value());
        }
    }
    if (!tooMany) return;

    // 2) Median cut sobre el histograma de 15 bits.
    std::vector<long> hist(kCells, 0);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) ++hist[cellOf(row[x])];
    }
    std::vector<Box> boxes;
    medianCut(hist, 256, &boxes);

    // Color representativo de cada caja (media ponderada) y tabla celda → índice.
    palette->clear();
    std::vector<uint8_t> cellIndex(kCells, 0);
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& bx = boxes[i];
        long sr = 0, sg = 0, sb = 0, n = 0;
        for (int r = bx.rMin; r <= bx.rMax; ++r)
            for (int g = bx.gMin; g <= bx.gMax; ++g)
                for (int b = bx.bMin; b <= bx.bMax; ++b) {
                    const int cell = (r << (2 * kBits)) | (g << kBits) | b;
                    const long c = hist[cell];
                    cellIndex[cell] = static_cast<uint8_t>(i);
                    if (!c) continue;
                    n += c;
                    // centro de la celda en 8 bits
                    sr += c * ((r << (8 - kBits)) | (1 << (7 - kBits)));
                    sg += c * ((g << (8 - kBits)) | (1 << (7 - kBits)));
                    sb += c * ((b << (8 - kBits)) | (1 << (7 - kBits)));
                }
        palette->append(n ? qRgb(static_cast<int>(sr / n), static_cast<int>(sg / n), static_cast<int>(sb / n)) : qRgb(0, 0, 0));
    }
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) (*indices)[y * w + x] = static_cast<char>(cellIndex[cellOf(row[x])]);
    }
}

bool GifEncoder::open(const QString& path, const QSize& size, int loop) {
    if (m_open) finish();
    m_error.clear();
    m_frames = 0;
    m_size = QSize(std::clamp(size.width(), 1, 65535), std::clamp(size.height(), 1, 65535));
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = file->errorString();
        return false;
    }
    m_device = std::move(file);
    m_buffer.clear();
    m_buffer.append("GIF89a");
    putU16(&m_buffer, m_size.width());
    putU16(&m_buffer, m_size.height());
    m_buffer.append(static_cast<char>(0x70));   // sin tabla global; 8 bits por canal
    m_buffer.append('\0');                      // color de fondo
    m_buffer.append('\0');                      // relación de aspecto
    // Extensión NETSCAPE2.0: repeticiones (0 = infinito)
    m_buffer.append("\x21\xFF\x0B", 3);
    m_buffer.append("NETSCAPE2.0");
    m_buffer.append("\x03\x01", 2);
    putU16(&m_buffer, std::clamp(loop, 0, 65535));
    m_buffer.append('\0');
    m_open = flush();
    return m_open;
}

bool GifEncoder::addFrame(const QImage& frame, int delayMs) {
    if (!m_open) return false;
    QImage img = frame.size() == m_size ? frame : frame.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QVector<QRgb> palette;
    QByteArray indices;
    quantize(img, &palette, &indices);

    // Tamaño de la tabla local: potencia de dos ≥ colores, mínimo 4 (LZW exige minCodeSize ≥ 2).
    int bits = 2;
    while ((1 << bits) < palette.size()) ++bits;
    const int tableSize = 1 << bits;

    // Graphic Control Extension: retardo en centésimas, sin transparencia, disposición "no borrar".
    m_buffer.append("\x21\xF9\x04", 3);
    m_buffer.append(static_cast<char>(0x04));
    putU16(&m_buffer, std::clamp((delayMs + 5) / 10, 2, 65535));
    m_buffer.append('\0');
    m_buffer.append('\0');
    // Image Descriptor con tabla de color local.
    m_buffer.append(static_cast<char>(0x2C));
    putU16(&m_buffer, 0);
    putU16(&m_buffer, 0);
    putU16(&m_buffer, m_size.width());
    putU16(&m_buffer, m_size.height());
    m_buffer.append(static_cast<char>(0x80 | (bits - 1)));
    for (int i = 0; i < tableSize; ++i) {
        const QRgb c = i < palette.size() ? palette[i] : 0;
        m_buffer.append(static_cast<char>(qRed(c)));
        m_buffer.append(static_cast<char>(qGreen(c)));
        m_buffer.append(static_cast<char>(qBlue(c)));
    }
    writeLzw(indices, bits);
    ++m_frames;
    return flush();
}

/// LZW variable (GIF): tabla hash abierta de 8192 entradas, código CLEAR al llenar los 4096 códigos.
void GifEncoder::writeLzw(const QByteArray& indices, int minCodeSize) {
    m_buffer.append(static_cast<char>(minCodeSize));
    BitWriter bw(&m_buffer);
    const int clearCode = 1 << minCodeSize;
    const int endCode = clearCode + 1;
    constexpr int kHash = 1 << 13;
    std::vector<int32_t> keys(kHash, -1);
    std::vector<int16_t> codes(kHash, 0);
    int codeSize = minCodeSize + 1;
    int next = endCode + 1;
    auto reset = [&]() {
        std::fill(keys.begin(), keys.end(), -1);
        codeSize = minCodeSize + 1;
        next = endCode + 1;
    };
    bw.write(clearCode, codeSize);
    if (indices.isEmpty()) { bw.write(endCode, codeSize); bw.finish(); return; }
    int prefix = static_cast<uint8_t>(indices[0]);
    for (int i = 1; i < indices.size(); ++i) {
        const int k = static_cast<uint8_t>(indices[i]);
        const int32_t key = (prefix << 8) | k;
        int h = static_cast<int>((static_cast<uint32_t>(key) * 2654435761u) >> 19) & (kHash - 1);
        bool found = false;
        while (keys[h] != -1) {
            if (keys[h] == key) { prefix = codes[h]; found = true; break; }
            h = (h + 1) & (kHash - 1);
        }
        if (found) continue;
        bw.write(prefix, codeSize);
        // Como giflib: el tamaño de código crece justo después de emitir el código anterior a la
        // entrada 2^n (el decodificador va una entrada por detrás), y al llegar a 4095 se reinicia.
        if (next >= (1 << codeSize) && codeSize < 12) ++codeSize;
        if (next >= 4095) {
            bw.write(clearCode, codeSize);
            reset();
        } else {
            keys[h] = key;
            codes[h] = static_cast<int16_t>(next);
            ++next;
        }
        prefix = k;
    }
    bw.write(prefix, codeSize);
    bw.write(endCode, codeSize);
    bw.finish();
}

bool GifEncoder::flush() {
    if (!m_device) return false;
    if (m_buffer.isEmpty()) return true;
    const qint64 n = m_device->write(m_buffer);
    const bool ok = n == m_buffer.size();
    if (!ok) m_error = m_device->errorString();
    m_buffer.clear();
    return ok;
}

bool GifEncoder::finish() {
    if (!m_open) return false;
    m_buffer.append(static_cast<char>(0x3B));
    const bool ok = flush();
    m_device->close();
    m_device.reset();
    m_open = false;
    return ok;
}

} // namespace qaflow
