// COL-1: Box2D を入れたことの確かめ(スモークテスト = 「火を入れて煙が出ないか」程度の、最低限の動作確認)。
// ふつうのコードは Box2D を直接使わない(設計メモ「当たり判定と物理」)。ここはライブラリの組み込みを確かめるテストなので、
// 例外として Box2D のヘッダを直接読む。COL-2 で PhysicsWorld2D を作ったら、そちらのテストが本筋になる。
// エンジンを DLL にした構成(HR-1)では、このテストの exe の中の Box2D は、DLL の中の Box2D とは別の写しになる。
// そのため「確保が MemoryTracker を通る」ことは、エンジンの窓口を通して PhysicsWorld2DTests.cpp で確かめる。
#include <box2d/box2d.h>
#include <doctest/doctest.h>

namespace
{
    // b2World_OverlapAABB が、重なった形ごとに呼ぶ関数。見つかった数を数える。
    bool CountShape(b2ShapeId, void* context)
    {
        ++*static_cast<int*>(context);
        return true; // true: 続けて探す
    }
}

TEST_CASE("COL-1: Box2D の世界を作り、四角を置いて、範囲の問い合わせで見つけられる")
{
    // 世界を作る(重力なし: 今は当たり判定だけに使う)
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{0.0f, 0.0f};
    const b2WorldId world = b2CreateWorld(&worldDef);
    REQUIRE(b2World_IsValid(world));

    // 動かない体(static)を (100, 50) に置き、32 x 32 の四角(半分の大きさ 16)を付ける
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.position = b2Vec2{100.0f, 50.0f};
    const b2BodyId body = b2CreateBody(world, &bodyDef);
    const b2ShapeDef shapeDef = b2DefaultShapeDef();
    const b2Polygon box = b2MakeBox(16.0f, 16.0f);
    b2CreatePolygonShape(body, &shapeDef, &box);
    b2World_Step(world, 1.0f / 60.0f, 4);

    // 四角に重なる範囲を問い合わせると 1 つ見つかり、離れた範囲では見つからない
    int found = 0;
    b2World_OverlapAABB(world, b2AABB{{95.0f, 45.0f}, {105.0f, 55.0f}}, b2DefaultQueryFilter(), CountShape, &found);
    CHECK(found == 1);
    found = 0;
    b2World_OverlapAABB(world, b2AABB{{300.0f, 300.0f}, {310.0f, 310.0f}}, b2DefaultQueryFilter(), CountShape, &found);
    CHECK(found == 0);

    // 世界を消すと、Box2D の確保は全部返る
    b2DestroyWorld(world);
    CHECK(b2GetByteCount() == 0); // このテストの exe の中の Box2D が数えた量
}
