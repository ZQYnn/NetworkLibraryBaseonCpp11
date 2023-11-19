#include "Logger.h"
#include "Timestamp.h"
#include <iostream>

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}


// 日志 级别，time msg
void Logger::log(std::string msg)
{
    switch (logLevel_)
    {
    case INFO:
        std::cout << "[INFO] ";
        break;

    case ERROR:
        std::cout << "[ERROR] "; 
        break;
        
    case FATAL:
        std::cout << "[FATAL] ";
        break;
    
    case DEBUG:
        std::cout << "[DEBUG] ";
        break;
    
    default:
        break;
    }
    std::cout << Timestamp::now().toString()<< ":" <<  msg << std::endl;
}


void Logger::setLogLevel(int level)
{
    logLevel_ = level;
}

