// ゲームオブジェクトの登録簿(レジストリ)。オブジェクトを作り、持ち主になり、破棄する。
// オブジェクトのメモリは、レジストリが持つ自作のヒープアロケータ(FreeListAllocator)から借りる。
//
// 使い方:
//   ObjectRegistry registry;
//   Enemy* enemy = registry.Create<Enemy>();       // 作る(持ち主はレジストリ)
//   ObjectHandle handle = enemy->GetHandle();      // 長く持つときはハンドルで
//   ...
//   enemy->Destroy();                              // 破棄を予約(まだメモリは有効)
//   registry.FlushDestroyed();                     // フレームの終わりに、予約分をまとめて破棄
//   registry.Resolve<Enemy>(handle);               // → nullptr(もう無い)
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/FreeListAllocator.h"
#include "core/HandleTable.h"
#include "core/Object.h"

#include <cstddef>
#include <memory>      // std::unique_ptr
#include <new>         // placement new
#include <type_traits> // std::is_base_of_v
#include <utility>     // std::forward
#include <vector>

namespace husky::core
{
    // unique_ptr の「削除の仕方」(デリータ)。
    // 普通の unique_ptr は delete で片付けるが、アロケータから借りたメモリは delete してはいけない。
    // そこで「デストラクタを呼んでから、メモリをアロケータに返す」という片付け方を、ここで指定する。
    struct ObjectDeleter
    {
        FreeListAllocator* allocator = nullptr;
        void* memory = nullptr; // アロケータから借りたメモリの先頭(Free に渡すアドレス)

        // unique_ptr が中身を片付けるときに呼ぶ。
        void operator()(Object* object) const
        {
            // デストラクタの明示的な呼び出し。メモリは解放せず、オブジェクトの後片付け(~Enemy など)だけを行う。
            // virtual なので、Object* から呼んでも、実際の型のデストラクタが呼ばれる。
            object->~Object();
            allocator->Free(memory);
        }
    };

    // アロケータから借りたメモリの上にあるオブジェクトを持つ unique_ptr。
    using ObjectPtr = std::unique_ptr<Object, ObjectDeleter>;

    class ObjectRegistry final
    {
    public:
        // オブジェクト用のメモリの既定の大きさ(16MB)。
        static constexpr std::size_t DefaultCapacityBytes = 16 * 1024 * 1024;

        // capacityBytes: オブジェクトに使うメモリの大きさ。これを使い切ると Create が nullptr を返す。
        explicit ObjectRegistry(std::size_t capacityBytes = DefaultCapacityBytes);
        // レジストリを破棄すると、残っているオブジェクトもすべて破棄される。
        ~ObjectRegistry();

        ObjectRegistry(const ObjectRegistry&) = delete;
        ObjectRegistry& operator=(const ObjectRegistry&) = delete;

        // T 型のオブジェクトを作り、ポインタを返す。持ち主はレジストリ。メモリが足りなければ nullptr。
        // args は T のコンストラクタにそのまま渡す。
        //
        // template <typename T, typename... Args> の「...」は可変長テンプレート。
        //   Create<Enemy>(1, "a") と書くと、Args は (int, const char*) になる。
        // Args&&... と std::forward は「受け取った引数を、コピーを増やさずにそのまま次へ渡す」ための定番の書き方。
        //
        // 返したポインタは、そのオブジェクトの破棄が確定する(FlushDestroyed / CollectGarbage)まで有効。
        // オブジェクト本体はアロケータの中の固定の場所にあるので、ほかのオブジェクトを作っても動かない。
        template <typename T, typename... Args>
        T* Create(Args&&... args)
        {
            // T が Object の子孫であることと、アラインメントが対応範囲内であることを、コンパイル時に確かめる。
            static_assert(std::is_base_of_v<Object, T>, "ObjectRegistry で作れるのは Object の子孫だけです");
            static_assert(alignof(T) <= FreeListAllocator::MaxAlignment, "アラインメントが大きすぎます");

            // 1. メモリを借りる(まだオブジェクトにはなっていない、ただのバイト列)。
            void* memory = m_allocator.Allocate(sizeof(T), alignof(T));
            if (memory == nullptr)
            {
                return nullptr;
            }

            // 2. placement new: 借りたメモリの上で、T のコンストラクタだけを呼ぶ(メモリの確保はしない)。
            //    CLAUDE.md で許可した例外。生の new(メモリの確保)とは別物。
            T* object = new (memory) T(std::forward<Args>(args)...);

            // 3. 片付け方(デリータ)を添えて unique_ptr に入れ、表に入れる。
            //    表の中身は unique_ptr(オブジェクトへの矢印)なので、表が大きくなっても本体の場所は変わらない。
            const Handle<ObjectPtr> slot = m_objects.Add(ObjectPtr(object, ObjectDeleter{&m_allocator, memory}));

            object->m_handle = ObjectHandle{slot.index, slot.generation};
            object->m_registry = this;
            return object;
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

        // オブジェクトのメモリを貸しているアロケータ(使用量などの確認用)。
        const FreeListAllocator& GetAllocator() const { return m_allocator; }

        // --- GC(マーク&スイープ) ---
        //
        // 根(ルート): GC が「必ず生かしておく」出発点。ワールドやアセットのキャッシュなどを登録する。
        // 根から、参照フラグの付いたプロパティ(登録した ObjectHandle)をたどって届くオブジェクトは生き残り、
        // 届かないオブジェクトは CollectGarbage で破棄される。
        // プロパティとして登録していない ObjectHandle は「弱い参照」で、相手を生かしておく力は無い
        // (相手が回収されたかどうかは、Resolve が nullptr を返すことで分かる)。

        // 根に加える。すでに根なら何もしない。戻り値: そのオブジェクトが存在すれば true。
        bool AddRoot(ObjectHandle handle);

        // 根から外す。戻り値: 根だったら true。
        bool RemoveRoot(ObjectHandle handle);

        // 根から届かないオブジェクトを、すべて破棄する。戻り値: 破棄した数。
        // フレームの終わりに、FlushDestroyed の後で、必要なときだけ呼ぶ(停止型: 終わるまで戻らない)。
        // 注意: デストラクタの中で、ほかのオブジェクトの Destroy を呼ばないこと。
        std::size_t CollectGarbage();

        // 今の根の数。
        std::size_t GetRootCount() const { return m_roots.size(); }

    private:
        friend class Object; // Object::Destroy から RequestDestroy を呼べるようにする

        // Object::Destroy から呼ばれる。破棄の予約リストに入れる。
        void RequestDestroy(Object& object);

        // Handle<Object> を、表の中で使っている Handle<ObjectPtr> に変換する。
        static Handle<ObjectPtr> ToSlot(ObjectHandle handle) { return Handle<ObjectPtr>{handle.index, handle.generation}; }

        // メンバーは、宣言と逆の順に破棄される(下から上へ)。
        // - m_allocator を一番上に置き、一番最後に破棄させる。オブジェクトの片付け(デリータ)がアロケータを使うため。
        // - m_objects を一番下に置き、一番最初に破棄させる。オブジェクトのデストラクタが動いている間も、
        //   ほかのメンバー(m_pendingDestroy など)がまだ有効であるようにするため。
        FreeListAllocator m_allocator;              // オブジェクトのメモリの貸し出し元
        std::vector<ObjectHandle> m_pendingDestroy; // 破棄を予約されたもの
        std::vector<ObjectHandle> m_roots;          // GC の根
        HandleTable<ObjectPtr> m_objects;           // 全オブジェクト。持ち主はこの表
    };
}
