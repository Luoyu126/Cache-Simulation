project(teamCache)
add_library(teamCache SHARED cache.c lifecycle.c lookup.c access.c eviction.c
                            replacement.c request_queue.c split.c write_buffer.c)
target_include_directories(teamCache PRIVATE ../common)
