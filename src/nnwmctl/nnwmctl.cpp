#include <argparse.hpp>
#include <json.hpp>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using json = nlohmann::json;

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

/* ---- Main ---- */

int main(int argc, char **argv)
{
    argparse::ArgumentParser program("nnwmctl");

    program.add_argument("-s", "--socket")
        .help("path to the IPC socket (default: $NNWM_SOCKET)")
        .nargs(1)
        .default_value(std::string(""));

    /* Subcommands */
    argparse::ArgumentParser cmd_workspace("workspace");
    cmd_workspace.add_description("Switch to workspace n");
    cmd_workspace.add_argument("n", "workspace number")
        .nargs(1);

    argparse::ArgumentParser cmd_move_to_workspace("move-to-workspace");
    cmd_move_to_workspace.add_description("Move focused window to workspace n");
    cmd_move_to_workspace.add_argument("n", "workspace number")
        .nargs(1);

    argparse::ArgumentParser cmd_focus("focus");
    cmd_focus.add_description("Focus in direction");
    cmd_focus.add_argument("direction", "left|right|up|down|next|prev")
        .nargs(1);

    argparse::ArgumentParser cmd_swap("swap");
    cmd_swap.add_description("Swap in direction");
    cmd_swap.add_argument("direction", "left|right|next|prev|master")
        .nargs(1);

    argparse::ArgumentParser cmd_layout("layout");
    cmd_layout.add_description("Layout operations");
    cmd_layout.add_argument("action", "next|prev|tile|tabbed|float|vertical-tile|master-ratio-grow|master-ratio-shrink")
        .nargs(1);

    argparse::ArgumentParser cmd_window("window");
    cmd_window.add_description("Window operations");
    cmd_window.add_argument("action", "close|float|fullscreen|fake-fullscreen|maximize|sticky")
        .nargs(1);

    argparse::ArgumentParser cmd_scratchpad("scratchpad");
    cmd_scratchpad.add_description("Toggle scratchpad");
    cmd_scratchpad.add_argument("name")
        .nargs(argparse::nargs_pattern::optional);

    argparse::ArgumentParser cmd_move_to_scratchpad("move-to-scratchpad");
    cmd_move_to_scratchpad.add_description("Move window to scratchpad");
    cmd_move_to_scratchpad.add_argument("name")
        .nargs(argparse::nargs_pattern::optional);

    argparse::ArgumentParser cmd_monitor("monitor");
    cmd_monitor.add_description("Monitor operations");
    cmd_monitor.add_argument("action", "next|prev")
        .nargs(1);

    argparse::ArgumentParser cmd_move_to_monitor("move-to-monitor");
    cmd_move_to_monitor.add_description("Move window to monitor");
    cmd_move_to_monitor.add_argument("action", "next|prev")
        .nargs(1);

    argparse::ArgumentParser cmd_spawn("spawn");
    cmd_spawn.add_description("Spawn a command");
    cmd_spawn.add_argument("command", "command to spawn")
        .nargs(argparse::nargs_pattern::at_least_one);

    argparse::ArgumentParser cmd_get("get");
    cmd_get.add_description("Query compositor state");
    cmd_get.add_argument("resource", "windows|workspaces|outputs|focused|workspace|output|version")
        .nargs(1);

    argparse::ArgumentParser cmd_screenshot("screenshot");
    cmd_screenshot.add_description("Take a screenshot (requires grim; --copy requires wl-copy)");
    cmd_screenshot.add_argument("--output", "-o")
        .help("capture a specific output by name")
        .nargs(1)
        .default_value(std::string(""));
    cmd_screenshot.add_argument("--window", "-w")
        .help("capture the focused window")
        .flag();
    cmd_screenshot.add_argument("--region", "-r")
        .help("capture a region (format: X,Y WxH)")
        .nargs(1)
        .default_value(std::string(""));
    cmd_screenshot.add_argument("--interactive", "-i")
        .help("interactively select a region using slurp")
        .flag();
    cmd_screenshot.add_argument("--copy", "-c")
        .help("copy result to clipboard instead of saving to a file")
        .flag();
    cmd_screenshot.add_argument("--notify", "-n")
        .help("send a desktop notification after capture (requires notify-send)")
        .flag();
    cmd_screenshot.add_argument("path")
        .help("destination file path (default: ~/Pictures/nnwm-TIMESTAMP.png)")
        .nargs(argparse::nargs_pattern::optional);

    argparse::ArgumentParser cmd_exec("exec");
    cmd_exec.add_description("Execute raw Lua code");
    cmd_exec.add_argument("code", "Lua code to execute")
        .nargs(argparse::nargs_pattern::at_least_one);

    /* Add subcommands */
    program.add_subparser(cmd_screenshot);
    program.add_subparser(cmd_workspace);
    program.add_subparser(cmd_move_to_workspace);
    program.add_subparser(cmd_focus);
    program.add_subparser(cmd_swap);
    program.add_subparser(cmd_layout);
    program.add_subparser(cmd_window);
    program.add_subparser(cmd_scratchpad);
    program.add_subparser(cmd_move_to_scratchpad);
    program.add_subparser(cmd_monitor);
    program.add_subparser(cmd_move_to_monitor);
    program.add_subparser(cmd_spawn);
    program.add_subparser(cmd_get);
    program.add_subparser(cmd_exec);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::fprintf(stderr, "%s\n", err.what());
        return 1;
    }

    /* Dispatch subcommands */
    if (program.is_subcommand_used(cmd_workspace))
        emit("nnwm.switch_workspace(%s)", cmd_workspace.get<std::string>("n").c_str());
    else if (program.is_subcommand_used(cmd_move_to_workspace))
        emit("nnwm.move_to_workspace(%s)", cmd_move_to_workspace.get<std::string>("n").c_str());
    else if (program.is_subcommand_used(cmd_focus))
    {
        std::string dir = cmd_focus.get<std::string>("direction");
        if (dir == "left")       emit("nnwm.focus_left()");
        else if (dir == "right")  emit("nnwm.focus_right()");
        else if (dir == "up")     emit("nnwm.focus_dir(\"up\")");
        else if (dir == "down")   emit("nnwm.focus_dir(\"down\")");
        else if (dir == "next")   emit("nnwm.focus_next()");
        else if (dir == "prev")   emit("nnwm.focus_prev()");
        else { std::fprintf(stderr, "nnwmctl: focus: unknown direction '%s'\n", dir.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_swap))
    {
        std::string dir = cmd_swap.get<std::string>("direction");
        if (dir == "left")       emit("nnwm.swap_left()");
        else if (dir == "right")  emit("nnwm.swap_right()");
        else if (dir == "next")   emit("nnwm.swap_next()");
        else if (dir == "prev")   emit("nnwm.swap_prev()");
        else if (dir == "master") emit("nnwm.swap_master()");
        else { std::fprintf(stderr, "nnwmctl: swap: unknown direction '%s'\n", dir.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_layout))
    {
        std::string act = cmd_layout.get<std::string>("action");
        if (act == "next")                  emit("nnwm.layout.next()");
        else if (act == "prev")             emit("nnwm.layout.prev()");
        else if (act == "tile")             emit("nnwm.set_layout(\"htile\")");
        else if (act == "tabbed")           emit("nnwm.set_layout(\"tabbed\")");
        else if (act == "float")            emit("nnwm.set_layout(\"float\")");
        else if (act == "vertical-tile")    emit("nnwm.set_layout(\"vtile\")");
        else if (act == "horizontal-tile")  emit("nnwm.set_layout(\"htile\")");
        else if (act == "master-ratio-grow")   emit("nnwm.master_ratio_grow()");
        else if (act == "master-ratio-shrink") emit("nnwm.master_ratio_shrink()");
        else { std::fprintf(stderr, "nnwmctl: layout: unknown action '%s'\n", act.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_window))
    {
        std::string act = cmd_window.get<std::string>("action");
        if (act == "close")            emit("nnwm.close()");
        else if (act == "float")       emit("nnwm.toggle_float()");
        else if (act == "fullscreen")  emit("nnwm.toggle_fullscreen()");
        else if (act == "fake-fullscreen") emit("nnwm.toggle_fake_fullscreen()");
        else if (act == "maximize")    emit("nnwm.toggle_maximize()");
        else if (act == "sticky")      emit("nnwm.toggle_sticky()");
        else { std::fprintf(stderr, "nnwmctl: window: unknown action '%s'\n", act.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_scratchpad))
    {
        if (cmd_scratchpad.is_used("name"))
            emit("nnwm.scratchpad_toggle(\"%s\")", cmd_scratchpad.get<std::string>("name").c_str());
        else
            emit("nnwm.scratchpad_toggle()");
    }
    else if (program.is_subcommand_used(cmd_move_to_scratchpad))
    {
        if (cmd_move_to_scratchpad.is_used("name"))
            emit("nnwm.move_to_scratchpad(\"%s\")", cmd_move_to_scratchpad.get<std::string>("name").c_str());
        else
            emit("nnwm.move_to_scratchpad()");
    }
    else if (program.is_subcommand_used(cmd_monitor))
    {
        std::string act = cmd_monitor.get<std::string>("action");
        if (act == "next") emit("nnwm.focus_monitor_next()");
        else if (act == "prev") emit("nnwm.focus_monitor_prev()");
        else { std::fprintf(stderr, "nnwmctl: monitor: unknown action '%s'\n", act.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_move_to_monitor))
    {
        std::string act = cmd_move_to_monitor.get<std::string>("action");
        if (act == "next") emit("nnwm.move_to_monitor_next()");
        else if (act == "prev") emit("nnwm.move_to_monitor_prev()");
        else { std::fprintf(stderr, "nnwmctl: move-to-monitor: unknown action '%s'\n", act.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_spawn))
    {
        auto cmds = cmd_spawn.get<std::vector<std::string>>("command");
        std::string joined;
        for (size_t i = 0; i < cmds.size(); i++)
        {
            if (i > 0) joined += " ";
            joined += cmds[i];
        }
        emit("nnwm.spawn(\"%s\")", joined.c_str());
    }
    else if (program.is_subcommand_used(cmd_get))
    {
        std::string res = cmd_get.get<std::string>("resource");
        if (res == "windows")         emit("return nnwm.windows()");
        else if (res == "workspaces") emit("return nnwm.workspaces()");
        else if (res == "outputs")    emit("return nnwm.outputs()");
        else if (res == "focused")    emit("return nnwm.current_window()");
        else if (res == "workspace")  emit("return nnwm.current_workspace()");
        else if (res == "output")     emit("return nnwm.current_output()");
        else if (res == "version")    emit("return nnwm.version()");
        else { std::fprintf(stderr, "nnwmctl: get: unknown resource '%s'\n", res.c_str()); return 1; }
    }
    else if (program.is_subcommand_used(cmd_screenshot))
    {
        std::string output_arg = cmd_screenshot.get<std::string>("--output");
        bool        do_window  = cmd_screenshot.get<bool>("--window");
        std::string region_arg = cmd_screenshot.get<std::string>("--region");
        bool        interactive = cmd_screenshot.get<bool>("--interactive");
        bool        do_copy    = cmd_screenshot.get<bool>("--copy");
        bool        do_notify  = cmd_screenshot.get<bool>("--notify");
        std::string path_arg;
        try { path_arg = cmd_screenshot.get<std::string>("path"); } catch (...) {}

        /* Build Lua table */
        std::string lua = "nnwm.screenshot({";

        if (interactive)
            lua += "type=\"interactive\"";
        else if (do_window)
            lua += "type=\"window\"";
        else if (!output_arg.empty())
            lua += "type=\"output\",output=\"" + output_arg + "\"";
        else if (!region_arg.empty())
        {
            /* parse "X,Y WxH" */
            int rx = 0, ry = 0, rw = 0, rh = 0;
            if (std::sscanf(region_arg.c_str(), "%d,%d %dx%d", &rx, &ry, &rw, &rh) == 4)
                lua += "type=\"region\",x=" + std::to_string(rx)
                     + ",y=" + std::to_string(ry)
                     + ",width=" + std::to_string(rw)
                     + ",height=" + std::to_string(rh);
            else
            {
                std::fprintf(stderr,
                             "nnwmctl: screenshot: --region format must be X,Y WxH\n");
                return 1;
            }
        }
        else
            lua += "type=\"screen\"";

        if (do_copy)   lua += ",copy=true";
        if (do_notify) lua += ",notify=true";
        if (!path_arg.empty()) lua += ",path=\"" + path_arg + "\"";

        lua += "})";
        snprintf(lua_buf, sizeof(lua_buf), "%s", lua.c_str());
    }
    else if (program.is_subcommand_used(cmd_exec))
    {
        auto code_parts = cmd_exec.get<std::vector<std::string>>("code");
        std::string code;
        for (size_t i = 0; i < code_parts.size(); i++)
        {
            if (i > 0) code += " ";
            code += code_parts[i];
        }
        snprintf(lua_buf, sizeof(lua_buf), "return (function() %s end)()", code.c_str());
    }
    else
    {
        std::fprintf(stderr, "%s\n", program.help().str().c_str());
        return 1;
    }

    /* Connect and send */
    std::string sock_str = program.get<std::string>("--socket");
    const char *sock_path = sock_str.empty() ? find_socket() : sock_str.c_str();
    if (!sock_path)
    {
        std::fprintf(stderr, "nnwmctl: no socket. Is nnwm running?\n");
        return 1;
    }

    int fd = ipc_connect(sock_path, argv[0]);
    if (fd < 0) return 1;

    /* Server needs a newline to delimit the command */
    size_t lua_len = strlen(lua_buf);
    if (lua_len + 1 < sizeof(lua_buf))
    {
        lua_buf[lua_len]     = '\n';
        lua_buf[lua_len + 1] = '\0';
    }

    char result[256 * 1024];
    if (!ipc_send_and_recv(fd, lua_buf, result, sizeof(result)))
    {
        std::fprintf(stderr, "nnwmctl: communication error\n");
        close(fd);
        return 1;
    }
    close(fd);

    if (!result[0])
        return 0;

    try
    {
        json j = json::parse(result);
        if (!j.value("ok", false))
        {
            std::fprintf(stderr, "nnwmctl: error: %s\n",
                         j.value("error", "unknown").c_str());
            return 1;
        }
        auto &data = j["data"];
        if (!data.is_null())
            std::printf("%s\n", data.dump(2).c_str());
    }
    catch (...)
    {
        std::printf("%s\n", result);
    }

    return 0;
}
