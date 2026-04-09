#pragma once

#include "QCTypes.h"
#include "QCVector.h"

namespace QK
{

    struct ImageContext
    {
        const QC::u8 *input = nullptr;
        QC::usize inputSize = 0;
        QC::u32 width = 0;
        QC::u32 height = 0;
        QC::u32 channels = 0;
        QC::Vector<QC::u8> pixels;
    };

    struct ImagePipelineVerb
    {
        char name[24] = {0};
        char arg[48] = {0};
    };

    struct ImageFormatDescriptor
    {
        char magic[8] = {0};
        char module[32] = {0};
        QC::u32 schemaVersion = 1;
        QC::Vector<ImagePipelineVerb> verbs;
    };

    class IMGModule
    {
    public:
        virtual ~IMGModule() = default;
        virtual const char *name() const = 0;
        virtual bool supportsVerb(const char *verb) const = 0;
        virtual QC::Status runVerb(const ImagePipelineVerb &verb, ImageContext &ctx) = 0;
    };

    class ImagePipelineDispatcher
    {
    public:
        static ImagePipelineDispatcher &instance();

        QC::Status registerModule(IMGModule *module);
        QC::Status dispatch(const ImageFormatDescriptor &desc, ImageContext &ctx);

    private:
        ImagePipelineDispatcher() = default;
        static constexpr QC::usize kMaxModules = 16;
        IMGModule *m_modules[kMaxModules] = {nullptr};
    };

} // namespace QK
