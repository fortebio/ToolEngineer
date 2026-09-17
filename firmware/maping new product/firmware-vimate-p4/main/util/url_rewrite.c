#include "url_rewrite.h"
#include "vimate.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static bool split_url(const char *url,
                      const char **host, size_t *host_len,
                      const char **path) {
    if (!url || !host || !host_len || !path) return false;
    const char *scheme = strstr(url, "://");
    if (!scheme) return false;
    const char *h = scheme + 3;
    const char *p = strchr(h, '/');
    if (!p) return false;

    const char *host_end = p;
    const char *colon = memchr(h, ':', (size_t)(host_end - h));
    if (colon) host_end = colon;

    *host = h;
    *host_len = (size_t)(host_end - h);
    *path = p;
    return *host_len > 0;
}

static bool is_loopback_host(const char *host, size_t host_len) {
    return (host_len == 9 && strncasecmp(host, "localhost", 9) == 0) ||
           (host_len == 9 && strncmp(host, "127.0.0.1", 9) == 0) ||
           (host_len == 7 && strncmp(host, "0.0.0.0", 7) == 0);
}

static bool server_origin(char *out, size_t out_size) {
    const char *base = g_vimate_server.base_url;
    if (!base || !base[0] || !out || out_size == 0) return false;

    const char *scheme = strstr(base, "://");
    if (!scheme) return false;
    const char *h = scheme + 3;
    const char *slash = strchr(h, '/');
    size_t n = slash ? (size_t)(slash - base) : strlen(base);
    if (n == 0 || n >= out_size) return false;
    memcpy(out, base, n);
    out[n] = '\0';
    return true;
}

const char *url_rewrite_localhost_media(const char *url, char *out, size_t out_size) {
    const char *host = NULL;
    const char *path = NULL;
    size_t host_len = 0;
    if (!split_url(url, &host, &host_len, &path)) return url;
    if (!is_loopback_host(host, host_len)) return url;
    if (strncmp(path, "/media/", 7) != 0) return url;

    char origin[128];
    if (!server_origin(origin, sizeof(origin))) return url;
    int n = snprintf(out, out_size, "%s%s", origin, path);
    if (n <= 0 || n >= (int)out_size) return url;
    ESP_LOGW(TAG_UI, "rewrite media URL for device: %s", out);
    return out;
}
