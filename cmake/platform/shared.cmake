if(UNIX AND NOT APPLE)
	set(LINUX TRUE)
endif()

if(WIN32)
	if(NOT MSVC)
		message(FATAL_ERROR "MSVC restricted")
	endif()

	set(WINDOWS TRUE)
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING
	"Only do Release and Debug"
	FORCE
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
