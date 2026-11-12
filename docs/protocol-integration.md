# 同步、统一端口和 h2c 使用说明

作者：Ethan；版本：1.0 初次建立；日期：2026.11.11。

## 1. HTTPS 和 HTTP 版本

`HttpClient`/`HttpServer` 的 TLS 模式复用 HTTP/1 解析与序列化；默认 HTTP/1.1。HTTP/1.0 不使用 chunked，不能把 HTTP/1.1 的流式分块上传方式直接套用。`Http2Client`/`Http2Server` 的 TLS 模式通过 ALPN `h2` 使用 HTTP/2。本次重点验证 HTTP/1.1 TLS 与 HTTP/2 TLS，HTTP/1.0 TLS 未单独做全面回归。

## 2. HTTP/2 同步完整请求

```cpp
#include <uvhttp/uvhttp.h>
uvhttp::Http2SyncClientSession client;
uvhttp::HttpRequest request;
request.method = "POST";
request.target = "/echo";
request.body = "hello";
uvhttp::Error error;
auto response = client.requestPlaintext("127.0.0.1", 8080, request, error);
if (error) {
    // 记录 error.message，不把 response 作为成功结果。
}
auto upgraded = client.requestUpgrade("127.0.0.1", 8080, request, error);
// tlsContext 为调用者配置好信任库的 SSL_CTX*，默认验证证书和主机名。
auto encrypted = client.requestTls("localhost", 8443, tlsContext, request, error);
auto stats = client.poolStats();
```

`Http2SyncClient` 的同名静态接口每次创建独立会话；希望复用连接应保留 `Http2SyncClientSession`。其 `close()` 取消全部请求并使会话不再接受请求。可在多个业务线程并发调用，但每个调用独占 Error 输出对象；析构前结束所有调用。总超时由 `Http2Client::Options::pool.requestTimeout` 配置，零禁用超时，可能一直等待。

## 3. 单端口 HTTP/1 与 HTTP/2 服务器

```cpp
uvhttp::EventLoop loop;
uvhttp::HttpServer::Options options;
options.enableHttp2 = true;
options.enableH2cUpgrade = true;
// options.tlsContext = serverTlsContext;  // 设置后成为 HTTPS 入口，明文 h2c 被禁用。
uvhttp::HttpServer::Callbacks callbacks;
callbacks.onRequest = [](const uvhttp::HttpRequest &request, uvhttp::HttpResponseWriter writer) {
    uvhttp::HttpResponse response;
    response.body = request.body;
    writer.send(std::move(response));
};
callbacks.http2.onRequest = [](const uvhttp::Http2Session::Ptr &session,
                               std::int32_t streamId, uvhttp::HttpRequest request) {
    uvhttp::HttpResponse response;
    response.body = request.body;
    session->respond(streamId, std::move(response));
};
uvhttp::HttpServer server(loop, "127.0.0.1", 8080, options, callbacks);
server.start();
loop.run();
```

启用 HTTP/2 时，服务端自动使用 ALPN 列表 `h2,http/1.1`；未协商 ALPN 的 TLS 连接走 HTTP/1。明文按最多24字节连接前言增量识别，识别超时由 `protocolDetectionTimeout` 配置，必须大于零。识别成功/断开会取消识别定时器。

HTTP/2 回调独立放在 `Callbacks::http2` 中。跨连接流式业务使用新增 `onRequestBodyWithSession(session, streamId, bytes)`，以 session 和 streamId 组合识别请求；设置后优先于旧 `onRequestBody(streamId, bytes)`，不会重复交付同一块。纯 `Http2Server` 也支持新回调。不能将 streamId 当作全局唯一值。

## 4. 同步流式通讯

```cpp
uvhttp::HttpSyncStream::Options options;
options.protocol = uvhttp::HttpSyncStream::Protocol::Http2;
options.highWaterBytes = 256 * 1024;
options.timeout = std::chrono::seconds(60);
uvhttp::HttpSyncStream stream(options);
uvhttp::HttpRequestHead request;
request.method = "POST";
request.target = "/echo";
uvhttp::Error error;
if (!stream.begin("127.0.0.1", 8080, request, error)) return;
if (!stream.write("first", error)) return;
if (!stream.write("second", error)) return;
if (!stream.endUpload(error)) return;
uvhttp::HttpResponseHead head;
if (!stream.readHead(head, error)) return;
char buffer[4096];
for (;;) {
    const auto count = stream.read(buffer, sizeof(buffer), error);
    if (error) break;
    if (!count) break;  // 正常 EOF
    consume(buffer, count);
}
```

- TLS 使用 `beginTls(host,port,context,request,error,verifyPeer=true)`，不是把 `https://` 放进 host。
- 单次 `write` 最大64 KiB，写入成功仅表示被接受；EAGAIN 表示未接受，可在请求仍有效时重试原块。必须调用 `endUpload()`，不能依靠析构表示成功完成。
- `read` 返回0时必须检查 Error，区分 EOF、取消、超时和网络失败。网络失败时不返回成功的截断结果。
- 接收缓冲达到高水位暂停专用 TCP 连接读，低于半高水位恢复。单次已进入 TLS/协议解析器的数据仍可能继续交付，所以硬限额为高水位额外加1 MiB；超过硬限额明确失败并关闭池，不无限增长。
- 请求期限包含等待用户读写的时间。`cancel()` 可由另一线程唤醒阻塞等待，取消后此实例不再使用。析构不得与公开方法调用并发。
- 同一实例只允许一个活动请求；必须消费完响应才能 `begin` 下一请求。用于串行流式业务，不提供同一同步实例的 HTTP/2 多流并发；需要多流请使用异步 Http2Client 或多个实例。
- 流式上传不走 h2c 升级握手；HTTP/2 同步流式使用 prior-knowledge 或 TLS。h2c 首次请求需要完整上传后才能切换协议，见下一节。

## 5. h2c Upgrade

异步 `Http2Client::requestUpgrade` 使用 HTTP/1.1 发送完整请求与 `Connection: Upgrade, HTTP2-Settings`、`Upgrade: h2c`、base64url SETTINGS。收到合法101后交由 nghttp2 `upgrade2` 初始化流1、发送客户端前言；后续请求在相同升级池中直接使用 HTTP/2 流。`requestStreamUpgrade` 使用同样的缓冲上传，但增量交付响应 body。

服务端须启用 `enableHttp2` 和 `enableH2cUpgrade`，并且不是 TLS listener。升级请求完整接收后发送101，把已读 body 导入HTTP/2流1；后续缓冲字节交给HTTP/2解析器，不重新当作HTTP/1报文解析。校验 Connection tokens、唯一的 HTTP2-Settings、base64url编码和已知 SETTINGS 值，删除升级请求中的逐跳头。

升级拒绝/非101/错误升级头将返回错误，**不自动重发为 HTTP/1**，避免重复执行非幂等请求。升级连接与 prior-knowledge 连接池隔离。首次升级不支持一边无限上传一边切换协议，也没有隐式把普通 `requestPlaintext` 改成 Upgrade。

## 6. 构建和交付边界

固定内置依赖在 `third_party`，CMake 与 PRI 不提供依赖路径覆盖。OpenSSL 的位置仍需外部配置。gzip/Brotli 仅内置当前压缩接口实际使用的编码器文件。测试代码及历史验证产物位于工程外，不参与交付或默认构建。Qt 示例是独立交互演示，保留，但不含 `--self-test` 测试模式。
