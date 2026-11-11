// すべてのゲームオブジェクトの基底クラスと、型情報を定義するマクロ。
//
// 使い方:
//   class Enemy : public Actor
//   {
//       HUSKY_OBJECT(Enemy, Actor)   // クラスの最初に1行書く
//   public:
//       int hp = 10;
//   };
//
//   Object* object = ...;
//   if (Enemy* enemy = Cast<Enemy>(object)) { enemy->hp -= 1; }   // C# の「object as Enemy」
#pragma once

#include "core/Handle.h"
#include "core/TypeInfo.h"

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
//   - StaticType(): この型の型情報を返す。関数の中の static 変数なので、最初に呼ばれたときに1回だけ作られる。
//   - GetType(): オブジェクトの実際の型の型情報を返す(virtual なので、基底クラスのポインタから呼んでも正しい型が返る)。
// マクロの最後で private: に戻す(class の既定と同じ)。メンバーを公開したいときは、マクロの後に public: と書く。
#define HUSKY_OBJECT(ThisClass, ParentClass)                                                      \
public:                                                                                          \
    using Super = ParentClass;                                                                   \
    static const ::husky::core::TypeInfo& StaticType()                                           \
    {                                                                                            \
        static const ::husky::core::TypeInfo info(#ThisClass, &ParentClass::StaticType());       \
        return info;                                                                             \
    }                                                                                            \
    const ::husky::core::TypeInfo& GetType() const override                                      \
    {                                                                                            \
        return StaticType();                                                                     \
    }                                                                                            \
                                                                                                 \
private:

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

    private:
        ObjectHandle m_handle;         // 作ったレジストリが設定する
        ObjectRegistry* m_registry = nullptr;  // 作ったレジストリ(持ち主)。借りているだけで、所有はしていない
        bool m_pendingDestroy = false;
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
