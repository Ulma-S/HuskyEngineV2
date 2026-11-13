// アセットの窓(ImGui。SD-3b): アセットの表の一覧を出し、データのアセット(.sdata、ScriptableData)を開くと、
// そのデータの窓が開いて値を見る・直すことができる。製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
//   "Assets" の窓      … アセットの表のパスを並べる(上の欄で絞り込み)。.sdata は "Open" で開ける(ほかの種類は、まだ開けない)
//   "Data: パス" の窓  … 開いたデータの型・GUID と、プロパティ(インスペクタと同じく、リフレクションから自動で作る)
//
// 読み込んだデータは読むだけ(書き込むと SD-2 の検知に引っかかる)なので、窓は「書き換えられる写し」を持ち、
// ImGui にはその写しを書かせる。部品から手を離したとき(どの部品も操作中でなくなったとき)、写しと読み込んだ中身で
// 値の違うプロパティごとに data.set を呼ぶ(ファイルを書き、新しいアセットとして読み直す。Undo できる)。
// 読み直しや Undo で中身が新しくなったら、写しも作り直す。data.set が今は使えない(再生中)なら、値は灰色で読むだけ。
//
// 使い方(main.cpp):
//   tools::AssetBrowser assets;                                // 開いている窓を覚えておくので、毎フレーム作り直さない
//   app.SetDevUiCallback([&](engine::Application& a) { assets.Draw(a, &commands); });
//
// 毎フレームの描画は、温まった後はメモリを確保しない(一覧はアセットの表が変わったときだけ作り直す)。
//
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/Guid.h"
#include "core/AssetRef.h"
#include "core/Handle.h"
#include "engine/Application.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace husky::tools
{
    class EditorCommandRegistry;

    class AssetBrowser
    {
    public:
        // 窓を壊すとき、開いている窓の写しと箱を根から外す。Application より先に壊すこと(main ではアプリの後に宣言する)。
        ~AssetBrowser();

        // "Assets" の窓と、開いているデータの窓を描く。commands が nullptr なら、直した値はファイルに書かない(テスト用)。
        void Draw(engine::Application& app, EditorCommandRegistry* commands);

        // データのアセットの窓を開く(すでに開いていれば何もしない)。.sdata でなければ false。
        bool OpenData(engine::Application& app, const core::Guid& guid);
        // 窓を閉じる(写しを根から外し、捨てる)。
        void CloseData(engine::Application& app, const core::Guid& guid);

        // 写しをコードから直したことを知らせる(ふつうは ImGui の部品が直す。テスト用)。次の Draw で、手を離したときと同じく書く。
        void MarkChanged(const core::Guid& guid);

        // 開いているデータの窓の数(確認とテスト用)。
        std::size_t GetOpenDataCount() const { return m_dataWindows.size(); }
        // 開いている窓の写し(ImGui が書く相手。確認とテスト用)。開いていなければ null のハンドル。
        core::ObjectHandle GetWorkingCopy(const core::Guid& guid) const;

    private:
        // 一覧の1行(パスの順に並べておく)。
        struct Row
        {
            core::Guid guid;
            std::string path;
            bool isData = false; // .sdata(開ける)
        };

        // 開いているデータの窓1つ。
        struct DataWindow
        {
            core::Guid guid;
            std::string guidText;     // 表示と data.set の引数用(毎フレーム文字列を作らないように、開いたときに作る)
            std::string title;        // "Data: data/bouncer.sdata###guid"(### の後ろが ImGui の窓の ID。表示は前だけ)
            // 開いているデータの参照。Resolve で読み直しに乗り換える。箱(asset)は根に入れて、開いている間は回収させない。
            core::AssetReference reference;
            core::ObjectHandle rootedBox; // 今、根に入れている箱(reference.asset が変わったら入れ替える)
            core::ObjectHandle copy; // 書き換えられる写し(根に入れて、GC から守る)
            core::ObjectHandle source; // 写しを作った元の中身(これが変わったら、読み直されたので写しを作り直す)
            bool dirty = false;       // 写しの値が変わったが、まだ data.set していない
            bool open = true;         // 窓の × で false になる
        };

        void RebuildRows(const engine::AssetDatabase& database);
        void DrawList(engine::Application& app);
        // 窓1つを描く。閉じられたら false。
        bool DrawDataWindow(engine::Application& app, EditorCommandRegistry* commands, DataWindow& window);
        // 元の中身から写しを作り直す(前の写しは根から外して捨てる)。読めていなければ何もしない。
        void RefreshCopy(engine::Application& app, DataWindow& window);
        // 写しと元の中身で値の違うプロパティごとに data.set を呼ぶ。
        void Commit(engine::Application& app, EditorCommandRegistry& commands, DataWindow& window);
        void ReleaseCopy(engine::Application& app, DataWindow& window);

        std::vector<Row> m_rows;
        std::uint32_t m_rowsVersion = 0; // m_rows を作ったときの AssetDatabase::GetVersion(0 はまだ作っていない)
        std::string m_filter;            // 一覧の絞り込み(パスの一部)
        std::vector<DataWindow> m_dataWindows;
        engine::Application* m_app = nullptr; // デストラクタで根から外すため(Draw / OpenData で覚える。借りているだけ)
    };
}

#endif
