# 本轮验证记录

日期：2026.11.11。环境：Windows、MinGW GCC 12.2、Qt 5.15.12、外部 OpenSSL 3.0.20。

## 构建

- 固定内置依赖 CMake Release 构建通过，启用 HTTP/2、gzip 和 Brotli。
- 将交付文件独立复制到一个不含同级第三方库的目录，CMake 构建通过。
- 独立复制目录下的 Qt 客户端、服务器 `.pro` 构建通过，均启用内置 gzip/Brotli。
- 关闭 HTTP/2、gzip、Brotli 的最小 CMake 配置构建通过。
- 编译输出存在 Qt 头文件的 MinGW dllimport 属性警告；不是零警告构建。

## 通讯验证

外部验证工程共16个可执行程序全部返回0，其中15个为原有回归程序，1个为新增统一协议验证：

- 原有 HTTP/1 语义、同步、TLS 缓冲/流式、连接池限额/超时、HTTP/2 缓冲/流式上传/下载、GOAWAY 和 WebSocket/框架层回归。
- 同一明文端口同时接收 HTTP/1 与 HTTP/2，同一 TLS 端口按 ALPN 分流。
- HTTP/2 同步 Session 的明文/TLS连接复用。
- h2c 首次升级请求对应流1，后续请求在升级连接上复用；同步升级、HEAD 升级与流式响应。
- 四种协议组合的同步流式上传/读取、顺序复用、2 MiB响应慢读取的暂停/恢复、超时和取消唤醒。
- session身份明确的流式 body 回调，同时运行升级连接与 prior-knowledge 连接。
- 非法/重复 HTTP2-Settings 拒绝；逐字节 HTTP/1 请求和 HTTP/2前言；未完成前言超时；升级报文与前言合并输入；TLS未协商ALPN时回落HTTP/1。

主动关闭/取消场景会产生 EOF、operation canceled 等服务端诊断日志；断言和进程退出状态均通过。

## Qt调试程序补充验证

- 当前源码的客户端、服务端 `.pro` 均构建通过，启用内置gzip/Brotli并链接外部OpenSSL。
- 独立验证程序直接调用窗口使用的 `DebugEngine` 和 `SyncRunner`，81个请求全部通过：七种协议选择下可用的异步缓冲、自动/手动流式、同步缓冲和同步流式组合，重复POST以及HEAD。逐项检查状态码、响应字节内容和累计字节数。
- 明文与TLS分别连接统一listener，使用本地测试CA验证TLS；同步请求执行期间GUI事件循环心跳正常。
- 界面已进行离屏启动与截图检查；上述81项是通信桥接层验证，不是逐个点击控件的GUI自动化。错误预设、导出、完整压力与所有异常组合尚未自动化穷举。

## 未验证与边界

- 未在 Linux/macOS/FreeBSD/OpenBSD/Haiku、ARM 或 MSVC 上实机运行，不能据此宣称全平台通过。
- 非完整 HTTP/2 RFC 一致性、模糊测试、安全审计或长期高并发压测。
- 同步流式会话强制单请求并发；h2c首次上传是完整缓冲请求，不提供无限流式升级上传。
- Qt调试窗口已增加HTTP/1.0、HTTP/1.1、HTTPS、明文/TLS HTTP/2、h2c，以及同步缓冲/同步流式入口；服务端支持统一listener。窗口提供断言和批量场景，但不代表完整RFC一致性测试。

测试源码、证书、构建产物和详细输出均在交付目录外的 `.uvhttp-validation`，不属于 uvhttp 默认构建或交付源文件。
