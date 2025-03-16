#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <stdint.h>
#include "errhelp.h"
#include "constants.h"
#include <vector>
#include <string>
#include <poll.h>
#include <fcntl.h>
#include <assert.h>
#include "hashtable.h"
#include "commonops.h"
#include "zset.h"
#include "cdlist.h"



typedef std::vector<uint8_t> Buffer;

// appending data to the back of event loop buffer
static void buf_append(Buffer &buf, const uint8_t *data, size_t len){
    buf.insert(buf.end(), data, data+len);
};

// removing data from the front of event loop buffer
static void buf_remove(Buffer &buf, size_t len){
    buf.erase(buf.begin(), buf.begin()+len);
};

// stores per-connection state for event loop
struct Conn {
    int fd = -1;
    // application's intention, for the event loop
    bool want_read = false;     
    bool want_write = false;
    bool want_close = false;    
    // buffered input and output
    Buffer incoming;      
    Buffer outgoing;
    // timer
    uint64_t last_active_ms = 0;
    CDNode idle_node; 
};

// global states
static struct{
    // top-level hashtable
    HMap db;
    // map of all client connections, keyed by fd
    std::vector<Conn*> fd2conn;
    // timer for idle connections
    CDNode idle_list;       // list head
}g_data;

// function for getting the monotonic seconds
static uint64_t get_monotonic_msecs(){
    struct timespec tv = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return uint64_t(tv.tv_sec)*1000+tv.tv_nsec/1000/1000;
}

// sets an fd to non-blocking mode
static void fd_set_nb(int fd){
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno){
        die("fcntl() error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if(errno){
        die("fcntl() error");
        return;
    }
}


// application callback when the listening socket is ready
static int32_t handle_accept(int fd){
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr*) &client_addr, &socklen);
    if (connfd<0){
        msg_err("accept() error");
        return -1;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip>>8)&255, (ip>>15)&255, ip>>24,
        ntohs(client_addr.sin_port)
    );

    // set new connection to nb mode
    fd_set_nb(connfd);

    // create new struct Conn
    Conn* conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    conn->last_active_ms = get_monotonic_msecs();
    cdlist_insert_before(&g_data.idle_list, &conn->idle_node);

    if (g_data.fd2conn.size()<=(size_t)conn->fd){
        g_data.fd2conn.resize(conn->fd+1);
    }
    assert(!g_data.fd2conn[conn->fd]);
    g_data.fd2conn[conn->fd] = conn;
    return 0;
}

static void conn_destroy(Conn *conn){
    (void) close(conn->fd);
    g_data.fd2conn[conn->fd] = nullptr;
    cdlist_detach(&conn->idle_node);
    delete conn;
}


//================================== reading and parsing requests ==================================//

// +------+-----+------+-----+------+-----+-----+------+
// | nstr | len | str1 | len | str2 | ... | len | strn |
// +------+-----+------+-----+------+-----+-----+------+
static bool 
read_prefix(const uint8_t *&cur, const u_int8_t *end, u_int32_t &out){
    if (cur+4>end){
        return false;
    }
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool 
read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out){
    if (cur+n>end){
        return false;
    }
    out.assign(cur, cur+n);
    cur += n;
    return true;
}

static int32_t
parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out){
    const uint8_t *end = data+size;
    uint32_t nstr = 0;
    if (!read_prefix(data, end, nstr)){
        return -1;
    }
    if (nstr>k_max_args){
        msg("Too many arguments");
        return -1;
    }

    while(out.size()<nstr){
        uint32_t len = 0;
        if (!read_prefix(data, end, len)){
            return -1;
        }
        out.push_back(std::string());
        if (!read_str(data, end, len, out.back())){
            return -1;
        }
    }
    if (data != end){
        return -1;  // trailing garbage exists
    }
    return 0;
}


//================================== Data serialisation and output related code ==================================//
// error code for TAG_ERR
enum {
    ERR_UNKNOWN = 1,    // unknown command
    ERR_TOO_BIG = 2,    // response too big
    ERR_BAD_TYP = 3,    // unexpected value type
    ERR_BAD_ARG = 4,    // bad arguments
};

// the data types that we support
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

// helper functions for appending different data types
static void buf_append_u8(Buffer &buf, uint8_t data){
    buf.push_back(data);
}

static void buf_append_u32(Buffer &buf, uint32_t data){
    buf_append(buf, (const uint8_t*) &data, 4);
}

static void buf_append_i64(Buffer &buf, int64_t data){
    buf_append(buf, (const uint8_t*) &data, 8);
}

static void buf_append_dbl(Buffer &buf, double data){
    buf_append(buf, (const uint8_t*) &data, 8);
}

// append serialised data types to the back of Conn buffers
static void out_nil(Buffer &out){
    buf_append_u8(out, TAG_NIL);
}

static void out_str(Buffer &out, const char *s, size_t size){
    buf_append_u8(out, TAG_STR);
    buf_append_u32(out, (uint32_t)size);
    buf_append(out, (const uint8_t*)s, size);
}

static void out_int(Buffer &out, const int64_t val){
    buf_append_u8(out, TAG_STR);
    buf_append_i64(out, val);
}

static void out_dbl(Buffer &out, double val){
    buf_append_u8(out, TAG_DBL);
    buf_append_dbl(out, val);
}

static void out_err(Buffer &out, uint32_t code, const std::string &msg){
    buf_append_u8(out, TAG_ERR);
    buf_append_u32(out, code);
    buf_append_u32(out, (uint32_t)msg.size());
    buf_append(out, (const uint8_t*)msg.data(), msg.size());
}

static void out_arr(Buffer &out, uint32_t n){
    buf_append_u8(out, TAG_ARR);
    buf_append_u32(out, n);
}

// used for zqueries
static size_t out_begin_arr(Buffer &out){
    out.push_back(TAG_ARR);
    buf_append_u32(out, 0);     // filled by out_end_arr()
    return out.size()-4;        // the `ctx` arg
}
static void out_end_arr(Buffer &out, size_t ctx, uint32_t n){
    assert(out[ctx-1]==TAG_ARR);
    memcpy(&out[ctx], &n, 4);
}


//================================== hashtable and container structures ==================================//
enum {
    T_INIT  = 0,
    T_STR   = 1,
    T_ZSET  = 2,
};

/*
other design choices exists, but add complexity
1. use a union for the types, need to explicitly call constructor and destructor, complicated
2. define 2 subtypes and use runtime polymorphism extra definitions and need to know type, complicated
*/
struct Entry {  // kv pair for the top-level hashtable
    struct HNode node;  // hash table node
    std::string key;
    // specify the type of value
    uint32_t type = 0;
    // value is one of the following
    std::string str;
    ZSet zset;
};

static Entry *entry_new(uint32_t type){
    Entry *ent = new Entry();
    ent->type = type;
    return ent;
}

static void entry_del(Entry* ent){
    if (ent->type==T_ZSET){
        zset_clear(&ent->zset);
    }
    delete ent;
}

// used for lookup as it is more compact than Entry
struct LookupKey {
    struct HNode node;
    std::string key;
};

// Equality comparison for `struct entry`
static bool entry_eql(HNode* node, HNode* key){
    struct Entry *ent = container_of(node, struct Entry, node);
    struct LookupKey *keydata = container_of(key, struct LookupKey, node);
    return ent->key == keydata->key;
}


//================================== GET SET DEL KEYS queries ==================================//

// Function for performing the GET request
static void do_get(std::vector<std::string> &cmd, Buffer &out){
    // a dummy struct just for the lookup
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hval = str_hash((uint8_t*)key.key.data(), key.key.size());
    // hashtable lookup
    HNode* node = hm_lookup(&g_data.db, &key.node, &entry_eql);
    if (!node) {
        return out_nil(out);
    }
    // copy the value
    Entry *ent = container_of(node, Entry, node);
    if(ent->type != T_STR){
        return out_err(out, ERR_BAD_TYP, "Not a string value");
    }
    return out_str(out, ent->str.data(), ent->str.size());
}

static void do_set(std::vector<std::string> &cmd, Buffer &out){
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hval = str_hash((uint8_t*)key.key.data(), key.key.size());
    // hashtable lookup
    HNode* node = hm_lookup(&g_data.db, &key.node, &entry_eql);
    if (node) {
        Entry *ent = container_of(node, Entry, node);
        if(ent->type!=T_STR){
            return out_err(out, ERR_BAD_TYP, "Not a string value!");
        }
        ent->str.swap(cmd[2]);
    } else {
        Entry* ent = entry_new(T_STR);
        ent->key.swap(key.key);
        ent->node.hval = key.node.hval;
        ent->str.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, Buffer &out){
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hval = str_hash((uint8_t*)key.key.data(), key.key.size());
    HNode* node = hm_delete(&g_data.db, &key.node, &entry_eql);
    if (node) {
        entry_del(container_of(node, Entry, node));
    }
    return out_int(out, node ? 1 : 0);
}

// the call back function on each key
static bool cb_keys(HNode* node, void* arg){
    Buffer &out = *(Buffer*) arg;
    const std::string &key = container_of(node, Entry, node)->key;
    out_str(out, key.data(), key.size());
    return true;
}

static void do_keys(std::vector<std::string> &, Buffer &out){
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    hm_foreach(&g_data.db, &cb_keys, (void *)&out);
}

//================================== Redis range and rank related queries ==================================//

// utility functions to convert string to int and double respectively
static bool str2dbl(const std::string& s, double& out){
    char* endp = nullptr;
    out = strtod(s.c_str(), &endp);
    return endp == s.c_str()+s.size() && !isnan(out);
}
static bool str2int(const std::string& s, int64_t& out){
    char* endp = nullptr;
    out = strtoll(s.c_str(), &endp, 10);
    return endp == s.c_str()+s.size();
}

//+------+------+-------+------+
//| ZADD | zset | score | name |
//+------+------+-------+------+
static void do_zadd(std::vector<std::string>& cmd, Buffer& out){
    double score = 0;
    if (!str2dbl(cmd[2], score)){
        return out_err(out, ERR_BAD_ARG, "Expected float");
    }

    // lookup or create the zset
    LookupKey key;
    key.key.swap(cmd[1]);
    key.node.hval = str_hash((uint8_t*)key.key.data(), key.key.size());
    HNode* hnode = hm_lookup(&g_data.db, &key.node, &entry_eql);

    Entry* ent = nullptr;
    if (!hnode) {
        ent = entry_new(T_ZSET);
        ent->key.swap(key.key);
        ent->node.hval = key.node.hval;
        hm_insert(&g_data.db, &ent->node);
    } else {
        ent = container_of(hnode, Entry, node);
        if(ent->type != T_ZSET){
            return out_err(out, ERR_BAD_TYP, "expected zset");
        }
    }

    const std::string &name = cmd[3];
    bool added = zset_insert(&ent->zset, name.data(), name.size(), score);
    return out_int(out, (int64_t)added);
}

static const ZSet k_empty_zset; // an empty zset

static ZSet* expect_zset(std::string& s){
    LookupKey key;
    key.key.swap(s);
    key.node.hval = str_hash((uint8_t*)key.key.data(), key.key.size());
    HNode* hnode = hm_lookup(&g_data.db, &key.node, &entry_eql);
    if (!hnode){    // a non-existent key is treated as an empty zset
        return (ZSet*)&k_empty_zset;
    }
    Entry* ent = container_of(hnode, Entry, node);
    return ent->type == T_ZSET ? &ent->zset : nullptr;
}

//+------+------+------+
//| ZREM | zset | name |
//+------+------+------+
static void do_zrem(std::vector<std::string>& cmd, Buffer &out){
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset){
        return out_err(out, ERR_BAD_TYP, "Expected zset");
    }

    const std::string &name = cmd[2];
    ZNode* znode = zset_lookup(zset, name.data(), name.size());
    if (znode){
        zset_delete(zset, znode);
    }
    return out_int(out, znode ? 1 : 0);
}

//+--------+------+------+
//| ZSCORE | zset | name |
//+--------+------+------+
static void do_zscore(std::vector<std::string>& cmd, Buffer &out) {
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset){
        return out_err(out, ERR_BAD_TYP, "Expected zset");
    }

    const std::string &name = cmd[2];
    ZNode* znode = zset_lookup(zset, name.data(), name.size());
    return znode ? out_dbl(out, znode->score) : out_nil(out);
}

//+--------+-----+-------+------+--------+-------+-----+------+
//| ZQUERY | key | score | name | offset | limit | len | strn |
//+--------+-----+-------+------+--------+-------+-----+------+
static void do_zquery(std::vector<std::string>& cmd, Buffer& out) {
    // parse arguments
    double score = 0;
    if (!str2dbl(cmd[2], score)){
        return out_err(out, ERR_BAD_ARG, "Expected float");
    }
    const std::string &name = cmd[3];
    int64_t offset = 0, limit = 0;
    if (!str2int(cmd[4], offset) || !str2int(cmd[5], limit)){
        return out_err(out, ERR_BAD_ARG, "Expected int");
    }

    // get the zset
    ZSet* zset = expect_zset(cmd[1]);
    if (!zset) {
        return out_err(out, ERR_BAD_TYP, "Expected zset");
    }

    // seek the key
    if (limit <= 0){
        return out_arr(out, 0);
    }
    ZNode* znode = zset_seekge(zset, score, name.data(), name.size());
    znode = znode_offset(znode, offset);

    // output
    size_t ctx = out_begin_arr(out);
    int64_t n = 0;
    while(znode && n < limit){
        out_str(out,znode->name, znode->len);
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1);
        n += 2;
    }
    out_end_arr(out, ctx, (uint32_t)n);
}

//================================== Code for handling reads/writes, requests, preparing responses ==================================//

static void handle_request(std::vector<std::string> &cmd, Buffer &out){
    if (cmd.size()==2 && cmd[0]=="GET"){
        return do_get(cmd, out);
    } else if (cmd.size()==3 && cmd[0]=="SET"){
        return do_set(cmd, out);
    } else if (cmd.size()==2 && cmd[0]=="DEL"){
        return do_del(cmd, out);
    } else if (cmd.size()==1 && cmd[0]=="KEYS"){
        return do_keys(cmd, out);
    } else if (cmd.size()==4 && cmd[0]=="ZADD") {
        return do_zadd(cmd, out);
    } else if (cmd.size()==3 && cmd[0]=="ZREM") {
        return do_zrem(cmd, out);
    } else if (cmd.size()==3 && cmd[0]=="ZSCORE") {
        return do_zscore(cmd, out);
    } else if (cmd.size()==6 && cmd[0]=="ZQUERY") {
        return do_zquery(cmd, out);
    } else {
        return out_err(out, ERR_UNKNOWN, "Unknown command.");
    }
}

static void response_begin(Buffer &out, size_t *header){
    *header = out.size();       // message header position
    buf_append_u32(out, 0);     // reserve space
}
static size_t response_size(Buffer &out, size_t header){
    return out.size()-header-4;
}
static void response_end(Buffer &out, size_t header){
    size_t msg_size = response_size(out, header);
    if (msg_size > k_max_msg) {
        out.resize(header+4);   // remove current message
        out_err(out, ERR_TOO_BIG, "response is too big.");
        msg_size = response_size(out, header);
    }
    // message header
    uint32_t len = (uint32_t)msg_size;
    memcpy(&out[header], &len, 4);
}

// process 1 request if there is enough data
static bool try_one_request(Conn* conn){
    // try to parse the header
    if (conn->incoming.size()<4){
        return false;   // for want read
    }
    uint32_t len = 0;
    memcpy(&len, conn->incoming.data(), 4);
    if(len > k_max_msg){    // protocol error
        msg("Message is too long");
        conn->want_close = true;
        return false;   // set to want close
    }
    // message body
    if (4+len>conn->incoming.size()){
        return false;   // for want read
    }
    const uint8_t *request = &conn->incoming[4];

    // application logic for one request
    std::vector<std::string> cmd;
    if (parse_req(request, len, cmd)<0){
        msg("Error parsing request");
        conn->want_close = true;
        return false;
    }
    size_t header_pos = 0;
    response_begin(conn->outgoing, &header_pos);
    handle_request(cmd, conn->outgoing);
    response_end(conn->outgoing, header_pos);
    
    // application logic done, remove the request message
    buf_remove(conn->incoming, 4+len);
    return true;
}



// application callback when socket is writable
static void handle_write(Conn* conn){
    assert(conn->outgoing.size()>0);
    ssize_t ret = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (ret < 0 && errno == EAGAIN){
        return; // not ready
    }
    if (ret < 0){
        msg_err("write() error");
        conn->want_close = true;
        return;
    }

    // remove written data from `outgoing`
    buf_remove(conn->outgoing, (size_t) ret);

    // update the readiness intention
    if (conn->outgoing.size()==0){
        conn->want_read = true;
        conn->want_write = false;
    }
}



// application callback when socket is readable
static void handle_read(Conn* conn){
    // 1. Do a non-blocking read, buf size is set big for batched requests
    uint8_t buf[64*1024];
    ssize_t ret = read(conn->fd, buf, sizeof(buf));
    if (ret < 0 && errno == EAGAIN) {
        return; // not ready
    }
    if (ret < 0){
        // handle IO error
        msg_err("read() error");
        conn->want_close = true;
        return;
    }
    if (ret == 0){
        if(conn->incoming.size()==0){
            msg("Client closed");
        } else {
            msg("Unexpected EOF");
        }
        conn->want_close = true;
        return;
    }
    // 2. Add new data to the Conn::incoming buffer
    buf_append(conn->incoming, buf, (size_t)ret);

    // 3. Try to handle this request, using loops for http piplining
    while(try_one_request(conn)) {}

    // 4. update the readiness intention
    if (conn->outgoing.size()>0){
        conn->want_read = false;
        conn->want_write = true; 
        // try to write it without waiting for the next iteration
        return handle_write(conn);
    }
}


//======================================== timer related code ========================================//

// determines how long poll() should wait before checking for idle connections 
static int32_t next_timer_ms() {
    if (cdlist_empty(&g_data.idle_list)){
        return -1;  // no timers no timeouts
    }

    uint64_t now_ms = get_monotonic_msecs();
    Conn* conn = container_of(g_data.idle_list.next, Conn, idle_node);
    // calculates when this connection should be considered timed out
    uint64_t next_ms = conn->last_active_ms+k_idle_timeout_ms;      
    if (next_ms <= now_ms){
        // missed, so poll should return immediately to handle expired timers
        return 0;  
    }
    return (int32_t)(next_ms-now_ms);
}

// runs after each poll() call to clean up idle connections that have exceeded their timeout
static void process_timers(){
    uint64_t now_ms = get_monotonic_msecs();
    while(!cdlist_empty(&g_data.idle_list)){
        Conn* conn = container_of(g_data.idle_list.next, Conn, idle_node);
        uint64_t next_ms = conn->last_active_ms + k_idle_timeout_ms;
        if (next_ms >= now_ms){
            break;      // timer expired
        }
        fprintf(stderr, "Removing idle connection: %d\n", conn->fd);
        conn_destroy(conn);
    }
}

//======================================== main server program ========================================//

int main(void){
    // initialisation of the timer list
    cdlist_init(&g_data.idle_list);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0){
        die("socket()");
    }
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);        // port: 1234
    addr.sin_addr.s_addr = ntohl(0);    // wildcard IP: 0.0.0.0
    int ret = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (ret){
        die("bind()");      
    }

    // set listen socket non-blocking
    fd_set_nb(fd);  

    // listen
    ret = listen(fd, SOMAXCONN);        
    if (ret){
        die("listen()");    
    }

    // the event loop
    std::vector<struct pollfd> poll_args;
    while(true){
        // 
        poll_args.clear();
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);   // put the listening sockets in the first place

        // put the all the remaining connection sockets in
        for (Conn* conn : g_data.fd2conn){
            if (!conn){
                continue;
            }
            // always poll() for error
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            // poll() flags from the application's intent
            if (conn->want_read){
                pfd.events |= POLLIN;
            }
            if (conn->want_write){
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        // wait for readiness
        int32_t timeout_ms = next_timer_ms();
        int ret = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
        if (ret < 0 && errno == EINTR){
            continue; // an interrupt, not an error
        }
        if (ret < 0){
            die("poll()");
        }

        // handle the listening socket
        if (poll_args[0].revents){
            handle_accept(fd);
        }
        // handle connection sockets
        for (size_t i = 1; i < poll_args.size(); i++){
            uint32_t ready = poll_args[i].revents;
            if (ready==0){
                continue;
            }

            Conn *conn = g_data.fd2conn[poll_args[i].fd];

            // update the idle timer and move it to the end of the list
            conn->last_active_ms = get_monotonic_msecs();
            cdlist_detach(&conn->idle_node);
            cdlist_insert_before(&g_data.idle_list, &conn->idle_node);

            // read and write
            if (ready & POLLIN){
                assert(conn->want_read);
                handle_read(conn);
            }
            if (ready & POLLOUT){
                assert(conn->want_write);
                handle_write(conn);
            }

            // close the socket from error or application logic
            if ((ready&POLLERR) || conn->want_close){
                conn_destroy(conn);
            }
        }   // loop for each connection socket

        // handle the timers
        process_timers();
    }   // the event loop
    
    return 0;
}