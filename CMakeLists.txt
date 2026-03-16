cmake_minimum_required(VERSION 3.20)
project(GameServer CXX)

# Allow subprojects that declare older cmake_minimum_required versions
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Lua REQUIRED)

include(FetchContent)

# sol2 (header-only)
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.0
)
FetchContent_MakeAvailable(sol2)

# Standalone Asio (header-only, no Boost dependency)
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

# nlohmann/json (header-only, for WebSocket JSON protocol)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.2
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

set(SOURCES
    src/main.cpp
    src/Entity.cpp
    src/LuaEngine.cpp
    src/Player.cpp
    src/Monster.cpp
    src/GameServer.cpp
    src/WsServer.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASIO_STANDALONE
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${LUA_LIBRARIES} sol2 nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0601
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    endif()
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lua"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/lua"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/web"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/web"
    COMMENT "Copying lua/ and web/ to output directory"
)
