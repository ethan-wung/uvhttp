/**
 * @file        session.cc
 * @brief       带有效期和读写锁的内存会话键值存储的实现。
 * @author      Ethan
 * @version 1.0 初次建立
 * @date        2026.11.11
 */

#include "uvhttp/session.h"
#include <atomic>
#include <mutex>
namespace uvhttp { namespace { std::string newId() { static std::atomic_uint64_t seq{1}; return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(seq.fetch_add(1)); } }
SessionStore::SessionStore(std::chrono::seconds ttl):ttl_(ttl){}
std::string SessionStore::create(Values values) { auto key=newId(); std::unique_lock lock(mutex_); sessions_[key]={std::move(values),std::chrono::steady_clock::now()+ttl_}; return key; }
bool SessionStore::get(const std::string &key, Values &values) const { std::shared_lock lock(mutex_); auto it=sessions_.find(key); if(it==sessions_.end()||it->second.expires<std::chrono::steady_clock::now()) return false; values=it->second.values; return true; }
bool SessionStore::set(const std::string &key, Values values) { std::unique_lock lock(mutex_); auto it=sessions_.find(key); if(it==sessions_.end()) return false; it->second={std::move(values),std::chrono::steady_clock::now()+ttl_}; return true; }
bool SessionStore::erase(const std::string &key) { std::unique_lock lock(mutex_); return sessions_.erase(key)!=0; }
void SessionStore::purge() { std::unique_lock lock(mutex_); auto now=std::chrono::steady_clock::now(); for(auto it=sessions_.begin();it!=sessions_.end();) if(it->second.expires<now) it=sessions_.erase(it); else ++it; }
}
