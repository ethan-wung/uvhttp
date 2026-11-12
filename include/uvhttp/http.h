/**
 * @file        http.h
 * @brief       HTTP 请求、响应、头字段以及流式读写器
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_HTTP_H
#define UVHTTP_HTTP_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "uvhttp/export.h"
#include "uvhttp/async_byte_stream.h"

namespace uvhttp
{

class TcpConnection;
class HttpServer;
class HttpClient;
namespace detail
{
struct HttpResponseWriterState;
struct HttpRequestBodyWriterState;
} // namespace detail

/**
 * @brief       HTTP 头字段集合，保留重复字段和插入顺序。
 */
class UVHTTP_EXPORT HttpHeaders
{
  public:
    /**
     * @brief       单个头字段的名称和值。
     */
    using Field = std::pair<std::string, std::string>;
    /**
     * @brief       保留插入顺序且允许同名字段的容器。
     */
    using Container = std::vector<Field>;

    /**
     * @brief       追加头字段，保留已有同名字段
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] name 字段名称，HTTP 头字段匹配不区分大小写
     * @param[in] value 待存入或追加的值
     * @return      无。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    void add(std::string name, std::string value);
    /**
     * @brief       替换全部同名头字段
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] name 字段名称，HTTP 头字段匹配不区分大小写
     * @param[in] value 待存入或追加的值
     * @return      无。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    void set(std::string name, std::string value);
    /**
     * @brief       移除条目
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] name 字段名称，HTTP 头字段匹配不区分大小写
     * @return      至少移除一个字段时为 true，否则为 false。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    bool remove(std::string_view name);
    /**
     * @brief       判断头字段是否存在
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] name 字段名称，HTTP 头字段匹配不区分大小写
     * @return      true 表示所查询条件成立，false 表示不成立。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    bool contains(std::string_view name) const noexcept;
    /**
     * @brief       取得第一个同名头字段的值
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] name 字段名称，HTTP 头字段匹配不区分大小写
     * @return      第一个同名字段的借用视图；不存在返回空视图，修改集合后可能失效。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    std::string_view get(std::string_view name) const noexcept;
    /**
     * @brief       获取全部同名 HTTP 头字段值
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @param[in] name 字段名称，HTTP 头字段匹配不区分大小写
     * @return      结果集合；string_view 元素仍借用原始对象存储。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    std::vector<std::string_view> getAll(std::string_view name) const;
    /**
     * @brief       获取头字段容器的只读引用
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      对象内部组件或容器引用，不能超过所属对象的生命周期。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    const Container &fields() const noexcept;
    /**
     * @brief       判断是否为空
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      true 表示所查询条件成立，false 表示不成立。
     * @details     字段名称匹配不区分大小写；此容器不替代网络序列化阶段的头字段合法性校验。
     */
    bool empty() const noexcept;

  private:
    Container fields_;
};

/**
 * @brief       HTTP 请求起始信息与头字段，不包含 body。
 */
struct HttpRequestHead
{
    /**
     * @brief       HTTP 方法字符串，默认 GET。
     */
    std::string method{"GET"};
    /**
     * @brief       请求路径及查询串，默认 /。
     */
    std::string target{"/"};
    /**
     * @brief       HTTP 报文版本文本；HTTP/2 实际帧格式由 nghttp2 管理。
     */
    std::string version{"HTTP/1.1"};
    /**
     * @brief       HTTP 头字段集合。
     */
    HttpHeaders headers;
};

/**
 * @brief       完整缓冲请求，在请求头基础上持有 body。
 */
struct HttpRequest : HttpRequestHead
{
    /**
     * @brief       缓冲 body 的原始字节，不自动进行 JSON 解析或字符编码转换。
     */
    std::string body;
};

/**
 * @brief       HTTP 响应起始信息与头字段，不包含 body。
 */
struct HttpResponseHead
{
    /**
     * @brief       HTTP 报文版本文本；HTTP/2 实际帧格式由 nghttp2 管理。
     */
    std::string version{"HTTP/1.1"};
    /**
     * @brief       响应状态码，默认 200。
     */
    int status{200};
    /**
     * @brief       HTTP/1 响应原因短语，空时按状态码生成。
     */
    std::string reason;
    /**
     * @brief       HTTP 头字段集合。
     */
    HttpHeaders headers;
};

/**
 * @brief       完整缓冲响应，在响应头基础上持有 body。
 */
struct HttpResponse : HttpResponseHead
{
    /**
     * @brief       缓冲 body 的原始字节，不自动进行 JSON 解析或字符编码转换。
     */
    std::string body;
};

/**
 * @brief       HTTP/1 响应写入器，支持缓冲和分块发送，副本共享写入状态。
 */
class UVHTTP_EXPORT HttpResponseWriter
{
  public:
    /**
     * @brief       构造 HttpResponseWriter 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     */
    HttpResponseWriter() = default;

    /**
     * @brief       检查对象是否具有有效状态
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @return      是否持有 writer 状态，不代表连接仍可写。
     */
    explicit operator bool() const noexcept;
    /**
     * @brief       发送完整 HTTP 响应或中间响应
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @param[in] response 响应状态、头字段及可选缓冲 body
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     返回 false 时不得认为数据已发送；高层 writer 尚无公开可写恢复通知。一次 write 的块大小不等于一次对端读取。 HEAD/204/304 不发送 body；非101的 1xx 不结束 writer，后续仍须发送最终响应。
     */
    bool send(HttpResponse response) const;
    /**
     * @brief       开始 HTTP/1 分块响应
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @param[in] response 响应状态、头字段及可选缓冲 body
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     返回 false 时不得认为数据已发送；高层 writer 尚无公开可写恢复通知。一次 write 的块大小不等于一次对端读取。 只接受最终响应；HEAD/204/304 抑制实体字节，仍应调用 end 完成 writer 生命周期。
     */
    bool beginChunked(HttpResponseHead response) const;
    /**
     * @brief       提交一块流式数据
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @param[in] chunk 本次 body 数据块；不代表 TCP 包或对端回调的边界
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     返回 false 时不得认为数据已发送；高层 writer 尚无公开可写恢复通知。一次 write 的块大小不等于一次对端读取。
     */
    bool write(std::string chunk) const;
    /**
     * @brief       结束分块 body
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     返回 false 时不得认为数据已发送；高层 writer 尚无公开可写恢复通知。一次 write 的块大小不等于一次对端读取。
     */
    bool end() const;

  private:
    explicit HttpResponseWriter(std::shared_ptr<detail::HttpResponseWriterState> state);
    static HttpResponseWriter fromStream(AsyncByteStream::Ptr stream, bool headRequest = false,
                                         bool keepAlive = false,
                                         std::function<void()> onFinished = {});
    std::shared_ptr<detail::HttpResponseWriterState> state_;

    friend class HttpServer;
};

/**
 * @brief       HTTP/1 分块请求 body 写入器，副本共享结束状态。
 */
class UVHTTP_EXPORT HttpRequestBodyWriter
{
  public:
    /**
     * @brief       构造 HttpRequestBodyWriter 对象
     * @threadsafe  否；构造期间不得向其他线程发布未完成构造的对象。
     */
    HttpRequestBodyWriter() = default;

    /**
     * @brief       检查对象是否具有有效状态
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @return      是否持有 writer 状态，不代表连接仍可写。
     */
    explicit operator bool() const noexcept;
    /**
     * @brief       提交一块流式数据
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @param[in] chunk 本次 body 数据块；不代表 TCP 包或对端回调的边界
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     返回 false 时不得认为数据已发送；高层 writer 尚无公开可写恢复通知。一次 write 的块大小不等于一次对端读取。
     */
    bool write(std::string chunk) const;
    /**
     * @brief       结束分块 body
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @return      true 表示操作成功或提交已接受；false 表示拒绝、状态不允许或失败，具体见接口说明。
     * @details     返回 false 时不得认为数据已发送；高层 writer 尚无公开可写恢复通知。一次 write 的块大小不等于一次对端读取。
     */
    bool end() const;
    /**
     * @brief       从字节流创建分块请求 body 写入器
     * @threadsafe  有限；writer 状态有互斥保护，但底层流的线程约束仍须遵守，不能与失效生命周期竞争。
     * @param[in] stream 底层字节流；协议会话持有其共享所有权
     * @return      共享底层流的请求 body writer；调用者必须传入非空且有效的流。
     * @details     低层适配会在 end 后调用底层 shutdownWrite；池化请求应使用 uploadReady 交付的 writer。
     */
    static HttpRequestBodyWriter fromStream(AsyncByteStream::Ptr stream);

  private:
    explicit HttpRequestBodyWriter(std::shared_ptr<detail::HttpRequestBodyWriterState> state);
    std::shared_ptr<detail::HttpRequestBodyWriterState> state_;

    friend class HttpClient;
};

} // namespace uvhttp

#endif

