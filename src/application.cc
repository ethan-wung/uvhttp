/**
 * @file        application.cc
 * @brief       HTTP 应用入口，组合路由、过滤器与会话存储的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/application.h"
namespace uvhttp {
HttpServer::Callbacks HttpApplication::callbacks() { HttpServer::Callbacks cb; cb.onRequest=[this](const HttpRequest &request,HttpResponseWriter writer){ auto requestCopy=std::make_shared<HttpRequest>(request); filters_.run(*requestCopy,writer,[this,requestCopy,writer]{ if(!dispatch(*requestCopy,writer)){ HttpResponse response; response.status=404; writer.send(std::move(response)); } }); }; return cb; }
bool HttpApplication::dispatch(const HttpRequest &request,HttpResponseWriter writer) const { return router_.dispatch(request,std::move(writer)); }
}
