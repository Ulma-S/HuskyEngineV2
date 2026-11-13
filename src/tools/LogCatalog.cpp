// ログのカタログ(log.catalog)。登録した全部のチャンネルとメッセージの定義を、JSON の値の木にして書く(LOG-3)。
//   {"format": 1, "channels": [{"name": "Engine.Asset", "parent": "Engine",
//     "messages": [{"id": 3, "name": "MetaMissing", "level": "Warning", "format": ".meta がありません({path})",
//                   "args": [{"name": "path", "type": "String"}]}]}]}
#if HUSKY_DEVELOPMENT // エディタの機能は開発ビルドだけ(製品からコードごと外す)

#include "tools/EditorCommands.h"

#include "core/Log.h"
#include "engine/JsonFormat.h"
#include "platform/FileSystem.h"

#include <string>
#include <utility>

namespace husky::tools
{
    engine::ArchiveValue BuildLogCatalog()
    {
        using engine::ArchiveValue;
        core::Log& log = core::Log::Get();

        ArchiveValue root = ArchiveValue::MakeObject();
        root.Set("format", ArchiveValue(std::int64_t{1})); // カタログの形の版(形を変えたら上げる)
        ArchiveValue& channels = root.Set("channels", ArchiveValue::MakeArray());
        const int channelCount = log.GetChannelCount();
        for (int channel = 0; channel < channelCount; ++channel)
        {
            ArchiveValue entry = ArchiveValue::MakeObject();
            entry.Set("name", ArchiveValue(log.GetChannelName(channel)));
            const int parent = log.GetParentChannel(channel);
            entry.Set("parent", parent >= 0 ? ArchiveValue(log.GetChannelName(parent)) : ArchiveValue());
            ArchiveValue& messages = entry.Set("messages", ArchiveValue::MakeArray());
            for (const core::Log::MessageSnapshot& message : log.GetMessages(channel))
            {
                ArchiveValue item = ArchiveValue::MakeObject();
                item.Set("id", ArchiveValue(static_cast<std::int64_t>(message.id)));
                item.Set("name", ArchiveValue(message.name));
                item.Set("level", ArchiveValue(core::ToString(message.level)));
                item.Set("format", ArchiveValue(message.format));
                ArchiveValue& args = item.Set("args", ArchiveValue::MakeArray());
                for (const core::Log::ArgSnapshot& arg : message.args)
                {
                    ArchiveValue argValue = ArchiveValue::MakeObject();
                    argValue.Set("name", ArchiveValue(arg.name));
                    argValue.Set("type", ArchiveValue(core::ToString(arg.type)));
                    args.Append(std::move(argValue));
                }
                messages.Append(std::move(item));
            }
            channels.Append(std::move(entry));
        }
        return root;
    }

    EditorCommand MakeLogCatalogCommand()
    {
        EditorCommand command;
        command.name = "log.catalog";
        command.label = "Write log catalog";
        command.description = "Write every log channel and message definition to a JSON file "
                              "(a future log viewer uses it to turn log files into text).";
        command.args = {
            {"path", EditorArgType::String, "File to write (default: logs/catalog.json next to the exe)", false},
        };
        command.execute = [](EditorCommandContext& context) {
            std::string path;
            if (const engine::ArchiveValue* value = context.args.Find("path"))
            {
                path = value->AsString();
            }
            else
            {
                const std::string directory = platform::GetExecutableDirectory() + "logs";
                platform::MakeDirectory(directory.c_str());
                path = directory + "/catalog.json";
            }

            const engine::ArchiveValue catalog = BuildLogCatalog();
            std::string text;
            if (!engine::JsonFormat::Write(catalog, text, true) || !platform::WriteTextFile(path.c_str(), text))
            {
                return EditorCommandResult::Failure("Could not write the log catalog: " + path);
            }
            std::size_t messageCount = 0;
            const engine::ArchiveValue* channels = catalog.Find("channels");
            for (std::size_t i = 0; channels != nullptr && i < channels->GetArraySize(); ++i)
            {
                messageCount += channels->At(i).Find("messages")->GetArraySize();
            }
            EditorCommandResult result = EditorCommandResult::Success(
                "Wrote " + std::to_string(channels != nullptr ? channels->GetArraySize() : 0) + " channels, " +
                std::to_string(messageCount) + " messages");
            result.details.push_back("file: " + path);
            return result;
        };
        return command;
    }
}

#endif
