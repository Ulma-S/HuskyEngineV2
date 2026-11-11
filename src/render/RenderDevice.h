// GPU を使って画面に描くための窓口。
// このヘッダには SDL_GPU の型を一切書かない(CLAUDE.md: SDL_GPU の型は描画層の実装の外に出さない)。
#pragma once

#include <memory>

namespace husky::platform
{
    class Window; // 前方宣言: 「Window というクラスがある」とだけ伝える。参照で受け取るだけなら中身は要らない
    struct Image;
}

namespace husky::render
{
    // 色。各要素は 0.0〜1.0。
    struct Color
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f; // 不透明度。1.0 で不透明
    };

    // 描きたいスプライト1枚分の情報。座標はピクセル単位で、画面の左上が (0, 0)、右が +x、下が +y。
    struct Sprite
    {
        float x = 0.0f;      // 左上の位置
        float y = 0.0f;
        float width = 0.0f;  // 画面上の大きさ
        float height = 0.0f;
        Color color = {1.0f, 1.0f, 1.0f, 1.0f}; // テクスチャの色に掛ける色。白ならテクスチャの色そのまま
    };

    // 直前のフレームの描画の集計。性能の確認に使う。
    struct FrameStats
    {
        int spriteCount = 0;        // 描いたスプライトの数
        int drawCallCount = 0;      // GPU に出した描画命令の回数
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

        RenderDevice();
        ~RenderDevice();

        RenderDevice(const RenderDevice&) = delete;
        RenderDevice& operator=(const RenderDevice&) = delete;

        // GPU を初期化し、window に描けるようにする。戻り値: 成功したら true。
        bool Initialize(platform::Window& window);

        // 動作確認用のテクスチャを GPU に送る。DrawSprite はすべてこのテクスチャを貼って描く。
        // 1回だけ呼べる。テクスチャを複数持てる仕組み(ハンドル)ができたら消す、仮のもの。
        // 戻り値: 成功したら true。
        bool LoadTestTexture(const platform::Image& image);

        // 1フレームの描画を始める。clearColor は、EndFrame で最初に画面を塗りつぶす色。
        // vsync のため、ここでモニターの書き換えのタイミングまで待つことがある。
        // 戻り値: false ならこのフレームは描けない(DrawSprite も EndFrame も呼ばない)。
        bool BeginFrame(const Color& clearColor);

        // スプライトを1枚、描く予定に加える。後から加えたものほど手前に描かれる。
        // BeginFrame と EndFrame の間で呼ぶ。
        void DrawSprite(const Sprite& sprite);

        // 溜めたスプライトをまとめて GPU に送り、1回の描画命令で描いて、画面に出す。
        void EndFrame();

        // 直前の EndFrame の集計を返す。
        FrameStats GetLastFrameStats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
