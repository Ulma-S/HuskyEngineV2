// 型情報。RTTI(dynamic_cast や typeid)を使わずに、「このオブジェクトは何の型か」を調べるためのもの。
// 型ごとに1つだけ作られ、アドレスがその型の目印になる。
#pragma once

namespace husky::core
{
    // 動作スレッド: どこでもよい(作られた後は読み取り専用)
    class TypeInfo final
    {
    public:
        // name: 型の名前(例: "Enemy")。parent: 親の型の型情報。一番上の型(Object)では nullptr。
        TypeInfo(const char* name, const TypeInfo* parent)
            : m_name(name)
            , m_parent(parent)
        {
        }

        // 型情報は型ごとに1つだけ。コピーできると「同じ型なのにアドレスが違う」ものができてしまうので禁止する。
        TypeInfo(const TypeInfo&) = delete;
        TypeInfo& operator=(const TypeInfo&) = delete;

        const char* GetName() const { return m_name; }
        const TypeInfo* GetParent() const { return m_parent; }

        // この型が other と同じか、other の子孫なら true(C# の is にあたる)。
        // 親をたどって、other と同じ型情報(同じアドレス)が見つかるかを調べる。
        bool IsA(const TypeInfo& other) const
        {
            for (const TypeInfo* type = this; type != nullptr; type = type->m_parent)
            {
                if (type == &other)
                {
                    return true;
                }
            }
            return false;
        }

    private:
        const char* m_name;
        const TypeInfo* m_parent;
    };
}
