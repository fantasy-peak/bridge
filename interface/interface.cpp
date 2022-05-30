#include <spdlog/spdlog.h>

#include "interface.h"

drogon::Task<drogon::HttpRequestPtr> brforeTest(drogon::HttpRequestPtr, std::shared_ptr<Context>&) {
	spdlog::info("call brforeTest");
	co_return drogon::HttpRequestPtr{};
}

drogon::Task<drogon::HttpResponsePtr> afterTest(drogon::HttpResponsePtr, std::shared_ptr<Context>&) {
	co_return drogon::HttpResponsePtr{};
}