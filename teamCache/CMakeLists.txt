project(teamCache)
add_library(teamCache SHARED cache.c lifecycle.c)
target_include_directories(teamCache PRIVATE ../common)
