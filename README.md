# uvhttp

C++17、libuv 非阻塞事件循环的 HTTP/1.0、HTTP/1.1 和 HTTP/2 客户端/服务器。支持异步缓冲/流式请求、连接池、OpenSSL TLS、HTTP/2 同步包装、同步流式读写及统一协议入口。HTTPS 指 HTTP over TLS，不是独立 HTTP 版本。

## 依赖与构建

只复制本目录即可获得所需的 libuv、nghttp2、gzip/Brotli 编码器源代码，不依赖同级目录，也不接受第三方源码路径覆盖。仅保留参与库构建的源文件、头文件和许可证，清单见 `third_party/README.md`。OpenSSL 必须由外部提供。没有测试目标、测试源码或构建时下载步骤。

### CMake

```powershell
cmake -S uvhttp -B build -G "MinGW Makefiles" -DOPENSSL_ROOT_DIR=E:/openssl-3.0.20-mingw64 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

应用链接 `uvhttp::uvhttp`。可关闭 `UVHTTP_WITH_HTTP2`、`UVHTTP_WITH_ZLIB`、`UVHTTP_WITH_BROTLI`，或者启用 `UVHTTP_BUILD_SHARED`。不要使用旧构建缓存；已移除依赖路径覆盖项和 `UVHTTP_BUILD_TESTS`。OpenSSL 的安装路径配置仍保留，因为它是外部库。

### qmake / Qt

```qmake
CONFIG += c++17
UVHTTP_OPENSSL_ROOT = E:/openssl-3.0.20-mingw64
include(path/to/uvhttp/uvhttp.pri)
```

PRI 直接编译内置 libuv/nghttp2 与 uvhttp 源码，不需要预编译它们。gzip/Brotli 编码器可通过 `CONFIG += uvhttp_with_zlib uvhttp_with_brotli` 启用。`FUNHEADERPATH` 指向 `include/uvhttp/uvhttp.h`。同一最终目标只 include 一次 PRI。

本仓库仅提供库源码及接口文档，不包含 examples、测试程序或本机构建产物。

## 客户端接口

- `HttpClient`：HTTP/1 明文/TLS，异步完整请求或双向流式，按 origin 池化。
- `HttpSyncClient`：一次性 HTTP/1 同步请求；`HttpSyncClientSession`：跨调用复用连接。
- `Http2Client`：HTTP/2 TLS/ALPN、明文 prior-knowledge、缓冲/流式请求和连接池。
- `Http2Client::requestUpgrade` / `requestStreamUpgrade`：HTTP/1.1 h2c Upgrade 后的 HTTP/2 通讯；首次升级采用完整缓冲上传，后者流式读取响应。不降级 HTTP/1，升级池与 prior-knowledge 池隔离。
- `Http2SyncClient` / `Http2SyncClientSession`：HTTP/2 完整请求同步包装，支持 `requestPlaintext`、`requestTls`、`requestUpgrade`；Session 复用池，允许多调用线程并发，不能并发析构。
- `HttpSyncStream`：HTTP/1.1 或 HTTP/2 同步流式上传/读取，同一实例串行复用连接。具有有界接收缓冲、读暂停/恢复、总超时和可跨线程取消。一个实例一次只运行一个请求，避免暂停连接读影响其他 HTTP/2 流。

同步接口不能在 GUI 线程或请求处理所依赖的事件循环里调用。同步并不意味着底层改用阻塞 socket，网络仍由独立 libuv 线程处理。

## 服务端接口

`HttpServer` 默认仅 HTTP/1，原有程序无需修改。配置 `enableHttp2=true` 可让一个 listener 同时接收 HTTP/1 和 HTTP/2：TLS 入口由 ALPN 选择 `h2`/`http/1.1`；明文入口增量识别 HTTP/2 连接前言；`enableH2cUpgrade=true` 另允许明文 HTTP/1.1 升级。

HTTP/1 仍使用 `Callbacks::onRequest` / 流式回调与 `HttpResponseWriter`，HTTP/2 使用 `Callbacks::http2` 和 `Http2Session` 回复。不会把 HTTP/2 请求错误传入 HTTP/1 writer。一个 listener 是 TLS 或明文二选一，不混合明文与加密流量。

`Http2Server` 保留为纯 HTTP/2 listener。服务端始终异步事件驱动，没有新增阻塞 accept API。

新增接口示例及行为边界见 `docs/protocol-integration.md`。全部细节以 `include/uvhttp` 中公开声明为准。没有 HTTP/3。

## 验证范围

本次在 Windows/MinGW 使用外置验证工程检查 CMake/qmake、统一明文与 TLS 端口、HTTP/2 同步池复用、h2c 流1及后续复用、同步流式读写/背压/取消/超时，另运行旧功能回归。验证代码和日志不属于本目录的交付源码。其他平台未在本机实测，不能将保留的平台构建分支视为跨平台测试通过。
