# Module for locating tconv, based on ICU module.
#
# Cutomizable variables:
#   TCONV_ROOT_DIR
#     This variable points to the tconv root directory. On Windows the
#     library location typically will have to be provided explicitly using the
#     -D command-line option. Alternatively, an environment variable can be set.
#
# Read-Only variables:
#   TCONV_FOUND
#     Indicates whether the library has been found.
#
#   TCONV_INCLUDE_DIRS
#     Points to the tconv include directory.
#
#   TCONV_LIBRARIES
#     Points to the tconv libraries that should be passed to
#     target_link_libraries in dynamic mode.
#
#   TCONV_STATIC_LIBRARIES
#     Points to the tconv libraries static that should be passed to
#     target_link_libraries in static mode.
#
#   TCONV_LINK_FLAGS
#     Linker flags when building linking against tconv. Defaults to "libtconv". Take care, this is a list.
#
#   TCONV_STATIC_LINK_FLAGS
#     Linker flags when building linking against static tconv. Defaults to "libtconv_static". Take care, this is a list.
#
#   TCONV_LDFLAGS
#     Stringified version of TCONV_LINK_FLAGS
#
#   TCONV_STATIC_LDFLAGS
#     Stringified version of TCONV_STATIC_LINK_FLAGS
#
INCLUDE (CMakeParseArguments)
INCLUDE (FindPackageHandleStandardArgs)

SET (_PF86 "ProgramFiles(x86)")
SET (_TCONV_POSSIBLE_DIRS
  ${TCONV_ROOT_DIR}
  "$ENV{TCONV_ROOT_DIR}"
  "C:/tconv"
  "$ENV{PROGRAMFILES}/tconv"
  "$ENV{${_PF86}}/tconv")

SET (_TCONV_POSSIBLE_INCLUDE_SUFFIXES include)

IF (CMAKE_SIZEOF_VOID_P EQUAL 8)
  SET (_TCONV_POSSIBLE_LIB_SUFFIXES lib64)
  SET (_TCONV_POSSIBLE_BIN_SUFFIXES bin64)

  IF (NOT WIN32)
    LIST (APPEND _TCONV_POSSIBLE_LIB_SUFFIXES lib)
    LIST (APPEND _TCONV_POSSIBLE_BIN_SUFFIXES bin)
  ENDIF (NOT WIN32)
ELSE (CMAKE_SIZEOF_VOID_P EQUAL 8)
  SET (_TCONV_POSSIBLE_LIB_SUFFIXES lib)
  SET (_TCONV_POSSIBLE_BIN_SUFFIXES bin)
ENDIF (CMAKE_SIZEOF_VOID_P EQUAL 8)

FIND_PATH (TCONV_ROOT_DIR
  NAMES include/tconv.h
  PATHS ${_TCONV_POSSIBLE_DIRS}
  DOC "tconv root directory")

IF (TCONV_ROOT_DIR)
  # Re-use the previous path:
  FIND_PATH (TCONV_INCLUDE_DIR
    NAMES tconv.h
    PATHS ${TCONV_ROOT_DIR}
    PATH_SUFFIXES ${_TCONV_POSSIBLE_INCLUDE_SUFFIXES}
    DOC "tconv include directory"
    # NO_DEFAULT_PATH
    )
ELSE (TCONV_ROOT_DIR)
  # Use default path search
  FIND_PATH (TCONV_INCLUDE_DIR
    NAMES tconv.h
    DOC "tconv include directory"
    )
ENDIF (TCONV_ROOT_DIR)

IF (NOT TCONV_FIND_COMPONENTS)
  SET (TCONV_FIND_COMPONENTS tconv)
ENDIF (NOT TCONV_FIND_COMPONENTS)

IF (TCONV_INCLUDE_DIR)
  # Loop over each components
  FOREACH (_TCONV_COMPONENT ${TCONV_FIND_COMPONENTS})
    SET (_TCONV_COMPONENT_BASE ${_TCONV_COMPONENT})
    SET (_TCONV_COMPONENT_POSSIBLE_DEBUG_NAMES ${_TCONV_COMPONENT_BASE}d)
    SET (_TCONV_COMPONENT_POSSIBLE_RELEASE_NAMES ${_TCONV_COMPONENT_BASE})

    SET (_TCONV_STATIC_COMPONENT_BASE "${_TCONV_COMPONENT}_static")
    SET (_TCONV_STATIC_COMPONENT_POSSIBLE_DEBUG_NAMES ${_TCONV_STATIC_COMPONENT_BASE}d)
    SET (_TCONV_STATIC_COMPONENT_POSSIBLE_RELEASE_NAMES ${_TCONV_STATIC_COMPONENT_BASE})

    STRING (TOUPPER ${_TCONV_COMPONENT} _TCONV_COMPONENT_UPPER)
    SET (_TCONV_LIBRARY_BASE TCONV_${_TCONV_COMPONENT_UPPER}_LIBRARY)
    SET (_TCONV_STATIC_LIBRARY_BASE TCONV_STATIC_${_TCONV_COMPONENT_UPPER}_LIBRARY)

    IF (TCONV_ROOT_DIR)
      FIND_LIBRARY (${_TCONV_LIBRARY_BASE}_DEBUG
	NAMES ${_TCONV_COMPONENT_POSSIBLE_DEBUG_NAMES}
	PATHS ${TCONV_ROOT_DIR} PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	# NO_DEFAULT_PATH
	)
      FIND_LIBRARY (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG
	NAMES ${_TCONV_STATIC_COMPONENT_POSSIBLE_DEBUG_NAMES}
	PATHS ${TCONV_ROOT_DIR} PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	# NO_DEFAULT_PATH
	)
    ELSE (TCONV_ROOT_DIR)
      FIND_LIBRARY (${_TCONV_LIBRARY_BASE}_DEBUG
	NAMES ${_TCONV_COMPONENT_POSSIBLE_DEBUG_NAMES}
	PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	)
      FIND_LIBRARY (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG
	NAMES ${_TCONV_COMPONENT_STATIC_POSSIBLE_DEBUG_NAMES}
	PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	)
    ENDIF (TCONV_ROOT_DIR)

    IF (TCONV_ROOT_DIR)
      FIND_LIBRARY (${_TCONV_LIBRARY_BASE}_RELEASE
	NAMES ${_TCONV_COMPONENT_POSSIBLE_RELEASE_NAMES}
	PATHS ${TCONV_ROOT_DIR} PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	# NO_DEFAULT_PATH
	)
      FIND_LIBRARY (${_TCONV_STATIC_LIBRARY_BASE}_RELEASE
	NAMES ${_TCONV_STATIC_COMPONENT_POSSIBLE_RELEASE_NAMES}
	PATHS ${TCONV_ROOT_DIR} PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	# NO_DEFAULT_PATH
	)
    ELSE (ICU_ROOT_DIR)
      FIND_LIBRARY (${_TCONV_LIBRARY_BASE}_RELEASE
	NAMES ${_TCONV_COMPONENT_POSSIBLE_RELEASE_NAMES}
	PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	)
      FIND_LIBRARY (${_TCONV_STATIC_LIBRARY_BASE}_RELEASE
	NAMES ${_TCONV_STATIC_COMPONENT_POSSIBLE_RELEASE_NAMES}
	PATH_SUFFIXES ${_TCONV_POSSIBLE_LIB_SUFFIXES}
	)
    ENDIF (TCONV_ROOT_DIR)

    SET (TCONV_${_TCONV_COMPONENT_UPPER}_FOUND TRUE)

    # Debug and release
    IF (${_TCONV_LIBRARY_BASE}_DEBUG AND ${_TCONV_LIBRARY_BASE}_RELEASE)
      SET (${_TCONV_LIBRARY_BASE}
        debug ${${_TCONV_LIBRARY_BASE}_DEBUG}
        optimized ${${_TCONV_LIBRARY_BASE}_RELEASE})
      # Debug only
    ELSEIF (${_TCONV_LIBRARY_BASE}_DEBUG)
      SET (${_TCONV_LIBRARY_BASE} ${${_TCONV_LIBRARY_BASE}_DEBUG})
      # Release only
    ELSEIF (${_TCONV_LIBRARY_BASE}_RELEASE)
      SET (${_TCONV_LIBRARY_BASE} ${${_TCONV_LIBRARY_BASE}_RELEASE})
    ELSE (${_TCONV_LIBRARY_BASE}_DEBUG AND ${_TCONV_LIBRARY_BASE}_RELEASE)
      # Component missing: record it for a later report
      LIST (APPEND _TCONV_MISSING_COMPONENTS ${_TCONV_COMPONENT})
      SET (TCONV_${__COMPONENT_UPPER}_FOUND FALSE)
    ENDIF (${_TCONV_LIBRARY_BASE}_DEBUG AND ${_TCONV_LIBRARY_BASE}_RELEASE)

    IF (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG AND ${_TCONV_STATIC_LIBRARY_BASE}_RELEASE)
      SET (${_TCONV_STATIC_LIBRARY_BASE}
        debug ${${_TCONV_STATIC_LIBRARY_BASE}_DEBUG}
        optimized ${${_TCONV_STATIC_LIBRARY_BASE}_RELEASE})
      # Debug only
    ELSEIF (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG)
      SET (${_TCONV_STATIC_LIBRARY_BASE} ${${_TCONV_STATIC_LIBRARY_BASE}_DEBUG})
      # Release only
    ELSEIF (${_TCONV_STATIC_LIBRARY_BASE}_RELEASE)
      SET (${_TCONV_STATIC_LIBRARY_BASE} ${${_TCONV_STATIC_LIBRARY_BASE}_RELEASE})
    ELSE (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG AND ${_TCONV_STATIC_LIBRARY_BASE}_RELEASE)
      # Component missing: record it for a later report
      LIST (APPEND _TCONV_STATIC_MISSING_COMPONENTS ${_TCONV_COMPONENT})
      SET (TCONV_STATIC_${__COMPONENT_UPPER}_FOUND FALSE)
    ENDIF (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG AND ${_TCONV_STATIC_LIBRARY_BASE}_RELEASE)

    MARK_AS_ADVANCED (${_TCONV_LIBRARY_BASE}_DEBUG ${_TCONV_LIBRARY_BASE}_RELEASE)
    MARK_AS_ADVANCED (${_TCONV_STATIC_LIBRARY_BASE}_DEBUG ${_TCONV_STATIC_LIBRARY_BASE}_RELEASE)

    # Make sure only libraries that have been actually found are registered
    IF (${_TCONV_LIBRARY_BASE})
      LIST (APPEND _TCONV_LIBRARIES ${${_TCONV_LIBRARY_BASE}})
    ENDIF (${_TCONV_LIBRARY_BASE})

    IF (${_TCONV_STATIC_LIBRARY_BASE})
      LIST (APPEND _TCONV_STATIC_LIBRARIES ${${_TCONV_STATIC_LIBRARY_BASE}})
    ENDIF (${_TCONV_STATIC_LIBRARY_BASE})

  ENDFOREACH (_TCONV_COMPONENT)

  IF (NOT DEFINED _TCONV_MISSING_COMPONENTS AND NOT DEFINED _TCONV_STATIC_MISSING_COMPONENTS)
    # Success: all components were found
    SET (TCONV_INCLUDE_DIRS "${TCONV_INCLUDE_DIR}")
    SET (TCONV_LIBRARIES ${_TCONV_LIBRARIES})
    SET (TCONV_STATIC_LIBRARIES ${_TCONV_STATIC_LIBRARIES} ${CMAKE_DL_LIBS})
  ELSE ()
    MESSAGE (STATUS "Several TCONV components were not found:")

    # Display missing components indented, each on a separate line
    FOREACH (_TCONV_MISSING_COMPONENT ${_TCONV_MISSING_COMPONENTS})
      MESSAGE (STATUS "  " ${_TCONV_MISSING_COMPONENT})
    ENDFOREACH ()
    FOREACH (_TCONV_STATIC_MISSING_COMPONENT ${_TCONV_STATIC_MISSING_COMPONENTS})
      MESSAGE (STATUS "  " ${_TCONV_STATIC_MISSING_COMPONENT})
    ENDFOREACH ()
  ENDIF ()

  #
  # Simulate hopefully correct flags based on heuristic discovery
  #
  IF (NOT _TCONV_C_FLAGS_SHARED)
    SET (_TCONV_C_FLAGS_SHARED "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
  ENDIF ()

  IF (NOT _TCONV_CXX_FLAGS_SHARED)
    SET (_TCONV_CXX_FLAGS_SHARED "${CMAKE_SHARED_LIBRARY_CXX_FLAGS}")
  ENDIF ()

  IF (NOT _TCONV_LINK_FLAGS)
    SET (_TCONV_LINK_FLAGS ${TCONV_LIBRARIES})
  ENDIF ()

  IF (NOT _TCONV_STATIC_LINK_FLAGS)
    SET (_TCONV_STATIC_LINK_FLAGS ${TCONV_STATIC_LIBRARIES})
  ENDIF ()

ENDIF (TCONV_INCLUDE_DIR)

SET (_TCONV_LDFLAGS " ")
FOREACH (_tconv_link_flag ${_TCONV_LINK_FLAGS})
  SET (_TCONV_LDFLAGS "${_TCONV_LDFLAGS} ${_tconv_link_flag}")
ENDFOREACH ()

SET (_TCONV_STATIC_LDFLAGS " ")
FOREACH (_tconv_static_link_flag ${_TCONV_STATIC_LINK_FLAGS})
  SET (_TCONV_STATIC_LDFLAGS "${_TCONV_STATIC_LDFLAGS} ${_tconv_static_link_flag}")
ENDFOREACH ()

MARK_AS_ADVANCED (TCONV_ROOT_DIR TCONV_INCLUDE_DIR)

FIND_PACKAGE_HANDLE_STANDARD_ARGS (TCONV
  REQUIRED_VARS
  TCONV_INCLUDE_DIR
  TCONV_LIBRARIES)

IF (_TCONV_LINK_FLAGS AND _TCONV_STATIC_LINK_FLAGS)
  SET (TCONV_FOUND 1 CACHE STRING "TCONV is found")
ENDIF ()

SET (TCONV_C_FLAGS_SHARED    "${_TCONV_C_FLAGS_SHARED}"    CACHE STRING "C flags when compiling against shared TCONV")
SET (TCONV_CXX_FLAGS_SHARED  "${_TCONV_CXX_FLAGS_SHARED}"  CACHE STRING "C flags when compiling against shared TCONV")
SET (TCONV_LINK_FLAGS        "${_TCONV_LINK_FLAGS}"        CACHE STRING "Linker flags when linking against TCONV")
SET (TCONV_LDFLAGS           "${_TCONV_LDFLAGS}"           CACHE STRING "Stringified version of TCONV_LINK_FLAGS")
SET (TCONV_STATIC_LINK_FLAGS "${_TCONV_STATIC_LINK_FLAGS}" CACHE STRING "Linker flags when linking against static TCONV")
SET (TCONV_STATIC_LDFLAGS    "${_TCONV_STATIC_LDFLAGS}"    CACHE STRING "Stringified version of TCONV_STATIC_LINK_FLAGS")

IF(TCONV_FOUND)
  MESSAGE(STATUS "-----------------------------------------")
  MESSAGE(STATUS "Setup TCONV:")
  MESSAGE(STATUS "")
  MESSAGE(STATUS "           ROOT_DIR: ${TCONV_ROOT_DIR}")
  MESSAGE(STATUS "     C_FLAGS_SHARED: ${TCONV_C_FLAGS_SHARED}")
  MESSAGE(STATUS "   CXX_FLAGS_SHARED: ${TCONV_CXX_FLAGS_SHARED}")
  MESSAGE(STATUS "        INCLUDE_DIR: ${TCONV_INCLUDE_DIR}")
  MESSAGE(STATUS "          LIBRARIES: ${TCONV_LIBRARIES}")
  MESSAGE(STATUS "   STATIC LIBRARIES: ${TCONV_STATIC_LIBRARIES}")
  MESSAGE(STATUS "         LINK_FLAGS: ${TCONV_LINK_FLAGS}")
  MESSAGE(STATUS "  STATIC LINK_FLAGS: ${TCONV_STATIC_LINK_FLAGS}")
  MESSAGE(STATUS "            LDFLAGS: ${TCONV_LDFLAGS}")
  MESSAGE(STATUS "     STATIC LDFLAGS: ${TCONV_STATIC_LDFLAGS}")
  MESSAGE(STATUS "-----------------------------------------")
ENDIF()

MARK_AS_ADVANCED (
  TCONV_FOUND
  TCONV_C_FLAGS_SHARED
  TCONV_CXX_FLAGS_SHARED
  TCONV_LINK_FLAGS
  TCONV_LDFLAGS
  TCONV_STATIC_LINK_FLAGS
  TCONV_STATIC_LDFLAGS
  )
