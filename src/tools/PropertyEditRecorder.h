// インスペクタでの値の書き換えを、エディタのコマンド(property.set)の1件にまとめる(ED-8b)。
// 製品(Release)には入らない(#if HUSKY_DEVELOPMENT)。
//
// なぜ要るか:
//   インスペクタの数の欄をマウスでドラッグすると、値は毎フレーム少しずつ変わる。毎フレーム property.set を呼ぶと、
//   Undo の履歴が何十件にもなり、Ctrl+Z を何十回も押さないと戻れない。そこで:
//     1. 最初に値が変わった瞬間に「前の値」を控える(編集の始まり)
//     2. ドラッグ中は、今までどおりその場で値を書き換える(動きを見ながら調整できるように)
//     3. 部品から手を離したら(操作中でなくなったら)、いったん前の値に戻し、property.set で最後の値にする
//   こうすると、値を変えるのはいつもコマンドになり、ドラッグ全体が1件の Undo になる。
//   チェックボックスや選択肢の箱は、変わったフレームにはもう操作中ではないので、その場で1件になる。
//
// 再生中は property.set が使えない(編集中だけのコマンド)。そのときは、値はその場で変わったまま、履歴には残さない
// (再生中の変更は Stop で消えるので、戻す必要が無い。Unity の再生中の変更と同じ)。
//
// ImGui を知らない(操作中かどうかは、呼ぶ側が ImGui::IsItemActive で調べて渡す)。そのため、マウスを使わずにテストできる。
//
// 使い方(Inspector.cpp):
//   recorder.BeginFrame(app, commands);
//   recorder.SetOwner(actor);                     // これから描く持ち主(アクターか、そのコンポーネント)
//   recorder.PushPath(property);                  // 構造体の中へ入るときも、同じように積む
//   recorder.BeforeLeaf(property, container);     // 値1つの部品を描く前
//   bool changed = ImGui::DragFloat(...);
//   recorder.AfterLeaf(property, container, changed, ImGui::IsItemActive());
//   recorder.PopPath();
//   recorder.EndFrame();                          // 描かれなくなった編集(選択を変えたなど)を、ここで確定する
//
// 動作スレッド: ゲームスレッドのみ。
#pragma once

#if HUSKY_DEVELOPMENT

#include "core/AssetRef.h"
#include "core/Object.h"
#include "core/Property.h"
#include "engine/Application.h"
#include "engine/ArchiveValue.h"
#include "render/RenderDevice.h" // render::Color

#include <cstddef>
#include <cstdint>
#include <string>

namespace husky::tools
{
    class EditorCommandRegistry;

    class PropertyEditRecorder
    {
    public:
        // 毎フレーム、描き始める前に呼ぶ。commands が nullptr なら、何も記録しない(値はその場で変わるだけ)。
        void BeginFrame(engine::Application& app, EditorCommandRegistry* commands);
        // これから描くプロパティの持ち主(アクターか、そのコンポーネント)。道筋は空に戻る。
        void SetOwner(const core::Object& owner);

        // プロパティの道筋("transform" → "position" → "x")を1段積む / 戻す。深すぎて積めなければ PushPath は false
        // (そのプロパティは記録しない。それでも PopPath は呼ぶ)。
        bool PushPath(const core::Property& property);
        void PopPath();

        // 値1つの部品を描く前に呼ぶ。今の値を控える(変わったときに、前の値が分かるように)。
        // 記録できない種類(ハンドル、guid など)なら何もしない。
        void BeforeLeaf(const core::Property& property, void* container);
        // 値1つの部品を描いた後に呼ぶ。changed: このフレームで値が変わったか。active: 部品がまだ操作中か(ドラッグ中など)。
        void AfterLeaf(const core::Property& property, void* container, bool changed, bool active);

        // 毎フレーム、描き終わった後に呼ぶ。編集の途中の部品が、このフレームに描かれなかったら(選択を変えた、
        // 見出しを閉じたなど)、そこで確定する。
        void EndFrame();

        // 配列(SF-2b)の要素を描く間だけ囲む。配列のプロパティまでを道筋にし、中(要素の構造体のメンバーなど)では
        // 道筋を積まない(要素の番号は道筋にしない)。中の値1つが変わると、配列まるごとを前の値として控え、
        // 確定するときは配列まるごとを property.set する(ドラッグ全体が1件の Undo)。
        void EnterArray() { ++m_inArray; }
        void LeaveArray() { --m_inArray; }

        // 配列の要素を足す・消すなど、部品を通さない変更の前後に呼ぶ(EnterArray の中で)。
        // 前で今の値(配列まるごと)を控え、後ですぐに確定する(property.set を1回)。
        void BeginWholeChange();
        void CommitWholeChange();

        // 編集の途中か(ドラッグ中など)。
        bool IsEditing() const { return m_editing; }

        // 道筋の深さの上限("transform.position.x" は 3)。
        static constexpr std::size_t MaxPathDepth = 8;

    private:
        // 記録できる値の種類。
        enum class Kind
        {
            None,
            Bool,
            Int32,
            Float,
            Int64,
            Double,
            Enum, // enum(数として控える。int64Value を使う)
            String,
            Color,    // render::Color(色の部品で、4つの値を一度に変える)
            AssetRef, // アセットの参照
            Reference, // ほかのオブジェクトへの参照(ObjectHandle / ObjectRef。REF-1。ハンドルを控える)
        };

        // 値1つの写し。文字列は、入れ物を使い回して毎フレームの確保を避ける。
        struct Snapshot
        {
            Kind kind = Kind::None;
            bool boolValue = false;
            std::int32_t intValue = 0;
            float floatValue = 0.0f;
            std::int64_t int64Value = 0;
            double doubleValue = 0.0;
            render::Color color;
            core::AssetReference assetRef;
            core::ObjectHandle handle;
            std::string text;
        };

        static Kind GetKind(const core::Property& property);
        static void Take(Kind kind, const core::Property& property, void* container, Snapshot& out);
        static void Apply(const Snapshot& snapshot, const core::Property& property, void* container);

        // 今描いている道筋が、編集の途中のものと同じか。
        bool IsEditingCurrentPath() const;
        // 持ち主の、道筋の値を、保存と同じ形の値(ArchiveValue)で読む。読めなければ Null。
        // 参照(REF-1)は、MakeReferenceArg の形(アクターの guid など)で読む(ハンドルは保存の形にならないため)。
        engine::ArchiveValue ReadValueAtPath(const core::Object& owner, const core::Property* const* path,
                                             std::size_t depth) const;
        // 編集を確定する: 前の値に戻し、property.set で今の値にする(履歴に1件積まれる)。
        void Commit();

        engine::Application* m_app = nullptr;
        EditorCommandRegistry* m_commands = nullptr;

        // ---- 今描いているもの(毎フレーム変わる)----
        core::ObjectHandle m_owner;
        const core::Property* m_path[MaxPathDepth] = {};
        std::size_t m_depth = 0;
        std::size_t m_overflow = 0; // 上限を超えて積めなかった段の数
        std::size_t m_inArray = 0;    // EnterArray の深さ(0 でなければ、PushPath は道筋を積まない)
        std::size_t m_suppressed = 0; // 配列の中で、積まずに数えただけの段の数(PopPath と数を合わせる)
        Snapshot m_snapshot;        // BeforeLeaf で控えた値
        Snapshot m_after;           // 編集の始まりに、変わった後の値を一時的に控える

        // ---- 編集の途中(ドラッグ中など)----
        bool m_editing = false;
        bool m_seenThisFrame = false; // このフレームで、編集の途中の部品が描かれたか
        core::ObjectHandle m_editOwner;
        const core::Property* m_editPath[MaxPathDepth] = {};
        std::size_t m_editDepth = 0;
        engine::ArchiveValue m_before; // 編集を始める前の値
    };
}

#endif
