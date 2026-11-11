// ObjectHeap.h と、Object の中でヒープを使う部分の実装。
#include "core/ObjectHeap.h"

namespace husky::core
{
    // --- Object ---

    void Object::Destroy()
    {
        if (m_heap == nullptr || m_pendingDestroy)
        {
            return; // ヒープで作ったものではないか、すでに予約済み
        }
        m_pendingDestroy = true;
        m_heap->RequestDestroy(*this); // *this: 自分自身(ポインタ this の指す先)
    }

    // --- ObjectHeap ---

    // = default: m_objects(HandleTable)が破棄され、中の unique_ptr が、残っているオブジェクトを delete する。
    ObjectHeap::~ObjectHeap() = default;

    Object* ObjectHeap::Resolve(ObjectHandle handle) const
    {
        const std::unique_ptr<Object>* entry = m_objects.Get(ToSlot(handle));
        return entry != nullptr ? entry->get() : nullptr;
    }

    void ObjectHeap::RequestDestroy(Object& object)
    {
        m_pendingDestroy.push_back(object.GetHandle());
    }

    std::size_t ObjectHeap::FlushDestroyed()
    {
        std::size_t destroyedCount = 0;
        for (const ObjectHandle handle : m_pendingDestroy)
        {
            // 表から消すと、中の unique_ptr が破棄され、オブジェクトのデストラクタが呼ばれる。
            if (m_objects.Remove(ToSlot(handle)))
            {
                ++destroyedCount;
            }
        }
        m_pendingDestroy.clear();
        return destroyedCount;
    }
}
