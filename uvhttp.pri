# @file    uvhttp.pri
# @brief   qmake 源码集成入口，添加公开头文件、实现及第三方库源码。
# @author  Ethan
# @version 1.0 初次建立
# @date    2026.11.11
# 使用方式：include(path/to/uvhttp/uvhttp.pri)
# 默认以本 PRI 所在目录为工程根目录，依赖源码固定内置于 third_party，不支持覆盖路径。

UVHTTP_ROOT = $$PWD
isEmpty(UVHTTP_OPENSSL_ROOT): UVHTTP_OPENSSL_ROOT = $$(OPENSSL_ROOT_DIR)
isEmpty(UVHTTP_OPENSSL_ROOT) {
    win32: error("Set UVHTTP_OPENSSL_ROOT to the OpenSSL installation prefix")
    !win32: LIBS += -lssl -lcrypto
}
!isEmpty(UVHTTP_OPENSSL_ROOT) {
    INCLUDEPATH += $$UVHTTP_OPENSSL_ROOT/include
    LIBS += -L$$UVHTTP_OPENSSL_ROOT/lib64 -L$$UVHTTP_OPENSSL_ROOT/lib -lssl -lcrypto
}

!exists($$PWD/third_party/libuv/include/uv.h): error("libuv source tree not found: $$PWD/third_party/libuv")
!exists($$PWD/third_party/nghttp2/lib/includes/nghttp2/nghttp2.h): error("nghttp2 source tree not found: $$PWD/third_party/nghttp2")


# 指定公开头文件搜索路径，使用 #include <uvhttp/uvhttp.h>。
INCLUDEPATH += $$UVHTTP_ROOT/include
# 指定供上层工程使用的主入口头文件；FUNHEADERPATH 为集成约定变量。
FUNHEADERPATH += $$UVHTTP_ROOT/include/uvhttp/uvhttp.h
# 声明工程头文件，不将声明集中复制到主入口文件。
HEADERS += \
    $$UVHTTP_ROOT/include/uvhttp/uvhttp.h \
    $$UVHTTP_ROOT/include/uvhttp/export.h \
    $$UVHTTP_ROOT/include/uvhttp/error.h \
    $$UVHTTP_ROOT/include/uvhttp/buffer.h \
    $$UVHTTP_ROOT/include/uvhttp/event_loop.h \
    $$UVHTTP_ROOT/include/uvhttp/event_loop_thread.h \
    $$UVHTTP_ROOT/include/uvhttp/tcp_connection.h \
    $$UVHTTP_ROOT/include/uvhttp/tcp_server.h \
    $$UVHTTP_ROOT/include/uvhttp/tcp_client.h \
    $$UVHTTP_ROOT/include/uvhttp/http.h \
    $$UVHTTP_ROOT/include/uvhttp/http_server.h \
    $$UVHTTP_ROOT/include/uvhttp/http_client.h \
    $$UVHTTP_ROOT/include/uvhttp/connection_pool.h \
    $$UVHTTP_ROOT/src/client_pool.h \
    $$UVHTTP_ROOT/include/uvhttp/http_sync_client.h \
    $$UVHTTP_ROOT/include/uvhttp/http_sync_stream.h \
    $$UVHTTP_ROOT/include/uvhttp/tls_stream.h \
    $$UVHTTP_ROOT/include/uvhttp/async_byte_stream.h \
    $$UVHTTP_ROOT/include/uvhttp/http2_session.h \
    $$UVHTTP_ROOT/include/uvhttp/http2_server.h \
    $$UVHTTP_ROOT/include/uvhttp/http2_client.h \
    $$UVHTTP_ROOT/include/uvhttp/http2_sync_client.h \
    $$UVHTTP_ROOT/include/uvhttp/router.h \
    $$UVHTTP_ROOT/include/uvhttp/filter.h \
    $$UVHTTP_ROOT/include/uvhttp/cookie.h \
    $$UVHTTP_ROOT/include/uvhttp/session.h \
    $$UVHTTP_ROOT/include/uvhttp/websocket.h \
    $$UVHTTP_ROOT/include/uvhttp/websocket_client.h \
    $$UVHTTP_ROOT/include/uvhttp/compression.h \
    $$UVHTTP_ROOT/include/uvhttp/application.h \
    $$UVHTTP_ROOT/include/uvhttp/detail/mpsc_queue.h \
    $$UVHTTP_ROOT/src/http_internal.h \
    $$UVHTTP_ROOT/src/http1_parser.h
contains(CONFIG, uvhttp_shared): DEFINES += UVHTTP_SHARED UVHTTP_BUILDING_LIBRARY

# libuv common sources and headers.
INCLUDEPATH += $$PWD/third_party/libuv/include $$PWD/third_party/libuv/src
SOURCES += \
    $$PWD/third_party/libuv/src/fs-poll.c \
    $$PWD/third_party/libuv/src/idna.c \
    $$PWD/third_party/libuv/src/inet.c \
    $$PWD/third_party/libuv/src/random.c \
    $$PWD/third_party/libuv/src/strscpy.c \
    $$PWD/third_party/libuv/src/strtok.c \
    $$PWD/third_party/libuv/src/thread-common.c \
    $$PWD/third_party/libuv/src/threadpool.c \
    $$PWD/third_party/libuv/src/timer.c \
    $$PWD/third_party/libuv/src/uv-common.c \
    $$PWD/third_party/libuv/src/uv-data-getter-setters.c \
    $$PWD/third_party/libuv/src/version.c

win32 {
    DEFINES += WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 _CRT_DECLARE_NONSTDC_NAMES=0
    SOURCES += \
        $$PWD/third_party/libuv/src/win/async.c \
        $$PWD/third_party/libuv/src/win/core.c \
        $$PWD/third_party/libuv/src/win/detect-wakeup.c \
        $$PWD/third_party/libuv/src/win/dl.c \
        $$PWD/third_party/libuv/src/win/error.c \
        $$PWD/third_party/libuv/src/win/fs.c \
        $$PWD/third_party/libuv/src/win/fs-event.c \
        $$PWD/third_party/libuv/src/win/getaddrinfo.c \
        $$PWD/third_party/libuv/src/win/getnameinfo.c \
        $$PWD/third_party/libuv/src/win/handle.c \
        $$PWD/third_party/libuv/src/win/loop-watcher.c \
        $$PWD/third_party/libuv/src/win/pipe.c \
        $$PWD/third_party/libuv/src/win/thread.c \
        $$PWD/third_party/libuv/src/win/poll.c \
        $$PWD/third_party/libuv/src/win/process.c \
        $$PWD/third_party/libuv/src/win/process-stdio.c \
        $$PWD/third_party/libuv/src/win/signal.c \
        $$PWD/third_party/libuv/src/win/snprintf.c \
        $$PWD/third_party/libuv/src/win/stream.c \
        $$PWD/third_party/libuv/src/win/tcp.c \
        $$PWD/third_party/libuv/src/win/tty.c \
        $$PWD/third_party/libuv/src/win/udp.c \
        $$PWD/third_party/libuv/src/win/util.c \
        $$PWD/third_party/libuv/src/win/winapi.c \
        $$PWD/third_party/libuv/src/win/winsock.c
    LIBS += -lpsapi -luser32 -ladvapi32 -liphlpapi -luserenv -lws2_32 -ldbghelp -lole32 -lshell32 -lsynchronization
}

linux {
    DEFINES += _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _GNU_SOURCE _POSIX_C_SOURCE=200112
    SOURCES += \
        $$PWD/third_party/libuv/src/unix/async.c \
        $$PWD/third_party/libuv/src/unix/core.c \
        $$PWD/third_party/libuv/src/unix/dl.c \
        $$PWD/third_party/libuv/src/unix/fs.c \
        $$PWD/third_party/libuv/src/unix/getaddrinfo.c \
        $$PWD/third_party/libuv/src/unix/getnameinfo.c \
        $$PWD/third_party/libuv/src/unix/loop-watcher.c \
        $$PWD/third_party/libuv/src/unix/loop.c \
        $$PWD/third_party/libuv/src/unix/pipe.c \
        $$PWD/third_party/libuv/src/unix/poll.c \
        $$PWD/third_party/libuv/src/unix/process.c \
        $$PWD/third_party/libuv/src/unix/random-devurandom.c \
        $$PWD/third_party/libuv/src/unix/signal.c \
        $$PWD/third_party/libuv/src/unix/stream.c \
        $$PWD/third_party/libuv/src/unix/tcp.c \
        $$PWD/third_party/libuv/src/unix/thread.c \
        $$PWD/third_party/libuv/src/unix/tty.c \
        $$PWD/third_party/libuv/src/unix/udp.c \
        $$PWD/third_party/libuv/src/unix/proctitle.c \
        $$PWD/third_party/libuv/src/unix/linux.c \
        $$PWD/third_party/libuv/src/unix/procfs-exepath.c \
        $$PWD/third_party/libuv/src/unix/random-getrandom.c \
        $$PWD/third_party/libuv/src/unix/random-sysctl-linux.c
    LIBS += -lpthread -ldl -lrt -lm
}

macx {
    DEFINES += _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _DARWIN_UNLIMITED_SELECT=1 _DARWIN_USE_64_BIT_INODE=1
    SOURCES += \
        $$PWD/third_party/libuv/src/unix/async.c \
        $$PWD/third_party/libuv/src/unix/core.c \
        $$PWD/third_party/libuv/src/unix/dl.c \
        $$PWD/third_party/libuv/src/unix/fs.c \
        $$PWD/third_party/libuv/src/unix/getaddrinfo.c \
        $$PWD/third_party/libuv/src/unix/getnameinfo.c \
        $$PWD/third_party/libuv/src/unix/loop-watcher.c \
        $$PWD/third_party/libuv/src/unix/loop.c \
        $$PWD/third_party/libuv/src/unix/pipe.c \
        $$PWD/third_party/libuv/src/unix/poll.c \
        $$PWD/third_party/libuv/src/unix/process.c \
        $$PWD/third_party/libuv/src/unix/random-devurandom.c \
        $$PWD/third_party/libuv/src/unix/signal.c \
        $$PWD/third_party/libuv/src/unix/stream.c \
        $$PWD/third_party/libuv/src/unix/tcp.c \
        $$PWD/third_party/libuv/src/unix/thread.c \
        $$PWD/third_party/libuv/src/unix/tty.c \
        $$PWD/third_party/libuv/src/unix/udp.c \
        $$PWD/third_party/libuv/src/unix/proctitle.c \
        $$PWD/third_party/libuv/src/unix/bsd-ifaddrs.c \
        $$PWD/third_party/libuv/src/unix/kqueue.c \
        $$PWD/third_party/libuv/src/unix/random-getentropy.c \
        $$PWD/third_party/libuv/src/unix/darwin-proctitle.c \
        $$PWD/third_party/libuv/src/unix/darwin.c \
        $$PWD/third_party/libuv/src/unix/fsevents.c
    LIBS += -lpthread -lm
}

freebsd {
    DEFINES += _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE
    SOURCES += \
        $$PWD/third_party/libuv/src/unix/async.c \
        $$PWD/third_party/libuv/src/unix/core.c \
        $$PWD/third_party/libuv/src/unix/dl.c \
        $$PWD/third_party/libuv/src/unix/fs.c \
        $$PWD/third_party/libuv/src/unix/getaddrinfo.c \
        $$PWD/third_party/libuv/src/unix/getnameinfo.c \
        $$PWD/third_party/libuv/src/unix/loop-watcher.c \
        $$PWD/third_party/libuv/src/unix/loop.c \
        $$PWD/third_party/libuv/src/unix/pipe.c \
        $$PWD/third_party/libuv/src/unix/poll.c \
        $$PWD/third_party/libuv/src/unix/process.c \
        $$PWD/third_party/libuv/src/unix/random-devurandom.c \
        $$PWD/third_party/libuv/src/unix/signal.c \
        $$PWD/third_party/libuv/src/unix/stream.c \
        $$PWD/third_party/libuv/src/unix/tcp.c \
        $$PWD/third_party/libuv/src/unix/thread.c \
        $$PWD/third_party/libuv/src/unix/tty.c \
        $$PWD/third_party/libuv/src/unix/udp.c \
        $$PWD/third_party/libuv/src/unix/freebsd.c \
        $$PWD/third_party/libuv/src/unix/posix-hrtime.c \
        $$PWD/third_party/libuv/src/unix/bsd-proctitle.c \
        $$PWD/third_party/libuv/src/unix/bsd-ifaddrs.c \
        $$PWD/third_party/libuv/src/unix/kqueue.c \
        $$PWD/third_party/libuv/src/unix/random-getrandom.c
    LIBS += -lpthread -lm
}

openbsd {
    DEFINES += _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE
    SOURCES += \
        $$PWD/third_party/libuv/src/unix/async.c \
        $$PWD/third_party/libuv/src/unix/core.c \
        $$PWD/third_party/libuv/src/unix/dl.c \
        $$PWD/third_party/libuv/src/unix/fs.c \
        $$PWD/third_party/libuv/src/unix/getaddrinfo.c \
        $$PWD/third_party/libuv/src/unix/getnameinfo.c \
        $$PWD/third_party/libuv/src/unix/loop-watcher.c \
        $$PWD/third_party/libuv/src/unix/loop.c \
        $$PWD/third_party/libuv/src/unix/pipe.c \
        $$PWD/third_party/libuv/src/unix/poll.c \
        $$PWD/third_party/libuv/src/unix/process.c \
        $$PWD/third_party/libuv/src/unix/random-devurandom.c \
        $$PWD/third_party/libuv/src/unix/signal.c \
        $$PWD/third_party/libuv/src/unix/stream.c \
        $$PWD/third_party/libuv/src/unix/tcp.c \
        $$PWD/third_party/libuv/src/unix/thread.c \
        $$PWD/third_party/libuv/src/unix/tty.c \
        $$PWD/third_party/libuv/src/unix/udp.c \
        $$PWD/third_party/libuv/src/unix/posix-hrtime.c \
        $$PWD/third_party/libuv/src/unix/bsd-proctitle.c \
        $$PWD/third_party/libuv/src/unix/bsd-ifaddrs.c \
        $$PWD/third_party/libuv/src/unix/kqueue.c \
        $$PWD/third_party/libuv/src/unix/random-getentropy.c \
        $$PWD/third_party/libuv/src/unix/openbsd.c
    LIBS += -lpthread -lm
}

haiku {
    DEFINES += _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE _BSD_SOURCE
    SOURCES += \
        $$PWD/third_party/libuv/src/unix/async.c \
        $$PWD/third_party/libuv/src/unix/core.c \
        $$PWD/third_party/libuv/src/unix/dl.c \
        $$PWD/third_party/libuv/src/unix/fs.c \
        $$PWD/third_party/libuv/src/unix/getaddrinfo.c \
        $$PWD/third_party/libuv/src/unix/getnameinfo.c \
        $$PWD/third_party/libuv/src/unix/loop-watcher.c \
        $$PWD/third_party/libuv/src/unix/loop.c \
        $$PWD/third_party/libuv/src/unix/pipe.c \
        $$PWD/third_party/libuv/src/unix/poll.c \
        $$PWD/third_party/libuv/src/unix/process.c \
        $$PWD/third_party/libuv/src/unix/random-devurandom.c \
        $$PWD/third_party/libuv/src/unix/signal.c \
        $$PWD/third_party/libuv/src/unix/stream.c \
        $$PWD/third_party/libuv/src/unix/tcp.c \
        $$PWD/third_party/libuv/src/unix/thread.c \
        $$PWD/third_party/libuv/src/unix/tty.c \
        $$PWD/third_party/libuv/src/unix/udp.c \
        $$PWD/third_party/libuv/src/unix/haiku.c \
        $$PWD/third_party/libuv/src/unix/bsd-ifaddrs.c \
        $$PWD/third_party/libuv/src/unix/no-fsevents.c \
        $$PWD/third_party/libuv/src/unix/no-proctitle.c \
        $$PWD/third_party/libuv/src/unix/posix-hrtime.c \
        $$PWD/third_party/libuv/src/unix/posix-poll.c
    LIBS += -lpthread -lnetwork
}

!win32:!linux:!macx:!freebsd:!openbsd:!haiku: error("Unsupported qmake OS for bundled libuv source integration")

# nghttp2 library sources (same library-only source set as its CMake target).
INCLUDEPATH += \
    $$PWD/third_party/nghttp2/lib \
    $$PWD/third_party/nghttp2/lib/includes \
    $$UVHTTP_ROOT/qmake_compat
DEFINES += NGHTTP2_STATICLIB BUILDING_NGHTTP2 UVHTTP_WITH_HTTP2=1

# Bundled compression sources (library directories only; no examples/tests).
contains(CONFIG, uvhttp_with_zlib) {
    DEFINES += UVHTTP_WITH_ZLIB
    INCLUDEPATH += $$PWD/third_party/zlib
    SOURCES += $$files($$PWD/third_party/zlib/*.c)
}
contains(CONFIG, uvhttp_with_brotli) {
    DEFINES += UVHTTP_WITH_BROTLI
    INCLUDEPATH += $$PWD/third_party/brotli/c/include
    SOURCES += $$files($$PWD/third_party/brotli/c/common/*.c, true)
    SOURCES += $$files($$PWD/third_party/brotli/c/enc/*.c, true)
}
SOURCES += \
    $$PWD/third_party/nghttp2/lib/nghttp2_pq.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_map.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_queue.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_frame.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_buf.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_stream.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_outbound_item.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_session.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_submit.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_helper.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_alpn.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_hd.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_hd_huffman.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_hd_huffman_data.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_version.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_priority_spec.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_option.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_callbacks.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_mem.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_http.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_rcbuf.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_extpri.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_ratelim.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_time.c \
    $$PWD/third_party/nghttp2/lib/nghttp2_debug.c \
    $$PWD/third_party/nghttp2/lib/sfparse.c


SOURCES += \
    $$UVHTTP_ROOT/src/buffer.cc \
    $$UVHTTP_ROOT/src/event_loop.cc \
    $$UVHTTP_ROOT/src/event_loop_thread.cc \
    $$UVHTTP_ROOT/src/tcp_connection.cc \
    $$UVHTTP_ROOT/src/tcp_client.cc \
    $$UVHTTP_ROOT/src/tcp_server.cc \
    $$UVHTTP_ROOT/src/http.cc \
    $$UVHTTP_ROOT/src/http_server.cc \
    $$UVHTTP_ROOT/src/http_client.cc \
    $$UVHTTP_ROOT/src/client_pool.cc \
    $$UVHTTP_ROOT/src/http_sync_client.cc \
    $$UVHTTP_ROOT/src/http_sync_stream.cc \
    $$UVHTTP_ROOT/src/tls_stream.cc \
    $$UVHTTP_ROOT/src/http2_session.cc \
    $$UVHTTP_ROOT/src/http2_server.cc \
    $$UVHTTP_ROOT/src/http2_client.cc \
    $$UVHTTP_ROOT/src/http2_sync_client.cc \
    $$UVHTTP_ROOT/src/router.cc \
    $$UVHTTP_ROOT/src/filter.cc \
    $$UVHTTP_ROOT/src/cookie.cc \
    $$UVHTTP_ROOT/src/session.cc \
    $$UVHTTP_ROOT/src/websocket.cc \
    $$UVHTTP_ROOT/src/websocket_client.cc \
    $$UVHTTP_ROOT/src/compression.cc \
    $$UVHTTP_ROOT/src/application.cc
