/**
 * @file        router.cc
 * @brief       按方法和路径模式匹配的 HTTP 路由器的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/router.h"
#include <string>
#include <string_view>
#include <vector>
namespace uvhttp { namespace {
std::vector<std::string> split(std::string_view value) { std::vector<std::string> result; std::size_t begin=value.empty()||value.front()!='/'?0:1; while(begin<=value.size()){auto end=value.find('/',begin);result.emplace_back(value.substr(begin,end==std::string_view::npos?value.size()-begin:end-begin));if(end==std::string_view::npos)break;begin=end+1;}if(result.size()==1&&result.front().empty())result.clear();return result; }
}
bool Router::add(std::string method,std::string pattern,Handler handler){if(pattern.empty()||pattern.front()!='/'||!handler)return false;routes_.push_back({std::move(method),std::move(pattern),std::move(handler)});return true;}
bool Router::remove(std::string_view method,std::string_view pattern){for(auto it=routes_.begin();it!=routes_.end();++it)if(it->method==method&&it->pattern==pattern){routes_.erase(it);return true;}return false;}
bool Router::match(std::string_view method,std::string_view path,RouteMatch &result,Handler *handler)const{const auto actual=split(path.substr(0,path.find('?')));for(const auto &route:routes_){if(route.method!="*"&&route.method!=method)continue;const auto pattern=split(route.pattern);RouteMatch candidate;std::size_t i=0;bool ok=true;for(;i<pattern.size();++i){if(pattern[i]=="**"){candidate.parameters["*"]=i<actual.size()?actual[i]:"";i=pattern.size();break;}if(i>=actual.size()){ok=false;break;}if(pattern[i].size()>1&&pattern[i].front()==':')candidate.parameters[pattern[i].substr(1)]=actual[i];else if(pattern[i]!=actual[i]){ok=false;break;}}if(ok&&i==pattern.size()&&(actual.size()==pattern.size()||(!pattern.empty()&&pattern.back()=="**"))){result=std::move(candidate);if(handler)*handler=route.handler;return true;}}return false;}
bool Router::dispatch(const HttpRequest &request,HttpResponseWriter writer)const{RouteMatch m;Handler h;if(!match(request.method,request.target,m,&h))return false;h(request,m,std::move(writer));return true;}
void Router::clear(){routes_.clear();}
}
