/**
 * @file        filter.cc
 * @brief       HTTP 过滤器链及同步调用栈内的后续处理接口的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/filter.h"
#include <memory>
namespace uvhttp { void FilterChain::add(Filter filter){if(filter)filters_.push_back(std::move(filter));} void FilterChain::clear(){filters_.clear();} void FilterChain::run(const HttpRequest &request,HttpResponseWriter writer,Next terminal)const{auto requestCopy=std::make_shared<HttpRequest>(request); std::function<void(std::size_t)> invoke;invoke=[this,requestCopy,writer,terminal,&invoke](std::size_t i){if(i==filters_.size()){if(terminal)terminal();return;}filters_[i](*requestCopy,writer,[&,i]{invoke(i+1);});};invoke(0);} }
