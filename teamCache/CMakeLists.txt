project(teamCache)
add_library(teamCache SHARED cache.c lifecycle.c lookup.c)
target_include_directories(teamCache PRIVATE ../common)
