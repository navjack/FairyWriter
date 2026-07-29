# SPDX-FileCopyrightText: 2022 Graeme Gott <graeme@gottcode.org>
#
# SPDX-License-Identifier: GPL-3.0-or-later

function(add_version_compile_definition versionstr_file versionstr_def)
	# VERSION is the product authority. Repository tags and dirtiness must not
	# make identical source trees report different application versions.
	set_property(
		SOURCE ${versionstr_file}
		APPEND
		PROPERTY COMPILE_DEFINITIONS ${versionstr_def}="${PROJECT_VERSION}"
	)
endfunction()
