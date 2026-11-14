// 構造体のリフレクション: Object ではない普通の構造体(値)のメンバーを登録し、プロパティとして使えるようにする。
// 登録した構造体は、Object の HUSKY_PROPERTY にそのまま書ける。保存すると、中のメンバーが入れ子で保存される。
//
// 使い方(登録は、構造体の型の外に書く):
//   namespace mygame                         ← 構造体と「同じ名前空間」の中に書くこと(下の「しくみ」を参照)
//   {
//       struct EnemyStats { std::int32_t attack = 5; float defense = 1.0f; };
//
//       HUSKY_STRUCT_BEGIN(EnemyStats)
//           HUSKY_PROPERTY(attack)
//           HUSKY_PROPERTY(defense)
//       HUSKY_STRUCT_END()
//   }
//
//   class Enemy : public Actor { ... EnemyStats stats; ... HUSKY_PROPERTY(stats) ... };
//
// 型の外に書けるので、自分で書き換えられない型(外部ライブラリの型)や、ほかのモジュールの型も登録できる。
// ただし、外から触るので、登録するメンバーは public であること。
// 構造体の中に、登録した別の構造体を入れ子にしてもよい(中の構造体を先に登録すること)。
//
// しくみ:
//   HUSKY_STRUCT_BEGIN(T) 〜 END は、「HuskyStructType(const T*) という関数」を作る。中で型情報(TypeInfo)を1つ作って返す。
//   core の側(Property.h の IsReflectedStruct)は、この関数が呼べるかどうかで「登録された構造体か」を見分ける。
//   関数は、引数の型 T と同じ名前空間に置く必要がある(C++ が引数の型の名前空間の中を探す、ADL という仕組みで見つけるため)。
//   登録を見えるようにしておかずに HUSKY_PROPERTY(stats) と書くと、「この型はプロパティにできません」のコンパイルエラーになる。
//
// 動作スレッド: 型情報は最初に使われたときに作られる(ゲームスレッドで)。
#pragma once

#include "core/Object.h" // HUSKY_PROPERTY
#include "core/TypeInfo.h"

// 構造体の登録を始める。StructType: 構造体の名前(名前空間を付けずに書く。HUSKY_STRUCT_BEGIN は同じ名前空間の中に書くため)。
//   書く場所は .cpp(HR-1)。ヘッダに書く inline の関数だと、中の static な型情報が exe と DLL のそれぞれに
//   別々にできてしまう(Object.h の HUSKY_OBJECT の説明)。ほかのファイルから使う構造体は、ヘッダに
//   HUSKY_STRUCT_DECLARE(StructType) と書いて関数の宣言だけを見せる(1つの .cpp の中だけで使う構造体なら要らない)。
//   Registrar: 関数の中だけで使う小さなクラス(ローカルクラス)。HUSKY_PROPERTY が使う ThisType と info を用意する。
// 構造体の型情報を返す関数の宣言だけを書く(ヘッダ用)。中身は .cpp の HUSKY_STRUCT_BEGIN 〜 END。
#define HUSKY_STRUCT_DECLARE(StructType) const ::husky::core::TypeInfo& HuskyStructType(const StructType*);

#define HUSKY_STRUCT_BEGIN(StructType)                                                            \
    const ::husky::core::TypeInfo& HuskyStructType(const StructType*)                            \
    {                                                                                            \
        struct Registrar                                                                         \
        {                                                                                        \
            static const char* Name() { return #StructType; }                                    \
            static void RegisterMembers(::husky::core::TypeInfo& info)                           \
            {                                                                                    \
                using ThisType = StructType;                                                     \
                (void)info;

// 構造体の登録を終える。ここで型情報を作る(関数の中の static なので、最初に呼ばれたときに1回だけ作られる)。
#define HUSKY_STRUCT_END()                                                                        \
            }                                                                                    \
        };                                                                                       \
        static const ::husky::core::TypeInfo info(Registrar::Name(), nullptr, &Registrar::RegisterMembers); \
        return info;                                                                             \
    }
