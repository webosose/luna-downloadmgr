# Copyright (c) 2012-2019 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 2.8.7)

include(webOS/webOS)

webos_modules_init(1 0 0 QUALIFIER RC4)
webos_component(4 0 0)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS_OTHER})

pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
include_directories(${GTHREAD2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GTHREAD2_CFLAGS_OTHER})

pkg_check_modules(LUNA_PREFS REQUIRED luna-prefs)
include_directories(${LUNA_PREFS_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNA_PREFS_CFLAGS_OTHER})

pkg_check_modules(LS2 REQUIRED luna-service2)
include_directories(${LS2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LS2_CFLAGS_OTHER})

pkg_check_modules(PBNJSON_CPP REQUIRED pbnjson_cpp)
include_directories(${PBNJSON_CPP_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_CPP_CFLAGS_OTHER})

pkg_check_modules(PMLOGLIB REQUIRED PmLogLib)
include_directories(${PMLOGLIB_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOGLIB_CFLAGS_OTHER})

pkg_check_modules(CURL REQUIRED libcurl)
include_directories(${CURL_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${CURL_CFLAGS_OTHER})

pkg_check_modules(SQLITE3 REQUIRED sqlite3)
include_directories(${SQLITE3_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${SQLITE3_CFLAGS_OTHER})

find_package(Boost REQUIRED COMPONENTS regex)
include_directories(${Boost_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${Boost_CFLAGS_OTHER})

# Environment
set(BIN_NAME LunaDownloadMgr)
file(GLOB_RECURSE SRC_INTERNAL ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE SRC_EXTERNAL ${PROJECT_SOURCE_DIR}/external/*.c)

include_directories(src)
include_directories(external)
include_directories(${WEBOS_BINARY_CONFIGURED_DIR})

webos_include_install_paths()
webos_add_compiler_flags(ALL CXX -std=c++0x)
webos_add_compiler_flags(ALL -Wall -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64)
add_executable(${BIN_NAME} ${SRC_EXTERNAL} ${SRC_INTERNAL})

# Link
set(LIBS
    ${GLIB2_LDFLAGS}
    ${GTHREAD2_LDFLAGS}
    ${LUNA_PREFS_LDFLAGS}
    ${LS2_LDFLAGS}
    ${PMLOGLIB_LDFLAGS}
    ${CURL_LDFLAGS}
    ${PBNJSON_CPP_LDFLAGS}
    ${SQLITE3_LDFLAGS}
    ${Boost_LIBRARIES}
    pthread
    uriparser
)
target_link_libraries(${BIN_NAME} ${LIBS})

# Install
install(TARGETS ${BIN_NAME} DESTINATION ${WEBOS_INSTALL_SBINDIR})

webos_distro_variant_dep()

set (${CMAKE_PROJECT_NAME}_SERVICE_TYPE "static")
set (${CMAKE_PROJECT_NAME}_ADD_START_COMMAND "start on rest-boot-done")
set (${CMAKE_PROJECT_NAME}_ADD_RESPAWN_OPTION "respawn")

# install luna-service configurations
webos_build_system_bus_files()

file(GLOB_RECURSE SCHEMAS files/schema/*.schema)
install(FILES ${SCHEMAS} DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}/schemas/luna-downloadmgr)

# install downloadManager.conf and downloadManager-platform.conf
configure_file(${CMAKE_SOURCE_DIR}/files/conf/downloadManager.conf.in
               ${CMAKE_BINARY_DIR}/files/conf/downloadManager.conf @ONLY)

install(FILES ${CMAKE_BINARY_DIR}/files/conf/downloadManager.conf
        DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

if (EXISTS ${CMAKE_SOURCE_DIR}/files/conf/downloadManager-${MACHINE}.conf.in)
	configure_file(${CMAKE_SOURCE_DIR}/files/conf/downloadManager-${MACHINE}.conf.in
	               ${CMAKE_BINARY_DIR}/files/conf/downloadManager-platform.conf @ONLY)

	install(FILES ${CMAKE_BINARY_DIR}/files/conf/downloadManager-platform.conf
	        DESTINATION ${WEBOS_INSTALL_WEBOS_SYSCONFDIR}
	        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()

webos_config_build_doxygen(files/doc Doxyfile)
