#pragma once


#include "cfg.h"
#include "context.h"

inline std::unique_ptr<sw::redis::AsyncRedis> createRedisClient(const RedisConfig& redis_config) {
	sw::redis::ConnectionOptions opt;
	if (redis_config.connection_options.type)
		opt.type = static_cast<sw::redis::ConnectionType>(redis_config.connection_options.type.value());
	opt.host = redis_config.connection_options.host;
	if (redis_config.connection_options.port)
		opt.port = redis_config.connection_options.port.value();
	if (redis_config.connection_options.path)
		opt.path = redis_config.connection_options.path.value();
	if (redis_config.connection_options.user)
		opt.user = redis_config.connection_options.user.value();
	if (redis_config.connection_options.password)
		opt.password = redis_config.connection_options.password.value();
	opt.db = redis_config.connection_options.db;
	if (redis_config.connection_options.keep_alive)
		opt.keep_alive = redis_config.connection_options.keep_alive.value();
	if (redis_config.connection_options.connect_timeout)
		opt.connect_timeout = redis_config.connection_options.connect_timeout.value();
	if (redis_config.connection_options.socket_timeout)
		opt.socket_timeout = redis_config.connection_options.socket_timeout.value();
	sw::redis::ConnectionPoolOptions pool_options;
	pool_options.size = redis_config.connection_pool_options.size;
	if (redis_config.connection_pool_options.wait_timeout)
		pool_options.wait_timeout = redis_config.connection_pool_options.wait_timeout.value();
	if (redis_config.connection_pool_options.connection_lifetime)
		pool_options.connection_lifetime = redis_config.connection_pool_options.connection_lifetime.value();
	return std::make_unique<sw::redis::AsyncRedis>(opt, pool_options);
}

inline std::unique_ptr<Context> createContext(const RedisConfig& redis_config) {
	return std::make_unique<Context>(createRedisClient(redis_config), std::make_unique<boost::executors::basic_thread_pool>(redis_config.threads));
}
