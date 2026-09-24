# Install script for directory: /mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so.0.24.5"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so.0.24"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      file(RPATH_CHECK
           FILE "${file}"
           RPATH "")
    endif()
  endforeach()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES
    "/mnt/d/code/project/Go2ProController/client/build2/_deps/libdatachannel-build/libdatachannel.so.0.24.5"
    "/mnt/d/code/project/Go2ProController/client/build2/_deps/libdatachannel-build/libdatachannel.so.0.24"
    )
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so.0.24.5"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so.0.24"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      if(CMAKE_INSTALL_DO_STRIP)
        execute_process(COMMAND "/usr/bin/strip" "${file}")
      endif()
    endif()
  endforeach()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/mnt/d/code/project/Go2ProController/client/build2/_deps/libdatachannel-build/libdatachannel.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libdatachannel.so")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/rtc" TYPE FILE FILES
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/candidate.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/channel.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/configuration.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/datachannel.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/dependencydescriptor.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/description.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/iceudpmuxlistener.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/mediahandler.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtcpreceivingsession.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/common.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/global.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/message.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/frameinfo.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/peerconnection.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/reliability.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtc.h"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtc.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtp.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/track.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/websocket.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/websocketserver.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtppacketizationconfig.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtcpsrreporter.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtppacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtpdepacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/h264rtppacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/h264rtpdepacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/nalunit.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/h265rtppacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/h265rtpdepacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/h265nalunit.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/av1rtppacketizer.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rtcpnackresponder.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/utils.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/plihandler.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/pacinghandler.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/rembhandler.hpp"
    "/mnt/d/code/project/Go2ProController/client/build/_deps/libdatachannel-src/include/rtc/version.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake"
         "/mnt/d/code/project/Go2ProController/client/build2/_deps/libdatachannel-build/CMakeFiles/Export/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel" TYPE FILE FILES "/mnt/d/code/project/Go2ProController/client/build2/_deps/libdatachannel-build/CMakeFiles/Export/lib/cmake/LibDataChannel/LibDataChannelTargets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel" TYPE FILE FILES "/mnt/d/code/project/Go2ProController/client/build2/_deps/libdatachannel-build/CMakeFiles/Export/lib/cmake/LibDataChannel/LibDataChannelTargets-release.cmake")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibDataChannel" TYPE FILE FILES
    "/mnt/d/code/project/Go2ProController/client/build2/LibDataChannelConfig.cmake"
    "/mnt/d/code/project/Go2ProController/client/build2/LibDataChannelConfigVersion.cmake"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.

endif()

