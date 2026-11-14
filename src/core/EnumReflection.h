// enum のリフレクション(SF-3b): enum の値と名前の表を登録し、プロパティにできるようにする。
//
// C++ には、enum の値の名前("Blue")を実行中に知る仕組みが無い(C# の Enum.GetName にあたるものが無い)。
// そこで、構造体(StructReflection.h)と同じく、型の外から名前の表を登録する:
//
//   enum class Team { Red, Blue, Green };
//   HUSKY_ENUM_BEGIN(Team)          // Team と同じ名前空間に書く(ADL で見つけるため)
//       HUSKY_ENUM_VALUE(Red)
//       HUSKY_ENUM_VALUE(Blue)
//       HUSKY_ENUM_VALUE(Green)
//   HUSKY_ENUM_END()
//
//   class Unit : public Actor { ... Team team = Team::Red; ... HUSKY_PROPERTY(team) ... };
//
// 保存は名前("team": "Blue")。enum の並びや数値を後から変えても、ファイルの値がずれない。
// インスペクタは、名前の選択肢から選ぶ。登録していない値は、保存では数で書く(読むときは、登録した値だけを受け付ける)。
//
// 中身(EnumInfo)は、enum の型を知らずに値を読み書きする関数(get / set)を持つ。シリアライザやインスペクタは、
// どの enum かを知らずに扱える(配列の ArrayInfo と同じ考え方)。enum の大きさ(int、std::uint8_t など)は問わない。
//
// 動作スレッド: どこでもよい(登録後は読み取り専用)。値の読み書きはゲームスレッドで。
#pragma once

#include <cstdint>
#include <type_traits> // std::false_type, std::void_t
#include <string_view>
#include <vector>

namespace husky::core
{
    struct EnumInfo
    {
        struct Entry
        {
            const char* name;   // 値の名前("Blue")
            std::int64_t value; // 値(数)
        };

        const char* name = "";      // enum の型の名前("Team")
        std::vector<Entry> entries; // 登録した値(登録した順。インスペクタの選択肢の順)
        std::int64_t (*get)(const void* address) = nullptr;     // address の enum の値を、数として読む
        void (*set)(void* address, std::int64_t value) = nullptr; // address の enum に、数を書く

        // 値の名前。登録していない値なら nullptr。
        const char* FindName(std::int64_t value) const
        {
            for (const Entry& entry : entries)
            {
                if (entry.value == value)
                {
                    return entry.name;
                }
            }
            return nullptr;
        }

        // 名前の値。登録していない名前なら nullptr。
        const Entry* FindByName(std::string_view text) const
        {
            for (const Entry& entry : entries)
            {
                if (text == entry.name)
                {
                    return &entry;
                }
            }
            return nullptr;
        }
    };

    // --- enum のリフレクションか(Property.h の IsReflectedStruct と同じく、ADL で関数が見つかるかで見分ける) ---
    template <typename T, typename = void>
    struct IsReflectedEnum : std::false_type
    {
    };
    template <typename T>
    struct IsReflectedEnum<T, std::void_t<decltype(HuskyEnumType(static_cast<const T*>(nullptr)))>> : std::true_type
    {
    };

    // 登録された enum T の名前の表。登録されていない型なら nullptr。
    template <typename T>
    const EnumInfo* EnumInfoOf()
    {
        if constexpr (IsReflectedEnum<T>::value)
        {
            return &HuskyEnumType(static_cast<const T*>(nullptr));
        }
        else
        {
            return nullptr;
        }
    }
}

// enum の名前の表を返す関数の宣言だけを書く(ヘッダ用。HR-1)。中身は .cpp の HUSKY_ENUM_BEGIN 〜 END。
// 理由は StructReflection.h の HUSKY_STRUCT_DECLARE と同じ(DLL の中に表を1つだけ作るため)。
#define HUSKY_ENUM_DECLARE(EnumType) const ::husky::core::EnumInfo& HuskyEnumType(const EnumType*);

// enum の登録を始める。中身は「HuskyEnumType(const EnumType*) という関数」で、最初に呼ばれたときに1回だけ表を作る。
// 書く場所は .cpp(ヘッダには HUSKY_ENUM_DECLARE)。
// get / set は、enum の実際の大きさで読み書きする(static_cast で数と enum を行き来する)。
#define HUSKY_ENUM_BEGIN(EnumType)                                                                \
    const ::husky::core::EnumInfo& HuskyEnumType(const EnumType*)                                \
    {                                                                                            \
        using ThisEnum = EnumType;                                                               \
        static const ::husky::core::EnumInfo info = [] {                                         \
            ::husky::core::EnumInfo e;                                                           \
            e.name = #EnumType;                                                                  \
            e.get = [](const void* address) -> std::int64_t {                                    \
                return static_cast<std::int64_t>(*static_cast<const ThisEnum*>(address));        \
            };                                                                                   \
            e.set = [](void* address, std::int64_t value) {                                      \
                *static_cast<ThisEnum*>(address) = static_cast<ThisEnum>(value);                 \
            };

// 値を1つ登録する(名前は、書いたとおりの文字列になる)。
#define HUSKY_ENUM_VALUE(ValueName) e.entries.push_back({#ValueName, static_cast<std::int64_t>(ThisEnum::ValueName)});

// enum の登録を終える。
#define HUSKY_ENUM_END()                                                                          \
            return e;                                                                            \
        }();                                                                                     \
        return info;                                                                             \
    }
