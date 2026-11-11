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
    endif()
endfunction()

# --- core モジュール ---
# 型、メモリ、ハンドル、リフレクション、GC など、エンジンの一番下の土台。ほかのモジュールを一切参照しない。
add_library(husky_core STATIC
    src/core/FreeListAllocator.cpp
    src/core/Method.cpp
    src/core/ObjectRegistry.cpp
)
husky_set_compile_options(husky_core)
target_include_directories(husky_core PUBLIC src)

# --- platform モジュール(静的ライブラリ) ---
# OS 依存のコードを集める場所。SDL3 を触ってよいのはここだけ。
# STATIC は「.lib を作り、使う側の exe に丸ごと組み込む」という意味。
add_library(husky_platform STATIC
    src/platform/Image.cpp
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
target_link_libraries(husky_platform PRIVATE SDL3::SDL3)

# --- engine モジュール(静的ライブラリ) ---
# ゲームループ、ワールド、アセットなど、OS に依存しないエンジンの本体を集める場所。
add_library(husky_engine STATIC
    src/engine/FrameClock.cpp
    src/engine/Input.cpp
    src/engine/Scene.cpp
)
husky_set_compile_options(husky_engine)
target_include_directories(husky_engine PUBLIC src)

# engine は platform を使う(依存の向きは engine → platform。逆は禁止)。
# PUBLIC にするのは、engine のヘッダ(Input.h)が platform のヘッダ(Keyboard.h)を読み込んでいて、
# engine を使う側にも platform のヘッダが見えている必要があるから。
# SDL3 は platform の中で PRIVATE なので、ここを PUBLIC にしても SDL3 は外に漏れない。
# core も同じ理由で PUBLIC(engine のヘッダは、これから core のハンドルなどを使う)。
target_link_libraries(husky_engine PUBLIC husky_core husky_platform)

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
# engine の描画抽象の、SDL_GPU による実装。platform と同じく SDL3 を触ってよい場所。
# engine 本体に SDL3 が見えないよう、別のライブラリにしている。
add_library(husky_render STATIC
    src/render/RenderDevice.cpp
    ${HUSKY_SHADER_HEADERS} # ソースに含めると、ビルドの前にシェーダの変換が走る
)
husky_set_compile_options(husky_render)
target_include_directories(husky_render PUBLIC src)
# 生成したシェーダのヘッダを #include "shaders/Sprite.vert.dxil.h" のように読めるようにする。
# render の中だけで使うので PRIVATE。
target_include_directories(husky_render PRIVATE "${HUSKY_SHADER_OUTPUT_DIR}")
# RenderDevice.h は platform::Window を使うので platform は PUBLIC。SDL3 は外に出さないので PRIVATE。
target_link_libraries(husky_render
    PUBLIC  husky_platform
    PRIVATE SDL3::SDL3
)

# --- 実行ファイル ---
# src/main.cpp をコンパイルして HuskyEngineV2.exe を作る。
add_executable(HuskyEngineV2
    src/main.cpp
)
husky_set_compile_options(HuskyEngineV2)

# exe は engine、render、platform を使う。SDL3 はそれらの中に隠れているので、ここには書かない。
target_link_libraries(HuskyEngineV2 PRIVATE husky_engine husky_render husky_platform)

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
        tests/engine/FrameClockTests.cpp
        tests/engine/InputTests.cpp
        tests/engine/SceneTests.cpp
        tests/core/HandleTableTests.cpp
        tests/core/ObjectTests.cpp
        tests/core/ObjectRegistryTests.cpp
        tests/core/PropertyTests.cpp
        tests/core/GarbageCollectionTests.cpp
        tests/core/MethodTests.cpp
        tests/core/FreeListAllocatorTests.cpp
    )
    husky_set_compile_options(HuskyTests)
    target_link_libraries(HuskyTests PRIVATE husky_core husky_engine doctest::doctest)
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
