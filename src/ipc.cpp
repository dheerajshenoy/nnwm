#include "ipc.hpp"
#include "nnwm.hpp"

extern "C" {
#include <lua5.4/lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ---- Per-client state ---- */

struct ipc_client
{
    int fd;
    nnwm_server *server;
    struct wl_event_source *read_source;
    struct wl_event_source *write_source;
    char read_buf[64 * 1024];
    int read_len;
    char write_buf[256 * 1024];
    int write_len;
    int write_off;
};

/* ---- Write-side handling ---- */

static void flush_write(ipc_client *cl)
{
    while (cl->write_off < cl->write_len)
    {
        ssize_t n = write(cl->fd, cl->write_buf + cl->write_off,
                          cl->write_len - cl->write_off);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            goto fail;
        }
        if (n == 0) goto fail;
        cl->write_off += n;
    }
    /* All written — disarm write watcher, rearm read watcher */
    cl->write_len = 0;
    cl->write_off = 0;
    wl_event_source_remove(cl->write_source);
    cl->write_source = nullptr;
    /* Re-arm read */
    return;

fail:
    /* Client gone — tear down */
    wl_event_source_remove(cl->read_source);
    cl->read_source = nullptr;
    if (cl->write_source)
    {
        wl_event_source_remove(cl->write_source);
        cl->write_source = nullptr;
    }
    close(cl->fd);
    delete cl;
}

static void schedule_write(ipc_client *cl)
{
    if (cl->write_source) return; /* already armed */
    cl->write_source = wl_event_loop_add_fd(
        wl_display_get_event_loop(cl->server->wl_display),
        cl->fd, WL_EVENT_WRITABLE,
        [](int /*fd*/, uint32_t /*mask*/, void *data) -> int
        {
            flush_write(static_cast<ipc_client *>(data));
            return 0;
        }, cl);
}

static void reply(ipc_client *cl, bool ok, const char *payload)
{
    int n = snprintf(cl->write_buf, sizeof(cl->write_buf),
                     "%s\n", payload);
    if (n < 0 || n >= (int)sizeof(cl->write_buf))
        return;
    cl->write_len = n;
    cl->write_off = 0;
    schedule_write(cl);
}

/* ---- Lua execution ---- */

static bool exec_lua(lua_State *L, const char *code, char *out, size_t out_size)
{
    /* Wrap code in a function so 'return' works at top level */
    char chunk[128 * 1024];
    snprintf(chunk, sizeof(chunk), "return (function() %s end)()", code);

    if (luaL_loadstring(L, chunk) != LUA_OK)
    {
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"%s\"}",
                 lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        const char *err = lua_tostring(L, -1);
        snprintf(out, out_size, "{\"ok\":false,\"error\":\"%s\"}",
                 err ? err : "unknown error");
        lua_pop(L, 1);
        return false;
    }

    /* Serialize result to string */
    const char *str = nullptr;
    size_t len = 0;

    int t = lua_type(L, -1);
    switch (t)
    {
    case LUA_TNIL:
        snprintf(out, out_size, "{\"ok\":true,\"data\":null}");
        break;
    case LUA_TBOOLEAN:
        snprintf(out, out_size, "{\"ok\":true,\"data\":%s}",
                 lua_toboolean(L, -1) ? "true" : "false");
        break;
    case LUA_TNUMBER:
        snprintf(out, out_size, "{\"ok\":true,\"data\":%s}",
                 lua_tostring(L, -1));
        break;
    case LUA_TSTRING:
        str = lua_tolstring(L, -1, &len);
        /* Escape for JSON */
        {
            char *dst = out;
            char *end = out + out_size;
            int wrote = snprintf(dst, end - dst, "{\"ok\":true,\"data\":\"");
            dst += wrote;
            if (dst < end)
            {
                for (size_t i = 0; i < len && dst < end - 8; i++)
                {
                    char c = str[i];
                    if (c == '"' || c == '\\')
                    {
                        *dst++ = '\\';
                        *dst++ = c;
                    }
                    else if (c == '\n')
                    {
                        *dst++ = '\\';
                        *dst++ = 'n';
                    }
                    else if (c == '\t')
                    {
                        *dst++ = '\\';
                        *dst++ = 't';
                    }
                    else if (c == '\r')
                    {
                        *dst++ = '\\';
                        *dst++ = 'r';
                    }
                    else if ((unsigned char)c < 0x20)
                    {
                        dst += snprintf(dst, end - dst, "\\u%04x", (unsigned)c);
                    }
                    else
                    {
                        *dst++ = c;
                    }
                }
                if (dst < end)
                    dst += snprintf(dst, end - dst, "\"}");
            }
        }
        break;
    case LUA_TTABLE:
        /* Pretty-print tables as JSON-ish via Lua serialization */
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, -2);
        lua_call(L, 1, 1);
        str = lua_tostring(L, -1);
        if (str)
        {
            snprintf(out, out_size, "{\"ok\":true,\"data\":\"%s\"}",
                     str);
        }
        else
        {
            snprintf(out, out_size,
                     "{\"ok\":true,\"data\":\"<table>\"}");
        }
        lua_pop(L, 2); /* tostring result + table */
        break;
    default:
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, -2);
        lua_call(L, 1, 1);
        str = lua_tostring(L, -1);
        snprintf(out, out_size, "{\"ok\":true,\"data\":\"%s\"}",
                 str ? str : "<unknown>");
        lua_pop(L, 2);
        break;
    }

    lua_pop(L, 1); /* pop result */
    return true;
}

/* ---- Read-side handling ---- */

static void handle_client_data(ipc_client *cl)
{
    ssize_t n = read(cl->fd, cl->read_buf + cl->read_len,
                     sizeof(cl->read_buf) - cl->read_len - 1);
    if (n <= 0)
    {
        /* Disconnect or error */
        wl_event_source_remove(cl->read_source);
        cl->read_source = nullptr;
        if (cl->write_source)
        {
            wl_event_source_remove(cl->write_source);
            cl->write_source = nullptr;
        }
        close(cl->fd);
        delete cl;
        return;
    }

    cl->read_len += n;
    cl->read_buf[cl->read_len] = '\0';

    /* Process complete lines */
    char *start = cl->read_buf;
    char *nl;
    while ((nl = strchr(start, '\n')) != nullptr)
    {
        *nl = '\0';

        /* Skip empty lines */
        if (nl > start)
        {
            char result[256 * 1024];
            exec_lua(cl->server->lua, start, result, sizeof(result));

            /* If the client already has pending writes, append */
            if (cl->write_len + strlen(result) + 1 < sizeof(cl->write_buf))
            {
                memcpy(cl->write_buf + cl->write_len, result, strlen(result));
                cl->write_len += strlen(result);
                cl->write_buf[cl->write_len++] = '\n';
                schedule_write(cl);
            }
            else
            {
                reply(cl, true, result);
            }
        }

        start = nl + 1;
    }

    /* Shift unprocessed data to front */
    cl->read_len = strlen(start);
    if (cl->read_len > 0)
        memmove(cl->read_buf, start, cl->read_len);
}

/* ---- New client acceptance ---- */

static void accept_ipc_client(nnwm_server *server)
{
    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    int client_fd = accept4(server->ipc_fd, (struct sockaddr *)&addr, &len,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) return;

    auto *cl = new ipc_client{};
    cl->fd = client_fd;
    cl->server = server;

    cl->read_source = wl_event_loop_add_fd(
        wl_display_get_event_loop(server->wl_display),
        client_fd, WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void *data) -> int
        {
            handle_client_data(static_cast<ipc_client *>(data));
            return 0;
        }, cl);
}

/* ---- Public API ---- */

int ipc_init(nnwm_server *server, struct wl_event_loop *loop)
{
    server->ipc_fd = -1;
    server->ipc_event_source = nullptr;

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !runtime[0])
    {
        std::fprintf(stderr, "nnwm: XDG_RUNTIME_DIR not set, IPC disabled\n");
        return -1;
    }

    /* Build socket path */
    char path[256];
    snprintf(path, sizeof(path), "%s/nnwm-ipc.sock", runtime);

    /* Remove stale socket */
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        std::fprintf(stderr, "nnwm: IPC socket() failed: %s\n",
                     strerror(errno));
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::fprintf(stderr, "nnwm: IPC bind() failed: %s\n",
                     strerror(errno));
        close(fd);
        return -1;
    }

    /* Owner-only permissions */
    chmod(path, 0600);

    if (listen(fd, 4) < 0)
    {
        std::fprintf(stderr, "nnwm: IPC listen() failed: %s\n",
                     strerror(errno));
        close(fd);
        unlink(path);
        return -1;
    }

    server->ipc_fd = fd;
    server->ipc_event_source = wl_event_loop_add_fd(
        loop, fd, WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void *data) -> int
        {
            accept_ipc_client(static_cast<nnwm_server *>(data));
            return 0;
        }, server);

    /* Publish socket path so nnwmctl can find it */
    setenv("NNWM_SOCKET", path, 1);

    return 0;
}

void ipc_fini(nnwm_server *server)
{
    if (server->ipc_event_source)
    {
        wl_event_source_remove(server->ipc_event_source);
        server->ipc_event_source = nullptr;
    }
    if (server->ipc_fd >= 0)
    {
        close(server->ipc_fd);
        server->ipc_fd = -1;
    }

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0])
    {
        char path[256];
        snprintf(path, sizeof(path), "%s/nnwm-ipc.sock", runtime);
        unlink(path);
    }
}
