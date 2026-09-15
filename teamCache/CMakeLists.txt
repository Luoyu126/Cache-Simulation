project(teamCache)
add_library(teamCache SHARED cache.c lifecycle.c lookup.c access.c eviction.c)
target_include_directories(teamCache PRIVATE ../common)
