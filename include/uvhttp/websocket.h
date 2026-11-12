/**
 * @file        websocket.h
 * @brief       WebSocket 帧编解码、连接操作与升级握手辅助函数
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_WEBSOCKET_H
#define UVHTTP_WEBSOCKET_H
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "uvhttp/export.h"
#include "uvhttp/http.h"
#include "uvhttp/async_byte_stream.h"
namespace uvhttp
{
/**
 * @brief       WebSocket 帧操作码。
 */
enum class WebSocketOpcode : unsigned char
{
    /**
     * @brief       消息后续分片。
     */
    Continuation = 0x0,
    /**
     * @brief       文本数据帧。
     */
    Text = 0x1,
    /**
     * @brief       二进制数据帧。
     */
    Binary = 0x2,
    /**
     * @brief       关闭控制帧。
     */
    Close = 0x8,
    /**
     * @brief       Ping 控制帧。
     */
    Ping = 0x9,
    /**
     * @brief       Pong 控制帧。
     */
    Pong = 0xA
};
/**
 * @brief       解析后的单个 WebSocket 帧数据，不是自动拼接的完整消息。
 */
struct UVHTTP_EXPORT WebSocketFrame
{
    /**
     * @brief       是否为消息的最后一帧。
     */
    bool fin{true};
    /**
     * @brief       帧是否具有掩码。
     */
    bool masked{false};
    /**
     * @brief       WebSocket 操作码。
     */
    WebSocketOpcode opcode{WebSocketOpcode::Text};
    /**
     * @brief       帧有效负载的原始字节。
     */
    std::string payload;
};
/**
 * @brief       有状态的 WebSocket 增量帧编解码器。
 */
class UVHTTP_EXPORT WebSocketFrameCodec
{
  public:
    /**
     * @brief       收到解析帧时同步调用的函数，帧按值交付。
     */
    using FrameCallback = std::function<void(WebSocketFrame)>;
    /**
     * @brief       错误回调；TCP 使用连接和 Error，帧解析器使用错误字符串。
     */
    using ErrorCallback = std::function<void(std::string)>;
    /**
     * @brief       构造 WebSocketFrameCodec 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] maxPayload 单帧有效负载限制，单位字节
     */
    explicit WebSocketFrameCodec(std::size_t maxPayload = 64 * 1024 * 1024);
    /**
     * @brief       增量输入 WebSocket 帧字节
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] bytes 本次输入字节；string_view 只在调用期间有效，底层按接口语义消费/复制
     * @param[in] onFrame 解析到帧时同步调用的回调
     * @param[in] onError 解析错误时同步调用的可选回调
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool feed(std::string_view bytes, FrameCallback onFrame, ErrorCallback onError = {});
    /**
     * @brief       将 WebSocket 帧编码成网络字节
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] opcode WebSocket 帧类型
     * @param[in] payload 帧的有效负载；控制帧不得超过协议限制
     * @param[in] fin 是否为当前消息的最后一帧
     * @param[in] masked 是否对帧有效负载施加掩码
     * @param[in] maskKey 32 位掩码值，仅 masked=true 时使用
     * @return      独立持有的数据字符串。
     */
    static std::string encode(WebSocketOpcode opcode, std::string_view payload, bool fin = true,
                              bool masked = false, std::uint32_t maskKey = 0);
    /**
     * @brief       重置状态以处理下一条数据流
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      无。
     */
    void reset();

  private:
    std::string buffer_;
    std::size_t maxPayload_;
    bool fragmented_{false};
    WebSocketOpcode fragmentOpcode_{WebSocketOpcode::Continuation};
};
/**
 * @brief       已升级字节流上的 WebSocket 连接。
 */
class UVHTTP_EXPORT WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection>
{
  public:
    /**
     * @brief       共享所有权指针；不得以裸指针跨异步生命周期使用对象。
     */
    using Ptr = std::shared_ptr<WebSocketConnection>;
    /**
     * @brief       事件回调集合，未设置的函数表示不接收该通知。
     */
    struct Callbacks
    {
        /**
         * @brief       收到 WebSocket 帧；参数为连接和帧，帧回调不等同于完整消息回调。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &, WebSocketFrame)> onFrame;
        /**
         * @brief       关闭通知；对象仍须存活到相关关闭回调完成。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &)> onClosed;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Ptr &, const Error &)> onError;
    };
    /**
     * @brief       从升级后的字节流创建 WebSocket 连接
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] stream 底层字节流；协议会话持有其共享所有权
     * @param[in] server true 表示服务端角色，false 表示客户端角色
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] maxPayload 单帧有效负载限制，单位字节
     * @return      创建/持有的对象指针；工厂初始化失败或可选后端未启用时可能为空。
     */
    static Ptr create(AsyncByteStream::Ptr stream, bool server, Callbacks callbacks = {},
                      std::size_t maxPayload = 64 * 1024 * 1024);
    /**
     * @brief       增量输入 WebSocket 帧字节
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] bytes 本次输入字节；string_view 只在调用期间有效，底层按接口语义消费/复制
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool feed(std::string_view bytes);
    /**
     * @brief       替换 WebSocket 连接的事件回调
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @return      回调集合，捕获当前应用对象，应用须活到 server 停止。
     */
    void setCallbacks(Callbacks callbacks);
    /**
     * @brief       发送指定类型的 WebSocket 帧
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] opcode WebSocket 帧类型
     * @param[in] payload 帧的有效负载；控制帧不得超过协议限制
     * @return      当前实现只区分底层 Closed；Backpressured 也可能返回 true，不能作为可靠送达或完整背压确认。
     */
    bool send(WebSocketOpcode opcode, std::string payload);
    /**
     * @brief       发送文本帧
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] payload 帧的有效负载；控制帧不得超过协议限制
     * @return      当前实现只区分底层 Closed；Backpressured 也可能返回 true，不能作为可靠送达或完整背压确认。
     */
    bool sendText(std::string payload);
    /**
     * @brief       发送二进制帧
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] payload 帧的有效负载；控制帧不得超过协议限制
     * @return      当前实现只区分底层 Closed；Backpressured 也可能返回 true，不能作为可靠送达或完整背压确认。
     */
    bool sendBinary(std::string payload);
    /**
     * @brief       发送 Ping 控制帧
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] payload 帧的有效负载；控制帧不得超过协议限制
     * @return      当前实现只区分底层 Closed；Backpressured 也可能返回 true，不能作为可靠送达或完整背压确认。
     */
    bool ping(std::string payload = {});
    /**
     * @brief       请求关闭连接或客户端
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] code WebSocket 关闭码，默认 1000
     * @param[in] reason WebSocket 关闭原因，应为合法 UTF-8 且满足控制帧长度限制
     * @return      当前实现只区分底层 Closed；Backpressured 也可能返回 true，不能作为可靠送达或完整背压确认。
     */
    bool close(std::uint16_t code = 1000, std::string reason = {});
    /**
     * @brief       查询是否为服务端 WebSocket 连接
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      true 表示所查询条件成立，false 表示不成立。
     */
    bool isServer() const noexcept;

  private:
    WebSocketConnection(AsyncByteStream::Ptr, bool, Callbacks, std::size_t);
    AsyncByteStream::Ptr stream_;
    bool server_;
    Callbacks callbacks_;
    WebSocketFrameCodec codec_;
    bool closed_{false};
};
/**
 * @brief       根据客户端握手键计算 Sec-WebSocket-Accept
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @param[in] key 客户端 Sec-WebSocket-Key 字段值
 * @return      计算得到的握手接受键字符串。
 */
UVHTTP_EXPORT std::string websocketAcceptKey(std::string_view key);
/**
 * @brief       判断请求头是否声明 WebSocket 升级
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @param[in] headers 待检查的 HTTP 请求头
 * @return      头字段包含所需 Upgrade/Connection 标记时为 true，不代表完整握手已经校验。
 */
UVHTTP_EXPORT bool isWebSocketUpgrade(const HttpHeaders &headers);
/**
 * @brief       生成 WebSocket 升级响应
 * @threadsafe  是；输入对象调用期间保持稳定，输出对象由调用方独占。
 * @param[in] request 请求头及可选缓冲 body；按值参数可被移动到内部操作
 * @return      响应值；同步请求须先检查输出 Error；握手辅助函数仅生成报文。
 */
UVHTTP_EXPORT HttpResponse websocketHandshakeResponse(const HttpRequestHead &request);
} // namespace uvhttp
#endif
