#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* ---- Socket helpers ---- */

static const char *find_socket(void)
{
    const char *s = getenv("NNWM_SOCKET");
    if (s && s[0]) return s;

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0])
    {
        static char path[256];
        snprintf(path, sizeof(path), "%s/nnwm-ipc.sock", runtime);
        return path;
    }
    return nullptr;
}

static int ipc_connect(const char *sock_path, const char *prog)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        std::fprintf(stderr, "%s: socket: %s\n", prog, strerror(errno));
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::fprintf(stderr, "%s: connect %s: %s\n", prog,
                     sock_path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static bool ipc_send_and_recv(int fd, const char *code, char *buf,
                               size_t buf_size)
{
    size_t len = strlen(code);
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = write(fd, code + sent, len - sent);
        if (n < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        sent += n;
    }

    shutdown(fd, SHUT_WR);

    size_t rlen = 0;
    for (;;)
    {
        ssize_t n = read(fd, buf + rlen, buf_size - rlen - 1);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        rlen += n;
    }
    buf[rlen] = '\0';

    /* Strip trailing newlines */
    while (rlen > 0 && (buf[rlen - 1] == '\n' || buf[rlen - 1] == '\r'))
        buf[--rlen] = '\0';

    return true;
}

/* ---- Lua code generation ---- */

static char lua_buf[256 * 1024];

static void emit(const char *fmt, ...)
        __attribute__((format(printf, 1, 2)));

static void emit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lua_buf, sizeof(lua_buf), fmt, ap);
    va_end(ap);
}

/* ---- Usage ---- */

static void usage(const char *prog)
{
    std::fprintf(stderr,
        "Usage: %s [-s SOCKET] COMMAND [ARGS...]\n"
        "\n"
        "Commands:\n"
        "  workspace <n>          Switch to workspace n\n"
        "  move-to-workspace <n>  Move focused window to workspace n\n"
        "  focus <dir>            Focus in direction (left|right|up|down)\n"
        "  swap <dir>             Swap in direction (left|right)\n"
        "  layout <action>        Layout operations:\n"
        "                           next, prev\n"
        "                           tile, tabbed, float\n"
        "                           vertical-tile, horizontal-tile\n"
        "                           master-ratio-grow, master-ratio-shrink\n"
        "  window <action>        Window operations:\n"
        "                           close, float, fullscreen, fake-fullscreen,\n"
        "                           maximize, sticky\n"
        "  scratchpad [name]      Toggle scratchpad (optionally named)\n"
        "  move-to-scratchpad [name]  Move to scratchpad\n"
        "  monitor <action>       Monitor operations: next, prev\n"
        "  move-to-monitor <action>   Move to monitor: next, prev\n"
        "  overview               Toggle overview\n"
        "  spawn <command...>     Spawn a command\n"
        "  quit                   Quit the compositor\n"
        "\n"
        "  get windows            List all windows\n"
        "  get workspaces         List all workspaces\n"
        "  get outputs            List all outputs\n"
        "  get focused            Get focused window\n"
        "  get workspace          Get current workspace info\n"
        "  get output             Get current output info\n"
        "  get version            Get compositor version\n"
        "\n"
        "  exec <lua-code>        Execute raw Lua code (escape hatch)\n"
        "\n"
        "Options:\n"
        "  -s SOCKET   Path to the IPC socket (default: $NNWM_SOCKET)\n"
        "  -h          Show this help\n",
        prog);
}

/* ---- Command dispatch ---- */

static int dispatch(int argc, char **argv, int argi, const char *prog)
{
    if (argi >= argc)
    {
        usage(prog);
        return 1;
    }

    const char *cmd = argv[argi++];

    if (std::strcmp(cmd, "-h") == 0 || std::strcmp(cmd, "--help") == 0)
    {
        usage(prog);
        return 0;
    }

    /* ---- Actions ---- */

    if (std::strcmp(cmd, "workspace") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: workspace: missing number\n", prog); return 1; }
        emit("nnwm.switch_workspace(%s)", argv[argi++]);
    }
    else if (std::strcmp(cmd, "move-to-workspace") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: move-to-workspace: missing number\n", prog); return 1; }
        emit("nnwm.move_to_workspace(%s)", argv[argi++]);
    }
    else if (std::strcmp(cmd, "focus") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: focus: missing direction (left|right|up|down)\n", prog); return 1; }
        const char *dir = argv[argi++];
        if (std::strcmp(dir, "left") == 0)       emit("nnwm.focus_left()");
        else if (std::strcmp(dir, "right") == 0)  emit("nnwm.focus_right()");
        else if (std::strcmp(dir, "up") == 0)     emit("nnwm.focus_dir(\"up\")");
        else if (std::strcmp(dir, "down") == 0)   emit("nnwm.focus_dir(\"down\")");
        else if (std::strcmp(dir, "next") == 0)   emit("nnwm.focus_next()");
        else if (std::strcmp(dir, "prev") == 0)   emit("nnwm.focus_prev()");
        else { std::fprintf(stderr, "%s: focus: unknown direction '%s'\n", prog, dir); return 1; }
    }
    else if (std::strcmp(cmd, "swap") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: swap: missing direction (left|right)\n", prog); return 1; }
        const char *dir = argv[argi++];
        if (std::strcmp(dir, "left") == 0)       emit("nnwm.swap_left()");
        else if (std::strcmp(dir, "right") == 0)  emit("nnwm.swap_right()");
        else if (std::strcmp(dir, "next") == 0)   emit("nnwm.swap_next()");
        else if (std::strcmp(dir, "prev") == 0)   emit("nnwm.swap_prev()");
        else if (std::strcmp(dir, "master") == 0) emit("nnwm.swap_master()");
        else { std::fprintf(stderr, "%s: swap: unknown direction '%s'\n", prog, dir); return 1; }
    }
    else if (std::strcmp(cmd, "layout") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: layout: missing action\n", prog); return 1; }
        const char *act = argv[argi++];
        if (std::strcmp(act, "next") == 0)                  emit("nnwm.layout.next()");
        else if (std::strcmp(act, "prev") == 0)             emit("nnwm.layout.prev()");
        else if (std::strcmp(act, "tile") == 0)             emit("nnwm.set_layout(\"htile\")");
        else if (std::strcmp(act, "tabbed") == 0)           emit("nnwm.set_layout(\"tabbed\")");
        else if (std::strcmp(act, "float") == 0)            emit("nnwm.set_layout(\"float\")");
        else if (std::strcmp(act, "vertical-tile") == 0)    emit("nnwm.set_layout(\"vtile\")");
        else if (std::strcmp(act, "horizontal-tile") == 0)  emit("nnwm.set_layout(\"htile\")");
        else if (std::strcmp(act, "master-ratio-grow") == 0)   emit("nnwm.master_ratio_grow()");
        else if (std::strcmp(act, "master-ratio-shrink") == 0) emit("nnwm.master_ratio_shrink()");
        else { std::fprintf(stderr, "%s: layout: unknown action '%s'\n", prog, act); return 1; }
    }
    else if (std::strcmp(cmd, "window") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: window: missing action\n", prog); return 1; }
        const char *act = argv[argi++];
        if (std::strcmp(act, "close") == 0)            emit("nnwm.close()");
        else if (std::strcmp(act, "float") == 0)       emit("nnwm.toggle_float()");
        else if (std::strcmp(act, "fullscreen") == 0)  emit("nnwm.toggle_fullscreen()");
        else if (std::strcmp(act, "fake-fullscreen") == 0) emit("nnwm.toggle_fake_fullscreen()");
        else if (std::strcmp(act, "maximize") == 0)    emit("nnwm.toggle_maximize()");
        else if (std::strcmp(act, "sticky") == 0)      emit("nnwm.toggle_sticky()");
        else { std::fprintf(stderr, "%s: window: unknown action '%s'\n", prog, act); return 1; }
    }
    else if (std::strcmp(cmd, "scratchpad") == 0)
    {
        if (argi < argc)
            emit("nnwm.scratchpad_toggle(\"%s\")", argv[argi++]);
        else
            emit("nnwm.scratchpad_toggle()");
    }
    else if (std::strcmp(cmd, "move-to-scratchpad") == 0)
    {
        if (argi < argc)
            emit("nnwm.move_to_scratchpad(\"%s\")", argv[argi++]);
        else
            emit("nnwm.move_to_scratchpad()");
    }
    else if (std::strcmp(cmd, "monitor") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: monitor: missing action (next|prev)\n", prog); return 1; }
        const char *act = argv[argi++];
        if (std::strcmp(act, "next") == 0) emit("nnwm.focus_monitor_next()");
        else if (std::strcmp(act, "prev") == 0) emit("nnwm.focus_monitor_prev()");
        else { std::fprintf(stderr, "%s: monitor: unknown action '%s'\n", prog, act); return 1; }
    }
    else if (std::strcmp(cmd, "move-to-monitor") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: move-to-monitor: missing action (next|prev)\n", prog); return 1; }
        const char *act = argv[argi++];
        if (std::strcmp(act, "next") == 0) emit("nnwm.move_to_monitor_next()");
        else if (std::strcmp(act, "prev") == 0) emit("nnwm.move_to_monitor_prev()");
        else { std::fprintf(stderr, "%s: move-to-monitor: unknown action '%s'\n", prog, act); return 1; }
    }
    else if (std::strcmp(cmd, "overview") == 0)
    {
        emit("nnwm.toggle_overview()");
    }
    else if (std::strcmp(cmd, "spawn") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: spawn: missing command\n", prog); return 1; }
        /* Rejoin remaining args as the command string */
        char cmd_buf[4096] = {};
        for (int i = argi; i < argc; i++)
        {
            if (i > argi) strcat(cmd_buf, " ");
            strcat(cmd_buf, argv[i]);
        }
        emit("nnwm.spawn(\"%s\")", cmd_buf);
    }
    else if (std::strcmp(cmd, "quit") == 0)
    {
        emit("nnwm.quit()");
    }

    /* ---- Queries ---- */

    else if (std::strcmp(cmd, "get") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: get: missing resource\n", prog); return 1; }
        const char *res = argv[argi++];
        if (std::strcmp(res, "windows") == 0)
            emit("return nnwm.current_output().windows");
        else if (std::strcmp(res, "workspaces") == 0)
            emit("return nnwm.current_output().workspaces");
        else if (std::strcmp(res, "outputs") == 0)
            emit("return nnwm.current_output()");
        else if (std::strcmp(res, "focused") == 0)
            emit("return nnwm.current_window()");
        else if (std::strcmp(res, "workspace") == 0)
            emit("return nnwm.current_workspace()");
        else if (std::strcmp(res, "output") == 0)
            emit("return nnwm.current_output()");
        else if (std::strcmp(res, "version") == 0)
            emit("return nnwm.version()");
        else { std::fprintf(stderr, "%s: get: unknown resource '%s'\n", prog, res); return 1; }
    }

    /* ---- Escape hatch ---- */

    else if (std::strcmp(cmd, "exec") == 0)
    {
        if (argi >= argc) { std::fprintf(stderr, "%s: exec: missing lua code\n", prog); return 1; }
        char code_buf[256 * 1000] = {};
        for (int i = argi; i < argc; i++)
        {
            if (i > argi) strcat(code_buf, " ");
            strcat(code_buf, argv[i]);
        }
        snprintf(lua_buf, sizeof(lua_buf), "return (function() %s end)()", code_buf);
    }
    else
    {
        std::fprintf(stderr, "%s: unknown command '%s'\n", prog, cmd);
        usage(prog);
        return 1;
    }

    return -1; /* signal: send lua_buf to server */
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    const char *sock_path = nullptr;
    int argi = 1;

    /* Parse -s flag */
    while (argi < argc && argv[argi][0] == '-')
    {
        if (std::strcmp(argv[argi], "-h") == 0 || std::strcmp(argv[argi], "--help") == 0)
        {
            usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[argi], "-s") == 0 && argi + 1 < argc)
        {
            sock_path = argv[++argi];
            argi++;
        }
        else
        {
            std::fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[argi]);
            usage(argv[0]);
            return 1;
        }
    }

    int rc = dispatch(argc, argv, argi, argv[0]);
    if (rc >= 0) return rc;

    /* rc == -1: send lua_buf to server */
    if (!sock_path) sock_path = find_socket();
    if (!sock_path)
    {
        std::fprintf(stderr, "%s: no socket. Is nnwm running?\n", argv[0]);
        return 1;
    }

    int fd = ipc_connect(sock_path, argv[0]);
    if (fd < 0) return 1;

    char result[256 * 1024];
    if (!ipc_send_and_recv(fd, lua_buf, result, sizeof(result)))
    {
        std::fprintf(stderr, "%s: communication error\n", argv[0]);
        close(fd);
        return 1;
    }
    close(fd);

    if (result[0])
        std::printf("%s\n", result);

    return 0;
}
