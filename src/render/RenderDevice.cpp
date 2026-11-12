// RenderDevice.h の SDL_GPU による実装。ここでは SDL を直接呼んでよい。
#include "render/RenderDevice.h"

#include "core/HandleTable.h"
#include "platform/Image.h"
#include "render/DeferredReleaseQueue.h"
#include "render/SpriteBatch.h"
#include "platform/Window.h"

#include <SDL3/SDL.h>

#if HUSKY_DEVELOPMENT
// 開発用の UI(Dear ImGui)。本体と、SDL3 につなぐ公式の部品(入力: imgui_impl_sdl3、描画: imgui_impl_sdlgpu3)。
#include "core/MemoryTracker.h"
#include "platform/Platform.h" // SetNativeEventHook
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#endif

#include <cstddef> // std::size_t, offsetof
#include <cstdint>
#include <cstring> // std::memcpy
#include <vector>

// ビルドの途中で DXC が生成したヘッダ。中身は const unsigned char g_〜[] = { 0x44, 0x58, ... }; という配列。
#include "shaders/Sprite.frag.dxil.h"
#include "shaders/Sprite.frag.spv.h"
#include "shaders/Sprite.vert.dxil.h"
#include "shaders/Sprite.vert.spv.h"

namespace husky::render
{
    // RenderDevice.h で前方宣言した Texture の中身。SDL_GPU の型を持つので、このファイルの中でだけ定義する。
    // 外からは TextureHandle でしか指せないので、SDL_GPU の型が描画層の外に出ることはない。
    struct Texture
    {
        SDL_GPUTexture* gpuTexture = nullptr; // 持ち主はこの Texture(手放すときに解放の待ち行列へ渡す)
        Uint32 width = 0;
        Uint32 height = 0;
    };

    namespace
    {
        // 頂点1つ分のデータ。Sprite.vert.hlsl の VertexInput と、並び・型を一致させる。
        struct Vertex
        {
            float x, y;       // 位置(ピクセル)          → TEXCOORD0 / location 0
            float u, v;       // テクスチャ座標(0〜1)     → TEXCOORD1 / location 1
            float r, g, b, a; // テクスチャに掛ける色     → TEXCOORD2 / location 2
        };

        // 頂点シェーダの cbuffer FrameUniforms と一致させる(4×4 の行列 = float 16 個 = 64 バイト)。
        // glm::mat4 は、メモリに「列」を順に並べる(列優先、column-major)。HLSL の cbuffer の行列も既定で列優先なので、
        // 並べ替えずにそのまま送ればよい。
        struct FrameUniforms
        {
            glm::mat4 viewProjection;
        };
        static_assert(sizeof(FrameUniforms) == 64, "FrameUniforms は HLSL の float4x4 と同じ 64 バイトにする");

        // スプライト1枚 = 頂点4つ、インデックス6つ(三角形2つ)。
        constexpr Uint32 VerticesPerSprite = 4;
        constexpr Uint32 IndicesPerSprite = 6;
        constexpr Uint32 MaxSprites = static_cast<Uint32>(RenderDevice::MaxSpritesPerFrame);
        constexpr Uint32 VertexBufferBytes = MaxSprites * VerticesPerSprite * sizeof(Vertex);
        constexpr Uint32 IndexBufferBytes = MaxSprites * IndicesPerSprite * sizeof(std::uint16_t);
        // インデックスは 16 ビット(0〜65535)なので、頂点の数がそれを超えないこと。
        // static_assert はコンパイル時の確認。条件が偽ならビルドが失敗する。
        static_assert(MaxSprites * VerticesPerSprite <= 65536, "16ビットのインデックスで表せる頂点数を超えています");

        // 変換済みのシェーダ1つ分のバイト列(DXIL 版と SPIR-V 版の両方)。
        struct ShaderBytecode
        {
            const unsigned char* dxil;
            std::size_t dxilSize;
            const unsigned char* spirv;
            std::size_t spirvSize;
        };

        // GPU が使える形式を選んで、シェーダを作る。失敗したら nullptr。
        // numUniformBuffers / numSamplers: このシェーダが使う cbuffer / テクスチャとサンプラーの組の数。
        //   シェーダの中身と一致させる必要がある。
        SDL_GPUShader* CreateShader(SDL_GPUDevice* device, SDL_GPUShaderStage stage,
                                    const ShaderBytecode& bytecode, Uint32 numUniformBuffers, Uint32 numSamplers)
        {
            SDL_GPUShaderCreateInfo info{};
            info.entrypoint = "main"; // HLSL で最初に呼ばれる関数の名前
            info.stage = stage;
            info.num_uniform_buffers = numUniformBuffers;
            info.num_samplers = numSamplers;

            // 実行時に選ばれた API(D3D12 か Vulkan)が受け付ける形式を選ぶ。
            const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
            if (formats & SDL_GPU_SHADERFORMAT_DXIL)
            {
                info.format = SDL_GPU_SHADERFORMAT_DXIL;
                info.code = bytecode.dxil;
                info.code_size = bytecode.dxilSize;
            }
            else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
            {
                info.format = SDL_GPU_SHADERFORMAT_SPIRV;
                info.code = bytecode.spirv;
                info.code_size = bytecode.spirvSize;
            }
            else
            {
                SDL_Log("CreateShader: 使えるシェーダの形式がありません");
                return nullptr;
            }

            SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
            if (shader == nullptr)
            {
                SDL_Log("SDL_CreateGPUShader に失敗しました: %s", SDL_GetError());
            }
            return shader;
        }
    }

    struct RenderDevice::Impl
    {
        SDL_GPUDevice* device = nullptr; // GPU そのもの
        SDL_Window* window = nullptr;    // 描き込む先のウィンドウ(持ち主は platform::Window。ここでは借りているだけ)

        // スプライトを描くためのもの。起動時に作り、終了まで使い続ける。
        SDL_GPUGraphicsPipeline* spritePipeline = nullptr;    // シェーダと描き方の設定をまとめたもの
        SDL_GPUBuffer* spriteVertexBuffer = nullptr;          // 全スプライトの頂点(毎フレーム書き換える)
        SDL_GPUBuffer* spriteIndexBuffer = nullptr;           // 全スプライトのインデックス(起動時に1回だけ書く)
        SDL_GPUTransferBuffer* spriteTransferBuffer = nullptr; // 頂点を GPU に送るための受け渡し場所
        SDL_GPUSampler* pointSampler = nullptr;               // テクスチャの読み方(最近傍、ぼかさない)

        // テクスチャ。ハンドルの表(HandleTable)に入れ、外には TextureHandle だけを渡す。
        core::HandleTable<Texture> textures;
        TextureHandle whiteTexture; // 1x1 の白。テクスチャ無しのスプライトに貼る(起動時に作り、表にも入れる)
        // 手放したテクスチャの、解放の待ち行列。frameNumber は BeginFrame のたびに 1 増える、フレームの番号。
        DeferredReleaseQueue<SDL_GPUTexture*> pendingReleases;
        std::uint64_t frameNumber = 0;

        // 1フレームの間だけ使うもの。BeginFrame で用意し、EndFrame で手放す。
        SDL_GPUCommandBuffer* commandBuffer = nullptr; // GPU への命令を溜める入れ物
        SDL_GPUTexture* swapchainTexture = nullptr;    // 今回描き込む画面(持ち主は SDL。借りているだけ)
        Uint32 screenWidth = 0;                        // 今回の画面の大きさ(ピクセル)
        Uint32 screenHeight = 0;
        glm::mat4 viewProjection = glm::mat4(1.0f);    // カメラの行列(SetViewProjection)。最初は単位行列
        Color clearColor;                             // 画面を最初に塗りつぶす色
        std::vector<Vertex> vertices;                  // DrawSprite で溜めた頂点(CPU 側)
        std::vector<SpriteBatch> batches;              // 同じテクスチャが続く区間(1区間 = 描画命令1回)
        int droppedSpriteCount = 0;                    // 上限を超えて描けなかった数

        FrameStats lastFrameStats; // 直前のフレームの集計

#if HUSKY_DEVELOPMENT
        bool devUiReady = false;      // ImGui の準備ができたか
        bool devUiFrameBegun = false; // このフレームで BeginDevUiFrame が呼ばれたか
        bool InitializeDevUi();
        void ShutdownDevUi();
#endif

        bool CreateSpritePipeline();
        bool CreateSpriteBuffers();
        bool CreateSampler();
        SDL_GPUTexture* CreateGpuTexture(const platform::Image& image);
        void UploadVertices(Uint32 bytes);
        void ResetFrame();

        ~Impl()
        {
            if (device != nullptr)
            {
                // GPU は CPU より遅れて動いているので、まだリソースを使っている途中かもしれない。
                // 全部終わるまで待ってから解放する。
                SDL_WaitForGPUIdle(device);
#if HUSKY_DEVELOPMENT
                ShutdownDevUi(); // ImGui の GPU のリソースも、デバイスより先に片付ける
#endif

                if (pointSampler != nullptr)
                {
                    SDL_ReleaseGPUSampler(device, pointSampler);
                }
                // GPU は止まっているので、待たずにすべて解放してよい。
                // 解放を待っているテクスチャと、まだ手放されていないテクスチャ(白いテクスチャも含む)。
                pendingReleases.ReleaseAll([this](SDL_GPUTexture* texture) { SDL_ReleaseGPUTexture(device, texture); });
                textures.ForEach([this](TextureHandle, Texture& texture) {
                    SDL_ReleaseGPUTexture(device, texture.gpuTexture);
                });
                if (spriteTransferBuffer != nullptr)
                {
                    SDL_ReleaseGPUTransferBuffer(device, spriteTransferBuffer);
                }
                if (spriteIndexBuffer != nullptr)
                {
                    SDL_ReleaseGPUBuffer(device, spriteIndexBuffer);
                }
                if (spriteVertexBuffer != nullptr)
                {
                    SDL_ReleaseGPUBuffer(device, spriteVertexBuffer);
                }
                if (spritePipeline != nullptr)
                {
                    SDL_ReleaseGPUGraphicsPipeline(device, spritePipeline);
                }
                if (window != nullptr)
                {
                    // ウィンドウを GPU から切り離す。ウィンドウ自体は壊さない(持ち主ではないので)。
                    SDL_ReleaseWindowFromGPUDevice(device, window);
                }
                SDL_DestroyGPUDevice(device);
            }
        }
    };

    bool RenderDevice::Impl::CreateSpritePipeline()
    {
        // sizeof(配列) は配列全体のバイト数。
        const ShaderBytecode vertexBytecode = {
            g_Sprite_vert_dxil, sizeof(g_Sprite_vert_dxil),
            g_Sprite_vert_spirv, sizeof(g_Sprite_vert_spirv),
        };
        const ShaderBytecode fragmentBytecode = {
            g_Sprite_frag_dxil, sizeof(g_Sprite_frag_dxil),
            g_Sprite_frag_spirv, sizeof(g_Sprite_frag_spirv),
        };

        // 頂点シェーダは cbuffer を1つ使う。ピクセルシェーダはテクスチャとサンプラーの組を1つ使う。
        SDL_GPUShader* vertexShader = CreateShader(device, SDL_GPU_SHADERSTAGE_VERTEX, vertexBytecode, 1, 0);
        SDL_GPUShader* fragmentShader = CreateShader(device, SDL_GPU_SHADERSTAGE_FRAGMENT, fragmentBytecode, 0, 1);

        if (vertexShader != nullptr && fragmentShader != nullptr)
        {
            // 頂点バッファの読み方。「1頂点が何バイトで、その中のどこに何があるか」を GPU に教える。
            SDL_GPUVertexBufferDescription bufferDesc{};
            bufferDesc.slot = 0;                                    // 0番目の頂点バッファ
            bufferDesc.pitch = sizeof(Vertex);                      // 1頂点のバイト数(次の頂点までの距離)
            bufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX; // 頂点ごとに次へ進む

            SDL_GPUVertexAttribute attributes[3]{};
            attributes[0].location = 0; // シェーダの TEXCOORD0: 位置
            attributes[0].buffer_slot = 0;
            attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attributes[0].offset = offsetof(Vertex, x); // offsetof: 構造体の先頭から、そのメンバーまでのバイト数
            attributes[1].location = 1; // シェーダの TEXCOORD1: UV
            attributes[1].buffer_slot = 0;
            attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attributes[1].offset = offsetof(Vertex, u);
            attributes[2].location = 2; // シェーダの TEXCOORD2: 色
            attributes[2].buffer_slot = 0;
            attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
            attributes[2].offset = offsetof(Vertex, r);

            // 描き込み先(画面)の形式。パイプラインは、描き込み先の形式に合わせて作る必要がある。
            SDL_GPUColorTargetDescription colorTargetDesc{};
            colorTargetDesc.format = SDL_GetGPUSwapchainTextureFormat(device, window);

            // アルファブレンド: 描く色と、すでに画面にある色を、描く色の不透明度(アルファ)で混ぜる。
            //   結果 = 描く色 × アルファ + 画面の色 × (1 - アルファ)
            // アルファが 0 の所(透明)は画面の色がそのまま残り、背景が透けて見える。
            colorTargetDesc.blend_state.enable_blend = true;
            colorTargetDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            colorTargetDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            colorTargetDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colorTargetDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            colorTargetDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            colorTargetDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vertexShader;
            info.fragment_shader = fragmentShader;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; // 3つずつで三角形1つ
            info.vertex_input_state.vertex_buffer_descriptions = &bufferDesc;
            info.vertex_input_state.num_vertex_buffers = 1;
            info.vertex_input_state.vertex_attributes = attributes;
            info.vertex_input_state.num_vertex_attributes = 3;
            info.target_info.color_target_descriptions = &colorTargetDesc;
            info.target_info.num_color_targets = 1;

            spritePipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (spritePipeline == nullptr)
            {
                SDL_Log("SDL_CreateGPUGraphicsPipeline に失敗しました: %s", SDL_GetError());
            }
        }

        // シェーダはパイプラインの中に取り込まれるので、作り終えたら手放してよい。
        if (vertexShader != nullptr)
        {
            SDL_ReleaseGPUShader(device, vertexShader);
        }
        if (fragmentShader != nullptr)
        {
            SDL_ReleaseGPUShader(device, fragmentShader);
        }
        return spritePipeline != nullptr;
    }

    bool RenderDevice::Impl::CreateSpriteBuffers()
    {
        // 1. GPU のメモリに、上限いっぱいのスプライトが入る大きさのバッファを作る。
        SDL_GPUBufferCreateInfo vertexInfo{};
        vertexInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vertexInfo.size = VertexBufferBytes;
        spriteVertexBuffer = SDL_CreateGPUBuffer(device, &vertexInfo);

        SDL_GPUBufferCreateInfo indexInfo{};
        indexInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        indexInfo.size = IndexBufferBytes;
        spriteIndexBuffer = SDL_CreateGPUBuffer(device, &indexInfo);

        // 頂点を毎フレーム送るための転送用バッファ。作りっぱなしにして使い回す。
        SDL_GPUTransferBufferCreateInfo vertexTransferInfo{};
        vertexTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        vertexTransferInfo.size = VertexBufferBytes;
        spriteTransferBuffer = SDL_CreateGPUTransferBuffer(device, &vertexTransferInfo);

        if (spriteVertexBuffer == nullptr || spriteIndexBuffer == nullptr || spriteTransferBuffer == nullptr)
        {
            SDL_Log("スプライト用バッファの作成に失敗しました: %s", SDL_GetError());
            return false;
        }

        // 2. インデックスを作る。どのスプライトも「頂点 0,1,2 と 0,2,3 で三角形2つ」という同じ形なので、
        //    中身は起動時に決まり、毎フレーム送る必要がない。
        //      i 枚目のスプライトの頂点は 4i, 4i+1, 4i+2, 4i+3 番目。
        std::vector<std::uint16_t> indices(MaxSprites * IndicesPerSprite);
        for (Uint32 i = 0; i < MaxSprites; ++i)
        {
            const auto base = static_cast<std::uint16_t>(i * VerticesPerSprite);
            std::uint16_t* out = &indices[i * IndicesPerSprite];
            out[0] = base + 0; out[1] = base + 1; out[2] = base + 2; // 右上の三角形
            out[3] = base + 0; out[4] = base + 2; out[5] = base + 3; // 左下の三角形
        }

        // 3. インデックスを GPU に送る(転送用バッファ → コピーパス)。1回きりなので、転送用バッファも使い捨て。
        SDL_GPUTransferBufferCreateInfo indexTransferInfo{};
        indexTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        indexTransferInfo.size = IndexBufferBytes;
        SDL_GPUTransferBuffer* indexTransfer = SDL_CreateGPUTransferBuffer(device, &indexTransferInfo);
        if (indexTransfer == nullptr)
        {
            SDL_Log("SDL_CreateGPUTransferBuffer に失敗しました: %s", SDL_GetError());
            return false;
        }
        void* mapped = SDL_MapGPUTransferBuffer(device, indexTransfer, false);
        if (mapped == nullptr)
        {
            SDL_Log("SDL_MapGPUTransferBuffer に失敗しました: %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(device, indexTransfer);
            return false;
        }
        std::memcpy(mapped, indices.data(), IndexBufferBytes);
        SDL_UnmapGPUTransferBuffer(device, indexTransfer);

        SDL_GPUCommandBuffer* uploadCommands = SDL_AcquireGPUCommandBuffer(device);
        if (uploadCommands == nullptr)
        {
            SDL_Log("SDL_AcquireGPUCommandBuffer に失敗しました: %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(device, indexTransfer);
            return false;
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCommands);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = indexTransfer;
        SDL_GPUBufferRegion destination{};
        destination.buffer = spriteIndexBuffer;
        destination.size = IndexBufferBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(uploadCommands);

        // SDL の Release は「GPU が使い終わってから解放する」ので、コピーの途中でも呼んでよい。
        SDL_ReleaseGPUTransferBuffer(device, indexTransfer);
        return true;
    }

    bool RenderDevice::Impl::CreateSampler()
    {
        SDL_GPUSamplerCreateInfo info{};
        // 最近傍(NEAREST): 一番近いピクセルの色をそのまま使う。拡大してもぼけず、ドット絵がくっきり見える。
        // LINEAR にすると、周りのピクセルと混ぜてなめらかにする(写真のような絵に向く)。
        info.min_filter = SDL_GPU_FILTER_NEAREST; // 縮小するとき
        info.mag_filter = SDL_GPU_FILTER_NEAREST; // 拡大するとき
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        // UV が 0〜1 の外に出たときは、端の色を使う(繰り返さない)。
        info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

        pointSampler = SDL_CreateGPUSampler(device, &info);
        if (pointSampler == nullptr)
        {
            SDL_Log("SDL_CreateGPUSampler に失敗しました: %s", SDL_GetError());
            return false;
        }
        return true;
    }

    void RenderDevice::Impl::UploadVertices(Uint32 bytes)
    {
        // cycle = true: この転送用バッファを GPU がまだ使っている(前のフレームの転送が終わっていない)なら、
        // SDL が裏で別のバッファに切り替えてから書かせてくれる。待たずに済み、前のフレームの中身も壊さない。
        void* mapped = SDL_MapGPUTransferBuffer(device, spriteTransferBuffer, true);
        if (mapped == nullptr)
        {
            SDL_Log("SDL_MapGPUTransferBuffer に失敗しました: %s", SDL_GetError());
            return;
        }
        std::memcpy(mapped, vertices.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, spriteTransferBuffer);

        // コピーパスは描画パスの外でしか開けないので、描画パスを始める前に行う。
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = spriteTransferBuffer;
        SDL_GPUBufferRegion destination{};
        destination.buffer = spriteVertexBuffer;
        destination.size = bytes;
        // こちらも cycle = true。前のフレームの描画が、まだこの頂点バッファを読んでいるかもしれないため。
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
        SDL_EndGPUCopyPass(copyPass);
    }

#if HUSKY_DEVELOPMENT
    namespace
    {
        // ImGui のメモリの確保を、エンジンの入口(MemoryTracker)に通す(user_data は使わない)。
        void* DevUiAllocate(std::size_t size, void* /*userData*/)
        {
            return core::TrackedAllocate(size, 16);
        }
        void DevUiFree(void* pointer, void* /*userData*/)
        {
            core::TrackedFree(pointer);
        }

        // platform から届く OS のイベントを ImGui に渡す(platform::SetNativeEventHook に登録する)。
        // 戻り値: ImGui がキーボードを使っているなら true(キーを押した入力はゲームに渡さない)。
        bool ForwardEventToDevUi(const void* nativeEvent)
        {
            const SDL_Event* event = static_cast<const SDL_Event*>(nativeEvent);
            ImGui_ImplSDL3_ProcessEvent(event);
            return ImGui::GetIO().WantCaptureKeyboard;
        }
    }

    bool RenderDevice::Impl::InitializeDevUi()
    {
        // 1. ImGui 本体。メモリの確保の関数は、コンテキスト(ImGui の状態の入れ物)を作る前に設定する。
        IMGUI_CHECKVERSION(); // ヘッダとライブラリの版が合っているかを確かめる
        ImGui::SetAllocatorFunctions(DevUiAllocate, DevUiFree);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // 窓の位置を imgui.ini に保存しない(作業フォルダにファイルを作らないため)
        ImGui::StyleColorsDark();

        // 2. 入力(SDL3)と描画(SDL_GPU)につなぐ。
        if (!ImGui_ImplSDL3_InitForSDLGPU(window))
        {
            SDL_Log("ImGui_ImplSDL3_InitForSDLGPU に失敗しました");
            ImGui::DestroyContext();
            return false;
        }
        ImGui_ImplSDLGPU3_InitInfo info;
        info.Device = device;
        info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(device, window); // 画面と同じ形式で描く
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&info))
        {
            SDL_Log("ImGui_ImplSDLGPU3_Init に失敗しました");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        // 3. OS のイベントを ImGui にも渡してもらう。
        platform::SetNativeEventHook(ForwardEventToDevUi);
        devUiReady = true;
        return true;
    }

    void RenderDevice::Impl::ShutdownDevUi()
    {
        if (!devUiReady)
        {
            return;
        }
        // 作った順の逆に片付ける。
        platform::SetNativeEventHook(nullptr);
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        devUiReady = false;
    }
#endif

    void RenderDevice::Impl::ResetFrame()
    {
        commandBuffer = nullptr;
        swapchainTexture = nullptr;
        vertices.clear(); // 中身だけ消す。確保したメモリは残るので、次のフレームで再確保しない
        batches.clear();
        droppedSpriteCount = 0;
    }

    RenderDevice::RenderDevice()
        : m_impl(std::make_unique<Impl>())
    {
        // 上限いっぱいの頂点が入る大きさを、最初に確保しておく(毎フレームのメモリ確保を避ける)。
        m_impl->vertices.reserve(MaxSprites * VerticesPerSprite);
        // 区間の数は、最も多くてもスプライトの数(1枚ごとにテクスチャが変わる場合)。同じく最初に確保しておく。
        m_impl->batches.reserve(MaxSprites);
    }

    // = default は「コンパイラが作る標準の処理でよい」という意味。m_impl の破棄から ~Impl が呼ばれる。
    RenderDevice::~RenderDevice() = default;

    bool RenderDevice::Initialize(platform::Window& window)
    {
        if (m_impl->device != nullptr)
        {
            SDL_Log("RenderDevice::Initialize: すでに初期化されています");
            return false;
        }

        // void* で受け取ったものを、元の SDL_Window* に戻す。
        SDL_Window* sdlWindow = static_cast<SDL_Window*>(window.GetNativeHandle());
        if (sdlWindow == nullptr)
        {
            SDL_Log("RenderDevice::Initialize: ウィンドウが開いていません");
            return false;
        }

        // デバッグビルドでは GPU の検証機能(使い方の誤りを教えてくれる)を有効にする。
        // NDEBUG は Release ビルドのときだけ定義されるマクロ。
#ifdef NDEBUG
        const bool debugMode = false;
#else
        const bool debugMode = true;
#endif

        // 第1引数は「こちらが用意できるシェーダの形式」。SDL はこの中から、使える GPU の API を選ぶ。
        // 用意しているのは DXIL(D3D12 用)と SPIR-V(Vulkan 用)。Mac 対応のときに MSL を足す。
        // 第3引数 nullptr は「API は SDL に任せる」(Windows では D3D12 か Vulkan)。
        m_impl->device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL,
            debugMode,
            nullptr);
        if (m_impl->device == nullptr)
        {
            SDL_Log("SDL_CreateGPUDevice に失敗しました: %s", SDL_GetError());
            return false;
        }
        SDL_Log("GPU の API: %s", SDL_GetGPUDeviceDriver(m_impl->device));

        // ウィンドウを GPU に結び付ける。これで、ウィンドウの画面(スワップチェーン)に描けるようになる。
        // 表示の方式は既定の VSYNC(モニターの書き換えに合わせて待つ)になる。
        if (!SDL_ClaimWindowForGPUDevice(m_impl->device, sdlWindow))
        {
            SDL_Log("SDL_ClaimWindowForGPUDevice に失敗しました: %s", SDL_GetError());
            return false;
        }
        m_impl->window = sdlWindow;

        if (!(m_impl->CreateSpritePipeline() && m_impl->CreateSpriteBuffers() && m_impl->CreateSampler()))
        {
            return false;
        }

        // テクスチャ無しのスプライトに貼る、1x1 の白いテクスチャ。シェーダは「テクスチャの色 × スプライトの色」なので、
        // 白を貼ると、スプライトの色そのままの板になる(シェーダを分けずに済む)。
        platform::Image white;
        white.width = 1;
        white.height = 1;
        white.pixels = {255, 255, 255, 255};
        m_impl->whiteTexture = CreateTexture(white);
        if (m_impl->whiteTexture.IsNull())
        {
            return false;
        }
#if HUSKY_DEVELOPMENT
        // 開発用の UI は、用意できなくてもゲームは動かせるので、失敗してもログを出すだけにする。
        if (!m_impl->InitializeDevUi())
        {
            SDL_Log("開発用の UI(ImGui)を用意できませんでした。UI なしで続けます");
        }
#endif
        return true;
    }

    SDL_GPUTexture* RenderDevice::Impl::CreateGpuTexture(const platform::Image& image)
    {
        const Uint32 width = static_cast<Uint32>(image.width);
        const Uint32 height = static_cast<Uint32>(image.height);
        const Uint32 bytes = width * height * 4;
        if (image.width <= 0 || image.height <= 0 || image.pixels.size() != bytes)
        {
            SDL_Log("CreateTexture: 画像の大きさが不正です");
            return nullptr;
        }

        // 1. GPU のメモリにテクスチャを作る(中身は空)。
        SDL_GPUTextureCreateInfo textureInfo{};
        textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
        textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; // R, G, B, A を1バイトずつ。Image と同じ並び
        textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;          // シェーダから読むために使う
        textureInfo.width = width;
        textureInfo.height = height;
        textureInfo.layer_count_or_depth = 1;
        textureInfo.num_levels = 1; // ミップマップ(縮小版)は作らない
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
        if (texture == nullptr)
        {
            SDL_Log("SDL_CreateGPUTexture に失敗しました: %s", SDL_GetError());
            return nullptr;
        }

        // 2〜4. 頂点バッファと同じく、転送用バッファ → コピーパス の順で GPU に送る。
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = bytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
        {
            SDL_Log("SDL_CreateGPUTransferBuffer に失敗しました: %s", SDL_GetError());
            SDL_ReleaseGPUTexture(device, texture);
            return nullptr;
        }

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_Log("SDL_MapGPUTransferBuffer に失敗しました: %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            SDL_ReleaseGPUTexture(device, texture);
            return nullptr;
        }
        std::memcpy(mapped, image.pixels.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        // フレームの途中で呼ばれても、そのフレームの命令とは混ぜず、別の入れ物(コマンドバッファ)で送る。
        // GPU は提出された順に実行するので、このフレームの描画より先にテクスチャの中身が届く。
        SDL_GPUCommandBuffer* uploadCommands = SDL_AcquireGPUCommandBuffer(device);
        if (uploadCommands == nullptr)
        {
            SDL_Log("SDL_AcquireGPUCommandBuffer に失敗しました: %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            SDL_ReleaseGPUTexture(device, texture);
            return nullptr;
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCommands);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transferBuffer;
        source.pixels_per_row = width; // 1行のピクセル数(行の間に隙間がないので幅と同じ)

        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = width;
        destination.h = height;
        destination.d = 1;

        SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(uploadCommands);
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
        return texture;
    }

    TextureHandle RenderDevice::CreateTexture(const platform::Image& image)
    {
        if (m_impl->device == nullptr)
        {
            SDL_Log("RenderDevice::CreateTexture: まだ初期化されていません");
            return {};
        }
        SDL_GPUTexture* gpuTexture = m_impl->CreateGpuTexture(image);
        if (gpuTexture == nullptr)
        {
            return {}; // {} は何も指さないハンドル
        }
        Texture texture;
        texture.gpuTexture = gpuTexture;
        texture.width = static_cast<Uint32>(image.width);
        texture.height = static_cast<Uint32>(image.height);
        return m_impl->textures.Add(texture);
    }

    void RenderDevice::ReleaseTexture(TextureHandle texture)
    {
        if (texture == m_impl->whiteTexture)
        {
            return; // 中で使う白いテクスチャは、終了まで手放さない
        }
        const Texture* found = m_impl->textures.Get(texture);
        if (found == nullptr)
        {
            return; // すでに手放した、または何も指さない
        }
        // GPU のテクスチャは、解放の待ち行列へ。ハンドルの表からはすぐ外す(世代が進み、古いハンドルは無効になる)。
        m_impl->pendingReleases.Push(found->gpuTexture, m_impl->frameNumber);
        m_impl->textures.Remove(texture);
    }

    bool RenderDevice::IsTextureAlive(TextureHandle texture) const
    {
        return texture != m_impl->whiteTexture && m_impl->textures.Contains(texture);
    }

    std::size_t RenderDevice::GetTextureCount() const
    {
        const std::size_t whiteCount = m_impl->textures.Contains(m_impl->whiteTexture) ? 1 : 0;
        return m_impl->textures.Size() - whiteCount;
    }

    std::size_t RenderDevice::GetPendingReleaseCount() const
    {
        return m_impl->pendingReleases.Size();
    }

    bool RenderDevice::BeginFrame(const Color& clearColor)
    {
        m_impl->ResetFrame();
        m_impl->clearColor = clearColor;

        // 0. フレームの番号を進め、手放してから十分に経ったテクスチャを、本当に解放する。
        //    描けないフレーム(最小化など)でも GPU は進むので、描けるかどうかに関係なく、ここで数える。
        ++m_impl->frameNumber;
        m_impl->pendingReleases.ReleaseReady(m_impl->frameNumber, [this](SDL_GPUTexture* texture) {
            SDL_ReleaseGPUTexture(m_impl->device, texture);
        });

        // 1. GPU への命令を溜める入れ物を用意する。
        m_impl->commandBuffer = SDL_AcquireGPUCommandBuffer(m_impl->device);
        if (m_impl->commandBuffer == nullptr)
        {
            SDL_Log("SDL_AcquireGPUCommandBuffer に失敗しました: %s", SDL_GetError());
            return false;
        }

        // 2. 今回描き込む画面(スワップチェーンのテクスチャ)と、その大きさを受け取る。
        //    vsync のため、前の画面がまだ表示中ならここで待つ。これがフレームの速さを決める。
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_impl->commandBuffer, m_impl->window, &m_impl->swapchainTexture,
                                                   &m_impl->screenWidth, &m_impl->screenHeight))
        {
            SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture に失敗しました: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(m_impl->commandBuffer);
            m_impl->ResetFrame();
            return false;
        }
        if (m_impl->swapchainTexture == nullptr)
        {
            // ウィンドウが最小化されているときなどは、描く先が無い。何も描かずに提出だけする。
            SDL_SubmitGPUCommandBuffer(m_impl->commandBuffer);
            m_impl->ResetFrame();
            return false;
        }
        return true;
    }

    void RenderDevice::DrawSprite(const Sprite& sprite)
    {
        if (m_impl->commandBuffer == nullptr)
        {
            return; // BeginFrame が成功していない
        }
        if (m_impl->vertices.size() >= MaxSprites * VerticesPerSprite)
        {
            ++m_impl->droppedSpriteCount; // 上限を超えた分は描かない
            return;
        }

        // スプライトの四隅を、ワールドの位置として頂点に書く(バッチ描画では、位置は CPU 側で決めてしまう)。
        // 画面の位置にするのは GPU(頂点シェーダがカメラの行列を掛ける)。
        // y は上が + なので、上の辺は y + height、下の辺は y。
        // UV は、スプライトが指定した範囲(既定は全体: u0 = v0 = 0、u1 = v1 = 1)。画像の上(v0)を上の辺に貼る。
        //   0 ---- 1      UV: (u0,v0) ---- (u1,v0)
        //   |      |          |                |
        //   3 ---- 2          (u0,v1) ---- (u1,v1)
        const float left = sprite.x;
        const float bottom = sprite.y;
        const float right = sprite.x + sprite.width;
        const float top = sprite.y + sprite.height;
        const Color& c = sprite.color;

        // どのテクスチャで描くかを、区間(バッチ)に記録する。ハンドルのまま覚えておき、EndFrame で中身を引く。
        const auto spriteIndex = static_cast<std::uint32_t>(m_impl->vertices.size() / VerticesPerSprite);
        AddSpriteToBatches(m_impl->batches, sprite.texture, spriteIndex);

        // push_back は vector の末尾に1つ足す(C# の List.Add)。
        m_impl->vertices.push_back({left,  top,    sprite.u0, sprite.v0, c.r, c.g, c.b, c.a}); // 0: 左上
        m_impl->vertices.push_back({right, top,    sprite.u1, sprite.v0, c.r, c.g, c.b, c.a}); // 1: 右上
        m_impl->vertices.push_back({right, bottom, sprite.u1, sprite.v1, c.r, c.g, c.b, c.a}); // 2: 右下
        m_impl->vertices.push_back({left,  bottom, sprite.u0, sprite.v1, c.r, c.g, c.b, c.a}); // 3: 左下
    }

#if HUSKY_DEVELOPMENT
    void RenderDevice::BeginDevUiFrame()
    {
        if (!m_impl->devUiReady || m_impl->commandBuffer == nullptr || m_impl->devUiFrameBegun)
        {
            return; // UI が無い、BeginFrame が成功していない、または、このフレームではもう始めている
        }
        // 描画の部品 → 入力の部品 → ImGui 本体、の順に「新しいフレーム」を伝える(ImGui の決まった手順)。
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        m_impl->devUiFrameBegun = true;
    }
#endif

    void RenderDevice::EndFrame()
    {
        if (m_impl->commandBuffer == nullptr)
        {
            return; // BeginFrame が成功していない
        }

#if HUSKY_DEVELOPMENT
        // 開発用の UI: 作った窓を、描くためのデータ(三角形の一覧)にまとめ、GPU に送る。
        // ImGui の SDL_GPU 用の部品の決まりで、送る処理(Prepare)は描画パスを始める「前」に呼ぶ。
        ImDrawData* devUiDrawData = nullptr;
        if (m_impl->devUiFrameBegun)
        {
            ImGui::Render();
            devUiDrawData = ImGui::GetDrawData();
            ImGui_ImplSDLGPU3_PrepareDrawData(devUiDrawData, m_impl->commandBuffer);
            m_impl->devUiFrameBegun = false;
        }
#endif

        const Uint32 spriteCount = static_cast<Uint32>(m_impl->vertices.size()) / VerticesPerSprite;
        const bool canDraw = spriteCount > 0;

        // 1. 溜めた頂点を、まとめて GPU に送る(描画パスの前に、コピーパスで)。
        if (canDraw)
        {
            m_impl->UploadVertices(static_cast<Uint32>(m_impl->vertices.size() * sizeof(Vertex)));
        }

        // 2. 描画パスを始める。「始めるときに clearColor で塗りつぶす」と指定するだけで画面が塗られる。
        //    {} はすべての項目を 0(や nullptr)で初期化するという意味。必要な所だけ設定する。
        const Color& clear = m_impl->clearColor;
        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = m_impl->swapchainTexture;
        colorTarget.clear_color = SDL_FColor{clear.r, clear.g, clear.b, clear.a};
        colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;   // 始めるときに塗りつぶす
        colorTarget.store_op = SDL_GPU_STOREOP_STORE; // 描いた結果を残す(画面に出すため)

        int drawCallCount = 0;
        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(m_impl->commandBuffer, &colorTarget, 1, nullptr);
        if (renderPass == nullptr)
        {
            SDL_Log("SDL_BeginGPURenderPass に失敗しました: %s", SDL_GetError());
        }
        else
        {
            if (canDraw)
            {
                // 3. 全スプライトで共通のもの(パイプライン、頂点とインデックスのバッファ、画面の大きさ)を1回だけ渡す。
                SDL_BindGPUGraphicsPipeline(renderPass, m_impl->spritePipeline);

                SDL_GPUBufferBinding vertexBinding{};
                vertexBinding.buffer = m_impl->spriteVertexBuffer;
                SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);

                SDL_GPUBufferBinding indexBinding{};
                indexBinding.buffer = m_impl->spriteIndexBuffer;
                SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

                // カメラの行列を、頂点シェーダの 0 番の cbuffer に渡す。
                const FrameUniforms uniforms = {m_impl->viewProjection};
                SDL_PushGPUVertexUniformData(m_impl->commandBuffer, 0, &uniforms, sizeof(uniforms));

                // 4. 区間(同じテクスチャが続くスプライト)ごとに、テクスチャを渡して1回描く。
                const Texture* white = m_impl->textures.Get(m_impl->whiteTexture);
                for (const SpriteBatch& batch : m_impl->batches)
                {
                    // ハンドルから中身を引く。何も指さない、または手放された後なら、白いテクスチャを使う。
                    const Texture* texture = m_impl->textures.Get(batch.texture);
                    if (texture == nullptr)
                    {
                        texture = white;
                    }

                    // ピクセルシェーダの 0 番に、テクスチャとサンプラーの組を渡す。
                    SDL_GPUTextureSamplerBinding textureBinding{};
                    textureBinding.texture = texture->gpuTexture;
                    textureBinding.sampler = m_impl->pointSampler;
                    SDL_BindGPUFragmentSamplers(renderPass, 0, &textureBinding, 1);

                    // インデックスのうち、この区間の分だけを描く。
                    //   第2引数: 描くインデックスの数(スプライト数 × 6)
                    //   第4引数: インデックスバッファの何番目から読むか(区間の最初のスプライト × 6)
                    SDL_DrawGPUIndexedPrimitives(renderPass, batch.spriteCount * IndicesPerSprite, 1,
                                                 batch.firstSprite * IndicesPerSprite, 0, 0);
                    ++drawCallCount;
                }
            }
#if HUSKY_DEVELOPMENT
            // 開発用の UI を、スプライトの後に(手前に)描く。描画命令の数(drawCallCount)には数えない。
            if (devUiDrawData != nullptr)
            {
                ImGui_ImplSDLGPU3_RenderDrawData(devUiDrawData, m_impl->commandBuffer, renderPass);
            }
#endif
            SDL_EndGPURenderPass(renderPass);
        }

        // 4. 溜めた命令を GPU に渡す。GPU はこれを CPU とは別に、少し遅れて実行し、画面に出す。
        SDL_SubmitGPUCommandBuffer(m_impl->commandBuffer);

        m_impl->lastFrameStats.spriteCount = static_cast<int>(spriteCount);
        m_impl->lastFrameStats.drawCallCount = drawCallCount;
        m_impl->lastFrameStats.droppedSpriteCount = m_impl->droppedSpriteCount;
        m_impl->ResetFrame();
    }

    FrameStats RenderDevice::GetLastFrameStats() const
    {
        return m_impl->lastFrameStats;
    }

    int RenderDevice::GetScreenWidth() const
    {
        return static_cast<int>(m_impl->screenWidth);
    }

    int RenderDevice::GetScreenHeight() const
    {
        return static_cast<int>(m_impl->screenHeight);
    }

    void RenderDevice::SetViewProjection(const glm::mat4& viewProjection)
    {
        m_impl->viewProjection = viewProjection;
    }
}
