// engine のログのメッセージ(core/Log.h)。番号は一度振ったら変えず、使い回さない。
#pragma once
#include "core/Log.h"

namespace husky::engine
{
    // アセット(表、ローダ、スプライトシートの整え)。
    enum class AssetLog : std::uint16_t
    {
        OrphanMeta = 1,
        MetaUnreadable = 2,
        MetaMissing = 3,
        DuplicateGuid = 4,
        MetaCreated = 5,
        GuidNotInDatabase = 6,
        OutOfObjectMemory = 7,
        LoadWarning = 8,
        ScriptableDataWrongType = 9,
        ScriptableDataNoType = 10,
        ScriptableDataTypeNotRegistered = 11,
        ScriptableDataTypeNotData = 12,
        ScriptableDataCreateFailed = 13,
        ReadOnlyDataModified = 14,
        SpriteSheetNormalized = 15,
        DefaultTextureMissing = 16,
        DevUiFontMissing = 17,
        FontAtlasFull = 18,
        KindAlreadyRegistered = 19,
        KindNotRegistered = 20,
    };

    HUSKY_LOG_CHANNEL_BEGIN(AssetLog, "Engine.Asset")
        HUSKY_LOG_MESSAGE(OrphanMeta, Warning, "対になるファイルが無い .meta があります({path})",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(MetaUnreadable, Warning, ".meta から GUID を読めません({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(MetaMissing, Warning, ".meta がありません({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(DuplicateGuid, Error, "GUID が重なっています。どちらも使いません({guid}: {path})",
                          HUSKY_LOG_ARG(guid, String), HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(MetaCreated, Info, ".meta を作りました({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(GuidNotInDatabase, Warning, "GUID が表にありません({guid})", HUSKY_LOG_ARG(guid, String))
        HUSKY_LOG_MESSAGE(OutOfObjectMemory, Error, "オブジェクト用のメモリが足りず、{kind} を作れません({guid})",
                          HUSKY_LOG_ARG(kind, String), HUSKY_LOG_ARG(guid, String))
        HUSKY_LOG_MESSAGE(LoadWarning, Warning, "{kind} の注意({path}): {warning}", HUSKY_LOG_ARG(kind, String),
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(warning, String))
        HUSKY_LOG_MESSAGE(ScriptableDataWrongType, Warning,
                          "ScriptableData の型が違います({guid} は {actual} で、{expected} を期待していました)",
                          HUSKY_LOG_ARG(guid, String), HUSKY_LOG_ARG(actual, String), HUSKY_LOG_ARG(expected, String))
        HUSKY_LOG_MESSAGE(ScriptableDataNoType, Error, "ScriptableData に \"type\"(型の名前の文字列)がありません({path})",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(ScriptableDataTypeNotRegistered, Error, "ScriptableData の型 {type} が登録されていません({path})",
                          HUSKY_LOG_ARG(type, String), HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(ScriptableDataTypeNotData, Error, "ScriptableData の型 {type} が ScriptableData の子ではありません({path})",
                          HUSKY_LOG_ARG(type, String), HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(ScriptableDataCreateFailed, Error, "ScriptableData を作れません({path})",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(ReadOnlyDataModified, Error,
                          "読むだけの ScriptableData が書き換えられています: {path}(型 {type}、GUID {guid})のプロパティ {property}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(type, String), HUSKY_LOG_ARG(guid, String),
                          HUSKY_LOG_ARG(property, String))
        HUSKY_LOG_MESSAGE(SpriteSheetNormalized, Info, "スプライトシートを整えました(ID とコマを {count} か所)({path})",
                          HUSKY_LOG_ARG(count, Int), HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(DefaultTextureMissing, Warning, "既定のテクスチャがアセットの表にありません({path})。テクスチャ無しで描きます",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(DevUiFontMissing, Warning, "開発用の UI のフォントを読めません({path})。既定のフォント(英数字だけ)で続けます",
                          HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(FontAtlasFull, Warning,
                          "字のアトラス({size} ピクセルの字、{textureSize} × {textureSize})がいっぱいで、これより後に初めて出た字は描きません",
                          HUSKY_LOG_ARG(size, Float), HUSKY_LOG_ARG(textureSize, Int))
        HUSKY_LOG_MESSAGE(KindAlreadyRegistered, Error, "アセットの種類 {type} は、もう登録されています(後のほうは登録しません)",
                          HUSKY_LOG_ARG(type, String))
        HUSKY_LOG_MESSAGE(KindNotRegistered, Error, "アセットの種類 {type} の読み方が登録されていないので、読めません({guid})",
                          HUSKY_LOG_ARG(type, String), HUSKY_LOG_ARG(guid, String))
    HUSKY_LOG_CHANNEL_END()

    // シーンのファイルの読み書き。
    enum class SceneLog : std::uint16_t
    {
        ReadFailed = 1,
        ReadWarning = 2,
        Loaded = 3,
        SaveWarning = 4,
        JsonWriteFailed = 5,
        Saved = 6,
        ParentRefused = 7,
        ChildBodyIgnored = 8,
        UIButtonTargetMissing = 9,
        // SC-2b: 入れ物のシーン
        ParentAcrossScenes = 10,
        SceneNotInList = 11,
        SceneAlreadyLoaded = 12,
        SceneNotLoaded = 13,
        RootSceneUnload = 14,
        SceneLoadFailed = 15,
        SceneUnloaded = 16,
        SceneListDuplicateName = 17,
        SceneListEntryMissing = 18,
        SceneMoveRefused = 19,
    };

    HUSKY_LOG_CHANNEL_BEGIN(SceneLog, "Engine.Scene")
        HUSKY_LOG_MESSAGE(ReadFailed, Error, "シーンのファイルを読めません({path}): {error}", HUSKY_LOG_ARG(path, String),
                          HUSKY_LOG_ARG(error, String))
        HUSKY_LOG_MESSAGE(ReadWarning, Warning, "シーンの読み込みの警告({path}): {warning}", HUSKY_LOG_ARG(path, String),
                          HUSKY_LOG_ARG(warning, String))
        HUSKY_LOG_MESSAGE(Loaded, Info, "シーンを読み込みました({path}): アクター {actors}、オブジェクト {objects}、警告 {warnings}",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(actors, Int), HUSKY_LOG_ARG(objects, Int),
                          HUSKY_LOG_ARG(warnings, Int))
        HUSKY_LOG_MESSAGE(SaveWarning, Warning, "シーンの保存の警告({path}): {warning}", HUSKY_LOG_ARG(path, String),
                          HUSKY_LOG_ARG(warning, String))
        HUSKY_LOG_MESSAGE(JsonWriteFailed, Error, "シーンを JSON にできません({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(Saved, Info, "シーンを保存しました({path}): アクター {actors}、{bytes} バイト",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(actors, Int), HUSKY_LOG_ARG(bytes, Int))
        HUSKY_LOG_MESSAGE(ParentRefused, Warning, "{actor} の親を {parent} にできません: {reason}", HUSKY_LOG_ARG(actor, String),
                          HUSKY_LOG_ARG(parent, String), HUSKY_LOG_ARG(reason, String))
        HUSKY_LOG_MESSAGE(ChildBodyIgnored, Warning,
                          "親を持つアクター {actor} の Rigidbody2D は使いません(動く体は親を持てません)。コライダーは形だけで置きます",
                          HUSKY_LOG_ARG(actor, String))
        HUSKY_LOG_MESSAGE(ParentAcrossScenes, Error,
                          "{actor} の親を {parent} にできません: 親子はシーンをまたげません(シーン「{scene}」と「{parentScene}」)",
                          HUSKY_LOG_ARG(actor, String), HUSKY_LOG_ARG(parent, String), HUSKY_LOG_ARG(scene, String),
                          HUSKY_LOG_ARG(parentScene, String))
        HUSKY_LOG_MESSAGE(SceneNotInList, Error, "シーン {name} はシーンの一覧にありません", HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(SceneAlreadyLoaded, Error, "シーン {name} はもう読んであります(同じシーンは同時に1つだけ)",
                          HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(SceneNotLoaded, Error, "シーン {name} は読まれていないので下ろせません", HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(RootSceneUnload, Error, "ルートシーン {name} は下ろせません", HUSKY_LOG_ARG(name, String))
        HUSKY_LOG_MESSAGE(SceneLoadFailed, Error, "シーン {name} を読めません: {reason}", HUSKY_LOG_ARG(name, String),
                          HUSKY_LOG_ARG(reason, String))
        HUSKY_LOG_MESSAGE(SceneUnloaded, Info, "シーン {name} を下ろしました: アクター {actors}", HUSKY_LOG_ARG(name, String),
                          HUSKY_LOG_ARG(actors, Int))
        HUSKY_LOG_MESSAGE(SceneListDuplicateName, Error, "シーンの一覧で名前 {name} が重なっています({path})。後のものは使いません",
                          HUSKY_LOG_ARG(name, String), HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(SceneListEntryMissing, Error, "シーンの一覧の {index} 番目のシーンがアセットの表にありません",
                          HUSKY_LOG_ARG(index, Int))
        HUSKY_LOG_MESSAGE(SceneMoveRefused, Error, "{actor} をシーン {scene} へ移せません: 親が別のシーンにいます",
                          HUSKY_LOG_ARG(actor, String), HUSKY_LOG_ARG(scene, String))
        HUSKY_LOG_MESSAGE(UIButtonTargetMissing, Warning, "ボタン {button} の呼び先を呼べません: {reason}",
                          HUSKY_LOG_ARG(button, String), HUSKY_LOG_ARG(reason, String))
    HUSKY_LOG_CHANNEL_END()

    // 毎秒の統計(テレメトリで、値ごとに追えるように1つずつの値にする)。
    enum class StatsLog : std::uint16_t
    {
        Frame = 1,
        Memory = 2,
        Profile = 3,
    };

    HUSKY_LOG_CHANNEL_BEGIN(StatsLog, "Engine.Stats")
        HUSKY_LOG_MESSAGE(Frame, Info,
                          "FPS: {fps}  平均 dt: {dtMs} ms  スプライト: {sprites}  描画命令: {drawCalls} 回  描けなかった数: {dropped}"
                          "  オブジェクト: {objects}  直前の GC で回収: {collected}",
                          HUSKY_LOG_ARG(fps, Int), HUSKY_LOG_ARG(dtMs, Float), HUSKY_LOG_ARG(sprites, Int),
                          HUSKY_LOG_ARG(drawCalls, Int), HUSKY_LOG_ARG(dropped, Int), HUSKY_LOG_ARG(objects, Int),
                          HUSKY_LOG_ARG(collected, Int))
        HUSKY_LOG_MESSAGE(Memory, Info, "ヒープ: {heapMb} MB({count} 件、最大 {peakMb} MB、この1秒の確保 {allocations} 回)",
                          HUSKY_LOG_ARG(heapMb, Float), HUSKY_LOG_ARG(count, Int), HUSKY_LOG_ARG(peakMb, Float),
                          HUSKY_LOG_ARG(allocations, Int))
        // 区間ごとに1件(量が多いので Verbose。画面には出ない。Debug の窓の "Profiler (ms)" で見られる)。
        HUSKY_LOG_MESSAGE(Profile, Verbose, "処理時間 {section}: 平均 {averageMs} ms / 最大 {maxMs} ms",
                          HUSKY_LOG_ARG(section, String), HUSKY_LOG_ARG(averageMs, Float), HUSKY_LOG_ARG(maxMs, Float))
    HUSKY_LOG_CHANNEL_END()

    // ゲームのコード(モジュール)の読み込み(HR-2。engine/GameModule.h)。
    enum class ModuleLog : std::uint16_t
    {
        Loaded = 1,
        EntryFailed = 2,
        VersionMismatch = 3,
        ObjectsRemain = 4,
        Unloaded = 5,
    };

    HUSKY_LOG_CHANNEL_BEGIN(ModuleLog, "Engine.Module")
        HUSKY_LOG_MESSAGE(Loaded, Info, "ゲームのモジュールを読み込みました({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(EntryFailed, Error, "ゲームのモジュールの入口が失敗を返しました({path})", HUSKY_LOG_ARG(path, String))
        HUSKY_LOG_MESSAGE(VersionMismatch, Error, "ゲームのモジュールの版が違います({path}: {found}、エンジンは {expected})。ビルドし直してください",
                          HUSKY_LOG_ARG(path, String), HUSKY_LOG_ARG(found, Int), HUSKY_LOG_ARG(expected, Int))
        HUSKY_LOG_MESSAGE(ObjectsRemain, Error, "ゲームの型のオブジェクトが {count} 個残っているので、ゲームのモジュールを外せません(最初の型: {type})",
                          HUSKY_LOG_ARG(count, Int), HUSKY_LOG_ARG(type, String))
        HUSKY_LOG_MESSAGE(Unloaded, Info, "ゲームのモジュールを外しました({path})", HUSKY_LOG_ARG(path, String))
    HUSKY_LOG_CHANNEL_END()
}
