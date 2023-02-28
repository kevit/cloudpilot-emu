#include "FSFixture.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

#include "CardImage.h"
#include "CardVolume.h"
#include "fatfs/diskio.h"
#include "fatfs/ff.h"

using namespace std;

namespace {
    constexpr uint8_t RLE_COMPRESSED_IMAGE[] = {
        0xff, 0x00, 0xbe, 0x03, 0x80, 0x00, 0x02, 0x00, 0x01, 0x00, 0x20, 0x7b, 0x01, 0xff, 0x00,
        0x03, 0xdf, 0x1e, 0xff, 0x00, 0x32, 0x55, 0xaa, 0xeb, 0x3c, 0x90, 0x6d, 0x6b, 0x66, 0x73,
        0x2e, 0x66, 0x61, 0x74, 0x00, 0x02, 0x10, 0x0b, 0x00, 0x02, 0x00, 0x02, 0xdf, 0x1e, 0xf8,
        0x02, 0x00, 0x20, 0x00, 0x02, 0x00, 0x01, 0xff, 0x00, 0x07, 0x80, 0x00, 0x29, 0x10, 0x1e,
        0x11, 0xbb, 0x43, 0x41, 0x52, 0x44, 0xff, 0x20, 0x07, 0x46, 0x41, 0x54, 0x31, 0x32, 0xff,
        0x20, 0x03, 0x0e, 0x1f, 0xbe, 0x5b, 0x7c, 0xac, 0x22, 0xc0, 0x74, 0x0b, 0x56, 0xb4, 0x0e,
        0xbb, 0x07, 0x00, 0xcd, 0x10, 0x5e, 0xeb, 0xf0, 0x32, 0xe4, 0xcd, 0x16, 0xcd, 0x19, 0xeb,
        0xfe, 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x61, 0x20,
        0x62, 0xff, 0x6f, 0x02, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x20, 0x64, 0x69, 0x73, 0x6b, 0x2e,
        0xff, 0x20, 0x02, 0x50, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x20, 0x69, 0x6e, 0x73, 0x65, 0x72,
        0x74, 0x20, 0x61, 0x20, 0x62, 0xff, 0x6f, 0x02, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x20, 0x66,
        0x6c, 0x6f, 0xff, 0x70, 0x02, 0x79, 0x20, 0x61, 0x6e, 0x64, 0x0d, 0x0a, 0x70, 0x72, 0x65,
        0xff, 0x73, 0x02, 0x20, 0x61, 0x6e, 0x79, 0x20, 0x6b, 0x65, 0x79, 0x20, 0x74, 0x6f, 0x20,
        0x74, 0x72, 0x79, 0x20, 0x61, 0x67, 0x61, 0x69, 0x6e, 0x20, 0xff, 0x2e, 0x03, 0x20, 0x0d,
        0x0a, 0xff, 0x00, 0xf9, 0x01, 0x9b, 0x22, 0x11, 0xbb, 0xff, 0x00, 0x02, 0x80, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x20, 0x7b, 0xff, 0x00, 0x04, 0xdf, 0x1e, 0xff, 0x00, 0x32, 0x55, 0xaa,
        0xff, 0x00, 0x80, 0x28, 0xf8, 0xff, 0xff, 0x02, 0xff, 0x00, 0xfd, 0x07, 0xf8, 0xff, 0xff,
        0x02, 0xff, 0x00, 0xfd, 0x07, 0x43, 0x41, 0x52, 0x44, 0xff, 0x20, 0x07, 0x08, 0xff, 0x00,
        0x02, 0xaa, 0xa6, 0x5c, 0x56, 0x5c, 0x56, 0xff, 0x00, 0x02, 0xaa, 0xa6, 0x5c, 0x56, 0xff,
        0x00, 0xe6, 0xbf, 0xf6, 0x01};

    constexpr size_t IMAGE_SIZE = 4 * 1024 * 1024;

    unique_ptr<CardImage> cardImage;
    unique_ptr<CardVolume> cardVolume;

    FATFS fs;

    unique_ptr<uint8_t[]> decompressImage() {
        unique_ptr<uint8_t[]> result = make_unique<uint8_t[]>(IMAGE_SIZE);
        uint8_t* buffer = result.get();

        size_t i = 0;
        size_t j = 0;
        while (j < sizeof(RLE_COMPRESSED_IMAGE)) {
            uint8_t token = RLE_COMPRESSED_IMAGE[j++];

            if (token != 0xff) {
                buffer[i++] = token;
                continue;
            }

            token = RLE_COMPRESSED_IMAGE[j++];

            int repeat = 0;
            for (int k = 0; k < 4; k++) {
                repeat |= (RLE_COMPRESSED_IMAGE[j] & 0x7f) << (7 * k);
                if ((RLE_COMPRESSED_IMAGE[j++] & 0x80) == 0) break;
            }

            memset(buffer + i, token, repeat);
            i += repeat;
        }

        return result;
    }
}  // namespace

namespace FSFixture {}

namespace FSFixture {
    void CreateAndMount() {
        cardImage = make_unique<CardImage>(decompressImage().release(), IMAGE_SIZE >> 9);
        cardVolume = make_unique<CardVolume>(*cardImage);

        register_card_volume(0, cardVolume.get());
        f_mount(&fs, "0:", 1);
    }

    void UnmountAndRelease() {
        f_unmount("0:");
        unregister_card_volume(0);

        cardImage.reset();
        cardVolume.reset();
    }
}  // namespace FSFixture
