cmake_minimum_required(VERSION 3.20)
project(KuroCat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(KuroCat WIN32
    src/main.cpp
    app.manifest
)

target_compile_definitions(KuroCat PRIVATE UNICODE _UNICODE NOMINMAX)
if(MSVC)
    target_compile_options(KuroCat PRIVATE /O2 /utf-8 /EHsc /W3 /MT)
else()
    target_compile_options(KuroCat PRIVATE -O2 -municode)
endif()

target_link_libraries(KuroCat PRIVATE user32 gdi32 gdiplus shell32 advapi32 winhttp wtsapi32 bcrypt ole32 oleaut32 uuid)
