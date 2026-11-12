/**
 * @file        websocket.cc
 * @brief       WebSocket 帧编解码、连接操作与升级握手辅助函数的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/websocket.h"
#include "uvhttp/http.h"
#include <array>
#include <openssl/sha.h>
#include <random>
#include <utility>
#include <cctype>
#include "http_internal.h"
namespace uvhttp { namespace {
const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64(const unsigned char *p,std::size_t n){std::string o; for(std::size_t i=0;i<n;i+=3){unsigned v=p[i]<<16; if(i+1<n)v|=p[i+1]<<8; if(i+2<n)v|=p[i+2]; o+=b64[(v>>18)&63];o+=b64[(v>>12)&63];o+=i+1<n?b64[(v>>6)&63]:'=';o+=i+2<n?b64[v&63]:'=';}return o;}
}
std::string websocketAcceptKey(std::string_view key){std::string input(key);input+="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";unsigned char digest[SHA_DIGEST_LENGTH];SHA1(reinterpret_cast<const unsigned char*>(input.data()),input.size(),digest);return base64(digest,sizeof(digest));}
bool isWebSocketUpgrade(const HttpHeaders &h){auto c=h.get("Connection"),u=h.get("Upgrade"); return !c.empty()&&!u.empty()&&detail::hasConnectionToken(h,"Upgrade")&&detail::lowerAscii(u)=="websocket";}
HttpResponse websocketHandshakeResponse(const HttpRequestHead &request){HttpResponse response;response.status=101;response.reason="Switching Protocols";response.headers.set("Upgrade","websocket");response.headers.set("Connection","Upgrade");response.headers.set("Sec-WebSocket-Accept",websocketAcceptKey(request.headers.get("Sec-WebSocket-Key")));return response;}
WebSocketFrameCodec::WebSocketFrameCodec(std::size_t limit):maxPayload_(limit){}
void WebSocketFrameCodec::reset(){buffer_.clear();fragmented_=false;}
std::string WebSocketFrameCodec::encode(WebSocketOpcode opcode,std::string_view payload,bool fin,bool mask,std::uint32_t key){std::string out;out.push_back(static_cast<char>((fin?0x80:0)|static_cast<unsigned char>(opcode)));auto n=payload.size();if(n<126)out.push_back(static_cast<char>((mask?0x80:0)|n));else if(n<=0xffff){out.push_back(static_cast<char>((mask?0x80:0)|126));out.push_back(static_cast<char>(n>>8));out.push_back(static_cast<char>(n));}else{out.push_back(static_cast<char>((mask?0x80:0)|127));for(int i=7;i>=0;--i)out.push_back(static_cast<char>((n>>(i*8))&255));}if(mask){for(int i=3;i>=0;--i)out.push_back(static_cast<char>((key>>(i*8))&255));}for(std::size_t i=0;i<n;++i)out.push_back(static_cast<char>(payload[i]^(mask?static_cast<char>((key>>((3-(i%4))*8))&255):0)));return out;}
bool WebSocketFrameCodec::feed(std::string_view bytes,FrameCallback cb,ErrorCallback err){buffer_.append(bytes);auto fail=[&](std::string s){if(err)err(std::move(s));reset();return false;};for(;;){if(buffer_.size()<2)return true;auto p= reinterpret_cast<const unsigned char*>(buffer_.data());bool fin=(p[0]&0x80)!=0, masked=(p[1]&0x80)!=0;auto opcode=static_cast<WebSocketOpcode>(p[0]&0xf);auto len=p[1]&0x7f;std::size_t pos=2;if((p[0]&0x70)!=0|| (opcode>=WebSocketOpcode::Close && (!fin || len>125)))return fail("invalid websocket frame");if(len==126){if(buffer_.size()<4)return true;len=(p[2]<<8)|p[3];pos=4;}else if(len==127){if(buffer_.size()<10)return true;len=0;for(int i=0;i<8;++i)len=(len<<8)|p[2+i];pos=10;}if((opcode>=WebSocketOpcode::Close && len>125)||len>maxPayload_)return fail("invalid websocket payload length");if(masked){if(buffer_.size()<pos+4)return true;pos+=4;}if(buffer_.size()<pos+len)return true;std::array<unsigned char,4> key{};if(masked)for(int i=0;i<4;++i)key[i]=static_cast<unsigned char>(buffer_[pos-4+i]);std::string payload(buffer_.data()+pos,len);if(masked)for(std::size_t i=0;i<len;++i)payload[i]^=key[i%4];buffer_.erase(0,pos+len);if(opcode==WebSocketOpcode::Continuation&&!fragmented_)return fail("unexpected continuation");if(opcode!=WebSocketOpcode::Continuation&&fragmented_)return fail("fragment interleave");if(!fin&&opcode!=WebSocketOpcode::Continuation){fragmented_=true;fragmentOpcode_=opcode;}if(fin&&fragmented_){fragmented_=false;opcode=fragmentOpcode_;}if(cb)cb({fin,masked,opcode,std::move(payload)});}}
}
namespace uvhttp {
WebSocketConnection::WebSocketConnection(AsyncByteStream::Ptr stream,bool server,Callbacks callbacks,std::size_t maxPayload):stream_(std::move(stream)),server_(server),callbacks_(std::move(callbacks)),codec_(maxPayload){}
WebSocketConnection::Ptr WebSocketConnection::create(AsyncByteStream::Ptr stream,bool server,Callbacks callbacks,std::size_t maxPayload){return Ptr(new WebSocketConnection(std::move(stream),server,std::move(callbacks),maxPayload));}
void WebSocketConnection::setCallbacks(Callbacks callbacks){callbacks_=std::move(callbacks);}
bool WebSocketConnection::feed(std::string_view bytes){if(closed_)return false;return codec_.feed(bytes,[self=shared_from_this()](WebSocketFrame frame){if(self->server_&& !frame.masked){if(self->callbacks_.onError)self->callbacks_.onError(self,Error{UV_EPROTO,"unmasked client websocket frame"});self->close(1002,"protocol error");return;}if(!self->server_&&frame.masked){if(self->callbacks_.onError)self->callbacks_.onError(self,Error{UV_EPROTO,"masked server websocket frame"});self->close(1002,"protocol error");return;}if(frame.opcode==WebSocketOpcode::Ping){self->send(WebSocketOpcode::Pong,frame.payload);}else if(frame.opcode==WebSocketOpcode::Close){if(!self->closed_){self->send(WebSocketOpcode::Close,frame.payload);self->closed_=true;if(self->callbacks_.onClosed)self->callbacks_.onClosed(self);self->stream_->closeStream();}}if(self->callbacks_.onFrame)self->callbacks_.onFrame(self,std::move(frame));},[self=shared_from_this()](std::string message){if(self->callbacks_.onError)self->callbacks_.onError(self,Error{UV_EPROTO,std::move(message)});});}
bool WebSocketConnection::send(WebSocketOpcode opcode,std::string payload){if(closed_&&opcode!=WebSocketOpcode::Close)return false;static thread_local std::mt19937 generator(std::random_device{}());const auto key=generator();auto status=stream_->writeBytes(WebSocketFrameCodec::encode(opcode,payload,true,!server_,key));return status!=AsyncByteStream::WriteStatus::Closed;}
bool WebSocketConnection::sendText(std::string payload){return send(WebSocketOpcode::Text,std::move(payload));} bool WebSocketConnection::sendBinary(std::string payload){return send(WebSocketOpcode::Binary,std::move(payload));} bool WebSocketConnection::ping(std::string payload){return send(WebSocketOpcode::Ping,std::move(payload));}
bool WebSocketConnection::close(std::uint16_t code,std::string reason){if(closed_)return false;std::string payload;payload.push_back(static_cast<char>(code>>8));payload.push_back(static_cast<char>(code));payload+=std::move(reason);auto ok=send(WebSocketOpcode::Close,std::move(payload));closed_=true;if(callbacks_.onClosed)callbacks_.onClosed(shared_from_this());stream_->closeStream();return ok;}
bool WebSocketConnection::isServer()const noexcept{return server_;}
}

