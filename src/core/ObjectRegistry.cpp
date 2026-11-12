// ObjectRegistry.h と、Object の中でレジストリを使う部分の実装。
#include "core/ObjectRegistry.h"

#include <algorithm> // std::find, std::remove_if

namespace husky::core
{
    // --- Object ---

    void Object::Destroy()
    {
        if (m_registry == nullptr || m_pendingDestroy)
        {
            return; // レジストリで作ったものではないか、すでに予約済み
        }
        m_pendingDestroy = true;
        m_registry->RequestDestroy(*this); // *this: 自分自身(ポインタ this の指す先)
    }

    // --- ObjectRegistry ---

    ObjectRegistry::ObjectRegistry(std::size_t capacityBytes)
        : m_allocator(capacityBytes)
    {
    }

    // = default: メンバーが宣言と逆の順に破棄される。
    // まず m_objects が破棄され、中の unique_ptr のデリータが、残っているオブジェクトを片付けてメモリを返す。
    // 最後に m_allocator が破棄され、メモリの塊そのものが解放される。
    ObjectRegistry::~ObjectRegistry() = default;

    Object* ObjectRegistry::Resolve(ObjectHandle handle) const
    {
        const ObjectPtr* entry = m_objects.Get(ToSlot(handle));
        return entry != nullptr ? entry->get() : nullptr;
    }

    void ObjectRegistry::RequestDestroy(Object& object)
    {
        m_pendingDestroy.push_back(object.GetHandle());
    }

    std::size_t ObjectRegistry::FlushDestroyed()
    {
        std::size_t destroyedCount = 0;
        for (const ObjectHandle handle : m_pendingDestroy)
        {
            // 表から消すと、中の unique_ptr のデリータが、デストラクタを呼んでメモリをアロケータに返す。
            if (m_objects.Remove(ToSlot(handle)))
            {
                ++destroyedCount;
            }
        }
        m_pendingDestroy.clear();
        return destroyedCount;
    }

    // --- GC ---

    bool ObjectRegistry::AddRoot(ObjectHandle handle)
    {
        if (Resolve(handle) == nullptr)
        {
            return false; // 存在しないオブジェクトは根にできない
        }
        // std::find: 範囲の中から値を探す。見つからなければ end() を返す(C# の List.Contains に近い)。
        if (std::find(m_roots.begin(), m_roots.end(), handle) == m_roots.end())
        {
            m_roots.push_back(handle);
        }
        return true;
    }

    bool ObjectRegistry::RemoveRoot(ObjectHandle handle)
    {
        const auto it = std::find(m_roots.begin(), m_roots.end(), handle);
        if (it == m_roots.end())
        {
            return false;
        }
        m_roots.erase(it);
        return true;
    }

    std::size_t ObjectRegistry::CollectGarbage()
    {
        // 1. 準備: 全オブジェクトの印を消す。
        //    (ラムダ式の引数を使わないときは、名前を書かずに型だけ書くと「使っていない引数」の警告が出ない)
        m_objects.ForEach([](Handle<ObjectPtr>, ObjectPtr& object) {
            object->m_gcMarked = false;
        });

        // 2. マーク: 根から届くオブジェクトに印を付ける。
        //    「これから中を調べるオブジェクト」を stack に積み、1つずつ取り出して、その参照先を積む。
        //    関数の再帰でたどると、参照の鎖が長いときに関数呼び出しの深さが限界を超えるので、自前のスタックを使う。
        //    stack はメンバーの m_gcStack を使い回す(毎回作ると、大きくなるたびに確保し直すため)。
        //    std::vector<Object*>& は参照(別名)で、ここで stack と書けば m_gcStack のこと。
        std::vector<Object*>& stack = m_gcStack;
        stack.clear(); // 中身を空にする。確保した容量はそのまま残る

        // 印を付けて stack に積む。すでに印があれば何もしない(循環参照でも無限に回らない)。
        // [&stack] はラムダ式が外の stack を参照で使うという意味。
        auto markAndPush = [&stack](Object* object) {
            if (object != nullptr && !object->m_gcMarked)
            {
                object->m_gcMarked = true;
                stack.push_back(object);
            }
        };

        for (const ObjectHandle root : m_roots)
        {
            markAndPush(Resolve(root));
        }

        while (!stack.empty())
        {
            Object* object = stack.back(); // 末尾を取り出す
            stack.pop_back();

            // このオブジェクトの全プロパティ(親クラスの分も含む)のうち、参照フラグの付いたものをたどる。
            // ForEachReference は、単独のハンドルでも配列でも、参照先のハンドルを1つずつ渡してくれる。
            object->GetType().ForEachProperty([&](const Property& property) {
                property.ForEachReference(*object, [&](ObjectHandle reference) {
                    markAndPush(Resolve(reference)); // 回収済みの相手なら Resolve が nullptr を返すだけ
                });
            });
        }

        // 3. スイープ: 印の無いオブジェクトを集めてから、まとめて破棄する。
        //    (ForEach で回っている最中に Remove すると表の形が変わるので、先に集める)
        std::vector<ObjectHandle>& garbage = m_gcGarbage; // stack と同じく使い回す
        garbage.clear();
        m_objects.ForEach([&garbage](Handle<ObjectPtr>, ObjectPtr& object) {
            if (!object->m_gcMarked)
            {
                garbage.push_back(object->GetHandle());
            }
        });
        for (const ObjectHandle handle : garbage)
        {
            m_objects.Remove(ToSlot(handle)); // デリータが、デストラクタを呼んでメモリを返す
        }

        // 4. 後片付け: もう存在しないオブジェクトを指す根を外す(Destroy で破棄された根など)。
        //    erase と remove_if の組み合わせは、vector から条件に合う要素を消す定番の書き方。
        m_roots.erase(std::remove_if(m_roots.begin(), m_roots.end(),
                                     [this](ObjectHandle root) { return Resolve(root) == nullptr; }),
                      m_roots.end());

        return garbage.size();
    }
}
