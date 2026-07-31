# SPDX-FileCopyrightText: 2022 Graeme Gott <graeme@gottcode.org>
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Add files to a macOS bundle.
function(bundle_data target source destination)
	if(IS_DIRECTORY ${source})
		# Recursively find files under source
		file(GLOB_RECURSE files RELATIVE ${source} ${source}/*)
		set(parent ${source})
	else()
		# Handle single file
		get_filename_component(files ${source} NAME)
		get_filename_component(parent ${source} DIRECTORY)
	endif()

	# Set each file to be located under destination
	foreach(resource ${files})
		get_filename_component(path ${resource} DIRECTORY)
		set_property(
			SOURCE ${parent}/${resource}
			PROPERTY
			MACOSX_PACKAGE_LOCATION ${destination}/${path}
		)
	endforeach()

	# Make target depend on resources
	list(TRANSFORM files PREPEND "${parent}/")
	target_sources(${target} PRIVATE ${files})
endfunction()

