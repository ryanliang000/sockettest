#include "leveldb/db.h"
#include "log.h"
#include <arpa/inet.h>
leveldb::DB* gs_host_db = nullptr;
const int gs_expire_time = 86400 * 3;
bool try_init_db(){
  if (gs_host_db == nullptr){
     leveldb::Options opt;
     opt.create_if_missing = true;
     leveldb::Status st;
     if (!(st=leveldb::DB::Open(opt, "./hostdb", &gs_host_db)).ok()){
        LOG_E("open hostdb failed! %s", st.ToString().c_str());
        return false;
     }
     LOG_R("host db inited");
  }
  return true;
}
bool s_init_db = try_init_db();
struct value_addr
{
  struct in_addr addr;
  time_t expire;
  value_addr(){}
  value_addr(const struct in_addr& inaddr):addr(inaddr){
    expire = time(NULL) + gs_expire_time + rand()%86400;
  }
};
bool query_host(const char* hostname, int len, struct in_addr& addr){
  if (!gs_host_db || !hostname) return false;
  leveldb::Slice key(hostname, len);
  std::string value;
  leveldb::ReadOptions opt;
  if (!gs_host_db->Get(opt, key, &value).ok()){
     LOG_R("query key<%s> not found", key.ToString().c_str());
     return false;
  }
  if (value.length() < sizeof(value_addr)){
     LOG_R("query key<%s> value length error<%d>", key.ToString().c_str(), value.length());
     return false;
  }
  value_addr v;
  memcpy((char*)&v, value.c_str(), sizeof(v));
  if (v.expire < time(NULL)){
    LOG_R("query key<%s> expired", key.ToString().c_str());
    return false;
  }
  addr = v.addr;
  return true;
}

bool add_host(const char* hostname, int len, const struct in_addr& addr){
  if (!gs_host_db || !hostname) return false;
  leveldb::Slice key(hostname, len);
  value_addr v(addr);
  leveldb::Slice value((char*)&v, sizeof(v));
  leveldb::WriteOptions opt;
  if (!gs_host_db->Put(opt, key, value).ok()){
    LOG_E("add host key<%s> failed", key.ToString().c_str());
    return false;
  }
  // check expire time
  return true;
}
