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

class AcceptorAwaiter {
public:
	AcceptorAwaiter(asio::ip::tcp::acceptor& acceptor,
		asio::ip::tcp::socket& socket)
		: acceptor_(acceptor)
		, socket_(socket) {}
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> handle) {
		acceptor_.async_accept(socket_, [this, handle](auto ec) mutable {
			ec_ = ec;
			handle.resume();
		});
	}
	auto await_resume() noexcept { return ec_; }
	auto coAwait(async_simple::Executor* executor) noexcept {
		return std::move(*this);
	}

private:
	asio::ip::tcp::acceptor& acceptor_;
	asio::ip::tcp::socket& socket_;
	std::error_code ec_{};
};

inline async_simple::coro::Lazy<std::error_code> async_accept(
	asio::ip::tcp::acceptor& acceptor, asio::ip::tcp::socket& socket) noexcept {
	co_return co_await AcceptorAwaiter{acceptor, socket};
}

template <typename Socket, typename AsioBuffer>
struct ReadSomeAwaiter {
public:
	ReadSomeAwaiter(Socket& socket, AsioBuffer&& buffer)
		: socket_(socket)
		, buffer_(buffer) {}
	bool await_ready() { return false; }
	auto await_resume() { return std::make_pair(ec_, size_); }
	void await_suspend(std::coroutine_handle<> handle) {
		socket_.async_read_some(std::move(buffer_),
			[this, handle](auto ec, auto size) mutable {
				ec_ = ec;
				size_ = size;
				handle.resume();
			});
	}
	auto coAwait(async_simple::Executor* executor) noexcept {
		return std::move(*this);
	}

private:
	Socket& socket_;
	AsioBuffer buffer_;

	std::error_code ec_{};
	size_t size_{0};
};

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>>
async_read_some(Socket& socket, AsioBuffer&& buffer) noexcept {
	co_return co_await ReadSomeAwaiter{socket, std::move(buffer)};
}

template <typename Socket, typename AsioBuffer>
struct ReadAwaiter {
public:
	ReadAwaiter(Socket& socket, AsioBuffer& buffer)
		: socket_(socket)
		, buffer_(buffer) {}
	bool await_ready() { return false; }
	auto await_resume() { return std::make_pair(ec_, size_); }
	void await_suspend(std::coroutine_handle<> handle) {
		asio::async_read(socket_, buffer_,
			[this, handle](auto ec, auto size) mutable {
				ec_ = ec;
				size_ = size;
				handle.resume();
			});
	}
	auto coAwait(async_simple::Executor* executor) noexcept {
		return std::move(*this);
	}

private:
	Socket& socket_;
	AsioBuffer& buffer_;

	std::error_code ec_{};
	size_t size_{0};
};

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>> async_read(
	Socket& socket, AsioBuffer& buffer) noexcept {
	co_return co_await ReadAwaiter{socket, buffer};
}

template <typename Socket, typename AsioBuffer>
struct ReadUntilAwaiter {
public:
	ReadUntilAwaiter(Socket& socket, AsioBuffer& buffer,
		asio::string_view delim)
		: socket_(socket)
		, buffer_(buffer)
		, delim_(delim) {}
	bool await_ready() { return false; }
	auto await_resume() { return std::make_pair(ec_, size_); }
	void await_suspend(std::coroutine_handle<> handle) {
		asio::async_read_until(socket_, buffer_, delim_,
			[this, handle](auto ec, auto size) mutable {
				ec_ = ec;
				size_ = size;
				handle.resume();
			});
	}
	auto coAwait(async_simple::Executor* executor) noexcept {
		return std::move(*this);
	}

private:
	Socket& socket_;
	AsioBuffer& buffer_;
	asio::string_view delim_;

	std::error_code ec_{};
	size_t size_{0};
};

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>>
async_read_until(Socket& socket, AsioBuffer& buffer,
	asio::string_view delim) noexcept {
	co_return co_await ReadUntilAwaiter{socket, buffer, delim};
}

template <typename Socket, typename AsioBuffer>
struct WriteAwaiter {
public:
	WriteAwaiter(Socket& socket, AsioBuffer&& buffer)
		: socket_(socket)
		, buffer_(std::move(buffer)) {}
	bool await_ready() { return false; }
	auto await_resume() { return std::make_pair(ec_, size_); }
	void await_suspend(std::coroutine_handle<> handle) {
		asio::async_write(socket_, std::move(buffer_),
			[this, handle](auto ec, auto size) mutable {
				ec_ = ec;
				size_ = size;
				handle.resume();
			});
	}
	auto coAwait(async_simple::Executor* executor) noexcept {
		return std::move(*this);
	}

private:
	Socket& socket_;
	AsioBuffer buffer_;

	std::error_code ec_{};
	size_t size_{0};
};

template <typename Socket, typename AsioBuffer>
inline async_simple::coro::Lazy<std::pair<std::error_code, size_t>> async_write(
	Socket& socket, AsioBuffer&& buffer) noexcept {
	co_return co_await WriteAwaiter{socket, std::move(buffer)};
}

class ConnectAwaiter {
public:
	ConnectAwaiter(asio::io_context& io_context, asio::ip::tcp::socket& socket,
		asio::ip::tcp::resolver::results_type& results_type, int32_t timeout)
		: io_context_(io_context)
		, m_socket(socket)
		, m_results_type(results_type)
		, m_steady_timer(io_context)
		, m_timeout(timeout) {
	}

	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> handle) {
		auto done = std::make_shared<bool>(false);
		m_steady_timer.expires_after(std::chrono::milliseconds(m_timeout));
		m_steady_timer.async_wait([this, handle, done](const std::error_code& ec) {
			if (*done)
				return;
			*done = true;
			m_ec = asio::error::timed_out;
			handle.resume();
		});
		asio::async_connect(m_socket, m_results_type, [this, handle, done](std::error_code ec, auto&&) mutable {
			if (*done)
				return;
			*done = true;
			m_ec = std::move(ec);
			handle.resume();
		});
	}
	auto await_resume() noexcept { return m_ec; }

private:
	asio::io_context& io_context_;
	asio::ip::tcp::socket& m_socket;
	asio::ip::tcp::resolver::results_type& m_results_type;
	asio::steady_timer m_steady_timer;
	int32_t m_timeout;
	std::error_code m_ec{};
};

inline async_simple::coro::Lazy<std::error_code> async_connect(asio::io_context& io_context, asio::ip::tcp::socket& socket,
	asio::ip::tcp::resolver::results_type& results_type, int32_t timeout = 5000) noexcept {
	co_return co_await ConnectAwaiter{io_context, socket, results_type, timeout};
}

class ResolveAwaiter {
public:
	ResolveAwaiter(asio::ip::tcp::resolver& resolver, std::string& server_host, std::string& server_port)
		: m_resolver(resolver)
		, m_server_host(server_host)
		, m_server_port(server_port) {
	}

	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> handle) {
		m_resolver.async_resolve(m_server_host.c_str(), m_server_port.c_str(),
			[this, handle](std::error_code ec, asio::ip::tcp::resolver::results_type results) {
				m_results_type = std::move(results);
				m_ec = std::move(ec);
				handle.resume();
			});
	}
	auto await_resume() noexcept { return std::make_tuple(std::move(m_ec), std::move(m_results_type)); }

private:
	asio::ip::tcp::resolver& m_resolver;
	std::string& m_server_host;
	std::string& m_server_port;
	std::error_code m_ec{};
	asio::ip::tcp::resolver::results_type m_results_type;
};

inline async_simple::coro::Lazy<std::tuple<std::error_code, asio::ip::tcp::resolver::results_type>> async_resolve(
	asio::ip::tcp::resolver& resolver, std::string& server_host, std::string& server_port) {
	co_return co_await ResolveAwaiter{resolver, server_host, server_port};
}

#endif //