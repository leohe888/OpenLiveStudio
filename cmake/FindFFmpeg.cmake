# Locate an FFmpeg development installation (headers and link libraries).
# Set FFmpeg_ROOT to the package directory; never bake a local path into source.
set(FFmpeg_ROOT "" CACHE PATH "FFmpeg development package root")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_FFmpeg QUIET libavdevice libavformat libavcodec libavutil libswresample)
endif()

find_path(FFmpeg_INCLUDE_DIR
    NAMES libavdevice/avdevice.h
    HINTS "${FFmpeg_ROOT}/include" ${PC_FFmpeg_INCLUDE_DIRS}
)

set(_FFmpeg_required_vars FFmpeg_INCLUDE_DIR)
foreach(_component avdevice avformat avcodec avutil swresample)
    find_library(FFmpeg_${_component}_LIBRARY
        NAMES ${_component}
        HINTS "${FFmpeg_ROOT}/lib" ${PC_FFmpeg_LIBRARY_DIRS}
    )
    list(APPEND _FFmpeg_required_vars FFmpeg_${_component}_LIBRARY)
endforeach()

# MSVC uses .lib import libraries; MinGW can use .dll.a import libraries.
# The runtime set also contains dependencies such as avfilter and swresample.
if(WIN32)
    set(FFmpeg_RUNTIME_DIR "" CACHE PATH "FFmpeg shared runtime directory")
    if(NOT FFmpeg_RUNTIME_DIR)
        foreach(_bin "${FFmpeg_ROOT}/bin" "${FFmpeg_INCLUDE_DIR}/../bin")
            file(GLOB _avdevice_dlls "${_bin}/avdevice-*.dll")
            if(_avdevice_dlls)
                get_filename_component(_bin "${_bin}" ABSOLUTE)
                set(FFmpeg_RUNTIME_DIR "${_bin}" CACHE PATH
                    "FFmpeg shared runtime directory" FORCE)
                break()
            endif()
        endforeach()
    endif()
    list(APPEND _FFmpeg_required_vars FFmpeg_RUNTIME_DIR)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS ${_FFmpeg_required_vars}
    REASON_FAILURE_MESSAGE
        "Set FFmpeg_ROOT to a shared FFmpeg development package containing include, lib and bin."
)

if(FFmpeg_FOUND)
    foreach(_component avdevice avformat avcodec avutil swresample)
        if(NOT TARGET FFmpeg::${_component})
            add_library(FFmpeg::${_component} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_component} PROPERTIES
                IMPORTED_LOCATION "${FFmpeg_${_component}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
            )
        endif()
    endforeach()
    if(WIN32)
        file(GLOB FFmpeg_RUNTIME_DLLS CONFIGURE_DEPENDS
            "${FFmpeg_RUNTIME_DIR}/*.dll")
    endif()
endif()

mark_as_advanced(FFmpeg_INCLUDE_DIR FFmpeg_RUNTIME_DIR
    FFmpeg_avdevice_LIBRARY FFmpeg_avformat_LIBRARY
    FFmpeg_avcodec_LIBRARY FFmpeg_avutil_LIBRARY FFmpeg_swresample_LIBRARY)
