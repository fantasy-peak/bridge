#pragma once

#include <sw/redis++/async_redis++.h>
#include <boost/thread/executors/basic_thread_pool.hpp>

struct Context {
	std::unique_ptr<sw::redis::AsyncRedis> async_redis_ptr;
	std::unique_ptr<boost::executors::basic_thread_pool> basic_thread_pool_ptr;
};
