cmake_minimum_required(VERSION 3.5)

project(EXPAT)

include(${CMAKE_CURRENT_LIST_DIR}/ConfigureChecks.cmake)

macro(expat_shy_set var default cache type desc)
    # Macro expat_shy_set came into life because:
    # - Expat was previously using an inconsistent mix of CMake's native set()
    #   and option() to define public build time options.
    # - option() is more friendly than set() with regard to configuring an
    #   external project that is pulled in by means of add_subdirectory() --
    #   see comments in issue #597 -- so we wanted to get away from set().
    # - option() auto-converts non-bool values to bool when writing to the CMake
    #   cache, so we needed something that supports non-bool better and hence
    #   wanted to get away from plain option(), too.
    #
    # As a result, this function serves as a hybrid between CMake's regular set()
    # and option(): from set() it takes support for non-bool types and the function
    # name and signature whereas from option() (with policy CMP0077 mode NEW) it
    # takes being shy when a value has previously been defined for that variable.
    #
    # So that resolves all need for set(.. FORCE) when pulling in Expat by means of
    # add_subdirectory().
    #
    if(NOT ${cache} STREQUAL "CACHE")
        message(SEND_ERROR "Macro usage is: expat_shy_set(var default CACHE type desc)")
    endif()

    if(DEFINED ${var})
        # NOTE: The idea is to (ideally) only add to the cache if
        #       there is no cache entry, yet.  "if(DEFINED CACHE{var})"
        #       requires CMake >=3.14.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.14" AND NOT DEFINED "CACHE{${var}}")
            set("${var}" "${${var}}" CACHE "${type}" "${desc}")
        endif()
    else()
        set("${var}" "${default}" CACHE "${type}" "${desc}")
    endif()
endmacro()

if(NOT WIN32)
    expat_shy_set(EXPAT_WITH_GETRANDOM "AUTO" CACHE STRING "Make use of getrandom function (ON|OFF|AUTO) [default=AUTO]")
    expat_shy_set(EXPAT_WITH_SYS_GETRANDOM "AUTO" CACHE STRING "Make use of syscall SYS_getrandom (ON|OFF|AUTO) [default=AUTO]")
endif()

macro(evaluate_detection_results use_ref have_ref thing_lower thing_title)
    if(${use_ref} AND NOT (${use_ref} STREQUAL "AUTO") AND NOT ${have_ref})
        message(SEND_ERROR
                "Use of ${thing_lower} was enforced by ${use_ref}=ON but it could not be found.")
    elseif(NOT ${use_ref} AND ${have_ref})
        message("${thing_title} was found but it will not be used due to ${use_ref}=OFF.")
        set(${have_ref} 0)
    endif()
endmacro()

if(NOT WIN32)
    evaluate_detection_results(EXPAT_WITH_GETRANDOM HAVE_GETRANDOM "function getrandom" "Function getrandom")
    evaluate_detection_results(EXPAT_WITH_SYS_GETRANDOM HAVE_SYSCALL_GETRANDOM "syscall SYS_getrandom" "Syscall SYS_getrandom")
endif()

# Keep the vendored build's historical feature set while using Expat 2.9's
# refreshed parser sources. These values are required by the 2.9 templates.
set(XML_CONTEXT_BYTES 1024)
set(XML_GE 1)
configure_file(expat_configure.h.cmake "${CMAKE_CURRENT_BINARY_DIR}/expat_configure.h")

if (BUILD_SHARED_LIBS AND MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(EXPAT_SOURCES
    xmlparse.c
    xmlrole.c
    xmltok.c
    xcs.c
    random_arc4random.c
    random_arc4random_buf.c
    random_dev_urandom.c
    random_getentropy.c
)
if(HAVE_SYSCALL_GETRANDOM)
    list(APPEND EXPAT_SOURCES random_getrandom.c)
endif()
if(WIN32)
    list(APPEND EXPAT_SOURCES random_rand_s.c)
endif()
add_library(expat ${EXPAT_SOURCES})

target_include_directories(expat PRIVATE ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

include(GNUInstallDirs)

install( 
    FILES
        ${PROJECT_SOURCE_DIR}/expat.h
        ${PROJECT_SOURCE_DIR}/expat_config.h
        ${PROJECT_SOURCE_DIR}/expat_external.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}
)

add_library(EXPAT INTERFACE)
target_link_libraries(EXPAT INTERFACE expat)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"     
    VERSION 2.9.0
    COMPATIBILITY AnyNewerVersion
)

install(TARGETS expat EXPAT
  EXPORT ${PROJECT_NAME}Targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} 
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

export(EXPORT ${PROJECT_NAME}Targets 
       FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Targets.cmake" 
       NAMESPACE ${PROJECT_NAME}:: )

set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(EXPORT ${PROJECT_NAME}Targets
  FILE
    "${PROJECT_NAME}Targets.cmake"
  NAMESPACE
    ${PROJECT_NAME}::
  DESTINATION
    ${ConfigPackageLocation}
)

configure_file(config.cmake.in ${PROJECT_NAME}Config.cmake @ONLY)

install(
    FILES
      "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION
      ${ConfigPackageLocation}
)

