set(ge_gtest_CXXFLAGS "-D_FORTIFY_SOURCE=2 -O2 -fstack-protector-all -Wl,-z,relro,-z,now,-z,noexecstack")
set(ge_gtest_CFLAGS "-D_FORTIFY_SOURCE=2 -O2 -fstack-protector-all -Wl,-z,relro,-z,now,-z,noexecstack")

if (ENABLE_GITEE)
    set(REQ_URL "https://gitee.com/mirrors/googletest/repository/archive/release-1.8.0.tar.gz")
    set(MD5 "89e13ca1aa48d370719d58010b83f62c")
else()
    set(REQ_URL "https://github.com/google/googletest/archive/release-1.8.0.tar.gz")
    set(MD5 "16877098823401d1bf2ed7891d7dce36")
endif ()

graphengine_add_pkg(ge_gtest
        VER 1.8.0
        LIBS gtest gtest_main
        URL ${REQ_URL}
        MD5 ${MD5}
        CMAKE_OPTION -DBUILD_TESTING=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=ON
        -DCMAKE_MACOSX_RPATH=TRUE -Dgtest_disable_pthreads=ON)

add_library(graphengine::gtest ALIAS ge_gtest::gtest)
add_library(graphengine::gtest_main ALIAS ge_gtest::gtest_main)
include_directories(${ge_gtest_INC})
file(COPY ${ge_gtest_INC}/../lib/libgtest.so DESTINATION ${CMAKE_SOURCE_DIR}/build/graphengine)
file(COPY ${ge_gtest_INC}/../lib/libgtest_main.so DESTINATION ${CMAKE_SOURCE_DIR}/build/graphengine)
