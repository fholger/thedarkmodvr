include(${CMAKE_CURRENT_LIST_DIR}/tdm_find_package.cmake)

set(openxr_FOUND 1)
set(openxr_INCLUDE_DIRS "${ARTEFACTS_DIR}/openxr/include")
set(openxr_LIBRARY_DIR "${ARTEFACTS_DIR}/openxr/lib/${PACKAGE_PLATFORM}")
if(MSVC)
	set(openxr_LIBRARIES "${openxr_LIBRARY_DIR}/openxr_loader.lib")
    set(openxr_BINARY "${ARTEFACTS_DIR}/openxr/bin/${PACKAGE_PLATFORM}/openxr_loader.dll")
else()
	set(openxr_LIBRARIES "${openxr_LIBRARY_DIR}/libopenxr_loader.so.1")
    set(openxr_BINARY "${openxr_LIBRARIES}")
endif()
