#include <ostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#ifndef SWIG
extern "C" {
#include "workgen_func.h"
}
#endif

namespace workgen {

struct Context;
struct Thread;
struct Transaction;

#ifndef SWIG
// There is one of these per Thread object.
// It is not exposed to Python.
struct ThreadEnvironment {
    int _errno;
    Thread *_thread;
    Context *_context;
    workgen_random_state *_rand_state;

    enum UsageFlags { USAGE_READ = 0x1, USAGE_WRITE = 0x2, USAGE_MIXED = 0x4 };
    std::map<uint32_t, uint32_t> _table_usage;     // key is _recno_index
    WT_CURSOR **_cursors;                          // indexed by _recno_index

    ThreadEnvironment();
    ~ThreadEnvironment();

    int create(WT_SESSION *session);
    int open(WT_SESSION *session);
    int close();
    static int cross_check(std::vector<ThreadEnvironment> &envs);
};

// A exception generated by the workgen classes.  Methods generally return an
// int errno, so this is useful primarily for notifying the caller about
// failures in constructors.
struct WorkgenException {
    std::string _str;
    WorkgenException() : _str() {}
    WorkgenException(int err, const char *msg = NULL) : _str() {
	if (err != 0)
	    _str += wiredtiger_strerror(err);
	if (msg != NULL) {
	    if (!_str.empty())
		_str += ": ";
	    _str += msg;
	}
    }
    WorkgenException(const WorkgenException &other) : _str(other._str) {}
    ~WorkgenException() {}
};
#endif

// A Context tracks the current record number for each uri, used
// for key generation.
//
struct Context {
    bool _verbose;
#ifndef SWIG
    std::map<std::string, uint32_t> _recno_index;  // per uri
    std::map<uint32_t, std::string> _table_names;  // reverse mapping
    uint64_t *_recno;                              // one per table
    uint32_t _recno_length;
    uint32_t _recno_next;
    // unique id per context, to work with multiple contexts, starts at 1.
    uint32_t _context_count;
#endif
    Context();
    ~Context();
    void describe(std::ostream &os) const {
	os << "Context: verbose " << (_verbose ? "true" : "false");
    }
    int create_all();
};

struct TableStats {
    uint64_t inserts;
    uint64_t reads;
    uint64_t failed_reads;
    uint64_t removes;
    uint64_t updates;
    uint64_t truncates;
    TableStats() : inserts(0), reads(0), failed_reads(0), removes(0),
	updates(0), truncates(0) {}
    void add(const TableStats&);
    void subtract(const TableStats&);
    void clear();
    void describe(std::ostream &os) const;
    void report(std::ostream &os) const;
    void final_report(std::ostream &os, timespec &totalsecs) const;
};

struct Table {
    std::string _tablename;
    TableStats stats;
#ifndef SWIG
    uint32_t _recno_index;
    uint32_t _context_count;
#endif

    /* XXX select table from range */

    Table();
    Table(const char *tablename);
    Table(const Table &other);
    ~Table();

    void describe(std::ostream &os) const;
};

struct Key {
    typedef enum { KEYGEN_APPEND, KEYGEN_PARETO, KEYGEN_UNIFORM } KeyType;
    KeyType _keytype;
    int _size;
#ifndef SWIG
    uint64_t _max;
#endif

    /* XXX specify more about key distribution */
    Key() : _keytype(KEYGEN_APPEND), _size(0), _max(0) {}
    Key(KeyType keytype, int size) : _keytype(keytype), _size(size), _max(0) {
	compute_max();
    }
    Key(const Key &other) : _keytype(other._keytype), _size(other._size),
	_max(other._max) {}
    ~Key() {}

    void describe(std::ostream &os) const { os << "Key: type " << _keytype << ", size " << _size; }
    void compute_max();

#ifndef SWIG
    void gen(uint64_t, char *) const;
    void size_buffer(size_t &keysize) const;
#endif
};

struct Value {
    int _size;
#ifndef SWIG
    uint64_t _max;
#endif

    /* XXX specify how value is calculated */
    Value() : _size(0), _max(0) {}
    Value(int size) : _size(size), _max(0) {
	compute_max();
    }
    Value(const Value &other) : _size(other._size), _max(other._max) {}
    ~Value() {}

    void describe(std::ostream &os) const { os << "Value: size " << _size; }
    void compute_max();

#ifndef SWIG
    void gen(uint64_t, char *) const;
    void size_buffer(size_t &keysize) const;
#endif
};

struct Operation {
    typedef enum { OP_NONE, OP_INSERT, OP_REMOVE, OP_SEARCH, OP_UPDATE } OpType;
    OpType _optype;

    Table _table;
    Key _key;
    Value _value;
    Transaction *_transaction;
    std::vector<Operation> *_children;
    int _repeatchildren;

    Operation();
    Operation(OpType optype, Table table, Key key, Value value);
    Operation(OpType optype, Table table, Key key);
    Operation(const Operation &other);
    ~Operation();

    void describe(std::ostream &os) const;

#ifndef SWIG
    int open_all(WT_SESSION *session, ThreadEnvironment &env);
    int run(ThreadEnvironment &env);
    void create_all(ThreadEnvironment &env, size_t &keysize,
	size_t &valuesize);
    void get_stats(TableStats &);
    void get_static_counts(TableStats &);
    void clear_stats();
#endif
};

struct Thread {
    std::vector<Operation> _ops;
    std::string _name;
    bool _stop;
    int _count;
#ifndef SWIG
    WT_SESSION *_session;
    char *_keybuf;
    char *_valuebuf;
    bool _repeat;
#endif

    /* XXX throttle info, etc. */

    Thread();
    Thread(const std::vector<Operation> &ops, int count = 1);
    Thread(const Thread &other);
    ~Thread();

    void describe(std::ostream &os) const;

#ifndef SWIG
    void free_all();
    int create_all(WT_CONNECTION *conn, ThreadEnvironment &env);
    int open_all(ThreadEnvironment &env);
    int close_all(ThreadEnvironment &env);
    void get_stats(TableStats &stats);
    void get_static_counts(TableStats &);
    void clear_stats();
    int run(ThreadEnvironment &env);
#endif
};

struct Transaction {
    bool _rollback;
    std::string _begin_config;
    std::string _commit_config;

    Transaction(const char *_config = NULL) : _rollback(false),
       _begin_config(_config == NULL ? "" : _config), _commit_config() {}

    void describe(std::ostream &os) const {
	os << "Transaction: ";
	if (_rollback)
	    os << "(rollback) ";
	os << "begin_config: " << _begin_config;
	if (!_commit_config.empty())
	    os << ", commit_config: " << _commit_config;
    }
};

struct Workload {
    Context *_context;
    std::vector<Thread> _threads;
    int _run_time;
    int _report_interval;

    Workload(Context *context, const std::vector<Thread> &threads);
    Workload(const Workload &other);
    ~Workload();

    void describe(std::ostream &os) const {
	os << "Workload: ";
	_context->describe(os);
	os << ", run_time " << _run_time;
	os << ", report_interval " << _report_interval;
	os << ", [" << std::endl;
	for (std::vector<Thread>::const_iterator i = _threads.begin(); i != _threads.end(); i++) {
	    os << "  "; i->describe(os); os << std::endl;
	}
	os << "]";
    }
    int run(WT_CONNECTION *conn);
    void report(int, int, TableStats &);
    void final_report(timespec &);

private:
    void get_stats(TableStats &stats);
    void clear_stats();
    int create_all(WT_CONNECTION *conn, Context *context,
	std::vector<ThreadEnvironment> &envs);
    int open_all(std::vector<ThreadEnvironment> &envs);
    int close_all(std::vector<ThreadEnvironment> &envs);
    int run_all(std::vector<ThreadEnvironment> &envs);
};

};
