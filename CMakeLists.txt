# GFOpenNews host build: the NNTP core, .newsrc, overview cache, the CLI and
# their unit tests. NETKIT_DIR is a gfopennetkit checkout (default: next to
# this one). MIT licence, (c) 2026 AmiCradle Team.
cmake_minimum_required(VERSION 3.10)
project(gfopennews C)
set(CMAKE_C_STANDARD 99)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Werror)
endif()

set(NETKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gfopennetkit" CACHE PATH "gfopennetkit checkout")
add_subdirectory(${NETKIT_DIR} netkit)

add_library(gfopennews_core STATIC src/nntp.c src/newsrc.c src/overview.c
  src/decode.c src/partfile.c src/nzb.c src/queue.c src/md5.c src/par2.c src/path.c src/ui_model.c src/score.c)
target_include_directories(gfopennews_core PUBLIC include)
target_link_libraries(gfopennews_core PUBLIC gfopennetkit)

# The reader window needs GFOpenLayout's UI (olui) built for the host: its
# sources from a gfopenlayout checkout (default: next to this one).
set(GFOPENLAYOUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gfopenlayout" CACHE PATH "gfopenlayout checkout")
if(EXISTS ${GFOPENLAYOUT_DIR}/include/gfopenlayout_ui.h)
  set(OL ${GFOPENLAYOUT_DIR})
  add_library(olui_host STATIC
    ${OL}/src/openlayout.c ${OL}/src/gfcss_parse.c ${OL}/src/gfcss_values.c ${OL}/src/gfcss_select.c
    ${OL}/src/openlayout_css.c ${OL}/ui/olui.c ${OL}/ui/olui_css.c ${OL}/ui/olui_markup.c ${OL}/ui/olui_theme_none.c)
  target_include_directories(olui_host PUBLIC ${OL}/include PRIVATE ${OL}/ui ${OL}/src)
  add_library(gfopennews_ui STATIC src/ui_window.c)
  target_link_libraries(gfopennews_ui PUBLIC gfopennews_core olui_host)
  set(HAVE_OLUI 1)
else()
  message(STATUS "gfopenlayout not found at ${GFOPENLAYOUT_DIR}: the reader window and its test are left out")
endif()

add_executable(gfopennews-cli src/cli.c src/plat_posix.c)
target_compile_definitions(gfopennews-cli PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(gfopennews-cli gfopennews_core)

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
foreach(t nntp newsrc overview binaries nzb queue_journal par2 path ui_model score)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} gfopennews_core)
  target_include_directories(test_${t} PRIVATE ${NETKIT_DIR}/tests)
endforeach()
add_test(NAME nntp COMMAND test_nntp)
add_test(NAME newsrc COMMAND test_newsrc ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME overview COMMAND test_overview ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME binaries COMMAND test_binaries ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME nzb COMMAND test_nzb)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qj)
add_test(NAME queue_journal COMMAND test_queue_journal ${CMAKE_CURRENT_BINARY_DIR}/qj)
add_test(NAME par2 COMMAND test_par2 ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME path COMMAND test_path)
add_test(NAME ui_model COMMAND test_ui_model)
add_test(NAME score COMMAND test_score)
if(HAVE_OLUI)
  add_executable(test_ui_window tests/test_ui_window.c)
  target_link_libraries(test_ui_window gfopennews_ui)
  target_include_directories(test_ui_window PRIVATE ${NETKIT_DIR}/tests)
  add_test(NAME ui_window COMMAND test_ui_window)
endif()
if(Python3_FOUND)
  add_test(NAME cli_fake_server
    COMMAND ${Python3_EXECUTABLE} ${NETKIT_DIR}/tests/fakes/fakeserv.py --run nntp,socks5 --
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py
            $<TARGET_FILE:gfopennews-cli> ${CMAKE_CURRENT_BINARY_DIR})
  # the fake server writes each posted article here, for test_cli.py to check
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/posted)
  set_tests_properties(cli_fake_server PROPERTIES
    ENVIRONMENT "NK_NNTP_POSTED=${CMAKE_CURRENT_BINARY_DIR}/posted")
  add_test(NAME queue_fake_servers
    COMMAND ${Python3_EXECUTABLE} ${NETKIT_DIR}/tests/fakes/fakeserv.py --run nntp,nntp2 --
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_queue.py
            $<TARGET_FILE:gfopennews-cli> ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(queue_fake_servers PROPERTIES ENVIRONMENT
    "NK_NNTP_SPOOL=${CMAKE_CURRENT_BINARY_DIR}/spool1;NK_NNTP2_SPOOL=${CMAKE_CURRENT_BINARY_DIR}/spool2")
  add_test(NAME par2_cli
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_par2.py
            $<TARGET_FILE:gfopennews-cli> ${CMAKE_CURRENT_BINARY_DIR})
  add_test(NAME par2_queue
    COMMAND ${Python3_EXECUTABLE} ${NETKIT_DIR}/tests/fakes/fakeserv.py --run nntp --
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_par2_queue.py
            $<TARGET_FILE:gfopennews-cli> ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(par2_queue PROPERTIES ENVIRONMENT
    "NK_NNTP_SPOOL=${CMAKE_CURRENT_BINARY_DIR}/spool-par2")
endif()
