#pragma once

// QPR Registry - System registry for configuration
// Namespace: QPR

#include "QCTypes.h"
#include "QCString.h"

namespace QPR
{

    // Registry value types
    enum class RegistryType : QC::u8
    {
        None,
        String,
        Integer,
        Binary,
        Boolean
    };

    // Registry value
    struct RegistryValue
    {
        RegistryType type;
        union
        {
            char *stringValue;
            QC::i64 intValue;
            bool boolValue;
            struct
            {
                QC::u8 *data;
                QC::usize size;
            } binary;
        };
    };

    // Registry key (node in tree)
    struct RegistryKey
    {
        char name[64];
        RegistryKey *parent;
        RegistryKey *firstChild;
        RegistryKey *nextSibling;

        // Values stored in this key
        struct ValueEntry
        {
            char name[64];
            RegistryValue value;
            ValueEntry *next;
        };
        ValueEntry *values;
    };

    class Registry
    {
    public:
        static Registry &instance();

        void initialize();

        // Key operations
        RegistryKey *openKey(const char *path);
        RegistryKey *createKey(const char *path);
        QC::Status deleteKey(const char *path);
        bool keyExists(const char *path);

        // Value operations
        QC::Status getValue(const char *path, const char *name, RegistryValue *value);
        QC::Status setValue(const char *path, const char *name, const RegistryValue *value);
        QC::Status deleteValue(const char *path, const char *name);

        // Convenience getters
        QC::Status getString(const char *path, const char *name, char *buffer, QC::usize bufferSize);
        QC::Status getInteger(const char *path, const char *name, QC::i64 *value);
        QC::Status getBool(const char *path, const char *name, bool *value);
        QC::Status getBinary(const char *path, const char *name, void *buffer, QC::usize *size);

        // Convenience setters
        QC::Status setString(const char *path, const char *name, const char *value);
        QC::Status setInteger(const char *path, const char *name, QC::i64 value);
        QC::Status setBool(const char *path, const char *name, bool value);
        QC::Status setBinary(const char *path, const char *name, const void *data, QC::usize size);

        // Enumeration
        QC::Status enumKeys(const char *path, char **names, QC::usize *count, QC::usize maxCount);
        QC::Status enumValues(const char *path, char **names, RegistryType *types,
                              QC::usize *count, QC::usize maxCount);

        // Persistence
        QC::Status save(const char *filename);
        QC::Status load(const char *filename);

        // Standard paths
        static constexpr const char *SYSTEM = "/System";
        static constexpr const char *HARDWARE = "/System/Hardware";
        static constexpr const char *DRIVERS = "/System/Drivers";
        static constexpr const char *SERVICES = "/System/Services";
        static constexpr const char *USERS = "/Users";
        static constexpr const char *SOFTWARE = "/Software";

    private:
        Registry();
        ~Registry();
        Registry(const Registry &) = delete;
        Registry &operator=(const Registry &) = delete;

        RegistryKey *findKey(const char *path, bool create);
        RegistryKey::ValueEntry *findValue(RegistryKey *key, const char *name, bool create);
        void freeValue(RegistryValue *value);
        void freeKey(RegistryKey *key);

        RegistryKey *m_root;
    };

} // namespace QPR
