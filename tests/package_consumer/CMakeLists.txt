# Builds against an installed libnmo to check the exported package.
#   cmake -S tests/package_consumer -B <dir> -DCMAKE_PREFIX_PATH=<install prefix>
cmake_minimum_required(VERSION 3.15)
project(libnmo_package_consumer LANGUAGES C)

find_package(libnmo CONFIG REQUIRED)

add_executable(package_consumer main.c)
target_link_libraries(package_consumer PRIVATE nmo::project nmo::lua nmo::nmo)
set_target_properties(package_consumer PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)
