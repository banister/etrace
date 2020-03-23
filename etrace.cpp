#include <iostream>
#include <experimental/filesystem>
#include <fstream>
#include <algorithm>
#include <ios>
#include <string>
#include <linux/netlink.h>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "etrace.h"

namespace fs = std::experimental::filesystem;

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
std::string ETrace::pathForPid(pid_t pid)
{
    std::ostringstream pathStream;
    pathStream << "/proc/" << pid << "/exe";

    fs::path path { pathStream.str() };
    return fs::read_symlink(path);
}

std::string ETrace::cmdLineForPid(pid_t pid)
{
    std::ostringstream path;
    path << "/proc/" << pid << "/cmdline";

    std::string cmdLine;
    std::fstream file{path.str(), std::ios::in};
    std::getline(file, cmdLine);

    std::replace(cmdLine.begin(), cmdLine.end(), '\0', ' ');

    return std::string{cmdLine};
}

bool ETrace::start()
{
    if(geteuid() != 0)
    {
      std::cerr << "You must be root." << std::endl;
      return false;
    }

    if(!initiateConnection())
      return false;

    startReadLoop();

    return true;
}

bool ETrace::initiateConnection()
{
    int sock;
    std::cerr << "Attempting to connect to Netlink" << std::endl;

    if (_sockFd != -1)
    {
        std::cerr << "Existing connection already exists, disconnecting first" << std::endl;
        teardown();
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

    if (::bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(sockaddr_nl)) == -1)
    {
        showError("::bind");
        ::close(sock);
        return false;
    }

    if (subscribeToProcEvents(sock, true) == -1)
    {
        std::cerr << "Could not subscribe to proc events" << std::endl;
        ::close(sock);
        return false;
    }

    std::cerr << "Successfully connected to Netlink" << std::endl;

    // Save the socket FD to an ivar
    _sockFd = sock;

    auto sigHandler = [](int) { std::cout << "Ending program" << std::endl; stop = 1; };
    ::signal(SIGTERM, sigHandler);

    return true;
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

void ETrace::teardown()
{
    if (_sockFd != -1)
    {
        std::cerr << "Attempting to disconnect from Netlink" << std::endl;
        // Unsubscribe from proc events
        subscribeToProcEvents(_sockFd, false);
        if(::close(_sockFd) != 0)
        {
            showError("::close");
            return;
        }
        std::cerr << "Successfully disconnected from Netlink" << std::endl;
    }

    _sockFd = -1;
}

ETrace::~ETrace()
{
    teardown();
}

void ETrace::startReadLoop()
{
    while(!stop)
    {
        NetlinkResponse message = {};
        ::recv(_sockFd, &message, sizeof(message), 0);

        // shortcut
        const auto &eventData = message.event.event_data;

        pid_t pid;
        std::string appName, cmdLine;

        switch (message.event.what)
        {
        case proc_event::PROC_EVENT_NONE:
            std::cerr << "Listening to process events" << std::endl;
            break;
        case proc_event::PROC_EVENT_EXEC:
            pid = eventData.exec.process_pid;

            try
            {
                // Get the launch path associated with the PID
                appName = pathForPid(pid);
                cmdLine = cmdLineForPid(pid);
                std:: cout << "[" << pid << "] " << cmdLine << std::endl;
            }
            catch(const fs::filesystem_error&)
            {

            }


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
