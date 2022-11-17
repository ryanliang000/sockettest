#include "leveldb/db.h"
#include "log.h"
#include <arpa/inet.h>
#include <string>
#include <map>
namespace leveldb{
  class MemDB : public DB {
  public:
      MemDB(){};
      virtual ~MemDB(){};
      virtual Status Put(const WriteOptions& options, const Slice& key, const Slice& value) override{
        map_[key.ToString()] = value.ToString();
        return Status();
      };
      virtual Status Delete(const WriteOptions& options, const Slice& key) override{return Status();}
      virtual Status Write(const WriteOptions& options, WriteBatch* updates) override{return Status();}
      virtual Status Get(const ReadOptions& options, const Slice& key, std::string* value) override{
        if (map_.find(key.ToString()) == map_.end()){
          return Status::NotFound("not found");
        }
        *value = map_[key.ToString()];
        return Status();
      }
      virtual Iterator* NewIterator(const ReadOptions& options) override{return nullptr;}
      virtual const Snapshot* GetSnapshot() {return nullptr;}
      virtual void ReleaseSnapshot(const Snapshot* snapshot){return;}
      virtual bool GetProperty(const Slice& property, std::string* value) {return true;}
      virtual void GetApproximateSizes(const Range* range, int n, uint64_t* sizes) {return;}
      virtual void CompactRange(const Slice* begin, const Slice* end) {return;}
      virtual void SuspendCompactions(){return;}
      virtual void ResumeCompactions(){return;}
  private:
      std::map<std::string, std::string> map_;   
  };
};
leveldb::DB* gs_host_db = nullptr;
const int gs_expire_time = 86400 * 3;
bool try_init_db(){
  if (gs_host_db == nullptr){
     leveldb::Options opt;
     opt.create_if_missing = true;
     leveldb::Status st;
     if (!(st=leveldb::DB::Open(opt, "./hostdb", &gs_host_db)).ok()){
        LOG_W("open hostdb failed! %s", st.ToString().c_str());
        LOG_W("switch mem db mode");
        gs_host_db = new leveldb::MemDB;
        return true;
     }
     LOG_R("host db inited");
  }
  return true;
}
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
  if (!gs_host_db) try_init_db();
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
