#include <iostream>
#include <list>
#include <ranges>
#include <thread>

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>

DEFINE_int32(threads, 8, "io threads");
DEFINE_string(ip, "0.0.0.0", "listen ip");
DEFINE_int32(port, 8848, "port");
DEFINE_string(destination_ip, "www.baidu.com", "destination ip");
DEFINE_string(destination_port, "443", "destination port");

template <typename... Args>
void close(Args&&... args) {
	auto func = [](auto& sock) {
		if (!sock.is_open())
			return;
		std::error_code ec;
		sock.cancel(ec);
		sock.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
		sock.close(ec);
	};
	(func(args), ...);
}

template <typename... Args>
void set_option(Args&&... args) {
	auto func = [](auto& sock) {
		std::error_code ec;
		sock.set_option(asio::ip::tcp::no_delay(true), ec);
		sock.set_option(asio::socket_base::keep_alive(true), ec);
	};
	(func(args), ...);
}

asio::awaitable<void> start_session(asio::ip::tcp::socket sock) {
	asio::ip::tcp::socket destination_socket(co_await asio::this_coro::executor);
	asio::ip::tcp::socket client_socket(std::move(sock));
	set_option(destination_socket, client_socket);

	auto resolver = asio::ip::tcp::resolver(co_await asio::this_coro::executor);
	auto [ec, results] = co_await resolver.async_resolve(FLAGS_destination_ip.data(), FLAGS_destination_port.data(), asio::as_tuple(asio::use_awaitable));
	if (ec) {
		SPDLOG_ERROR("async_resolve: {}, host: [{}]:[{}]", ec.message(), FLAGS_destination_ip, FLAGS_destination_port);
		close(client_socket);
		co_return;
	}
	// spdlog::debug("resolver_results size: [{}]", results.size());
	// for (auto& endpoint : results) {
	// 	std::stringstream ss;
	// 	ss << endpoint.endpoint();
	// 	spdlog::debug("results: [{}]", ss.str());
	// }
	if (auto [ec, count] = co_await asio::async_connect(destination_socket, results, asio::as_tuple(asio::use_awaitable)); ec) {
		SPDLOG_ERROR("async_connect: {}", ec.message());
		close(client_socket);
		co_return;
	}
	auto transfer = [](asio::ip::tcp::socket& from, asio::ip::tcp::socket& to) -> asio::awaitable<void> {
		std::array<unsigned char, 4096> data;
		for (;;) {
			auto [ec, n] = co_await from.async_read_some(asio::buffer(data, 4095), asio::as_tuple(asio::use_awaitable));
			if (ec) {
				close(to);
				co_return;
			}
			if (auto [ec, _] = co_await asio::async_write(to, asio::buffer(data, n), asio::as_tuple(asio::use_awaitable)); ec) {
				close(from);
				co_return;
			}
		}
	};
	using namespace asio::experimental::awaitable_operators;
	co_await (transfer(client_socket, destination_socket) && transfer(destination_socket, client_socket));
	co_return;
}

class IoContextPool final {
public:
	explicit IoContextPool(std::size_t);

	void start();
	void stop();

	asio::io_context& getIoContext();

private:
	std::vector<std::shared_ptr<asio::io_context>> m_io_contexts;
	std::list<asio::any_io_executor> m_work;
	std::size_t m_next_io_context;
	std::vector<std::jthread> m_threads;
};

inline IoContextPool::IoContextPool(std::size_t pool_size)
	: m_next_io_context(0) {
	if (pool_size == 0)
		throw std::runtime_error("IoContextPool size is 0");
	for (std::size_t i = 0; i < pool_size; ++i) {
		auto io_context_ptr = std::make_shared<asio::io_context>();
		m_io_contexts.emplace_back(io_context_ptr);
		m_work.emplace_back(asio::require(io_context_ptr->get_executor(), asio::execution::outstanding_work.tracked));
	}
}

inline void IoContextPool::start() {
	for (auto& context : m_io_contexts)
		m_threads.emplace_back(std::jthread([&] { context->run(); }));
}

inline void IoContextPool::stop() {
	for (auto& context_ptr : m_io_contexts)
		context_ptr->stop();
}

inline asio::io_context& IoContextPool::getIoContext() {
	asio::io_context& io_context = *m_io_contexts[m_next_io_context];
	++m_next_io_context;
	if (m_next_io_context == m_io_contexts.size())
		m_next_io_context = 0;
	return io_context;
}

int main(int argc, char** argv) {
	gflags ::SetVersionString("0.0.0.1");
	gflags ::SetUsageMessage("Usage : ./ecgw-tester");
	google::ParseCommandLineFlags(&argc, &argv, true);

	spdlog::set_level(spdlog::level::info);
	IoContextPool pool(FLAGS_threads);
	pool.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	IoContextPool accept_pool{1};
	accept_pool.start();
	auto& context = accept_pool.getIoContext();
	asio::ip::tcp::acceptor acceptor(context);

	spdlog::info("start accept at {}:{} ...", FLAGS_ip, FLAGS_port);
	asio::ip::tcp::endpoint endpoint(asio::ip::make_address(FLAGS_ip), FLAGS_port);
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
	std::binary_semaphore smph_signal_main_to_thread{0};
	sigset.async_wait([&](const std::error_code&, int) {
		acceptor.close();
		smph_signal_main_to_thread.release();
	});
	auto accept = [](auto& acceptor, auto& pool) -> asio::awaitable<void> {
		while (true) {
			auto& context = pool.getIoContext();
			asio::ip::tcp::socket socket(context);
			auto [ec] = co_await acceptor.async_accept(socket, asio::as_tuple(asio::use_awaitable));
			if (ec) {
				if (ec == asio::error::operation_aborted)
					break;
				spdlog::error("Accept failed, error: {}", ec.message());
				continue;
			}
			asio::co_spawn(context, start_session(std::move(socket)), asio::detached);
		}
	};
	asio::co_spawn(context, accept(acceptor, pool), asio::detached);
	smph_signal_main_to_thread.acquire();
	SPDLOG_INFO("stoped ...");
	accept_pool.stop();
	pool.stop();
	return 0;
}
