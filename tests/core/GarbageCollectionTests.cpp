// GC(マーク&スイープ)のテスト。
#include "core/ObjectRegistry.h"

#include <doctest/doctest.h>

using husky::core::Object;
using husky::core::ObjectHandle;
using husky::core::ObjectRegistry;

namespace
{
    int g_destroyedCount = 0;

    // ほかのオブジェクトへの参照を2本(強い参照)と、登録しない参照を1本(弱い参照)持つ。
    class Node : public Object
    {
        HUSKY_OBJECT(Node, Object)
    public:
        ~Node() override { ++g_destroyedCount; }

        ObjectHandle left;     // 強い参照(プロパティとして登録する)
        ObjectHandle right;    // 強い参照
        ObjectHandle weakLink; // 弱い参照(登録しない)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(left)
            HUSKY_PROPERTY(right)
        HUSKY_REFLECTION_END()
    };

    // 参照のプロパティを親(Node)から引き継ぐ子クラス。
    class DerivedNode : public Node
    {
        HUSKY_OBJECT(DerivedNode, Node)
    };

    // テストの最初に数を 0 に戻す。
    void ResetCount() { g_destroyedCount = 0; }
}

TEST_CASE("GC: どこからも届かないオブジェクトは回収される")
{
    ResetCount();
    ObjectRegistry registry;
    const ObjectHandle lonely = registry.Create<Node>()->GetHandle();

    CHECK(registry.CollectGarbage() == 1);
    CHECK(g_destroyedCount == 1);
    CHECK(registry.Resolve(lonely) == nullptr);
}

TEST_CASE("GC: 根は回収されない")
{
    ResetCount();
    ObjectRegistry registry;
    Node* root = registry.Create<Node>();
    CHECK(registry.AddRoot(root->GetHandle()));

    CHECK(registry.CollectGarbage() == 0);
    CHECK(registry.Resolve(root->GetHandle()) == root);
}

TEST_CASE("GC: 根から参照をたどって届くものは残り、届かないものは回収される")
{
    ResetCount();
    ObjectRegistry registry;
    // root → a → b と、どこからも参照されない c
    Node* root = registry.Create<Node>();
    Node* a = registry.Create<Node>();
    Node* b = registry.Create<Node>();
    const ObjectHandle c = registry.Create<Node>()->GetHandle();
    root->left = a->GetHandle();
    a->right = b->GetHandle();
    registry.AddRoot(root->GetHandle());

    CHECK(registry.CollectGarbage() == 1);
    CHECK(registry.Resolve(a->GetHandle()) == a);
    CHECK(registry.Resolve(b->GetHandle()) == b);
    CHECK(registry.Resolve(c) == nullptr);
}

TEST_CASE("GC: お互いを参照し合っていても(循環参照)、根から届かなければ両方回収される")
{
    ResetCount();
    ObjectRegistry registry;
    Node* a = registry.Create<Node>();
    Node* b = registry.Create<Node>();
    a->left = b->GetHandle();
    b->left = a->GetHandle();

    CHECK(registry.CollectGarbage() == 2);
    CHECK(g_destroyedCount == 2);
}

TEST_CASE("GC: 循環参照でも、根から届けば両方残る(マークが無限に回らない)")
{
    ResetCount();
    ObjectRegistry registry;
    Node* a = registry.Create<Node>();
    Node* b = registry.Create<Node>();
    a->left = b->GetHandle();
    b->left = a->GetHandle();
    registry.AddRoot(a->GetHandle());

    CHECK(registry.CollectGarbage() == 0);
    CHECK(registry.GetObjectCount() == 2);
}

TEST_CASE("GC: 根から外すと、次の GC で回収される")
{
    ResetCount();
    ObjectRegistry registry;
    const ObjectHandle handle = registry.Create<Node>()->GetHandle();
    registry.AddRoot(handle);
    CHECK(registry.CollectGarbage() == 0);

    CHECK(registry.RemoveRoot(handle));
    CHECK_FALSE(registry.RemoveRoot(handle)); // もう根ではない
    CHECK(registry.CollectGarbage() == 1);
}

TEST_CASE("GC: 登録していない参照(弱い参照)は、相手を生かさない。回収後は nullptr を返す")
{
    ResetCount();
    ObjectRegistry registry;
    Node* root = registry.Create<Node>();
    const ObjectHandle target = registry.Create<Node>()->GetHandle();
    root->weakLink = target; // 弱い参照
    registry.AddRoot(root->GetHandle());

    CHECK(registry.CollectGarbage() == 1);
    // 参照は残っているが、相手はもう無いことが分かる(解放済みのメモリを指すことはない)。
    CHECK(registry.Resolve(root->weakLink) == nullptr);
}

TEST_CASE("GC: 親クラスで登録した参照も、子クラスのオブジェクトからたどられる")
{
    ResetCount();
    ObjectRegistry registry;
    DerivedNode* root = registry.Create<DerivedNode>();
    Node* child = registry.Create<Node>();
    root->left = child->GetHandle(); // left は親クラス Node で登録したプロパティ
    registry.AddRoot(root->GetHandle());

    CHECK(registry.CollectGarbage() == 0);
    CHECK(registry.Resolve(child->GetHandle()) == child);
}

TEST_CASE("GC: Destroy で破棄された根は、GC のときに根の一覧から外れる")
{
    ResetCount();
    ObjectRegistry registry;
    Node* root = registry.Create<Node>();
    registry.AddRoot(root->GetHandle());
    CHECK(registry.GetRootCount() == 1);

    root->Destroy();
    registry.FlushDestroyed(); // フレームの終わり: まず破棄を確定し、
    registry.CollectGarbage(); // その後で GC
    CHECK(registry.GetRootCount() == 0);
}

TEST_CASE("GC: 存在しないオブジェクトは根にできない")
{
    ObjectRegistry registry;
    CHECK_FALSE(registry.AddRoot(ObjectHandle{}));
}

TEST_CASE("GC: 長い参照の鎖でもたどれる(再帰を使わないので、深さで落ちない)")
{
    ResetCount();
    ObjectRegistry registry;
    Node* head = registry.Create<Node>();
    registry.AddRoot(head->GetHandle());

    Node* tail = head;
    for (int i = 0; i < 100000; ++i)
    {
        Node* next = registry.Create<Node>();
        tail->left = next->GetHandle();
        tail = next;
    }
    registry.Create<Node>(); // どこからも届かない1つ

    CHECK(registry.CollectGarbage() == 1);
    CHECK(registry.GetObjectCount() == 100001);
}
