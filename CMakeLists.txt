cmake_minimum_required(VERSION 3.10) # Atualizado para evitar avisos de depreciação

project(chiaki)

# Like option(), but the value can also be AUTO
macro(tri_option name desc default)
    set("${name}" "${default}" CACHE STRING "${desc}")
    set_property(CACHE "${name}" PROPERTY STRINGS AUTO ON OFF)
endmacro()

# --- CONFIGURAÇÃO PARA BUILD DE CONTROLE (SEM ÁUDIO/VÍDEO) ---
option(CHIAKI_ENABLE_TESTS "Enable tests for Chiaki" OFF)
option(CHIAKI_ENABLE_CLI "Enable CLI for Chiaki" OFF)
option(CHIAKI_ENABLE_GUI "Enable Qt GUI" ON)
option(CHIAKI_ENABLE_ANDROID "Enable Android" OFF)
option(CHIAKI_ENABLE_BOREALIS "Enable Borealis GUI" OFF)
tri_option(CHIAKI_ENABLE_SETSU "Enable libsetsu" OFF) # Linux Only
tri_option(CHIAKI_ENABLE_STEAMDECK_NATIVE "Enable sdeck" OFF)

# Desativando codecs e processamento pesado
option(CHIAKI_LIB_ENABLE_OPUS "Use Opus" OFF)
tri_option(CHIAKI_ENABLE_SPEEX "Use speex" OFF)
tri_option(CHIAKI_ENABLE_RUDP "Enable Remote Play over Internet" ON)

# Forçando FFMPEG e PI Decoders para OFF (Remove erro de vídeo)
set(CHIAKI_FFMPEG_DEFAULT OFF)
tri_option(CHIAKI_ENABLE_FFMPEG_DECODER "Enable FFMPEG video decoder" OFF)
tri_option(CHIAKI_ENABLE_PI_DECODER "Enable Raspberry Pi decoder" OFF)

option(CHIAKI_LIB_ENABLE_MBEDTLS "Use mbedtls" OFF)
option(CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER "Use SDL Gamecontroller for Input" ON)
option(CHIAKI_ENABLE_STEAM_SHORTCUT "Add ability to create Steam shortcut" ON)

# Versão
set(CHIAKI_VERSION_MAJOR 1)
set(CHIAKI_VERSION_MINOR 9)
set(CHIAKI_VERSION_PATCH 5)
set(CHIAKI_VERSION ${CHIAKI_VERSION_MAJOR}.${CHIAKI_VERSION_MINOR}.${CHIAKI_VERSION_PATCH})

# --- REMOVIDO: PkgConfig e Libplacebo (Causadores do erro no Windows) ---
# Se o build for apenas para controle, não precisamos dessas bibliotecas de processamento de imagem.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_CXX_STANDARD 11)

# Verificação de dependências básicas de rede
if(CHIAKI_USE_SYSTEM_CURL)
    find_package(CURL REQUIRED COMPONENTS HTTP HTTPS WS WSS)
endif()

find_package(PythonInterp 3 REQUIRED)

add_subdirectory(third-party)

add_definitions(-DCHIAKI_VERSION_MAJOR=${CHIAKI_VERSION_MAJOR} 
                -DCHIAKI_VERSION_MINOR=${CHIAKI_VERSION_MINOR} 
                -DCHIAKI_VERSION_PATCH=${CHIAKI_VERSION_PATCH} 
                -DCHIAKI_VERSION=\"${CHIAKI_VERSION}\")

# Subdiretórios Essenciais
add_subdirectory(lib)

if(CHIAKI_ENABLE_GUI)
    find_package(SDL2 MODULE REQUIRED) # Necessário para o controle funcionar
    add_subdirectory(gui)
endif()

if(CHIAKI_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
