if(NOT DEFINED FAIRYWRITER_BUNDLE)
	message(FATAL_ERROR "FAIRYWRITER_BUNDLE is required")
endif()

# macdeployqt copies Qt's own localizations into the bundle as *.lproj even with
# -no-plugins, and the cartridge owns every glyph it draws, so none of it is
# reachable. mac_deploy.sh runs this script again after macdeployqt for exactly
# that reason -- the POST_BUILD invocation cannot see what macdeployqt adds later.
#
# The retired desktop UI's icons, sounds, themes, symbol table and font notices
# were pruned here too. They have been removed from the tree along with that UI,
# so there is nothing left of them to strip.
set(resources "${FAIRYWRITER_BUNDLE}/Contents/Resources")
file(GLOB qt_localizations "${resources}/*.lproj")
if(qt_localizations)
	file(REMOVE_RECURSE ${qt_localizations})
endif()
