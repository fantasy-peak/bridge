#define ASYNC_SIMPLE_HAS_NOT_AIO

#include <ranges>

#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
#include <yaml_cpp_struct.hpp>

#include "utils.h"

struct Config {
	std::string host;
	int16_t port;
	std::vector<std::string> endpoints;
	uint32_t threads;
};
YCS_ADD_STRUCT(Config, host, port, endpoints, threads)

template <typename... Args>
void close(Args... args) {
	auto func = [](auto& sock_ptr) {
		if (!sock_ptr->is_open())
			return;
		std::error_code ec;
		sock_ptr->cancel(ec);
		sock_ptr->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		sock_ptr->close(ec);
	};
	(func(args), ...);
}

template <typename... Args>
void set_option(Args... args) {
	auto func = [](auto& sock_ptr) {
		std::error_code ec;
		sock_ptr->set_option(asio::ip::tcp::no_delay(true), ec);
		sock_ptr->set_option(asio::socket_base::keep_alive(true), ec);
	};
	(func(args), ...);
}

async_simple::coro::Lazy<void> start_forward(std::shared_ptr<asio::ip::tcp::socket> read_sock_ptr,
	std::shared_ptr<asio::ip::tcp::socket> write_socket_ptr, std::string flow_to) {
	constexpr int32_t BufferLen{8192};
	std::unique_ptr<uint8_t[]> buffer_ptr = std::make_unique<uint8_t[]>(BufferLen);
	constexpr int32_t count = BufferLen - 1;
	while (true) {
		auto [ec, bytes_transferred] = co_await async_read_some(*read_sock_ptr, asio::buffer(buffer_ptr.get(), count));
		if (ec) {
			close(write_socket_ptr);
			if (ec != asio::error::operation_aborted)
				spdlog::warn("[forward] async_read_some [{}]: {}", flow_to, ec.message());
			co_return;
		}
		if (auto [ec, _] = co_await async_write(*write_socket_ptr, asio::buffer(buffer_ptr.get(), bytes_transferred)); ec) {
			close(read_sock_ptr);
			if (ec != asio::error::operation_aborted)
				spdlog::warn("[forward] async_write [{}]: {}", flow_to, ec.message());
			co_return;
		}
		std::memset(buffer_ptr.get(), 0x00, bytes_transferred);
	}
	co_return;
}

async_simple::coro::Lazy<void> start_session(asio::ip::tcp::socket sock, std::shared_ptr<AsioExecutor> executor_ptr, Config& cfg) {
	auto server_socket_ptr = std::make_shared<asio::ip::tcp::socket>(executor_ptr->m_io_context);
	auto client_socket_ptr = std::make_shared<asio::ip::tcp::socket>(std::move(sock));
	set_option(server_socket_ptr, client_socket_ptr);

	auto to_vector = [](auto&& r) {
		auto r_common = r | std::views::common;
		return std::vector(r_common.begin(), r_common.end());
	};
	auto index = std::hash<std::string>()([]() {
		uuid_t uuid;
		char uuid_str[37]{};
		uuid_generate_random(uuid);
		uuid_unparse(uuid, uuid_str);
		return std::string(uuid_str);
	}()) % cfg.endpoints.size();
	auto host_ports = to_vector(cfg.endpoints[index] | std::views::split(':') | std::views::transform([](auto&& rng) {
		return std::string_view(&*rng.begin(), std::ranges::distance(rng.begin(), rng.end()));
	}));
	std::string server_host{host_ports[0]};
	std::string server_port{host_ports[1]};

	asio::ip::tcp::resolver resolver{executor_ptr->m_io_context};
	auto [resolver_ec, resolver_results] = co_await async_resolve(resolver, server_host, server_port);
	if (resolver_ec) {
		spdlog::error("async_resolve: {} host: [{}] port: [{}]", resolver_ec.message(), server_host, server_port);
		close(client_socket_ptr);
		co_return;
	}
	spdlog::debug("resolver_results size: [{}]", resolver_results.size());
	for (auto& endpoint : resolver_results) {
		std::stringstream ss;
		ss << endpoint.endpoint();
		spdlog::debug("resolver_results: [{}]", ss.str());
	}

	spdlog::debug("async_connect: [{}:{}]", server_host, server_port);
	if (auto ec = co_await async_connect(executor_ptr->m_io_context, *server_socket_ptr, resolver_results); ec) {
		spdlog::error("async_connect: {}, host: [{}] port: [{}]", ec.message(), server_host, server_port);
		close(client_socket_ptr);
		co_return;
	}
	spdlog::debug("Connected: [{}:{}]", server_host, server_port);

	start_forward(client_socket_ptr, server_socket_ptr, "client-to-server").via(executor_ptr.get()).detach();
	start_forward(server_socket_ptr, client_socket_ptr, "server-to-client").via(executor_ptr.get()).detach();
	co_return;
}

int main(int argc, char** argv) {
	auto [cfg, error] = yaml_cpp_struct::from_yaml<Config>(argv[1]);
	if (!cfg) {
		spdlog::error("{}", error);
		return -1;
	}
	spdlog::set_level(spdlog::level::info);
	IoContextPool pool(cfg.value().threads);
	pool.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	auto& context = pool.getIoContext();
	asio::ip::tcp::acceptor acceptor(context);
	asio::ip::tcp::resolver resolver(context);
	asio::ip::tcp::endpoint endpoint = *resolver.resolve(cfg.value().host, std::to_string(cfg.value().port)).begin();
	std::stringstream ss;
	ss << endpoint;
	spdlog::info("start accept at {} ...", ss.str());
	acceptor.open(endpoint.protocol());
	std::error_code ec;
	acceptor.set_option(asio::detail::socket_option::integer<IPPROTO_TCP, TCP_FASTOPEN>(50), ec);
	spdlog::info("start fastopen: {}", ec.message());
	acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
	acceptor.bind(endpoint);
	acceptor.listen(asio::socket_base::max_listen_connections, ec);
	if (ec) {
		spdlog::error("{}", ec.message());
		return -1;
	}
	asio::signal_set sigset(context, SIGINT, SIGTERM);
	sigset.async_wait([&](const std::error_code&, int) { acceptor.close(); });
	async_simple::coro::syncAwait([&]() -> async_simple::coro::Lazy<void> {
		while (true) {
			auto& context = pool.getIoContext();
			asio::ip::tcp::socket socket(context);
			auto ec = co_await async_accept(acceptor, socket);
			if (ec) {
				if (ec == asio::error::operation_aborted)
					break;
				spdlog::error("Accept failed, error: {}", ec.message());
				continue;
			}
			auto executor = std::make_shared<AsioExecutor>(context);
			start_session(std::move(socket), executor, cfg.value()).via(executor.get()).detach();
		}
	}());
	pool.stop();
	return 0;
}