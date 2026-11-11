// GC(マーク&スイープ)のテスト。
#include "core/ObjectRegistry.h"

#include <doctest/doctest.h>

#include <vector>

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

// --- 配列のプロパティ(std::vector<ObjectHandle>) ---

namespace
{
    // 子の一覧を持つオブジェクト(シーンがアクターの一覧を持つのと同じ形)。
    class Group : public Object
    {
        HUSKY_OBJECT(Group, Object)
    public:
        ~Group() override { ++g_destroyedCount; }

        std::vector<ObjectHandle> members;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(members)
        HUSKY_REFLECTION_END()
    };
}

TEST_CASE("Property: std::vector<ObjectHandle> は ObjectHandleArray として登録され、参照フラグを持つ")
{
    const husky::core::Property* property = Group::StaticType().FindProperty("members");
    REQUIRE(property != nullptr);
    CHECK(property->GetType() == husky::core::PropertyType::ObjectHandleArray);
    CHECK(property->IsReference());
}

TEST_CASE("GC: 配列のプロパティに入っているオブジェクトは、すべて生き残る")
{
    ResetCount();
    ObjectRegistry registry;
    Group* group = registry.Create<Group>();
    registry.AddRoot(group->GetHandle());

    for (int i = 0; i < 10; ++i)
    {
        group->members.push_back(registry.Create<Node>()->GetHandle());
    }
    const ObjectHandle outsider = registry.Create<Node>()->GetHandle(); // 一覧に入っていない

    CHECK(registry.CollectGarbage() == 1); // outsider だけが回収される
    CHECK(registry.Resolve(outsider) == nullptr);
    for (const ObjectHandle member : group->members)
    {
        CHECK(registry.Resolve(member) != nullptr);
    }
}

TEST_CASE("GC: 配列から外したオブジェクトは、次の GC で回収される")
{
    ResetCount();
    ObjectRegistry registry;
    Group* group = registry.Create<Group>();
    registry.AddRoot(group->GetHandle());
    group->members.push_back(registry.Create<Node>()->GetHandle());
    group->members.push_back(registry.Create<Node>()->GetHandle());

    const ObjectHandle removed = group->members.back();
    group->members.pop_back(); // 一覧から外す(シーンからアクターを外すのと同じ)

    CHECK(registry.CollectGarbage() == 1);
    CHECK(registry.Resolve(removed) == nullptr);
}

TEST_CASE("GC: 配列の中に、すでに回収済みのハンドルや空のハンドルがあっても大丈夫")
{
    ResetCount();
    ObjectRegistry registry;
    Group* group = registry.Create<Group>();
    registry.AddRoot(group->GetHandle());

    Node* doomed = registry.Create<Node>();
    group->members.push_back(doomed->GetHandle());
    group->members.push_back(ObjectHandle{}); // どこも指さないハンドル
    doomed->Destroy();
    registry.FlushDestroyed(); // 一覧には古いハンドルが残ったまま

    CHECK(registry.CollectGarbage() == 0); // 落ちずに終わり、group は生き残る
    CHECK(registry.Resolve(group->GetHandle()) == group);
}
