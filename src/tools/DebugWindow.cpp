// DebugWindow.h の実装。製品(Release)では中身が空になる。
#include "tools/DebugWindow.h"

#if HUSKY_DEVELOPMENT

#include "core/MemoryTracker.h"

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(std::string*)

#include <string>

namespace husky::tools
{
    std::vector<CommandArgInput>& DebugWindow::GetArgInputs(const EditorCommand& command)
    {
        // try_emplace: まだ無ければ空の並びを作る(あれば、今のものをそのまま返す)。
        auto [it, created] = m_argInputs.try_emplace(command.name);
        std::vector<CommandArgInput>& inputs = it->second;
        if (created || inputs.size() != command.args.size())
        {
            // 初めて開くとき: 引数の形の既定値から始める。既定値があるものは、そのまま渡す(send)。
            inputs.assign(command.args.size(), CommandArgInput{});
            for (std::size_t i = 0; i < command.args.size(); ++i)
            {
                const engine::ArchiveValue& value = command.args[i].defaultValue;
                CommandArgInput& input = inputs[i];
                input.send = !value.IsNull();
                input.text = value.AsString();
                input.number = value.AsFloat();
                input.integer = static_cast<int>(value.AsInt());
                input.flag = value.AsBool();
            }
        }
        return inputs;
    }

    engine::ArchiveValue DebugWindow::BuildArgs(const EditorCommand& command, const std::vector<CommandArgInput>& inputs)
    {
        engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
        for (std::size_t i = 0; i < command.args.size() && i < inputs.size(); ++i)
        {
            const EditorCommandArg& arg = command.args[i];
            const CommandArgInput& input = inputs[i];
            if (!arg.required && !input.send)
            {
                continue; // 省略できて、既定値も無く、人も触っていない: 渡さない(コマンドが自分の既定で決める)
            }
            switch (arg.type)
            {
            case EditorArgType::Bool: args.Set(arg.name, engine::ArchiveValue(input.flag)); break;
            case EditorArgType::Int: args.Set(arg.name, engine::ArchiveValue(static_cast<std::int64_t>(input.integer))); break;
            case EditorArgType::Float: args.Set(arg.name, engine::ArchiveValue(input.number)); break;
            case EditorArgType::String: args.Set(arg.name, engine::ArchiveValue(input.text)); break;
            }
        }
        return args;
    }

    bool DebugWindow::DrawArgsPopup(engine::Application& app, const EditorCommand& command, engine::ArchiveValue& outArgs)
    {
        std::vector<CommandArgInput>& inputs = GetArgInputs(command);
        ImGui::TextUnformatted(command.label.c_str());
        ImGui::Separator();

        bool missingRequired = false;
        for (std::size_t i = 0; i < command.args.size(); ++i)
        {
            const EditorCommandArg& arg = command.args[i];
            CommandArgInput& input = inputs[i];
            ImGui::PushID(static_cast<int>(i));
            // 入力欄の名前は引数の名前。必須のものには * を付ける。
            const char* label = arg.name.c_str();
            bool changed = false;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            switch (arg.type)
            {
            case EditorArgType::Bool:
                changed = ImGui::Checkbox(label, &input.flag);
                break;
            case EditorArgType::Int:
                changed = ImGui::InputInt(label, &input.integer);
                break;
            case EditorArgType::Float:
                changed = ImGui::InputDouble(label, &input.number, 1.0, 10.0, "%.3f");
                break;
            case EditorArgType::String:
                if (arg.choices)
                {
                    // 選べる値が決まっている引数は、選択肢の箱にする(打ち間違いが起きない)。
                    // 一覧は、小窓が開いている間だけ毎フレーム作る(登録された型などが変われば、すぐ反映される)。
                    if (ImGui::BeginCombo(label, input.text.empty() ? "(choose)" : input.text.c_str()))
                    {
                        for (const std::string& choice : arg.choices(app))
                        {
                            if (ImGui::Selectable(choice.c_str(), choice == input.text))
                            {
                                input.text = choice;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    changed = ImGui::InputText(label, &input.text);
                }
                break;
            }
            if (changed)
            {
                input.send = true; // 人が触った省略できる引数は、渡す
            }
            if (arg.required)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("*");
                // 文字列の必須の引数が空なら、Run を押せなくする(押してから断られるより、先に分かるほうが親切)。
                missingRequired = missingRequired || (arg.type == EditorArgType::String && input.text.empty());
            }
            // 入力欄に乗せると、引数の説明を出す。
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s (%s%s)", arg.description.c_str(), GetArgTypeName(arg.type),
                                  arg.required ? ", required" : "");
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        bool run = false;
        ImGui::BeginDisabled(missingRequired);
        if (ImGui::Button("Run"))
        {
            outArgs = BuildArgs(command, inputs);
            run = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        if (missingRequired)
        {
            ImGui::TextDisabled("Fill in the fields marked *");
        }
        return run;
    }

    // 「Editor commands」の欄: 登録されたコマンドを、登録した順にボタンで並べ、最後に実行した結果を出す。
    void DebugWindow::DrawCommands(engine::Application& app, EditorCommandRegistry& commands)
    {
        ImGui::SeparatorText("Editor commands");
        // ボタンを押すと一覧の外のもの(シーン)が変わるので、押されたコマンドは、並べ終わってから実行する。
        const EditorCommand* clicked = nullptr;
        // 押されたコマンドに渡す引数は m_clickedArgs に入れる(引数の無いコマンドは空のまま)。
        // 毎フレームここで ArchiveValue を作ると、中の std::vector が(Debug の STL では空でも)確保するので、メンバーを使い回す。
        const EditorCommand* previous = nullptr;
        for (const EditorCommand& command : commands.GetCommands())
        {
            // 前のコマンドと同じまとまり(group)なら、同じ行に並べる(Play / Pause / Step / Stop など)。
            if (previous != nullptr && !command.group.empty() && command.group == previous->group)
            {
                ImGui::SameLine();
            }
            previous = &command;

            // ボタンを区別する ID は、機械向けの名前にする(表示名が同じコマンドがあっても、押し間違えないように)。
            ImGui::PushID(command.name.c_str());
            // 今は使えないコマンドは、ボタンを灰色にして押せなくする(BeginDisabled と EndDisabled の間)。
            const char* unavailable = EditorCommandRegistry::GetUnavailableReason(command, app);
            ImGui::BeginDisabled(unavailable != nullptr);
            // ボタンの文字: "Save scene (F5)"。引数のあるコマンドは "Spawn actor..."(押すと入力欄が開く、という印)。
            // 文字はメンバーの入れ物で組み立てる(毎フレーム新しく作ると確保が起きる。clear や assign は容量を残す)。
            m_buttonText.assign(command.label);
            if (!command.args.empty())
            {
                m_buttonText += "...";
            }
            if (command.shortcut != engine::Key::Unknown)
            {
                m_buttonText += " (";
                m_buttonText += GetKeyDisplayName(command.shortcut);
                m_buttonText += ')';
            }
            // Button は、押されたフレームだけ true を返す。
            const bool pressed = ImGui::Button(m_buttonText.c_str());
            // マウスを乗せると、何をするかと、CLI や AI から呼ぶときの名前を出す。
            // 説明が長くても読めるように、文字の大きさの 30 倍ほどの幅で折り返す。名前は控えめな色で下に添える。
            // 灰色のボタンにも乗せられるように AllowWhenDisabled を付け、使えない理由も出す。
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                ImGui::TextUnformatted(command.description.c_str());
                // 引数の形も並べる(何を渡せばよいかが分かるように)。
                for (const EditorCommandArg& arg : command.args)
                {
                    ImGui::BulletText("%s (%s%s): %s", arg.name.c_str(), GetArgTypeName(arg.type),
                                      arg.required ? ", required" : "", arg.description.c_str());
                }
                if (unavailable != nullptr)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Not available: %s", unavailable);
                }
                ImGui::TextDisabled("[%s]", command.name.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::EndDisabled();

            if (command.args.empty())
            {
                if (pressed)
                {
                    clicked = &command;
                }
            }
            else
            {
                // 引数のあるコマンド: ボタン(またはOpenArgsInput)で入力欄の小窓を開き、Run で実行する。
                // 小窓は、使えなくなったら(再生を始めた、など)開かない。
                const bool requested = !m_requestedArgsInput.empty() && m_requestedArgsInput == command.name;
                if ((pressed || requested) && unavailable == nullptr)
                {
                    ImGui::OpenPopup("args");
                }
                if (requested)
                {
                    m_requestedArgsInput.clear();
                }
                if (ImGui::BeginPopup("args"))
                {
                    if (DrawArgsPopup(app, command, m_clickedArgs))
                    {
                        clicked = &command;
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }
        if (clicked != nullptr)
        {
            const std::string name = clicked->name; // 実行中に一覧が変わっても困らないように、名前をコピーしてから呼ぶ
            // 引数の無いコマンドなら、前に使った引数が残っていないように空にしてから呼ぶ。
            if (clicked->args.empty())
            {
                m_clickedArgs = engine::ArchiveValue();
            }
            commands.Execute(name, app, m_clickedArgs);
        }

        // 最後の結果: 成功は緑、失敗は赤。細かい内容は、マウスを乗せると出す。
        if (!commands.GetLastCommandName().empty())
        {
            const EditorCommandResult& result = commands.GetLastResult();
            const ImVec4 color = result.succeeded ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(color, "%s: %s", result.succeeded ? "OK" : "FAILED", result.message.c_str());
            if (!result.details.empty() && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                for (const std::string& detail : result.details)
                {
                    ImGui::TextUnformatted(detail.c_str());
                }
                ImGui::EndTooltip();
            }
        }
    }

    void DebugWindow::Draw(engine::Application& app, EditorCommandRegistry* commands, const EditorPlayMode* playMode)
    {
        // 最初に出す位置(ImGuiCond_FirstUseEver: 初めて出すときだけ。その後はマウスで動かした位置のまま)。
        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);

        // 大きさは、毎フレーム中身に合わせる(AlwaysAutoResize)。コマンドの結果の行が増えたり長くなったりしても、
        // 前に保存された窓の大きさ(imgui.ini)のせいで、はみ出して見えなくなることが無いように。
        // Begin が false を返したら、窓が畳まれている。中身は作らない。End は、どちらの場合も必ず呼ぶ(ImGui の決まり)。
        if (ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            // 一番上に、今の状態(編集中か、再生中か)を色つきで出す。再生中の変更は Stop で消えるので、見落とさないように。
            if (playMode != nullptr)
            {
                const EditorPlayMode::State state = playMode->GetState();
                const ImVec4 color = state == EditorPlayMode::State::Edit      ? ImVec4(0.6f, 0.8f, 1.0f, 1.0f)
                                     : state == EditorPlayMode::State::Playing ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f)
                                                                               : ImVec4(1.0f, 0.75f, 0.3f, 1.0f);
                ImGui::TextColored(color, "Mode: %s", EditorPlayMode::GetStateName(state));
            }

            const ImGuiIO& io = ImGui::GetIO();
            ImGui::Text("FPS: %.1f (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

            const render::FrameStats& render = app.GetLastRenderStats();
            ImGui::Text("Sprites: %d  Draw calls: %d  Dropped: %d", render.spriteCount, render.drawCallCount,
                        render.droppedSpriteCount);

            ImGui::SeparatorText("Objects");
            ImGui::Text("Objects: %zu  Actors: %zu", app.GetRegistry().GetObjectCount(), app.GetScene().actors.size());
            ImGui::Text("Collected by last GC: %zu", app.GetCollectedLastGc());

            ImGui::SeparatorText("Memory");
            const core::MemoryStats memory = core::GetMemoryStats();
            const double megabyte = 1024.0 * 1024.0;
            ImGui::Text("Heap: %.2f MB (%zu allocations)", static_cast<double>(memory.currentBytes) / megabyte,
                        memory.currentCount);
            ImGui::Text("Peak: %.2f MB", static_cast<double>(memory.peakBytes) / megabyte);
            const core::FreeListAllocator& objects = app.GetRegistry().GetAllocator();
            ImGui::Text("Object heap: %.2f / %.2f MB", static_cast<double>(objects.GetUsedBytes()) / megabyte,
                        static_cast<double>(objects.GetCapacity()) / megabyte);

            if (commands != nullptr)
            {
                DrawCommands(app, *commands);
            }
        }
        ImGui::End();
    }

    const char* GetKeyDisplayName(engine::Key key)
    {
        switch (key)
        {
        case engine::Key::W: return "W";
        case engine::Key::A: return "A";
        case engine::Key::S: return "S";
        case engine::Key::D: return "D";
        case engine::Key::Up: return "Up";
        case engine::Key::Down: return "Down";
        case engine::Key::Left: return "Left";
        case engine::Key::Right: return "Right";
        case engine::Key::Space: return "Space";
        case engine::Key::Enter: return "Enter";
        case engine::Key::Escape: return "Escape";
        case engine::Key::F5: return "F5";
        case engine::Key::Unknown:
        case engine::Key::Count: break;
        }
        return "";
    }
}

#endif
