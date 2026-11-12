add_library(uvhttp_uv STATIC
    libuv/src/fs-poll.c
    libuv/src/idna.c
    libuv/src/inet.c
    libuv/src/random.c
    libuv/src/strscpy.c
    libuv/src/strtok.c
    libuv/src/thread-common.c
    libuv/src/threadpool.c
    libuv/src/timer.c
    libuv/src/uv-common.c
    libuv/src/uv-data-getter-setters.c
    libuv/src/version.c
)
if(WIN32)
    target_sources(uvhttp_uv PRIVATE
        libuv/src/win/async.c
        libuv/src/win/core.c
        libuv/src/win/detect-wakeup.c
        libuv/src/win/dl.c
        libuv/src/win/error.c
        libuv/src/win/fs.c
        libuv/src/win/fs-event.c
        libuv/src/win/getaddrinfo.c
        libuv/src/win/getnameinfo.c
        libuv/src/win/handle.c
        libuv/src/win/loop-watcher.c
        libuv/src/win/pipe.c
        libuv/src/win/thread.c
        libuv/src/win/poll.c
        libuv/src/win/process.c
        libuv/src/win/process-stdio.c
        libuv/src/win/signal.c
        libuv/src/win/snprintf.c
        libuv/src/win/stream.c
        libuv/src/win/tcp.c
        libuv/src/win/tty.c
        libuv/src/win/udp.c
        libuv/src/win/util.c
        libuv/src/win/winapi.c
        libuv/src/win/winsock.c
    )
    target_compile_definitions(uvhttp_uv PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 _CRT_DECLARE_NONSTDC_NAMES=0)
    target_link_libraries(uvhttp_uv PUBLIC psapi user32 advapi32 iphlpapi userenv ws2_32 dbghelp ole32 shell32 synchronization)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(uvhttp_uv PRIVATE
        libuv/src/unix/async.c
        libuv/src/unix/core.c
        libuv/src/unix/dl.c
        libuv/src/unix/fs.c
        libuv/src/unix/getaddrinfo.c
        libuv/src/unix/getnameinfo.c
        libuv/src/unix/loop-watcher.c
        libuv/src/unix/loop.c
        libuv/src/unix/pipe.c
        libuv/src/unix/poll.c
        libuv/src/unix/process.c
        libuv/src/unix/random-devurandom.c
        libuv/src/unix/signal.c
        libuv/src/unix/stream.c
        libuv/src/unix/tcp.c
        libuv/src/unix/thread.c
        libuv/src/unix/tty.c
        libuv/src/unix/udp.c
        libuv/src/unix/proctitle.c
        libuv/src/unix/linux.c
        libuv/src/unix/procfs-exepath.c
        libuv/src/unix/random-getrandom.c
        libuv/src/unix/random-sysctl-linux.c
    )
    target_compile_definitions(uvhttp_uv PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _GNU_SOURCE _POSIX_C_SOURCE=200112)
    target_link_libraries(uvhttp_uv PUBLIC inux pthread dl rt m)
elseif(APPLE)
    target_sources(uvhttp_uv PRIVATE
        libuv/src/unix/async.c
        libuv/src/unix/core.c
        libuv/src/unix/dl.c
        libuv/src/unix/fs.c
        libuv/src/unix/getaddrinfo.c
        libuv/src/unix/getnameinfo.c
        libuv/src/unix/loop-watcher.c
        libuv/src/unix/loop.c
        libuv/src/unix/pipe.c
        libuv/src/unix/poll.c
        libuv/src/unix/process.c
        libuv/src/unix/random-devurandom.c
        libuv/src/unix/signal.c
        libuv/src/unix/stream.c
        libuv/src/unix/tcp.c
        libuv/src/unix/thread.c
        libuv/src/unix/tty.c
        libuv/src/unix/udp.c
        libuv/src/unix/proctitle.c
        libuv/src/unix/bsd-ifaddrs.c
        libuv/src/unix/kqueue.c
        libuv/src/unix/random-getentropy.c
        libuv/src/unix/darwin-proctitle.c
        libuv/src/unix/darwin.c
        libuv/src/unix/fsevents.c
    )
    target_compile_definitions(uvhttp_uv PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _DARWIN_UNLIMITED_SELECT=1 _DARWIN_USE_64_BIT_INODE=1)
    target_link_libraries(uvhttp_uv PUBLIC pthread m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_sources(uvhttp_uv PRIVATE
        libuv/src/unix/async.c
        libuv/src/unix/core.c
        libuv/src/unix/dl.c
        libuv/src/unix/fs.c
        libuv/src/unix/getaddrinfo.c
        libuv/src/unix/getnameinfo.c
        libuv/src/unix/loop-watcher.c
        libuv/src/unix/loop.c
        libuv/src/unix/pipe.c
        libuv/src/unix/poll.c
        libuv/src/unix/process.c
        libuv/src/unix/random-devurandom.c
        libuv/src/unix/signal.c
        libuv/src/unix/stream.c
        libuv/src/unix/tcp.c
        libuv/src/unix/thread.c
        libuv/src/unix/tty.c
        libuv/src/unix/udp.c
        libuv/src/unix/freebsd.c
        libuv/src/unix/posix-hrtime.c
        libuv/src/unix/bsd-proctitle.c
        libuv/src/unix/bsd-ifaddrs.c
        libuv/src/unix/kqueue.c
        libuv/src/unix/random-getrandom.c
    )
    target_compile_definitions(uvhttp_uv PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
    target_link_libraries(uvhttp_uv PUBLIC pthread m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    target_sources(uvhttp_uv PRIVATE
        libuv/src/unix/async.c
        libuv/src/unix/core.c
        libuv/src/unix/dl.c
        libuv/src/unix/fs.c
        libuv/src/unix/getaddrinfo.c
        libuv/src/unix/getnameinfo.c
        libuv/src/unix/loop-watcher.c
        libuv/src/unix/loop.c
        libuv/src/unix/pipe.c
        libuv/src/unix/poll.c
        libuv/src/unix/process.c
        libuv/src/unix/random-devurandom.c
        libuv/src/unix/signal.c
        libuv/src/unix/stream.c
        libuv/src/unix/tcp.c
        libuv/src/unix/thread.c
        libuv/src/unix/tty.c
        libuv/src/unix/udp.c
        libuv/src/unix/posix-hrtime.c
        libuv/src/unix/bsd-proctitle.c
        libuv/src/unix/bsd-ifaddrs.c
        libuv/src/unix/kqueue.c
        libuv/src/unix/random-getentropy.c
        libuv/src/unix/openbsd.c
    )
    target_compile_definitions(uvhttp_uv PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
    target_link_libraries(uvhttp_uv PUBLIC pthread m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    target_sources(uvhttp_uv PRIVATE
        libuv/src/unix/async.c
        libuv/src/unix/core.c
        libuv/src/unix/dl.c
        libuv/src/unix/fs.c
        libuv/src/unix/getaddrinfo.c
        libuv/src/unix/getnameinfo.c
        libuv/src/unix/loop-watcher.c
        libuv/src/unix/loop.c
        libuv/src/unix/pipe.c
        libuv/src/unix/poll.c
        libuv/src/unix/process.c
        libuv/src/unix/random-devurandom.c
        libuv/src/unix/signal.c
        libuv/src/unix/stream.c
        libuv/src/unix/tcp.c
        libuv/src/unix/thread.c
        libuv/src/unix/tty.c
        libuv/src/unix/udp.c
        libuv/src/unix/haiku.c
        libuv/src/unix/bsd-ifaddrs.c
        libuv/src/unix/no-fsevents.c
        libuv/src/unix/no-proctitle.c
        libuv/src/unix/posix-hrtime.c
        libuv/src/unix/posix-poll.c
    )
    target_compile_definitions(uvhttp_uv PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _BSD_SOURCE)
    target_link_libraries(uvhttp_uv PUBLIC pthread network)
else()
    message(FATAL_ERROR "Unsupported libuv target platform")
endif()
target_include_directories(uvhttp_uv PUBLIC libuv/include PRIVATE libuv/src)
set_target_properties(uvhttp_uv PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(UVHTTP_WITH_HTTP2)
add_library(uvhttp_nghttp2 STATIC
    nghttp2/lib/nghttp2_alpn.c
    nghttp2/lib/nghttp2_buf.c
    nghttp2/lib/nghttp2_callbacks.c
    nghttp2/lib/nghttp2_debug.c
    nghttp2/lib/nghttp2_extpri.c
    nghttp2/lib/nghttp2_frame.c
    nghttp2/lib/nghttp2_hd.c
    nghttp2/lib/nghttp2_hd_huffman.c
    nghttp2/lib/nghttp2_hd_huffman_data.c
    nghttp2/lib/nghttp2_helper.c
    nghttp2/lib/nghttp2_http.c
    nghttp2/lib/nghttp2_map.c
    nghttp2/lib/nghttp2_mem.c
    nghttp2/lib/nghttp2_option.c
    nghttp2/lib/nghttp2_outbound_item.c
    nghttp2/lib/nghttp2_pq.c
    nghttp2/lib/nghttp2_priority_spec.c
    nghttp2/lib/nghttp2_queue.c
    nghttp2/lib/nghttp2_ratelim.c
    nghttp2/lib/nghttp2_rcbuf.c
    nghttp2/lib/nghttp2_session.c
    nghttp2/lib/nghttp2_stream.c
    nghttp2/lib/nghttp2_submit.c
    nghttp2/lib/nghttp2_time.c
    nghttp2/lib/nghttp2_version.c
    nghttp2/lib/sfparse.c
)
target_include_directories(uvhttp_nghttp2 PUBLIC nghttp2/lib/includes ../qmake_compat PRIVATE nghttp2/lib)
target_compile_definitions(uvhttp_nghttp2 PUBLIC NGHTTP2_STATICLIB PRIVATE BUILDING_NGHTTP2)
set_target_properties(uvhttp_nghttp2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
if(UVHTTP_WITH_ZLIB)
add_library(uvhttp_zlib STATIC zlib/adler32.c zlib/crc32.c zlib/deflate.c zlib/trees.c zlib/zutil.c)
target_include_directories(uvhttp_zlib PUBLIC zlib)
set_target_properties(uvhttp_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
if(UVHTTP_WITH_BROTLI)
add_library(uvhttp_brotli STATIC
    brotli/c/common/constants.c
    brotli/c/common/context.c
    brotli/c/common/dictionary.c
    brotli/c/common/platform.c
    brotli/c/common/shared_dictionary.c
    brotli/c/common/transform.c
    brotli/c/enc/backward_references.c
    brotli/c/enc/backward_references_hq.c
    brotli/c/enc/bit_cost.c
    brotli/c/enc/block_splitter.c
    brotli/c/enc/brotli_bit_stream.c
    brotli/c/enc/cluster.c
    brotli/c/enc/command.c
    brotli/c/enc/compound_dictionary.c
    brotli/c/enc/compress_fragment.c
    brotli/c/enc/compress_fragment_two_pass.c
    brotli/c/enc/dictionary_hash.c
    brotli/c/enc/encode.c
    brotli/c/enc/encoder_dict.c
    brotli/c/enc/entropy_encode.c
    brotli/c/enc/fast_log.c
    brotli/c/enc/histogram.c
    brotli/c/enc/literal_cost.c
    brotli/c/enc/memory.c
    brotli/c/enc/metablock.c
    brotli/c/enc/static_dict.c
    brotli/c/enc/static_dict_lut.c
    brotli/c/enc/static_init.c
    brotli/c/enc/utf8_util.c
)
target_include_directories(uvhttp_brotli PUBLIC brotli/c/include)
set_target_properties(uvhttp_brotli PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
