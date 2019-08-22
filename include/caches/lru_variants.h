#ifndef LRU_VARIANTS_H
#define LRU_VARIANTS_H

#include <unordered_map>
#include <unordered_set>
#include <list>
#include <random>
#include "cache.h"
#include "adaptsize_const.h" /* AdaptSize constants */
#include <fstream>

typedef std::list<uint64_t >::iterator ListIteratorType;
typedef std::unordered_map<uint64_t , ListIteratorType> lruCacheMapType;


using namespace std;
/*
  LRU: Least Recently Used eviction
*/
class LRUCache : public Cache
{
protected:
    // list for recency order
    std::list<uint64_t > _cacheList;
    // map to find objects in list
    lruCacheMapType _cacheMap;
    unordered_map<uint64_t , uint64_t > _size_map;
    uint32_t current_t;
    unordered_map<uint64_t, uint32_t> future_timestamps;
    vector<uint8_t> eviction_qualities;
    vector<uint16_t> eviction_logic_timestamps;
    unordered_map<uint64_t, uint32_t> last_timestamps;
    vector<uint8_t> hits;
    vector<uint16_t> hit_timestamps;
    uint64_t byte_million_req;
    string task_id;


    void init_with_params(map<string, string> params) override {
        //set params
        for (auto &it: params) {
            if (it.first == "byte_million_req") {
                byte_million_req = stoull(it.second);
            } else if (it.first == "task_id") {
                task_id = it.second;
            } else {
                cerr << "unrecognized parameter: " << it.first << endl;
            }
        }
    }


    void update_stat(std::map<std::string, std::string> &res) override {
        //log eviction qualities. The value is too big to store in mongodb
        string webcachesim_trace_dir = getenv("WEBCACHESIM_TRACE_DIR");
        {
            ofstream outfile(webcachesim_trace_dir + "/" + task_id + ".evictions");
            if (!outfile) {
                cerr << "Exception opening file" << endl;
                abort();
            }
            for (auto &b: eviction_qualities)
                outfile << b;
            outfile.close();
        }
        {
            ofstream outfile(webcachesim_trace_dir + "/" + task_id + ".eviction_timestamps");
            if (!outfile) {
                cerr << "Exception opening file" << endl;
                abort();
            }
            for (auto &b: eviction_logic_timestamps)
                outfile.write((char *) &b, sizeof(uint16_t));
            outfile.close();
        }
        {
            ofstream outfile(webcachesim_trace_dir + "/" + task_id + ".hits");
            if (!outfile) {
                cerr << "Exception opening file" << endl;
                abort();
            }
            for (auto &b: hits)
                outfile << b;
            outfile.close();
        }
        {
            ofstream outfile(webcachesim_trace_dir + "/" + task_id + ".hit_timestamps");
            if (!outfile) {
                cerr << "Exception opening file" << endl;
                abort();
            }
            for (auto &b: hit_timestamps)
                outfile.write((char *) &b, sizeof(uint16_t));
            outfile.close();
        }
    }

    virtual void hit(lruCacheMapType::const_iterator it, uint64_t size);

public:
    LRUCache()
        : Cache()
    {
    }
    virtual ~LRUCache()
    {
    }

    virtual bool lookup(SimpleRequest& req);
    virtual void admit(SimpleRequest& req);
    virtual void evict(SimpleRequest& req);
    virtual void evict();
    virtual const SimpleRequest & evict_return();
};

static Factory<LRUCache> factoryLRU("LRU");

class InfCache : public Cache
{
protected:
    // map to find objects in list
    unordered_set<uint64_t> _cacheMap;

public:
    InfCache()
            : Cache()
    {
    }
    virtual ~InfCache()
    {
    }

    virtual bool lookup(SimpleRequest& req);
    virtual void admit(SimpleRequest& req);
    virtual void evict(SimpleRequest& req){};
    virtual void evict(){};
    size_t memory_overhead() override {
        //the estimation of unordered_setq not very accurate
        size_t count = 0;
        for (unsigned i = 0; i < _cacheMap.bucket_count(); ++i) {
            size_t bucket_size = _cacheMap.bucket_size(i);
            if (bucket_size == 0) {
                count++;
            }
            else {
                count += bucket_size;
            }
        }
        return count*sizeof(uint64_t) + Cache::memory_overhead();
    }
};

static Factory<InfCache> factoryInf("Inf");


/*
  FIFO: First-In First-Out eviction
*/
class FIFOCache : public LRUCache
{
protected:
    virtual void hit(lruCacheMapType::const_iterator it, uint64_t size);

public:
    FIFOCache()
        : LRUCache()
    {
    }
    virtual ~FIFOCache()
    {
    }
};

static Factory<FIFOCache> factoryFIFO("FIFO");

///*
//  FilterCache (admit only after N requests)
//*/
//class FilterCache : public LRUCache
//{
//protected:
//    uint64_t _nParam;
//    std::unordered_map<CacheObject, uint64_t> _filter;
//
//public:
//    FilterCache();
//    virtual ~FilterCache()
//    {
//    }
//
//    virtual void setPar(std::string parName, std::string parValue);
//    virtual bool lookup(SimpleRequest& req);
//    virtual void admit(SimpleRequest& req);
//};
//
//static Factory<FilterCache> factoryFilter("Filter");

/*
  AdaptSize: ExpLRU with automatic adaption of the _cParam
*/
class AdaptSizeCache : public LRUCache
{
public:
    AdaptSizeCache();
    virtual ~AdaptSizeCache()
    {
    }

    virtual void setPar(std::string parName, std::string parValue);
    virtual bool lookup(SimpleRequest&);
    virtual void admit(SimpleRequest&);

private:
    double _cParam; //
    uint64_t statSize;
    uint64_t _maxIterations;
    uint64_t _reconfiguration_interval;
    uint64_t _nextReconfiguration;
    double _gss_v;  // golden section search book parameters
    // for random number generation
    std::uniform_real_distribution<double> _uniform_real_distribution =
            std::uniform_real_distribution<double>(0.0, 1.0);

    struct ObjInfo {
        double requestCount; // requestRate in adaptsize_stub.h
        uint64_t objSize;

        ObjInfo() : requestCount(0.0), objSize(0) { }
    };
    std::unordered_map<uint64_t , ObjInfo> _longTermMetadata;
    std::unordered_map<uint64_t , ObjInfo> _intervalMetadata;

    void reconfigure();
    double modelHitRate(double c);

    // align data for vectorization
    std::vector<double> _alignedReqCount;
    std::vector<double> _alignedObjSize;
    std::vector<double> _alignedAdmProb;
};

static Factory<AdaptSizeCache> factoryAdaptSize("AdaptSize");


/*
  S4LRU
  enter at segment 0
  if hit on segment i, segment i+1
  if evicted on segment i, segment i-1
*/
class S4LRUCache : public Cache
{
protected:
    LRUCache segments[4];

public:
    S4LRUCache()
            : Cache()
    {
        segments[0] = LRUCache();
        segments[1] = LRUCache();
        segments[2] = LRUCache();
        segments[3] = LRUCache();
    }
    virtual ~S4LRUCache()
    {
    }

    virtual void setSize(uint64_t cs);
    virtual bool lookup(SimpleRequest& req);
    virtual void admit(SimpleRequest& req);
    virtual void segment_admit(uint8_t idx, SimpleRequest& req);
    virtual void evict(SimpleRequest& req);
    virtual void evict();
};

static Factory<S4LRUCache> factoryS4LRU("S4LRU");


class ThS4LRUCache : public S4LRUCache
{
protected:
    uint64_t _sizeThreshold;

public:
    void init_with_params(map<string, string> params) override {
        //set params
        for (auto& it: params) {
            if (it.first == "t") {
                _sizeThreshold = stoul(it.second);
            } else {
                cerr << "unrecognized parameter: " << it.first << endl;
            }
        }
    }
    virtual void admit(SimpleRequest& req);
};

static Factory<ThS4LRUCache> factoryThS4LRU("ThS4LRU");






/*
  ThLRU: LRU eviction with a size admission threshold
*/
class ThLRUCache : public LRUCache
{
protected:
    uint64_t _sizeThreshold;

public:
    ThLRUCache();
    virtual ~ThLRUCache()
    {
    }
    void init_with_params(map<string, string> params) override {
        //set params
        for (auto& it: params) {
            if (it.first == "t") {
                _sizeThreshold = stoul(it.second);
            } else {
                cerr << "unrecognized parameter: " << it.first << endl;
            }
        }
    }
    virtual void admit(SimpleRequest& req);
};

static Factory<ThLRUCache> factoryThLRU("ThLRU");

///*
//  ExpLRU: LRU eviction with size-aware probabilistic cache admission
//*/
//class ExpLRUCache : public LRUCache
//{
//protected:
//    double _cParam;
//
//public:
//    ExpLRUCache();
//    virtual ~ExpLRUCache()
//    {
//    }
//
//    virtual void setPar(std::string parName, std::string parValue);
//    virtual void admit(SimpleRequest* req);
//};
//
//static Factory<ExpLRUCache> factoryExpLRU("ExpLRU");

#endif
