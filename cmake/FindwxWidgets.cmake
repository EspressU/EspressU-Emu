# SPDX-FileCopyrightText: 2022 Andrea Pappacoda <andrea@pappacoda.it>
# SPDX-License-Identifier: ISC

include(FindPackageHandleStandardArgs)
find_package(wxWidgets CONFIG COMPONENTS ${wxWidgets_FIND_COMPONENTS})

if (wxWidgets_FOUND)
	find_package_handle_standard_args(wxWidgets CONFIG_MODE)
else()
	set(_tmp_module_path "${CMAKE_MODULE_PATH}")
	set(CMAKE_MODULE_PATH "")

	find_package(wxWidgets MODULE QUIET COMPONENTS ${wxWidgets_FIND_COMPONENTS})

	set(CMAKE_MODULE_PATH "${_tmp_module_path}")
	unset(_tmp_module_path)

	if (wxWidgets_FOUND)
		add_library(wx::base IMPORTED INTERFACE)
		target_include_directories(wx::base INTERFACE ${wxWidgets_INCLUDE_DIRS})
		target_link_libraries(wx::base INTERFACE ${wxWidgets_LIBRARIES})
		target_link_directories(wx::base INTERFACE ${wxWidgets_LIBRARY_DIRS})
		target_compile_definitions(wx::base INTERFACE ${wxWidgets_DEFINITIONS})
		target_compile_options(wx::base INTERFACE ${wxWidgets_CXX_FLAGS})

		foreach(component ${wxWidgets_FIND_COMPONENTS})
			if (NOT component STREQUAL "base")
				add_library(wx::${component} ALIAS wx::base)
			endif()
		endforeach()
	endif()

	find_package_handle_standard_args(wxWidgets
		REQUIRED_VARS
			wxWidgets_LIBRARIES
			wxWidgets_FOUND
		VERSION_VAR
			wxWidgets_VERSION_STRING
	)
endif()
