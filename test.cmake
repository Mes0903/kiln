include(OurTestBigEndian)
test_big_endian(IS_BIG_ENDIAN)
message("IS_BIG_ENDIAN: ${IS_BIG_ENDIAN}")

include(CheckTypeSize)

# Check for size of long.
check_type_size(long SIZEOF_LONG)

message("HAVE_SIZEOF_LONG: ${HAVE_SIZEOF_LONG}")
message("SIZEOF_LONG: ${SIZEOF_LONG}")
message("SIZEOF_LONG_CODE: ${SIZEOF_LONG_CODE}")
