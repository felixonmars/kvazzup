find_package( Threads REQUIRED )

# Git
find_package(Git)

# GTest / GMock
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.13.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON  CACHE BOOL "" FORCE)
set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_library(GTest::GMock     ALIAS gmock)
add_library(GTest::GMockMain ALIAS gmock_main)
add_library(GTest::GTest     ALIAS gtest)
add_library(GTest::GTestMain ALIAS gtest_main)
