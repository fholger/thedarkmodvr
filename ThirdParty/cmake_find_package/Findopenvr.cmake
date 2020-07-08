include(${CMAKE_CURRENT_LIST_DIR}/tdm_find_package.cmake)

set(openvr_FOUND 1)
set(openvr_INCLUDE_DIRS "${ARTEFACTS_DIR}/openvr/include")
set(openvr_LIBRARY_DIR "${ARTEFACTS_DIR}/openvr/lib/${PACKAGE_PLATFORM}")
if(MSVC)
	set(openvr_LIBRARIES "${openvr_LIBRARY_DIR}/openvr_api.lib")
	set(openvr_BINARY "${ARTEFACTS_DIR}/openvr/bin/${PACKAGE_PLATFORM}/openvr_api.dll")
else()
	set(openvr_LIBRARIES "${openvr_LIBRARY_DIR}/libopenvr_api.so")
    set(openvr_BINARY "${openvr_LIBRARIES}")
endif()
