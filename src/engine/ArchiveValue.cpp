// ArchiveValue.h の実装。
#include "engine/ArchiveValue.h"

#include <utility> // std::move

namespace husky::engine
{
    namespace
    {
        // 「無い」ことを表す値と空の文字列。参照で返すので、関数が終わっても消えない場所(static)に置く。
        const ArchiveValue& NullValue()
        {
            static const ArchiveValue value;
            return value;
        }

        const std::string& EmptyString()
        {
            static const std::string value;
            return value;
        }
    }

    ArchiveValue::ArchiveValue(bool value) : m_type(Type::Bool), m_bool(value) {}
    ArchiveValue::ArchiveValue(std::int64_t value) : m_type(Type::Int), m_int(value) {}
    ArchiveValue::ArchiveValue(double value) : m_type(Type::Float), m_float(value) {}
    ArchiveValue::ArchiveValue(float value) : m_type(Type::Float), m_float(value), m_singlePrecision(true) {}
    // std::move: value の中身を、コピーせずに m_string へ移す(移した後の value は空になってよい)。
    ArchiveValue::ArchiveValue(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
    ArchiveValue::ArchiveValue(const char* value) : m_type(Type::String), m_string(value != nullptr ? value : "") {}

    ArchiveValue ArchiveValue::MakeArray()
    {
        ArchiveValue value;
        value.m_type = Type::Array;
        return value;
    }

    ArchiveValue ArchiveValue::MakeObject()
    {
        ArchiveValue value;
        value.m_type = Type::Object;
        return value;
    }

    bool ArchiveValue::AsBool(bool fallback) const
    {
        return m_type == Type::Bool ? m_bool : fallback;
    }

    std::int64_t ArchiveValue::AsInt(std::int64_t fallback) const
    {
        return m_type == Type::Int ? m_int : fallback;
    }

    double ArchiveValue::AsFloat(double fallback) const
    {
        if (m_type == Type::Float)
        {
            return m_float;
        }
        if (m_type == Type::Int)
        {
            return static_cast<double>(m_int);
        }
        return fallback;
    }

    const std::string& ArchiveValue::AsString() const
    {
        return m_type == Type::String ? m_string : EmptyString();
    }

    std::size_t ArchiveValue::GetArraySize() const
    {
        return m_type == Type::Array ? m_children.size() : 0;
    }

    const ArchiveValue& ArchiveValue::At(std::size_t index) const
    {
        return (m_type == Type::Array && index < m_children.size()) ? m_children[index] : NullValue();
    }

    ArchiveValue& ArchiveValue::Append(ArchiveValue value)
    {
        if (m_type != Type::Array)
        {
            *this = MakeArray();
        }
        m_children.push_back(std::move(value));
        return m_children.back();
    }

    std::size_t ArchiveValue::GetMemberCount() const
    {
        return m_type == Type::Object ? m_children.size() : 0;
    }

    const std::string& ArchiveValue::GetMemberName(std::size_t index) const
    {
        return (m_type == Type::Object && index < m_names.size()) ? m_names[index] : EmptyString();
    }

    const ArchiveValue& ArchiveValue::GetMemberValue(std::size_t index) const
    {
        return (m_type == Type::Object && index < m_children.size()) ? m_children[index] : NullValue();
    }

    const ArchiveValue* ArchiveValue::Find(std::string_view name) const
    {
        if (m_type != Type::Object)
        {
            return nullptr;
        }
        for (std::size_t i = 0; i < m_names.size(); ++i)
        {
            if (m_names[i] == name)
            {
                return &m_children[i];
            }
        }
        return nullptr;
    }

    ArchiveValue& ArchiveValue::Set(std::string_view name, ArchiveValue value)
    {
        if (m_type != Type::Object)
        {
            *this = MakeObject();
        }
        for (std::size_t i = 0; i < m_names.size(); ++i)
        {
            if (m_names[i] == name)
            {
                m_children[i] = std::move(value);
                return m_children[i];
            }
        }
        m_names.emplace_back(name); // emplace_back: 要素をその場で作って足す(string_view から string を作る)
        m_children.push_back(std::move(value));
        return m_children.back();
    }
}
