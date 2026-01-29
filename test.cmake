file(STRINGS "/usr/include/openssl/opensslv.h" OPENSSL_VERSION_STR
    REGEX "^#[\t ]*define[\t ]+OPENSSL_VERSION_STR[\t ]+\"([0-9])+\\.([0-9])+\\.([0-9])+\".*")
message(STATUS "Found OpenSSL version: ${OPENSSL_VERSION_STR}")
string(REGEX REPLACE "^.*OPENSSL_VERSION_STR[\t ]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*$"
       "\\1" OPENSSL_VERSION_STR "${OPENSSL_VERSION_STR}")
message(STATUS "Parsed OpenSSL version: ${OPENSSL_VERSION_STR}")
