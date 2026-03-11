#pragma once

// QCJsonOverlay - JSON layering (override) + string templating utilities
// Namespace: QC::JSON
//
// Goals:
// - Support override precedence without mutating QC::JSON::Value (which has private setters)
// - Support string-only templating with {name} placeholders
// - Allow deep-merge semantics for nested objects (object fields fall back through layers)
// - Arrays are treated as replace-on-override (highest layer wins)

#include "QCJson.h"
#include "QCString.h"
#include "QCVector.h"

namespace QC
{
    namespace JSON
    {
        struct ParamEntry
        {
            const char *key = nullptr;
            const char *value = nullptr;
        };

        class ParamMap
        {
        public:
            void clear() { m_entries.clear(); }

            void set(const char *key, const char *value);
            const char *get(const char *key) const;

        private:
            QC::Vector<ParamEntry> m_entries;
        };

        // A read-only view over one or more JSON values representing the same logical node.
        // Layers are ordered by precedence: index 0 is highest priority.
        class Overlay
        {
        public:
            Overlay() = default;

            void clear() { m_layers.clear(); }
            void addLayer(const Value *value);

            bool empty() const { return m_layers.size() == 0; }

            // Returns the highest-priority concrete value for this node.
            const Value *value() const;

            // Key lookup across object layers.
            // - If a higher-priority layer contains the key with a non-object value, it fully overrides.
            // - If multiple layers contain the key with object values, they are combined into a child Overlay.
            Overlay child(const char *key) const;

            // Convenience accessors for the resolved highest-priority value.
            bool isNull() const;
            bool isBool() const;
            bool isNumber() const;
            bool isString() const;
            bool isObject() const;
            bool isArray() const;

            bool asBool(bool defaultValue = false) const;
            double asNumber(double defaultValue = 0.0) const;
            const char *asString(const char *defaultValue = "") const;
            const Object *asObject() const;
            const Array *asArray() const;

            // Finds a key on the resolved object only (no deep merge).
            const Value *findResolved(const char *key) const;

            // Collects templating params from this node.
            // Looks for an object named "params" within each object layer and merges keys by precedence.
            // Precedence: highest Overlay layer wins.
            void collectParams(ParamMap &out) const;

            // Expands {name} placeholders in a string value using params.
            // If the resolved value is not a string, returns defaultValue.
            // Output is written to outBuffer (always null-terminated). The returned pointer remains
            // valid until outBuffer is modified.
            const char *asTemplatedString(const ParamMap &params, QC::Vector<char> &outBuffer, const char *defaultValue = "") const;

        private:
            QC::Vector<const Value *> m_layers;
        };

        // Low-level templating helper for raw C strings.
        // - Only substitutes placeholders of the form {name}
        // - name characters: [A-Za-z0-9_]
        // - Unknown placeholders are left unchanged.
        // Returns a pointer into outBuffer (null-terminated).
        const char *expandTemplateString(const char *input, const ParamMap &params, QC::Vector<char> &outBuffer);

    } // namespace JSON
} // namespace QC
