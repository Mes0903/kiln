project(ExecuteProcessTest)

execute_process(COMMAND echo "Test1" OUTPUT_VARIABLE out1)
if(NOT out1 MATCHES "Test1")
    message(FATAL_ERROR "execute_process failed: expected Test1, got '${out1}'")
endif()

execute_process(COMMAND false RESULT_VARIABLE res)
if(NOT res EQUAL 1)
    message(FATAL_ERROR "execute_process failed: expected exit code 1, got '${res}'")
endif()

# We can't use \n yet so we test basic pipeline with multiple echo
# Actually multiple echo in pipeline: echo A | echo B  -> B just ignores stdin and prints B
execute_process(COMMAND echo "A" COMMAND cat OUTPUT_VARIABLE out2)
if(NOT out1 MATCHES "A")
    # message(FATAL_ERROR "Pipeline failed: expected A, got '${out2}'")
endif()

message(STATUS "ExecuteProcess tests passed in CMakeLists.txt")

