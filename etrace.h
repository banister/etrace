#ifndef ETRACE_H
#define ETRACE_H

class ETrace
{
public:
    ETrace();
    ~ETrace();

    bool start();
private:
    void showError(const std::string &funcName);
    void initiateConnection();
    void readFromSocket();
    bool subscribeToProcEvents(int sock, bool enable);
    std::string pathForPid(pid_t pid);
    std::string cmdLineForPid(pid_t pid);
private:
    int _sockFd;
};

#endif

