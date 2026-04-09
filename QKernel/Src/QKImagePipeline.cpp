#include "QKImagePipeline.h"

#include "QCString.h"

namespace QK
{

    ImagePipelineDispatcher &ImagePipelineDispatcher::instance()
    {
        static ImagePipelineDispatcher d;
        return d;
    }

    QC::Status ImagePipelineDispatcher::registerModule(IMGModule *module)
    {
        if (!module || !module->name() || !module->name()[0])
            return QC::Status::InvalidParam;

        for (QC::usize i = 0; i < kMaxModules; ++i)
        {
            if (m_modules[i] == module)
                return QC::Status::Success;
            if (!m_modules[i])
            {
                m_modules[i] = module;
                return QC::Status::Success;
            }
        }
        return QC::Status::OutOfMemory;
    }

    QC::Status ImagePipelineDispatcher::dispatch(const ImageFormatDescriptor &desc, ImageContext &ctx)
    {
        if (!desc.module[0])
            return QC::Status::InvalidParam;

        IMGModule *target = nullptr;
        for (QC::usize i = 0; i < kMaxModules; ++i)
        {
            if (!m_modules[i])
                continue;
            if (QC::String::strcmp(m_modules[i]->name(), desc.module) == 0)
            {
                target = m_modules[i];
                break;
            }
        }

        if (!target)
            return QC::Status::NotFound;

        for (QC::usize i = 0; i < desc.verbs.size(); ++i)
        {
            const ImagePipelineVerb &verb = desc.verbs[i];
            if (!target->supportsVerb(verb.name))
                return QC::Status::NotSupported;
            QC::Status st = target->runVerb(verb, ctx);
            if (st != QC::Status::Success)
                return st;
        }

        return QC::Status::Success;
    }

} // namespace QK
