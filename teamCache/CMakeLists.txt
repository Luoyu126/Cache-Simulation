project(teamCache)
add_library(teamCache SHARED cache.c lifecycle.c lookup.c access.c)
target_include_directories(teamCache PRIVATE ../common)
