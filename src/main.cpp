#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
#define BOOST_THREAD_PROVIDES_EXECUTORS
#define BOOST_THREAD_USES_MOVE

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <boost/dll/import.hpp>
#include <boost/thread/executors/basic_thread_pool.hpp>
#include "cfg.h"
#include "utils.h"

DEFINE_string(config_file, "./cfg.yaml", "config file");

class SessionFilter : public drogon::HttpFilter<SessionFilter> {
public:
	SessionFilter() = default;

	virtual void doFilter(const drogon::HttpRequestPtr& req, drogon::FilterCallback&& fcb, drogon::FilterChainCallback&& fccb) override {
		if (req->peerAddr().isIntranetIp()) {
			fccb();
			return;
		}
		auto res = drogon::HttpResponse::newNotFoundResponse();
		fcb(res);
	}
};

int main(int argc, char** argv) {
	SessionFilter ss;
	gflags ::SetVersionString("0.0.0.1");
	gflags ::SetUsageMessage("Usage : ./bridge");
	google::ParseCommandLineFlags(&argc, &argv, true);
	auto [cfg, error] = yaml_cpp_struct::from_yaml<BridgeConfig>(FLAGS_config_file);
	if (!cfg) {
		spdlog::error("{}", error);
		return -1;
	}
	std::shared_ptr<Context> context_ptr = createContext(cfg.value().redis_config);
	boost::dll::shared_library lib(cfg.value().interface_lib_path);

	for (auto& c : cfg.value().configs) {
		std::vector<drogon::internal::HttpConstraint> filters_and_methods;
		for (auto& m : c.methods)
			filters_and_methods.emplace_back(m);
		filters_and_methods.emplace_back(drogon::internal::HttpConstraint{"SessionFilter"});
		using BeforeFunc = std::function<drogon::Task<drogon::HttpRequestPtr>(drogon::HttpRequestPtr, std::shared_ptr<Context>&)>;
		BeforeFunc before_func{nullptr};
		if (c.before_func_name) {
			if (!lib.has(c.before_func_name.value().c_str())) {
				spdlog::error("[main] not found {}", c.before_func_name.value());
				return -1;
			}
			else {
				before_func = lib.get<drogon::Task<drogon::HttpRequestPtr>(drogon::HttpRequestPtr, std::shared_ptr<Context>&)>(c.before_func_name.value().c_str());
			}
		}

		auto handler = [context_ptr, before_func](drogon::HttpRequestPtr http_request_ptr) mutable -> drogon::Task<drogon::HttpResponsePtr> {
			auto resp = drogon::HttpResponse::newHttpResponse();
			spdlog::info("ddddddddddddddd");
			auto new_http_request_ptr = co_await before_func(std::move(http_request_ptr), context_ptr);
			co_return resp;
		};
		drogon::app().registerHandler(c.path, std::move(handler), filters_and_methods);
	}
	drogon::app()
		.loadConfigFile(cfg.value().http_server_config)
		.setIntSignalHandler([] { drogon::app().quit(); })
		.run();
	return 0;
}