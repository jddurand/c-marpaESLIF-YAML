CMAKE_MINIMUM_REQUIRED (VERSION 3.0.0 FATAL_ERROR)
IF (NOT MYPACKAGEBOOTSTRAP_DONE)
  INCLUDE ("3rdparty/github/cmake-utils/cmake/MyPackageBootstrap.cmake")
ENDIF ()
MYPACKAGESTART (genericHash 1 0 56)

###############
# Executables #
###############
MYPACKAGETESTEXECUTABLE(genericHashTester test/genericHash.c)

################
# Dependencies #
################
MYPACKAGEDEPENDENCY (genericStack  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/github/genericStack" LOCAL)
MYPACKAGEDEPENDENCY (genericLogger "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/github/genericLogger" TESTS LOCAL)

#########
# Tests #
#########
MYPACKAGECHECK(genericHashTester)

#############
# Packaging #
#############
MYPACKAGEPACK("" "Generic Logger")

#########
# Setup #
#########
MYPACKAGEPRINTSETUP()
