/**
 * @file        compression.cc
 * @brief       增量压缩接口及 identity、gzip、Brotli 后端工厂的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/compression.h"
#include <cstring>
#if defined(UVHTTP_WITH_ZLIB)
#include <zlib.h>
#endif
#if defined(UVHTTP_WITH_BROTLI)
#include <brotli/encode.h>
#endif
namespace uvhttp { namespace {
class Identity final : public CompressionCodec { public: std::string_view name() const noexcept override { return "identity"; } bool compress(std::string_view input,std::string &output,bool) override { output.append(input); return true; } bool reset() override { return true; } };
#if defined(UVHTTP_WITH_ZLIB)
class Gzip final : public CompressionCodec { z_stream stream_{}; bool initialized_{false}; public: Gzip(){reset();} ~Gzip(){if(initialized_)deflateEnd(&stream_);} std::string_view name()const noexcept override{return "gzip";} bool reset()override{if(initialized_)deflateEnd(&stream_);std::memset(&stream_,0,sizeof(stream_));initialized_=deflateInit2(&stream_,Z_DEFAULT_COMPRESSION,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY)==Z_OK;return initialized_;} bool compress(std::string_view input,std::string &output,bool finish)override{if(!initialized_)return false;stream_.next_in=(Bytef*)input.data();stream_.avail_in=(uInt)input.size();char buffer[16384];int flush=finish?Z_FINISH:Z_NO_FLUSH;do{stream_.next_out=(Bytef*)buffer;stream_.avail_out=sizeof(buffer);int rc=deflate(&stream_,flush);if(rc!=Z_OK&&rc!=Z_STREAM_END)return false;output.append(buffer,sizeof(buffer)-stream_.avail_out);if(rc==Z_STREAM_END){reset();break;}}while(stream_.avail_in||stream_.avail_out==0);return true;} };
#endif
#if defined(UVHTTP_WITH_BROTLI)
class Brotli final : public CompressionCodec { BrotliEncoderState *state_{nullptr}; public: Brotli(){reset();} ~Brotli(){if(state_)BrotliEncoderDestroyInstance(state_);} std::string_view name()const noexcept override{return "br";} bool reset()override{if(state_)BrotliEncoderDestroyInstance(state_);state_=BrotliEncoderCreateInstance(nullptr,nullptr,nullptr);return state_!=nullptr;} bool compress(std::string_view input,std::string &output,bool finish)override{if(!state_)return false;const uint8_t *nextIn=(const uint8_t*)input.data();size_t availIn=input.size();uint8_t buffer[16384];auto op=finish?BROTLI_OPERATION_FINISH:BROTLI_OPERATION_PROCESS;do{uint8_t *nextOut=buffer;size_t availOut=sizeof(buffer);if(!BrotliEncoderCompressStream(state_,op,&availIn,&nextIn,&availOut,&nextOut,nullptr))return false;output.append((char*)buffer,sizeof(buffer)-availOut);if(finish&&BrotliEncoderIsFinished(state_)){reset();break;}}while(availIn||BrotliEncoderHasMoreOutput(state_));return true;} };
#endif
}
std::unique_ptr<CompressionCodec> createIdentityCodec(){return std::make_unique<Identity>();}
std::unique_ptr<CompressionCodec> createGzipCodec(){
#if defined(UVHTTP_WITH_ZLIB)
return std::make_unique<Gzip>();
#else
return {};
#endif
}
std::unique_ptr<CompressionCodec> createBrotliCodec(){
#if defined(UVHTTP_WITH_BROTLI)
return std::make_unique<Brotli>();
#else
return {};
#endif
}
}
