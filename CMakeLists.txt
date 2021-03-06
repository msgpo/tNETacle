# tNETacle CMakeLists.txt
# ========================

project(tNETacle)
cmake_minimum_required(VERSION 2.8)

# CMake Configuration
# ------------------
set(CMAKE_MODULE_PATH ${CMAME_MODULE_PATH} ${CMAKE_SOURCE_DIR}/util/cmake/Modules/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(${CMAKE_SYSTEM_NAME} True)

include(${CMAKE_SOURCE_DIR}/CMakeLists.txt.local OPTIONAL)

# CMake global options
# --------------------

# None at the moment

# Global Packages search
# ----------------------
#find_package(ZLIB REQUIRED)
find_package(Yajl REQUIRED)
find_package(Tuntap)
find_package(OpenSSL REQUIRED)

if (ClientQT)
  find_package(Tclt REQUIRED)
endif()

if(WIN32)
	find_package(Event COMPONENTS core REQUIRED)
else()
	find_package(Event COMPONENTS core openssl REQUIRED)
endif()

if (NOT TUNTAP_FOUND)
    find_package(Tapcfg REQUIRED)
endif()

# Global CPP definitions
# ----------------------
add_definitions(-D${CMAKE_SYSTEM_NAME})

if (ClientQT)
  add_definitions(-DUSE_TCLT)
endif()

if (TUNTAP_FOUND)
    add_definitions(-DUSE_LIBTUNTAP)
else()
    if (TAPCFG_FOUND)
        add_definitions(-DUSE_TAPCFG)
    endif()
endif()

# Global include directories
# --------------------------


include_directories(${CMAKE_HOME_DIRECTORY}/include)
include_directories(${EVENT_INCLUDE_DIR})
#include_directories(${ZLIB_INCLUDE_DIR})
include_directories(${YAJL_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${TCLT_INCLUDE_DIR})

if (${CALM_INCLUDE_DIR})
        include_directories(${CALM_INCLUDE_DIR})
else()
        set(CALM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/sub/calm-containers/include)
        include_directories(${CALM_INCLUDE_DIR})
endif()


if (TUNTAP_FOUND)
	include_directories(${TUNTAP_INCLUDE_DIRS})
else()
	if (TAPCFG_FOUND)
		include_directories(${TAPCFG_INCLUDE_DIRS})
	endif()
endif()

if (ClientQT)
  include_directories(${TCLT_INCLUDE_DIR})
endif()

# Portable source files
# ------------------
set(SOURCES_LIST
  src/conf.c
  src/tun-compat.c
  #src/compress.c
  #src/pipeline.c
  src/mc.c
  src/hexdump.c
  src/server.c
  src/udp.c
  src/device.c
  src/frame.c
  src/coro.c
  src/sched.c
  src/dtls.c
  src/subset.c
  src/endpoint.c
  src/log_extern.c
)

set(HEADERS_LIST 
    include/compress.h
    include/hexdump.h
    include/log.h
    include/mc.h
    include/options.h
    include/pathnames.h
    include/server.h
    include/tnetacle.h
    include/tntexits.h
    include/tntsocket.h
    include/tun.h
    ${CALM_INCLUDE_DIR}/map.h
    ${CALM_INCLUDE_DIR}/vector.h
    include/client.h
    include/imsg.h
    include/wincompat.h
    include/udp.h
    include/device.h
    include/frame.h
    include/coro.h
    include/tntsched.h
    include/dtls.h
    include/subset.h
    include/endpoint.h
    include/log_extern.h
)

# Add Source for LibTclt
if (ClientQT)
  set(SOURCES_LIST ${SOURCES_LIST}
    src/client.c
    )

  set(HEADERS_LIST ${HEADERS_LIST}
    include/client.h
    )
endif()

# OS families specific things
# ---------------------------
if(UNIX)
	# Unix specific include directories
	# ------------------
	include_directories(/usr/include/)

	# Unix specific definitions
	# ------------------
  	add_definitions(-DUnix)

	# Unix specific source files
	# ------------------
    set(SOURCES_LIST ${SOURCES_LIST} ${HEADERS_LIST} sys/unix/log.c
		sys/unix/imsg.c sys/unix/imsg-buffer.c
		sys/unix/tnetacle.c sys/unix/tnetacled.c
		sys/unix/util.c sys/unix/conf.c
		sys/unix/tntsocket.c)
endif()

if (Windows)
	# CMake windows Configuration
	# ------------------
	set(CMAKE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR})
  	add_definitions(-DWindows)
    add_definitions(-DCORO_WIN32)


	# Windows source files
	# ------------------
	set(SOURCES_LIST ${SOURCES_LIST} ${HEADERS_LIST}
		sys/wnt/conf.c
		sys/wnt/log.c
		sys/wnt/tntsocket.c
		sys/wnt/winstrtonum.c
		sys/wnt/main.c
		sys/wnt/utils.c
	)
endif()

# OS specific things
# ------------------
if(Linux)
        set(CMAKE_SYSTEM_INCLUDE_PATH
                ${CMAKE_SYSTEM_INCLUDE_PATH} /usr/local/include)

	# Linux specific Options
	# ----------------------
        option(ENABLE_BSDCOMPAT "Enable the use of the libbsd" OFF)

	# Linux specific package search
	# -----------------------------
	if (ENABLE_BSDCOMPAT)
                find_package(Bsd)
	endif()

        # Definition for the coroutine backend
        # ------------------------------------
        add_definitions(-DCORO_SJLJ)

	# Linux specific definitions
	# --------------------------
	add_definitions(-D_GNU_SOURCE)
	add_definitions(-DHAVE_SETRESXID)
        if (ENABLE_BSDCOMPAT AND BSD_FOUND)
		add_definitions(-DHAVE_BSD_COMPAT)
                add_definitions(-DHAVE_SETPROCTITLE)
	endif()

        # Linux specifig include directories
	# ----------------------------------
	include_directories(/usr/local/include)
        if (ENABLE_BSDCOMPAT AND BSD_FOUND)
                include_directories(${BSD_INCLUDE_DIRS})
	endif()
endif()

if (OpenBSD)
        add_definitions(-DCORO_SJLJ)
        set(CMAKE_SYSTEM_INCLUDE_PATH
                ${CMAKE_SYSTEM_INCLUDE_PATH} /usr/local/include)
	include_directories(/usr/local/include)
	add_definitions(-DHAVE_SETPROCTITLE)
	add_definitions(-DHAVE_SETRESXID)
endif()

if (NetBSD)
        add_definitions(-DCORO_SJLJ)
        set(CMAKE_SYSTEM_INCLUDE_PATH
                ${CMAKE_SYSTEM_INCLUDE_PATH} /usr/pkg/include)
	include_directories(/usr/pkg/include)
	add_definitions(-DHAVE_SETPROCTITLE)
endif()

if (FreeBSD)
        add_definitions(-DCORO_SJLJ)
        set(CMAKE_SYSTEM_INCLUDE_PATH
                ${CMAKE_SYSTEM_INCLUDE_PATH} /usr/local/include)
	include_directories(/usr/local/include)
	add_definitions(-DHAVE_SETPROCTITLE)
	add_definitions(-DHAVE_SETRESXID)
endif()

if (Darwin)
        add_definitions(-DCORO_SJLJ)
	# Darwin specific include directories (macport)
	# ------------------
        set(CMAKE_SYSTEM_INCLUDE_PATH
                ${CMAKE_SYSTEM_INCLUDE_PATH} /opt/local/include)
	include_directories(/opt/local/include)

	# Darwin specific definitions
	# ------------------
endif()

# Executable definitons
# ------------------
add_executable(tNETacle ${SOURCES_LIST})


# Global linked libraries
# ------------------

target_link_libraries(tNETacle
        ${EVENT_LIBRARIES}
        ${YAJL_LIBRARIES}
        ${OPENSSL_LIBRARIES}
)
# ${ZLIB_LIBRARY}

if (TUNTAP_FOUND)
  target_link_libraries(tNETacle ${TUNTAP_LIBRARIES})
elseif(TAPCFG_FOUND)
  target_link_libraries(tNETacle ${TAPCFG_LIBRARIES})
endif()

if (ClientQT)
  target_link_libraries(tNETacle ${TCLT_LIBRARY})
endif()

# Linux linked libraries
# ------------------------
if (ENABLE_BSDCOMPAT AND BSD_FOUND)
  target_link_libraries(tNETacle ${BSD_LIBRARIES})
endif()

# Windows linked libraries
# ------------------------

if (Windows)
    target_link_libraries(tNETacle ws2_32.lib)
endif()

if (ClientQT)
  include(${CMAKE_SOURCE_DIR}/client/qt/CMakeLists.txt)
endif()
if (TNT_BROADCAST)
  include(${CMAKE_SOURCE_DIR}/client/tnt_broadcast/CMakeLists.txt)
endif()

include(CMakeLists.txt.local OPTIONAL)
include(${CMAKE_SOURCE_DIR}/CMakeLists.txt.link.local OPTIONAL)
