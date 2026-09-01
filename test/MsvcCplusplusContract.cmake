if(NOT DEFINED ROOT_CMAKE)
  message(FATAL_ERROR "ROOT_CMAKE is required")
endif()

file(READ "${ROOT_CMAKE}" root_cmake_contents)

if(NOT root_cmake_contents MATCHES
   "if\\(MSVC\\)[ \t\r\n]+add_compile_options\\(/Zc:__cplusplus\\)[ \t\r\n]+endif\\(\\)")
  message(FATAL_ERROR
          "MSVC builds must guard and enable /Zc:__cplusplus for C++20 dependencies")
endif()
