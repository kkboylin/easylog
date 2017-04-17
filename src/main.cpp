
#include <chrono>
#include <thread>

#include "Log.h"

using namespace kkboylin::log;

static void onLog(const bool* terminate)
{
    do
    {
        CManager::GetInstance()->Process();
        std::this_thread::sleep_for( std::chrono::seconds(1) );
    } while (*terminate == false);

    LogOutput(ELL_NOTICE, "thread : %s\n", "end");
}

struct SAccount
{
    std::string loginname;
    std::string nickname;
};

static void ValueOutput_(std::string& output, const char*& fmt, const SAccount& value)
{
    int count = GetFormatLength_(fmt);
    if (count > 0)
    {
        char buffer[1024];
        sprintf(buffer,
                "\naccount : %s\nnickname : %s\n",
                value.loginname.c_str(),
                value.nickname.c_str());
            output += buffer;
        fmt += (count + 1);
    }
    else
    {
        fmt += strlen(fmt);
    }   
}

int main(int argc, const char** argv)
{
    Manager mgr = Create();
    mgr->Append( "console", CreateConsoleOutput(ELL_NOTICE) );
    mgr->Append( "debuger", CreateDebugerOutput(ELL_DEBUG) );
    mgr->Append( "log", CreateFileOutput(ELL_INFO, "Test", "./logs" ) );
    mgr->EnableOption(EO_TIME);
    mgr->EnableOption(EO_DATE);
    mgr->EnableOption(EO_DAY);
    mgr->EnableOption(EO_THREAD);
    mgr->EnableOption(EO_LEVEL);

    bool terminate = false;
    std::thread t1(onLog, &terminate);
    LogOutput(ELL_NOTICE, "test : %s\n", std::string("aaa") );

    SAccount account;
    account.loginname = "tester";
    account.nickname  = "player1";
    LogOutput(ELL_NOTICE, "account : %s\n", account);
    while(terminate != true)
    {
        std::this_thread::sleep_for( std::chrono::seconds(1) );
        terminate = true;
    }
    t1.join();
    mgr.reset();
    return 0;
}
