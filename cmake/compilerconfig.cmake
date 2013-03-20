##### Verbose make ####
option( VERBOSE_MAKE "Verbose Makefile output" OFF) # defaults to OFF
	set(CMAKE_VERBOSE_MAKEFILE ${VERBOSE_MAKE})

### Additional compiler and linker flags ##
set(CMAKE_C_FLAGS "-g -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "-Wl,-gc-sections")

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "ag++")
set(CMAKE_AGPP_FLAGS "--real-instances" CACHE STRING "Additional ag++ flags, e.g. --real-instances --keep_woven")
  ## Here we add the build dir holding the generated header files (protobuf)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --Xweaver -p ${CMAKE_SOURCE_DIR} ${CMAKE_AGPP_FLAGS} --Xcompiler")

add_definitions(-D_FILE_OFFSET_BITS=64)

message(STATUS "[${PROJECT_NAME}] Compiler: ${CMAKE_C_COMPILER}/${CMAKE_CXX_COMPILER}" )
