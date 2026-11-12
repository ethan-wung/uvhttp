/**
 * @file        error.h
 * @brief       统一错误结果及 libuv 错误转换
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#ifndef UVHTTP_ERROR_H
#define UVHTTP_ERROR_H

#include <string>
#include <uv.h>

namespace uvhttp
{

/**
 * @brief       统一错误结果及 libuv 错误转换。
 */
struct Error
{
    /**
     * @brief       错误码，0 表示成功，非零表示失败。
     */
    int code{0};
    /**
     * @brief       供日志使用的错误说明。
     */
    std::string message;

    /**
     * @brief       判断结果是否表示错误
     * @threadsafe  否；同一对象需串行使用或由调用者同步，不得与修改/析构并发。
     * @return      code 非零时返回 true，表示失败。
     */
    explicit operator bool() const noexcept
    {
        return code != 0;
    }

    /**
     * @brief       将 libuv 状态码转换为统一错误结果
     * @threadsafe  是；静态转换函数不依赖实例状态。
     * @param[in] status libuv 返回码，0 表示成功，非零转换为错误
     * @return      统一错误结果；输入为 0 时返回成功结果。
     */
    static Error fromUv(int status)
    {
        return status == 0 ? Error{} : Error{status, uv_strerror(status)};
    }
};

} // namespace uvhttp

#endif
