#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_EXECUTORS
#define BOOST_THREAD_USES_MOVE

#include <drogon/drogon.h>
#include <boost/dll.hpp>

#include "context.h"

#define API extern "C" BOOST_SYMBOL_EXPORT

API drogon::Task<drogon::HttpRequestPtr> brforeTest(drogon::HttpRequestPtr, std::shared_ptr<Context>&);
API drogon::Task<drogon::HttpResponsePtr> afterTest(drogon::HttpResponsePtr, std::shared_ptr<Context>&);
