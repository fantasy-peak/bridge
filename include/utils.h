#ifndef _ASIO_UTIL_H_
#define _ASIO_UTIL_H_

#include <cstdlib>
#include <iostream>
#include <thread>

#include <async_simple/coro/Lazy.h>
#include <async_simple/coro/SyncAwait.h>
#include <async_simple/executors/SimpleExecutor.h>
#include <asio.hpp>

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

template <typename AsioBuffer>
std::pair<asio::error_code, size_t> read_some(asio::ip::tcp::socket& sock,
	AsioBuffer&& buffer) {
	asio::error_code error;
	size_t length = sock.read_some(std::forward<AsioBuffer>(buffer), error);
	return std::make_pair(error, length);
}

template <typename AsioBuffer>
std::pair<asio::error_code, size_t> write(asio::ip::tcp::socket& sock,
	AsioBuffer&& buffer) {
	asio::error_code error;
	auto length = asio::write(sock, std::forward<AsioBuffer>(buffer), error);
	return std::make_pair(error, length);
}

std::pair<std::error_code, asio::ip::tcp::socket> accept(
	asio::ip::tcp::acceptor& a) {
	std::error_code error;
	auto socket = a.accept(error);
	return std::make_pair(error, std::move(socket));
}

std::pair<std::error_code, asio::ip::tcp::socket> connect(
	asio::io_context& io_context, std::string host, std::string port) {
	asio::ip::tcp::socket s(io_context);
	asio::ip::tcp::resolver resolver(io_context);
	std::error_code error;
	asio::connect(s, resolver.resolve(host, port), error);
	return std::make_pair(error, std::move(s));
}

class AsioExecutor : public async_simple::Executor {
public:
	AsioExecutor(asio::io_context& io_context)
		: m_io_context(io_context) {}

	virtual bool schedule(Func func) override {
		asio::post(m_io_context, std::move(func));
		return true;
	}

	asio::io_context& m_io_context;
};

template <typename T>
requires(!std::is_reference<T>::value) struct AsioCallbackAwaiter {
public:
	using CallbackFunction =
		std::function<void(std::coroutine_handle<>, std::function<void(T)>)>;

	AsioCallbackAwaiter(CallbackFunction callback_function)
		: callback_function_(std::move(callback_function)) {}

	bool await_ready() noexcept { return false; }

	void await_suspend(std::coroutine_handle<> handle) {
		callback_function_(handle, [this](T t) { result_ = std::move(t); });
	}

	auto coAwait(async_simple::Executor* executor) noexcept {
		return std::move(*this);
	}

	T await_resume() noexcept { return std::move(result_); }

private:
	CallbackFunction callback_function_;
	T result_;
};

inline async_simple::coro::Lazy<std::error_code> async_accept(
	asio::ip::tcp::acceptor& acceptor, asio::ip::tcp::socket& socket) noexcept {
	co_return co_await AsioCallbackAwaiter<std::error_code>{
		[&](std::coroutine_handle<> handle, auto set_resume_value) {
			acceptor.async_accept(
				socket, [handle, set_resume_value = std::move(
									 set_resume_value)](auto ec) mutable {
					set_resume_value(std::move(ec));
					handle.resume();
				});
		}};
}

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>> async_read_some(Socket& socket, AsioBuffer&& buffer) noexcept
	requires std::is_rvalue_reference<decltype(buffer)>::value {
	co_return co_await AsioCallbackAwaiter<std::pair<std::error_code, size_t>>{
		[&](std::coroutine_handle<> handle, auto set_resume_value) mutable {
			socket.async_read_some(std::move(buffer),
				[handle, set_resume_value = std::move(set_resume_value)](
					auto ec, auto size) mutable {
					set_resume_value(std::make_pair(std::move(ec), size));
					handle.resume();
				});
		}};
}

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>> async_read(
	Socket& socket, AsioBuffer& buffer) noexcept {
	co_return co_await AsioCallbackAwaiter<std::pair<std::error_code, size_t>>{
		[&](std::coroutine_handle<> handle, auto set_resume_value) mutable {
			asio::async_read(socket, buffer,
				[handle, set_resume_value = std::move(set_resume_value)](
					auto ec, auto size) mutable {
					set_resume_value(std::make_pair(std::move(ec), size));
					handle.resume();
				});
		}};
}

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>> async_write(
	Socket& socket, AsioBuffer&& buffer) noexcept requires std::is_rvalue_reference<decltype(buffer)>::value {
	co_return co_await AsioCallbackAwaiter<std::pair<std::error_code, size_t>>{
		[&](std::coroutine_handle<> handle, auto set_resume_value) mutable {
			asio::async_write(socket, std::move(buffer),
				[handle, set_resume_value = std::move(set_resume_value)](
					auto ec, auto size) mutable {
					set_resume_value(std::make_pair(std::move(ec), size));
					handle.resume();
				});
		}};
}

inline async_simple::coro::Lazy<std::error_code> async_connect(asio::io_context& io_context, asio::ip::tcp::socket& socket,
	asio::ip::tcp::resolver::results_type& results_type, int32_t timeout = 5000) noexcept {
	asio::steady_timer steady_timer{io_context};
	co_return co_await AsioCallbackAwaiter<std::error_code>{
		[&](std::coroutine_handle<> handle, auto set_resume_value) mutable {
			auto done = std::make_shared<bool>(false);
			steady_timer.expires_after(std::chrono::milliseconds(timeout));
			steady_timer.async_wait([handle, done, set_resume_value](const std::error_code& ec) {
				if (*done)
					return;
				*done = true;
				set_resume_value(asio::error::timed_out);
				handle.resume();
			});
			asio::async_connect(socket, results_type, [handle, done, set_resume_value](std::error_code ec, auto&&) mutable {
				if (*done)
					return;
				*done = true;
				set_resume_value(std::move(ec));
				handle.resume();
			});
		}};
}

inline async_simple::coro::Lazy<std::tuple<std::error_code, asio::ip::tcp::resolver::results_type>> async_resolve(
	asio::ip::tcp::resolver& resolver, std::string& server_host, std::string& server_port) {
	co_return co_await AsioCallbackAwaiter<std::tuple<std::error_code, asio::ip::tcp::resolver::results_type>>{
		[&](std::coroutine_handle<> handle, auto set_resume_value) mutable {
			resolver.async_resolve(server_host.c_str(), server_port.c_str(),
				[handle, set_resume_value = std::move(set_resume_value)](std::error_code ec, asio::ip::tcp::resolver::results_type results) {
					set_resume_value(std::make_tuple(move(ec), std::move(results)));
					handle.resume();
				});
		}};
}

#endif //