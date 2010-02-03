/* Copyright (c) 2009 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef RAMCLOUD_SERVER_SERVER_H
#define RAMCLOUD_SERVER_SERVER_H

#include <config.h>

#include <shared/common.h>
#include <shared/Log.h>
#include <shared/rcrpc.h>
#include <shared/backup_client.h>

#include <Net.h>
#include <Hashtable.h>

#include <inttypes.h>
#include <assert.h>
#include <stdio.h>

namespace RAMCloud {

struct object_mutable {
    uint64_t refcnt;
};

#define DECLARE_OBJECT(name, el) \
    char name##_buf[sizeof(object) + (el)] __attribute__((aligned (8))); \
    object *name = new(name##_buf) object(sizeof(name##_buf)); \
    assert((reinterpret_cast<uint64_t>(name) & 0x7) == 0);

struct object {

    /*
     * This buf_size parameter is here to annoy you a little bit if you try
     * stack-allocating one of these. You'll think twice about it, maybe
     * realize sizeof(entries) is bogus, and proceed to dynamically allocating
     * a buffer instead.
     */
    object(size_t buf_size) : key(-1), table(-1), version(-1), checksum(0),
                              is_tombstone(false), mut(NULL), data_len(0) {
        assert(buf_size >= sizeof(*this));
    }

    size_t size() const {
        return sizeof(*this) + this->data_len;
    }

    // WARNING: The hashtable code (for the moment) assumes that the
    // object's key is the first 64 bits of the struct
    uint64_t key;
    uint64_t table;
    uint64_t version;
    uint64_t checksum;
    bool is_tombstone;
    object_mutable *mut;
    uint64_t data_len;
    char data[0];

  private:
    DISALLOW_COPY_AND_ASSIGN(object);
};

class Table {
  public:
    static const int TABLE_NAME_MAX_LEN = 64;
    explicit Table() : next_key(0), next_version(1), object_map(HASH_NLINES) {
    }
    const char *GetName() { return &name[0]; }
    void SetName(const char *new_name) {
        strncpy(&name[0], new_name, TABLE_NAME_MAX_LEN);
        name[TABLE_NAME_MAX_LEN - 1] = '\0';
    }
    uint64_t AllocateKey() {
        while (Get(next_key))
            ++next_key;
        return next_key;
    }
    uint64_t AllocateVersion() {
        return next_version++;
    }
    const object *Get(uint64_t key) {
        void *val = object_map.Lookup(key);
        const object *o = static_cast<const object *>(val);
        return o;
    }
    void Put(uint64_t key, const object *o) {
        object_map.Delete(key);
        object_map.Insert(key, const_cast<object *>(o));
    }
    void Delete(uint64_t key) {
        object_map.Delete(key);
    }

  private:
    char name[64];
    uint64_t next_key;
    uint64_t next_version;
    Hashtable object_map;
    DISALLOW_COPY_AND_ASSIGN(Table);
};

struct ServerConfig {
    // Restore from backups before resuming operation
    bool restore;
  ServerConfig() : restore(false) {}
};

class Server {
  public:
    void Ping(const rcrpc_ping_request *req,
              rcrpc_ping_response *resp);
    void Read(const rcrpc_read_request *req,
              rcrpc_read_response *resp);
    void Write(const rcrpc_write_request *req,
               rcrpc_write_response *resp);
    void InsertKey(const rcrpc_insert_request *req,
                   rcrpc_insert_response *resp);
    void DeleteKey(const rcrpc_delete_request *req,
                   rcrpc_delete_response *resp);
    void CreateTable(const rcrpc_create_table_request *req,
                     rcrpc_create_table_response *resp);
    void OpenTable(const rcrpc_open_table_request *req,
                   rcrpc_open_table_response *resp);
    void DropTable(const rcrpc_drop_table_request *req,
                   rcrpc_drop_table_response *resp);

    explicit Server(const ServerConfig *sconfig, Net *net_impl);
    Server(const Server& server);
    Server& operator=(const Server& server);
    ~Server();
    void Run();

  private:
    static bool RejectOperation(const rcrpc_reject_rules *reject_rules,
                                uint64_t version);
    void Restore();
    void HandleRPC();
    bool StoreData(uint64_t table,
                   uint64_t key,
                   const rcrpc_reject_rules *reject_rules,
                   const char *buf,
                   uint64_t buf_len,
                   uint64_t *new_version);
    explicit Server();

    const ServerConfig *config;
    Log *log;
    Net *net;
    BackupClient backup;
    Table tables[RC_NUM_TABLES];
    friend void LogEvictionCallback(log_entry_type_t type,
                                    const void *p,
                                    uint64_t len,
                                    void *cookie);
    friend void SegmentReplayCallback(Segment *seg, void *cookie);
    friend void ObjectReplayCallback(log_entry_type_t type,
                                     const void *p,
                                     uint64_t len,
                                     void *cookie);
};

} // namespace RAMCloud

#endif