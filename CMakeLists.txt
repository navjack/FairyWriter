# SPDX-FileCopyrightText: 2021 Graeme Gott <graeme@gottcode.org>
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

# Configure project
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" fairywriter_version)
string(STRIP "${fairywriter_version}" fairywriter_version)
if(NOT fairywriter_version MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
	message(FATAL_ERROR "VERSION must contain one MAJOR.MINOR.PATCH number")
endif()
project(fairywriter VERSION "${fairywriter_version}" LANGUAGES CXX)
enable_language(C)

set(project_copyright "2008-2026 Graeme Gott and FairyWriter contributors")
configure_file(
	resources/unix/fairywriter.1.in
	${CMAKE_CURRENT_BINARY_DIR}/fairywriter.1
	@ONLY
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep the selected Qt headers ahead of stale headers in compiler-default
# locations such as /usr/local/include. This is required when replacing an old
# Intel Homebrew Qt with the native Apple Silicon framework installation.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Production depends only on qtbase. Keep the retired desktop UI's wider module
# set behind its own option so tester builds on all platforms consume the same
# small Qt surface.
option(FAIRYWRITER_DEVELOPMENT_UI "Build the transitional Qt editor instead of the SNES product" OFF)
option(FAIRYWRITER_DEVELOPER_TOOLS "Build standalone cartridge developer hosts" OFF)
set(fairywriter_qt_components Core Gui OpenGLWidgets Widgets)
if(FAIRYWRITER_DEVELOPMENT_UI)
	list(APPEND fairywriter_qt_components
		Concurrent LinguistTools Multimedia Network PrintSupport)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS ${fairywriter_qt_components})
find_package(ZLIB REQUIRED)
include(GNUInstallDirs)

# Pinned, repository-owned GFM parser. Only the two static libraries are built;
# upstream programs, manuals and tests stay out of the FairyWriter product gate.
set(CMARK_TESTS OFF CACHE BOOL "" FORCE)
set(CMARK_STATIC ON CACHE BOOL "" FORCE)
set(CMARK_SHARED OFF CACHE BOOL "" FORCE)
set(CMARK_LIB_FUZZER OFF CACHE BOOL "" FORCE)
set(CMARK_FUZZ_QUADRATIC OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(third_party/cmark-gfm EXCLUDE_FROM_ALL)
add_library(fairywriter_markdown INTERFACE)
target_compile_definitions(fairywriter_markdown INTERFACE
	CMARK_GFM_STATIC_DEFINE
	CMARK_GFM_EXTENSIONS_STATIC_DEFINE
)
target_include_directories(fairywriter_markdown INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/src
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/extensions
	${CMAKE_CURRENT_BINARY_DIR}/third_party/cmark-gfm/src
	${CMAKE_CURRENT_BINARY_DIR}/third_party/cmark-gfm/extensions
)
target_link_libraries(fairywriter_markdown INTERFACE
	libcmark-gfm-extensions_static
	libcmark-gfm_static
)

# The visible FairyWriter product is a source-owned SNES cartridge. Keep its
# generation explicit and ROM-free; the desktop backend will consume this
# artifact when the SNES runtime replaces the transitional Qt presentation.
find_program(FAIRYWRITER_GO_EXECUTABLE go)
if(NOT FAIRYWRITER_GO_EXECUTABLE)
	message(FATAL_ERROR "FairyWriter requires Go to generate its source-owned cartridge")
endif()
if(FAIRYWRITER_GO_EXECUTABLE)
	set(fairywriter_cartridge "${CMAKE_CURRENT_BINARY_DIR}/fairywriter.sfc")
	add_custom_command(
		OUTPUT "${fairywriter_cartridge}"
		COMMAND "${FAIRYWRITER_GO_EXECUTABLE}" run "${CMAKE_CURRENT_SOURCE_DIR}/tools/fairywriter-rom/main.go" "${fairywriter_cartridge}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/fairywriter-rom/main.go"
		COMMENT "Building the ROM-free FairyWriter SNES cartridge"
		VERBATIM
	)
	add_custom_target(fairywriter_cartridge DEPENDS "${fairywriter_cartridge}")
	set(fairywriter_cartridge_source "${CMAKE_CURRENT_BINARY_DIR}/fairywriter_cartridge.cpp")
	add_custom_command(
		OUTPUT "${fairywriter_cartridge_source}"
		COMMAND "${FAIRYWRITER_GO_EXECUTABLE}" run "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-rom/main.go"
			"${fairywriter_cartridge}" "${fairywriter_cartridge_source}"
		DEPENDS "${fairywriter_cartridge}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-rom/main.go"
		COMMENT "Embedding the FairyWriter cartridge in the production executable"
		VERBATIM
	)
endif()
add_library(fairywriter_cartridge_image STATIC "${fairywriter_cartridge_source}")
target_include_directories(fairywriter_cartridge_image PUBLIC src)

# Apple Silicon builds must not silently run an x86 CMake or bind to the old
# /usr/local x86 Qt. hw.optional.arm64 identifies the hardware even when the
# current process is translated by Rosetta.
if(APPLE)
	execute_process(
		COMMAND /usr/sbin/sysctl -n hw.optional.arm64
		OUTPUT_VARIABLE apple_silicon
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(apple_silicon STREQUAL "1")
		if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
			message(FATAL_ERROR
				"FairyWriter requires a native arm64 CMake process on Apple Silicon. "
				"Use /opt/homebrew/bin/cmake instead of an x86 toolchain.")
		endif()

		get_target_property(qt_core_configurations Qt6::Core IMPORTED_CONFIGURATIONS)
		foreach(qt_core_configuration IN LISTS qt_core_configurations)
			get_target_property(qt_core_location Qt6::Core "IMPORTED_LOCATION_${qt_core_configuration}")
			if(qt_core_location)
				break()
			endif()
		endforeach()
		if(NOT qt_core_location)
			get_target_property(qt_core_location Qt6::Core IMPORTED_LOCATION)
		endif()
		if(NOT qt_core_location)
			message(FATAL_ERROR "Unable to determine the Qt Core binary architecture")
		endif()

		execute_process(
			COMMAND /usr/bin/lipo -archs "${qt_core_location}"
			OUTPUT_VARIABLE qt_core_architectures
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET
		)
		if(NOT qt_core_architectures MATCHES "(^| )arm64($| )")
			message(FATAL_ERROR
				"FairyWriter requires native arm64 Qt on Apple Silicon; found '${qt_core_architectures}' at ${qt_core_location}. "
				"Configure with /opt/homebrew/opt/qt instead of /usr/local Qt.")
		endif()
	endif()
endif()

add_compile_definitions(
	QT_NO_KEYWORDS
	$<$<CONFIG:DEBUG>:QT_STRICT_ITERATORS>
	$<$<CONFIG:DEBUG>:QT_NO_NARROWING_CONVERSIONS_IN_CONNECT>
	$<$<CONFIG:DEBUG>:QT_DISABLE_DEPRECATED_BEFORE=0x061000>
)
add_compile_options(
	$<$<CONFIG:DEBUG>:-Wshadow>
)

# Version number
include(cmake/AddVersionCompileDefinition.cmake)

# Create program
if(FAIRYWRITER_DEVELOPMENT_UI)
	set(fairywriter_entry_source src/main.cpp)
else()
	set(fairywriter_entry_source src/fairywriter_production_main.cpp)
	set(fairywriter_runtime_source src/snesrecomp_player_main.cpp)
endif()
add_version_compile_definition(${fairywriter_entry_source} VERSIONSTR)
# The player reports the same version through --version and sets it as the
# application version, so it needs the definition too. Set unconditionally on
# the path rather than on ${fairywriter_runtime_source}, which is empty in the
# development-UI configuration where fairywriter_xband_player still compiles it.
add_version_compile_definition(src/snesrecomp_player_main.cpp VERSIONSTR)
if(FAIRYWRITER_DEVELOPMENT_UI)
qt_add_executable(fairywriter
	# Headers
	src/action_manager.h
	src/alert.h
	src/alert_layer.h
	src/block_stats.h
	src/color_button.h
	src/deltas.h
	src/daily_progress.h
	src/daily_progress_dialog.h
	src/daily_progress_label.h
	src/document.h
	src/cartridge_image.h
	src/document_engine.h
	src/document_persistence.h
	src/markdown_codec.h
	src/document_bridge.h
	src/document_cache.h
	src/document_watcher.h
	src/document_writer.h
	src/find_dialog.h
	src/focal_point_control.h
	src/file_catalog.h
	src/focus_presentation.h
	src/gzip.h
	src/image_button.h
	src/load_screen.h
	src/locale_dialog.h
	src/mailbox.h
	src/paths.h
	src/preferences.h
	src/preferences_dialog.h
	src/presentation_geometry.h
	src/presentation_surface.h
	src/scene_list.h
	src/scene_model.h
	src/session.h
	src/session_manager.h
	src/shortcut_edit.h
	src/smart_quotes.h
	src/snes_color.h
	src/snes_side_panel.h
	src/snes_style.h
	src/sound.h
	src/stack.h
	src/stats.h
	src/symbols_dialog.h
	src/symbols_model.h
	src/text_codec.h
	src/theme.h
	src/theme_palette.h
	src/theme_preview_document.h
	src/theme_dialog.h
	src/theme_manager.h
	src/theme_renderer.h
	src/session_store.h
	src/theme_tone_mapper.h
	src/timer.h
	src/timer_display.h
	src/timer_manager.h
	src/ui_layout_audit.h
	src/utils.h
	src/window.h
	src/fileformats/docx_reader.h
	src/fileformats/docx_writer.h
	src/fileformats/format_manager.h
	src/fileformats/html_writer.h
	src/fileformats/odt_reader.h
	src/fileformats/odt_writer.h
	src/fileformats/rtf_reader.h
	src/fileformats/rtf_tokenizer.h
	src/fileformats/rtf_writer.h
	src/fileformats/txt_reader.h
	src/spelling/abstract_dictionary.h
	src/spelling/abstract_dictionary_provider.h
	src/spelling/dictionary_dialog.h
	src/spelling/dictionary_manager.h
	src/spelling/dictionary_ref.h
	src/spelling/highlighter.h
	src/spelling/spell_checker.h
	src/3rdparty/qtzip/qtzipreader.h
	src/3rdparty/qtzip/qtzipwriter.h
	# Sources
	src/action_manager.cpp
	src/alert.cpp
	src/alert_layer.cpp
	src/block_stats.cpp
	src/color_button.cpp
	src/deltas.cpp
	src/daily_progress.cpp
	src/daily_progress_dialog.cpp
	src/daily_progress_label.cpp
	src/document.cpp
	src/document_engine.cpp
	src/document_persistence.cpp
	src/markdown_codec.cpp
	src/document_bridge.cpp
	src/document_cache.cpp
	src/document_watcher.cpp
	src/document_writer.cpp
	src/find_dialog.cpp
	src/focal_point_control.cpp
	src/file_catalog.cpp
	src/focus_presentation.cpp
	src/gzip.cpp
	src/image_button.cpp
	src/load_screen.cpp
	src/locale_dialog.cpp
	src/mailbox.cpp
	${fairywriter_entry_source}
	${fairywriter_runtime_source}
	src/paths.cpp
	src/preferences.cpp
	src/preferences_dialog.cpp
	src/presentation_geometry.cpp
	src/presentation_surface.cpp
	src/scene_list.cpp
	src/scene_model.cpp
	src/session.cpp
	src/session_manager.cpp
	src/shortcut_edit.cpp
	src/smart_quotes.cpp
	src/snes_style.cpp
	src/snes_side_panel.cpp
	src/sound.cpp
	src/stack.cpp
	src/stats.cpp
	src/symbols_dialog.cpp
	src/symbols_model.cpp
	src/text_codec.cpp
	src/theme.cpp
	src/theme_dialog.cpp
	src/theme_manager.cpp
	src/theme_preview_document.cpp
	src/theme_renderer.cpp
	src/session_store.cpp
	src/theme_tone_mapper.cpp
	src/timer.cpp
	src/timer_display.cpp
	src/timer_manager.cpp
	src/ui_layout_audit.cpp
	src/utils.cpp
	src/wallpaper_motion_engine.cpp
	src/wallpaper_motion_layer.cpp
	src/window.cpp
	src/fileformats/docx_reader.cpp
	src/fileformats/docx_writer.cpp
	src/fileformats/format_manager.cpp
	src/fileformats/html_writer.cpp
	src/fileformats/odt_reader.cpp
	src/fileformats/odt_writer.cpp
	src/fileformats/rtf_reader.cpp
	src/fileformats/rtf_tokenizer.cpp
	src/fileformats/rtf_writer.cpp
	src/fileformats/txt_reader.cpp
	src/spelling/dictionary_dialog.cpp
	src/spelling/dictionary_manager.cpp
	src/spelling/highlighter.cpp
	src/spelling/spell_checker.cpp
	src/3rdparty/qtzip/qtzip.cpp
	# Resources
	resources/images/images.qrc
	${translations_QM}
)
else()
	qt_add_executable(fairywriter
		src/fairywriter_production_main.cpp
		src/snesrecomp_player_main.cpp
		src/cartridge_image.h
		src/document_engine.h
		src/document_engine.cpp
		src/document_persistence.h
		src/document_persistence.cpp
		src/markdown_codec.h
		src/markdown_codec.cpp
		src/document_bridge.h
		src/document_bridge.cpp
		src/document_writer.h
		src/document_writer.cpp
		src/file_catalog.h
		src/file_catalog.cpp
		src/mailbox.h
		src/mailbox.cpp
		src/session_store.h
		src/session_store.cpp
		src/text_codec.h
		src/text_codec.cpp
		src/fileformats/docx_reader.h
		src/fileformats/docx_reader.cpp
		src/fileformats/docx_writer.h
		src/fileformats/docx_writer.cpp
		src/fileformats/format_manager.h
		src/fileformats/format_manager.cpp
		src/fileformats/odt_reader.h
		src/fileformats/odt_reader.cpp
		src/fileformats/odt_writer.h
		src/fileformats/odt_writer.cpp
		src/fileformats/rtf_reader.h
		src/fileformats/rtf_reader.cpp
		src/fileformats/rtf_tokenizer.h
		src/fileformats/rtf_tokenizer.cpp
		src/fileformats/rtf_writer.h
		src/fileformats/rtf_writer.cpp
		src/fileformats/txt_reader.h
		src/fileformats/txt_reader.cpp
		src/3rdparty/qtzip/qtzipreader.h
		src/3rdparty/qtzip/qtzipwriter.h
		src/3rdparty/qtzip/qtzip.cpp
	)
endif()

if(FAIRYWRITER_DEVELOPER_TOOLS)
	# Standalone cartridge host. This deliberately bypasses RetroArch's window
	# and event layer for low-level cartridge bring-up; it is not a tester build.
	qt_add_executable(fairywriter_snes_player
		src/snes_player_main.cpp
	)
	target_include_directories(fairywriter_snes_player PRIVATE src)
	target_link_libraries(fairywriter_snes_player PRIVATE
		fairywriter_cartridge_image Qt6::Core Qt6::Gui Qt6::Widgets)
	set_target_properties(fairywriter_snes_player PROPERTIES
		OUTPUT_NAME "FairyWriter-SNES"
	)
endif()

# Native XBAND oracle: the same ROM running on snesrecomp's low-level SNES
# devices and 65816 interpreter. Unlike the generic bsnes libretro boundary,
# this host exposes the rare controller-port-2 keyboard protocol to the ROM.
set(FAIRYWRITER_SNESRECOMP_ROOT
	"${CMAKE_SOURCE_DIR}/third_party/snesrecomp" CACHE PATH "Path to the patched snesrecomp checkout")
if(NOT EXISTS "${FAIRYWRITER_SNESRECOMP_ROOT}/runner/src/snes/interp816.c")
	message(FATAL_ERROR
		"FairyWriter requires the patched snesrecomp checkout at ${FAIRYWRITER_SNESRECOMP_ROOT}. "
		"Run scripts/bootstrap-snesrecomp.sh first.")
endif()
if(EXISTS "${FAIRYWRITER_SNESRECOMP_ROOT}/runner/src/snes/interp816.c")
	enable_language(C)
	set(fairywriter_snes_device_root "${FAIRYWRITER_SNESRECOMP_ROOT}/runner/src")
	add_library(fairywriter_snes_machine STATIC
		src/snes_machine.c
		${fairywriter_snes_device_root}/audio_trace.c
		${fairywriter_snes_device_root}/snes/apu.c
		${fairywriter_snes_device_root}/snes/cart.c
		${fairywriter_snes_device_root}/snes/cpu.c
		${fairywriter_snes_device_root}/snes/dma.c
		${fairywriter_snes_device_root}/snes/dsp.c
		${fairywriter_snes_device_root}/snes/audio_shadow.c
		${fairywriter_snes_device_root}/snes/dsp_shadow.c
		${fairywriter_snes_device_root}/snes/interp816.c
		${fairywriter_snes_device_root}/snes/ppu.c
		${fairywriter_snes_device_root}/snes/ppu_old.c
		${fairywriter_snes_device_root}/snes/ws_shadow.c
		${fairywriter_snes_device_root}/snes/snes.c
		${fairywriter_snes_device_root}/snes/snes_other.c
		${fairywriter_snes_device_root}/snes/spc.c
		${fairywriter_snes_device_root}/snes/superfx.c
		${fairywriter_snes_device_root}/snes/xband_keyboard.c
	)
	target_include_directories(fairywriter_snes_machine PRIVATE
		src/snesrecomp_support
		${fairywriter_snes_device_root}
		${fairywriter_snes_device_root}/snes
	)
	target_include_directories(fairywriter_snes_machine PUBLIC src)
	target_compile_definitions(fairywriter_snes_machine PRIVATE
		SNESRECOMP_TRACE=0
		SNESRECOMP_REVERSE_DEBUG=0
	)
	if(FAIRYWRITER_DEVELOPER_TOOLS)
		qt_add_executable(fairywriter_xband_player MACOSX_BUNDLE
			src/snesrecomp_player_main.cpp
			src/xband_player_main.cpp
			src/document_bridge.cpp
			src/session_store.cpp
			src/document_engine.cpp
			src/document_persistence.cpp
			src/markdown_codec.cpp
			src/document_writer.cpp
			src/file_catalog.cpp
			src/mailbox.cpp
			src/fileformats/docx_reader.cpp
			src/fileformats/docx_writer.cpp
			src/fileformats/format_manager.cpp
			src/fileformats/odt_reader.cpp
			src/fileformats/odt_writer.cpp
			src/fileformats/rtf_reader.cpp
			src/fileformats/rtf_writer.cpp
			src/fileformats/rtf_tokenizer.cpp
			src/fileformats/txt_reader.cpp
			src/text_codec.cpp
			src/3rdparty/qtzip/qtzip.cpp)
		target_include_directories(fairywriter_xband_player PRIVATE src src/fileformats src/3rdparty/qtzip)
		target_link_libraries(fairywriter_xband_player PRIVATE
			fairywriter_cartridge_image fairywriter_snes_machine
			fairywriter_markdown
			Qt6::Core Qt6::Gui Qt6::OpenGLWidgets Qt6::Widgets ZLIB::ZLIB)
		set_target_properties(fairywriter_xband_player PROPERTIES
			OUTPUT_NAME "FairyWriter-XBAND"
			MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.navjack.FairyWriter.XBAND"
			MACOSX_BUNDLE_BUNDLE_NAME "FairyWriter XBAND"
			MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
			MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
		)
	endif()
	add_executable(fairywriter_snes_machine_tests
		tests/snes_machine_tests.cpp
	)
	target_link_libraries(fairywriter_snes_machine_tests PRIVATE
		fairywriter_cartridge_image fairywriter_snes_machine)
endif()

target_include_directories(fairywriter PRIVATE
	src
	src/fileformats
	src/spelling
	src/3rdparty/qtzip
)

# The window/taskbar icon. Compiled in rather than installed alongside, so it is
# present identically on all three platforms without any datadir lookup.
set(fairywriter_app_icon ${CMAKE_SOURCE_DIR}/resources/images/icons/hicolor/256x256/apps/fairywriter.png)
set_source_files_properties(${fairywriter_app_icon} PROPERTIES QT_RESOURCE_ALIAS fairywriter.png)
qt_add_resources(fairywriter "fairywriter_appicon"
	PREFIX "/icons"
	FILES ${fairywriter_app_icon}
)
if(WIN32)
	# Embed the native executable/taskbar icon as a Win32 resource. The same
	# artwork is also in the Qt resource above for QWindow::setWindowIcon().
	configure_file(
		resources/windows/fairywriter-version.rc.in
		${CMAKE_CURRENT_BINARY_DIR}/fairywriter-version.rc
		@ONLY
	)
	target_sources(fairywriter PRIVATE
		resources/windows/fairywriter.rc
		resources/windows/fairywriter.ico
		${CMAKE_CURRENT_BINARY_DIR}/fairywriter-version.rc
	)
	target_include_directories(fairywriter PRIVATE resources/windows)
	set_source_files_properties(resources/windows/fairywriter.ico PROPERTIES HEADER_FILE_ONLY TRUE)
	set_target_properties(fairywriter PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(FAIRYWRITER_DEVELOPMENT_UI)
	target_link_libraries(fairywriter PRIVATE
		fairywriter_cartridge_image
		fairywriter_markdown
		Qt6::Concurrent
		Qt6::Core
		Qt6::Gui
		Qt6::Multimedia
		Qt6::Network
		Qt6::PrintSupport
		Qt6::Widgets
		ZLIB::ZLIB
	)
else()
	target_link_libraries(fairywriter PRIVATE
		fairywriter_cartridge_image
		fairywriter_snes_machine
		fairywriter_markdown
		Qt6::Core
		Qt6::Gui
		Qt6::OpenGLWidgets
		Qt6::Widgets
		ZLIB::ZLIB
	)
endif()

if(FAIRYWRITER_DEVELOPMENT_UI)
# Blasphemous is redistributable under CC BY 3.0 and is always available as a
# bundled pixel-font fallback.
set(blasphemous_font ${CMAKE_SOURCE_DIR}/fonts/blasphemous_otf/blasphemous.otf)
set_source_files_properties(${blasphemous_font} PROPERTIES QT_RESOURCE_ALIAS Blasphemous.otf)
qt_add_resources(fairywriter "fairywriter_fonts"
	PREFIX "/fonts"
	FILES ${blasphemous_font}
)


# Create translations
file(GLOB translations_SRCS translations/*.ts)
qt_add_translations(fairywriter
	TS_FILES ${translations_SRCS}
	QM_FILES_OUTPUT_VARIABLE translations_QM
	LUPDATE_OPTIONS -no-obsolete -locations none
)

# Add spellchecking support
if(APPLE)
	target_sources(fairywriter PRIVATE
		src/spelling/dictionary_provider_nsspellchecker.h
		src/spelling/dictionary_provider_nsspellchecker.mm
	)

	find_library(APPKIT AppKit)
	target_link_libraries(fairywriter PRIVATE ${APPKIT})
elseif(NOT WIN32)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(Hunspell REQUIRED IMPORTED_TARGET hunspell)

	target_sources(fairywriter PRIVATE
		src/spelling/dictionary_provider_hunspell.h
		src/spelling/dictionary_provider_hunspell.cpp
		src/spelling/dictionary_provider_voikko.h
		src/spelling/dictionary_provider_voikko.cpp
	)

	target_link_libraries(fairywriter PRIVATE PkgConfig::Hunspell)
endif()

# Make single instance
find_package(KDSingleApplication-qt6)
if(TARGET KDAB::kdsingleapplication)
	target_link_libraries(fairywriter PRIVATE KDAB::kdsingleapplication)
else()
	message(STATUS "Using 3rdparty KDSingleApplication")

	target_compile_definitions(fairywriter PRIVATE KDSINGLEAPPLICATION_STATIC_BUILD)

	target_include_directories(fairywriter PRIVATE src/3rdparty/kdsingleapplication)

	target_sources(fairywriter PRIVATE
		src/3rdparty/kdsingleapplication/kdsingleapplication.cpp
		src/3rdparty/kdsingleapplication/kdsingleapplication_localsocket.cpp
	)
endif()
endif()

# Optimize build
option(ENABLE_LINK_TIME_OPTIMIZATION "Enable link time optimization" OFF)
if(ENABLE_LINK_TIME_OPTIMIZATION)
	include(CheckIPOSupported)
	check_ipo_supported(RESULT result)
	if(result)
		set_target_properties(fairywriter PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
	endif()
endif()

option(ENABLE_STRIP "Enable automatic stripping of builds" OFF)
if(ENABLE_STRIP)
	add_custom_command(TARGET fairywriter
		POST_BUILD
		COMMAND ${CMAKE_STRIP} $<TARGET_FILE:fairywriter>
	)
endif()

# Create symbols only for the disposable desktop development UI. The cartridge
# production target has one fixed visual identity and no host theme/symbol UI.
if(FAIRYWRITER_DEVELOPMENT_UI)
	add_subdirectory(resources/symbols)
endif()

# Install
if(APPLE)
	set(datadir "../Resources")

	set_target_properties(fairywriter PROPERTIES
		OUTPUT_NAME FairyWriter
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/mac/Info.plist.in
	)

	include(cmake/BundleResources.cmake)
	bundle_data(fairywriter ${CMAKE_SOURCE_DIR}/resources/mac/fairywriter.icns Resources)
	if(FAIRYWRITER_DEVELOPMENT_UI)
		bundle_data(fairywriter ${CMAKE_SOURCE_DIR}/resources/images/icons/oxygen/hicolor Resources/icons/hicolor)
		bundle_data(fairywriter ${CMAKE_SOURCE_DIR}/resources/sounds Resources/sounds)
		bundle_data(fairywriter ${CMAKE_SOURCE_DIR}/resources/symbols/symbols1700.dat Resources)
		bundle_data(fairywriter ${CMAKE_SOURCE_DIR}/resources/themes Resources/themes)
		bundle_translations(fairywriter "${translations_QM}")
	else()
		add_custom_command(TARGET fairywriter POST_BUILD
			COMMAND "${CMAKE_COMMAND}"
				-DFAIRYWRITER_BUNDLE=$<TARGET_BUNDLE_DIR:fairywriter>
				-P "${CMAKE_SOURCE_DIR}/cmake/PruneProductionBundle.cmake"
			COMMAND /usr/bin/codesign
				--force
				--sign -
				$<TARGET_BUNDLE_DIR:fairywriter>
			COMMENT "Pruning and sealing the cartridge product bundle"
			VERBATIM
		)
	endif()
elseif(WIN32)
	# windeployqt consumes this minimal install tree. Tester packaging supplies
	# the Qt runtime, vcpkg application-local dependencies, licenses, and testing
	# guide beside the executable.
	set(datadir ".")
	install(TARGETS fairywriter RUNTIME DESTINATION .)
else()
	file(RELATIVE_PATH datadir ${CMAKE_INSTALL_FULL_BINDIR} ${CMAKE_INSTALL_FULL_DATADIR}/fairywriter)

	install(TARGETS fairywriter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(DIRECTORY resources/images/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
	if(FAIRYWRITER_DEVELOPMENT_UI)
		target_sources(fairywriter PRIVATE resources/images/icons/icons.qrc)
		install(FILES ${translations_QM} DESTINATION ${CMAKE_INSTALL_DATADIR}/fairywriter/translations)
		install(DIRECTORY resources/images/icons/oxygen/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/fairywriter/icons)
		install(DIRECTORY resources/sounds DESTINATION ${CMAKE_INSTALL_DATADIR}/fairywriter)
		install(DIRECTORY resources/themes DESTINATION ${CMAKE_INSTALL_DATADIR}/fairywriter)
		install(FILES resources/symbols/symbols1700.dat DESTINATION ${CMAKE_INSTALL_DATADIR}/fairywriter)
	endif()
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fairywriter.1
		DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT doc)

	include(cmake/TranslateMetainfo.cmake)
	process_and_install_metainfo(PO_DIR ${CMAKE_SOURCE_DIR}/resources/unix/po)
endif()

set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS FAIRYWRITER_DATADIR="${datadir}")

# Keep presentation invariants testable without a window system or Qt event loop.
include(CTest)
if(BUILD_TESTING)
	target_compile_definitions(fairywriter PRIVATE
		FAIRYWRITER_PERSISTENCE_TESTING=1)
	find_package(Qt6 REQUIRED COMPONENTS Test)
	if(FAIRYWRITER_GO_EXECUTABLE)
		add_test(
			NAME fairywriter_cartridge_contract
			COMMAND "${CMAKE_COMMAND}" -E env GO111MODULE=off
				"${FAIRYWRITER_GO_EXECUTABLE}" test .
		)
		set_tests_properties(fairywriter_cartridge_contract PROPERTIES
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tools/fairywriter-rom")
		# The contract test checks what the builder produces; this checks that
		# the produced image is a conformant SNES cartridge. The distinction
		# matters because the vendored emulator is lenient in exactly the places
		# real loaders are not -- it ignores the map-mode byte entirely and
		# accepts any checksum pair that sums to $ffff.
		add_test(
			NAME fairywriter_cartridge_conformance
			COMMAND "${CMAKE_COMMAND}" -E env GO111MODULE=off
				"${FAIRYWRITER_GO_EXECUTABLE}" run
				"${CMAKE_CURRENT_SOURCE_DIR}/tools/fairywriter-romcheck/main.go"
				"${fairywriter_cartridge}"
		)
		set_tests_properties(fairywriter_cartridge_conformance PROPERTIES
			DEPENDS fairywriter_cartridge_contract)
	endif()
	if(TARGET fairywriter_snes_machine_tests AND FAIRYWRITER_GO_EXECUTABLE)
		add_test(NAME fairywriter_xband_end_to_end
			COMMAND fairywriter_snes_machine_tests "${fairywriter_cartridge}")
		set_tests_properties(fairywriter_xband_end_to_end PROPERTIES
			DEPENDS fairywriter_cartridge_contract)
	endif()
	add_executable(fairywriter_core_tests
		tests/core_tests.cpp
		src/presentation_geometry.cpp
	)
	target_include_directories(fairywriter_core_tests PRIVATE src)
	add_test(NAME fairywriter_core COMMAND fairywriter_core_tests)
	add_executable(fairywriter_mailbox_tests
		tests/mailbox_tests.cpp
		src/mailbox.cpp
	)
	target_include_directories(fairywriter_mailbox_tests PRIVATE src)
	add_test(NAME fairywriter_mailbox COMMAND fairywriter_mailbox_tests)
	add_executable(fairywriter_document_engine_tests
		tests/document_engine_tests.cpp
		src/document_bridge.cpp
		src/session_store.cpp
		src/document_engine.cpp
		src/document_persistence.cpp
		src/markdown_codec.cpp
		src/file_catalog.cpp
		src/document_writer.cpp
		src/mailbox.cpp
		src/fileformats/docx_reader.cpp
		src/fileformats/docx_writer.cpp
		src/fileformats/format_manager.cpp
		src/fileformats/odt_reader.cpp
		src/fileformats/odt_writer.cpp
		src/fileformats/rtf_reader.cpp
		src/fileformats/rtf_tokenizer.cpp
		src/fileformats/rtf_writer.cpp
		src/fileformats/txt_reader.cpp
		src/3rdparty/qtzip/qtzip.cpp
		src/text_codec.cpp
	)
	set(fairywriter_public_test_fixture
		"${CMAKE_CURRENT_BINARY_DIR}/FairyWriter Public Regression.odt")
	add_custom_command(
		OUTPUT "${fairywriter_public_test_fixture}"
		COMMAND "${CMAKE_COMMAND}" -E env GO111MODULE=off
			"${FAIRYWRITER_GO_EXECUTABLE}" run .
			"${fairywriter_public_test_fixture}"
		DEPENDS
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/public-test-fixture/main.go"
		WORKING_DIRECTORY
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/public-test-fixture"
		COMMENT "Generating the public ODT regression fixture"
		VERBATIM
	)
	add_custom_target(fairywriter_public_test_fixture
		DEPENDS "${fairywriter_public_test_fixture}")
	add_dependencies(fairywriter_document_engine_tests
		fairywriter_public_test_fixture)
	target_include_directories(fairywriter_document_engine_tests PRIVATE src src/fileformats src/3rdparty/qtzip)
	target_link_libraries(fairywriter_document_engine_tests PRIVATE
		fairywriter_markdown Qt6::Core Qt6::Gui ZLIB::ZLIB)
	add_test(NAME fairywriter_document_engine
		COMMAND fairywriter_document_engine_tests
			"${fairywriter_public_test_fixture}")

	add_executable(fairywriter_persistence_tests
		tests/persistence_tests.cpp
		src/document_engine.cpp
		src/document_persistence.cpp
		src/document_writer.cpp
		src/markdown_codec.cpp
		src/mailbox.cpp
		src/fileformats/docx_reader.cpp
		src/fileformats/docx_writer.cpp
		src/fileformats/format_manager.cpp
		src/fileformats/odt_reader.cpp
		src/fileformats/odt_writer.cpp
		src/fileformats/rtf_reader.cpp
		src/fileformats/rtf_tokenizer.cpp
		src/fileformats/rtf_writer.cpp
		src/fileformats/txt_reader.cpp
		src/3rdparty/qtzip/qtzip.cpp
		src/text_codec.cpp
	)
	target_include_directories(fairywriter_persistence_tests PRIVATE
		src src/fileformats src/3rdparty/qtzip)
	target_link_libraries(fairywriter_persistence_tests PRIVATE
		fairywriter_markdown Qt6::Core Qt6::Gui ZLIB::ZLIB)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	add_dependencies(fairywriter_persistence_tests cmark-gfm)
	add_test(NAME fairywriter_persistence
		COMMAND "${CMAKE_COMMAND}"
			-DFAIRYWRITER_PYTHON=${Python3_EXECUTABLE}
			-DFAIRYWRITER_GFM_RUNNER=${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/test/spec_tests.py
			-DFAIRYWRITER_GFM_SPEC=${CMAKE_CURRENT_SOURCE_DIR}/third_party/cmark-gfm/test/spec.txt
			-DFAIRYWRITER_CMARK=$<TARGET_FILE:cmark-gfm>
			-DFAIRYWRITER_PERSISTENCE_TEST=$<TARGET_FILE:fairywriter_persistence_tests>
			-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPersistenceGate.cmake")

	add_executable(fairywriter_persistence_process_tests
		tests/persistence_process_e2e.cpp)
	target_link_libraries(fairywriter_persistence_process_tests PRIVATE Qt6::Core)
	add_test(NAME fairywriter_persistence_process_e2e
		COMMAND fairywriter_persistence_process_tests $<TARGET_FILE:fairywriter>)
	set_tests_properties(fairywriter_persistence_process_e2e PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 180)

	add_executable(fairywriter_file_catalog_tests tests/file_catalog_tests.cpp src/file_catalog.cpp)
	target_include_directories(fairywriter_file_catalog_tests PRIVATE src)
	target_link_libraries(fairywriter_file_catalog_tests PRIVATE Qt6::Core)
	add_test(NAME fairywriter_file_catalog COMMAND fairywriter_file_catalog_tests)

	# The three suites below cover the retired Qt theming/wallpaper presentation
	# track (theme.cpp, snes_style.cpp, wallpaper_motion_engine.cpp), none of
	# which is in the shipping cartridge product. Keeping them out of the
	# production configuration means all three platforms build and run exactly
	# the same set of tests, instead of Windows needing dead-track sources to
	# compile. Build with -DFAIRYWRITER_DEVELOPMENT_UI=ON to run them.
	if(FAIRYWRITER_DEVELOPMENT_UI)
	add_executable(fairywriter_presentation_tests
		tests/presentation_tests.cpp
		src/presentation_geometry.cpp
		src/presentation_surface.cpp
		src/snes_style.cpp
		src/snes_side_panel.cpp
		src/theme.cpp
		src/theme_tone_mapper.cpp
		src/utils.cpp
	)
	target_include_directories(fairywriter_presentation_tests PRIVATE src)
	target_compile_definitions(fairywriter_presentation_tests PRIVATE
		FAIRYWRITER_TEST_THEME_PATH="${CMAKE_SOURCE_DIR}/resources/themes"
	)
	target_link_libraries(fairywriter_presentation_tests PRIVATE
		Qt6::Concurrent
		Qt6::Core
		Qt6::Gui
		Qt6::Widgets
	)
	add_test(NAME fairywriter_presentation COMMAND fairywriter_presentation_tests -platform offscreen)
	set_tests_properties(fairywriter_presentation PROPERTIES
		ENVIRONMENT "FAIRYWRITER_PRESENTATION_AUDIT_DIR=${CMAKE_CURRENT_BINARY_DIR}/presentation-audit"
	)

	add_executable(fairywriter_focus_presentation_tests
		tests/focus_presentation_tests.cpp
		src/focus_presentation.cpp
	)
	target_include_directories(fairywriter_focus_presentation_tests PRIVATE src)
	target_link_libraries(fairywriter_focus_presentation_tests PRIVATE Qt6::Core Qt6::Test)
	add_test(NAME fairywriter_focus_presentation COMMAND fairywriter_focus_presentation_tests)

	add_executable(fairywriter_wallpaper_motion_tests
		tests/wallpaper_motion_tests.cpp
		src/presentation_geometry.cpp
		src/presentation_surface.cpp
		src/snes_style.cpp
		src/snes_side_panel.cpp
		src/theme.cpp
		src/theme_tone_mapper.cpp
		src/utils.cpp
		src/wallpaper_motion_engine.cpp
	)
	target_include_directories(fairywriter_wallpaper_motion_tests PRIVATE src)
	target_link_libraries(fairywriter_wallpaper_motion_tests PRIVATE
		Qt6::Concurrent
		Qt6::Core
		Qt6::Gui
		Qt6::Test
		Qt6::Widgets
	)
	add_test(NAME fairywriter_wallpaper_motion COMMAND fairywriter_wallpaper_motion_tests -platform offscreen)
	endif()

	add_test(NAME fairywriter_cli_version COMMAND fairywriter --version)
	set_tests_properties(fairywriter_cli_version PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=fairywriter-intentionally-invalid"
		PASS_REGULAR_EXPRESSION "^FairyWriter ${PROJECT_VERSION}"
	)

	if(FAIRYWRITER_DEVELOPMENT_UI)
		add_test(NAME fairywriter_ui_layout COMMAND fairywriter --verify-ui-layout)
		set_tests_properties(fairywriter_ui_layout PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FAIRYWRITER_UI_AUDIT_DIR=${CMAKE_CURRENT_BINARY_DIR}/ui-layout-audit"
			PASS_REGULAR_EXPRESSION "FairyWriter UI layout audit passed"
			TIMEOUT 60
		)
	endif()
endif()
