// SpriteSlices.h の実装。
#include "engine/SpriteSlices.h"

#if HUSKY_DEVELOPMENT
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"

#include <cstdio>  // std::printf
#include <random>  // std::random_device
#endif

#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility> // std::move

namespace husky::engine
{
    namespace
    {
        // 数(整数)のメンバーを、int32 として読む。無い、整数でない、範囲外なら false。
        bool ReadInt32(const ArchiveValue& object, const char* name, std::int32_t& out)
        {
            const ArchiveValue* value = object.Find(name);
            if (value == nullptr || value->GetType() != ArchiveValue::Type::Int)
            {
                return false;
            }
            const std::int64_t number = value->AsInt();
            if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max())
            {
                return false;
            }
            out = static_cast<std::int32_t>(number);
            return true;
        }

        void AddWarning(std::vector<std::string>* warnings, std::string text)
        {
            if (warnings != nullptr)
            {
                warnings->push_back(std::move(text));
            }
        }
    }

    std::vector<SpriteSlice> ReadSpriteSlices(const ArchiveValue& meta, std::vector<std::string>* outWarnings)
    {
        std::vector<SpriteSlice> slices;
        const ArchiveValue* sprites = meta.Find("sprites");
        if (sprites == nullptr)
        {
            return slices; // 切り出しを書いていない(画像の全体だけを使う)
        }
        if (sprites->GetType() != ArchiveValue::Type::Array)
        {
            AddWarning(outWarnings, "sprites: 配列ではありません");
            return slices;
        }

        std::unordered_set<std::int32_t> seenIds;
        for (std::size_t i = 0; i < sprites->GetArraySize(); ++i)
        {
            const ArchiveValue& entry = sprites->At(i);
            const std::string where = "sprites[" + std::to_string(i) + "]";
            SpriteSlice slice;
            if (!ReadInt32(entry, "x", slice.rect.x) || !ReadInt32(entry, "y", slice.rect.y)
                || !ReadInt32(entry, "width", slice.rect.width) || !ReadInt32(entry, "height", slice.rect.height))
            {
                AddWarning(outWarnings, where + ": x, y, width, height のどれかが、整数ではありません");
                continue;
            }
            if (slice.rect.IsEmpty())
            {
                AddWarning(outWarnings, where + ": 大きさが 0 以下です");
                continue;
            }
            if (!ReadInt32(entry, "id", slice.id) || slice.id == 0)
            {
                // 手で書き足したばかりで、まだ ID が無い(開発ビルドが次の起動で振る)。
                AddWarning(outWarnings, where + ": id がありません");
                continue;
            }
            if (!seenIds.insert(slice.id).second) // insert の second は「新しく入った」なら true
            {
                AddWarning(outWarnings, where + ": id " + std::to_string(slice.id) + " がほかの切り出しと重なっています");
                continue;
            }
            if (const ArchiveValue* name = entry.Find("name"))
            {
                slice.name = name->AsString(); // 文字列でなければ空
            }
            slices.push_back(std::move(slice));
        }
        return slices;
    }

#if HUSKY_DEVELOPMENT
    std::size_t AssignMissingSliceIds(ArchiveValue& meta)
    {
        const ArchiveValue* sprites = meta.Find("sprites");
        if (sprites == nullptr || sprites->GetType() != ArchiveValue::Type::Array)
        {
            return 0;
        }

        // 1. 今ある(正しい)ID を集める。重なっている2つ目以降は、振り直す対象にする。
        std::unordered_set<std::int32_t> usedIds;
        std::vector<bool> needsId(sprites->GetArraySize(), false);
        std::size_t missing = 0;
        for (std::size_t i = 0; i < sprites->GetArraySize(); ++i)
        {
            std::int32_t id = 0;
            if (!ReadInt32(sprites->At(i), "id", id) || id == 0 || !usedIds.insert(id).second)
            {
                needsId[i] = true;
                ++missing;
            }
        }
        if (missing == 0)
        {
            return 0;
        }

        // 2. 新しい ID を振りながら、配列を作り直す(要素の中を直接書き換える口が ArchiveValue に無いため)。
        //    "id" を先頭にして、ほかのメンバーは元の順番のまま写す。
        std::random_device random; // OS の乱数(Guid::Generate と同じ)
        ArchiveValue rebuilt = ArchiveValue::MakeArray();
        for (std::size_t i = 0; i < sprites->GetArraySize(); ++i)
        {
            const ArchiveValue& entry = sprites->At(i);
            if (!needsId[i])
            {
                rebuilt.Append(entry);
                continue;
            }
            std::int32_t id = 0;
            do
            {
                // 31 ビットの正の数(0 と、すでに使っている ID は避ける)。
                id = static_cast<std::int32_t>(random() & 0x7FFFFFFFu);
            } while (id == 0 || !usedIds.insert(id).second);

            ArchiveValue withId = ArchiveValue::MakeObject();
            withId.Set("id", ArchiveValue(static_cast<std::int64_t>(id)));
            for (std::size_t m = 0; m < entry.GetMemberCount(); ++m)
            {
                if (entry.GetMemberName(m) != "id")
                {
                    withId.Set(entry.GetMemberName(m), entry.GetMemberValue(m));
                }
            }
            rebuilt.Append(std::move(withId));
        }
        meta.Set("sprites", std::move(rebuilt)); // 同じ名前なので、元の場所で置き換わる(メンバーの順番は変わらない)
        return missing;
    }

    std::size_t AssignMissingSliceIdsInFolder(const std::string& rootDirectory, const std::string& mirrorDirectory)
    {
        std::vector<std::string> files;
        if (!platform::ListFilesRecursive(rootDirectory.c_str(), files))
        {
            return 0;
        }
        const std::string root = rootDirectory + "/";
        const std::string mirror = mirrorDirectory.empty() ? std::string() : mirrorDirectory + "/";

        std::size_t rewritten = 0;
        for (const std::string& file : files)
        {
            constexpr std::string_view Suffix = ".spritesheet";
            if (file.size() < Suffix.size() || file.compare(file.size() - Suffix.size(), Suffix.size(), Suffix) != 0)
            {
                continue;
            }
            std::string text;
            ArchiveValue meta;
            if (!platform::ReadTextFile((root + file).c_str(), text) || !JsonFormat::Read(text, meta))
            {
                continue; // 読めないファイルは、読み込むときにローダが知らせる
            }
            const std::size_t assigned = AssignMissingSliceIds(meta);
            if (assigned == 0)
            {
                continue;
            }
            std::string updated;
            if (!JsonFormat::Write(meta, updated) || !platform::WriteTextFile((root + file).c_str(), updated))
            {
                continue;
            }
            std::printf("アセット: 切り出しに ID を %zu 個振りました(%s)\n", assigned, file.c_str());
            if (!mirror.empty())
            {
                platform::WriteTextFile((mirror + file).c_str(), updated);
            }
            ++rewritten;
        }
        return rewritten;
    }
#endif
}
