set(_curl_platform_flags 
  -DENABLE_IPV6:BOOL=ON
  -DENABLE_VERSIONED_SYMBOLS:BOOL=ON
  -DENABLE_THREADED_RESOLVER:BOOL=ON
  -DENABLE_MANUAL:BOOL=OFF
  -DCURL_DISABLE_LDAP:BOOL=ON
  -DCURL_DISABLE_LDAPS:BOOL=ON
  -DCURL_DISABLE_RTSP:BOOL=ON
  -DCURL_DISABLE_DICT:BOOL=ON
  -DCURL_DISABLE_TELNET:BOOL=ON
  -DCURL_DISABLE_POP3:BOOL=ON
  -DCURL_DISABLE_IMAP:BOOL=ON
  -DCURL_DISABLE_SMB:BOOL=ON
  -DCURL_DISABLE_SMTP:BOOL=ON
  -DCURL_DISABLE_GOPHER:BOOL=ON
  -DCURL_DISABLE_TFTP:BOOL=ON
  -DCURL_DISABLE_MQTT:BOOL=ON
  #-DHTTP_ONLY=ON

  -DCMAKE_USE_GSSAPI:BOOL=OFF
  -DCMAKE_USE_LIBSSH2:BOOL=OFF
  -DUSE_RTMP:BOOL=OFF
  -DUSE_NGHTTP2:BOOL=OFF
  -DUSE_MBEDTLS:BOOL=OFF
  -DCURL_USE_LIBPSL:BOOL=OFF
  # Keep the dependency closure explicit; these codecs are not bundled here.
  -DCURL_BROTLI:BOOL=OFF
  -DCURL_ZSTD:BOOL=OFF
)

if (WIN32)
  #set(_curl_platform_flags  ${_curl_platform_flags} -DCMAKE_USE_SCHANNEL=ON)
  set(_curl_platform_flags  ${_curl_platform_flags} -DCURL_USE_OPENSSL:BOOL=ON -DCURL_CA_PATH:STRING=none)
elseif (APPLE)
  set(_curl_platform_flags 
    
    ${_curl_platform_flags}

    #-DCMAKE_USE_SECTRANSP:BOOL=ON 
    -DCURL_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_curl_platform_flags 

    ${_curl_platform_flags}

    -DCURL_USE_OPENSSL:BOOL=ON

    -DCURL_CA_PATH:STRING=none
    -DCURL_CA_BUNDLE:STRING=none
    -DCURL_CA_FALLBACK:BOOL=ON
  )
endif ()

if (BUILD_SHARED_LIBS)
  set(_curl_static OFF)
else()
  set(_curl_static ON)
endif()

bambustudio_add_cmake_project(CURL
  # GIT_REPOSITORY      https://github.com/curl/curl.git
  # GIT_TAG             curl-8_23_0
  URL                 https://curl.se/download/curl-8.23.0.tar.gz
  URL_HASH            SHA256=e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
  DEPENDS             ${ZLIB_PKG}
  # PATCH_COMMAND       ${GIT_EXECUTABLE} checkout -f -- . && git clean -df && 
  #                     ${GIT_EXECUTABLE} apply --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/curl-mods.patch
  CMAKE_ARGS
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DBUILD_TESTING:BOOL=OFF
    -DBUILD_CURL_EXE:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_STATIC_LIBS:BOOL=${_curl_static}
    ${_curl_platform_flags}
)

if (DEP_BUILD_OPENSSL)
  add_dependencies(dep_CURL ${OPENSSL_PKG})
endif ()

if (MSVC)
    add_debug_dep(dep_CURL)
endif ()
