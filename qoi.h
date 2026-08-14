#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"

#include <cstdint>
#include <cstring>
#include <iostream>

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0;
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

/**
 * Encode raw RGB or RGBA bytes from standard input as a QOI image on
 * standard output.
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels,
               uint8_t colorspace = 0);

/** Decode a QOI image from standard input to raw RGB/RGBA bytes. */
bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels,
               uint8_t &colorspace);

namespace qoi_detail {

struct Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

inline bool Equal(const Pixel &lhs, const Pixel &rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b &&
           lhs.a == rhs.a;
}

// QOI colour differences are signed 8-bit differences and therefore wrap at
// the ends of the byte range (for example, 0 - 255 is +1).
inline int WrappedDifference(uint8_t value, uint8_t previous) {
    const unsigned difference = static_cast<uint8_t>(value - previous);
    return difference < 128u ? static_cast<int>(difference)
                             : static_cast<int>(difference) - 256;
}

inline void Store(Pixel history[64], const Pixel &pixel) {
    history[QoiColorHash(pixel.r, pixel.g, pixel.b, pixel.a)] = pixel;
}

}  // namespace qoi_detail

bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels,
               uint8_t colorspace) {
    if (width == 0 || height == 0 || (channels != 3 && channels != 4) ||
        colorspace > 1) {
        return false;
    }

    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    QoiWriteU32(width);
    QoiWriteU32(height);
    QoiWriteU8(channels);
    QoiWriteU8(colorspace);

    using qoi_detail::Pixel;
    Pixel history[64] = {};
    Pixel previous = {0u, 0u, 0u, 255u};
    uint8_t run = 0;
    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;

    for (uint64_t i = 0; i < pixel_count; ++i) {
        Pixel pixel;
        pixel.r = QoiReadU8();
        pixel.g = QoiReadU8();
        pixel.b = QoiReadU8();
        pixel.a = channels == 4 ? QoiReadU8() : 255u;
        if (!std::cin) {
            return false;
        }

        if (qoi_detail::Equal(pixel, previous)) {
            ++run;
            if (run == 62 || i + 1 == pixel_count) {
                QoiWriteU8(static_cast<uint8_t>(QOI_OP_RUN_TAG | (run - 1)));
                run = 0;
            }
            continue;
        }

        if (run != 0) {
            QoiWriteU8(static_cast<uint8_t>(QOI_OP_RUN_TAG | (run - 1)));
            run = 0;
        }

        const int index = QoiColorHash(pixel.r, pixel.g, pixel.b, pixel.a);
        if (qoi_detail::Equal(history[index], pixel)) {
            QoiWriteU8(static_cast<uint8_t>(QOI_OP_INDEX_TAG | index));
        } else {
            history[index] = pixel;
            if (pixel.a != previous.a) {
                QoiWriteU8(QOI_OP_RGBA_TAG);
                QoiWriteU8(pixel.r);
                QoiWriteU8(pixel.g);
                QoiWriteU8(pixel.b);
                QoiWriteU8(pixel.a);
            } else {
                const int dr = qoi_detail::WrappedDifference(pixel.r, previous.r);
                const int dg = qoi_detail::WrappedDifference(pixel.g, previous.g);
                const int db = qoi_detail::WrappedDifference(pixel.b, previous.b);

                if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 &&
                    db >= -2 && db <= 1) {
                    QoiWriteU8(static_cast<uint8_t>(QOI_OP_DIFF_TAG |
                               ((dr + 2) << 4) | ((dg + 2) << 2) |
                               (db + 2)));
                } else {
                    const int dr_dg = dr - dg;
                    const int db_dg = db - dg;
                    if (dg >= -32 && dg <= 31 && dr_dg >= -8 &&
                        dr_dg <= 7 && db_dg >= -8 && db_dg <= 7) {
                        QoiWriteU8(static_cast<uint8_t>(QOI_OP_LUMA_TAG |
                                                       (dg + 32)));
                        QoiWriteU8(static_cast<uint8_t>(((dr_dg + 8) << 4) |
                                                       (db_dg + 8)));
                    } else {
                        QoiWriteU8(QOI_OP_RGB_TAG);
                        QoiWriteU8(pixel.r);
                        QoiWriteU8(pixel.g);
                        QoiWriteU8(pixel.b);
                    }
                }
            }
        }
        previous = pixel;
    }

    for (uint8_t byte : QOI_PADDING) {
        QoiWriteU8(byte);
    }
    return static_cast<bool>(std::cout);
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels,
               uint8_t &colorspace) {
    const char c1 = QoiReadChar();
    const char c2 = QoiReadChar();
    const char c3 = QoiReadChar();
    const char c4 = QoiReadChar();
    if (!std::cin || c1 != 'q' || c2 != 'o' || c3 != 'i' || c4 != 'f') {
        return false;
    }

    width = QoiReadU32();
    height = QoiReadU32();
    channels = QoiReadU8();
    colorspace = QoiReadU8();
    if (!std::cin || width == 0 || height == 0 ||
        (channels != 3 && channels != 4) || colorspace > 1) {
        return false;
    }

    using qoi_detail::Pixel;
    Pixel history[64] = {};
    Pixel pixel = {0u, 0u, 0u, 255u};
    uint8_t run = 0;
    const uint64_t pixel_count = static_cast<uint64_t>(width) * height;

    for (uint64_t i = 0; i < pixel_count; ++i) {
        if (run != 0) {
            --run;
        } else {
            const uint8_t first = QoiReadU8();
            if (!std::cin) {
                return false;
            }

            if (first == QOI_OP_RGB_TAG) {
                pixel.r = QoiReadU8();
                pixel.g = QoiReadU8();
                pixel.b = QoiReadU8();
            } else if (first == QOI_OP_RGBA_TAG) {
                pixel.r = QoiReadU8();
                pixel.g = QoiReadU8();
                pixel.b = QoiReadU8();
                pixel.a = QoiReadU8();
            } else {
                switch (first & QOI_MASK_2) {
                    case QOI_OP_INDEX_TAG:
                        pixel = history[first & 0x3fu];
                        break;
                    case QOI_OP_DIFF_TAG:
                        pixel.r = static_cast<uint8_t>(pixel.r +
                                                       ((first >> 4) & 0x03u) - 2);
                        pixel.g = static_cast<uint8_t>(pixel.g +
                                                       ((first >> 2) & 0x03u) - 2);
                        pixel.b = static_cast<uint8_t>(pixel.b +
                                                       (first & 0x03u) - 2);
                        break;
                    case QOI_OP_LUMA_TAG: {
                        const uint8_t second = QoiReadU8();
                        const int dg = (first & 0x3fu) - 32;
                        const int dr_dg = ((second >> 4) & 0x0fu) - 8;
                        const int db_dg = (second & 0x0fu) - 8;
                        pixel.r = static_cast<uint8_t>(pixel.r + dg + dr_dg);
                        pixel.g = static_cast<uint8_t>(pixel.g + dg);
                        pixel.b = static_cast<uint8_t>(pixel.b + dg + db_dg);
                        break;
                    }
                    case QOI_OP_RUN_TAG:
                        run = first & 0x3fu;
                        // The encoded run includes this iteration.  It must not
                        // extend beyond the number of pixels in the header.
                        if (static_cast<uint64_t>(run) > pixel_count - i - 1) {
                            return false;
                        }
                        break;
                }
            }
            if (!std::cin) {
                return false;
            }
        }

        qoi_detail::Store(history, pixel);
        QoiWriteU8(pixel.r);
        QoiWriteU8(pixel.g);
        QoiWriteU8(pixel.b);
        if (channels == 4) {
            QoiWriteU8(pixel.a);
        }
    }

    for (uint8_t expected : QOI_PADDING) {
        const uint8_t actual = QoiReadU8();
        if (!std::cin || actual != expected) {
            return false;
        }
    }
    return static_cast<bool>(std::cout);
}

#endif  // QOI_FORMAT_CODEC_QOI_H_
