#include "QJFunctionRegistry.h"

namespace QC
{
    namespace JFunc
    {
        Registry &Registry::instance()
        {
            static Registry g;
            return g;
        }

        const Function *Registry::find(const char *name, QC::u32 version) const
        {
            if (!name)
                return nullptr;
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                const Entry &e = m_entries[i];
                if (e.fn.version == version && QC::String::strcmp(e.fn.name.c_str(), name) == 0)
                    return &e.fn;
            }
            return nullptr;
        }

        bool Registry::registerFunction(Function &&fn, const char *jsonPath)
        {
            // Reject duplicates (exact name+version)
            if (find(fn.name.c_str(), fn.version))
                return false;

            Entry e;
            e.fn = static_cast<Function &&>(fn);
            e.jsonPath = QC::String(jsonPath ? jsonPath : "");
            m_entries.push_back(static_cast<Entry &&>(e));
            return true;
        }

    } // namespace JFunc
} // namespace QC
