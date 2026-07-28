if(NOT DEFINED FAIRYWRITER_BUNDLE)
	message(FATAL_ERROR "FAIRYWRITER_BUNDLE is required")
endif()

set(resources "${FAIRYWRITER_BUNDLE}/Contents/Resources")
file(GLOB legacy_translations "${resources}/*.lproj")
file(REMOVE_RECURSE
	${legacy_translations}
	"${resources}/icons"
	"${resources}/sounds"
	"${resources}/themes"
)
file(REMOVE "${resources}/symbols1700.dat")
file(REMOVE_RECURSE "${resources}/font-notices")
