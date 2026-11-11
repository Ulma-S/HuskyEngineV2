// 型情報。RTTI(dynamic_cast や typeid)を使わずに、「このオブジェクトは何の型か」を調べるためのもの。
// 型ごとに1つだけ作られ、アドレスがその型の目印になる。
// リフレクションのプロパティ(メンバー変数の一覧)も、ここに持つ。
#pragma once

#include "core/Property.h"

#include <cstddef>
#include <cstring> // std::strcmp
#include <vector>

namespace husky::core
{
    // 動作スレッド: 作られた後は読み取り専用なので、どこからでも読んでよい
    class TypeInfo final
    {
    public:
        // プロパティを登録する関数の型。TypeInfo を作るときに1回だけ呼ばれる。
        // void (*)(TypeInfo&) は「TypeInfo& を受け取り、何も返さない関数へのポインタ」(C# の Action<TypeInfo> に近い)。
        using RegisterPropertiesFunction = void (*)(TypeInfo&);

        // name: 型の名前(例: "Enemy")。parent: 親の型の型情報。一番上の型(Object)では nullptr。
        // registerProperties: この型のプロパティを登録する関数。プロパティが無ければ nullptr。
        TypeInfo(const char* name, const TypeInfo* parent, RegisterPropertiesFunction registerProperties = nullptr)
            : m_name(name)
            , m_parent(parent)
        {
            if (registerProperties != nullptr)
            {
                registerProperties(*this);
            }
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

        // プロパティを1つ登録する。HUSKY_PROPERTY マクロから呼ばれる。
        void AddProperty(const char* name, PropertyType type, std::size_t offset)
        {
            m_properties.emplace_back(name, type, offset); // emplace_back: 引数から、その場で要素を作って末尾に足す
        }

        // この型で登録したプロパティだけを返す(親の型のものは含まない)。
        const std::vector<Property>& GetOwnProperties() const { return m_properties; }

        // 名前でプロパティを探す。見つからなければ、親の型を順に探す。それでも無ければ nullptr。
        const Property* FindProperty(const char* name) const
        {
            for (const TypeInfo* type = this; type != nullptr; type = type->m_parent)
            {
                for (const Property& property : type->m_properties)
                {
                    if (std::strcmp(property.GetName(), name) == 0)
                    {
                        return &property;
                    }
                }
            }
            return nullptr;
        }

        // 親の型のものも含めて、すべてのプロパティについて function(property) を呼ぶ。
        // 一番上の親から順に呼ぶ(Object → Actor → Enemy の順)。GC やシリアライズで使う。
        // template <typename Function>: function にはラムダ式(C# の (p) => { ... })を渡せる。
        template <typename Function>
        void ForEachProperty(Function&& function) const
        {
            if (m_parent != nullptr)
            {
                m_parent->ForEachProperty(function);
            }
            for (const Property& property : m_properties)
            {
                function(property);
            }
        }

    private:
        const char* m_name;
        const TypeInfo* m_parent;
        std::vector<Property> m_properties;
    };
}
