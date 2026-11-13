// SpriteSheetData.h の実装。
#include "engine/SpriteSheetData.h"
#include "engine/EngineLog.h"

#if HUSKY_DEVELOPMENT
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"


#include <random>  // std::random_device
#endif

#include <cmath> // std::floor
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility> // std::move

namespace husky::engine
{
    namespace
    {
        // 値(整数)を int32 として読む。整数でない、範囲外なら false。
        bool ToInt32(const ArchiveValue& value, std::int32_t& out)
        {
            if (value.GetType() != ArchiveValue::Type::Int)
            {
                return false;
            }
            const std::int64_t number = value.AsInt();
            if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max())
            {
                return false;
            }
            out = static_cast<std::int32_t>(number);
            return true;
        }

        // オブジェクトのメンバー(整数)を int32 として読む。無い、整数でない、範囲外なら false。
        bool ReadInt32(const ArchiveValue& object, const char* name, std::int32_t& out)
        {
            const ArchiveValue* value = object.Find(name);
            return value != nullptr && ToInt32(*value, out);
        }

        void AddWarning(std::vector<std::string>* warnings, std::string text)
        {
            if (warnings != nullptr)
            {
                warnings->push_back(std::move(text));
            }
        }

        // 配列の要素の、正しい ID(0 以外で、前の要素と重ならない)を読む。読めなければ false。
        bool ReadUniqueId(const ArchiveValue& entry, std::unordered_set<std::int32_t>& seenIds, std::int32_t& outId)
        {
            return ReadInt32(entry, "id", outId) && outId != 0 && seenIds.insert(outId).second;
        }
    }

    std::vector<SpriteSlice> ReadSpriteSlices(const ArchiveValue& sheet, std::vector<std::string>* outWarnings)
    {
        std::vector<SpriteSlice> slices;
        const ArchiveValue* sprites = sheet.Find("sprites");
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
            if (!ReadUniqueId(entry, seenIds, slice.id))
            {
                // ID が無い(手で書き足したばかり。開発ビルドが次の起動で振る)か、ほかと重なっている。
                AddWarning(outWarnings, where + ": id が無いか、ほかの切り出しと重なっています");
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

    std::vector<SpriteAnimation> ReadSpriteAnimations(const ArchiveValue& sheet, const std::vector<SpriteSlice>& slices,
                                                      std::vector<std::string>* outWarnings)
    {
        std::vector<SpriteAnimation> animations;
        const ArchiveValue* list = sheet.Find("animations");
        if (list == nullptr)
        {
            return animations;
        }
        if (list->GetType() != ArchiveValue::Type::Array)
        {
            AddWarning(outWarnings, "animations: 配列ではありません");
            return animations;
        }

        std::unordered_set<std::int32_t> sliceIds;
        for (const SpriteSlice& slice : slices)
        {
            sliceIds.insert(slice.id);
        }

        std::unordered_set<std::int32_t> seenIds;
        for (std::size_t i = 0; i < list->GetArraySize(); ++i)
        {
            const ArchiveValue& entry = list->At(i);
            const std::string where = "animations[" + std::to_string(i) + "]";
            SpriteAnimation animation;
            if (!ReadUniqueId(entry, seenIds, animation.id))
            {
                AddWarning(outWarnings, where + ": id が無いか、ほかのアニメーションと重なっています");
                continue;
            }
            if (const ArchiveValue* name = entry.Find("name"))
            {
                animation.name = name->AsString();
            }
            if (const ArchiveValue* seconds = entry.Find("frameSeconds"))
            {
                animation.frameSeconds = static_cast<float>(seconds->AsFloat(-1.0)); // 数でなければ -1(下で弾く)
            }
            if (!(animation.frameSeconds > 0.0f))
            {
                AddWarning(outWarnings, where + ": frameSeconds が 0 より大きい数ではありません");
                continue;
            }
            if (const ArchiveValue* loop = entry.Find("loop"))
            {
                animation.loop = loop->AsBool(true);
            }

            const ArchiveValue* frames = entry.Find("frames");
            const std::size_t frameCount = frames != nullptr ? frames->GetArraySize() : 0;
            for (std::size_t f = 0; f < frameCount; ++f)
            {
                std::int32_t frame = 0;
                if (!ToInt32(frames->At(f), frame))
                {
                    // 名前のまま(開発ビルドで起動すると ID に直る)か、数でない。
                    AddWarning(outWarnings, where + ".frames[" + std::to_string(f) + "]: 切り出しの ID ではありません");
                    continue;
                }
                if (sliceIds.count(frame) == 0)
                {
                    AddWarning(outWarnings, where + ".frames[" + std::to_string(f) + "]: 切り出し " + std::to_string(frame)
                                                + " がありません");
                }
                animation.frames.push_back(frame); // 無い切り出しでも残す(描くときは、見つからない切り出しと同じ扱い)
            }
            if (animation.frames.empty())
            {
                AddWarning(outWarnings, where + ": コマがありません");
                continue;
            }
            animations.push_back(std::move(animation));
        }
        return animations;
    }

    std::int32_t SampleAnimationFrame(const SpriteAnimation& animation, double time)
    {
        if (animation.frames.empty() || !(animation.frameSeconds > 0.0f))
        {
            return 0;
        }
        const std::size_t count = animation.frames.size();
        // 何コマ目か: 経った時間を 1 コマの時間で割り、小数を切り捨てる(0.25 秒ごとなら、0.6 秒は 2 コマ目)。
        const double position = time > 0.0 ? std::floor(time / static_cast<double>(animation.frameSeconds)) : 0.0;
        // とても長い時間でも、size_t に直す前に十分小さくしておく(繰り返すなら、コマ数で割った余りだけが意味を持つ)。
        const double wrapped = animation.loop ? std::fmod(position, static_cast<double>(count)) : position;
        std::size_t index = static_cast<std::size_t>(wrapped < static_cast<double>(count) ? wrapped : count - 1);
        if (index >= count)
        {
            index = count - 1;
        }
        return animation.frames[index];
    }

#if HUSKY_DEVELOPMENT
    namespace
    {
        // arrayName の配列の要素のうち、ID が無い・0・重なるものに、新しい ID を振る。戻り値: 振った数。
        // 配列を作り直して置き換える(要素の中を直接書き換える口が ArchiveValue に無いため)。"id" は要素の先頭に置く。
        std::size_t AssignMissingIds(ArchiveValue& sheet, const char* arrayName, std::random_device& random)
        {
            const ArchiveValue* list = sheet.Find(arrayName);
            if (list == nullptr || list->GetType() != ArchiveValue::Type::Array)
            {
                return 0;
            }

            // 1. 今ある(正しい)ID を集める。重なっている2つ目以降は、振り直す対象にする。
            std::unordered_set<std::int32_t> usedIds;
            std::vector<bool> needsId(list->GetArraySize(), false);
            std::size_t missing = 0;
            for (std::size_t i = 0; i < list->GetArraySize(); ++i)
            {
                std::int32_t id = 0;
                if (!ReadUniqueId(list->At(i), usedIds, id))
                {
                    needsId[i] = true;
                    ++missing;
                }
            }
            if (missing == 0)
            {
                return 0;
            }

            // 2. 新しい ID を振りながら、配列を作り直す。
            ArchiveValue rebuilt = ArchiveValue::MakeArray();
            for (std::size_t i = 0; i < list->GetArraySize(); ++i)
            {
                const ArchiveValue& entry = list->At(i);
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
            sheet.Set(arrayName, std::move(rebuilt)); // 同じ名前なので、元の場所で置き換わる
            return missing;
        }

        // アニメーションのコマのうち、切り出しの名前で書かれたものを、その切り出しの ID に書き換える。戻り値: 書き換えた数。
        std::size_t ResolveFrameNames(ArchiveValue& sheet)
        {
            const ArchiveValue* list = sheet.Find("animations");
            if (list == nullptr || list->GetType() != ArchiveValue::Type::Array)
            {
                return 0;
            }
            // 名前 → ID(ID を振った後の切り出しから作る)。同じ名前が2つあれば、先のものを使う。
            std::unordered_map<std::string, std::int32_t> idsByName;
            for (const SpriteSlice& slice : ReadSpriteSlices(sheet))
            {
                idsByName.emplace(slice.name, slice.id);
            }

            std::size_t resolved = 0;
            ArchiveValue rebuilt = ArchiveValue::MakeArray();
            for (std::size_t i = 0; i < list->GetArraySize(); ++i)
            {
                const ArchiveValue& entry = list->At(i);
                const ArchiveValue* frames = entry.Find("frames");
                if (frames == nullptr || frames->GetType() != ArchiveValue::Type::Array)
                {
                    rebuilt.Append(entry);
                    continue;
                }
                ArchiveValue newFrames = ArchiveValue::MakeArray();
                for (std::size_t f = 0; f < frames->GetArraySize(); ++f)
                {
                    const ArchiveValue& frame = frames->At(f);
                    const auto found = frame.GetType() == ArchiveValue::Type::String ? idsByName.find(frame.AsString())
                                                                                     : idsByName.end();
                    if (found != idsByName.end())
                    {
                        newFrames.Append(ArchiveValue(static_cast<std::int64_t>(found->second)));
                        ++resolved;
                    }
                    else
                    {
                        newFrames.Append(frame); // ID のまま、または見つからない名前(読むときに注意が出る)
                    }
                }
                ArchiveValue copy = entry;
                copy.Set("frames", std::move(newFrames));
                rebuilt.Append(std::move(copy));
            }
            if (resolved > 0)
            {
                sheet.Set("animations", std::move(rebuilt));
            }
            return resolved;
        }
    }

    std::size_t NormalizeSpriteSheet(ArchiveValue& sheet)
    {
        std::random_device random; // OS の乱数(Guid::Generate と同じ)
        std::size_t changed = AssignMissingIds(sheet, "sprites", random);
        changed += AssignMissingIds(sheet, "animations", random);
        changed += ResolveFrameNames(sheet); // 切り出しに ID を振った後で行う(名前 → ID の表を作るため)
        return changed;
    }

    std::size_t NormalizeSpriteSheetsInFolder(const std::string& rootDirectory, const std::string& mirrorDirectory)
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
            ArchiveValue sheet;
            if (!platform::ReadTextFile((root + file).c_str(), text) || !JsonFormat::Read(text, sheet))
            {
                continue; // 読めないファイルは、読み込むときにローダが知らせる
            }
            const std::size_t changed = NormalizeSpriteSheet(sheet);
            if (changed == 0)
            {
                continue;
            }
            std::string updated;
            if (!JsonFormat::Write(sheet, updated) || !platform::WriteTextFile((root + file).c_str(), updated))
            {
                continue;
            }
            HUSKY_LOG(AssetLog::SpriteSheetNormalized, changed, file);
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
