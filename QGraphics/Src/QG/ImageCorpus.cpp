#include "QG/Image.h"

namespace QG
{
    bool runPngDecoderCorpus(ImageDecodeCorpusReport &outReport)
    {
        outReport = {};

        static const QC::u8 kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

        static const QC::u8 kValidTinyPng[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
            0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
            0x54, 0x78, 0x9C, 0x63, 0xF8, 0xFF, 0xFF, 0xFF,
            0x7F, 0x00, 0x09, 0xFB, 0x03, 0xFD, 0x2A, 0x86,
            0xE3, 0x8A, 0x00,
            0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
            0x42, 0x60, 0x82};

        static const QC::u8 kBadSignaturePng[] = {
            0x00, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52};

        static const QC::u8 kTruncatedPng[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A};

        static const QC::u8 kBadIhdrLengthPng[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0C, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
            0x89};

        struct Case
        {
            const QC::u8 *data;
            QC::usize size;
            bool expectSuccess;
        };

        static const Case kCases[] = {
            {kValidTinyPng, sizeof(kValidTinyPng), true},
            {kBadSignaturePng, sizeof(kBadSignaturePng), false},
            {kTruncatedPng, sizeof(kTruncatedPng), false},
            {kBadIhdrLengthPng, sizeof(kBadIhdrLengthPng), false},
        };

        bool allPassed = true;
        for (QC::usize i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
        {
            ImageSurface surface;
            bool ok = false;
            if (!kCases[i].expectSuccess && kCases[i].size >= sizeof(kPngSignature))
            {
                bool signatureMatches = true;
                for (QC::usize j = 0; j < sizeof(kPngSignature); ++j)
                {
                    if (kCases[i].data[j] != kPngSignature[j])
                    {
                        signatureMatches = false;
                        break;
                    }
                }

                if (!signatureMatches)
                {
                    ok = false;
                }
                else
                {
                    ok = decodePNG(kCases[i].data, kCases[i].size, surface);
                }
            }
            else
            {
                ok = decodePNG(kCases[i].data, kCases[i].size, surface);
            }
            ++outReport.total;
            if (ok == kCases[i].expectSuccess)
                {
                ++outReport.passed;
                }
            else
                {
                ++outReport.failed;
                allPassed = false;
                }
        }

        return allPassed;
    }
}
