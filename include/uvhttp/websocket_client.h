/**
 * @file        websocket_client.h
 * @brief       WebSocket 明文及 TLS 升级客户端
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_WEBSOCKET_CLIENT_H
#define UVHTTP_WEBSOCKET_CLIENT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <openssl/ssl.h>

#include "uvhttp/export.h"
#include "uvhttp/tcp_client.h"
#include "uvhttp/websocket.h"

namespace uvhttp
{

/**
 * @brief       WebSocket 明文及 TLS 升级客户端。
 */
class UVHTTP_EXPORT WebSocketClient
{
  public:
    /**
     * @brief       初始化配置；不与对象运行状态同步修改。
     */
    struct Options
    {
        /**
         * @brief       底层 TCP 配置，构造时读取。
         */
        TcpConnection::Options tcp;
        /**
         * @brief       请求或响应头字段大小上限（字节）。
         */
        std::size_t maxHeaderBytes{64 * 1024};
        /**
         * @brief       WebSocket 帧负载上限（字节）。
         */
        std::size_t maxPayloadBytes{64 * 1024 * 1024};
    };

    /**
     * @brief       事件回调集合，未设置的函数表示不接收该通知。
     */
    struct Callbacks
    {
        /**
         * @brief       连接成功通知；参数为连接共享指针，可在此安装后续回调。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const WebSocketConnection::Ptr &)> onConnected;
        /**
         * @brief       错误通知；Error 引用仅在回调期间有效，回调不得抛异常。
         * @details     在所属事件循环线程执行；引用和字节视图仅在本次回调期间有效，需延迟使用时复制。
         */
        std::function<void(const Error &)> onError;
    };

    /**
     * @brief       构造 WebSocketClient 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     */
    explicit WebSocketClient(EventLoop &loop);
    /**
     * @brief       构造 WebSocketClient 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     * @param[in] loop 所属事件循环，必须比使用它的对象及异步操作存活更久
     * @param[in] options 配置值，在构造时复制/移动，调用后修改原配置不影响对象
     */
    WebSocketClient(EventLoop &loop, Options options);
    /**
     * @brief       释放 WebSocketClient 对象及其持有资源
     * @threadsafe  否；先结束并发调用和异步操作，再在对象规定的线程销毁。
     */
    ~WebSocketClient();
    /**
     * @brief       禁止复制该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    WebSocketClient(const WebSocketClient &other) = delete;
    /**
     * @brief       禁止复制赋值该对象
     * @threadsafe  不适用，此操作已禁止。
     * @param[in] other 另一实例；此复制操作被显式禁止
     */
    WebSocketClient &operator=(const WebSocketClient &other) = delete;

    /**
     * @brief       异步建立连接
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] target 请求路径及可选查询串，通常以 / 开始
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool connect(std::string host, std::uint16_t port, std::string target,
                 Callbacks callbacks = {});
    /**
     * @brief       通过 TLS 建立 WebSocket 连接
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] host 目标主机名或 IP；TLS 校验默认使用该主机身份
     * @param[in] port 目标端口；服务端构造时 0 表示由系统选择空闲端口
     * @param[in] context 非空 OpenSSL 上下文；调用者配置证书或信任库，不与操作并发修改
     * @param[in] target 请求路径及可选查询串，通常以 / 开始
     * @param[in] callbacks 事件回调集合；网络回调在所属 loop 执行，不得抛出异常
     * @param[in] verifyPeer 是否校验证书链与主机身份，默认 true，生产环境不要禁用
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     */
    bool connectTls(std::string host, std::uint16_t port, SSL_CTX *context, std::string target,
                    Callbacks callbacks = {}, bool verifyPeer = true);

  private:
    EventLoop &loop_;
    Options options_;
    TcpClient tcpClient_;
};

} // namespace uvhttp

#endif
