cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# C++20 required for WinRT / cppwinrt
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/now-playing-source.cpp
    src/smtc-reader.cpp
    src/browser-bridge.cpp
)

if(WIN32)
  # WinRT: required for SMTC (System Media Transport Controls)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE WindowsApp)

  # obs-browser headers: look in the OBS source fetched by buildspec
  file(GLOB _obs_studio_source_dirs "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-*")
  foreach(_dir IN LISTS _obs_studio_source_dirs)
    if(EXISTS "${_dir}/plugins/obs-browser/obs-browser-api.h")
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${_dir}/plugins/obs-browser")
      message(STATUS "Found obs-browser headers: ${_dir}/plugins/obs-browser")
      break()
    endif()
  endforeach()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(WIN32)
  set(CPACK_GENERATOR "NSIS")
  set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
  set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
  set(CPACK_PACKAGE_VENDOR "zapdel")
  # $INSTDIR = CPACK_NSIS_INSTALL_ROOT\CPACK_PACKAGE_INSTALL_DIRECTORY
  # Install rules place files under zapdel-now-playing/bin/64bit/ and zapdel-now-playing/data/
  # so $INSTDIR must resolve to %ProgramData%\obs-studio\plugins
  set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMDATA\\obs-studio")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "plugins")
  set(CPACK_NSIS_DISPLAY_NAME "Now Playing OBS Plugin")
  set(CPACK_NSIS_PACKAGE_NAME "Now Playing OBS Plugin ${CMAKE_PROJECT_VERSION}")
  set(CPACK_NSIS_UNINSTALL_NAME "Uninstall Now Playing OBS Plugin")
  include(CPack)
endif()
