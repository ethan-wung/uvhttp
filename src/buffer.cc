/**
 * @file        buffer.cc
 * @brief       可紧凑整理的字节缓冲区，用于增量协议解析的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/buffer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace uvhttp
{

Buffer::Buffer(std::size_t initialCapacity) : storage_(initialCapacity)
{
}

const char *Buffer::readableData() const noexcept
{
    return storage_.data() + readIndex_;
}

std::size_t Buffer::readableBytes() const noexcept
{
    return writeIndex_ - readIndex_;
}

std::size_t Buffer::writableBytes() const noexcept
{
    return storage_.size() - writeIndex_;
}

void Buffer::append(const void *data, std::size_t length)
{
    if (length == 0)
    {
        return;
    }
    if (data == nullptr)
    {
        throw std::invalid_argument("Buffer::append received a null data pointer");
    }
    ensureWritable(length);
    std::memcpy(storage_.data() + writeIndex_, data, length);
    writeIndex_ += length;
}

void Buffer::append(const std::string &data)
{
    append(data.data(), data.size());
}

void Buffer::retrieve(std::size_t length)
{
    if (length > readableBytes())
    {
        throw std::out_of_range("Buffer::retrieve length exceeds readable bytes");
    }
    if (length == readableBytes())
    {
        retrieveAll();
    }
    else
    {
        readIndex_ += length;
    }
}

void Buffer::retrieveAll() noexcept
{
    readIndex_ = 0;
    writeIndex_ = 0;
}

std::string Buffer::retrieveAsString(std::size_t length)
{
    if (length > readableBytes())
    {
        throw std::out_of_range("Buffer::retrieveAsString length exceeds readable bytes");
    }
    std::string result(readableData(), length);
    retrieve(length);
    return result;
}

std::string Buffer::retrieveAllAsString()
{
    return retrieveAsString(readableBytes());
}

void Buffer::ensureWritable(std::size_t length)
{
    if (writableBytes() >= length)
    {
        return;
    }
    const std::size_t readable = readableBytes();
    if (readIndex_ + writableBytes() >= length)
    {
        std::memmove(storage_.data(), storage_.data() + readIndex_, readable);
        readIndex_ = 0;
        writeIndex_ = readable;
        return;
    }
    if (length > storage_.max_size() - readable)
    {
        throw std::length_error("Buffer capacity overflow");
    }
    std::size_t capacity = std::max(storage_.size() * 2, readable + length);
    storage_.resize(capacity);
    if (readIndex_ != 0)
    {
        std::memmove(storage_.data(), storage_.data() + readIndex_, readable);
        readIndex_ = 0;
        writeIndex_ = readable;
    }
}

}  // namespace uvhttp
