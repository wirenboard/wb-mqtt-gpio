#include "exceptions.h"

using namespace std;
using namespace WBMQTT;

TGpioDriverException::TGpioDriverException(const char * file, int line, const string & message)
    : TBaseException(file, line, message)
{}
