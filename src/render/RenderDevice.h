// GPU を使って画面に描くための窓口。
// このヘッダには SDL_GPU の型を一切書かない(CLAUDE.md: SDL_GPU の型は描画層の実装の外に出さない)。
#pragma once

#include "core/Handle.h"

#include "core/Matrix4.h" // 行列は自前の型で受け取る(glm の型はこのヘッダに出さない)

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace husky::platform
{
    class Window; // 前方宣言: 「Window というクラスがある」とだけ伝える。参照で受け取るだけなら中身は要らない
    struct Image;
}

namespace husky::render
{
    // GPU に送ったテクスチャ1枚。中身(SDL_GPU のテクスチャ)は RenderDevice.cpp の中でだけ定義する。
    // ここでは「Texture という型がある」とだけ伝え(前方宣言)、外からは TextureHandle を通してしか指せないようにする。
    struct Texture;

    // テクスチャを指す世代付きハンドル。RenderDevice::CreateTexture で受け取る。
    // 手放した(ReleaseTexture)後の古いハンドルは、世代が合わないので「もう無い」と分かる。
    // 何も指さない(既定の)ハンドルは「テクスチャ無し」= 白い板として描く。
    using TextureHandle = core::Handle<Texture>;

    // 色。各要素は 0.0〜1.0。
    struct Color
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f; // 不透明度。1.0 で不透明
    };

    // 描きたいスプライト1枚分の情報。座標はワールドの単位で、右が +x、上が +y。
    // 画面のどこに出るかは、カメラの行列(RenderDevice::SetViewProjection)で決まる。
    //
    // 形の決め方(RS-1): まず「ピボットを原点にした、幅 width・高さ height の四角形」(ローカルの座標)を考え、
    // その四隅にモデル行列(model。移動・回転・拡大縮小)を掛けて、ワールドの位置にする。
    //   ピボット (0.5, 0.5) なら四角形の真ん中が原点になり、回転と拡大縮小は真ん中を中心にかかる。
    //   ピボット (0, 0) なら左下の角が原点になる。
    struct Sprite
    {
        core::Matrix4 model; // ローカル → ワールドの行列(既定は何もしない「単位行列」)
        float width = 0.0f;  // ローカルでの大きさ(拡大縮小の前)
        float height = 0.0f;
        float pivotX = 0.5f; // ピボット: 四角形の中のどこを原点にするか(割合。0 が左・下、1 が右・上)
        float pivotY = 0.5f;
        Color color = {1.0f, 1.0f, 1.0f, 1.0f}; // テクスチャの色に掛ける色。白ならテクスチャの色そのまま
        // 貼るテクスチャ。何も指さないか、もう手放されたテクスチャなら、白い板(color の色そのまま)として描く。
        TextureHandle texture;
        // テクスチャのどの範囲を貼るか(UV。テクスチャの左上が (0, 0)、右下が (1, 1))。既定は全体。
        // 1 枚の画像に並べた絵(スプライトシート)から、1 つを切り出して貼るのに使う。
        float u0 = 0.0f; // 左
        float v0 = 0.0f; // 上
        float u1 = 1.0f; // 右
        float v1 = 1.0f; // 下
    };

    // スプライトの四隅のワールドでの位置(x, y)。順番は 0: 左上、1: 右上、2: 右下、3: 左下(ローカルでの向き。回すと向きも回る)。
    struct SpriteCorners
    {
        float x[4];
        float y[4];
    };

    // スプライトの四隅を計算する(ローカルの四角形にモデル行列を掛ける)。GPU を使わないので、テストできる。
    // 動作スレッド: どこでもよい
    SpriteCorners ComputeSpriteCorners(const Sprite& sprite);

    // 直前のフレームの描画の集計。性能の確認に使う。
    struct FrameStats
    {
        int spriteCount = 0;        // 描いたスプライトの数
        int drawCallCount = 0;      // GPU に出した描画命令の回数(テクスチャが変わるたびに 1 回増える)
        int droppedSpriteCount = 0; // 上限を超えて描けなかったスプライトの数
    };

    // GPU デバイスと、ウィンドウへの描画をまとめて持つ。
    // 使う Window より先に破棄すること(同じブロックで Window の後に宣言すれば、自動でそうなる)。
    //
    // 1フレームの使い方:
    //   if (BeginFrame(背景色)) { DrawSprite(...) を何回でも; EndFrame(); }
    // DrawSprite は描きたいものを溜めるだけ。GPU への命令は、EndFrame の中でまとめて出す。
    // 動作スレッド: メインスレッド
    class RenderDevice final
    {
    public:
        // 1フレームに描けるスプライトの上限。
        static constexpr int MaxSpritesPerFrame = 8192;
        // 1フレームに使える行列(SetViewProjection)の数の上限(UI-2a。シーン、UI、エディタの重ね描きなどで数個の見込み)。
        static constexpr int MaxViewProjectionsPerFrame = 16;

        RenderDevice();
        ~RenderDevice();

        RenderDevice(const RenderDevice&) = delete;
        RenderDevice& operator=(const RenderDevice&) = delete;

        // GPU を初期化し、window に描けるようにする。戻り値: 成功したら true。
        bool Initialize(platform::Window& window);

        // 画像を GPU に送り、テクスチャを作る。Initialize の後なら、いつ呼んでもよい(フレームの途中でもよい)。
        // 戻り値: 作ったテクスチャのハンドル。失敗したら何も指さないハンドル(IsNull() が true)。
        TextureHandle CreateTexture(const platform::Image& image);

        // テクスチャを手放す。ハンドルはすぐに無効になる(この後の DrawSprite では白い板になる)が、
        // GPU のメモリは、GPU が使い終わるまで数フレーム待ってから解放する(DeferredReleaseQueue.h)。
        // すでに手放したハンドルや、何も指さないハンドルを渡しても何もしない。
        void ReleaseTexture(TextureHandle texture);

        // ハンドルが、まだ手放されていないテクスチャを指しているか。
        bool IsTextureAlive(TextureHandle texture) const;

        // 今あるテクスチャの数(中で使う白いテクスチャは数えない)と、解放を待っている数。確認とテスト用。
        std::size_t GetTextureCount() const;
        std::size_t GetPendingReleaseCount() const;

        // 1フレームの描画を始める。clearColor は、EndFrame で最初に画面を塗りつぶす色。
        // vsync のため、ここでモニターの書き換えのタイミングまで待つことがある。
        // 戻り値: false ならこのフレームは描けない(DrawSprite も EndFrame も呼ばない)。
        bool BeginFrame(const Color& clearColor);

        // 今回のフレームの画面の大きさ(ピクセル)。BeginFrame が成功した後に正しい値になる。
        // カメラの射影行列を、画面の大きさに合わせて作るのに使う。
        int GetScreenWidth() const;
        int GetScreenHeight() const;

        // カメラの行列(ビュー行列 × 射影行列)を渡す。スプライトの四隅(ワールドの位置)に、GPU がこの行列を掛けて
        // クリップ座標(画面の左下が (-1, -1)、右上が (+1, +1))にする。
        // BeginFrame と EndFrame の間で呼ぶ。このフレームで呼ばなければ、前のフレームの最後の行列のまま描く
        // (一度も呼ばなければ単位行列。ワールドの -1〜+1 が画面いっぱいになる)。
        // フレームの途中で呼んでよい(UI-2a): 呼んだ後に積んだスプライトから、新しい行列で描く。前に積んだものは前の行列のまま。
        // 例: シーンをワールドのカメラで積む → 画面ちょうどの行列に替えて UI を積む(UI はカメラが動いても動かない)。
        // 1フレームに MaxViewProjectionsPerFrame 個まで(まだ何も積んでいない行列は、上書きするだけで数に入らない)。
        void SetViewProjection(const core::Matrix4& viewProjection);

        // スプライトを1枚、描く予定に加える。後から加えたものほど手前に描かれる。
        // BeginFrame と EndFrame の間で呼ぶ。
        void DrawSprite(const Sprite& sprite);

#if HUSKY_DEVELOPMENT
        // 【開発ビルドだけ】開発用の UI(ImGui)のフォント(TTF のファイルの中身)を渡す。Initialize の前に呼ぶ。
        // 渡さなければ ImGui の既定のフォント(英数字だけ。日本語は ? になる)。日本語の字は、使うときに ImGui が作る。
        void SetDevUiFont(std::string fontFileData, float sizePixels);

        // 【開発ビルドだけ】開発用の UI(ImGui)の1フレームを始める。BeginFrame が成功した後、EndFrame の前に呼ぶ。
        // これを呼んでから EndFrame までの間に、ImGui の関数(ImGui::Begin など)で窓を作れる。
        // 作った UI は、EndFrame でスプライトより手前に描かれる。呼ばなければ、UI は描かれない。
        void BeginDevUiFrame();
#endif

        // 溜めたスプライトをまとめて GPU に送り、画面に出す。
        // 描画命令は、同じテクスチャが続く区間ごとに1回(SpriteBatch.h)。全部同じテクスチャなら1回で済む。
        void EndFrame();

        // 直前の EndFrame の集計を返す。
        FrameStats GetLastFrameStats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
