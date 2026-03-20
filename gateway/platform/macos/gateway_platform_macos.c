/**
 * gateway_platform_macos.c — macOS/POSIX implementation of gateway_platform.h
 *
 * Dependencies (install via Homebrew):
 *   brew install libwebsockets libcurl cjson
 *
 * WebSocket: libwebsockets (lws) in callback-driven mode, run from a
 *   background pthread.  Received frames are posted to a POSIX message queue
 *   that platform_ws_service() drains on the gateway core's 100 ms cadence.
 *
 * HTTP:  libcurl (synchronous, blocking per-request).
 * TCP:   BSD sockets (POSIX, same API as lwIP on ESP32).
 * Time:  clock_gettime(CLOCK_MONOTONIC).
 */

#include "gateway_platform.h"
#include "gateway_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <mach/mach_time.h>

// libwebsockets
#include <libwebsockets.h>

// libcurl
#include <curl/curl.h>

// ── Logging ──────────────────────────────────────────────────────────────────

static const char *level_str[] = { "E", "W", "I", "D" };

void platform_log(int level, const char *tag, const char *fmt, ...)
{
    if (level < 0 || level > 3) level = 0;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

    fprintf(level == 0 ? stderr : stdout,
            "[%s.%03ld] %s (%s): ",
            timebuf, ts.tv_nsec / 1000000L,
            level_str[level], tag);

    va_list args;
    va_start(args, fmt);
    vfprintf(level == 0 ? stderr : stdout, fmt, args);
    va_end(args);

    fputc('\n', level == 0 ? stderr : stdout);
    fflush(level == 0 ? stderr : stdout);
}

// ── Timing & system ──────────────────────────────────────────────────────────

int64_t platform_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

void platform_delay_ms(uint32_t ms)
{
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000L,
    };
    nanosleep(&ts, NULL);
}

size_t platform_get_free_heap(void)
{
    // Not meaningful on macOS; return a large constant
    return (size_t)256 * 1024 * 1024;
}

void platform_reboot(void)
{
    // On macOS just exit — the process supervisor (launchd, shell loop) restarts
    exit(0);
}

// ── Network identity ─────────────────────────────────────────────────────────

// Find the primary non-loopback IPv4 interface
static bool get_primary_if(char *ip_out, size_t ip_sz,
                            char *mask_out, size_t mask_sz,
                            char *name_out, size_t name_sz)
{
    struct ifaddrs *ifa_list = NULL;
    if (getifaddrs(&ifa_list) != 0) return false;

    bool found = false;
    for (struct ifaddrs *ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        if (ip_out)   inet_ntop(AF_INET, &sa->sin_addr,  ip_out,   ip_sz);
        if (mask_out && ifa->ifa_netmask) {
            struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;
            inet_ntop(AF_INET, &nm->sin_addr, mask_out, mask_sz);
        }
        if (name_out) strncpy(name_out, ifa->ifa_name, name_sz - 1);
        found = true;
        break;
    }

    freeifaddrs(ifa_list);
    return found;
}

void platform_get_mac_str(char *buf, size_t buf_size)
{
    // macOS: read MAC via ifaddrs AF_LINK
    struct ifaddrs *ifa_list = NULL;
    if (getifaddrs(&ifa_list) != 0) {
        strncpy(buf, "00:00:00:00:00:00", buf_size);
        return;
    }

    char ifname[64] = "";
    get_primary_if(NULL, 0, NULL, 0, ifname, sizeof(ifname));

    bool found = false;
    for (struct ifaddrs *ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_LINK) continue;
        if (strcmp(ifa->ifa_name, ifname) != 0) continue;

        struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
        if (sdl->sdl_alen == 6) {
            unsigned char *mac = (unsigned char *)LLADDR(sdl);
            snprintf(buf, buf_size,
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            found = true;
        }
        break;
    }

    freeifaddrs(ifa_list);
    if (!found) strncpy(buf, "00:00:00:00:00:00", buf_size);
}

void platform_get_local_ip(char *buf, size_t buf_size)
{
    if (!get_primary_if(buf, buf_size, NULL, 0, NULL, 0))
        strncpy(buf, "127.0.0.1", buf_size);
}

void platform_get_netmask(char *buf, size_t buf_size)
{
    if (!get_primary_if(NULL, 0, buf, buf_size, NULL, 0))
        strncpy(buf, "255.255.255.0", buf_size);
}

void platform_get_hostname(char *buf, size_t buf_size)
{
    if (gethostname(buf, buf_size) != 0)
        strncpy(buf, "gateway-mac", buf_size);
    buf[buf_size - 1] = '\0';
}

// ── ARP ──────────────────────────────────────────────────────────────────────

/* Normalize a MAC string to uppercase colon form for comparison. */
static void normalize_mac(const char *in, char *out, size_t out_size)
{
    int fields[6] = {0};
    if (sscanf(in, "%x:%x:%x:%x:%x:%x",
               &fields[0], &fields[1], &fields[2],
               &fields[3], &fields[4], &fields[5]) == 6) {
        snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
                 fields[0], fields[1], fields[2],
                 fields[3], fields[4], fields[5]);
    } else {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

/* Parse one line of `arp -an` output: "? (IP) at MAC on iface ..."
 * Returns true and fills ip_out / mac_out if the line is valid. */
static bool parse_arp_line(const char *line, char *ip_out, char *mac_out)
{
    /* Expected: ? (192.168.1.1) at aa:bb:cc:dd:ee:ff on en0 ... */
    const char *lp = strchr(line, '(');
    const char *rp = lp ? strchr(lp, ')') : NULL;
    if (!lp || !rp) return false;

    int iplen = (int)(rp - lp - 1);
    if (iplen <= 0 || iplen >= 16) return false;
    memcpy(ip_out, lp + 1, iplen);
    ip_out[iplen] = '\0';

    const char *at = strstr(rp, " at ");
    if (!at) return false;
    at += 4;

    /* Skip "incomplete" entries */
    if (strncmp(at, "(incomplete)", 12) == 0) return false;

    int mlen = 0;
    while (at[mlen] && at[mlen] != ' ' && at[mlen] != '\n') mlen++;
    if (mlen < 11 || mlen >= 18) return false;
    memcpy(mac_out, at, mlen);
    mac_out[mlen] = '\0';
    return true;
}

bool platform_arp_mac_to_ip(const char *mac, char *ip_buf, size_t ip_buf_size)
{
    char norm_target[18];
    normalize_mac(mac, norm_target, sizeof(norm_target));

    FILE *fp = popen("arp -an 2>/dev/null", "r");
    if (!fp) return false;

    char line[256], ip[16], entry_mac[18], norm_entry[18];
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        if (!parse_arp_line(line, ip, entry_mac)) continue;
        normalize_mac(entry_mac, norm_entry, sizeof(norm_entry));
        if (strcmp(norm_entry, norm_target) == 0) {
            strncpy(ip_buf, ip, ip_buf_size - 1);
            ip_buf[ip_buf_size - 1] = '\0';
            found = true;
            break;
        }
    }
    pclose(fp);
    return found;
}

bool platform_arp_ip_to_mac(const char *ip, char *mac_buf, size_t mac_buf_size)
{
    /* Ping first to ensure an ARP entry exists */
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "ping -c1 -t1 %s > /dev/null 2>&1", ip);
    system(cmd);

    snprintf(cmd, sizeof(cmd), "arp -n %s 2>/dev/null", ip);
    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    char line[256], entry_ip[16], entry_mac[18];
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        if (!parse_arp_line(line, entry_ip, entry_mac)) continue;
        if (strcmp(entry_ip, ip) == 0) {
            char norm[18];
            normalize_mac(entry_mac, norm, sizeof(norm));
            strncpy(mac_buf, norm, mac_buf_size - 1);
            mac_buf[mac_buf_size - 1] = '\0';
            found = true;
            break;
        }
    }
    pclose(fp);
    return found;
}

// ── Memory ───────────────────────────────────────────────────────────────────

void *platform_malloc_large(size_t size)
{
    return malloc(size);
}

// ── WebSocket (libwebsockets) ─────────────────────────────────────────────────
//
// Architecture:
//   - A background pthread runs the lws event loop.
//   - The LWS callback posts events to a mutex-protected ring buffer.
//   - platform_ws_service() (called from the gateway core at 100 ms) drains
//     the ring buffer and dispatches to gateway_core_on_*().

#define LWS_RING_CAP    64
#define LWS_MSG_MAX_LEN 65536

typedef enum {
    LWS_GW_EVT_CONNECTED,
    LWS_GW_EVT_DISCONNECTED,
    LWS_GW_EVT_MESSAGE,
} lws_gw_evt_type_t;

typedef struct {
    lws_gw_evt_type_t type;
    char             *data;
    int               len;
} lws_gw_evt_t;

// Ring buffer for events from LWS thread → gateway core thread
static lws_gw_evt_t  s_ring[LWS_RING_CAP];
static int           s_ring_head = 0;
static int           s_ring_tail = 0;
static pthread_mutex_t s_ring_mutex = PTHREAD_MUTEX_INITIALIZER;

// Send queue: main thread → LWS service thread
// lws_write() MUST only be called from within the LWS service thread.
// Outgoing messages are queued here; LWS_CALLBACK_CLIENT_WRITEABLE drains them.
#define LWS_SEND_QUEUE_CAP 32
typedef struct {
    unsigned char *buf;  // malloc'd with LWS_PRE prefix
    int            len;
} lws_send_item_t;
static lws_send_item_t s_send_q[LWS_SEND_QUEUE_CAP];
static int             s_send_head = 0;
static int             s_send_tail = 0;
static pthread_mutex_t s_send_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile int       s_pong_pending   = 0;   // set from LWS thread, drained in WRITEABLE
static volatile bool      s_lws_connected  = false;
static volatile bool      s_lws_stop       = false;
static struct lws_context *s_lws_ctx       = NULL;
static struct lws         *s_lws_wsi       = NULL;
static pthread_t           s_lws_thread;
static char                s_lws_url[512]  = "";

// Fragment reassembly buffer
static char  *s_frag_buf  = NULL;
static int    s_frag_len  = 0;
static int    s_frag_cap  = 0;

static void ring_push(lws_gw_evt_t *evt)
{
    pthread_mutex_lock(&s_ring_mutex);
    int next = (s_ring_head + 1) % LWS_RING_CAP;
    if (next == s_ring_tail) {
        // Ring full — drop oldest (disconnect/message)
        if (s_ring[s_ring_tail].data) free(s_ring[s_ring_tail].data);
        s_ring_tail = (s_ring_tail + 1) % LWS_RING_CAP;
    }
    s_ring[s_ring_head] = *evt;
    s_ring_head = next;
    pthread_mutex_unlock(&s_ring_mutex);
}

static bool ring_pop(lws_gw_evt_t *out)
{
    pthread_mutex_lock(&s_ring_mutex);
    if (s_ring_head == s_ring_tail) {
        pthread_mutex_unlock(&s_ring_mutex);
        return false;
    }
    *out = s_ring[s_ring_tail];
    s_ring_tail = (s_ring_tail + 1) % LWS_RING_CAP;
    pthread_mutex_unlock(&s_ring_mutex);
    return true;
}

static int lws_callback(struct lws *wsi,
                         enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    (void)user;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        s_lws_connected = true;
        s_lws_wsi       = wsi;
        {
            lws_gw_evt_t e = { .type = LWS_GW_EVT_CONNECTED };
            ring_push(&e);
        }
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        // Priority: send PONG before anything else in the queue
        if (s_pong_pending) {
            s_pong_pending = 0;
            static unsigned char pong_buf[LWS_PRE + 4];
            memcpy(pong_buf + LWS_PRE, "PONG", 4);
            GW_LOGI("gw_ws", "TX (4): PONG");
            lws_write(wsi, pong_buf + LWS_PRE, 4, LWS_WRITE_TEXT);
            lws_callback_on_writable(wsi); // drain normal queue next
            break;
        }

        // Drain one item from the send queue (service thread context — safe to lws_write)
        pthread_mutex_lock(&s_send_mutex);
        if (s_send_head != s_send_tail) {
            lws_send_item_t item = s_send_q[s_send_tail];
            s_send_tail = (s_send_tail + 1) % LWS_SEND_QUEUE_CAP;
            int more = (s_send_head != s_send_tail);
            pthread_mutex_unlock(&s_send_mutex);

            GW_LOGI("gw_ws", "TX (%d): %.*s", item.len, item.len, (char *)(item.buf + LWS_PRE));
            lws_write(wsi, item.buf + LWS_PRE, item.len, LWS_WRITE_TEXT);
            free(item.buf);

            if (more) lws_callback_on_writable(wsi);
        } else {
            pthread_mutex_unlock(&s_send_mutex);
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    case LWS_CALLBACK_CLIENT_CLOSED:
        s_lws_connected = false;
        s_lws_wsi       = NULL;
        {
            lws_gw_evt_t e = { .type = LWS_GW_EVT_DISCONNECTED };
            ring_push(&e);
        }
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        // Accumulate fragments into s_frag_buf
        int remaining = (int)lws_remaining_packet_payload(wsi);
        int is_final  = lws_is_final_fragment(wsi);

        // Grow reassembly buffer if needed
        int needed = s_frag_len + (int)len + 1;
        if (needed > s_frag_cap) {
            int new_cap = needed * 2;
            if (new_cap > LWS_MSG_MAX_LEN) new_cap = LWS_MSG_MAX_LEN;
            char *nb = realloc(s_frag_buf, new_cap);
            if (!nb) break;
            s_frag_buf = nb;
            s_frag_cap = new_cap;
        }

        int copy = (int)len;
        if (s_frag_len + copy >= s_frag_cap) copy = s_frag_cap - s_frag_len - 1;
        memcpy(s_frag_buf + s_frag_len, in, copy);
        s_frag_len += copy;
        s_frag_buf[s_frag_len] = '\0';

        if (is_final && remaining == 0) {
            GW_LOGI("gw_ws", "RX (%d): %.*s", s_frag_len, s_frag_len, s_frag_buf);

            // Handle PING inline — don't route through ring buffer or main thread.
            // During scans the main thread may be blocked for seconds; replying
            // here keeps us well within the 5 s server deadline.
            if (s_frag_len == 4 && memcmp(s_frag_buf, "PING", 4) == 0) {
                s_pong_pending = 1;
                lws_callback_on_writable(wsi);
                s_frag_len = 0;
                break;
            }

            char *copy_buf = malloc(s_frag_len + 1);
            if (copy_buf) {
                memcpy(copy_buf, s_frag_buf, s_frag_len + 1);
                lws_gw_evt_t e = {
                    .type = LWS_GW_EVT_MESSAGE,
                    .data = copy_buf,
                    .len  = s_frag_len,
                };
                ring_push(&e);
            }
            s_frag_len = 0;
        }
        (void)remaining;
        break;
    }

    default:
        break;
    }

    return 0;
}

static const struct lws_protocols s_protocols[] = {
    { "gateway-trpc", lws_callback, 0, LWS_MSG_MAX_LEN, 0, NULL, 0 },
    LWS_PROTOCOL_LIST_TERM,
};

static void *lws_thread_fn(void *arg)
{
    (void)arg;
    while (!s_lws_stop && s_lws_ctx) {
        lws_service(s_lws_ctx, 50);
        /* After each service cycle, schedule a writable callback if messages
         * are queued. This is the reliable cross-thread send pattern for lws —
         * lws_callback_on_writable() called from within the service thread. */
        if (s_lws_wsi && s_lws_connected) {
            pthread_mutex_lock(&s_send_mutex);
            int has_pending = (s_send_head != s_send_tail);
            pthread_mutex_unlock(&s_send_mutex);
            if (has_pending) lws_callback_on_writable(s_lws_wsi);
        }
    }
    return NULL;
}

// Parse wss://host[:port]/path into components
static void parse_ws_url(const char *url, bool *out_ssl,
                          char *out_host, int host_sz,
                          int *out_port,
                          char *out_path, int path_sz)
{
    *out_ssl  = (strncmp(url, "wss://", 6) == 0);
    *out_port = *out_ssl ? 443 : 80;

    const char *after_scheme = url + (*out_ssl ? 6 : 5); // skip wss:// or ws://
    const char *slash = strchr(after_scheme, '/');
    const char *query = strchr(after_scheme, '?');
    const char *colon = strchr(after_scheme, ':');

    // Determine where the host ends (stop at slash, query, or end of string)
    const char *host_end = slash ? slash : (query ? query : after_scheme + strlen(after_scheme));
    if (colon && colon < host_end) {
        // There's a port
        int hlen = (int)(colon - after_scheme);
        if (hlen >= host_sz) hlen = host_sz - 1;
        strncpy(out_host, after_scheme, hlen);
        out_host[hlen] = '\0';
        *out_port = atoi(colon + 1);
    } else {
        int hlen = (int)(host_end - after_scheme);
        if (hlen >= host_sz) hlen = host_sz - 1;
        strncpy(out_host, after_scheme, hlen);
        out_host[hlen] = '\0';
    }

    if (slash) {
        // Path present — includes any query string that follows
        strncpy(out_path, slash, path_sz - 1);
        out_path[path_sz - 1] = '\0';
    } else if (query) {
        // No path, but query string present: synthesise "/<query>"
        snprintf(out_path, path_sz, "/%s", query);
    } else {
        strncpy(out_path, "/", path_sz - 1);
    }
}

void platform_ws_connect(const char *url)
{
    strncpy(s_lws_url, url, sizeof(s_lws_url) - 1);
    s_lws_stop      = false;
    s_lws_connected = false;
    s_frag_len      = 0;

    bool use_ssl;
    char host[256] = "";
    char path[512] = "";
    int  port;
    parse_ws_url(url, &use_ssl, host, sizeof(host), &port, path, sizeof(path));

    // Suppress libwebsockets log noise (demo plugin warnings, etc.)
    lws_set_log_level(LLL_ERR, NULL);

    // lws opens /dev/urandom for entropy; if stdin (fd 0) is closed, open()
    // returns 0 and lws prints "ZERO RANDOM FD" then fails SSL init.
    // Guard against this by ensuring fd 0 is occupied.
    {
        struct stat st;
        if (fstat(STDIN_FILENO, &st) == -1 && errno == EBADF) {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull > 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }
    }

    struct lws_context_creation_info ctx_info = { 0 };
    ctx_info.port      = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = s_protocols;
    if (use_ssl) {
        ctx_info.options         = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        ctx_info.ssl_ca_filepath = "/etc/ssl/cert.pem";  // macOS system CA bundle
    }

    s_lws_ctx = lws_create_context(&ctx_info);
    if (!s_lws_ctx) {
        GW_LOGE("gw_ws", "Failed to create lws context");
        return;
    }

    struct lws_client_connect_info conn_info = { 0 };
    conn_info.context        = s_lws_ctx;
    conn_info.address        = host;
    conn_info.port           = port;
    conn_info.path           = path;
    conn_info.host           = host;
    conn_info.origin         = host;
    conn_info.protocol       = s_protocols[0].name;
    conn_info.ssl_connection = use_ssl
        ? (LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK)
        : 0;

    s_lws_wsi = lws_client_connect_via_info(&conn_info);
    if (!s_lws_wsi) {
        GW_LOGE("gw_ws", "lws_client_connect_via_info failed");
    }

    pthread_create(&s_lws_thread, NULL, lws_thread_fn, NULL);
}

void platform_ws_disconnect(void)
{
    s_lws_stop = true;
    if (s_lws_wsi) {
        lws_set_timeout(s_lws_wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
        s_lws_wsi = NULL;
    }
    pthread_join(s_lws_thread, NULL);

    if (s_lws_ctx) {
        lws_context_destroy(s_lws_ctx);
        s_lws_ctx = NULL;
    }
    s_lws_connected = false;

    // Drain receive ring
    lws_gw_evt_t e;
    while (ring_pop(&e)) {
        if (e.data) free(e.data);
    }

    // Drain send queue
    pthread_mutex_lock(&s_send_mutex);
    while (s_send_head != s_send_tail) {
        free(s_send_q[s_send_tail].buf);
        s_send_tail = (s_send_tail + 1) % LWS_SEND_QUEUE_CAP;
    }
    pthread_mutex_unlock(&s_send_mutex);

    if (s_frag_buf) {
        free(s_frag_buf);
        s_frag_buf = NULL;
        s_frag_len = 0;
        s_frag_cap = 0;
    }
}

bool platform_ws_is_connected(void)
{
    return s_lws_connected;
}

// Queue a message for sending; the LWS service thread delivers it via
// LWS_CALLBACK_CLIENT_WRITEABLE. Safe to call from any thread.
void platform_ws_send(const char *data, int len)
{
    if (!s_lws_wsi || !s_lws_connected) return;

    unsigned char *buf = malloc(LWS_PRE + len);
    if (!buf) return;
    memcpy(buf + LWS_PRE, data, len);

    pthread_mutex_lock(&s_send_mutex);
    int next = (s_send_head + 1) % LWS_SEND_QUEUE_CAP;
    if (next == s_send_tail) {
        // Queue full — drop oldest
        free(s_send_q[s_send_tail].buf);
        s_send_tail = (s_send_tail + 1) % LWS_SEND_QUEUE_CAP;
    }
    s_send_q[s_send_head] = (lws_send_item_t){ .buf = buf, .len = len };
    s_send_head = next;
    pthread_mutex_unlock(&s_send_mutex);

    // Wake the LWS service thread. LWS_CALLBACK_EVENT_WAIT_CANCELLED will fire
    // inside the service loop and call lws_callback_on_writable() from there —
    // the correct cross-thread pattern for libwebsockets.
    lws_cancel_service(s_lws_ctx);
}

void platform_ws_service(void)
{
    lws_gw_evt_t e;
    while (ring_pop(&e)) {
        switch (e.type) {
        case LWS_GW_EVT_CONNECTED:
            gateway_core_on_ws_connected();
            break;
        case LWS_GW_EVT_DISCONNECTED:
            gateway_core_on_ws_disconnected();
            break;
        case LWS_GW_EVT_MESSAGE:
            if (e.data) {
                gateway_core_on_message(e.data, e.len);
                free(e.data);
            }
            break;
        }
    }
}

// ── HTTP (libcurl) ────────────────────────────────────────────────────────────

typedef struct {
    char *buf;
    int   len;
    int   cap;
} curl_resp_t;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_resp_t *r    = (curl_resp_t *)userdata;
    size_t       bytes = size * nmemb;

    if (r->len + (int)bytes < r->cap) {
        memcpy(r->buf + r->len, ptr, bytes);
        r->len += (int)bytes;
        r->buf[r->len] = '\0';
    }
    return bytes; // always consume, even if buffer full (prevents curl error)
}

int platform_http_request(const char *url,
                           const char *method,
                           const char *body,
                           char *resp_buf, int buf_size,
                           int timeout_ms)
{
    if (resp_buf && buf_size > 0) resp_buf[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_resp_t resp = { .buf = resp_buf, .len = 0, .cap = buf_size };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // dev convenience

    struct curl_slist *headers = NULL;

    if (method && strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
        headers = curl_slist_append(headers, "Content-Type: application/json");
    } else if (method && strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
        headers = curl_slist_append(headers, "Content-Type: application/json");
    } else if (method && strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }
    // GET is the default

    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    long http_code = -1;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (int)http_code;
}

// ── TCP ──────────────────────────────────────────────────────────────────────

bool platform_tcp_send_recv(const char *ip, int port,
                             const char *send_buf,
                             char *recv_buf, int recv_size,
                             int timeout_ms)
{
    struct sockaddr_in dest = { 0 };
    dest.sin_family = AF_INET;
    dest.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &dest.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    int secs  = timeout_ms / 1000;
    int usecs = (timeout_ms % 1000) * 1000;
    struct timeval tv = { .tv_sec = secs ? secs : 1, .tv_usec = usecs };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        close(sock);
        return false;
    }

    ssize_t sent = send(sock, send_buf, strlen(send_buf), 0);
    if (sent < 0) {
        close(sock);
        return false;
    }

    int total = 0;
    while (total < recv_size - 1) {
        int r = (int)recv(sock, recv_buf + total, recv_size - 1 - total, 0);
        if (r <= 0) break;
        total += r;
    }
    recv_buf[total] = '\0';
    close(sock);
    return total > 0;
}

// ── TCP probe ─────────────────────────────────────────────────────────────────

bool platform_tcp_probe(const char *ip, int port, int timeout_ms)
{
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &dest.sin_addr) != 1) return false;

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) return false;

    /* Non-blocking connect + select */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    connect(sock, (struct sockaddr *)&dest, sizeof(dest)); /* EINPROGRESS expected */

    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    if (tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_sec = 1;

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    bool connected = false;
    if (select(sock + 1, NULL, &wfds, NULL, &tv) == 1) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
        connected = (err == 0);
    }
    close(sock);
    return connected;
}
