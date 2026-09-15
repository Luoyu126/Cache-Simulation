project(teamCache)
add_library(teamCache SHARED cache.c)
target_include_directories(teamCache PRIVATE ../common)
