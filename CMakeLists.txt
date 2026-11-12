cmake_minimum_required(VERSION 3.20)
project(uvhttp VERSION 0.1.0 LANGUAGES C CXX)
option(UVHTTP_WITH_HTTP2 "Build HTTP/2 support" ON)
option(UVHTTP_BUILD_SHARED "Build uvhttp as a shared library" OFF)
option(UVHTTP_WITH_ZLIB "Build bundled gzip encoder" ON)
option(UVHTTP_WITH_BROTLI "Build bundled Brotli encoder" ON)
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(third_party)
set(UVHTTP_SOURCES
    src/buffer.cc
    src/event_loop.cc
    src/event_loop_thread.cc
    src/tcp_connection.cc
    src/tcp_client.cc
    src/tcp_server.cc
    src/http.cc
    src/http_server.cc
    src/http_client.cc
    src/client_pool.cc
    src/http_sync_client.cc
    src/http_sync_stream.cc
    src/tls_stream.cc
    src/router.cc
    src/filter.cc
    src/cookie.cc
    src/session.cc
    src/websocket.cc
    src/websocket_client.cc
    src/compression.cc
    src/application.cc)
if(UVHTTP_WITH_HTTP2)
    list(APPEND UVHTTP_SOURCES src/http2_session.cc
    src/http2_server.cc
    src/http2_client.cc
    src/http2_sync_client.cc)
endif()
if(UVHTTP_BUILD_SHARED)
    add_library(uvhttp SHARED ${UVHTTP_SOURCES})
    target_compile_definitions(uvhttp PUBLIC UVHTTP_SHARED PRIVATE UVHTTP_BUILDING_LIBRARY)
else()
    add_library(uvhttp STATIC ${UVHTTP_SOURCES})
endif()
add_library(uvhttp::uvhttp ALIAS uvhttp)
target_compile_features(uvhttp PUBLIC cxx_std_17)
target_include_directories(uvhttp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(uvhttp PUBLIC uvhttp_uv OpenSSL::SSL OpenSSL::Crypto)
if(UVHTTP_WITH_HTTP2)
    target_link_libraries(uvhttp PUBLIC uvhttp_nghttp2)
    target_compile_definitions(uvhttp PUBLIC UVHTTP_WITH_HTTP2=1)
endif()
if(UVHTTP_WITH_ZLIB)
    target_link_libraries(uvhttp PRIVATE uvhttp_zlib)
    target_compile_definitions(uvhttp PUBLIC UVHTTP_WITH_ZLIB=1)
endif()
if(UVHTTP_WITH_BROTLI)
    target_link_libraries(uvhttp PRIVATE uvhttp_brotli)
    target_compile_definitions(uvhttp PUBLIC UVHTTP_WITH_BROTLI=1)
endif()
if(MSVC)
    target_compile_options(uvhttp PRIVATE /utf-8)
endif()
