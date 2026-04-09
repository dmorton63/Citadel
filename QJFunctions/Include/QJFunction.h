#pragma once

// QJFunctions - JSON-defined function modules (MVP)
// Namespace: QC::JFunc

#include "QCTypes.h"
#include "QCString.h"
#include "QCVector.h"
#include "QCJson.h"

namespace QC
{
    namespace JFunc
    {
        enum class Mode : QC::u8
        {
            Debug,
            Production
        };

        enum class Visibility : QC::u8
        {
            Private,
            Public
        };

        enum class ScalarType : QC::u8
        {
            Bool,
            I32,
            U32,
            F32,
            F64
        };

        struct TypedValue
        {
            ScalarType type = ScalarType::I32;
            union
            {
                bool b;
                QC::i32 i32;
                QC::u32 u32;
                QC::f32 f32;
                QC::f64 f64;
            } v;

            static TypedValue makeBool(bool x)
            {
                TypedValue tv;
                tv.type = ScalarType::Bool;
                tv.v.b = x;
                return tv;
            }
        };

        enum class ErrorCode : QC::u8
        {
            E_SCHEMA,
            E_AUTH,
            E_HASH,
            E_SIG,
            E_LIMIT,
            E_OP,
            E_TYPE,
            E_DIV0,
            E_DOMAIN,
            E_CALL
        };

        struct Error
        {
            ErrorCode code = ErrorCode::E_SCHEMA;
            QC::u32 stepIndex = 0xFFFFFFFFu; // null
            const char *message = nullptr;   // may be nullptr in production
        };

        struct Param
        {
            QC::String name;
            ScalarType type = ScalarType::I32;
        };

        enum class Op : QC::u8
        {
            Add,
            Sub,
            Multiply,
            Divide,
            Neg,
            Sqrt,
            Min,
            Max,
            Abs,
            CmpEq,
            CmpLt,
            CmpGt,
            And,
            Or,
            Not,
            Select
        };

        struct StepArg
        {
            bool isConst = false;
            QC::String ref;
            TypedValue constant;
        };

        struct Step
        {
            Op op = Op::Add;
            QC::Vector<StepArg> args;
            QC::String out;
            ScalarType outType = ScalarType::I32;
        };

        struct Function
        {
            QC::u32 specVersion = 0;
            QC::String id;
            QC::String stableIdentity;
            QC::String signatureHashHex;
            QC::String name;
            QC::u32 version = 0;
            Mode mode = Mode::Debug;
            Visibility visibility = Visibility::Private;
            QC::String ownership;

            QC::Vector<Param> inputs;
            QC::Vector<Param> outputs;
            QC::Vector<Step> steps;
        };

        class Engine
        {
        public:
            static constexpr QC::u32 MAX_STEPS = 256;
            static constexpr QC::u32 MAX_ARGS_PER_STEP = 8;
            static constexpr QC::u32 MAX_LOCALS = 512;
            static constexpr QC::u32 MAX_NAME_LEN = 64;
            static constexpr QC::u32 MAX_FILE_BYTES = 128 * 1024;
            static constexpr QC::u32 CANONICAL_INPUT_FORMAT_VERSION = 1;
            static constexpr QC::u32 HASH_BYTES = 32;

            static bool loadFromText(const char *jsonText, Function &outFn, Error &outErr);
            static bool loadFromVfsPath(const char *path, Function &outFn, Error &outErr);

            static bool validate(const QC::JSON::Value &root, Function &outFn, Error &outErr);

            static bool buildStableIdentity(const Function &fn, QC::String &outIdentity, Error &outErr);

            static bool computeSignatureHash(const Function &fn,
                                             QC::u8 outDigest[HASH_BYTES],
                                             QC::String &outHex,
                                             Error &outErr);

            static bool encodeCanonicalInputs(const Function &fn,
                                              const TypedValue *inputs,
                                              QC::usize inputCount,
                                              QC::Vector<QC::u8> &outBytes,
                                              Error &outErr);

            static bool computeInputHash(const Function &fn,
                                         const TypedValue *inputs,
                                         QC::usize inputCount,
                                         QC::u8 outDigest[HASH_BYTES],
                                         QC::String &outHex,
                                         Error &outErr);

            static bool execute(const Function &fn,
                                const TypedValue *inputs,
                                QC::usize inputCount,
                                TypedValue *outputs,
                                QC::usize outputCount,
                                Error &outErr);
        };

    } // namespace JFunc
} // namespace QC
