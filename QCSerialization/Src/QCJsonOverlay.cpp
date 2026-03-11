#include "QCJsonOverlay.h"

namespace QC
{
    namespace JSON
    {
        static bool isNameChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_');
        }

        void ParamMap::set(const char *key, const char *value)
        {
            if (!key)
                return;

            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                if (m_entries[i].key && QC::String::strcmp(m_entries[i].key, key) == 0)
                {
                    m_entries[i].value = value;
                    return;
                }
            }

            ParamEntry entry;
            entry.key = key;
            entry.value = value;
            m_entries.push_back(entry);
        }

        const char *ParamMap::get(const char *key) const
        {
            if (!key)
                return nullptr;

            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                if (m_entries[i].key && QC::String::strcmp(m_entries[i].key, key) == 0)
                {
                    return m_entries[i].value;
                }
            }
            return nullptr;
        }

        void Overlay::addLayer(const Value *value)
        {
            if (!value)
                return;
            m_layers.push_back(value);
        }

        const Value *Overlay::value() const
        {
            if (m_layers.size() == 0)
                return nullptr;
            return m_layers[0];
        }

        Overlay Overlay::child(const char *key) const
        {
            Overlay result;
            if (!key)
                return result;

            // Gather candidate values for key across layers.
            // If the highest-priority found value is non-object, it overrides everything.
            // If the highest-priority found value is object, we merge object layers.
            const Value *highestFound = nullptr;
            bool highestIsObject = false;

            // First pass: find the highest-priority value.
            for (QC::usize i = 0; i < m_layers.size(); ++i)
            {
                const Value *layer = m_layers[i];
                if (!layer || !layer->isObject())
                    continue;

                const Value *v = layer->find(key);
                if (!v)
                    continue;

                highestFound = v;
                highestIsObject = v->isObject();
                break;
            }

            if (!highestFound)
                return result;

            // If highest is not an object, it fully overrides.
            if (!highestIsObject)
            {
                result.addLayer(highestFound);
                return result;
            }

            // Highest is an object. Merge any object values for this key across layers.
            for (QC::usize i = 0; i < m_layers.size(); ++i)
            {
                const Value *layer = m_layers[i];
                if (!layer || !layer->isObject())
                    continue;

                const Value *v = layer->find(key);
                if (!v)
                    continue;

                if (v->isObject())
                {
                    result.addLayer(v);
                }
                else
                {
                    // A non-object at a lower priority does not participate in object merge.
                    // We ignore it to preserve deep-merge behavior.
                }
            }

            return result;
        }

        bool Overlay::isNull() const
        {
            const Value *v = value();
            return !v || v->isNull();
        }
        bool Overlay::isBool() const
        {
            const Value *v = value();
            return v && v->isBool();
        }
        bool Overlay::isNumber() const
        {
            const Value *v = value();
            return v && v->isNumber();
        }
        bool Overlay::isString() const
        {
            const Value *v = value();
            return v && v->isString();
        }
        bool Overlay::isObject() const
        {
            const Value *v = value();
            return v && v->isObject();
        }
        bool Overlay::isArray() const
        {
            const Value *v = value();
            return v && v->isArray();
        }

        bool Overlay::asBool(bool defaultValue) const
        {
            const Value *v = value();
            return v ? v->asBool(defaultValue) : defaultValue;
        }

        double Overlay::asNumber(double defaultValue) const
        {
            const Value *v = value();
            return v ? v->asNumber(defaultValue) : defaultValue;
        }

        const char *Overlay::asString(const char *defaultValue) const
        {
            const Value *v = value();
            return v ? v->asString(defaultValue) : defaultValue;
        }

        const Object *Overlay::asObject() const
        {
            const Value *v = value();
            return (v && v->isObject()) ? v->asObject() : nullptr;
        }

        const Array *Overlay::asArray() const
        {
            const Value *v = value();
            return (v && v->isArray()) ? v->asArray() : nullptr;
        }

        const Value *Overlay::findResolved(const char *key) const
        {
            const Value *v = value();
            if (!v || !v->isObject() || !key)
                return nullptr;
            return v->find(key);
        }

        void Overlay::collectParams(ParamMap &out) const
        {
            out.clear();

            // We want highest priority to win.
            // Apply low priority first, then overwrite with higher.
            for (QC::isize i = static_cast<QC::isize>(m_layers.size()) - 1; i >= 0; --i)
            {
                const Value *layer = m_layers[static_cast<QC::usize>(i)];
                if (!layer || !layer->isObject())
                    continue;

                const Value *params = layer->find("params");
                if (!params || !params->isObject())
                    continue;

                const Object *obj = params->asObject();
                for (QC::usize e = 0; e < obj->size(); ++e)
                {
                    const ObjectEntry &entry = (*obj)[e];
                    if (!entry.key || !entry.value)
                        continue;
                    if (!entry.value->isString())
                        continue;

                    out.set(entry.key, entry.value->asString(""));
                }
            }
        }

        const char *Overlay::asTemplatedString(const ParamMap &params, QC::Vector<char> &outBuffer, const char *defaultValue) const
        {
            const Value *v = value();
            if (!v || !v->isString())
                return defaultValue;
            return expandTemplateString(v->asString(""), params, outBuffer);
        }

        const char *expandTemplateString(const char *input, const ParamMap &params, QC::Vector<char> &outBuffer)
        {
            outBuffer.clear();
            if (!input)
            {
                outBuffer.push_back('\0');
                return outBuffer.data();
            }

            const char *p = input;
            while (*p)
            {
                if (*p == '{')
                {
                    const char *start = p + 1;
                    const char *q = start;

                    if (*q && isNameChar(*q))
                    {
                        while (*q && isNameChar(*q))
                            ++q;

                        if (*q == '}')
                        {
                            // We have {name}
                            // Copy name into a small stack buffer for lookup.
                            char nameBuf[64];
                            QC::usize len = static_cast<QC::usize>(q - start);
                            if (len >= sizeof(nameBuf))
                                len = sizeof(nameBuf) - 1;

                            for (QC::usize i = 0; i < len; ++i)
                                nameBuf[i] = start[i];
                            nameBuf[len] = '\0';

                            const char *replacement = params.get(nameBuf);
                            if (replacement)
                            {
                                for (const char *r = replacement; *r; ++r)
                                    outBuffer.push_back(*r);
                            }
                            else
                            {
                                // Unknown placeholder: keep original text.
                                outBuffer.push_back('{');
                                for (const char *t = start; t < q; ++t)
                                    outBuffer.push_back(*t);
                                outBuffer.push_back('}');
                            }

                            p = q + 1;
                            continue;
                        }
                    }
                }

                outBuffer.push_back(*p);
                ++p;
            }

            outBuffer.push_back('\0');
            return outBuffer.data();
        }

    } // namespace JSON
} // namespace QC
