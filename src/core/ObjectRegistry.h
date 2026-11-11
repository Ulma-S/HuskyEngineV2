// ゲームオブジェクトの登録簿(レジストリ)。オブジェクトを作り、持ち主になり、破棄する。
// メモリの確保そのものは標準の仕組み(std::make_unique)に任せている。自前のメモリアロケータではない。
//
// 使い方:
//   ObjectRegistry registry;
//   Enemy* enemy = registry.Create<Enemy>();          // 作る(持ち主はレジストリ)
//   ObjectHandle handle = enemy->GetHandle();      // 長く持つときはハンドルで
//   ...
//   enemy->Destroy();                              // 破棄を予約(まだメモリは有効)
//   registry.FlushDestroyed();                         // フレームの終わりに、予約分をまとめて破棄
//   registry.Resolve<Enemy>(handle);                   // → nullptr(もう無い)
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/HandleTable.h"
#include "core/Object.h"

#include <cstddef>
#include <memory>      // std::unique_ptr, std::make_unique
#include <type_traits> // std::is_base_of_v
#include <utility>     // std::forward
#include <vector>

namespace husky::core
{
    class ObjectRegistry final
    {
    public:
        ObjectRegistry() = default;
        // レジストリを破棄すると、残っているオブジェクトもすべて破棄される(unique_ptr が自動で delete する)。
        ~ObjectRegistry();

        ObjectRegistry(const ObjectRegistry&) = delete;
        ObjectRegistry& operator=(const ObjectRegistry&) = delete;

        // T 型のオブジェクトを作り、ポインタを返す。持ち主はレジストリ。
        // args は T のコンストラクタにそのまま渡す。
        //
        // template <typename T, typename... Args> の「...」は可変長テンプレート。
        //   Create<Enemy>(1, "a") と書くと、Args は (int, const char*) になる。
        // Args&&... と std::forward は「受け取った引数を、コピーを増やさずにそのまま次へ渡す」ための定番の書き方。
        //
        // 返したポインタは、そのオブジェクトの破棄が確定する(FlushDestroyed)まで有効。
        // オブジェクト本体はメモリ上の固定の場所にあるので、ほかのオブジェクトを作っても動かない。
        template <typename T, typename... Args>
        T* Create(Args&&... args)
        {
            // T が Object の子孫であることを、コンパイル時に確かめる。
            static_assert(std::is_base_of_v<Object, T>, "ObjectRegistry で作れるのは Object の子孫だけです");

            // make_unique: new を書かずに、オブジェクトを作って unique_ptr に入れる。
            std::unique_ptr<T> object = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = object.get(); // 持ち主は unique_ptr のまま、中身のアドレスだけを取り出す

            // 表に入れる。表の中身は unique_ptr(オブジェクトへの矢印)なので、
            // 表(vector)が大きくなって引っ越しても、オブジェクト本体の場所は変わらない。
            // std::move: unique_ptr はコピーできないので、持ち主を表へ「移す」。
            const Handle<std::unique_ptr<Object>> slot = m_objects.Add(std::move(object));

            raw->m_handle = ObjectHandle{slot.index, slot.generation};
            raw->m_registry = this;
            return raw;
        }

        // ハンドルが指すオブジェクトを返す。もう無ければ nullptr。
        // 破棄を予約済み(IsPendingDestroy)でも、確定前なら返す。
        Object* Resolve(ObjectHandle handle) const;

        // 型付き版。T 型(か T の子孫)でなければ nullptr。
        template <typename T>
        T* Resolve(ObjectHandle handle) const
        {
            return Cast<T>(Resolve(handle));
        }

        // 予約された破棄を、まとめて確定する。フレームの終わりに1回呼ぶ。戻り値: 破棄した数。
        // 注意: デストラクタの中で、ほかのオブジェクトの Destroy を呼ばないこと。
        std::size_t FlushDestroyed();

        // 今あるオブジェクトの数(破棄を予約済みのものも含む)。
        std::size_t GetObjectCount() const { return m_objects.Size(); }

    private:
        friend class Object; // Object::Destroy から RequestDestroy を呼べるようにする

        // Object::Destroy から呼ばれる。破棄の予約リストに入れる。
        void RequestDestroy(Object& object);

        // Handle<Object> を、表の中で使っている Handle<unique_ptr<Object>> に変換する。
        static Handle<std::unique_ptr<Object>> ToSlot(ObjectHandle handle)
        {
            return Handle<std::unique_ptr<Object>>{handle.index, handle.generation};
        }

        // メンバーは、宣言と逆の順に破棄される(下から上へ)。
        // m_objects を後に宣言して先に破棄させ、オブジェクトのデストラクタが動いている間も、
        // m_pendingDestroy がまだ有効であるようにしておく。
        std::vector<ObjectHandle> m_pendingDestroy;     // 破棄を予約されたもの
        HandleTable<std::unique_ptr<Object>> m_objects; // 全オブジェクト。持ち主はこの表
    };
}
