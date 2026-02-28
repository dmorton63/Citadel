#include "QJFunction.h"

#include "QCLinearAlgebra.h"
#include "QCString.h"

#include "QFSVFS.h"
#include "QFSFile.h"

namespace QC
{
    namespace JFunc
    {
        namespace
        {
            static void setErr(Error &e, ErrorCode code, const char *msg, QC::u32 stepIndex = 0xFFFFFFFFu)
            {
                e.code = code;
                e.message = msg;
                e.stepIndex = stepIndex;
            }

            static bool isAsciiLetter(char c)
            {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            }

            static bool isAsciiDigit(char c)
            {
                return (c >= '0' && c <= '9');
            }

            static bool isValidIdent(const char *s)
            {
                if (!s)
                    return false;
                QC::usize len = QC::String::strlen(s);
                if (len == 0 || len > Engine::MAX_NAME_LEN)
                    return false;
                if (!(isAsciiLetter(s[0]) || s[0] == '_'))
                    return false;
                for (QC::usize i = 1; i < len; ++i)
                {
                    const char c = s[i];
                    if (!(isAsciiLetter(c) || isAsciiDigit(c) || c == '_'))
                        return false;
                }
                return true;
            }

            static bool scalarTypeFromString(const char *s, ScalarType &out)
            {
                if (!s)
                    return false;
                if (QC::String::strcmp(s, "bool") == 0)
                {
                    out = ScalarType::Bool;
                    return true;
                }
                if (QC::String::strcmp(s, "i32") == 0)
                {
                    out = ScalarType::I32;
                    return true;
                }
                if (QC::String::strcmp(s, "u32") == 0)
                {
                    out = ScalarType::U32;
                    return true;
                }
                if (QC::String::strcmp(s, "f32") == 0)
                {
                    out = ScalarType::F32;
                    return true;
                }
                if (QC::String::strcmp(s, "f64") == 0)
                {
                    out = ScalarType::F64;
                    return true;
                }
                return false;
            }

            static bool opFromString(const char *s, Op &out)
            {
                if (!s)
                    return false;
                if (QC::String::strcmp(s, "add") == 0)
                {
                    out = Op::Add;
                    return true;
                }
                if (QC::String::strcmp(s, "sub") == 0)
                {
                    out = Op::Sub;
                    return true;
                }
                if (QC::String::strcmp(s, "multiply") == 0)
                {
                    out = Op::Multiply;
                    return true;
                }
                if (QC::String::strcmp(s, "divide") == 0)
                {
                    out = Op::Divide;
                    return true;
                }
                if (QC::String::strcmp(s, "neg") == 0)
                {
                    out = Op::Neg;
                    return true;
                }
                if (QC::String::strcmp(s, "sqrt") == 0)
                {
                    out = Op::Sqrt;
                    return true;
                }
                if (QC::String::strcmp(s, "min") == 0)
                {
                    out = Op::Min;
                    return true;
                }
                if (QC::String::strcmp(s, "max") == 0)
                {
                    out = Op::Max;
                    return true;
                }
                if (QC::String::strcmp(s, "abs") == 0)
                {
                    out = Op::Abs;
                    return true;
                }
                if (QC::String::strcmp(s, "cmp_eq") == 0)
                {
                    out = Op::CmpEq;
                    return true;
                }
                if (QC::String::strcmp(s, "cmp_lt") == 0)
                {
                    out = Op::CmpLt;
                    return true;
                }
                if (QC::String::strcmp(s, "cmp_gt") == 0)
                {
                    out = Op::CmpGt;
                    return true;
                }
                if (QC::String::strcmp(s, "and") == 0)
                {
                    out = Op::And;
                    return true;
                }
                if (QC::String::strcmp(s, "or") == 0)
                {
                    out = Op::Or;
                    return true;
                }
                if (QC::String::strcmp(s, "not") == 0)
                {
                    out = Op::Not;
                    return true;
                }
                if (QC::String::strcmp(s, "select") == 0)
                {
                    out = Op::Select;
                    return true;
                }
                return false;
            }

            static bool objectHasOnlyKeys(const QC::JSON::Value &objVal, const char *const *keys, QC::usize keyCount, bool allowMetadata)
            {
                const QC::JSON::Object *obj = objVal.asObject();
                if (!obj)
                    return false;

                for (QC::usize i = 0; i < obj->size(); ++i)
                {
                    const char *k = (*obj)[i].key;
                    if (!k)
                        return false;
                    if (allowMetadata && QC::String::strcmp(k, "metadata") == 0)
                        continue;
                    bool ok = false;
                    for (QC::usize j = 0; j < keyCount; ++j)
                    {
                        if (QC::String::strcmp(k, keys[j]) == 0)
                        {
                            ok = true;
                            break;
                        }
                    }
                    if (!ok)
                        return false;
                }
                return true;
            }

            static const Param *findParamByName(const QC::Vector<Param> &params, const char *name)
            {
                for (QC::usize i = 0; i < params.size(); ++i)
                {
                    if (QC::String::strcmp(params[i].name.c_str(), name) == 0)
                        return &params[i];
                }
                return nullptr;
            }

            struct NameType
            {
                const char *name = nullptr;
                ScalarType type = ScalarType::I32;
            };

            static const NameType *findLocal(const QC::Vector<NameType> &locals, const char *name)
            {
                for (QC::usize i = 0; i < locals.size(); ++i)
                {
                    if (QC::String::strcmp(locals[i].name, name) == 0)
                        return &locals[i];
                }
                return nullptr;
            }

            static bool parseConst(const QC::JSON::Value &arg, TypedValue &out, Error &outErr)
            {
                if (!arg.isObject())
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "Const arg must be object");
                    return false;
                }

                static const char *const outerKeys[] = {"const"};
                if (!objectHasOnlyKeys(arg, outerKeys, 1, false))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "Const arg has unknown keys");
                    return false;
                }

                const QC::JSON::Value *c = arg.find("const");
                if (!c || !c->isObject())
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "Const arg missing 'const' object");
                    return false;
                }

                static const char *const innerKeys[] = {"type", "value"};
                if (!objectHasOnlyKeys(*c, innerKeys, 2, false))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "Const payload has unknown keys");
                    return false;
                }

                const QC::JSON::Value *t = c->find("type");
                const QC::JSON::Value *v = c->find("value");
                if (!t || !t->isString() || !v)
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "Const requires type(string) and value");
                    return false;
                }

                ScalarType st;
                if (!scalarTypeFromString(t->asString(), st))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "Unknown const type");
                    return false;
                }

                out.type = st;
                switch (st)
                {
                case ScalarType::Bool:
                    if (!v->isBool())
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "Bool const must be JSON bool");
                        return false;
                    }
                    out.v.b = v->asBool(false);
                    return true;
                case ScalarType::I32:
                {
                    if (!v->isNumber())
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "i32 const must be JSON number");
                        return false;
                    }
                    const double d = v->asNumber();
                    const double di = static_cast<double>(static_cast<QC::i64>(d));
                    if (d != di)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "i32 const must be integer");
                        return false;
                    }
                    if (d < -2147483648.0 || d > 2147483647.0)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "i32 const out of range");
                        return false;
                    }
                    out.v.i32 = static_cast<QC::i32>(d);
                    return true;
                }
                case ScalarType::U32:
                {
                    if (!v->isNumber())
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "u32 const must be JSON number");
                        return false;
                    }
                    const double d = v->asNumber();
                    const double di = static_cast<double>(static_cast<QC::i64>(d));
                    if (d != di || d < 0.0)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "u32 const must be non-negative integer");
                        return false;
                    }
                    if (d > 4294967295.0)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "u32 const out of range");
                        return false;
                    }
                    out.v.u32 = static_cast<QC::u32>(d);
                    return true;
                }
                case ScalarType::F32:
                {
                    if (!v->isNumber())
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "f32 const must be JSON number");
                        return false;
                    }
                    const double d = v->asNumber();
                    // Range check against max finite f32 (~3.4e38)
                    if (d > 3.402823466e38 || d < -3.402823466e38)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "f32 const out of range");
                        return false;
                    }
                    out.v.f32 = static_cast<QC::f32>(d);
                    return true;
                }
                case ScalarType::F64:
                    if (!v->isNumber())
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "f64 const must be JSON number");
                        return false;
                    }
                    out.v.f64 = v->asNumber();
                    return true;
                }

                setErr(outErr, ErrorCode::E_TYPE, "Unsupported const type");
                return false;
            }

            static bool computeUnary(const Step &s, const TypedValue &a, TypedValue &out, Error &err, QC::u32 stepIndex)
            {
                (void)s;
                switch (s.op)
                {
                case Op::Neg:
                    out = a;
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = -a.v.i32;
                        return true;
                    case ScalarType::U32:
                        setErr(err, ErrorCode::E_TYPE, "neg not supported for u32", stepIndex);
                        return false;
                    case ScalarType::F32:
                        out.v.f32 = -a.v.f32;
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = -a.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "neg requires numeric", stepIndex);
                        return false;
                    }
                case Op::Abs:
                    out = a;
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = (a.v.i32 < 0) ? -a.v.i32 : a.v.i32;
                        return true;
                    case ScalarType::U32:
                        return true;
                    case ScalarType::F32:
                        out.v.f32 = QC::absf(a.v.f32);
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = (a.v.f64 < 0.0) ? -a.v.f64 : a.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "abs requires numeric", stepIndex);
                        return false;
                    }
                case Op::Not:
                    if (a.type != ScalarType::Bool)
                    {
                        setErr(err, ErrorCode::E_TYPE, "not requires bool", stepIndex);
                        return false;
                    }
                    out = TypedValue::makeBool(!a.v.b);
                    return true;
                case Op::Sqrt:
                    if (a.type == ScalarType::F32)
                    {
                        if (a.v.f32 < 0.0f)
                        {
                            setErr(err, ErrorCode::E_DOMAIN, "sqrt domain", stepIndex);
                            return false;
                        }
                        out.type = ScalarType::F32;
                        out.v.f32 = QC::sqrtf_sse(a.v.f32);
                        return true;
                    }
                    if (a.type == ScalarType::F64)
                    {
                        if (a.v.f64 < 0.0)
                        {
                            setErr(err, ErrorCode::E_DOMAIN, "sqrt domain", stepIndex);
                            return false;
                        }
                        QC::f64 r;
                        asm volatile(
                            "sqrtsd %1, %0"
                            : "=x"(r)
                            : "x"(a.v.f64));
                        out.type = ScalarType::F64;
                        out.v.f64 = r;
                        return true;
                    }
                    setErr(err, ErrorCode::E_TYPE, "sqrt requires f32/f64", stepIndex);
                    return false;
                default:
                    setErr(err, ErrorCode::E_OP, "unsupported unary op", stepIndex);
                    return false;
                }
            }

            static bool computeBinarySameType(const Step &s, const TypedValue &a, const TypedValue &b, TypedValue &out, Error &err, QC::u32 stepIndex)
            {
                if (a.type != b.type)
                {
                    setErr(err, ErrorCode::E_TYPE, "type mismatch", stepIndex);
                    return false;
                }
                out.type = a.type;

                switch (s.op)
                {
                case Op::Add:
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = a.v.i32 + b.v.i32;
                        return true;
                    case ScalarType::U32:
                        out.v.u32 = a.v.u32 + b.v.u32;
                        return true;
                    case ScalarType::F32:
                        out.v.f32 = a.v.f32 + b.v.f32;
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = a.v.f64 + b.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "add requires numeric", stepIndex);
                        return false;
                    }
                case Op::Sub:
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = a.v.i32 - b.v.i32;
                        return true;
                    case ScalarType::U32:
                        out.v.u32 = a.v.u32 - b.v.u32;
                        return true;
                    case ScalarType::F32:
                        out.v.f32 = a.v.f32 - b.v.f32;
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = a.v.f64 - b.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "sub requires numeric", stepIndex);
                        return false;
                    }
                case Op::Multiply:
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = a.v.i32 * b.v.i32;
                        return true;
                    case ScalarType::U32:
                        out.v.u32 = a.v.u32 * b.v.u32;
                        return true;
                    case ScalarType::F32:
                        out.v.f32 = a.v.f32 * b.v.f32;
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = a.v.f64 * b.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "multiply requires numeric", stepIndex);
                        return false;
                    }
                case Op::Divide:
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        if (b.v.i32 == 0)
                        {
                            setErr(err, ErrorCode::E_DIV0, "divide by zero", stepIndex);
                            return false;
                        }
                        out.v.i32 = a.v.i32 / b.v.i32;
                        return true;
                    case ScalarType::U32:
                        if (b.v.u32 == 0)
                        {
                            setErr(err, ErrorCode::E_DIV0, "divide by zero", stepIndex);
                            return false;
                        }
                        out.v.u32 = a.v.u32 / b.v.u32;
                        return true;
                    case ScalarType::F32:
                        if (b.v.f32 == 0.0f)
                        {
                            setErr(err, ErrorCode::E_DIV0, "divide by zero", stepIndex);
                            return false;
                        }
                        out.v.f32 = a.v.f32 / b.v.f32;
                        return true;
                    case ScalarType::F64:
                        if (b.v.f64 == 0.0)
                        {
                            setErr(err, ErrorCode::E_DIV0, "divide by zero", stepIndex);
                            return false;
                        }
                        out.v.f64 = a.v.f64 / b.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "divide requires numeric", stepIndex);
                        return false;
                    }
                case Op::Min:
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = (a.v.i32 < b.v.i32) ? a.v.i32 : b.v.i32;
                        return true;
                    case ScalarType::U32:
                        out.v.u32 = (a.v.u32 < b.v.u32) ? a.v.u32 : b.v.u32;
                        return true;
                    case ScalarType::F32:
                        out.v.f32 = QC::minf(a.v.f32, b.v.f32);
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = (a.v.f64 < b.v.f64) ? a.v.f64 : b.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "min requires numeric", stepIndex);
                        return false;
                    }
                case Op::Max:
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.i32 = (a.v.i32 > b.v.i32) ? a.v.i32 : b.v.i32;
                        return true;
                    case ScalarType::U32:
                        out.v.u32 = (a.v.u32 > b.v.u32) ? a.v.u32 : b.v.u32;
                        return true;
                    case ScalarType::F32:
                        out.v.f32 = QC::maxf(a.v.f32, b.v.f32);
                        return true;
                    case ScalarType::F64:
                        out.v.f64 = (a.v.f64 > b.v.f64) ? a.v.f64 : b.v.f64;
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "max requires numeric", stepIndex);
                        return false;
                    }
                case Op::CmpEq:
                    out.type = ScalarType::Bool;
                    switch (a.type)
                    {
                    case ScalarType::Bool:
                        out.v.b = (a.v.b == b.v.b);
                        return true;
                    case ScalarType::I32:
                        out.v.b = (a.v.i32 == b.v.i32);
                        return true;
                    case ScalarType::U32:
                        out.v.b = (a.v.u32 == b.v.u32);
                        return true;
                    case ScalarType::F32:
                        out.v.b = (a.v.f32 == b.v.f32);
                        return true;
                    case ScalarType::F64:
                        out.v.b = (a.v.f64 == b.v.f64);
                        return true;
                    }
                    break;
                case Op::CmpLt:
                    out.type = ScalarType::Bool;
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.b = (a.v.i32 < b.v.i32);
                        return true;
                    case ScalarType::U32:
                        out.v.b = (a.v.u32 < b.v.u32);
                        return true;
                    case ScalarType::F32:
                        out.v.b = (a.v.f32 < b.v.f32);
                        return true;
                    case ScalarType::F64:
                        out.v.b = (a.v.f64 < b.v.f64);
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "cmp_lt requires numeric", stepIndex);
                        return false;
                    }
                case Op::CmpGt:
                    out.type = ScalarType::Bool;
                    switch (a.type)
                    {
                    case ScalarType::I32:
                        out.v.b = (a.v.i32 > b.v.i32);
                        return true;
                    case ScalarType::U32:
                        out.v.b = (a.v.u32 > b.v.u32);
                        return true;
                    case ScalarType::F32:
                        out.v.b = (a.v.f32 > b.v.f32);
                        return true;
                    case ScalarType::F64:
                        out.v.b = (a.v.f64 > b.v.f64);
                        return true;
                    default:
                        setErr(err, ErrorCode::E_TYPE, "cmp_gt requires numeric", stepIndex);
                        return false;
                    }
                case Op::And:
                    if (a.type != ScalarType::Bool)
                    {
                        setErr(err, ErrorCode::E_TYPE, "and requires bool", stepIndex);
                        return false;
                    }
                    out.type = ScalarType::Bool;
                    out.v.b = a.v.b && b.v.b;
                    return true;
                case Op::Or:
                    if (a.type != ScalarType::Bool)
                    {
                        setErr(err, ErrorCode::E_TYPE, "or requires bool", stepIndex);
                        return false;
                    }
                    out.type = ScalarType::Bool;
                    out.v.b = a.v.b || b.v.b;
                    return true;
                default:
                    break;
                }

                setErr(err, ErrorCode::E_OP, "unsupported binary op", stepIndex);
                return false;
            }

            static bool evalArg(const StepArg &a,
                                const QC::Vector<NameType> &locals,
                                const QC::Vector<TypedValue> &values,
                                TypedValue &out,
                                Error &err,
                                QC::u32 stepIndex)
            {
                if (a.isConst)
                {
                    out = a.constant;
                    return true;
                }

                const NameType *nt = findLocal(locals, a.ref.c_str());
                if (!nt)
                {
                    setErr(err, ErrorCode::E_TYPE, "unknown reference", stepIndex);
                    return false;
                }

                // locals and values are kept in sync by index
                for (QC::usize i = 0; i < locals.size(); ++i)
                {
                    if (QC::String::strcmp(locals[i].name, nt->name) == 0)
                    {
                        out = values[i];
                        return true;
                    }
                }

                setErr(err, ErrorCode::E_TYPE, "internal frame lookup failed", stepIndex);
                return false;
            }

            static bool addLocal(QC::Vector<NameType> &locals, QC::Vector<TypedValue> &values, const char *name, ScalarType type, const TypedValue *initial)
            {
                NameType nt;
                nt.name = name;
                nt.type = type;
                locals.push_back(nt);
                if (initial)
                    values.push_back(*initial);
                else
                {
                    TypedValue v;
                    v.type = type;
                    values.push_back(v);
                }
                return true;
            }

        } // namespace

        bool Engine::loadFromText(const char *jsonText, Function &outFn, Error &outErr)
        {
            if (!jsonText)
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "null jsonText");
                return false;
            }

            const QC::usize len = QC::String::strlen(jsonText);
            if (len > MAX_FILE_BYTES)
            {
                setErr(outErr, ErrorCode::E_LIMIT, "json too large");
                return false;
            }

            QC::JSON::Value root;
            QC::JSON::Parser::Options opt;
            opt.forbidExponent = true;
            opt.requireCanonicalNumbers = true;
            if (!QC::JSON::parseEx(jsonText, len, root, opt))
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "json parse failed");
                return false;
            }

            return validate(root, outFn, outErr);
        }

        bool Engine::loadFromVfsPath(const char *path, Function &outFn, Error &outErr)
        {
            if (!path)
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "null path");
                return false;
            }

            QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!f || !f->isOpen())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "failed to open file");
                return false;
            }

            const QC::u64 sz = f->size();
            if (sz > MAX_FILE_BYTES)
            {
                QFS::VFS::instance().close(f);
                setErr(outErr, ErrorCode::E_LIMIT, "file too large");
                return false;
            }

            char *buf = static_cast<char *>(operator new[](static_cast<QC::usize>(sz) + 1));
            if (!buf)
            {
                QFS::VFS::instance().close(f);
                setErr(outErr, ErrorCode::E_LIMIT, "out of memory");
                return false;
            }

            QC::usize total = 0;
            while (total < static_cast<QC::usize>(sz))
            {
                QC::isize r = f->read(buf + total, static_cast<QC::usize>(sz) - total);
                if (r <= 0)
                    break;
                total += static_cast<QC::usize>(r);
            }
            buf[total] = '\0';
            QFS::VFS::instance().close(f);

            const bool ok = loadFromText(buf, outFn, outErr);
            operator delete[](buf);
            return ok;
        }

        bool Engine::validate(const QC::JSON::Value &root, Function &outFn, Error &outErr)
        {
            if (!root.isObject())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "root must be object");
                return false;
            }

            static const char *const rootKeys[] = {"function"};
            if (!objectHasOnlyKeys(root, rootKeys, 1, false))
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "unknown top-level keys");
                return false;
            }

            const QC::JSON::Value *fnv = root.find("function");
            if (!fnv || !fnv->isObject())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "missing function object");
                return false;
            }

            // Enforce spec_version is the first key in file order.
            const QC::JSON::Object *fnObj = fnv->asObject();
            if (!fnObj || fnObj->size() == 0 || !(*fnObj)[0].key || QC::String::strcmp((*fnObj)[0].key, "spec_version") != 0)
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "spec_version must be first key in function object");
                return false;
            }

            static const char *const fnKeys[] = {
                "spec_version",
                "auth",
                "mode",
                "visibility",
                "ownership",
                "inputs",
                "outputs",
                "steps",
                "name",
                "version",
                "id",
                "requires",
                "calls",
                "metadata"};

            if (!objectHasOnlyKeys(*fnv, fnKeys, sizeof(fnKeys) / sizeof(fnKeys[0]), true))
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "unknown keys in function object");
                return false;
            }

            const QC::JSON::Value *spec = fnv->find("spec_version");
            if (!spec || !spec->isNumber() || spec->asNumber() != 1.0)
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "spec_version must be 1");
                return false;
            }
            outFn.specVersion = 1;

            const QC::JSON::Value *name = fnv->find("name");
            const QC::JSON::Value *ver = fnv->find("version");
            const QC::JSON::Value *mode = fnv->find("mode");
            const QC::JSON::Value *vis = fnv->find("visibility");
            const QC::JSON::Value *own = fnv->find("ownership");
            const QC::JSON::Value *ins = fnv->find("inputs");
            const QC::JSON::Value *outs = fnv->find("outputs");
            const QC::JSON::Value *steps = fnv->find("steps");

            if (!name || !name->isString() || !isValidIdent(name->asString()))
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "invalid name");
                return false;
            }
            outFn.name = QC::String(name->asString());

            if (!ver || !ver->isNumber())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "version must be number");
                return false;
            }
            const double vd = ver->asNumber();
            if (vd < 1.0 || vd != static_cast<double>(static_cast<QC::u32>(vd)))
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "version must be u32 >= 1");
                return false;
            }
            outFn.version = static_cast<QC::u32>(vd);

            if (!mode || !mode->isString())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "missing mode");
                return false;
            }
            if (QC::String::strcmp(mode->asString(), "DEBUG") == 0)
                outFn.mode = Mode::Debug;
            else if (QC::String::strcmp(mode->asString(), "PRODUCTION") == 0)
                outFn.mode = Mode::Production;
            else
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "invalid mode");
                return false;
            }

            if (!vis || !vis->isString())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "missing visibility");
                return false;
            }
            if (QC::String::strcmp(vis->asString(), "private") == 0)
                outFn.visibility = Visibility::Private;
            else if (QC::String::strcmp(vis->asString(), "public") == 0)
                outFn.visibility = Visibility::Public;
            else
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "invalid visibility");
                return false;
            }

            if (!own || !own->isString())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "missing ownership");
                return false;
            }
            outFn.ownership = QC::String(own->asString());

            const QC::JSON::Value *idv = fnv->find("id");
            if (idv)
            {
                if (!idv->isString())
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "id must be string");
                    return false;
                }
                outFn.id = QC::String(idv->asString());
            }

            const QC::JSON::Value *req = fnv->find("requires");
            if (req)
            {
                if (!req->isArray() || req->asArray()->size() != 0)
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "requires must be empty array in MVP");
                    return false;
                }
            }
            const QC::JSON::Value *calls = fnv->find("calls");
            if (calls)
            {
                if (!calls->isArray() || calls->asArray()->size() != 0)
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "calls must be empty array in MVP");
                    return false;
                }
            }

            if (!ins || !ins->isArray() || !outs || !outs->isArray() || !steps || !steps->isArray())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "inputs/outputs/steps must be arrays");
                return false;
            }

            if (steps->asArray()->size() > MAX_STEPS)
            {
                setErr(outErr, ErrorCode::E_LIMIT, "too many steps");
                return false;
            }

            outFn.inputs.clear();
            outFn.outputs.clear();
            outFn.steps.clear();

            // Parse inputs
            {
                const QC::JSON::Array *arr = ins->asArray();
                for (QC::usize i = 0; i < arr->size(); ++i)
                {
                    const QC::JSON::Value *pv = (*arr)[i];
                    if (!pv || !pv->isObject())
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "input must be object");
                        return false;
                    }
                    static const char *const keys[] = {"name", "type"};
                    if (!objectHasOnlyKeys(*pv, keys, 2, false))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "unknown keys in input");
                        return false;
                    }
                    const QC::JSON::Value *pn = pv->find("name");
                    const QC::JSON::Value *pt = pv->find("type");
                    if (!pn || !pn->isString() || !isValidIdent(pn->asString()) || !pt || !pt->isString())
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "invalid input name/type");
                        return false;
                    }
                    ScalarType st;
                    if (!scalarTypeFromString(pt->asString(), st))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "unknown input type");
                        return false;
                    }
                    if (findParamByName(outFn.inputs, pn->asString()))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "duplicate input name");
                        return false;
                    }
                    Param p;
                    p.name = QC::String(pn->asString());
                    p.type = st;
                    outFn.inputs.push_back(static_cast<Param &&>(p));
                }
            }

            // Parse outputs
            {
                const QC::JSON::Array *arr = outs->asArray();
                for (QC::usize i = 0; i < arr->size(); ++i)
                {
                    const QC::JSON::Value *pv = (*arr)[i];
                    if (!pv || !pv->isObject())
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "output must be object");
                        return false;
                    }
                    static const char *const keys[] = {"name", "type"};
                    if (!objectHasOnlyKeys(*pv, keys, 2, false))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "unknown keys in output");
                        return false;
                    }
                    const QC::JSON::Value *pn = pv->find("name");
                    const QC::JSON::Value *pt = pv->find("type");
                    if (!pn || !pn->isString() || !isValidIdent(pn->asString()) || !pt || !pt->isString())
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "invalid output name/type");
                        return false;
                    }
                    ScalarType st;
                    if (!scalarTypeFromString(pt->asString(), st))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "unknown output type");
                        return false;
                    }
                    if (findParamByName(outFn.outputs, pn->asString()))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "duplicate output name");
                        return false;
                    }
                    if (findParamByName(outFn.inputs, pn->asString()))
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "output overlaps input");
                        return false;
                    }
                    Param p;
                    p.name = QC::String(pn->asString());
                    p.type = st;
                    outFn.outputs.push_back(static_cast<Param &&>(p));
                }
            }

            // Production auth gate (MVP: not implemented)
            if (outFn.mode == Mode::Production)
            {
                setErr(outErr, ErrorCode::E_AUTH, "PRODUCTION modules not supported (signature verification not implemented)");
                return false;
            }

            // Prepare local type table (inputs first)
            QC::Vector<NameType> locals;
            locals.reserve(outFn.inputs.size() + outFn.outputs.size() + 16);
            for (QC::usize i = 0; i < outFn.inputs.size(); ++i)
            {
                NameType nt;
                nt.name = outFn.inputs[i].name.c_str();
                nt.type = outFn.inputs[i].type;
                locals.push_back(nt);
            }

            // Parse steps
            const QC::JSON::Array *sarr = steps->asArray();
            for (QC::usize si = 0; si < sarr->size(); ++si)
            {
                const QC::JSON::Value *sv = (*sarr)[si];
                if (!sv || !sv->isObject())
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "step must be object");
                    return false;
                }
                static const char *const keys[] = {"op", "args", "out"};
                if (!objectHasOnlyKeys(*sv, keys, 3, false))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "unknown keys in step");
                    return false;
                }
                const QC::JSON::Value *opv = sv->find("op");
                const QC::JSON::Value *argv = sv->find("args");
                const QC::JSON::Value *outv = sv->find("out");
                if (!opv || !opv->isString() || !argv || !argv->isArray() || !outv || !outv->isString())
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "step requires op(string), args(array), out(string)");
                    return false;
                }

                if (!isValidIdent(outv->asString()))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "invalid step out name");
                    return false;
                }

                // out must not shadow an input
                if (findParamByName(outFn.inputs, outv->asString()))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "step out cannot reuse input name");
                    return false;
                }

                // unique out
                if (findLocal(locals, outv->asString()))
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "duplicate out name");
                    return false;
                }

                Op op;
                if (!opFromString(opv->asString(), op))
                {
                    setErr(outErr, ErrorCode::E_OP, "unknown op");
                    return false;
                }

                const QC::JSON::Array *aarr = argv->asArray();
                if (aarr->size() > MAX_ARGS_PER_STEP)
                {
                    setErr(outErr, ErrorCode::E_LIMIT, "too many args");
                    return false;
                }

                // Parse args and resolve types
                QC::Vector<StepArg> args;
                args.reserve(aarr->size());
                QC::Vector<ScalarType> argTypes;
                argTypes.reserve(aarr->size());

                for (QC::usize ai = 0; ai < aarr->size(); ++ai)
                {
                    const QC::JSON::Value *av = (*aarr)[ai];
                    if (!av)
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "null arg");
                        return false;
                    }

                    StepArg a;
                    if (av->isString())
                    {
                        const char *ref = av->asString();
                        const NameType *nt = findLocal(locals, ref);
                        if (!nt)
                        {
                            setErr(outErr, ErrorCode::E_TYPE, "unknown reference");
                            return false;
                        }
                        a.isConst = false;
                        a.ref = QC::String(ref);
                        argTypes.push_back(nt->type);
                    }
                    else if (av->isObject())
                    {
                        a.isConst = true;
                        if (!parseConst(*av, a.constant, outErr))
                            return false;
                        argTypes.push_back(a.constant.type);
                    }
                    else
                    {
                        setErr(outErr, ErrorCode::E_SCHEMA, "arg must be string ref or const object");
                        return false;
                    }
                    args.push_back(static_cast<StepArg &&>(a));
                }

                // Op arity + type rules
                ScalarType outType = ScalarType::I32;
                auto requireArgs = [&](QC::usize n) -> bool {
                    if (aarr->size() != n)
                    {
                        setErr(outErr, ErrorCode::E_OP, "wrong arg count");
                        return false;
                    }
                    return true;
                };

                switch (op)
                {
                case Op::Neg:
                case Op::Sqrt:
                case Op::Abs:
                case Op::Not:
                    if (!requireArgs(1))
                        return false;
                    outType = argTypes[0];
                    if (op == Op::Not)
                    {
                        if (outType != ScalarType::Bool)
                        {
                            setErr(outErr, ErrorCode::E_TYPE, "not requires bool");
                            return false;
                        }
                    }
                    if (op == Op::Sqrt)
                    {
                        if (!(outType == ScalarType::F32 || outType == ScalarType::F64))
                        {
                            setErr(outErr, ErrorCode::E_TYPE, "sqrt requires f32/f64");
                            return false;
                        }
                    }
                    break;

                case Op::Add:
                case Op::Sub:
                case Op::Multiply:
                case Op::Divide:
                case Op::Min:
                case Op::Max:
                    if (!requireArgs(2))
                        return false;
                    if (argTypes[0] != argTypes[1] || argTypes[0] == ScalarType::Bool)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "binary op requires same numeric type");
                        return false;
                    }
                    outType = argTypes[0];
                    break;

                case Op::CmpEq:
                case Op::CmpLt:
                case Op::CmpGt:
                    if (!requireArgs(2))
                        return false;
                    if (argTypes[0] != argTypes[1])
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "cmp requires same type");
                        return false;
                    }
                    outType = ScalarType::Bool;
                    break;

                case Op::And:
                case Op::Or:
                    if (!requireArgs(2))
                        return false;
                    if (argTypes[0] != ScalarType::Bool || argTypes[1] != ScalarType::Bool)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "and/or requires bool");
                        return false;
                    }
                    outType = ScalarType::Bool;
                    break;

                case Op::Select:
                    if (!requireArgs(3))
                        return false;
                    if (argTypes[0] != ScalarType::Bool)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "select cond must be bool");
                        return false;
                    }
                    if (argTypes[1] != argTypes[2])
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "select requires matching branches");
                        return false;
                    }
                    outType = argTypes[1];
                    break;
                }

                // If writing to a declared output, enforce type matches output type.
                const Param *outParam = findParamByName(outFn.outputs, outv->asString());
                if (outParam && outParam->type != outType)
                {
                    setErr(outErr, ErrorCode::E_TYPE, "output type mismatch");
                    return false;
                }

                // Record local
                NameType nt;
                nt.name = outv->asString();
                nt.type = outType;
                locals.push_back(nt);
                if (locals.size() > MAX_LOCALS)
                {
                    setErr(outErr, ErrorCode::E_LIMIT, "too many locals");
                    return false;
                }

                Step step;
                step.op = op;
                step.args = static_cast<QC::Vector<StepArg> &&>(args);
                step.out = QC::String(outv->asString());
                step.outType = outType;
                outFn.steps.push_back(static_cast<Step &&>(step));
            }

            // Output production rule: every output must be produced by a step out.
            for (QC::usize i = 0; i < outFn.outputs.size(); ++i)
            {
                const char *on = outFn.outputs[i].name.c_str();
                bool found = false;
                for (QC::usize si = 0; si < outFn.steps.size(); ++si)
                {
                    if (QC::String::strcmp(outFn.steps[si].out.c_str(), on) == 0)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "output not produced by any step");
                    return false;
                }
            }

            return true;
        }

        bool Engine::execute(const Function &fn,
                             const TypedValue *inputs,
                             QC::usize inputCount,
                             TypedValue *outputs,
                             QC::usize outputCount,
                             Error &outErr)
        {
            if (fn.mode == Mode::Production)
            {
                setErr(outErr, ErrorCode::E_AUTH, "PRODUCTION execution blocked (auth not implemented)");
                return false;
            }

            if (inputCount != fn.inputs.size() || outputCount != fn.outputs.size())
            {
                setErr(outErr, ErrorCode::E_SCHEMA, "input/output count mismatch");
                return false;
            }

            // Typed frame: keep locals+values aligned by index.
            QC::Vector<NameType> locals;
            QC::Vector<TypedValue> values;
            locals.reserve(fn.inputs.size() + fn.steps.size());
            values.reserve(fn.inputs.size() + fn.steps.size());

            // Load inputs
            for (QC::usize i = 0; i < fn.inputs.size(); ++i)
            {
                if (inputs[i].type != fn.inputs[i].type)
                {
                    setErr(outErr, ErrorCode::E_TYPE, "input type mismatch");
                    return false;
                }
                addLocal(locals, values, fn.inputs[i].name.c_str(), fn.inputs[i].type, &inputs[i]);
            }

            // Execute steps
            for (QC::usize si = 0; si < fn.steps.size(); ++si)
            {
                const Step &s = fn.steps[si];
                const QC::u32 stepIndex = static_cast<QC::u32>(si);

                TypedValue out;
                if (s.op == Op::Select)
                {
                    if (s.args.size() != 3)
                    {
                        setErr(outErr, ErrorCode::E_OP, "select arg count", stepIndex);
                        return false;
                    }
                    TypedValue cond;
                    TypedValue a;
                    TypedValue b;
                    if (!evalArg(s.args[0], locals, values, cond, outErr, stepIndex) ||
                        !evalArg(s.args[1], locals, values, a, outErr, stepIndex) ||
                        !evalArg(s.args[2], locals, values, b, outErr, stepIndex))
                        return false;
                    if (cond.type != ScalarType::Bool)
                    {
                        setErr(outErr, ErrorCode::E_TYPE, "select cond type", stepIndex);
                        return false;
                    }
                    out = cond.v.b ? a : b;
                }
                else if (s.args.size() == 1)
                {
                    TypedValue a;
                    if (!evalArg(s.args[0], locals, values, a, outErr, stepIndex))
                        return false;
                    if (!computeUnary(s, a, out, outErr, stepIndex))
                        return false;
                }
                else if (s.args.size() == 2)
                {
                    TypedValue a;
                    TypedValue b;
                    if (!evalArg(s.args[0], locals, values, a, outErr, stepIndex) ||
                        !evalArg(s.args[1], locals, values, b, outErr, stepIndex))
                        return false;
                    if (!computeBinarySameType(s, a, b, out, outErr, stepIndex))
                        return false;
                }
                else
                {
                    setErr(outErr, ErrorCode::E_OP, "unsupported arg count", stepIndex);
                    return false;
                }

                // Store local
                addLocal(locals, values, s.out.c_str(), s.outType, &out);
            }

            // Extract outputs
            for (QC::usize oi = 0; oi < fn.outputs.size(); ++oi)
            {
                const char *on = fn.outputs[oi].name.c_str();
                bool found = false;
                for (QC::usize li = 0; li < locals.size(); ++li)
                {
                    if (QC::String::strcmp(locals[li].name, on) == 0)
                    {
                        outputs[oi] = values[li];
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    setErr(outErr, ErrorCode::E_SCHEMA, "missing output at runtime");
                    return false;
                }
                if (outputs[oi].type != fn.outputs[oi].type)
                {
                    setErr(outErr, ErrorCode::E_TYPE, "output type mismatch");
                    return false;
                }
            }

            return true;
        }

    } // namespace JFunc
} // namespace QC
