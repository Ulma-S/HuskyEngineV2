// AssetDatabase.h の実装。
#include "engine/AssetDatabase.h"
#include "engine/EngineLog.h"

#include "engine/ArchiveValue.h"
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"


#include <unordered_set>
#include <utility> // std::move
#include <vector>

namespace husky::engine
{
    namespace
    {
        bool EndsWith(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // アセットとして扱わないファイルか。名前が「.」で始まる(.gitkeep など)か、.tmp で終わる(書きかけ)。
        bool IsIgnoredFile(std::string_view relativePath)
        {
            // 最後の「/」の後ろがファイルの名前。「/」が無ければ全体が名前(npos + 1 は 0 になる)。
            const std::string_view fileName = relativePath.substr(relativePath.rfind('/') + 1);
            return (!fileName.empty() && fileName[0] == '.') || EndsWith(fileName, ".tmp");
        }

        // フォルダのパスの末尾に「/」を付ける(付いていなければ)。
        std::string WithTrailingSlash(const std::string& directory)
        {
            if (directory.empty() || directory.back() == '/' || directory.back() == '\\')
            {
                return directory;
            }
            return directory + '/';
        }

        // フォルダの中のファイルを、アセットと .meta に分ける(扱わないファイルは捨てる)。
        bool ListAssetsAndMetas(const std::string& root, std::vector<std::string>& outAssets,
                                std::vector<std::string>& outMetas)
        {
            std::vector<std::string> files;
            if (!platform::ListFilesRecursive(root.c_str(), files))
            {
                return false;
            }
            for (std::string& file : files)
            {
                if (IsIgnoredFile(file))
                {
                    continue;
                }
                if (EndsWith(file, AssetDatabase::MetaSuffix))
                {
                    outMetas.push_back(std::move(file));
                }
                else
                {
                    outAssets.push_back(std::move(file));
                }
            }
            return true;
        }
    }

    std::string AssetDatabase::MakeMetaText(const core::Guid& guid)
    {
        ArchiveValue root = ArchiveValue::MakeObject();
        root.Set("guid", ArchiveValue(guid.ToString()));
        std::string text;
        JsonFormat::Write(root, text); // 文字列と小数を含まないので失敗しない
        return text;
    }

    bool AssetDatabase::ParseMetaText(std::string_view text, core::Guid& outGuid)
    {
        ArchiveValue root;
        if (!JsonFormat::Read(text, root))
        {
            return false;
        }
        const ArchiveValue* guidValue = root.Find("guid");
        if (guidValue == nullptr || guidValue->GetType() != ArchiveValue::Type::String)
        {
            return false;
        }
        core::Guid guid;
        if (!core::Guid::Parse(guidValue->AsString(), guid) || guid.IsNull())
        {
            return false; // すべて 0 の GUID は「どこも指さない」なので、アセットの GUID には使えない
        }
        outGuid = guid;
        return true;
    }

    bool AssetDatabase::Scan(const std::string& rootDirectory, AssetScanReport* outReport)
    {
        ++m_version; // 中身が変わったことを、一覧を覚えている側(アセットの窓など)に知らせる
        m_paths.clear();
        m_guids.clear();
        m_rootDirectory = WithTrailingSlash(rootDirectory);
        AssetScanReport report;

        std::vector<std::string> assets;
        std::vector<std::string> metas;
        if (!ListAssetsAndMetas(m_rootDirectory, assets, metas))
        {
            if (outReport != nullptr)
            {
                *outReport = report;
            }
            return false;
        }

        // 1. .meta を読む。std::unordered_set は C# の HashSet(「入っているか」をすぐに調べられる)。
        const std::unordered_set<std::string> assetSet(assets.begin(), assets.end());
        struct Entry
        {
            core::Guid guid;
            std::string assetPath;
        };
        std::vector<Entry> entries;
        std::unordered_map<core::Guid, int> guidUseCount; // GUID ごとに、何個の .meta が使っているか
        std::unordered_set<std::string> assetsWithMeta;   // .meta のファイルがあるアセット(読めたかどうかは問わない)
        for (const std::string& meta : metas)
        {
            std::string assetPath = meta.substr(0, meta.size() - MetaSuffix.size());
            if (assetSet.count(assetPath) == 0)
            {
                HUSKY_LOG(AssetLog::OrphanMeta, meta);
                ++report.orphanMetaCount;
                continue;
            }
            assetsWithMeta.insert(assetPath);

            std::string text;
            core::Guid guid;
            if (!platform::ReadTextFile((m_rootDirectory + meta).c_str(), text) || !ParseMetaText(text, guid))
            {
                HUSKY_LOG(AssetLog::MetaUnreadable, meta);
                ++report.invalidMetaCount;
                continue;
            }
            entries.push_back(Entry{guid, std::move(assetPath)});
            ++guidUseCount[guid]; // [] は、鍵が無ければ 0 で作ってから返す
        }

        // 2. .meta が無いアセットを数える。
        for (const std::string& asset : assets)
        {
            if (assetsWithMeta.count(asset) == 0)
            {
                HUSKY_LOG(AssetLog::MetaMissing, asset);
                ++report.missingMetaCount;
            }
        }

        // 3. 表に入れる。同じ GUID が2つ以上あるものは、どれが正しいか分からないので、どれも入れない。
        for (Entry& entry : entries)
        {
            if (guidUseCount[entry.guid] > 1)
            {
                HUSKY_LOG(AssetLog::DuplicateGuid, entry.guid.ToString(), entry.assetPath);
                ++report.duplicateGuidCount;
                continue;
            }
            m_guids.emplace(entry.assetPath, entry.guid);
            m_paths.emplace(entry.guid, std::move(entry.assetPath));
        }

        report.assetCount = m_paths.size();
        if (outReport != nullptr)
        {
            *outReport = report;
        }
        return true;
    }

    const std::string* AssetDatabase::FindPath(const core::Guid& guid) const
    {
        const auto found = m_paths.find(guid);
        return found != m_paths.end() ? &found->second : nullptr; // second は「値」の側(first が鍵)
    }

    std::string AssetDatabase::GetFullPath(const core::Guid& guid) const
    {
        const std::string* path = FindPath(guid);
        return path != nullptr ? m_rootDirectory + *path : std::string();
    }

    const core::Guid* AssetDatabase::FindGuid(std::string_view relativePath) const
    {
        const auto found = m_guids.find(std::string(relativePath));
        return found != m_guids.end() ? &found->second : nullptr;
    }

#if HUSKY_DEVELOPMENT
    std::size_t CreateMissingMetaFiles(const std::string& rootDirectory, const std::string& mirrorDirectory)
    {
        const std::string root = WithTrailingSlash(rootDirectory);
        const std::string mirror = WithTrailingSlash(mirrorDirectory);
        std::vector<std::string> assets;
        std::vector<std::string> metas;
        if (!ListAssetsAndMetas(root, assets, metas))
        {
            return 0;
        }
        const std::unordered_set<std::string> metaSet(metas.begin(), metas.end());

        std::size_t created = 0;
        for (const std::string& asset : assets)
        {
            const std::string meta = asset + std::string(AssetDatabase::MetaSuffix);
            if (metaSet.count(meta) != 0)
            {
                continue; // もうある(中身が壊れていても、勝手に作り直さない。GUID が変わると参照が切れるため)
            }
            const std::string text = AssetDatabase::MakeMetaText(core::Guid::Generate());
            if (platform::WriteTextFile((root + meta).c_str(), text))
            {
                HUSKY_LOG(AssetLog::MetaCreated, meta);
                ++created;
                if (!mirror.empty())
                {
                    platform::WriteTextFile((mirror + meta).c_str(), text); // 失敗の理由は platform がログに出す
                }
            }
        }
        return created;
    }
#endif
}
