# HuskyEngineV2 のビルド設計図。
# CMake はこのファイルを読んで、Visual Studio のソリューション(.sln / .vcxproj)を「生成」する。
# 生成物は build/ 以下に出る。.sln は手で編集せず、変更はすべてこのファイルに書く。

# このファイルが前提とする CMake の最低バージョン。プリセット(CMakePresets.json)の v6 形式に 3.25 が必要。
cmake_minimum_required(VERSION 3.25)

# プロジェクト名と、使う言語。自分のコードは C++ だけだが、SDL3 が C で書かれているので C も有効にする。
project(HuskyEngineV2 LANGUAGES C CXX)

# --- C++ の言語バージョン ---
# C++17 を必須にする。REQUIRED を付けないと、コンパイラが未対応でも黙って古い規格で進んでしまう。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# コンパイラ独自の拡張を切り、標準 C++ だけを使う(将来 Mac の Clang でも同じ挙動にするため)。
set(CMAKE_CXX_EXTENSIONS OFF)

# --- MSVC 全体に効く設定: 例外と RTTI を無効にする ---
# CMake は MSVC 向けに、既定で /EHsc(例外を有効) と /GR(RTTI を有効) をフラグに入れている。
# CLAUDE.md の方針(例外と RTTI は使わない)に合わせて、ここで取り除く。
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# --- AddressSanitizer(ASan) ---
# ON にすると、自分のコード(husky_set_compile_options を付けたもの)を ASan 付きでビルドする。
# ASan はメモリの誤用(範囲外の読み書き、解放済みメモリの使用、二重解放など)を、起きた瞬間に止めて場所を教える。
# 普段のビルド(vs2022 プリセット)では OFF。ASan 用のプリセット(vs2022-asan)が ON にする。
option(HUSKY_ENABLE_ASAN "AddressSanitizer を有効にする(MSVC)" OFF)
if(HUSKY_ENABLE_ASAN AND MSVC)
    # Debug の既定に入っている /RTC1(実行時チェック)は、ASan と一緒に使えないので取り除く。
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # ASan の実行時ライブラリ(DLL)は、コンパイラ(cl.exe)と同じフォルダにある。exe の隣へコピーするために場所を覚える。
    # get_filename_component(... DIRECTORY) は、ファイルのパスからフォルダの部分だけを取り出す。
    get_filename_component(HUSKY_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()

# --- 依存ライブラリ: SDL3 ---
# FetchContent は「生成(configure)のときに、指定したバージョンのソースを取ってきて、
# 自分のプロジェクトの一部としてビルドする」仕組み。取ってきたソースは build/ 以下に置かれる。
include(FetchContent)

# SDL3 を自分のプロジェクトに組み込むときの設定。FetchContent_MakeAvailable より前に決めておく必要がある。
set(SDL_SHARED OFF CACHE BOOL "" FORCE)       # DLL 版は作らない
set(SDL_STATIC ON CACHE BOOL "" FORCE)        # 静的ライブラリ版を作り、exe に組み込む(DLL のコピーが不要になる)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE) # SDL 自身のテスト用ライブラリは要らない

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.20   # バージョンを固定する。毎回同じ結果でビルドされるようにするため
    GIT_SHALLOW    TRUE             # 履歴は不要なので、最新の1コミット分だけ取ってくる
)
# ここで実際にダウンロードし、SDL3 の CMakeLists.txt を読み込む。
# これ以降、SDL3::SDL3 という名前のターゲット(ライブラリ)が使えるようになる。
FetchContent_MakeAvailable(SDL3)

# --- 依存ライブラリ: doctest(テスト用。ゲームの exe には入らない) ---
# テストを作るかどうか。OFF にすると、doctest の取り込みもテスト用 exe も作らない。
option(HUSKY_BUILD_TESTS "テスト用の exe を作る" ON)
if(HUSKY_BUILD_TESTS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE) # doctest 自身のテストは要らない
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.5.3
        GIT_SHALLOW    TRUE
        # SYSTEM: doctest のヘッダを「外部のヘッダ」として扱い、その中の警告は出さない(自分のコードの警告だけ見る)。
        SYSTEM
    )
    FetchContent_MakeAvailable(doctest)
endif()

# --- 依存ライブラリ: yyjson(JSON の読み書き。2026-11-12 にユーザーと相談して追加) ---
# C で書かれた速い JSON ライブラリ。例外を使わない。MIT ライセンス。
# 使うのは engine の JsonFormat.cpp の中だけ(PRIVATE でリンクする)。ほかのコードは yyjson を知らないので、
# 別のライブラリに替えるときは JsonFormat.cpp だけを書き直せばよい。
set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG        0.12.0   # バージョンを固定する(出たばかりの最新版は避ける)
    GIT_SHALLOW    TRUE
    SYSTEM                  # yyjson の中の警告は出さない
)
FetchContent_MakeAvailable(yyjson)

# --- 依存ライブラリ: glm(数学。ベクトルと行列。2026-11-12 にユーザーと相談して追加) ---
# ヘッダだけでできたライブラリ(ビルドするものが無く、#include するだけで使える)。例外を使わない。MIT ライセンス。
# シェーダの言語(GLSL)と同じ名前と書き方(glm::vec3、glm::mat4、glm::ortho など)で、ゲームでよく使われる。
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE) # 静的ライブラリ版は作らない(ヘッダだけを使う)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1    # バージョンを固定する
    GIT_SHALLOW    TRUE
    SYSTEM                  # glm の中の警告は出さない
)
# 注意: glm 1.0.1 の CMakeLists.txt は古い版の CMake を前提に書かれているので、生成(cmake --preset)のときに
# 「CMake Deprecation Warning」が glm のファイルについて出る。glm 側の書き方の問題で、ビルドには影響しない。
FetchContent_MakeAvailable(glm)

# エンジンで使う glm の設定を、1つの入れ物(INTERFACE ライブラリ: ビルドするものは無く、設定だけを渡すターゲット)にまとめる。
# glm を使うターゲットは、glm を直接ではなく husky_glm をリンクする。こうすると、全員が同じ設定で glm を読む
# (設定が食い違うと、同じ関数が場所によって違う結果を返してしまう)。
add_library(husky_glm INTERFACE)
target_link_libraries(husky_glm INTERFACE glm::glm-header-only)
# 射影行列が作る奥行き(z)の範囲を 0〜1 にする。glm の既定は OpenGL の -1〜+1 だが、
# SDL_GPU(Direct3D 12、Vulkan、Metal)のクリップ座標の z は 0〜1。3D で奥行きを使うときに必要になる。
target_compile_definitions(husky_glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)

# --- 依存ライブラリ: Dear ImGui(開発用の UI。2026-11-12 にユーザーと相談して追加) ---
# 開発ビルド(Debug)だけでリンクし、製品(Release)には入れない(CLAUDE.md: ImGui のコードをリリースビルドに含めない)。
# ImGui のリポジトリには CMakeLists.txt が無いので、FetchContent でソースを取ってくるだけにして、
# 使うファイルを自分で並べてライブラリ(husky_imgui)にする。
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b  # バージョンを固定する(出たばかりの最新版は避け、一つ前の系統の修正版にする)
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(husky_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    # std::string を ImGui::InputText でそのまま編集できるようにする小さな部品(インスペクタで使う)
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    # SDL3 とつなぐ公式の部品(バックエンド)。入力は imgui_impl_sdl3、描画は SDL_GPU 用の imgui_impl_sdlgpu3。
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
# SYSTEM: ImGui のヘッダを外部のヘッダとして扱い、その中の警告は出さない。
target_include_directories(husky_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
# バックエンドは SDL3 を使う。ImGui を直接使ってよいのは、render(つなぎ込み)と tools(画面)だけ。
target_link_libraries(husky_imgui PRIVATE SDL3::SDL3)
# ASan のビルドでは、ImGui も ASan 付きでコンパイルする。
# ASan 付きの MSVC の std::string / std::vector は、範囲外を見つけるための印(注釈)を持つ作りに変わる。
# imgui_stdlib.cpp は std::string を使うので、ImGui だけ ASan なしだと、同じ型が2通りの作りで混ざってしまい、
# リンカが「annotate_string の不一致」で止める(混ざると、メモリの扱いが食い違って危険なため)。
if(HUSKY_ENABLE_ASAN AND MSVC)
    target_compile_options(husky_imgui PRIVATE /fsanitize=address /Zi)
endif()

# --- 自分のコード全部に共通するコンパイル設定 ---
# function は CMake の関数。ターゲット名を受け取り、同じ設定をまとめて付ける。
# モジュールが増えても、設定を1か所で管理できる。
function(husky_set_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4        # 警告レベル4(高め)。問題を早く見つけるため。
            /utf-8     # ソースを UTF-8 として読む。日本語コメントの文字化けや誤警告を防ぐ。
            /EHs-c-    # C++ 例外を無効にする。
            /GR-       # RTTI(dynamic_cast や typeid に使う実行時型情報)を無効にする。
        )
        # 標準ライブラリ(MSVC STL)にも「例外なし」で動くよう伝える。これが無いと STL のヘッダが警告を出す。
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)

        if(HUSKY_ENABLE_ASAN)
            # /fsanitize=address: コンパイラが、メモリを読み書きする場所に確認のコードを差し込む。
            # /Zi: デバッグ情報を出す(ASan の報告に、ファイル名と行番号を出すため)。
            target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
            # /INCREMENTAL:NO: 差分リンクは ASan と一緒に使えないので切る。/DEBUG: リンク時にもデバッグ情報を残す。
            # (静的ライブラリにはリンクの段階が無いので、exe にだけ効く)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
        endif()
    endif()

    # 開発ビルドの印。Debug の構成(ASan のビルドも Debug)でだけ HUSKY_DEVELOPMENT=1 を定義する。
    # Release は「製品」として扱い、エディタの機能(シーンの保存など)は #if HUSKY_DEVELOPMENT でコードごと外す。
    # $<$<CONFIG:Debug>:...> は「ジェネレータ式」。Visual Studio のように1つのソリューションに Debug と Release が
    # 同居する場合でも、ビルドする構成ごとに中身を切り替えられる(普通の if では、生成のときに1回しか判断できない)。
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:HUSKY_DEVELOPMENT=1>)
endfunction()

# exe を ASan 付きで動かすために、ASan の実行時ライブラリ(DLL)を exe の隣へコピーする。
# Debug 用(dbg が付くもの)とそれ以外の両方をコピーしておく(どちらを使うかは、ビルドの構成で決まる)。
function(husky_copy_asan_runtime target)
    if(HUSKY_ENABLE_ASAN AND MSVC)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${HUSKY_MSVC_BIN_DIR}/clang_rt.asan_dynamic-x86_64.dll"
                    "${HUSKY_MSVC_BIN_DIR}/clang_rt.asan_dbg_dynamic-x86_64.dll"
                    "$<TARGET_FILE_DIR:${target}>"
            COMMENT "ASan の実行時ライブラリを exe の隣へコピー"
            VERBATIM
        )
    endif()
endfunction()

# --- core モジュール ---
# 型、メモリ、ハンドル、リフレクション、GC など、エンジンの一番下の土台。ほかのモジュールを一切参照しない。
add_library(husky_core STATIC
    src/core/FreeListAllocator.cpp
    src/core/Guid.cpp
    src/core/JobSystem.cpp
    src/core/MemoryTracker.cpp
    src/core/Method.cpp
    src/core/ObjectRegistry.cpp
)
husky_set_compile_options(husky_core)
target_include_directories(husky_core PUBLIC src)

# プログラム全体の operator new / delete の差し替え(core/GlobalOperatorNew.cpp)。
# 静的ライブラリ(husky_core)には入れず、exe ごとに直接ソースとして入れる。
# 静的ライブラリの中に置くと、リンカが標準の operator new を先に選んで、差し替えが効かないことがあるため。
set(HUSKY_GLOBAL_NEW_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/core/GlobalOperatorNew.cpp")

# --- platform モジュール(静的ライブラリ) ---
# OS 依存のコードを集める場所。SDL3 を触ってよいのはここだけ。
# STATIC は「.lib を作り、使う側の exe に丸ごと組み込む」という意味。
add_library(husky_platform STATIC
    src/platform/FileSystem.cpp
    src/platform/Image.cpp
    src/platform/MemoryHooks.cpp
    src/platform/Platform.cpp
    src/platform/Window.cpp
)
husky_set_compile_options(husky_platform)

# #include "platform/Window.h" のように、src/ からのパスでヘッダを書けるようにする。
# PUBLIC は「自分も使うし、自分を使う側(exe)にも伝える」という意味。
target_include_directories(husky_platform PUBLIC src)

# platform が SDL3 を使うことを宣言する。
# これで (1)SDL3 のヘッダのフォルダがインクルードパスに入り、(2)SDL3 のライブラリがリンクされる。
# PRIVATE にしたので、SDL3 のヘッダのフォルダは platform を使う側(exe)には伝わらない。
# つまり main.cpp で #include <SDL3/SDL.h> と書くと、コンパイルエラーになる。
# 「SDL3 は platform だけが触る」というルールを、ビルドの設定で守らせている。
# core: SDL のメモリの確保を MemoryTracker に通すため(MemoryHooks.cpp)。ヘッダには出さないので PRIVATE。
target_link_libraries(husky_platform PRIVATE SDL3::SDL3 husky_core)

# --- engine モジュール(静的ライブラリ) ---
# ゲームループ、ワールド、アセットなど、OS に依存しないエンジンの本体を集める場所。
add_library(husky_engine STATIC
    src/engine/Actor.cpp
    src/engine/Application.cpp
    src/engine/AssetDatabase.cpp
    src/engine/AssetLoader.cpp
    src/engine/Camera.cpp
    src/engine/CameraComponent.cpp
    src/engine/CameraFollowComponent.cpp
    src/engine/EngineTypes.cpp
    src/engine/FrameClock.cpp
    src/engine/Input.cpp
    src/engine/ArchiveValue.cpp
    src/engine/JsonFormat.cpp
    src/engine/ObjectSerializer.cpp
    src/engine/Scene.cpp
    src/engine/SceneSerializer.cpp
    src/engine/SpriteAnimatorComponent.cpp
    src/engine/SpriteComponent.cpp
    src/engine/SpriteSheetData.cpp
)
husky_set_compile_options(husky_engine)
target_include_directories(husky_engine PUBLIC src)

# engine は platform を使う(依存の向きは engine → platform。逆は禁止)。
# PUBLIC にするのは、engine のヘッダ(Input.h)が platform のヘッダ(Keyboard.h)を読み込んでいて、
# engine を使う側にも platform のヘッダが見えている必要があるから。
# SDL3 は platform の中で PRIVATE なので、ここを PUBLIC にしても SDL3 は外に漏れない。
# core も同じ理由で PUBLIC(engine のヘッダは、これから core のハンドルなどを使う)。
# render も PUBLIC(engine のヘッダ SpriteComponent.h が render::Color を使うため)。
# 依存の向きは engine → render。render は下の層で、engine を知らない(逆は禁止)。
# render の中の SDL3 は PRIVATE なので、ここを PUBLIC にしても SDL3 は外に漏れない。
# husky_render は、このファイルの下のほうで定義している。CMake はターゲットの名前を最後にまとめて解決するので、
# 定義より前に名前を書いてもよい。
target_link_libraries(husky_engine PUBLIC husky_core husky_platform husky_render husky_glm) # husky_glm: カメラの行列などの計算に使う
# yyjson は JsonFormat.cpp の中だけで使うので PRIVATE。engine を使う側(exe やテスト)には yyjson のヘッダが見えない。
target_link_libraries(husky_engine PRIVATE yyjson)

# --- シェーダの変換 ---
# HLSL で書いたシェーダを、ビルドの途中で DXC(Vulkan SDK に付属)に変換させる。
#   DXIL   : Direct3D 12 用
#   SPIR-V : Vulkan 用
# 変換結果は C++ の配列としてヘッダに書き出し(DXC の -Fh オプション)、exe に埋め込む。
# DXC は環境変数 VULKAN_SDK から探すので、パスを Windows 固有の書き方にしなくて済む。
find_program(HUSKY_DXC dxc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
message(STATUS "DXC: ${HUSKY_DXC}")

set(HUSKY_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated")

# シェーダ1つを DXIL と SPIR-V に変換する。
#   name   : 出力の名前(例: Sprite.vert)
#   stage  : vs(頂点シェーダ) か ps(ピクセルシェーダ)
#   OUTPUTS: 生成されるヘッダのパスを、この名前の変数に足して返す
function(husky_compile_shader name stage outputsVar)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${name}.hlsl")
    # 変数名に使えない「.」を「_」に置き換える(例: Sprite.vert → Sprite_vert)
    string(REPLACE "." "_" symbol "${name}")
    set(outDxil "${HUSKY_SHADER_OUTPUT_DIR}/shaders/${name}.dxil.h")
    set(outSpirv "${HUSKY_SHADER_OUTPUT_DIR}/shaders/${name}.spv.h")

    # add_custom_command は「この出力ファイルが必要になったら、このコマンドで作る」という規則を登録する。
    # 入力(.hlsl)が変わったときだけ、作り直される。
    add_custom_command(
        OUTPUT "${outDxil}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HUSKY_SHADER_OUTPUT_DIR}/shaders"
        # -Wno-ignored-attributes: [[vk::...]] は Vulkan 用の指定で、DXIL では無視される。その警告を出さない。
        COMMAND "${HUSKY_DXC}" -T ${stage}_6_0 -E main -Wno-ignored-attributes -Fh "${outDxil}" -Vn g_${symbol}_dxil "${input}"
        DEPENDS "${input}"
        COMMENT "シェーダを DXIL に変換: ${name}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${outSpirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${HUSKY_SHADER_OUTPUT_DIR}/shaders"
        COMMAND "${HUSKY_DXC}" -spirv -T ${stage}_6_0 -E main -Fh "${outSpirv}" -Vn g_${symbol}_spirv "${input}"
        DEPENDS "${input}"
        COMMENT "シェーダを SPIR-V に変換: ${name}"
        VERBATIM
    )
    # PARENT_SCOPE: 関数の外(呼んだ側)の変数を書き換える指定
    set(${outputsVar} ${${outputsVar}} "${outDxil}" "${outSpirv}" PARENT_SCOPE)
endfunction()

set(HUSKY_SHADER_HEADERS "")
husky_compile_shader(Sprite.vert vs HUSKY_SHADER_HEADERS)
husky_compile_shader(Sprite.frag ps HUSKY_SHADER_HEADERS)

# --- render(描画層、静的ライブラリ) ---
# SDL_GPU で描く層。公開するのは RenderDevice と独自の型(Color、Sprite など)だけ。
# platform と同じく SDL3 を触ってよい場所。engine 本体に SDL3 が見えないよう、別のライブラリにしている。
# engine はこれを使う(engine → render)。render は engine を知らない。
add_library(husky_render STATIC
    src/render/RenderDevice.cpp
    ${HUSKY_SHADER_HEADERS} # ソースに含めると、ビルドの前にシェーダの変換が走る
)
husky_set_compile_options(husky_render)
target_include_directories(husky_render PUBLIC src)
# 生成したシェーダのヘッダを #include "shaders/Sprite.vert.dxil.h" のように読めるようにする。
# render の中だけで使うので PRIVATE。
target_include_directories(husky_render PRIVATE "${HUSKY_SHADER_OUTPUT_DIR}")
# RenderDevice.h は platform::Window と core::Handle(TextureHandle)を使うので、platform と core は PUBLIC。
# SDL3 は外に出さないので PRIVATE。
target_link_libraries(husky_render
    PUBLIC  husky_platform
            husky_core
            husky_glm # RenderDevice.h がカメラの行列(glm::mat4)を受け取るので PUBLIC
    PRIVATE SDL3::SDL3
            # 開発ビルドだけ、ImGui をつなぐ(ジェネレータ式: Debug のときだけ husky_imgui になり、Release では空になる)
            $<$<CONFIG:Debug>:husky_imgui>
)

# --- ゲームのコード(このゲームの型、型の登録、最初の配置。src/game) ---
# エンジンの部品(付け外しするもの)ではなく、エンジンを使う側(アプリケーション)の本体。engine に依存する。
# 起動する exe(HuskyEngineV2)と、エディタ(HuskyEditorCli)の両方がリンクする(エディタもゲームの型を知る必要があるため)。
add_library(husky_game STATIC
    src/game/Game.cpp
)
husky_set_compile_options(husky_game)
target_include_directories(husky_game PUBLIC src)
target_link_libraries(husky_game PUBLIC husky_engine)

# --- tools モジュール(開発用の UI とエディタの機能。開発ビルドだけ) ---
# engine に依存する。中のコードは #if HUSKY_DEVELOPMENT で囲んであり、Release では空のライブラリになる。
add_library(husky_tools STATIC
    src/tools/DebugWindow.cpp
    src/tools/EditorCli.cpp
    src/tools/EditorCommands.cpp
    src/tools/EditorPlayMode.cpp
    src/tools/Inspector.cpp
)
husky_set_compile_options(husky_tools)
target_include_directories(husky_tools PUBLIC src)
target_link_libraries(husky_tools PUBLIC husky_engine PRIVATE $<$<CONFIG:Debug>:husky_imgui>)

# --- エディタの CLI(HuskyEditorCli.exe。画面を開かずに、エディタのコマンドを1回だけ実行する) ---
# 中身は tools::RunEditorCli。開発ビルドだけで使える(Release のビルドでは「使えない」と出して終わる)。
add_executable(HuskyEditorCli
    src/tools/EditorCliMain.cpp
    ${HUSKY_GLOBAL_NEW_SOURCE} # ゲームとテストと同じく、すべての new を MemoryTracker に通す
)
husky_set_compile_options(HuskyEditorCli)
husky_copy_asan_runtime(HuskyEditorCli)
# 既定のアセットのフォルダ(ソースの assets)を教える。ゲームと同じく、開発ビルドだけ。
target_compile_definitions(HuskyEditorCli PRIVATE "$<$<CONFIG:Debug>:HUSKY_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/\">")
target_link_libraries(HuskyEditorCli PRIVATE husky_platform $<$<CONFIG:Debug>:husky_tools> $<$<CONFIG:Debug>:husky_game>)

# --- 実行ファイル ---
# src/main.cpp をコンパイルして HuskyEngineV2.exe を作る。
add_executable(HuskyEngineV2
    src/main.cpp
    ${HUSKY_GLOBAL_NEW_SOURCE} # すべての new を MemoryTracker に通す
)
husky_set_compile_options(HuskyEngineV2)
husky_copy_asan_runtime(HuskyEngineV2)

# 開発ビルドだけ、ソースのフォルダの場所を教える(開発ビルドは、ソースの assets/ を直接読み書きするため)。
# この PC の場所が exe に埋め込まれるが、開発ビルドだけで、製品(Release)には入らない。
target_compile_definitions(HuskyEngineV2 PRIVATE "$<$<CONFIG:Debug>:HUSKY_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}/\">")

# exe は engine、render、platform を使う。SDL3 はそれらの中に隠れているので、ここには書かない。
target_link_libraries(HuskyEngineV2 PRIVATE husky_game husky_engine husky_render husky_platform)
# tools(開発用の UI)は、開発ビルドだけリンクする。製品(Release)には入らない。
target_link_libraries(HuskyEngineV2 PRIVATE $<$<CONFIG:Debug>:husky_tools>)

# ビルドのたびに、assets フォルダを exe の隣へコピーする。
# プログラムは exe の場所を基準にアセットを読むので、どこから起動しても同じパスで見つかる。
# $<TARGET_FILE_DIR:...> は「その exe が出力されるフォルダ」(Debug / Release で変わる)を表す。
add_custom_command(TARGET HuskyEngineV2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:HuskyEngineV2>/assets"
    COMMENT "assets を exe の隣へコピー"
    VERBATIM
)

# --- テスト用の実行ファイル ---
# ウィンドウも GPU も使わない(ヘッドレス)。engine など、OS や SDL に依存しない部分をテストする。
if(HUSKY_BUILD_TESTS)
    add_executable(HuskyTests
        tests/TestMain.cpp
        tests/TestFolder.h
        tests/tools/HeadlessImGui.h
        ${HUSKY_GLOBAL_NEW_SOURCE} # ゲームと同じく、すべての new を MemoryTracker に通す
        tests/engine/FrameClockTests.cpp
        tests/engine/InputTests.cpp
        tests/engine/SceneTests.cpp
        tests/engine/ComponentTests.cpp
        tests/engine/SpriteComponentTests.cpp
        tests/engine/SpriteAnimatorComponentTests.cpp
        tests/engine/ApplicationTests.cpp
        tests/engine/JsonFormatTests.cpp
        tests/engine/ObjectSerializerTests.cpp
        tests/engine/SceneSerializerTests.cpp
        tests/engine/AssetDatabaseTests.cpp
        tests/engine/AssetLoaderTests.cpp
        tests/engine/AssetRefTests.cpp
        tests/engine/SpriteSheetDataTests.cpp
        tests/engine/CameraTests.cpp
        tests/engine/CameraFollowComponentTests.cpp
        tests/engine/TransformTests.cpp
        tests/platform/FileSystemTests.cpp
        tests/platform/MemoryHooksTests.cpp
        tests/render/SpriteBatchTests.cpp
        tests/render/DeferredReleaseQueueTests.cpp
        tests/tools/InspectorTests.cpp
        tests/tools/EditorCliTests.cpp
        tests/tools/EditorCommandsTests.cpp
        tests/tools/EditorPlayModeTests.cpp
        tests/core/TypeRegistryTests.cpp
        tests/core/StructReflectionTests.cpp
        tests/core/GuidTests.cpp
        tests/core/JobSystemTests.cpp
        tests/game/GameTests.cpp
        tests/core/HandleTableTests.cpp
        tests/core/ObjectTests.cpp
        tests/core/ObjectRegistryTests.cpp
        tests/core/PropertyTests.cpp
        tests/core/GarbageCollectionTests.cpp
        tests/core/MethodTests.cpp
        tests/core/FreeListAllocatorTests.cpp
        tests/core/MemoryTrackerTests.cpp
    )
    husky_set_compile_options(HuskyTests)
    husky_copy_asan_runtime(HuskyTests)
    target_link_libraries(HuskyTests PRIVATE husky_core husky_engine husky_game doctest::doctest)
    # テストで共通に使うヘッダ(TestFolder.h など)を、#include "TestFolder.h" で読めるようにする。
    target_include_directories(HuskyTests PRIVATE tests)
    # 開発ビルドだけ、tools(インスペクタ)と ImGui のテストもする。
    target_link_libraries(HuskyTests PRIVATE $<$<CONFIG:Debug>:husky_tools> $<$<CONFIG:Debug>:husky_imgui>)
    # 例外が無効なので、doctest にも「例外を使わずに動く」設定を伝える。
    # _BUT_WITH_ALL_ASSERTS: 例外なしでも REQUIRE(失敗したら打ち切る確認)を使えるようにする。
    #   例外で「そのテストだけ」を抜けられないので、REQUIRE が失敗するとテストの実行全体がそこで止まる
    #   (止まっても失敗として報告されるので、見逃すことはない)。
    target_compile_definitions(HuskyTests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)

    # CTest(CMake のテスト実行の仕組み)に登録する。
    # これで `ctest --preset debug` から HuskyTests が実行され、失敗すると ctest も失敗を返す。
    enable_testing()
    add_test(NAME HuskyTests COMMAND HuskyTests)
endif()

# Visual Studio でソリューションを開いたとき、F5 で起動するプロジェクトを HuskyEngineV2 にする。
# (指定しないと、CMake が作る補助プロジェクト ALL_BUILD が選ばれてしまう)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT HuskyEngineV2)
