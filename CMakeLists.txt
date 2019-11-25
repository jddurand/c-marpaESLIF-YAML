CMAKE_MINIMUM_REQUIRED (VERSION 3.0.0 FATAL_ERROR)
IF (NOT MYPACKAGEBOOTSTRAP_DONE)
  INCLUDE ("3rdparty/github/cmake-utils/cmake/MyPackageBootstrap.cmake")
ENDIF ()
#
# Usually the version here correspong to the NEXT version - it is git tagged
# when we do make marpaESLIFPerlRelease
#
# SET (MYPACKAGE_DEBUG TRUE)
MYPACKAGESTART (marpaESLIF_YAML 3 0 29)

MYPACKAGELIBRARY(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/config.h.in
  ${INCLUDE_OUTPUT_PATH}/marpaESLIF_YAML/internal/config.h
  src/marpaESLIF_YAML.c)


###############
# Executables #
###############
MYPACKAGETESTEXECUTABLE(marpaESLIFYAMLTester       test/marpaESLIF_YAML_Tester.c)

################
# Dependencies #
################
MYPACKAGEDEPENDENCY (marpaESLIF    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/github/marpaESLIF"   LIBS LOCAL)
MYPACKAGEDEPENDENCY (genericStack  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/github/genericStack" LIBS LOCAL)
MYPACKAGEDEPENDENCY (genericHash   "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/github/genericHash"  LIBS LOCAL)

#########
# Tests #
#########
MYPACKAGECHECK(marpaESLIFYAMLTester)

###########
# Install #
###########
# MYPACKAGEMAN("doc/API/README.pod"                 "MARPAESLIF_YAML"        "3" "${MARPAESLIF_YAML_VERSION}")

#############
# Packaging #
#############
MYPACKAGEPACK ("" "YAML implementation using Marpa")

#########
# Setup #
#########
MYPACKAGEPRINTSETUP()
