# 内置依赖清单

本目录仅收录库运行时源文件、被包含的头文件、许可证及本工程的构建清单，不收录上游 test/example/benchmark/tool/doc 和生成器。

- libuv 1.53.0：Windows、Linux、macOS、FreeBSD、OpenBSD、Haiku 所需 C 源码及递归头文件。不同平台由构建系统选择，不将所有平台实现一起编译。
- nghttp2 1.70.0：HTTP/2 库核心；版本头固定在 `../qmake_compat/nghttp2/nghttp2ver.h`，无运行时下载或生成器依赖。
- zlib：仅 gzip 压缩所需 adler32、crc32、deflate、trees、zutil 及头文件，不提供文件工具或完整解压库目标。
- Brotli：仅编码器、common 及所需头文件，不含解码器和命令行工具。

各依赖保留自身 LICENSE / COPYING。CMake 与 qmake 固定引用本目录；不支持依赖路径覆盖、系统 libuv/nghttp2 查找或者联网下载。

OpenSSL 不在本目录内置：由使用者安装与编译器 ABI 匹配的 OpenSSL 开发包和运行库。仓库不包含 Qt 调试程序。

本机验证平台为 Windows/MinGW；保留其他列出平台的源码和配置不等于已在这些平台实机验证。
