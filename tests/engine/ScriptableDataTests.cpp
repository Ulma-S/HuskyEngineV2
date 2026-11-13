// ScriptableData(engine/ScriptableData.h)と、ローダでの読み込み(.sdata)のテスト。SD-1。
#include "core/AssetRef.h"
#include "engine/Application.h"
#include "engine/AssetLoader.h"
#include "engine/JsonFormat.h"
#include "engine/ScriptableData.h"

#include "TestFolder.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using husky::core::Guid;
using husky::core::ObjectHandle;
using husky::engine::Actor;
using husky::engine::Application;
using husky::engine::AssetLoader;
using husky::engine::AssetState;
using husky::engine::ScriptableData;
using husky::engine::ScriptableDataAsset;

namespace
{
    // 武器のデータ(ゲームが作る型の例)。
    class WeaponData : public ScriptableData
    {
        HUSKY_OBJECT(WeaponData, ScriptableData)
    public:
        float damage = 1.0f;
        std::string label;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(damage)
            HUSKY_PROPERTY(label)
        HUSKY_REFLECTION_END()
    };

    // 武器の子の型(AssetRef<WeaponData> が、子の型のファイルも指せることを確かめる)。
    class SwordData : public WeaponData
    {
        HUSKY_OBJECT(SwordData, WeaponData)
    public:
        std::int32_t length = 0;
        ObjectHandle owner; // ファイルには書かれないハンドル(WriteScriptableData の警告を確かめる)

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(length)
            HUSKY_PROPERTY(owner)
        HUSKY_REFLECTION_END()
    };

    // データを AssetRef で指すアクター。
    class Holder : public Actor
    {
        HUSKY_OBJECT(Holder, Actor)
    public:
        husky::core::AssetRef<WeaponData> weapon;

        HUSKY_REFLECTION_BEGIN()
            HUSKY_PROPERTY(weapon)
        HUSKY_REFLECTION_END()
    };

    const Guid SwordGuid = husky::tests::MakeGuid(0x5d1a2b3c4d5e4f60ull, 0x8a9b0c1d2e3f4a01ull);
    const Guid WeaponGuid = husky::tests::MakeGuid(0x5d1a2b3c4d5e4f60ull, 0x8a9b0c1d2e3f4a02ull);

    void RegisterTestTypes(Application& app)
    {
        app.GetTypeRegistry().Register<WeaponData>();
        app.GetTypeRegistry().Register<SwordData>();
        app.GetTypeRegistry().Register<Holder>();
    }

    // relativePath に text を置き、.meta に guid を書く。
    void WriteData(const husky::tests::TestFolder& folder, const std::string& relativePath, const std::string& text,
                   const Guid& guid)
    {
        folder.Write(relativePath, text);
        folder.Write(relativePath + ".meta", husky::engine::AssetDatabase::MakeMetaText(guid));
    }
}

TEST_CASE("ScriptableData: .sdata を AssetRef で指すと、ファイルの型の名前から中身を作り、値が入る。子の型のファイルも指せる")
{
    husky::tests::TestFolder folder("husky_test_sdata_load");
    WriteData(folder, "data/sword.sdata",
              R"({ "type": "SwordData", "properties": { "damage": 12.5, "label": "Long sword", "length": 90 } })", SwordGuid);

    Application app; // ワーカー 0 本(同期実行)
    RegisterTestTypes(app);
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();

    husky::core::AssetRef<WeaponData> weapon;
    weapon.guid = SwordGuid;
    CHECK(loader.Resolve(weapon) == nullptr); // 読み始めたところ(中身は読み終わってから作る)
    const ScriptableDataAsset* asset = loader.ResolveScriptableDataAsset(weapon.asset);
    REQUIRE(asset != nullptr);
    CHECK(asset->GetState() == AssetState::Loading);
    CHECK(loader.LoadScriptableData(SwordGuid) == weapon.asset); // 同じ GUID は同じ箱

    loader.WaitForAllLoads();
    REQUIRE(asset->IsLoaded());
    const WeaponData* data = loader.Resolve(weapon); // const で返る(読むだけのデータ)
    REQUIRE(data != nullptr);
    CHECK(data->IsA<SwordData>()); // AssetRef<WeaponData> から、子の型(SwordData)の中身に届く
    CHECK(data->damage == 12.5f);
    CHECK(data->label == "Long sword");
    CHECK(static_cast<const SwordData*>(data)->length == 90);
    CHECK(data->GetAssetGuid() == SwordGuid);
    CHECK(loader.GetLoadedScriptableDataCount() == 1);

    CHECK(AssetLoader::CanLoadAs(WeaponData::StaticType(), "data/sword.sdata"));
    CHECK_FALSE(AssetLoader::CanLoadAs(WeaponData::StaticType(), "prefabs/coin.prefab"));
}

TEST_CASE("ScriptableData: 中身の型が期待する型(か、その子)でなければ nullptr を返す")
{
    husky::tests::TestFolder folder("husky_test_sdata_mismatch");
    WriteData(folder, "data/weapon.sdata", R"({ "type": "WeaponData", "properties": { "damage": 3 } })", WeaponGuid);

    Application app;
    RegisterTestTypes(app);
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();

    husky::core::AssetRef<SwordData> sword; // ファイルは親の型(WeaponData)なので、SwordData としては使えない
    sword.guid = WeaponGuid;
    loader.Resolve(sword);
    loader.WaitForAllLoads();
    CHECK(loader.Resolve(sword) == nullptr);
    CHECK(loader.Resolve(sword) == nullptr); // 2回目も同じ(警告は1回だけ)

    const ScriptableDataAsset* asset = loader.ResolveScriptableDataAsset(sword.asset);
    REQUIRE(asset != nullptr);
    CHECK(asset->IsLoaded()); // 箱は読めている。型が合わないだけ

    husky::core::AssetRef<WeaponData> weapon; // 同じファイルを親の型として指せば使える
    weapon.guid = WeaponGuid;
    const WeaponData* data = loader.Resolve(weapon);
    REQUIRE(data != nullptr);
    CHECK(data->damage == 3.0f);
}

TEST_CASE("ScriptableData: 型の名前が無い・登録されていない・ScriptableData の子でない・JSON でないときは「失敗」になる")
{
    husky::tests::TestFolder folder("husky_test_sdata_broken");
    const Guid noType = husky::tests::MakeGuid(0x5d1a2b3c4d5e4f60ull, 0x8a9b0c1d2e3f4a10ull);
    const Guid unknown = husky::tests::MakeGuid(0x5d1a2b3c4d5e4f60ull, 0x8a9b0c1d2e3f4a11ull);
    const Guid notData = husky::tests::MakeGuid(0x5d1a2b3c4d5e4f60ull, 0x8a9b0c1d2e3f4a12ull);
    const Guid broken = husky::tests::MakeGuid(0x5d1a2b3c4d5e4f60ull, 0x8a9b0c1d2e3f4a13ull);
    WriteData(folder, "no_type.sdata", R"({ "properties": { "damage": 3 } })", noType);
    WriteData(folder, "unknown.sdata", R"({ "type": "NoSuchData", "properties": {} })", unknown);
    WriteData(folder, "actor.sdata", R"({ "type": "Holder", "properties": {} })", notData); // アクターの型は作らない
    WriteData(folder, "broken.sdata", "{ not json", broken);

    Application app;
    RegisterTestTypes(app);
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();
    const std::size_t objectsBefore = app.GetRegistry().GetObjectCount();

    std::vector<const ScriptableDataAsset*> assets;
    for (const Guid& guid : {noType, unknown, notData, broken, husky::tests::MakeGuid(1, 2)})
    {
        assets.push_back(loader.ResolveScriptableDataAsset(loader.LoadScriptableData(guid)));
    }
    loader.WaitForAllLoads();
    for (const ScriptableDataAsset* asset : assets)
    {
        REQUIRE(asset != nullptr);
        CHECK(asset->GetState() == AssetState::Failed);
        CHECK(loader.GetScriptableData(*asset) == nullptr);
    }
    CHECK(app.GetRegistry().GetObjectCount() == objectsBefore + assets.size()); // 箱だけで、中身(Holder など)は作っていない
}

TEST_CASE("ScriptableData: AssetRef で指している間は、箱も中身も GC に回収されない。指す人がいなくなると両方回収される")
{
    husky::tests::TestFolder folder("husky_test_sdata_gc");
    WriteData(folder, "data/sword.sdata", R"({ "type": "SwordData", "properties": { "damage": 7 } })", SwordGuid);

    Application app;
    RegisterTestTypes(app);
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();

    Holder* holder = app.GetScene().Spawn<Holder>();
    holder->weapon.guid = SwordGuid;
    loader.Resolve(holder->weapon);
    loader.WaitForAllLoads();
    const WeaponData* data = loader.Resolve(holder->weapon);
    REQUIRE(data != nullptr);
    const ObjectHandle dataHandle = data->GetHandle();

    // シーン(根)→ アクター → AssetRef → 箱 → 箱のプロパティ → 中身、とたどれるので回収されない。
    app.GetRegistry().CollectGarbage();
    CHECK(app.GetRegistry().Resolve(dataHandle) != nullptr);
    CHECK(loader.GetLoadedScriptableDataCount() == 1);

    holder->Destroy();
    app.GetClock().SetFixedDelta(0.01);
    app.StepFrame(0, {}); // フレームの終わりに、アクターが一覧から外れ、破棄が確定する
    app.GetRegistry().CollectGarbage();
    CHECK(loader.GetLoadedScriptableDataCount() == 0);
    CHECK(app.GetRegistry().Resolve(dataHandle) == nullptr);
}

TEST_CASE("ScriptableData: WriteScriptableData で書いたものを .sdata にして読み直すと、同じ値になる。ハンドルは書かずに警告する")
{
    Application app;
    RegisterTestTypes(app);

    SwordData* original = app.GetRegistry().Create<SwordData>();
    REQUIRE(original != nullptr);
    original->damage = 4.25f;
    original->label = "Short sword";
    original->length = 40;
    original->owner = original->GetHandle();

    std::vector<std::string> warnings;
    const husky::engine::ArchiveValue value = husky::engine::WriteScriptableData(*original, &warnings);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("owner") != std::string::npos);
    std::string text;
    REQUIRE(husky::engine::JsonFormat::Write(value, text));
    CHECK(text.find("\"owner\"") == std::string::npos);

    husky::tests::TestFolder folder("husky_test_sdata_write");
    WriteData(folder, "data/short.sdata", text, SwordGuid);
    REQUIRE(app.GetAssetDatabase().Scan(folder.Root()));
    AssetLoader& loader = app.GetAssetLoader();
    husky::core::AssetRef<SwordData> sword;
    sword.guid = SwordGuid;
    loader.Resolve(sword);
    loader.WaitForAllLoads();
    const SwordData* loaded = loader.Resolve(sword);
    REQUIRE(loaded != nullptr);
    CHECK(loaded != original);
    CHECK(loaded->damage == 4.25f);
    CHECK(loaded->label == "Short sword");
    CHECK(loaded->length == 40);
    CHECK(loaded->owner.IsNull());
}
