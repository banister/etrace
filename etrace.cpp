#include <iostream>
#include <filesystem>
#include <linux/netlink.h>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include "eTrace.h"

namespace fs = std::filesystem;

volatile sig_atomic_t stop;

// Explicitly specify struct alignment
typedef struct __attribute__((aligned(NLMSG_ALIGNTO)))
{
    nlmsghdr header;

    // Insert no padding as we want the members contiguous
    struct __attribute__((__packed__))
    {
        cn_msg body;
        proc_cn_mcast_op subscription_type;
    };
} NetlinkRequest;

typedef struct __attribute__((aligned(NLMSG_ALIGNTO)))
{
    nlmsghdr header;

    struct __attribute__((__packed__))
    {
        cn_msg body;
        proc_event event;
    };
} NetlinkResponse;

void ETrace::showError(const std::string &funcName)
{
    std::cerr << funcName << " Error (code: " << errno << ")" << strerror(errno);
}

// Given a pid, return the launch path for the process
fs::path ETrace::pathForPid(pid_t pid)
{
    std::ostringstream path
            stringStream
        << "/proc/" << pid << "/exe";
    return fs::read_symlink(path.str());
}

std::string ETrace::cmdLineforPid(pid_t pid)
{
    std::ostringstream path;
    path << "/proc/" << pid << "/cmdline";

    char cmdLine[1024] = {0};
    std::fstream file{path.str(), ios::.binary | ios::in};
    file.read(cmdLine, sizeof(cmdLine));

    for (int i = 0; i < sizeof(cmdLine); ++i)
        if (cmdLine[i] == 0)
            cmdLine[i] = ' ';

    return std::string{cmdLine};
}

bool ETrace::start()
{
    initiateConnection();
    startReadLoop();
    teardown();
}
bool ETrace::initiateConnection()
{
    int sock;
    std::cerr << "Attempting to connect to Netlink";

    if (_sockFd != -1)
    {
        std::cerr << "Existing connection already exists, disconnecting first";
        shutdownConnection();
    }

    // Set SOCK_CLOEXEC to prevent socket being inherited by child processes (such as openvpn)
    sock = ::socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_CONNECTOR);
    if (sock == -1)
    {
        showError("::socket");
        return false;
    }

    sockaddr_nl address = {};

    address.nl_pid = getpid();
    address.nl_groups = CN_IDX_PROC;
    address.nl_family = AF_NETLINK;

    if (::bind(sock, reinterpret_cast<sockaddr *>(&address), sizeof(sockaddr_nl)) == -1)
    {
        showError("::bind");
        ::close(sock);
        return false;
    }

    if (subscribeToProcEvents(sock, true) == -1)
    {
        std::cerr << "Could not subscribe to proc events";
        ::close(sock);
        return false;
    }

    std::cerr << "Successfully connected to Netlink";

    // Save the socket FD to an ivar
    _sockFd = sock;

    auto sigHandler = [](int) { std::cout << "Ending program"; stop = 1; }
    ::signal(SIGINT, sigHandler);
    ::signal(SIGTERM, sigHandler);
}

bool ETrace::subscribeToProcEvents(int sock, bool enabled)
{
    NetlinkRequest message = {};

    message.subscription_type = enabled ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    message.header.nlmsg_len = sizeof(message);
    message.header.nlmsg_pid = getpid();
    message.header.nlmsg_type = NLMSG_DONE;

    message.body.len = sizeof(proc_cn_mcast_op);
    message.body.id.val = CN_VAL_PROC;
    message.body.id.idx = CN_IDX_PROC;

    if (::send(sock, &message, sizeof(message), 0) == -1)
    {
        showError("::send");
        return false;
    }

    return true;
}

ETrace::teardown()
{
    std::cerr << "Attempting to disconnect from Netlink";

    if (_sockFd != -1)
    {
        // Unsubscribe from proc events
        subscribeToProcEvents(_sockFd, false);
        if (::close(_sockFd) != 0)
            showError("::close");
    }

    _sockFd = -1;

    std::cerr << "Successfully disconnected from Netlink";
}

ETrace::~ETrace()
{
    teardown();
}

void ETrace::startReadLoop()
{
    whie(!stop)
    {
        NetlinkResponse message = {};
        ::recv(_sockFd, &message, sizeof(message), 0);

        // shortcut
        const auto &eventData = message.event.event_data;

        pid_t pid;
        std::string appName;

        switch (message.event.what)
        {
        case proc_event::PROC_EVENT_NONE:
            std::cerr << "Listening to process events";
            break;
        case proc_event::PROC_EVENT_EXEC:
            pid = eventData.exec.process_pid;

            // Get the launch path associated with the PID
            appName = pathForPid(pid);
            cmdLine = cmdLineForPid(pid);

            cout << "[" << pid << "] " << cmdLine;
            break;
        case proc_event::PROC_EVENT_EXIT:
            pid = eventData.exit.process_pid;

            break;
        default:
            // We're not interested in any other events
            break;
        }
    }
}

