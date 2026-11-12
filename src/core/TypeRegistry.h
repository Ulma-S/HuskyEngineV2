// 型の登録簿: 型の名前(文字列)から、その型のオブジェクトを作れるようにする。
// ファイルから読み込むときに、"type": "SpriteComponent" のような文字列から、本物のオブジェクトを作るのに使う。
// C# なら、リフレクションで Type.GetType("名前") と Activator.CreateInstance が使えるが、
// C++ には「名前から型を探す」仕組みが無いので、作れる型を自分で登録しておく。
//
// 使い方:
//   TypeRegistry types;
//   types.Register<Enemy>();                                 // 起動時に、作りたい型を全部登録する
//   ...
//   Object* object = types.Create("Enemy", registry);        // 名前から作る(持ち主は registry)
//   if (Enemy* enemy = Cast<Enemy>(object)) { ... }
//
// なぜ自動で登録しないのか:
//   C++ でも「グローバル変数の初期化で自動登録する」書き方はあるが、
//   (1) 静的ライブラリの中のものは、どこからも使われないとリンカに捨てられて登録されないことがある、
//   (2) グローバル変数の初期化の順番が決まっておらず、登録簿より先に登録しようとして壊れることがある、
//   という落とし穴がある。最初は、登録の場所が目で見える「明示的な登録」にする。
//
// 動作スレッド: 登録は起動時にゲームスレッドで。登録の後の検索と作成は、ゲームスレッドで。
#pragma once

#include "core/ObjectRegistry.h"

#include <cstring>
#include <string_view>
#include <type_traits>
#include <vector>

namespace husky::core
{
    class TypeRegistry final
    {
    public:
        // オブジェクトを1つ作る関数の型(作れなければ nullptr を返す)。
        using CreateFunction = Object* (*)(ObjectRegistry&);

        // T を登録する。戻り値: 登録できたら true。同じ名前がすでにあれば false(何もしない)。
        // T は Object の子孫で、引数なしで作れる(デフォルトコンストラクタがある)こと。違えばコンパイルエラー。
        template <typename T>
        bool Register()
        {
            static_assert(std::is_base_of_v<Object, T>, "登録できるのは Object の子孫だけです");
            static_assert(std::is_default_constructible_v<T>,
                          "ファイルから作るには、引数なしのコンストラクタが要ります");
            // [](ObjectRegistry& registry) -> Object* { ... } は、何も取り込まない(外の変数を使わない)ラムダ式。
            // 取り込みが無いラムダ式は、普通の関数のポインタ(CreateFunction)に変換できる。
            return Add(T::StaticType(), [](ObjectRegistry& registry) -> Object* { return registry.Create<T>(); });
        }

        // 名前で型を探す。無ければ nullptr。
        const TypeInfo* Find(std::string_view name) const
        {
            const Entry* entry = FindEntry(name);
            return entry != nullptr ? entry->type : nullptr;
        }

        // 名前の型のオブジェクトを registry に作る。登録されていない名前か、メモリが足りなければ nullptr。
        Object* Create(std::string_view name, ObjectRegistry& registry) const
        {
            const Entry* entry = FindEntry(name);
            return entry != nullptr ? entry->create(registry) : nullptr;
        }

        // 登録されている型の数。
        std::size_t GetCount() const { return m_entries.size(); }

    private:
        struct Entry
        {
            const TypeInfo* type;
            CreateFunction create;
        };

        bool Add(const TypeInfo& type, CreateFunction create)
        {
            if (FindEntry(type.GetName()) != nullptr)
            {
                return false;
            }
            m_entries.push_back(Entry{&type, create});
            return true;
        }

        // 型の数は多くても数百なので、先頭から順に探す(並べ替えや辞書は、遅いと分かってから)。
        const Entry* FindEntry(std::string_view name) const
        {
            for (const Entry& entry : m_entries)
            {
                if (name == entry.type->GetName())
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        std::vector<Entry> m_entries;
    };
}
