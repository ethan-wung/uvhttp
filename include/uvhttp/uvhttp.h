/**
 * @file        uvhttp.h
 * @brief       uvhttp 统一公开入口，汇总 C++17 网络、HTTP 及应用层接口
 * @details     包含本文件即可访问公开模块；UVHTTP_WITH_HTTP2 控制 HTTP/2 声明。
 *              当前提供 C++17 API，尚无独立 C++20 协程适配器。
 * @author      Ethan
 * @version     1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_UVHTTP_H
#define UVHTTP_UVHTTP_H

#include "uvhttp/export.h"
#include "uvhttp/async_byte_stream.h"
#include "uvhttp/buffer.h"
#include "uvhttp/error.h"
#include "uvhttp/event_loop.h"
#include "uvhttp/event_loop_thread.h"
#include "uvhttp/tcp_connection.h"
#include "uvhttp/tcp_client.h"
#include "uvhttp/tcp_server.h"
#include "uvhttp/http.h"
#include "uvhttp/http_server.h"
#include "uvhttp/http_client.h"
#include "uvhttp/http_sync_client.h"
#include "uvhttp/http_sync_stream.h"
#include "uvhttp/tls_stream.h"
#ifdef UVHTTP_WITH_HTTP2
#include "uvhttp/http2_session.h"
#include "uvhttp/http2_client.h"
#include "uvhttp/http2_sync_client.h"
#include "uvhttp/http2_server.h"
#endif
#include "uvhttp/router.h"
#include "uvhttp/filter.h"
#include "uvhttp/cookie.h"
#include "uvhttp/session.h"
#include "uvhttp/websocket.h"
#include "uvhttp/websocket_client.h"
#include "uvhttp/compression.h"
#include "uvhttp/application.h"

#endif
