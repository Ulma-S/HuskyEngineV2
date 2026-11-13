// すべてのゲームオブジェクトの基底クラスと、型情報を定義するマクロ。
//
// 使い方:
//   class Enemy : public Actor
//   {
//       HUSKY_OBJECT(Enemy, Actor)   // クラスの最初に1行書く
//   public:
//       std::int32_t hp = 10;
//       ObjectHandle target;
//       void Heal(std::int32_t amount) { hp += amount; }
//
//       HUSKY_REFLECTION_BEGIN()      // リフレクションに見せるメンバーを登録する(クラスの最後に書く)
//           HUSKY_PROPERTY(hp)
//           HUSKY_PROPERTY(target)
//           HUSKY_METHOD(Heal)
//       HUSKY_REFLECTION_END()
//   };
//
//   Object* object = ...;
//   if (Enemy* enemy = Cast<Enemy>(object)) { enemy->hp -= 1; }   // C# の「object as Enemy」
#pragma once

#include "core/Handle.h"
#include "core/TypeInfo.h"

#include <cstddef> // offsetof

// 型情報を定義するマクロ。クラスの宣言の最初に書く。
//   ThisClass: このクラスの名前。ParentClass: 親クラスの名前(HUSKY_OBJECT を書いたクラスであること)。
//
// マクロは、コンパイルの前に「文字の置き換え」でコードを作る仕組み(C# にはない)。
//   - 各行の末尾の \ は「次の行も同じマクロの続き」という意味。
//   - #ThisClass は、ThisClass に渡した名前を文字列にする(Enemy → "Enemy")。
//   - ::husky::core:: と先頭に :: を付けるのは、どの namespace の中で使われても同じ型を指すようにするため。
//
// 中身:
//   - Super: 親クラスの別名(C# の base にあたるものを、Super::Foo() のように書ける)。
//   - ThisType: このクラス自身の別名。HUSKY_PROPERTY の中で使う。
//   - StaticType(): この型の型情報を返す。関数の中の static 変数なので、最初に呼ばれたときに1回だけ作られる。
//     作るときに、このクラスの RegisterMembers(プロパティとメソッドの登録)を1回だけ呼ぶ。
//     ただし、このクラスが HUSKY_REFLECTION_BEGIN を書いていない場合、ThisClass::RegisterMembers は
//     親クラスのものを指してしまう(C++ では、static 関数も親から引き継がれるため)。
//     そのまま呼ぶと親のプロパティを二重に登録するので、親と同じ関数なら nullptr(登録なし)を渡す。
//   - GetType(): オブジェクトの実際の型の型情報を返す(virtual なので、基底クラスのポインタから呼んでも正しい型が返る)。
// マクロの最後で private: に戻す(class の既定と同じ)。メンバーを公開したいときは、マクロの後に public: と書く。
#define HUSKY_OBJECT(ThisClass, ParentClass)                                                      \
public:                                                                                          \
    using Super = ParentClass;                                                                   \
    using ThisType = ThisClass;                                                                  \
    static const ::husky::core::TypeInfo& StaticType()                                           \
    {                                                                                            \
        static const ::husky::core::TypeInfo info(                                               \
            #ThisClass,                                                                          \
            &ParentClass::StaticType(),                                                          \
            &ThisClass::RegisterMembers != &ParentClass::RegisterMembers                         \
                ? &ThisClass::RegisterMembers                                                    \
                : nullptr);                                                                      \
        return info;                                                                             \
    }                                                                                            \
    const ::husky::core::TypeInfo& GetType() const override                                      \
    {                                                                                            \
        return StaticType();                                                                     \
    }                                                                                            \
                                                                                                 \
private:

// リフレクションの登録を始める。BEGIN と END の間に HUSKY_PROPERTY と HUSKY_METHOD を並べる。
// 中身は「RegisterMembers という static 関数」の始まり。END がその関数を閉じる。
// END の後は public: のままなので、クラスの最後に書くとよい。
#define HUSKY_REFLECTION_BEGIN()                                                                  \
public:                                                                                          \
    static void RegisterMembers(::husky::core::TypeInfo& info)                                   \
    {                                                                                            \
        (void)info; /* 登録が1つも無いときの「使っていない引数」の警告を消す */

// メンバー変数1つをプロパティとして登録する。
//   #member: 名前の文字列("hp")。
//   decltype(ThisType::member): メンバーの型(std::int32_t など)。対応していない型ならコンパイルエラー。
//   offsetof(ThisType, member): オブジェクトの先頭から、このメンバーまでのバイト数。
//   DetailTypeOf: メンバーが登録された構造体ならその型情報、AssetRef<T> なら T の型情報(それ以外は nullptr)。
//   ArrayInfoOf: メンバーが std::vector<E> なら、配列の中身の扱い方(それ以外は nullptr。SF-2)。
// 構造体の登録(HUSKY_STRUCT_BEGIN)の中でも、同じマクロをそのまま使う。
#define HUSKY_PROPERTY(member)                                                                    \
    info.AddProperty(#member,                                                                    \
                     ::husky::core::PropertyTypeOf<decltype(ThisType::member)>(),                \
                     offsetof(ThisType, member),                                                 \
                     ::husky::core::DetailTypeOf<decltype(ThisType::member)>(),                  \
                     ::husky::core::ArrayInfoOf<decltype(ThisType::member)>());

// メンバー関数1つをメソッドとして登録する。
//   &ThisType::method: メンバー関数のアドレス。同じ名前の関数が複数ある(オーバーロード)と、
//   どれのことか決められずにコンパイルエラーになる(オーバーロードは非対応)。
//   引数や戻り値に対応していない型があっても、コンパイルエラーになる。
#define HUSKY_METHOD(method) info.AddMethod(::husky::core::MakeMethod<&ThisType::method>(#method));

// リフレクションの登録を終える。
#define HUSKY_REFLECTION_END() }

namespace husky::core
{
    class Object;
    class ObjectRegistry;

    // オブジェクトを指すハンドル。外部(スクリプトなど)に渡すときや、長く持つときはこれを使う。
    using ObjectHandle = Handle<Object>;

    // すべてのゲームオブジェクトの基底クラス。
    // 生成と破棄は ObjectRegistry が行う(registry.Create<T>() で作り、Destroy() で破棄を予約する)。
    // 動作スレッド: ゲームスレッドのみ
    class Object
    {
        // friend: ObjectRegistry には private なメンバー(m_handle など)を触らせる。
        // ハンドルや持ち主のレジストリは、作ったレジストリだけが設定してよいため。
        friend class ObjectRegistry;

    public:
        Object() = default;

        // デストラクタを virtual にする。
        // これが無いと、Object* として持っている Enemy を破棄したときに、Enemy のデストラクタが呼ばれない。
        // 継承して使うクラスでは、基底クラスのデストラクタは必ず virtual にする(C++ の決まりごと)。
        virtual ~Object() = default;

        // オブジェクトは「その1つ」に意味があるので、コピーを禁止する(コピーすると別の個体ができてしまう)。
        Object(const Object&) = delete;
        Object& operator=(const Object&) = delete;

        // Object 自身の型情報。親が無い一番上の型なので、マクロを使わずに直接書く。
        static const TypeInfo& StaticType()
        {
            static const TypeInfo info("Object", nullptr);
            return info;
        }

        // プロパティの登録(Object にはプロパティが無いので空)。
        // 子クラスは HUSKY_REFLECTION_BEGIN で同じ名前の関数を作る。作らなかった子クラスでは、これが引き継がれる。
        static void RegisterMembers(TypeInfo&) {}

        // このオブジェクトの実際の型の型情報。子クラスでは HUSKY_OBJECT が上書き(override)する。
        virtual const TypeInfo& GetType() const { return StaticType(); }

        // このオブジェクトが T 型か、T の子孫なら true(C# の「object is T」)。
        template <typename T>
        bool IsA() const
        {
            return GetType().IsA(T::StaticType());
        }

        // このオブジェクトを指すハンドル。ObjectRegistry で作ったものでなければ、どこも指さないハンドル。
        ObjectHandle GetHandle() const { return m_handle; }

        // 破棄を予約する。実際に破棄されるのは、フレームの終わりの ObjectRegistry::FlushDestroyed のとき。
        // それまではメモリは有効で、ハンドルからも引ける(IsPendingDestroy で予約済みかが分かる)。
        // 何度呼んでもよい。ObjectRegistry で作ったものでなければ何もしない。
        void Destroy();

        // Destroy が呼ばれ、破棄を待っているなら true。
        bool IsPendingDestroy() const { return m_pendingDestroy; }

        // 「読むだけ」の印(SD-2)。読み込んだアセット(ScriptableData の中身など)に、ローダが読み終えたときに付ける。
        // プロパティの書く入口(ObjectSerializer::ReadProperties。エディタの property.set もここを通る。将来のスクリプトも)が
        // この印を見て、書き込みを断ってエラーにする。一度付けたら外さない(読み込んだアセットは変わらないデータのため)。
        // C++ のコードからの直接の書き込みは止められない(const のポインタで渡すのと、開発ビルドの控えとの比べで見つける)。
        void MarkReadOnly() { m_readOnly = true; }
        bool IsReadOnly() const { return m_readOnly; }

        // 作ったレジストリ(持ち主)。作っていなければ nullptr。シリアライザが、型の付いた参照(ObjectRef)の相手を引いて
        // 型を確かめるのに使う(子クラス向けの GetRegistry は protected のまま)。
        ObjectRegistry* GetOwnerRegistry() const { return m_registry; }

    protected:
        // このオブジェクトを作ったレジストリ。レジストリで作っていなければ nullptr。
        // 子クラス(Scene など)が、ほかのオブジェクトを作ったり引いたりするのに使う。
        // 注意: レジストリが設定するのはコンストラクタの後なので、コンストラクタの中では nullptr。
        // protected: 外からは見えないが、子クラスからは使える(C# の protected と同じ)。
        ObjectRegistry* GetRegistry() const { return m_registry; }

    private:
        ObjectHandle m_handle;         // 作ったレジストリが設定する
        ObjectRegistry* m_registry = nullptr;  // 作ったレジストリ(持ち主)。借りているだけで、所有はしていない
        bool m_pendingDestroy = false;
        bool m_readOnly = false;               // 読むだけの印(MarkReadOnly)
        bool m_gcMarked = false;              // GC のマークの印。CollectGarbage の中だけで使う
    };

    // object が T 型(か T の子孫)なら T* として返し、違えば nullptr を返す(C# の「object as T」)。
    // 型を確かめてから static_cast(確認なしの型変換)するので、安全に変換できる。
    template <typename T>
    T* Cast(Object* object)
    {
        return (object != nullptr && object->IsA<T>()) ? static_cast<T*>(object) : nullptr;
    }

    // const 版。const Object* からは const T* が返る(中身は書き換えられない)。
    template <typename T>
    const T* Cast(const Object* object)
    {
        return (object != nullptr && object->IsA<T>()) ? static_cast<const T*>(object) : nullptr;
    }
}
