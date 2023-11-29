/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *///123

#ifndef __REDIS_H
#define __REDIS_H

#include "fmacros.h"
#include "config.h"
#include "solarisfixes.h"
#include "rio.h"
#include "atomicvar.h"
#include "commands.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <lua.h>
#include <signal.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifndef static_assert
#define static_assert(expr, lit) extern char __static_assert_failure[(expr) ? 1:-1]
#endif

typedef long long mstime_t; /* millisecond time type. */
typedef long long ustime_t; /* microsecond time type. */

#include "ae.h"      /* Event driven programming library */
#include "sds.h"     /* Dynamic safe strings */
#include "dict.h"    /* Hash tables */
#include "adlist.h"  /* Linked lists */
#include "zmalloc.h" /* total memory usage aware version of malloc/free */
#include "anet.h"    /* Networking the easy way */
#include "version.h" /* Version macro */
#include "util.h"    /* Misc functions useful in many places */
#include "latency.h" /* Latency monitor API */
#include "sparkline.h" /* ASCII graphs API */
#include "quicklist.h"  /* Lists are encoded as linked lists of
                           N-elements flat arrays */
#include "rax.h"     /* Radix tree */
#include "connection.h" /* Connection abstraction */

#define REDISMODULE_CORE 1
typedef struct redisObject robj;
#include "redismodule.h"    /* Redis modules API defines. */

/* Following includes allow test functions to be called from Redis main() */
#include "zipmap.h"
#include "ziplist.h" /* Compact list data structure */
#include "sha1.h"
#include "endianconv.h"
#include "crc64.h"

struct hdr_histogram;

/* helpers */
#define numElements(x) (sizeof(x)/sizeof((x)[0]))

/* min/max */
#undef min
#undef max
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

/* Get the pointer of the outer struct from a member address */
#define redis_member2struct(struct_name, member_name, member_addr) \
            ((struct_name *)((char*)member_addr - offsetof(struct_name, member_name)))

/* Error codes */
#define C_OK                    0
#define C_ERR                   -1

/* Static server configuration */
#define CONFIG_DEFAULT_HZ        10             /* Time interrupt calls/sec. */
#define CONFIG_MIN_HZ            1
#define CONFIG_MAX_HZ            500
#define MAX_CLIENTS_PER_CLOCK_TICK 200          /* HZ is adapted based on that. */
#define CRON_DBS_PER_CALL 16
#define NET_MAX_WRITES_PER_EVENT (1024*64)
#define PROTO_SHARED_SELECT_CMDS 10
#define OBJ_SHARED_INTEGERS 10000
#define OBJ_SHARED_BULKHDR_LEN 32
#define OBJ_SHARED_HDR_STRLEN(_len_) (((_len_) < 10) ? 4 : 5) /* see shared.mbulkhdr etc. */
#define LOG_MAX_LEN    1024 /* Default maximum length of syslog messages.*/
#define AOF_REWRITE_ITEMS_PER_CMD 64
#define AOF_ANNOTATION_LINE_MAX_LEN 1024
#define CONFIG_RUN_ID_SIZE 40
#define RDB_EOF_MARK_SIZE 40
#define CONFIG_REPL_BACKLOG_MIN_SIZE (1024*16)          /* 16k */
#define CONFIG_BGSAVE_RETRY_DELAY 5 /* Wait a few secs before trying again. */
#define CONFIG_DEFAULT_PID_FILE "/var/run/redis.pid"
#define CONFIG_DEFAULT_BINDADDR_COUNT 2
#define CONFIG_DEFAULT_BINDADDR { "*", "-::*" }
#define NET_HOST_STR_LEN 256 /* Longest valid hostname */
#define NET_IP_STR_LEN 46 /* INET6_ADDRSTRLEN is 46, but we need to be sure */
#define NET_ADDR_STR_LEN (NET_IP_STR_LEN+32) /* Must be enough for ip:port */
#define NET_HOST_PORT_STR_LEN (NET_HOST_STR_LEN+32) /* Must be enough for hostname:port */
#define CONFIG_BINDADDR_MAX 16
#define CONFIG_MIN_RESERVED_FDS 32
#define CONFIG_DEFAULT_PROC_TITLE_TEMPLATE "{title} {listen-addr} {server-mode}"

/* Bucket sizes for client eviction pools. Each bucket stores clients with
 * memory usage of up to twice the size of the bucket below it. */
#define CLIENT_MEM_USAGE_BUCKET_MIN_LOG 15 /* Bucket sizes start at up to 32KB (2^15) */
#define CLIENT_MEM_USAGE_BUCKET_MAX_LOG 33 /* Bucket for largest clients: sizes above 4GB (2^32) */
#define CLIENT_MEM_USAGE_BUCKETS (1+CLIENT_MEM_USAGE_BUCKET_MAX_LOG-CLIENT_MEM_USAGE_BUCKET_MIN_LOG)

#define ACTIVE_EXPIRE_CYCLE_SLOW 0
#define ACTIVE_EXPIRE_CYCLE_FAST 1

 /* Children process will exit with this status code to signal that the
  * process terminated without an error: this is useful in order to kill
  * a saving child (RDB or AOF one), without triggering in the parent the
  * write protection that is normally turned on on write errors.
  * Usually children that are terminated with SIGUSR1 will exit with this
  * special code. */
#define SERVER_CHILD_NOERROR_RETVAL    255

  /* Reading copy-on-write info is sometimes expensive and may slow down child
   * processes that report it continuously. We measure the cost of obtaining it
   * and hold back additional reading based on this factor. */
#define CHILD_COW_DUTY_CYCLE           100

   /* Instantaneous metrics tracking. */
#define STATS_METRIC_SAMPLES 16     /* Number of samples per metric. */
#define STATS_METRIC_COMMAND 0      /* Number of commands executed. */
#define STATS_METRIC_NET_INPUT 1    /* Bytes read to network. */
#define STATS_METRIC_NET_OUTPUT 2   /* Bytes written to network. */
#define STATS_METRIC_NET_INPUT_REPLICATION 3   /* Bytes read to network during replication. */
#define STATS_METRIC_NET_OUTPUT_REPLICATION 4   /* Bytes written to network during replication. */
#define STATS_METRIC_EL_CYCLE 5     /* Number of eventloop cycled. */
#define STATS_METRIC_EL_DURATION 6  /* Eventloop duration. */
#define STATS_METRIC_COUNT 7

/* Protocol and I/O related defines */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define PROTO_RESIZE_THRESHOLD  (1024*32) /* Threshold for determining whether to resize query buffer */
#define PROTO_REPLY_MIN_BYTES   (1024) /* the lower limit on reply buffer size */
#define REDIS_AUTOSYNC_BYTES (1024*1024*4) /* Sync file every 4MB. */

#define REPLY_BUFFER_DEFAULT_PEAK_RESET_TIME 5000 /* 5 seconds */

/* When configuring the server eventloop, we setup it so that the total number
 * of file descriptors we can handle are server.maxclients + RESERVED_FDS +
 * a few more to stay safe. Since RESERVED_FDS defaults to 32, we add 96
 * in order to make sure of not over provisioning more than 128 fds. */
#define CONFIG_FDSET_INCR (CONFIG_MIN_RESERVED_FDS+96)

 /* OOM Score Adjustment classes. */
#define CONFIG_OOM_MASTER 0
#define CONFIG_OOM_REPLICA 1
#define CONFIG_OOM_BGCHILD 2
#define CONFIG_OOM_COUNT 3

extern int configOOMScoreAdjValuesDefaults[CONFIG_OOM_COUNT];

/* Hash table parameters */
#define HASHTABLE_MIN_FILL        10      /* Minimal hash table fill 10% */
#define HASHTABLE_MAX_LOAD_FACTOR 1.618   /* Maximum hash table load factor. */

/* Command flags. Please check the definition of struct redisCommand in this file
 * for more information about the meaning of every flag. */
#define CMD_WRITE (1ULL<<0)
#define CMD_READONLY (1ULL<<1)
#define CMD_DENYOOM (1ULL<<2)
#define CMD_MODULE (1ULL<<3)           /* Command exported by module. */
#define CMD_ADMIN (1ULL<<4)
#define CMD_PUBSUB (1ULL<<5)
#define CMD_NOSCRIPT (1ULL<<6)
#define CMD_BLOCKING (1ULL<<8)       /* Has potential to block. */
#define CMD_LOADING (1ULL<<9)
#define CMD_STALE (1ULL<<10)
#define CMD_SKIP_MONITOR (1ULL<<11)
#define CMD_SKIP_SLOWLOG (1ULL<<12)
#define CMD_ASKING (1ULL<<13)
#define CMD_FAST (1ULL<<14)
#define CMD_NO_AUTH (1ULL<<15)
#define CMD_MAY_REPLICATE (1ULL<<16)
#define CMD_SENTINEL (1ULL<<17)
#define CMD_ONLY_SENTINEL (1ULL<<18)
#define CMD_NO_MANDATORY_KEYS (1ULL<<19)
#define CMD_PROTECTED (1ULL<<20)
#define CMD_MODULE_GETKEYS (1ULL<<21)  /* Use the modules getkeys interface. */
#define CMD_MODULE_NO_CLUSTER (1ULL<<22) /* Deny on Redis Cluster. */
#define CMD_NO_ASYNC_LOADING (1ULL<<23)
#define CMD_NO_MULTI (1ULL<<24)
#define CMD_MOVABLE_KEYS (1ULL<<25) /* The legacy range spec doesn't cover all keys.
                                     * Populated by populateCommandLegacyRangeSpec. */
#define CMD_ALLOW_BUSY ((1ULL<<26))
#define CMD_MODULE_GETCHANNELS (1ULL<<27)  /* Use the modules getchannels interface. */
#define CMD_TOUCHES_ARBITRARY_KEYS (1ULL<<28)

                                     /* Command flags that describe ACLs categories. */
#define ACL_CATEGORY_KEYSPACE (1ULL<<0)
#define ACL_CATEGORY_READ (1ULL<<1)
#define ACL_CATEGORY_WRITE (1ULL<<2)
#define ACL_CATEGORY_SET (1ULL<<3)
#define ACL_CATEGORY_SORTEDSET (1ULL<<4)
#define ACL_CATEGORY_LIST (1ULL<<5)
#define ACL_CATEGORY_HASH (1ULL<<6)
#define ACL_CATEGORY_STRING (1ULL<<7)
#define ACL_CATEGORY_BITMAP (1ULL<<8)
#define ACL_CATEGORY_HYPERLOGLOG (1ULL<<9)
#define ACL_CATEGORY_GEO (1ULL<<10)
#define ACL_CATEGORY_STREAM (1ULL<<11)
#define ACL_CATEGORY_PUBSUB (1ULL<<12)
#define ACL_CATEGORY_ADMIN (1ULL<<13)
#define ACL_CATEGORY_FAST (1ULL<<14)
#define ACL_CATEGORY_SLOW (1ULL<<15)
#define ACL_CATEGORY_BLOCKING (1ULL<<16)
#define ACL_CATEGORY_DANGEROUS (1ULL<<17)
#define ACL_CATEGORY_CONNECTION (1ULL<<18)
#define ACL_CATEGORY_TRANSACTION (1ULL<<19)
#define ACL_CATEGORY_SCRIPTING (1ULL<<20)

/* Key-spec flags *
 * -------------- */
 /* The following refer what the command actually does with the value or metadata
  * of the key, and not necessarily the user data or how it affects it.
  * Each key-spec may must have exactly one of these. Any operation that's not
  * distinctly deletion, overwrite or read-only would be marked as RW. */
#define CMD_KEY_RO (1ULL<<0)     /* Read-Only - Reads the value of the key, but
                                  * doesn't necessarily returns it. */
#define CMD_KEY_RW (1ULL<<1)     /* Read-Write - Modifies the data stored in the
                                  * value of the key or its metadata. */
#define CMD_KEY_OW (1ULL<<2)     /* Overwrite - Overwrites the data stored in
                                  * the value of the key. */
#define CMD_KEY_RM (1ULL<<3)     /* Deletes the key. */
                                  /* The following refer to user data inside the value of the key, not the metadata
                                   * like LRU, type, cardinality. It refers to the logical operation on the user's
                                   * data (actual input strings / TTL), being used / returned / copied / changed,
                                   * It doesn't refer to modification or returning of metadata (like type, count,
                                   * presence of data). Any write that's not INSERT or DELETE, would be an UPDATE.
                                   * Each key-spec may have one of the writes with or without access, or none: */
#define CMD_KEY_ACCESS (1ULL<<4) /* Returns, copies or uses the user data from
                                  * the value of the key. */
#define CMD_KEY_UPDATE (1ULL<<5) /* Updates data to the value, new value may
                                  * depend on the old value. */
#define CMD_KEY_INSERT (1ULL<<6) /* Adds data to the value with no chance of
                                  * modification or deletion of existing data. */
#define CMD_KEY_DELETE (1ULL<<7) /* Explicitly deletes some content
                                  * from the value of the key. */
                                  /* Other flags: */
#define CMD_KEY_NOT_KEY (1ULL<<8)     /* A 'fake' key that should be routed
                                       * like a key in cluster mode but is
                                       * excluded from other key checks. */
#define CMD_KEY_INCOMPLETE (1ULL<<9)  /* Means that the keyspec might not point
                                       * out to all keys it should cover */
#define CMD_KEY_VARIABLE_FLAGS (1ULL<<10)  /* Means that some keys might have
                                            * different flags depending on arguments */

                                            /* Key flags for when access type is unknown */
#define CMD_KEY_FULL_ACCESS (CMD_KEY_RW | CMD_KEY_ACCESS | CMD_KEY_UPDATE)

/* Key flags for how key is removed */
#define DB_FLAG_KEY_NONE 0
#define DB_FLAG_KEY_DELETED (1ULL<<0)
#define DB_FLAG_KEY_EXPIRED (1ULL<<1)
#define DB_FLAG_KEY_EVICTED (1ULL<<2)
#define DB_FLAG_KEY_OVERWRITE (1ULL<<3)

/* Channel flags share the same flag space as the key flags */
#define CMD_CHANNEL_PATTERN (1ULL<<11)     /* The argument is a channel pattern */
#define CMD_CHANNEL_SUBSCRIBE (1ULL<<12)   /* The command subscribes to channels */
#define CMD_CHANNEL_UNSUBSCRIBE (1ULL<<13) /* The command unsubscribes to channels */
#define CMD_CHANNEL_PUBLISH (1ULL<<14)     /* The command publishes to channels. */

/* AOF states */
#define AOF_OFF 0             /* AOF is off */
#define AOF_ON 1              /* AOF is on */
#define AOF_WAIT_REWRITE 2    /* AOF waits rewrite to start appending */

/* AOF return values for loadAppendOnlyFiles() and loadSingleAppendOnlyFile() */
#define AOF_OK 0
#define AOF_NOT_EXIST 1
#define AOF_EMPTY 2
#define AOF_OPEN_ERR 3
#define AOF_FAILED 4
#define AOF_TRUNCATED 5

/* RDB return values for rdbLoad. */
#define RDB_OK 0
#define RDB_NOT_EXIST 1 /* RDB file doesn't exist. */
#define RDB_FAILED 2 /* Failed to load the RDB file. */

/* Command doc flags */
#define CMD_DOC_NONE 0
#define CMD_DOC_DEPRECATED (1<<0) /* Command is deprecated */
#define CMD_DOC_SYSCMD (1<<1) /* System (internal) command */

/* Client flags */
#define CLIENT_SLAVE (1<<0)   /* This client is a replica */
#define CLIENT_MASTER (1<<1)  /* This client is a master */
#define CLIENT_MONITOR (1<<2) /* This client is a slave monitor, see MONITOR */
#define CLIENT_MULTI (1<<3)   /* This client is in a MULTI context */
#define CLIENT_BLOCKED (1<<4) /* The client is waiting in a blocking operation */
#define CLIENT_DIRTY_CAS (1<<5) /* Watched keys modified. EXEC will fail. */
#define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
#define CLIENT_UNBLOCKED (1<<7) /* This client was unblocked and is stored in
                                  server.unblocked_clients */
#define CLIENT_SCRIPT (1<<8) /* This is a non connected client used by Lua */
#define CLIENT_ASKING (1<<9)     /* Client issued the ASKING command */
#define CLIENT_CLOSE_ASAP (1<<10)/* Close this client ASAP */
#define CLIENT_UNIX_SOCKET (1<<11) /* Client connected via Unix domain socket */
#define CLIENT_DIRTY_EXEC (1<<12)  /* EXEC will fail for errors while queueing */
#define CLIENT_MASTER_FORCE_REPLY (1<<13)  /* Queue replies even if is master */
#define CLIENT_FORCE_AOF (1<<14)   /* Force AOF propagation of current cmd. */
#define CLIENT_FORCE_REPL (1<<15)  /* Force replication of current cmd. */
#define CLIENT_PRE_PSYNC (1<<16)   /* Instance don't understand PSYNC. */
#define CLIENT_READONLY (1<<17)    /* Cluster client is in read-only state. */
#define CLIENT_PUBSUB (1<<18)      /* Client is in Pub/Sub mode. */
#define CLIENT_PREVENT_AOF_PROP (1<<19)  /* Don't propagate to AOF. */
#define CLIENT_PREVENT_REPL_PROP (1<<20)  /* Don't propagate to slaves. */
#define CLIENT_PREVENT_PROP (CLIENT_PREVENT_AOF_PROP|CLIENT_PREVENT_REPL_PROP)
#define CLIENT_PENDING_WRITE (1<<21) /* Client has output to send but a write
                                        handler is yet not installed. */
#define CLIENT_REPLY_OFF (1<<22)   /* Don't send replies to client. */
#define CLIENT_REPLY_SKIP_NEXT (1<<23)  /* Set CLIENT_REPLY_SKIP for next cmd */
#define CLIENT_REPLY_SKIP (1<<24)  /* Don't send just this reply. */
#define CLIENT_LUA_DEBUG (1<<25)  /* Run EVAL in debug mode. */
#define CLIENT_LUA_DEBUG_SYNC (1<<26)  /* EVAL debugging without fork() */
#define CLIENT_MODULE (1<<27) /* Non connected client used by some module. */
#define CLIENT_PROTECTED (1<<28) /* Client should not be freed for now. */
#define CLIENT_EXECUTING_COMMAND (1<<29) /* Indicates that the client is currently in the process of handling
                                          a command. usually this will be marked only during call()
                                          however, blocked clients might have this flag kept until they
                                          will try to reprocess the command. */

#define CLIENT_PENDING_COMMAND (1<<30) /* Indicates the client has a fully
                                        * parsed command ready for execution. */
#define CLIENT_TRACKING (1ULL<<31) /* Client enabled keys tracking in order to
                                   perform client side caching. */
#define CLIENT_TRACKING_BROKEN_REDIR (1ULL<<32) /* Target client is invalid. */
#define CLIENT_TRACKING_BCAST (1ULL<<33) /* Tracking in BCAST mode. */
#define CLIENT_TRACKING_OPTIN (1ULL<<34)  /* Tracking in opt-in mode. */
#define CLIENT_TRACKING_OPTOUT (1ULL<<35) /* Tracking in opt-out mode. */
#define CLIENT_TRACKING_CACHING (1ULL<<36) /* CACHING yes/no was given,
                                              depending on optin/optout mode. */
#define CLIENT_TRACKING_NOLOOP (1ULL<<37) /* Don't send invalidation messages
                                             about writes performed by myself.*/
#define CLIENT_IN_TO_TABLE (1ULL<<38) /* This client is in the timeout table. */
#define CLIENT_PROTOCOL_ERROR (1ULL<<39) /* Protocol error chatting with it. */
#define CLIENT_CLOSE_AFTER_COMMAND (1ULL<<40) /* Close after executing commands
                                               * and writing entire reply. */
#define CLIENT_DENY_BLOCKING (1ULL<<41) /* Indicate that the client should not be blocked.
                                           currently, turned on inside MULTI, Lua, RM_Call,
                                           and AOF client */
#define CLIENT_REPL_RDBONLY (1ULL<<42) /* This client is a replica that only wants
                                          RDB without replication buffer. */
#define CLIENT_NO_EVICT (1ULL<<43) /* This client is protected against client
                                      memory eviction. */
#define CLIENT_ALLOW_OOM (1ULL<<44) /* Client used by RM_Call is allowed to fully execute
                                       scripts even when in OOM */
#define CLIENT_NO_TOUCH (1ULL<<45) /* This client will not touch LFU/LRU stats. */
#define CLIENT_PUSHING (1ULL<<46) /* This client is pushing notifications. */
#define CLIENT_MODULE_AUTH_HAS_RESULT (1ULL<<47) /* Indicates a client in the middle of module based
                                                    auth had been authenticated from the Module. */
#define CLIENT_MODULE_PREVENT_AOF_PROP (1ULL<<48) /* Module client do not want to propagate to AOF */
#define CLIENT_MODULE_PREVENT_REPL_PROP (1ULL<<49) /* Module client do not want to propagate to replica */

                                                    /* Client block type (btype field in client structure)
                                                     * if CLIENT_BLOCKED flag is set. */
typedef enum blocking_type {
    BLOCKED_NONE,    /* Not blocked, no CLIENT_BLOCKED flag set. */
    BLOCKED_LIST,    /* BLPOP & co. */
    BLOCKED_WAIT,    /* WAIT for synchronous replication. */
    BLOCKED_WAITAOF, /* WAITAOF for AOF file fsync. */
    BLOCKED_MODULE,  /* Blocked by a loadable module. */
    BLOCKED_STREAM,  /* XREAD. */
    BLOCKED_ZSET,    /* BZPOP et al. */
    BLOCKED_POSTPONE, /* Blocked by processCommand, re-try processing later. */
    BLOCKED_SHUTDOWN, /* SHUTDOWN. */
    BLOCKED_NUM,      /* Number of blocked states. */
    BLOCKED_END       /* End of enumeration */
} blocking_type;

/* Client request types */
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2

/* Client classes for client limits, currently used only for
 * the max-client-output-buffer limit implementation. */
#define CLIENT_TYPE_NORMAL 0 /* Normal req-reply clients + MONITORs */
#define CLIENT_TYPE_SLAVE 1  /* Slaves. */
#define CLIENT_TYPE_PUBSUB 2 /* Clients subscribed to PubSub channels. */
#define CLIENT_TYPE_MASTER 3 /* Master. */
#define CLIENT_TYPE_COUNT 4  /* Total number of client types. */
#define CLIENT_TYPE_OBUF_COUNT 3 /* Number of clients to expose to output
                                    buffer configuration. Just the first
                                    three: normal, slave, pubsub. */

                                    /* Slave replication state. Used in server.repl_state for slaves to remember
                                     * what to do next. */
typedef enum {
    REPL_STATE_NONE = 0,            /* No active replication */
    REPL_STATE_CONNECT,             /* Must connect to master */
    REPL_STATE_CONNECTING,          /* Connecting to master */
    /* --- Handshake states, must be ordered --- */
    REPL_STATE_RECEIVE_PING_REPLY,  /* Wait for PING reply */
    REPL_STATE_SEND_HANDSHAKE,      /* Send handshake sequence to master */
    REPL_STATE_RECEIVE_AUTH_REPLY,  /* Wait for AUTH reply */
    REPL_STATE_RECEIVE_PORT_REPLY,  /* Wait for REPLCONF reply */
    REPL_STATE_RECEIVE_IP_REPLY,    /* Wait for REPLCONF reply */
    REPL_STATE_RECEIVE_CAPA_REPLY,  /* Wait for REPLCONF reply */
    REPL_STATE_SEND_PSYNC,          /* Send PSYNC */
    REPL_STATE_RECEIVE_PSYNC_REPLY, /* Wait for PSYNC reply */
    /* --- End of handshake states --- */
    REPL_STATE_TRANSFER,        /* Receiving .rdb from master */
    REPL_STATE_CONNECTED,       /* Connected to master */
} repl_state;

/* The state of an in progress coordinated failover */
typedef enum {
    NO_FAILOVER = 0,        /* No failover in progress */
    FAILOVER_WAIT_FOR_SYNC, /* Waiting for target replica to catch up */
    FAILOVER_IN_PROGRESS    /* Waiting for target replica to accept
                             * PSYNC FAILOVER request. */
} failover_state;

/* State of slaves from the POV of the master. Used in client->replstate.
 * In SEND_BULK and ONLINE state the slave receives new updates
 * in its output queue. In the WAIT_BGSAVE states instead the server is waiting
 * to start the next background saving in order to send updates to it. */
#define SLAVE_STATE_WAIT_BGSAVE_START 6 /* We need to produce a new RDB file. */
#define SLAVE_STATE_WAIT_BGSAVE_END 7 /* Waiting RDB file creation to finish. */
#define SLAVE_STATE_SEND_BULK 8 /* Sending RDB file to slave. */
#define SLAVE_STATE_ONLINE 9 /* RDB file transmitted, sending just updates. */
#define SLAVE_STATE_RDB_TRANSMITTED 10 /* RDB file transmitted - This state is used only for
                                        * a replica that only wants RDB without replication buffer  */

                                        /* Slave capabilities. */
#define SLAVE_CAPA_NONE 0
#define SLAVE_CAPA_EOF (1<<0)    /* Can parse the RDB EOF streaming format. */
#define SLAVE_CAPA_PSYNC2 (1<<1) /* Supports PSYNC2 protocol. */

/* Slave requirements */
#define SLAVE_REQ_NONE 0
#define SLAVE_REQ_RDB_EXCLUDE_DATA (1 << 0)      /* Exclude data from RDB */
#define SLAVE_REQ_RDB_EXCLUDE_FUNCTIONS (1 << 1) /* Exclude functions from RDB */
/* Mask of all bits in the slave requirements bitfield that represent non-standard (filtered) RDB requirements */
#define SLAVE_REQ_RDB_MASK (SLAVE_REQ_RDB_EXCLUDE_DATA | SLAVE_REQ_RDB_EXCLUDE_FUNCTIONS)

/* Synchronous read timeout - slave side */
#define CONFIG_REPL_SYNCIO_TIMEOUT 5

/* The default number of replication backlog blocks to trim per call. */
#define REPL_BACKLOG_TRIM_BLOCKS_PER_CALL 64

/* In order to quickly find the requested offset for PSYNC requests,
 * we index some nodes in the replication buffer linked list into a rax. */
#define REPL_BACKLOG_INDEX_PER_BLOCKS 64

 /* List related stuff */
#define LIST_HEAD 0
#define LIST_TAIL 1
#define ZSET_MIN 0
#define ZSET_MAX 1

/* Sort operations */
#define SORT_OP_GET 0

/* Log levels */
#define LL_DEBUG 0
#define LL_VERBOSE 1
#define LL_NOTICE 2
#define LL_WARNING 3
#define LL_NOTHING 4
#define LL_RAW (1<<10) /* Modifier to log without timestamp */

/* Supervision options */
#define SUPERVISED_NONE 0
#define SUPERVISED_AUTODETECT 1
#define SUPERVISED_SYSTEMD 2
#define SUPERVISED_UPSTART 3

/* Anti-warning macro... */
#define UNUSED(V) ((void) V)

#define ZSKIPLIST_MAXLEVEL 32 /* Should be enough for 2^64 elements */
#define ZSKIPLIST_P 0.25      /* Skiplist P = 1/4 */

/* Append only defines */
#define AOF_FSYNC_NO 0
#define AOF_FSYNC_ALWAYS 1
#define AOF_FSYNC_EVERYSEC 2

/* Replication diskless load defines */
#define REPL_DISKLESS_LOAD_DISABLED 0
#define REPL_DISKLESS_LOAD_WHEN_DB_EMPTY 1
#define REPL_DISKLESS_LOAD_SWAPDB 2

/* TLS Client Authentication */
#define TLS_CLIENT_AUTH_NO 0
#define TLS_CLIENT_AUTH_YES 1
#define TLS_CLIENT_AUTH_OPTIONAL 2

/* Sanitize dump payload */
#define SANITIZE_DUMP_NO 0
#define SANITIZE_DUMP_YES 1
#define SANITIZE_DUMP_CLIENTS 2

/* Enable protected config/command */
#define PROTECTED_ACTION_ALLOWED_NO 0
#define PROTECTED_ACTION_ALLOWED_YES 1
#define PROTECTED_ACTION_ALLOWED_LOCAL 2

/* Sets operations codes */
#define SET_OP_UNION 0
#define SET_OP_DIFF 1
#define SET_OP_INTER 2

/* oom-score-adj defines */
#define OOM_SCORE_ADJ_NO 0
#define OOM_SCORE_RELATIVE 1
#define OOM_SCORE_ADJ_ABSOLUTE 2

/* Redis maxmemory strategies. Instead of using just incremental number
 * for this defines, we use a set of flags so that testing for certain
 * properties common to multiple policies is faster. */
#define MAXMEMORY_FLAG_LRU (1<<0)
#define MAXMEMORY_FLAG_LFU (1<<1)
#define MAXMEMORY_FLAG_ALLKEYS (1<<2)
#define MAXMEMORY_FLAG_NO_SHARED_INTEGERS \
    (MAXMEMORY_FLAG_LRU|MAXMEMORY_FLAG_LFU)

#define MAXMEMORY_VOLATILE_LRU ((0<<8)|MAXMEMORY_FLAG_LRU)
#define MAXMEMORY_VOLATILE_LFU ((1<<8)|MAXMEMORY_FLAG_LFU)
#define MAXMEMORY_VOLATILE_TTL (2<<8)
#define MAXMEMORY_VOLATILE_RANDOM (3<<8)
#define MAXMEMORY_ALLKEYS_LRU ((4<<8)|MAXMEMORY_FLAG_LRU|MAXMEMORY_FLAG_ALLKEYS)
#define MAXMEMORY_ALLKEYS_LFU ((5<<8)|MAXMEMORY_FLAG_LFU|MAXMEMORY_FLAG_ALLKEYS)
#define MAXMEMORY_ALLKEYS_RANDOM ((6<<8)|MAXMEMORY_FLAG_ALLKEYS)
#define MAXMEMORY_NO_EVICTION (7<<8)

 /* Units */
#define UNIT_SECONDS 0
#define UNIT_MILLISECONDS 1

/* SHUTDOWN flags */
#define SHUTDOWN_NOFLAGS 0      /* No flags. */
#define SHUTDOWN_SAVE 1         /* Force SAVE on SHUTDOWN even if no save
                                   points are configured. */
#define SHUTDOWN_NOSAVE 2       /* Don't SAVE on SHUTDOWN. */
#define SHUTDOWN_NOW 4          /* Don't wait for replicas to catch up. */
#define SHUTDOWN_FORCE 8        /* Don't let errors prevent shutdown. */

                                   /* Command call flags, see call() function */
#define CMD_CALL_NONE 0
#define CMD_CALL_PROPAGATE_AOF (1<<0)
#define CMD_CALL_PROPAGATE_REPL (1<<1)
#define CMD_CALL_REPROCESSING (1<<2)
#define CMD_CALL_FROM_MODULE (1<<3)  /* From RM_Call */
#define CMD_CALL_PROPAGATE (CMD_CALL_PROPAGATE_AOF|CMD_CALL_PROPAGATE_REPL)
#define CMD_CALL_FULL (CMD_CALL_PROPAGATE)

/* Command propagation flags, see propagateNow() function */
#define PROPAGATE_NONE 0
#define PROPAGATE_AOF 1
#define PROPAGATE_REPL 2

/* Actions pause types */
#define PAUSE_ACTION_CLIENT_WRITE     (1<<0)
#define PAUSE_ACTION_CLIENT_ALL       (1<<1) /* must be bigger than PAUSE_ACTION_CLIENT_WRITE */
#define PAUSE_ACTION_EXPIRE           (1<<2)
#define PAUSE_ACTION_EVICT            (1<<3)
#define PAUSE_ACTION_REPLICA          (1<<4) /* pause replica traffic */

/* common sets of actions to pause/unpause */
#define PAUSE_ACTIONS_CLIENT_WRITE_SET (PAUSE_ACTION_CLIENT_WRITE|\
                                        PAUSE_ACTION_EXPIRE|\
                                        PAUSE_ACTION_EVICT|\
                                        PAUSE_ACTION_REPLICA)
#define PAUSE_ACTIONS_CLIENT_ALL_SET   (PAUSE_ACTION_CLIENT_ALL|\
                                        PAUSE_ACTION_EXPIRE|\
                                        PAUSE_ACTION_EVICT|\
                                        PAUSE_ACTION_REPLICA)

/* Client pause purposes. Each purpose has its own end time and pause type. */
typedef enum {
    PAUSE_BY_CLIENT_COMMAND = 0,
    PAUSE_DURING_SHUTDOWN,
    PAUSE_DURING_FAILOVER,
    NUM_PAUSE_PURPOSES /* This value is the number of purposes above. */
} pause_purpose;

typedef struct {
    uint32_t paused_actions; /* Bitmask of actions */
    mstime_t end;
} pause_event;

/* Ways that a clusters endpoint can be described */
typedef enum {
    CLUSTER_ENDPOINT_TYPE_IP = 0,          /* Show IP address */
    CLUSTER_ENDPOINT_TYPE_HOSTNAME,        /* Show hostname */
    CLUSTER_ENDPOINT_TYPE_UNKNOWN_ENDPOINT /* Show NULL or empty */
} cluster_endpoint_type;

/* RDB active child save type. */
#define RDB_CHILD_TYPE_NONE 0
#define RDB_CHILD_TYPE_DISK 1     /* RDB is written to disk. */
#define RDB_CHILD_TYPE_SOCKET 2   /* RDB is written to slave socket. */

/* Keyspace changes notification classes. Every class is associated with a
 * character for configuration purposes. */
#define NOTIFY_KEYSPACE (1<<0)    /* K */
#define NOTIFY_KEYEVENT (1<<1)    /* E */
#define NOTIFY_GENERIC (1<<2)     /* g */
#define NOTIFY_STRING (1<<3)      /* $ */
#define NOTIFY_LIST (1<<4)        /* l */
#define NOTIFY_SET (1<<5)         /* s */
#define NOTIFY_HASH (1<<6)        /* h */
#define NOTIFY_ZSET (1<<7)        /* z */
#define NOTIFY_EXPIRED (1<<8)     /* x */
#define NOTIFY_EVICTED (1<<9)     /* e */
#define NOTIFY_STREAM (1<<10)     /* t */
#define NOTIFY_KEY_MISS (1<<11)   /* m (Note: This one is excluded from NOTIFY_ALL on purpose) */
#define NOTIFY_LOADED (1<<12)     /* module only key space notification, indicate a key loaded from rdb */
#define NOTIFY_MODULE (1<<13)     /* d, module key space notification */
#define NOTIFY_NEW (1<<14)        /* n, new key notification */
#define NOTIFY_ALL (NOTIFY_GENERIC | NOTIFY_STRING | NOTIFY_LIST | NOTIFY_SET | NOTIFY_HASH | NOTIFY_ZSET | NOTIFY_EXPIRED | NOTIFY_EVICTED | NOTIFY_STREAM | NOTIFY_MODULE) /* A flag */

 /* Using the following macro you can run code inside serverCron() with the
  * specified period, specified in milliseconds.
  * The actual resolution depends on server.hz. */
#define run_with_period(_ms_) if (((_ms_) <= 1000/server.hz) || !(server.cronloops%((_ms_)/(1000/server.hz))))

  /* We can print the stacktrace, so our assert is defined this way: */
#define serverAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (_serverAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),redis_unreachable()))
#define serverAssert(_e) ((_e)?(void)0 : (_serverAssert(#_e,__FILE__,__LINE__),redis_unreachable()))
#define serverPanic(...) _serverPanic(__FILE__,__LINE__,__VA_ARGS__),redis_unreachable()

/* latency histogram per command init settings */
#define LATENCY_HISTOGRAM_MIN_VALUE 1L        /* >= 1 nanosec */
#define LATENCY_HISTOGRAM_MAX_VALUE 1000000000L  /* <= 1 secs */
#define LATENCY_HISTOGRAM_PRECISION 2  /* Maintain a value precision of 2 significant digits across LATENCY_HISTOGRAM_MIN_VALUE and LATENCY_HISTOGRAM_MAX_VALUE range.
                                        * Value quantization within the range will thus be no larger than 1/100th (or 1%) of any value.
                                        * The total size per histogram should sit around 40 KiB Bytes. */

                                        /* Busy module flags, see busy_module_yield_flags */
#define BUSY_MODULE_YIELD_NONE (0)
#define BUSY_MODULE_YIELD_EVENTS (1<<0)
#define BUSY_MODULE_YIELD_CLIENTS (1<<1)

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

 /* A redis object, that is a type able to hold a string / list / set */

 /* The actual Redis Object */
#define OBJ_STRING 0    /* String object. */
#define OBJ_LIST 1      /* List object. */
#define OBJ_SET 2       /* Set object. */
#define OBJ_ZSET 3      /* Sorted set object. */
#define OBJ_HASH 4      /* Hash object. */

/* The "module" object type is a special one that signals that the object
 * is one directly managed by a Redis module. In this case the value points
 * to a moduleValue struct, which contains the object value (which is only
 * handled by the module itself) and the RedisModuleType struct which lists
 * function pointers in order to serialize, deserialize, AOF-rewrite and
 * free the object.
 *
 * Inside the RDB file, module types are encoded as OBJ_MODULE followed
 * by a 64 bit module type ID, which has a 54 bits module-specific signature
 * in order to dispatch the loading to the right module, plus a 10 bits
 * encoding version. */
#define OBJ_MODULE 5    /* Module object. */
#define OBJ_STREAM 6    /* Stream object. */
#define OBJ_TYPE_MAX 7  /* Maximum number of object types */

 /* Extract encver / signature from a module type ID. */
#define REDISMODULE_TYPE_ENCVER_BITS 10
#define REDISMODULE_TYPE_ENCVER_MASK ((1<<REDISMODULE_TYPE_ENCVER_BITS)-1)
#define REDISMODULE_TYPE_ENCVER(id) ((id) & REDISMODULE_TYPE_ENCVER_MASK)
#define REDISMODULE_TYPE_SIGN(id) (((id) & ~((uint64_t)REDISMODULE_TYPE_ENCVER_MASK)) >>REDISMODULE_TYPE_ENCVER_BITS)

/* Bit flags for moduleTypeAuxSaveFunc */
#define REDISMODULE_AUX_BEFORE_RDB (1<<0)
#define REDISMODULE_AUX_AFTER_RDB (1<<1)

struct RedisModule;
struct RedisModuleIO;
struct RedisModuleDigest;
struct RedisModuleCtx;
struct moduleLoadQueueEntry;
struct RedisModuleKeyOptCtx;
struct RedisModuleCommand;

/* Each module type implementation should export a set of methods in order
 * to serialize and deserialize the value in the RDB file, rewrite the AOF
 * log, create the digest for "DEBUG DIGEST", and free the value when a key
 * is deleted. */
typedef void* (*moduleTypeLoadFunc)(struct RedisModuleIO* io, int encver);
typedef void (*moduleTypeSaveFunc)(struct RedisModuleIO* io, void* value);
typedef int (*moduleTypeAuxLoadFunc)(struct RedisModuleIO* rdb, int encver, int when);
typedef void (*moduleTypeAuxSaveFunc)(struct RedisModuleIO* rdb, int when);
typedef void (*moduleTypeRewriteFunc)(struct RedisModuleIO* io, struct redisObject* key, void* value);
typedef void (*moduleTypeDigestFunc)(struct RedisModuleDigest* digest, void* value);
typedef size_t(*moduleTypeMemUsageFunc)(const void* value);
typedef void (*moduleTypeFreeFunc)(void* value);
typedef size_t(*moduleTypeFreeEffortFunc)(struct redisObject* key, const void* value);
typedef void (*moduleTypeUnlinkFunc)(struct redisObject* key, void* value);
typedef void* (*moduleTypeCopyFunc)(struct redisObject* fromkey, struct redisObject* tokey, const void* value);
typedef int (*moduleTypeDefragFunc)(struct RedisModuleDefragCtx* ctx, struct redisObject* key, void** value);
typedef size_t(*moduleTypeMemUsageFunc2)(struct RedisModuleKeyOptCtx* ctx, const void* value, size_t sample_size);
typedef void (*moduleTypeFreeFunc2)(struct RedisModuleKeyOptCtx* ctx, void* value);
typedef size_t(*moduleTypeFreeEffortFunc2)(struct RedisModuleKeyOptCtx* ctx, const void* value);
typedef void (*moduleTypeUnlinkFunc2)(struct RedisModuleKeyOptCtx* ctx, void* value);
typedef void* (*moduleTypeCopyFunc2)(struct RedisModuleKeyOptCtx* ctx, const void* value);
typedef int (*moduleTypeAuthCallback)(struct RedisModuleCtx* ctx, void* username, void* password, const char** err);


/* The module type, which is referenced in each value of a given type, defines
 * the methods and links to the module exporting the type. */
typedef struct RedisModuleType {
    uint64_t id; /* Higher 54 bits of type ID + 10 lower bits of encoding ver. */
    struct RedisModule* module;
    moduleTypeLoadFunc rdb_load;
    moduleTypeSaveFunc rdb_save;
    moduleTypeRewriteFunc aof_rewrite;
    moduleTypeMemUsageFunc mem_usage;
    moduleTypeDigestFunc digest;
    moduleTypeFreeFunc free;
    moduleTypeFreeEffortFunc free_effort;
    moduleTypeUnlinkFunc unlink;
    moduleTypeCopyFunc copy;
    moduleTypeDefragFunc defrag;
    moduleTypeAuxLoadFunc aux_load;
    moduleTypeAuxSaveFunc aux_save;
    moduleTypeMemUsageFunc2 mem_usage2;
    moduleTypeFreeEffortFunc2 free_effort2;
    moduleTypeUnlinkFunc2 unlink2;
    moduleTypeCopyFunc2 copy2;
    moduleTypeAuxSaveFunc aux_save2;
    int aux_save_triggers;
    char name[10]; /* 9 bytes name + null term. Charset: A-Z a-z 0-9 _- */
} moduleType;

/* In Redis objects 'robj' structures of type OBJ_MODULE, the value pointer
 * is set to the following structure, referencing the moduleType structure
 * in order to work with the value, and at the same time providing a raw
 * pointer to the value, as created by the module commands operating with
 * the module type.
 *
 * So for example in order to free such a value, it is possible to use
 * the following code:
 *
 *  if (robj->type == OBJ_MODULE) {
 *      moduleValue *mt = robj->ptr;
 *      mt->type->free(mt->value);
 *      zfree(mt); // We need to release this in-the-middle struct as well.
 *  }
 */
typedef struct moduleValue {
    moduleType* type;
    void* value;
} moduleValue;

/* This structure represents a module inside the system. */
struct RedisModule {
    void* handle;   /* Module dlopen() handle. */
    char* name;     /* Module name. */
    int ver;        /* Module version. We use just progressive integers. */
    int apiver;     /* Module API version as requested during initialization.*/
    list* types;    /* Module data types. */
    list* usedby;   /* List of modules using APIs from this one. */
    list* using;    /* List of modules we use some APIs of. */
    list* filters;  /* List of filters the module has registered. */
    list* module_configs; /* List of configurations the module has registered */
    int configs_initialized; /* Have the module configurations been initialized? */
    int in_call;    /* RM_Call() nesting level */
    int in_hook;    /* Hooks callback nesting level for this module (0 or 1). */
    int options;    /* Module options and capabilities. */
    int blocked_clients;         /* Count of RedisModuleBlockedClient in this module. */
    RedisModuleInfoFunc info_cb; /* Callback for module to add INFO fields. */
    RedisModuleDefragFunc defrag_cb;    /* Callback for global data defrag. */
    struct moduleLoadQueueEntry* loadmod; /* Module load arguments for config rewrite. */
    int num_commands_with_acl_categories; /* Number of commands in this module included in acl categories */
    int onload;     /* Flag to identify if the call is being made from Onload (0 or 1) */
};
typedef struct RedisModule RedisModule;

/* This is a wrapper for the 'rio' streams used inside rdb.c in Redis, so that
 * the user does not have to take the total count of the written bytes nor
 * to care about error conditions. */
struct RedisModuleIO {
    size_t bytes;       /* Bytes read / written so far. */
    rio* rio;           /* Rio stream. */
    moduleType* type;   /* Module type doing the operation. */
    int error;          /* True if error condition happened. */
    struct RedisModuleCtx* ctx; /* Optional context, see RM_GetContextFromIO()*/
    struct redisObject* key;    /* Optional name of key processed */
    int dbid;            /* The dbid of the key being processed, -1 when unknown. */
    sds pre_flush_buffer; /* A buffer that should be flushed before next write operation
                           * See rdbSaveSingleModuleAux for more details */
};

/* Macro to initialize an IO context. Note that the 'ver' field is populated
 * inside rdb.c according to the version of the value to load. */
#define moduleInitIOContext(iovar,mtype,rioptr,keyptr,db) do { \
    iovar.rio = rioptr; \
    iovar.type = mtype; \
    iovar.bytes = 0; \
    iovar.error = 0; \
    iovar.key = keyptr; \
    iovar.dbid = db; \
    iovar.ctx = NULL; \
    iovar.pre_flush_buffer = NULL; \
} while(0)

 /* This is a structure used to export DEBUG DIGEST capabilities to Redis
  * modules. We want to capture both the ordered and unordered elements of
  * a data structure, so that a digest can be created in a way that correctly
  * reflects the values. See the DEBUG DIGEST command implementation for more
  * background. */
struct RedisModuleDigest {
    unsigned char o[20];    /* Ordered elements. */
    unsigned char x[20];    /* Xored elements. */
    struct redisObject* key; /* Optional name of key processed */
    int dbid;                /* The dbid of the key being processed */
};

/* Just start with a digest composed of all zero bytes. */
#define moduleInitDigestContext(mdvar) do { \
    memset(mdvar.o,0,sizeof(mdvar.o)); \
    memset(mdvar.x,0,sizeof(mdvar.x)); \
} while(0)

/* Macro to check if the client is in the middle of module based authentication. */
#define clientHasModuleAuthInProgress(c) ((c)->module_auth_ctx != NULL)

/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
#define OBJ_ENCODING_RAW 0     /* Raw representation */
#define OBJ_ENCODING_INT 1     /* Encoded as integer */
#define OBJ_ENCODING_HT 2      /* Encoded as hash table */
#define OBJ_ENCODING_ZIPMAP 3  /* No longer used: old hash encoding. */
#define OBJ_ENCODING_LINKEDLIST 4 /* No longer used: old list encoding. */
#define OBJ_ENCODING_ZIPLIST 5 /* No longer used: old list/hash/zset encoding. */
#define OBJ_ENCODING_INTSET 6  /* Encoded as intset */
#define OBJ_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
#define OBJ_ENCODING_EMBSTR 8  /* Embedded sds string encoding */
#define OBJ_ENCODING_QUICKLIST 9 /* Encoded as linked list of listpacks */
#define OBJ_ENCODING_STREAM 10 /* Encoded as a radix tree of listpacks */
#define OBJ_ENCODING_LISTPACK 11 /* Encoded as a listpack */

#define LRU_BITS 24
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1) /* Max value of obj->lru */
#define LRU_CLOCK_RESOLUTION 1000 /* LRU clock resolution in ms */

#define OBJ_SHARED_REFCOUNT INT_MAX     /* Global object never destroyed. */
#define OBJ_STATIC_REFCOUNT (INT_MAX-1) /* Object allocated in the stack. */
#define OBJ_FIRST_SPECIAL_REFCOUNT OBJ_STATIC_REFCOUNT
struct redisObject {
    unsigned type : 4;
    unsigned encoding : 4;
    unsigned lru : LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
    int refcount;
    void* ptr;
};

/* The string name for an object's type as listed above
 * Native types are checked against the OBJ_STRING, OBJ_LIST, OBJ_* defines,
 * and Module types have their registered name returned. */
char* getObjectTypeName(robj*);

/* Macro used to initialize a Redis object allocated on the stack.
 * Note that this macro is taken near the structure definition to make sure
 * we'll update it when the structure is changed, to avoid bugs like
 * bug #85 introduced exactly in this way. */
#define initStaticStringObject(_var,_ptr) do { \
    _var.refcount = OBJ_STATIC_REFCOUNT; \
    _var.type = OBJ_STRING; \
    _var.encoding = OBJ_ENCODING_RAW; \
    _var.ptr = _ptr; \
} while(0)

struct evictionPoolEntry; /* Defined in evict.c */

/* This structure is used in order to represent the output buffer of a client,
 * which is actually a linked list of blocks like that, that is: client->reply. */
typedef struct clientReplyBlock {
    size_t size, used;
    char buf[];
} clientReplyBlock;

/* Replication buffer blocks is the list of replBufBlock.
 *
 * +--------------+       +--------------+       +--------------+
 * | refcount = 1 |  ...  | refcount = 0 |  ...  | refcount = 2 |
 * +--------------+       +--------------+       +--------------+
 *      |                                            /       \
 *      |                                           /         \
 *      |                                          /           \
 *  Repl Backlog                               Replica_A    Replica_B
 *
 * Each replica or replication backlog increments only the refcount of the
 * 'ref_repl_buf_node' which it points to. So when replica walks to the next
 * node, it should first increase the next node's refcount, and when we trim
 * the replication buffer nodes, we remove node always from the head node which
 * refcount is 0. If the refcount of the head node is not 0, we must stop
 * trimming and never iterate the next node. */

 /* Similar with 'clientReplyBlock', it is used for shared buffers between
  * all replica clients and replication backlog. */
typedef struct replBufBlock {
    int refcount;           /* Number of replicas or repl backlog using. */
    long long id;           /* The unique incremental number. */
    long long repl_offset;  /* Start replication offset of the block. */
    size_t size, used;
    char buf[];
} replBufBlock;

/* Opaque type for the Slot to Key API. */
typedef struct clusterSlotToKeyMapping clusterSlotToKeyMapping;

/* Redis database representation. There are multiple databases identified
 * by integers from 0 (the default database) up to the max configured
 * database. The database number is the 'id' field in the structure. */
typedef struct redisDb {
    dict* dict;                 /* The keyspace for this DB */
    dict* expires;              /* Timeout of keys with a timeout set */
    dict* blocking_keys;        /* Keys with clients waiting for data (BLPOP)*/
    dict* blocking_keys_unblock_on_nokey;   /* Keys with clients waiting for
                                             * data, and should be unblocked if key is deleted (XREADEDGROUP).
                                             * This is a subset of blocking_keys*/
    dict* ready_keys;           /* Blocked keys that received a PUSH */
    dict* watched_keys;         /* WATCHED keys for MULTI/EXEC CAS */
    int id;                     /* Database ID */
    long long avg_ttl;          /* Average TTL, just for stats */
    unsigned long expires_cursor; /* Cursor of the active expire cycle. */
    list* defrag_later;         /* List of key names to attempt to defrag one by one, gradually. */
    clusterSlotToKeyMapping* slots_to_keys; /* Array of slots to keys. Only used in cluster mode (db 0). */
} redisDb;

/* forward declaration for functions ctx */
typedef struct functionsLibCtx functionsLibCtx;

/* Holding object that need to be populated during
 * rdb loading. On loading end it is possible to decide
 * whether not to set those objects on their rightful place.
 * For example: dbarray need to be set as main database on
 *              successful loading and dropped on failure. */
typedef struct rdbLoadingCtx {
    redisDb* dbarray;
    functionsLibCtx* functions_lib_ctx;
}rdbLoadingCtx;

/* Client MULTI/EXEC state */
typedef struct multiCmd {
    robj** argv;
    int argv_len;
    int argc;
    struct redisCommand* cmd;
} multiCmd;

typedef struct multiState {
    multiCmd* commands;     /* Array of MULTI commands */
    int count;              /* Total number of MULTI commands */
    int cmd_flags;          /* The accumulated command flags OR-ed together.
                               So if at least a command has a given flag, it
                               will be set in this field. */
    int cmd_inv_flags;      /* Same as cmd_flags, OR-ing the ~flags. so that it
                               is possible to know if all the commands have a
                               certain flag. */
    size_t argv_len_sums;    /* mem used by all commands arguments */
    int alloc_count;         /* total number of multiCmd struct memory reserved. */
} multiState;

/* This structure holds the blocking operation state for a client.
 * The fields used depend on client->btype. */
typedef struct blockingState {
    /* Generic fields. */
    blocking_type btype;                  /* Type of blocking op if CLIENT_BLOCKED. */
    mstime_t timeout;           /* Blocking operation timeout. If UNIX current time
                                 * is > timeout then the operation timed out. */
    int unblock_on_nokey;       /* Whether to unblock the client when at least one of the keys
                                   is deleted or does not exist anymore */
                                   /* BLOCKED_LIST, BLOCKED_ZSET and BLOCKED_STREAM or any other Keys related blocking */
    dict* keys;                 /* The keys we are blocked on */

    /* BLOCKED_WAIT and BLOCKED_WAITAOF */
    int numreplicas;        /* Number of replicas we are waiting for ACK. */
    int numlocal;           /* Indication if WAITAOF is waiting for local fsync. */
    long long reploffset;   /* Replication offset to reach. */

    /* BLOCKED_MODULE */
    void* module_blocked_handle; /* RedisModuleBlockedClient structure.
                                    which is opaque for the Redis core, only
                                    handled in module.c. */

    void* async_rm_call_handle; /* RedisModuleAsyncRMCallPromise structure.
                                   which is opaque for the Redis core, only
                                   handled in module.c. */
} blockingState;

/* The following structure represents a node in the server.ready_keys list,
 * where we accumulate all the keys that had clients blocked with a blocking
 * operation such as B[LR]POP, but received new data in the context of the
 * last executed command.
 *
 * After the execution of every command or script, we iterate over this list to check
 * if as a result we should serve data to clients blocked, unblocking them.
 * Note that server.ready_keys will not have duplicates as there dictionary
 * also called ready_keys in every structure representing a Redis database,
 * where we make sure to remember if a given key was already added in the
 * server.ready_keys list. */
typedef struct readyList {
    redisDb* db;
    robj* key;
} readyList;

/* This structure represents a Redis user. This is useful for ACLs, the
 * user is associated to the connection after the connection is authenticated.
 * If there is no associated user, the connection uses the default user. */
#define USER_COMMAND_BITS_COUNT 1024    /* The total number of command bits
                                           in the user structure. The last valid
                                           command ID we can set in the user
                                           is USER_COMMAND_BITS_COUNT-1. */
#define USER_FLAG_ENABLED (1<<0)        /* The user is active. */
#define USER_FLAG_DISABLED (1<<1)       /* The user is disabled. */
#define USER_FLAG_NOPASS (1<<2)         /* The user requires no password, any
                                           provided password will work. For the
                                           default user, this also means that
                                           no AUTH is needed, and every
                                           connection is immediately
                                           authenticated. */
#define USER_FLAG_SANITIZE_PAYLOAD (1<<3)       /* The user require a deep RESTORE
                                                 * payload sanitization. */
#define USER_FLAG_SANITIZE_PAYLOAD_SKIP (1<<4)  /* The user should skip the
                                                 * deep sanitization of RESTORE
                                                 * payload. */

#define SELECTOR_FLAG_ROOT (1<<0)           /* This is the root user permission
                                             * selector. */
#define SELECTOR_FLAG_ALLKEYS (1<<1)        /* The user can mention any key. */
#define SELECTOR_FLAG_ALLCOMMANDS (1<<2)    /* The user can run all commands. */
#define SELECTOR_FLAG_ALLCHANNELS (1<<3)    /* The user can mention any Pub/Sub
                                               channel. */

typedef struct {
    sds name;       /* The username as an SDS string. */
    uint32_t flags; /* See USER_FLAG_* */
    list* passwords; /* A list of SDS valid passwords for this user. */
    list* selectors; /* A list of selectors this user validates commands
                        against. This list will always contain at least
                        one selector for backwards compatibility. */
    robj* acl_string; /* cached string represent of ACLs */
} user;

/* With multiplexing we need to take per-client state.
 * Clients are taken in a linked list. */

#define CLIENT_ID_AOF (UINT64_MAX) /* Reserved ID for the AOF client. If you
                                      need more reserved IDs use UINT64_MAX-1,
                                      -2, ... and so forth. */

                                      /* Replication backlog is not a separate memory, it just is one consumer of
                                       * the global replication buffer. This structure records the reference of
                                       * replication buffers. Since the replication buffer block list may be very long,
                                       * it would cost much time to search replication offset on partial resync, so
                                       * we use one rax tree to index some blocks every REPL_BACKLOG_INDEX_PER_BLOCKS
                                       * to make searching offset from replication buffer blocks list faster. */
typedef struct replBacklog {
    listNode* ref_repl_buf_node; /* Referenced node of replication buffer blocks,
                                  * see the definition of replBufBlock. */
    size_t unindexed_count;      /* The count from last creating index block. */
    rax* blocks_index;           /* The index of recorded blocks of replication
                                  * buffer for quickly searching replication
                                  * offset on partial resynchronization. */
    long long histlen;           /* Backlog actual data length */
    long long offset;            /* Replication "master offset" of first
                                  * byte in the replication backlog buffer.*/
} replBacklog;

typedef struct {
    list* clients;
    size_t mem_usage_sum;
} clientMemUsageBucket;

#ifdef LOG_REQ_RES
/* Structure used to log client's requests and their
 * responses (see logreqres.c) */
typedef struct {
    /* General */
    int argv_logged; /* 1 if the command was logged */
    /* Vars for log buffer */
    unsigned char* buf; /* Buffer holding the data (request and response) */
    size_t used;
    size_t capacity;
    /* Vars for offsets within the client's reply */
    struct {
        /* General */
        int saved; /* 1 if we already saved the offset (first time we call addReply*) */
        /* Offset within the static reply buffer */
        int bufpos;
        /* Offset within the reply block list */
        struct {
            int index;
            size_t used;
        } last_node;
    } offset;
} clientReqResInfo;
#endif

typedef struct client {
    uint64_t id;            /* Client incremental unique ID. */
    uint64_t flags;         /* Client flags: CLIENT_* macros. */
    connection* conn;
    int resp;               /* RESP protocol version. Can be 2 or 3. */
    redisDb* db;            /* Pointer to currently SELECTed DB. */
    robj* name;             /* As set by CLIENT SETNAME. */
    robj* lib_name;         /* The client library name as set by CLIENT SETINFO. */
    robj* lib_ver;          /* The client library version as set by CLIENT SETINFO. */
    sds querybuf;           /* Buffer we use to accumulate client queries. */
    size_t qb_pos;          /* The position we have read in querybuf. */
    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size. */
    int argc;               /* Num of arguments of current command. */
    robj** argv;            /* Arguments of current command. */
    int argv_len;           /* Size of argv array (may be more than argc) */
    int original_argc;      /* Num of arguments of original command if arguments were rewritten. */
    robj** original_argv;   /* Arguments of original command if arguments were rewritten. */
    size_t argv_len_sum;    /* Sum of lengths of objects in argv list. */
    struct redisCommand* cmd, * lastcmd;  /* Last command executed. */
    struct redisCommand* realcmd; /* The original command that was executed by the client,
                                     Used to update error stats in case the c->cmd was modified
                                     during the command invocation (like on GEOADD for example). */
    user* user;             /* User associated with this connection. If the
                               user is set to NULL the connection can do
                               anything (admin). */
    int reqtype;            /* Request protocol type: PROTO_REQ_* */
    int multibulklen;       /* Number of multi bulk arguments left to read. */
    long bulklen;           /* Length of bulk argument in multi bulk request. */
    list* reply;            /* List of reply objects to send to the client. */
    unsigned long long reply_bytes; /* Tot bytes of objects in reply list. */
    list* deferred_reply_errors;    /* Used for module thread safe contexts. */
    size_t sentlen;         /* Amount of bytes already sent in the current
                               buffer or object being sent. */
    time_t ctime;           /* Client creation time. */
    long duration;          /* Current command duration. Used for measuring latency of blocking/non-blocking cmds */
    int slot;               /* The slot the client is executing against. Set to -1 if no slot is being used */
    dictEntry* cur_script;  /* Cached pointer to the dictEntry of the script being executed. */
    time_t lastinteraction; /* Time of the last interaction, used for timeout */
    time_t obuf_soft_limit_reached_time;
    int authenticated;      /* Needed when the default user requires auth. */
    int replstate;          /* Replication state if this is a slave. */
    int repl_start_cmd_stream_on_ack; /* Install slave write handler on first ACK. */
    int repldbfd;           /* Replication DB file descriptor. */
    off_t repldboff;        /* Replication DB file offset. */
    off_t repldbsize;       /* Replication DB file size. */
    sds replpreamble;       /* Replication DB preamble. */
    long long read_reploff; /* Read replication offset if this is a master. */
    long long reploff;      /* Applied replication offset if this is a master. */
    long long repl_applied; /* Applied replication data count in querybuf, if this is a replica. */
    long long repl_ack_off; /* Replication ack offset, if this is a slave. */
    long long repl_aof_off; /* Replication AOF fsync ack offset, if this is a slave. */
    long long repl_ack_time;/* Replication ack time, if this is a slave. */
    long long repl_last_partial_write; /* The last time the server did a partial write from the RDB child pipe to this replica  */
    long long psync_initial_offset; /* FULLRESYNC reply offset other slaves
                                       copying this slave output buffer
                                       should use. */
    char replid[CONFIG_RUN_ID_SIZE + 1]; /* Master replication ID (if master). */
    int slave_listening_port; /* As configured with: REPLCONF listening-port */
    char* slave_addr;       /* Optionally given by REPLCONF ip-address */
    int slave_capa;         /* Slave capabilities: SLAVE_CAPA_* bitwise OR. */
    int slave_req;          /* Slave requirements: SLAVE_REQ_* */
    multiState mstate;      /* MULTI/EXEC state */
    blockingState bstate;     /* blocking state */
    long long woff;         /* Last write global replication offset. */
    list* watched_keys;     /* Keys WATCHED for MULTI/EXEC CAS */
    dict* pubsub_channels;  /* channels a client is interested in (SUBSCRIBE) */
    dict* pubsub_patterns;  /* patterns a client is interested in (PSUBSCRIBE) */
    dict* pubsubshard_channels;  /* shard level channels a client is interested in (SSUBSCRIBE) */
    sds peerid;             /* Cached peer ID. */
    sds sockname;           /* Cached connection target address. */
    listNode* client_list_node; /* list node in client list */
    listNode* postponed_list_node; /* list node within the postponed list */
    listNode* pending_read_list_node; /* list node in clients pending read list */
    void* module_blocked_client; /* Pointer to the RedisModuleBlockedClient associated with this
                                  * client. This is set in case of module authentication before the
                                  * unblocked client is reprocessed to handle reply callbacks. */
    void* module_auth_ctx; /* Ongoing / attempted module based auth callback's ctx.
                            * This is only tracked within the context of the command attempting
                            * authentication. If not NULL, it means module auth is in progress. */
    RedisModuleUserChangedFunc auth_callback; /* Module callback to execute
                                               * when the authenticated user
                                               * changes. */
    void* auth_callback_privdata; /* Private data that is passed when the auth
                                   * changed callback is executed. Opaque for
                                   * Redis Core. */
    void* auth_module;      /* The module that owns the callback, which is used
                             * to disconnect the client if the module is
                             * unloaded for cleanup. Opaque for Redis Core.*/

                             /* If this client is in tracking mode and this field is non zero,
                              * invalidation messages for keys fetched by this client will be sent to
                              * the specified client ID. */
    uint64_t client_tracking_redirection;
    rax* client_tracking_prefixes; /* A dictionary of prefixes we are already
                                      subscribed to in BCAST mode, in the
                                      context of client side caching. */
                                      /* In updateClientMemoryUsage() we track the memory usage of
                                       * each client and add it to the sum of all the clients of a given type,
                                       * however we need to remember what was the old contribution of each
                                       * client, and in which category the client was, in order to remove it
                                       * before adding it the new value. */
    size_t last_memory_usage;
    int last_memory_type;

    listNode* mem_usage_bucket_node;
    clientMemUsageBucket* mem_usage_bucket;

    listNode* ref_repl_buf_node; /* Referenced node of replication buffer blocks,
                                  * see the definition of replBufBlock. */
    size_t ref_block_pos;        /* Access position of referenced buffer block,
                                  * i.e. the next offset to send. */

                                  /* list node in clients_pending_write list */
    listNode clients_pending_write_node;
    /* Response buffer */
    size_t buf_peak; /* Peak used size of buffer in last 5 sec interval. */
    mstime_t buf_peak_last_reset_time; /* keeps the last time the buffer peak value was reset */
    int bufpos;
    size_t buf_usable_size; /* Usable size of buffer. */
    char* buf;
#ifdef LOG_REQ_RES
    clientReqResInfo reqres;
#endif
} client;

/* ACL information */
typedef struct aclInfo {
    long long user_auth_failures; /* Auth failure counts on user level */
    long long invalid_cmd_accesses; /* Invalid command accesses that user doesn't have permission to */
    long long invalid_key_accesses; /* Invalid key accesses that user doesn't have permission to */
    long long invalid_channel_accesses; /* Invalid channel accesses that user doesn't have permission to */
} a