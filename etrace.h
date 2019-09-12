#ifndef ETRACE_H
#define ETRACE_H
#include <string>
#include <iostream>

class ETrace
{
public:
    ETrace() : _sockFd{-1} {}
    ~ETrace();

    bool start();
private:
    void teardown();
    void showError(const std::string &funcName);
    bool initiateConnection();
    void readFromSocket();
    void startReadLoop();
    bool subscribeToProcEvents(int sock, bool enable);
    std::string pathForPid(pid_t pid);
    std::string cmdLineForPid(pid_t pid);
private:
    int _sockFd;
};

#endif

