// Inspector.h の実装。製品(Release)では中身が空になる。
#include "tools/Inspector.h"

#if HUSKY_DEVELOPMENT

#include "core/Property.h"
#include "engine/Actor.h"
#include "engine/Collider2D.h"
#include "engine/AssetLoader.h"
#include "engine/Component.h"
#include "engine/Prefab.h" // プレハブとの違い(印と、Revert / Apply のボタン)
#include "tools/EditorCommands.h" // Revert all / Apply all のボタンが、コマンドを名前で呼ぶ
#include "engine/RenderReflection.h" // render::Color の型情報(色は色の選び方で編集するため)
#include "engine/SpriteAnimatorComponent.h" // アニメーションの ID を名前で選ぶ表示
#include "engine/SpriteComponent.h"   // 切り出しの ID を名前で選ぶ表示
#include "engine/SpriteSheetAsset.h"

#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(std::string*)

#include <algorithm> // std::sort
#include <cstdint>
#include <cstring> // std::strlen、std::memcpy
#include <string>
#include <utility>   // std::pair
#include <vector>

namespace husky::tools
{
    namespace
    {
        using core::Property;
        using core::PropertyType;

        // ---- プロパティごとの表示の差し替え ----

        struct DrawerEntry
        {
            const Property* property; // 差し替えるプロパティ(TypeInfo の中にあるもの。登録の後は場所が変わらない)
            PropertyDrawer drawer;
        };

        // 差し替えの一覧。関数の中の static 変数なので、最初に呼ばれたときに1回だけ作られる
        // (グローバル変数にしないのは、ほかのグローバル変数の初期化との順番の問題を避けるため)。
        std::vector<DrawerEntry>& Drawers()
        {
            static std::vector<DrawerEntry> drawers;
            return drawers;
        }

        PropertyDrawer FindDrawer(const Property& property);

        // プレハブと違う値に印を付けるための情報(PF-5c)。描いている道筋("transform.position.x")を、
        // 決まった大きさの文字の並びに積む(毎フレームの文字列の確保を避けるため。std::string は伸びるたびに確保する)。
        struct OverrideMarks
        {
            const std::vector<std::pair<std::string, std::string>>* overrides = nullptr; // (コンポーネントの型の名前, 道筋)
            const char* component = "";                                                 // 描いている持ち主(アクターなら "")
            char path[256] = {};
            std::size_t length = 0;
            bool overflow = false; // 道筋が長すぎて積めなかった(印を付けない)

            // 道筋に名前を1つ足す。足せなければ false。
            bool Push(const char* name)
            {
                const std::size_t nameLength = std::strlen(name);
                const std::size_t dot = length > 0 ? 1 : 0;
                if (overflow || length + dot + nameLength >= sizeof(path))
                {
                    overflow = true;
                    return false;
                }
                if (dot != 0)
                {
                    path[length++] = '.';
                }
                std::memcpy(path + length, name, nameLength);
                length += nameLength;
                path[length] = '\0';
                return true;
            }

            // Push の前の長さに戻す。
            void Pop(std::size_t savedLength)
            {
                length = savedLength;
                path[length] = '\0';
                overflow = false;
            }

            // 今の道筋そのもの、またはその中(構造体のメンバー)に、プレハブとの違いがあるか。
            bool IsOverridden() const
            {
                for (const auto& [owner, overridePath] : *overrides)
                {
                    if (owner == component && overridePath.compare(0, length, path, length) == 0 &&
                        (overridePath.size() == length || overridePath[length] == '.'))
                    {
                        return true;
                    }
                }
                return false;
            }
        };

        bool DrawPropertiesImpl(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry,
                                const engine::AssetDatabase* assets, PropertyEditRecorder* recorder,
                                OverrideMarks* marks);

        // SpriteComponent::sprite(切り出しの ID)を、スプライトシートの切り出しの名前から選ぶ箱で並べる。
        // スプライトシートがまだ読めていなければ、ID の数字を表示するだけ。
        bool DrawSpriteSlicePicker(const Property& property, void* container, core::ObjectRegistry& registry,
                                   const engine::AssetDatabase* /*assets*/)
        {
            // container はこのプロパティの持ち主(SpriteComponent)の先頭。単一継承なので、そのまま型を戻せる。
            auto* component = static_cast<engine::SpriteComponent*>(container);
            std::int32_t* id = property.GetValuePtrAt<std::int32_t>(container);
            const engine::SpriteSheetAsset* sheet = registry.Resolve<engine::SpriteSheetAsset>(component->sheet.asset);
            if (sheet == nullptr || !sheet->IsLoaded() || sheet->GetGuid() != component->sheet.guid)
            {
                ImGui::Text("%s: %d (sheet not loaded)", property.GetName(), *id);
                return false;
            }

            const engine::SpriteSlice* current = sheet->FindSlice(*id);
            const char* preview = *id == 0 ? "(none)" : (current != nullptr ? current->name.c_str() : "(missing)");
            bool changed = false;
            if (ImGui::BeginCombo(property.GetName(), preview))
            {
                if (ImGui::Selectable("(none)", *id == 0))
                {
                    *id = 0;
                    changed = true;
                }
                // 切り出しは、ファイルに書いた順に並べる(名前が同じでも ID で区別できるように、PushID で区切る)。
                for (const engine::SpriteSlice& slice : sheet->GetSlices())
                {
                    ImGui::PushID(slice.id);
                    if (ImGui::Selectable(slice.name.c_str(), slice.id == *id) && slice.id != *id)
                    {
                        *id = slice.id;
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // SpriteAnimatorComponent::animation(アニメーションの ID)を、アニメーションの名前から選ぶ箱で並べる。
        // アニメーションは、同じアクターの SpriteComponent が指すスプライトシートから探す。
        // SpriteComponent が無いか、スプライトシートがまだ読めていなければ、ID の数字を表示するだけ。
        bool DrawSpriteAnimationPicker(const Property& property, void* container, core::ObjectRegistry& registry,
                                       const engine::AssetDatabase* /*assets*/)
        {
            // container はこのプロパティの持ち主(SpriteAnimatorComponent)の先頭。単一継承なので、そのまま型を戻せる。
            auto* animator = static_cast<engine::SpriteAnimatorComponent*>(container);
            std::int32_t* id = property.GetValuePtrAt<std::int32_t>(container);
            engine::Actor* owner = animator->GetOwner();
            const engine::SpriteComponent* sprite = owner != nullptr ? owner->GetComponent<engine::SpriteComponent>() : nullptr;
            const engine::SpriteSheetAsset* sheet = sprite != nullptr ? engine::FindLoadedSpriteSheet(*sprite, registry) : nullptr;
            if (sheet == nullptr)
            {
                ImGui::Text("%s: %d (sheet not loaded)", property.GetName(), *id);
                return false;
            }

            const engine::SpriteAnimation* current = sheet->FindAnimation(*id);
            const char* preview = *id == 0 ? "(none)" : (current != nullptr ? current->name.c_str() : "(missing)");
            bool changed = false;
            if (ImGui::BeginCombo(property.GetName(), preview))
            {
                if (ImGui::Selectable("(none)", *id == 0))
                {
                    *id = 0;
                    changed = true;
                }
                for (const engine::SpriteAnimation& clip : sheet->GetAnimations())
                {
                    ImGui::PushID(clip.id);
                    if (ImGui::Selectable(clip.name.c_str(), clip.id == *id) && clip.id != *id)
                    {
                        *id = clip.id; // 時間を 0 に戻すのは、アニメーター自身(次の Tick で ID の変化に気づく)
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // 最初から入っている差し替えを登録する(1回だけ)。
        void RegisterBuiltInDrawers()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }
            registered = true;
            RegisterPropertyDrawer(engine::SpriteComponent::StaticType(), "sprite", DrawSpriteSlicePicker);
            RegisterPropertyDrawer(engine::SpriteAnimatorComponent::StaticType(), "animation", DrawSpriteAnimationPicker);
        }

        PropertyDrawer FindDrawer(const Property& property)
        {
            RegisterBuiltInDrawers();
            for (const DrawerEntry& entry : Drawers())
            {
                if (entry.property == &property)
                {
                    return entry.drawer;
                }
            }
            return nullptr;
        }

        // ハンドルが指している相手を、短い文字で表す(名前があるアクターなら名前、それ以外は型の名前)。
        // 文字列を作らずに、相手が持っている文字列をそのまま返す(毎フレームのメモリの確保を避けるため)。
        // 返す文字は、次に呼ぶまでだけ有効(コンポーネントの「アクター / 型」は、使い回す文字の並びに書くため)。
        const char* DescribeHandle(core::ObjectHandle handle, core::ObjectRegistry& registry)
        {
            if (handle.IsNull())
            {
                return "(none)";
            }
            const core::Object* object = registry.Resolve(handle);
            if (object == nullptr || object->IsPendingDestroy())
            {
                return "(missing)"; // もう無い(相手が消えた。保存すると null になる)
            }
            if (const engine::Actor* actor = core::Cast<engine::Actor>(object); actor != nullptr && !actor->name.empty())
            {
                return actor->name.c_str();
            }
            if (const engine::Component* component = core::Cast<engine::Component>(object))
            {
                // コンポーネントは「持ち主のアクター / 型」(決まった大きさの文字の並び。毎フレームの確保を避ける)
                static char text[160];
                const engine::Actor* owner = component->GetOwner();
                std::snprintf(text, sizeof(text), "%s / %s",
                              owner != nullptr && !owner->name.empty() ? owner->name.c_str() : "(actor)",
                              component->GetType().GetName());
                return text;
            }
            return object->GetType().GetName();
        }

        // 参照の相手を選べるシーン(SceneInspector::Draw が描く間だけ設定する。無ければ、参照は表示だけ)。
        engine::Scene* g_pickScene = nullptr;

        // 参照(ObjectHandle / ObjectRef。REF-1)を、相手を選ぶ箱(コンボボックス)で描く。
        // allowed: 選べる型(ObjectRef<T> の T。nullptr ならアクターなら何でも)。コンポーネントの型なら、各アクターのその型のものを並べる。
        // 選んだら、ハンドルをその場で書き換えて true(記録係が property.set の1件にする)。
        bool DrawReferencePicker(const char* name, core::ObjectHandle& handle, const core::TypeInfo* allowed,
                                 core::ObjectRegistry& registry)
        {
            if (g_pickScene == nullptr)
            {
                ImGui::Text("%s: %s", name, DescribeHandle(handle, registry));
                return false;
            }
            bool changed = false;
            if (!ImGui::BeginCombo(name, DescribeHandle(handle, registry)))
            {
                return false;
            }
            if (ImGui::Selectable("(none)", handle.IsNull()) && !handle.IsNull())
            {
                handle = {};
                changed = true;
            }
            const bool wantComponents = allowed != nullptr && allowed->IsA(engine::Component::StaticType());
            // 候補を選んだときの書き換え(アクターでもコンポーネントでも同じ)
            auto offer = [&](core::ObjectHandle candidate) {
                ImGui::PushID(static_cast<int>(candidate.index));
                if (ImGui::Selectable(DescribeHandle(candidate, registry), candidate == handle) && candidate != handle)
                {
                    handle = candidate;
                    changed = true;
                }
                ImGui::PopID();
            };
            g_pickScene->ForEachActor([&](engine::Actor& actor) {
                if (!wantComponents)
                {
                    if (allowed == nullptr || actor.GetType().IsA(*allowed))
                    {
                        offer(actor.GetHandle());
                    }
                    return;
                }
                for (const core::ObjectHandle componentHandle : actor.components)
                {
                    const core::Object* component = registry.Resolve(componentHandle);
                    if (component != nullptr && !component->IsPendingDestroy() && component->GetType().IsA(*allowed))
                    {
                        offer(componentHandle);
                    }
                }
            });
            ImGui::EndCombo();
            return changed;
        }

        // アセットの参照を、選択肢の箱(コンボボックス)で並べる。選べるのは、assetType として読めるアセットだけ。
        // 選び直したら GUID だけを書き換え、読んであったアセットのハンドルは手放す(描くときに、ローダが新しい GUID で読む)。
        bool DrawAssetRef(const char* name, core::AssetReference& reference, const core::TypeInfo& assetType,
                          const engine::AssetDatabase* assets)
        {
            // 今の値の表示: 表にあればパス、無ければ GUID(ファイルが消えた、.meta が壊れたなど)。
            // ふだん(何も指さない、表にある)は、文字列を作らずに、ある文字列をそのまま使う(毎フレームの確保を避ける)。
            const std::string* currentPath = assets != nullptr ? assets->FindPath(reference.guid) : nullptr;
            std::string missing; // 表に無いときだけ作る
            const char* preview = "(none)";
            if (currentPath != nullptr)
            {
                preview = currentPath->c_str();
            }
            else if (!reference.IsNull())
            {
                missing = "(missing) " + reference.guid.ToString();
                preview = missing.c_str();
            }
            if (assets == nullptr)
            {
                ImGui::Text("%s: %s", name, preview);
                return false;
            }

            bool changed = false;
            // BeginCombo: 箱が開いているときだけ true を返す。中身を作ったら EndCombo で閉じる。
            // 一覧を集めて並べ替えるのは、箱が開いている間だけ(閉じているときは何もしないので、毎フレームの負担が無い)。
            if (ImGui::BeginCombo(name, preview))
            {
                if (ImGui::Selectable("(none)", reference.IsNull()))
                {
                    reference.guid = {};
                    reference.asset = {};
                    changed = true;
                }
                std::vector<std::pair<std::string, core::Guid>> choices; // (パス, GUID)。パスの順に並べるため
                assets->ForEachAsset([&](const core::Guid& guid, const std::string& path) {
                    if (engine::AssetLoader::CanLoadAs(assetType, path))
                    {
                        choices.emplace_back(path, guid);
                    }
                });
                std::sort(choices.begin(), choices.end());
                for (const auto& [path, guid] : choices)
                {
                    if (ImGui::Selectable(path.c_str(), guid == reference.guid) && guid != reference.guid)
                    {
                        reference.guid = guid;
                        reference.asset = {};
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        // プロパティ1つを、型に合った部品(チェックボックス、数の入力、文字の入力など)で並べる。
        // 配列(SF-2b)を、折りたためる見出しの中に要素ごとに描く。DrawProperty の中で使うので、先に宣言だけする。
        bool DrawArray(const Property& property, void* container, core::ObjectRegistry& registry,
                       const engine::AssetDatabase* assets, PropertyEditRecorder* recorder);

        bool DrawProperty(const Property& property, void* container, core::ObjectRegistry& registry,
                          const engine::AssetDatabase* assets, PropertyEditRecorder* recorder,
                                OverrideMarks* marks)
        {
            const char* name = property.GetName();
            switch (property.GetType())
            {
            case PropertyType::Bool:
                return ImGui::Checkbox(name, property.GetValuePtrAt<bool>(container));
            case PropertyType::Int32:
                // DragInt: マウスで左右にドラッグすると値が変わる。ダブルクリックで直接打ち込める。
                return ImGui::DragInt(name, property.GetValuePtrAt<std::int32_t>(container));
            case PropertyType::Float:
                return ImGui::DragFloat(name, property.GetValuePtrAt<float>(container), 0.5f);
            case PropertyType::Int64:
                // DragScalar: 型を指定してドラッグする部品(DragInt / DragFloat は int と float 専用のため)
                return ImGui::DragScalar(name, ImGuiDataType_S64, property.GetValuePtrAt<std::int64_t>(container));
            case PropertyType::Double:
                return ImGui::DragScalar(name, ImGuiDataType_Double, property.GetValuePtrAt<double>(container), 0.5f);
            case PropertyType::Enum:
            {
                // enum(SF-3b): 値の名前の選択肢から選ぶ(登録していない値なら、数を見出しに出す)
                const core::EnumInfo& info = *property.GetEnumInfo();
                void* address = property.GetEnumAddressAt(container);
                const std::int64_t current = info.get(address);
                const char* currentName = info.FindName(current);
                char numberText[24]; // 決まった大きさの文字の並び(毎フレームの確保を避ける)
                if (currentName == nullptr)
                {
                    std::snprintf(numberText, sizeof(numberText), "%lld", static_cast<long long>(current));
                    currentName = numberText;
                }
                bool changed = false;
                if (ImGui::BeginCombo(name, currentName))
                {
                    for (const core::EnumInfo::Entry& entry : info.entries)
                    {
                        if (ImGui::Selectable(entry.name, entry.value == current) && entry.value != current)
                        {
                            info.set(address, entry.value);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }
            case PropertyType::String:
                return ImGui::InputText(name, property.GetValuePtrAt<std::string>(container));
            case PropertyType::ObjectHandle:
                // 型の無い参照: シーンのアクターから選ぶ(REF-1)
                return DrawReferencePicker(name, *property.GetValuePtrAt<core::ObjectHandle>(container), nullptr, registry);
            case PropertyType::ObjectRef:
                // 型の付いた参照: ObjectRef<T> の T に合うものだけを候補にする(REF-1)
                return DrawReferencePicker(name, property.GetValuePtrAt<core::ObjectReference>(container)->handle,
                                           property.GetObjectRefType(), registry);
            case PropertyType::Array:
                return DrawArray(property, container, registry, assets, recorder);
            case PropertyType::ObjectHandleArray:
            {
                const auto& handles = *property.GetValuePtrAt<std::vector<core::ObjectHandle>>(container);
                // TreeNode: 折りたためる見出し。開いているときだけ true を返し、中身を作ったら TreePop で閉じる。
                if (ImGui::TreeNode(name, "%s (%zu)", name, handles.size()))
                {
                    for (const core::ObjectHandle handle : handles)
                    {
                        ImGui::BulletText("%s", DescribeHandle(handle, registry));
                    }
                    ImGui::TreePop();
                }
                return false;
            }
            case PropertyType::Struct:
            {
                void* structAddress = property.GetStructAddressAt(container);
                // 色は、数字4つより、色の選び方の部品で編集したほうが分かりやすいので、特別に扱う。
                // render::Color は float が r, g, b, a の順に4つ並んでいるので、先頭のアドレスを float[4] として渡せる。
                if (property.GetStructType() == core::StructTypeOf<render::Color>())
                {
                    return ImGui::ColorEdit4(name, &static_cast<render::Color*>(structAddress)->r);
                }
                // それ以外の構造体は、折りたためる見出しの中に、中のメンバーを同じやり方で並べる。
                bool changed = false;
                if (ImGui::TreeNode(name))
                {
                    changed = DrawPropertiesImpl(*property.GetStructType(), structAddress, registry, assets, recorder, marks);
                    ImGui::TreePop();
                }
                return changed;
            }
            case PropertyType::AssetRef:
                return DrawAssetRef(name, *property.GetValuePtrAt<core::AssetReference>(container),
                                    *property.GetAssetType(), assets);
            case PropertyType::Guid:
            {
                // 変わらない ID は、人が書き換えるものではないので、灰色の文字で読むだけにする(人は名前で区別すればよい)。
                // GUID の文字列の形(8-4-4-4-12 桁)を、文字列を作らずに書く(毎フレームの確保を避ける)。
                const core::Guid& guid = *property.GetValuePtrAt<core::Guid>(container);
                ImGui::TextDisabled("%s: %08llx-%04llx-%04llx-%04llx-%012llx", name,
                                    static_cast<unsigned long long>(guid.high >> 32),
                                    static_cast<unsigned long long>((guid.high >> 16) & 0xffffu),
                                    static_cast<unsigned long long>(guid.high & 0xffffu),
                                    static_cast<unsigned long long>(guid.low >> 48),
                                    static_cast<unsigned long long>(guid.low & 0xffffffffffffull));
                return false;
            }
            }
            return false;
        }

        bool DrawArray(const Property& property, void* container, core::ObjectRegistry& registry,
                       const engine::AssetDatabase* assets, PropertyEditRecorder* recorder)
        {
            const core::ArrayInfo& array = *property.GetArrayInfo();
            void* address = property.GetArrayAddressAt(container);
            const std::size_t count = array.size(address);
            // 見出し: 名前と要素の数。開いているときだけ中を描く(閉じていれば、要素の部品は作らない)。
            if (!ImGui::TreeNode(property.GetName(), "%s (%zu)", property.GetName(), count))
            {
                return false;
            }
            if (recorder != nullptr)
            {
                recorder->EnterArray(); // ここから中は、道筋を配列のプロパティで止める(配列まるごとを property.set する)
            }
            bool changed = false;
            std::size_t removeAt = count; // 「-」を押した要素(count なら無し)
            for (std::size_t i = 0; i < count; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("-"))
                {
                    removeAt = i;
                }
                ImGui::SameLine();
                ImGui::Text("%zu", i);
                ImGui::SameLine();
                bool elementChanged = false;
                if (array.IsProxy())
                {
                    // 要素の場所が取れない配列(std::vector<bool>): 手元の変数に写して描き、変わったら書き戻す
                    bool temporary = false;
                    array.load(address, i, &temporary);
                    if (recorder != nullptr)
                    {
                        recorder->BeforeLeaf(array.element, &temporary);
                    }
                    elementChanged = ImGui::Checkbox(array.element.GetName(), &temporary);
                    if (recorder != nullptr)
                    {
                        // 記録係は、変わったときに配列まるごとを前の値として読む(まだ書き戻していないので、配列は前の値のまま)
                        recorder->AfterLeaf(array.element, &temporary, elementChanged, ImGui::IsItemActive());
                    }
                    if (elementChanged)
                    {
                        array.store(address, i, &temporary);
                    }
                }
                else
                {
                    // 要素1つを、ふつうのプロパティと同じ部品で描く(要素のプロパティの名前は "##element" なので、ラベルは出ない)
                    void* element = array.at(address, i);
                    if (recorder != nullptr)
                    {
                        recorder->BeforeLeaf(array.element, element);
                    }
                    elementChanged = DrawProperty(array.element, element, registry, assets, recorder, nullptr);
                    if (recorder != nullptr)
                    {
                        recorder->AfterLeaf(array.element, element, elementChanged, ImGui::IsItemActive());
                    }
                }
                changed |= elementChanged;
                ImGui::PopID();
            }
            const bool add = ImGui::SmallButton("+");
            if (add || removeAt < count)
            {
                // 足す・消すは、部品を通さない変更: 前の値を控えてから変え、すぐに確定する(Undo できる)
                if (recorder != nullptr)
                {
                    recorder->BeginWholeChange();
                }
                if (add)
                {
                    array.resize(address, count + 1); // 増えた要素は既定の値
                }
                else
                {
                    array.erase(address, removeAt);
                }
                if (recorder != nullptr)
                {
                    recorder->CommitWholeChange();
                }
                changed = true;
            }
            if (recorder != nullptr)
            {
                recorder->LeaveArray();
            }
            ImGui::TreePop();
            return changed;
        }

        // recorder: 値の書き換えを property.set の1件にまとめる記録係(nullptr なら記録せず、その場で書き換えるだけ)。
        bool DrawPropertiesImpl(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry,
                                const engine::AssetDatabase* assets, PropertyEditRecorder* recorder,
                                OverrideMarks* marks)
        {
            bool changed = false;
            type.ForEachProperty([&](const Property& property) {
                // PushID: ImGui は部品を「名前」で見分けるので、同じ名前(x など)が別の場所にあっても混ざらないように、区切りを付ける。
                ImGui::PushID(property.GetName());
                // 道筋("transform" → "position" → "x")を積む。構造体の中に入るときも、ここで積まれる。
                if (recorder != nullptr)
                {
                    recorder->PushPath(property);
                    recorder->BeforeLeaf(property, container); // 値1つの部品なら、今の値を控える
                }
                // プレハブと違う値(か、中に違いのある構造体)なら、左に青い線を引く(Unity の印と同じ)。
                const std::size_t savedLength = marks != nullptr ? marks->length : 0;
                if (marks != nullptr && marks->Push(property.GetName()) && marks->IsOverridden())
                {
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x - 6.0f, at.y),
                                                              ImVec2(at.x - 3.0f, at.y + ImGui::GetFrameHeight()),
                                                              IM_COL32(70, 150, 255, 255));
                }
                // 差し替えが登録されていれば、そちらで表示する。無ければ、型に合った標準の部品で表示する。
                const PropertyDrawer drawer = FindDrawer(property);
                const bool propertyChanged = drawer != nullptr ? drawer(property, container, registry, assets)
                                                               : DrawProperty(property, container, registry, assets, recorder, marks);
                if (recorder != nullptr)
                {
                    // IsItemActive: 直前の部品が操作中(ドラッグ中、文字の入力中など)か。手を離すと false になる。
                    recorder->AfterLeaf(property, container, propertyChanged, ImGui::IsItemActive());
                    recorder->PopPath();
                }
                if (marks != nullptr)
                {
                    marks->Pop(savedLength);
                }
                changed |= propertyChanged;
                ImGui::PopID();
            });
            return changed;
        }
    }

    bool DrawProperties(const core::TypeInfo& type, void* container, core::ObjectRegistry& registry,
                        const engine::AssetDatabase* assets)
    {
        return DrawPropertiesImpl(type, container, registry, assets, nullptr, nullptr);
    }

    bool RegisterPropertyDrawer(const core::TypeInfo& type, const char* propertyName, PropertyDrawer drawer)
    {
        const Property* property = type.FindProperty(propertyName);
        if (property == nullptr)
        {
            return false;
        }
        for (DrawerEntry& entry : Drawers())
        {
            if (entry.property == property)
            {
                entry.drawer = drawer; // 同じプロパティなら置き換える
                return true;
            }
        }
        Drawers().push_back(DrawerEntry{property, drawer});
        return true;
    }

    void SceneInspector::Draw(engine::Application& app, EditorCommandRegistry* commands)
    {
        engine::Scene& scene = app.GetScene();
        core::ObjectRegistry& registry = app.GetRegistry();
        m_recorder.BeginFrame(app, commands);
        g_pickScene = &scene; // 参照の相手は、このシーンのアクター・コンポーネントから選ぶ(描き終わったら外す)

        // 最初は画面の右上に出す(左上のデバッグの窓は、コマンドが増えると下へ伸びるので、その下に置くと重なる)。
        const float width = 360.0f;
        const float x = ImGui::GetIO().DisplaySize.x - width - 16.0f;
        ImGui::SetNextWindowPos(ImVec2(x > 16.0f ? x : 16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(width, 400.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene"))
        {
            // --- アクターの一覧 ---
            ImGui::Text("Actors: %zu", scene.actors.size());
            ImGui::BeginChild("Actors", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders);
            // ImGuiListClipper: 見えている行だけを作る(1000 行あっても、画面に入る十数行ぶんしか処理しない)。
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(scene.actors.size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const core::ObjectHandle handle = scene.actors[static_cast<std::size_t>(i)];
                    const engine::Actor* actor = registry.Resolve<engine::Actor>(handle);
                    if (actor == nullptr)
                    {
                        continue;
                    }
                    const char* label = actor->name.empty() ? actor->GetType().GetName() : actor->name.c_str();
                    ImGui::PushID(i);
                    if (ImGui::Selectable(label, handle == m_selected))
                    {
                        m_selected = handle;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            // --- 選んだアクター ---
            engine::Actor* selected = registry.Resolve<engine::Actor>(m_selected);
            if (selected == nullptr || selected->IsPendingDestroy())
            {
                ImGui::TextDisabled("(no actor selected)");
            }
            else
            {
                ImGui::SeparatorText(selected->GetType().GetName());

                // --- プレハブ(PF-5c) ---
                // 違いの一覧は、選び直したときと 30 フレームごとにだけ作り直す(毎フレーム比べると、値の木を毎回作るため)。
                if (m_selected != m_overridesFor || ++m_overridesAge >= 30)
                {
                    RefreshOverrides(*selected, app.GetTypeRegistry());
                }
                if (!selected->prefab.IsNull())
                {
                    const std::string* prefabPath = app.GetAssetDatabase().FindPath(selected->prefab.guid);
                    ImGui::Text("Prefab: %s (%zu overrides)", prefabPath != nullptr ? prefabPath->c_str() : "(missing)",
                                m_overrides.size());
                    // 足した・外したコンポーネントは、値の違いとは別に数を出す(Revert all / Apply all の対象ではない)。
                    // 外したものは1つずつ並べ、Revert(prefab.revertComponent)でプレハブの値で戻せる(PF-7d)。
                    // 足したものの Revert は、そのコンポーネントの Remove component。
                    if (!m_addedComponentIndexes.empty() || !m_removedComponents.empty())
                    {
                        ImGui::Text("Components: +%zu added, -%zu removed", m_addedComponentIndexes.size(),
                                    m_removedComponents.size());
                    }
                    const EditorCommand* revertComponent = commands != nullptr ? commands->Find("prefab.revertComponent") : nullptr;
                    const EditorCommand* applyComponent = commands != nullptr ? commands->Find("prefab.applyComponent") : nullptr;
                    for (std::size_t r = 0; r < m_removedComponents.size(); ++r)
                    {
                        ImGui::PushID(static_cast<int>(r));
                        ImGui::BulletText("removed: %s", m_removedComponents[r].first.c_str());
                        if (revertComponent != nullptr)
                        {
                            ImGui::SameLine();
                            ImGui::BeginDisabled(commands->GetUnavailableReason(*revertComponent, app) != nullptr);
                            if (ImGui::SmallButton("Revert"))
                            {
                                engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                                args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                                args.Set("componentId", engine::ArchiveValue(m_removedComponents[r].second));
                                commands->Execute("prefab.revertComponent", app, args);
                                m_overridesAge = 30; // 次のフレームで一覧を作り直す
                            }
                            ImGui::EndDisabled();
                        }
                        // Apply: 外したことをプレハブに書き戻す(プレハブから消し、同じプレハブから置いた全員から外す。PF-7e)
                        if (applyComponent != nullptr)
                        {
                            ImGui::SameLine();
                            ImGui::BeginDisabled(commands->GetUnavailableReason(*applyComponent, app) != nullptr);
                            if (ImGui::SmallButton("Apply"))
                            {
                                engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                                args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                                args.Set("removed", engine::ArchiveValue(m_removedComponents[r].second));
                                commands->Execute("prefab.applyComponent", app, args);
                                m_overridesAge = 30;
                            }
                            ImGui::EndDisabled();
                        }
                        ImGui::PopID();
                    }
                    // ボタンは名前でコマンドを呼ぶだけ(全部: 名前・位置・回転は除く)。押したら、すぐに印を作り直す。
                    if (commands != nullptr)
                    {
                        const std::pair<const char*, const char*> buttons[] = {{"prefab.revert", "Revert all"},
                                                                               {"prefab.apply", "Apply all"}};
                        for (const auto& [name, label] : buttons)
                        {
                            const EditorCommand* command = commands->Find(name);
                            if (command == nullptr)
                            {
                                continue;
                            }
                            if (label != buttons[0].second)
                            {
                                ImGui::SameLine(); // ボタンは文字の次の行に並べる(窓が狭くても切れないように)
                            }
                            ImGui::BeginDisabled(commands->GetUnavailableReason(*command, app) != nullptr);
                            if (ImGui::SmallButton(label))
                            {
                                engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                                args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                                commands->Execute(name, app, args);
                                m_overridesAge = 30;
                            }
                            ImGui::EndDisabled();
                        }
                    }
                }

                OverrideMarks marks;
                marks.overrides = &m_overrides;
                m_recorder.SetOwner(*selected); // ここから描くプロパティの持ち主(書き換えをコマンドにするため)
                DrawPropertiesImpl(selected->GetType(), selected, registry, &app.GetAssetDatabase(), &m_recorder, &marks);
                if (ImGui::Button("Destroy"))
                {
                    // 編集中は actor.destroy を通す(Ctrl+Z で戻せる)。再生中など、コマンドが使えないときは、
                    // 値の書き換えと同じく、その場で消すだけで履歴に残さない(Stop で元に戻る)。
                    // どちらも消えるのはフレームの終わり(弱い参照なので、選択は自然に外れる)。
                    const EditorCommand* destroy = commands != nullptr ? commands->Find("actor.destroy") : nullptr;
                    if (destroy != nullptr && commands->GetUnavailableReason(*destroy, app) == nullptr)
                    {
                        engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                        args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                        commands->Execute("actor.destroy", app, args);
                    }
                    else
                    {
                        selected->Destroy();
                    }
                }

                // --- コンポーネント ---
                // Remove を押したものは、一覧を回し終わってから外す(回している途中で一覧を変えると、i がずれるため)。
                engine::Component* removing = nullptr;
                std::size_t removingIndex = 0;
                for (std::size_t i = 0; i < selected->components.size(); ++i)
                {
                    engine::Component* component = registry.Resolve<engine::Component>(selected->components[i]);
                    if (component == nullptr || component->IsPendingDestroy())
                    {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::CollapsingHeader(component->GetType().GetName(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        if (std::find(m_addedComponentIndexes.begin(), m_addedComponentIndexes.end(), i) !=
                            m_addedComponentIndexes.end())
                        {
                            ImGui::TextDisabled("(added: not in the prefab)");
                            // 足したことをプレハブに書き戻す(プレハブに足し、同じプレハブから置いた全員に足す。PF-7e)。
                            // 型の名前で指すので、同じ型を2つ足していると、最初のものが書き戻される(今の制限)。
                            const EditorCommand* apply = commands != nullptr ? commands->Find("prefab.applyComponent") : nullptr;
                            if (apply != nullptr)
                            {
                                ImGui::SameLine();
                                ImGui::BeginDisabled(commands->GetUnavailableReason(*apply, app) != nullptr);
                                if (ImGui::SmallButton("Apply to prefab"))
                                {
                                    engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                                    args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                                    args.Set("added", engine::ArchiveValue(std::string(component->GetType().GetName())));
                                    commands->Execute("prefab.applyComponent", app, args);
                                    m_overridesAge = 30;
                                }
                                ImGui::EndDisabled();
                            }
                        }
                        m_recorder.SetOwner(*component);
                        marks.component = component->GetType().GetName();
                        DrawPropertiesImpl(component->GetType(), component, registry, &app.GetAssetDatabase(), &m_recorder,
                                           &marks);
                        if (ImGui::SmallButton("Remove component"))
                        {
                            removing = component;
                            removingIndex = i;
                        }
                    }
                    ImGui::PopID();
                }
                if (removing != nullptr)
                {
                    // 編集中は component.remove を通す(Ctrl+Z で同じ位置・同じ値で戻る)。位置で指すので、同じ型が2つあっても取り違えない。
                    // 使えないとき(再生中)は、Destroy ボタンと同じく、その場で外すだけで履歴に残さない(Stop で元に戻る)。
                    const EditorCommand* remove = commands != nullptr ? commands->Find("component.remove") : nullptr;
                    if (remove != nullptr && commands->GetUnavailableReason(*remove, app) == nullptr)
                    {
                        engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                        args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                        args.Set("component", engine::ArchiveValue(std::string(removing->GetType().GetName())));
                        args.Set("index", engine::ArchiveValue(static_cast<std::int64_t>(removingIndex)));
                        commands->Execute("component.remove", app, args);
                        m_overridesAge = 30; // 足した・外したものの一覧を、次のフレームで作り直す
                    }
                    else
                    {
                        selected->RemoveComponent(*removing);
                    }
                }

                // 型を選んで足す。一覧は型の登録簿をそのまま回す(名前は登録簿が持つ文字列なので、毎フレームの確保が無い)。
                ImGui::Separator();
                if (ImGui::BeginCombo("##addComponent", "Add component..."))
                {
                    const core::TypeRegistry& types = app.GetTypeRegistry();
                    for (std::size_t t = 0; t < types.GetCount(); ++t)
                    {
                        const core::TypeInfo* type = types.GetType(t);
                        if (type == nullptr || !type->IsA(engine::Component::StaticType()) ||
                            type == &engine::Component::StaticType()) // 何もしない基底そのものは並べない
                        {
                            continue;
                        }
                        if (ImGui::Selectable(type->GetName()))
                        {
                            const EditorCommand* add = commands != nullptr ? commands->Find("component.add") : nullptr;
                            if (add != nullptr && commands->GetUnavailableReason(*add, app) == nullptr)
                            {
                                engine::ArchiveValue args = engine::ArchiveValue::MakeObject();
                                args.Set("actor", engine::ArchiveValue(selected->guid.ToString()));
                                args.Set("type", engine::ArchiveValue(std::string(type->GetName())));
                                commands->Execute("component.add", app, args);
                                m_overridesAge = 30;
                            }
                            else if (engine::Component* added =
                                         core::Cast<engine::Component>(types.Create(type->GetName(), registry)))
                            {
                                if (engine::Collider2D* collider = core::Cast<engine::Collider2D>(added))
                                {
                                    engine::FitColliderToSprite(*collider, *selected); // コマンドと同じく、絵に合わせる
                                }
                                selected->InsertComponent(*added, selected->components.size()); // 再生中: 履歴に残さない
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
        ImGui::End();
        // 描き終わった: 編集の途中の部品が描かれなくなっていたら(選択を変えたなど)、ここで確定する。
        g_pickScene = nullptr;
        m_recorder.EndFrame();
    }

    void SceneInspector::RefreshOverrides(const engine::Actor& actor, const core::TypeRegistry& types)
    {
        m_overrides.clear(); // 入れ物は使い回す(中の文字列は作り直す)
        const engine::Prefab::OverridesResult found = engine::Prefab::FindOverrides(actor, &types);
        for (const engine::Prefab::Override& item : found.overrides)
        {
            m_overrides.emplace_back(item.component, item.path);
        }
        m_addedComponentIndexes.clear();
        for (const engine::Prefab::ComponentChange& added : found.addedComponents)
        {
            m_addedComponentIndexes.push_back(added.index);
        }
        m_removedComponents.clear();
        for (const engine::Prefab::ComponentChange& removed : found.removedComponents)
        {
            m_removedComponents.emplace_back(removed.type, removed.componentId.ToString());
        }
        m_overridesFor = actor.GetHandle();
        m_overridesAge = 0;
    }
}

#endif
