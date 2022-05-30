#pragma once

#include <drogon/drogon.h>
#include <yaml_cpp_struct.hpp>

struct ConnectionOptions {
	std::optional<uint8_t> type;
	std::string host;
	std::optional<int> port;
	std::optional<std::string> path;
	std::optional<std::string> user;
	std::optional<std::string> password;
	int db{0};
	std::optional<bool> keep_alive;
	std::optional<std::chrono::milliseconds> connect_timeout;
	std::optional<std::chrono::milliseconds> socket_timeout;
};
YCS_ADD_STRUCT(ConnectionOptions, type, host, port, path, user, password, db, keep_alive, connect_timeout, socket_timeout)

struct ConnectionPoolOptions {
	std::size_t size;
	std::optional<std::chrono::milliseconds> wait_timeout;
	std::optional<std::chrono::milliseconds> connection_lifetime;
};
YCS_ADD_STRUCT(ConnectionPoolOptions, size, wait_timeout, connection_lifetime)

struct RedisConfig {
	ConnectionOptions connection_options;
	ConnectionPoolOptions connection_pool_options;
	uint16_t threads;
};
YCS_ADD_STRUCT(RedisConfig, connection_options, connection_pool_options, threads)

YCS_ADD_ENUM(drogon::HttpMethod, Get, Post, Head, Put, Delete, Options, Patch, Invalid)

struct Config {
	std::string path;
	std::string endpoint;
	std::vector<drogon::HttpMethod> methods;
	std::optional<std::string> before_func_name;
	std::optional<std::string> after_func_name;
};
YCS_ADD_STRUCT(Config, path, endpoint, before_func_name, after_func_name)

struct BridgeConfig {
	std::string http_server_config;
	std::string interface_lib_path;
	RedisConfig redis_config;
	std::vector<Config> configs;
};
YCS_ADD_STRUCT(BridgeConfig, http_server_config, interface_lib_path, redis_config, configs)
