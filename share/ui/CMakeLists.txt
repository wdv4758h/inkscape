file(GLOB _FILES "*.xml" "*.rc" "*.css" "*.ui" "*.glade")
install(FILES ${_FILES} DESTINATION ${INKSCAPE_SHARE_INSTALL}/ui)

