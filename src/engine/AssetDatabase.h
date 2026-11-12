// アセットのデータベース: 「GUID → ファイルのパス」の対応表。
//
// アセット(画像など)のファイルの隣には、同じ名前に .meta を付けたファイルを置き、その中にアセットの GUID を書く。
//   assets/textures/enemy.png
//   assets/textures/enemy.png.meta   … {"guid": "3f2a9c1e-7b40-4d2e-9a1f-0c5e8b7d6a43"}
// シーンなどはアセットを GUID で指し、パスはこの表で引く。ファイルを動かすときは、.meta も一緒に動かせば、
// GUID は変わらないので、参照は切れない(Unity の .meta と同じ考え方)。
//
// 表は Scan で作る。assets のフォルダの中をすべて見て回り、.meta を読んで表に入れる。
// 次のものは、表に入れずに、ログに出して数える(AssetScanReport):
//   - .meta が無いアセット(開発ビルドなら CreateMissingMetaFiles で作れる)
//   - 読めない .meta、GUID が無い・壊れている .meta
//   - 対になるアセットが無い .meta(アセットだけを消した、動かしたなど)
//   - 同じ GUID を持つ .meta が2つ以上ある(.meta ごとファイルをコピーしたなど)。
//     どちらが正しいかは分からないので、黙ってどちらかを選ばず、どれも表に入れない
//
// 名前が「.」で始まるファイル(.gitkeep など)と、.tmp で終わるファイル(書きかけ)は、アセットとして扱わない。
//
// 動作スレッド: ゲームスレッドのみ
#pragma once

#include "core/Guid.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace husky::engine
{
    // Scan の結果の集計。問題の内容は、Scan がログに出す。
    struct AssetScanReport
    {
        std::size_t assetCount = 0;         // 表に入れたアセットの数
        std::size_t missingMetaCount = 0;   // .meta が無いアセットの数
        std::size_t invalidMetaCount = 0;   // 読めない、または GUID が正しくない .meta の数
        std::size_t orphanMetaCount = 0;    // 対になるアセットが無い .meta の数
        std::size_t duplicateGuidCount = 0; // 同じ GUID を持っていた .meta の数(重なったものすべて)

        bool HasProblems() const
        {
            return missingMetaCount + invalidMetaCount + orphanMetaCount + duplicateGuidCount > 0;
        }
    };

    class AssetDatabase final
    {
    public:
        // .meta のファイルの、末尾に付ける文字列。
        static constexpr std::string_view MetaSuffix = ".meta";

        // rootDirectory(assets のフォルダ)の中を見て回り、表を作り直す(前の中身は消す)。
        // outReport が nullptr でなければ、集計を入れる。
        // 戻り値: フォルダを読めたら true(個々の問題があっても true。問題は outReport とログで分かる)。
        bool Scan(const std::string& rootDirectory, AssetScanReport* outReport = nullptr);

        // GUID のアセットの、rootDirectory からの相対パス("textures/enemy.png")。表に無ければ nullptr。
        const std::string* FindPath(const core::Guid& guid) const;

        // GUID のアセットを開くときのパス(rootDirectory + 相対パス)。表に無ければ空の文字列。
        std::string GetFullPath(const core::Guid& guid) const;

        // 相対パスから GUID を引く(インスペクタでファイルを選んだときなどに使う)。表に無ければ nullptr。
        const core::Guid* FindGuid(std::string_view relativePath) const;

        std::size_t GetAssetCount() const { return m_paths.size(); }

        // 最後に Scan したフォルダ(末尾は「/」)。
        const std::string& GetRootDirectory() const { return m_rootDirectory; }

        // .meta の中身の文字列を作る / 読む。ファイルの形式を、このクラスの中に閉じ込めるための関数。
        static std::string MakeMetaText(const core::Guid& guid);
        static bool ParseMetaText(std::string_view text, core::Guid& outGuid);

    private:
        std::string m_rootDirectory;
        // std::unordered_map は C# の Dictionary。Guid を鍵にできるのは、std::hash<Guid> を用意したから(Guid.h)。
        std::unordered_map<core::Guid, std::string> m_paths; // GUID → 相対パス
        std::unordered_map<std::string, core::Guid> m_guids; // 相対パス → GUID(逆引き)
    };

#if HUSKY_DEVELOPMENT
    // 【開発ビルドだけ】rootDirectory の中で、.meta が無いアセットに、新しい GUID を振って .meta を作る。
    // GUID を振るのはエディタの役目なので、製品(Release)にはコードごと入らない。
    // mirrorDirectory が空でなければ、作った .meta を、同じ相対パスでそこにも書く(同じ GUID。exe の隣の assets など)。
    // すでにある .meta は、中身が壊れていても作り直さない(GUID が変わると、そのアセットへの参照が切れるため)。
    // 戻り値: 作った .meta の数。
    std::size_t CreateMissingMetaFiles(const std::string& rootDirectory, const std::string& mirrorDirectory = {});
#endif
}
