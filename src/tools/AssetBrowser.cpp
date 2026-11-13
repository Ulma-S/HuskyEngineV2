// AssetBrowser.h の実装。
#include "tools/AssetBrowser.h"

#if HUSKY_DEVELOPMENT

#include "engine/AssetLoader.h"
#include "engine/ObjectSerializer.h"
#include "engine/ScriptableData.h"
#include "tools/EditorCommands.h" // data.set を名前で呼ぶ
#include "tools/Inspector.h"      // DrawProperties(リフレクションから画面を作る)

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(std::string*)

#include <algorithm> // std::sort
#include <cstdio>    // std::printf

namespace husky::tools
{
    AssetBrowser::~AssetBrowser()
    {
        if (m_app == nullptr)
        {
            return;
        }
        for (DataWindow& window : m_dataWindows)
        {
            ReleaseCopy(*m_app, window);
        }
    }

    bool AssetBrowser::OpenData(engine::Application& app, const core::Guid& guid)
    {
        m_app = &app;
        for (const DataWindow& window : m_dataWindows)
        {
            if (window.guid == guid)
            {
                return true; // もう開いている
            }
        }
        const std::string* path = app.GetAssetDatabase().FindPath(guid);
        if (path == nullptr || !engine::AssetLoader::CanLoadAs(engine::ScriptableData::StaticType(), *path))
        {
            return false;
        }
        DataWindow window;
        window.guid = guid;
        window.guidText = guid.ToString();
        window.title = "Data: " + *path + "###data-" + window.guidText; // ### の後ろは ImGui の窓の ID(パスが変わっても同じ窓)
        window.reference.guid = guid;
        m_dataWindows.push_back(std::move(window));
        return true;
    }

    void AssetBrowser::CloseData(engine::Application& app, const core::Guid& guid)
    {
        for (std::size_t i = 0; i < m_dataWindows.size(); ++i)
        {
            if (m_dataWindows[i].guid == guid)
            {
                ReleaseCopy(app, m_dataWindows[i]);
                m_dataWindows.erase(m_dataWindows.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
    }

    void AssetBrowser::MarkChanged(const core::Guid& guid)
    {
        for (DataWindow& window : m_dataWindows)
        {
            if (window.guid == guid)
            {
                window.dirty = true;
            }
        }
    }

    core::ObjectHandle AssetBrowser::GetWorkingCopy(const core::Guid& guid) const
    {
        for (const DataWindow& window : m_dataWindows)
        {
            if (window.guid == guid)
            {
                return window.copy;
            }
        }
        return {};
    }

    void AssetBrowser::Draw(engine::Application& app, EditorCommandRegistry* commands)
    {
        m_app = &app;
        DrawList(app);
        for (std::size_t i = 0; i < m_dataWindows.size();)
        {
            if (DrawDataWindow(app, commands, m_dataWindows[i]))
            {
                ++i;
                continue;
            }
            ReleaseCopy(app, m_dataWindows[i]); // × で閉じた
            m_dataWindows.erase(m_dataWindows.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    void AssetBrowser::RebuildRows(const engine::AssetDatabase& database)
    {
        m_rows.clear();
        database.ForEachAsset([this](const core::Guid& guid, const std::string& path) {
            m_rows.push_back({guid, path, engine::AssetLoader::CanLoadAs(engine::ScriptableData::StaticType(), path)});
        });
        std::sort(m_rows.begin(), m_rows.end(), [](const Row& a, const Row& b) { return a.path < b.path; });
        m_rowsVersion = database.GetVersion();
    }

    void AssetBrowser::DrawList(engine::Application& app)
    {
        const engine::AssetDatabase& database = app.GetAssetDatabase();
        if (m_rowsVersion != database.GetVersion())
        {
            RebuildRows(database); // 表が作り直されたときだけ(毎フレームは写さない)
        }

        // 最初は右側、Scene の窓(右上、高さ 400)の下に出す。
        const float width = 360.0f;
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float x = display.x - width - 16.0f;
        ImGui::SetNextWindowPos(ImVec2(x > 16.0f ? x : 16.0f, 432.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, 260.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Assets"))
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##filter", "Filter by path", &m_filter);
            ImGui::BeginChild("AssetRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            for (std::size_t i = 0; i < m_rows.size(); ++i)
            {
                const Row& row = m_rows[i];
                if (!m_filter.empty() && row.path.find(m_filter) == std::string::npos)
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                if (row.isData)
                {
                    if (ImGui::SmallButton("Open"))
                    {
                        OpenData(app, row.guid);
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(row.path.c_str());
                }
                else
                {
                    ImGui::TextDisabled("%s", row.path.c_str()); // まだ開けない種類(テクスチャ、プレハブなど)
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }

    bool AssetBrowser::DrawDataWindow(engine::Application& app, EditorCommandRegistry* commands, DataWindow& window)
    {
        core::ObjectRegistry& registry = app.GetRegistry();
        engine::AssetLoader& loader = app.GetAssetLoader();

        // 中身を引く(読み直されていれば、新しい箱に乗り換える)。箱は根に入れて、開いている間は回収させない。
        const engine::ScriptableData* data = loader.ResolveScriptableData(window.reference, engine::ScriptableData::StaticType());
        if (window.reference.asset != window.rootedBox)
        {
            if (!window.rootedBox.IsNull())
            {
                registry.RemoveRoot(window.rootedBox);
            }
            window.rootedBox = window.reference.asset;
            if (!window.rootedBox.IsNull())
            {
                registry.AddRoot(window.rootedBox);
            }
        }
        // 元の中身が変わった(data.set、Undo、ほかからの読み直し)なら、写しを作り直す。直している途中は待つ。
        if (data != nullptr && data->GetHandle() != window.source && !window.dirty && !ImGui::IsAnyItemActive())
        {
            RefreshCopy(app, window);
        }

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float x = display.x - 360.0f - 16.0f - 340.0f;
        ImGui::SetNextWindowPos(ImVec2(x > 16.0f ? x : 16.0f, 432.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(330.0f, 260.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(window.title.c_str(), &window.open))
        {
            ImGui::End(); // 畳まれている(Begin が false でも End は呼ぶ決まり)
            return window.open;
        }

        core::Object* copy = registry.Resolve(window.copy);
        if (data == nullptr || copy == nullptr)
        {
            const engine::ScriptableDataAsset* box = loader.ResolveScriptableDataAsset(window.reference.asset);
            ImGui::TextUnformatted(box != nullptr && box->GetState() == engine::AssetState::Loading
                                       ? "Loading..."
                                       : "Could not load this data (see the log)");
            ImGui::End();
            return window.open;
        }

        ImGui::Text("Type: %s", data->GetType().GetName());
        ImGui::TextDisabled("guid: %s", window.guidText.c_str());
        ImGui::Separator();

        // data.set が今は使えない(再生中など)なら、読むだけで出す(写しは書き換えさせない)。
        // Find は std::string を受け取るので、文字列の定数を渡すと毎フレーム一時的な std::string ができる
        // (MSVC の Debug の STL は、短い文字列でも確保する)。名前は1回だけ作って使い回す。
        static const std::string SetDataName = "data.set";
        const EditorCommand* setData = commands != nullptr ? commands->Find(SetDataName) : nullptr;
        const char* why = setData != nullptr && setData->whyUnavailable ? setData->whyUnavailable(app) : nullptr;
        if (commands != nullptr && setData == nullptr)
        {
            why = "data.set is not registered";
        }
        ImGui::BeginDisabled(why != nullptr);
        if (DrawProperties(copy->GetType(), copy, registry, &app.GetAssetDatabase()))
        {
            window.dirty = true;
        }
        ImGui::EndDisabled();
        if (why != nullptr)
        {
            ImGui::TextDisabled("Read only: %s", why);
        }
        ImGui::End();

        // 部品から手を離したら(どの部品も操作中でない)、変わった値をファイルに書く。ドラッグの途中では書かない。
        if (window.dirty && commands != nullptr && why == nullptr && !ImGui::IsAnyItemActive())
        {
            Commit(app, *commands, window);
        }
        return window.open;
    }

    void AssetBrowser::RefreshCopy(engine::Application& app, DataWindow& window)
    {
        const engine::ScriptableData* data =
            app.GetAssetLoader().ResolveScriptableData(window.reference, engine::ScriptableData::StaticType());
        if (data == nullptr)
        {
            return;
        }
        // 前の写しを捨てる(箱は根に入れたまま)。
        if (!window.copy.IsNull())
        {
            app.GetRegistry().RemoveRoot(window.copy);
            if (core::Object* old = app.GetRegistry().Resolve(window.copy))
            {
                old->Destroy();
            }
            window.copy = {};
        }
        // 同じ型の、読むだけの印の無いオブジェクトを作り、今の値を入れる(Property 越し。ObjectSerializer)。
        core::Object* copy = app.GetTypeRegistry().Create(data->GetType().GetName(), app.GetRegistry());
        if (copy == nullptr)
        {
            std::printf("アセットの窓: %s の写しを作れません(型が登録されていないか、メモリが足りません)\n",
                        data->GetType().GetName());
            return;
        }
        engine::ObjectSerializer::ReadProperties(engine::ObjectSerializer::Write(*data), *copy);
        app.GetRegistry().AddRoot(copy->GetHandle()); // 写しはどこからも指されないので、根に入れて GC から守る
        window.copy = copy->GetHandle();
        window.source = data->GetHandle();
        window.dirty = false;
    }

    void AssetBrowser::Commit(engine::Application& app, EditorCommandRegistry& commands, DataWindow& window)
    {
        window.dirty = false;
        const engine::ScriptableData* data =
            app.GetAssetLoader().ResolveScriptableData(window.reference, engine::ScriptableData::StaticType());
        const core::Object* copy = app.GetRegistry().Resolve(window.copy);
        if (data == nullptr || copy == nullptr)
        {
            return;
        }
        const engine::ArchiveValue before = engine::ObjectSerializer::Write(*data);
        const engine::ArchiveValue after = engine::ObjectSerializer::Write(*copy);
        const engine::ArchiveValue* oldValues = before.Find("properties");
        const engine::ArchiveValue* newValues = after.Find("properties");
        if (oldValues == nullptr || newValues == nullptr)
        {
            return;
        }
        // 違うプロパティ(上の段の名前。構造体はまるごと)ごとに data.set を1回。ふつうは1つだけ(1つの部品を動かしたので)。
        for (std::size_t i = 0; i < newValues->GetMemberCount(); ++i)
        {
            const std::string& name = newValues->GetMemberName(i);
            const engine::ArchiveValue* old = oldValues->Find(name);
            if (old != nullptr && old->Equals(newValues->GetMemberValue(i)))
            {
                continue;
            }
            engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
            args.Set("asset", engine::ArchiveValue(window.guidText));
            args.Set("property", engine::ArchiveValue(name));
            args.Set("value", newValues->GetMemberValue(i));
            commands.Execute("data.set", app, args); // 結果はログと Debug の窓に出る
        }
        // 成功しても失敗しても、次のフレームで今の中身から写しを作り直す(失敗したら、直した値は元に戻る)。
        window.source = {};
    }

    void AssetBrowser::ReleaseCopy(engine::Application& app, DataWindow& window)
    {
        core::ObjectRegistry& registry = app.GetRegistry();
        if (!window.copy.IsNull())
        {
            registry.RemoveRoot(window.copy);
            if (core::Object* copy = registry.Resolve(window.copy))
            {
                copy->Destroy();
            }
            window.copy = {};
        }
        if (!window.rootedBox.IsNull())
        {
            registry.RemoveRoot(window.rootedBox);
            window.rootedBox = {};
        }
    }
}

#endif
