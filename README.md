# LibIME2

A C++ library used to develop Windows TSF-based input methods.
LibIME2 adds a thin layer around Windows TSF to make it more user-friendly.

It's largely based on the old libIME project used by PIME.

The aim of libIME2 is to improve the API design and quality of the original
libIME by introducing more modern C++ syntax and testing.

# Development

## Tool Requirements
*   [CMake](http://www.cmake.org/) >= 3.0
*   [Visual Studio 2019](https://visualstudio.microsoft.com/vs)
*   [git](http://windows.github.com/)

## How to Build
*   Get source from github.

        git clone https://github.com/EasyIME/libIME2.git

*   Use one of the following CMake commands to generate a Visual Studio project.

        cmake -G "Visual Studio 16 2019" <path to source folder>
        cmake -G "Visual Studio 16 2019 Win64" <path to source folder>

*   Open generated project with Visual Studio and build it.
