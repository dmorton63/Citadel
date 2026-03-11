#pragma once

// QJFunctionRegistry - validated JSON function registry (MVP)
// Namespace: QC::JFunc

#include "QJFunction.h"

namespace QC
{
    namespace JFunc
    {
        class Registry
        {
        public:
            static Registry &instance();

            bool registerFunction(Function &&fn, const char *jsonPath);
            const Function *find(const char *name, QC::u32 version) const;

            QC::usize count() const { return m_entries.size(); }

        private:
            Registry() = default;

            struct Entry
            {
                Function fn;
                QC::String jsonPath;
            };

            QC::Vector<Entry> m_entries;
        };
    } // namespace JFunc
} // namespace QC
