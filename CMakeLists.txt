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

# The product depends only on qtbase, so every platform's tester build consumes
# the same small Qt surface. The retired FocusWriter-derived desktop UI, which
# needed Concurrent/LinguistTools/Multimedia/Network/PrintSupport on top of this,
# was removed along with its resources and spell-checking dependencies.
option(FAIRYWRITER_DEVELOPER_TOOLS "Build standalone cartridge developer hosts" OFF)
set(fairywriter_qt_components Core Gui OpenGLWidgets Widgets)
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

# Audio output. miniaudio is a single vendored header wrapping the three
# platforms' native APIs, chosen over Qt Multimedia because QAudioSink routes
# through the platform media backend plugin -- the ffmpeg one on Linux -- which
# the AppImage gate in packaging/linux/build-linux.sh would then have to deploy
# and resolve. This adds nothing to any packaging step. Only the backends we
# ship on are compiled in; the rest of miniaudio (decoding, encoding, synthesis,
# resource manager, node graph, engine) is disabled in src/audio_output.cpp.
add_library(fairywriter_audio INTERFACE)
target_include_directories(fairywriter_audio INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
)
target_compile_definitions(fairywriter_audio INTERFACE MA_ENABLE_ONLY_SPECIFIC_BACKENDS)
if(APPLE)
	target_compile_definitions(fairywriter_audio INTERFACE MA_ENABLE_COREAUDIO)
	find_library(fairywriter_coreaudio CoreAudio REQUIRED)
	find_library(fairywriter_audiotoolbox AudioToolbox REQUIRED)
	find_library(fairywriter_audiounit AudioUnit REQUIRED)
	find_library(fairywriter_corefoundation CoreFoundation REQUIRED)
	target_link_libraries(fairywriter_audio INTERFACE
		${fairywriter_coreaudio} ${fairywriter_audiotoolbox}
		${fairywriter_audiounit} ${fairywriter_corefoundation})
elseif(WIN32)
	target_compile_definitions(fairywriter_audio INTERFACE MA_ENABLE_WASAPI)
else()
	# Both are loaded with dlopen at runtime, so neither is a link-time or
	# package dependency: the AppImage runs against whichever the host has.
	target_compile_definitions(fairywriter_audio INTERFACE
		MA_ENABLE_PULSEAUDIO MA_ENABLE_ALSA)
	target_link_libraries(fairywriter_audio INTERFACE ${CMAKE_DL_LIBS} pthread m)
endif()

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
	set(fairywriter_entry_source src/fairywriter_production_main.cpp)
	set(fairywriter_runtime_source src/snesrecomp_player_main.cpp)
add_version_compile_definition(${fairywriter_entry_source} VERSIONSTR)
# The player reports the same version through --version and sets it as the
# application version, so it needs the definition too. Set unconditionally on
# the path rather than on ${fairywriter_runtime_source}, which is empty in the
# development-UI configuration where fairywriter_xband_player still compiles it.
add_version_compile_definition(src/snesrecomp_player_main.cpp VERSIONSTR)
	qt_add_executable(fairywriter
		src/fairywriter_production_main.cpp
		src/snesrecomp_player_main.cpp
		src/audio_output.h
		src/audio_output.cpp
		src/canvas_palette.h
		src/snes_color.h
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
			src/audio_output.cpp
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
			fairywriter_audio
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

	target_link_libraries(fairywriter PRIVATE
		fairywriter_audio
		fairywriter_cartridge_image
		fairywriter_snes_machine
		fairywriter_markdown
		Qt6::Core
		Qt6::Gui
		Qt6::OpenGLWidgets
		Qt6::Widgets
		ZLIB::ZLIB
	)


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

# Install
if(APPLE)
	set_target_properties(fairywriter PROPERTIES
		OUTPUT_NAME FairyWriter
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/mac/Info.plist.in
	)

	include(cmake/BundleResources.cmake)
	bundle_data(fairywriter ${CMAKE_SOURCE_DIR}/resources/mac/fairywriter.icns Resources)
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
elseif(WIN32)
	# windeployqt consumes this minimal install tree. Tester packaging supplies
	# the Qt runtime, vcpkg application-local dependencies, licenses, and testing
	# guide beside the executable.
	install(TARGETS fairywriter RUNTIME DESTINATION .)
else()
	install(TARGETS fairywriter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(DIRECTORY resources/images/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/fairywriter.1
		DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 COMPONENT doc)

	include(cmake/TranslateMetainfo.cmake)
	process_and_install_metainfo(PO_DIR ${CMAKE_SOURCE_DIR}/resources/unix/po)
endif()


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
	# The process test launches the real executable. Opening an output device is
	# already best-effort, but a container or CI runner has no sound card and the
	# backend can spend real time discovering that; keep the test deterministic
	# by not asking for one.
	set_tests_properties(fairywriter_persistence_process_e2e PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FAIRYWRITER_NO_AUDIO=1"
		TIMEOUT 180)

	# A function key is only live if the host's Qt->scancode table and the
	# cartridge's scancode->key-code map agree. Nothing else covers that seam:
	# F5 once shipped mapped in the cartridge and missing from the host, so the
	# key did nothing in the real application while every cartridge test passed.
	add_test(NAME fairywriter_function_keys
		COMMAND fairywriter --function-key-e2e-child)
	set_tests_properties(fairywriter_function_keys PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FAIRYWRITER_NO_AUDIO=1"
		TIMEOUT 120)

	add_executable(fairywriter_file_catalog_tests tests/file_catalog_tests.cpp src/file_catalog.cpp)
	target_include_directories(fairywriter_file_catalog_tests PRIVATE src)
	target_link_libraries(fairywriter_file_catalog_tests PRIVATE Qt6::Core)
	add_test(NAME fairywriter_file_catalog COMMAND fairywriter_file_catalog_tests)

	add_test(NAME fairywriter_cli_version COMMAND fairywriter --version)
	set_tests_properties(fairywriter_cli_version PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=fairywriter-intentionally-invalid"
		PASS_REGULAR_EXPRESSION "^FairyWriter ${PROJECT_VERSION}"
	)

endif()
