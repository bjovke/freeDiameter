#!/bin/bash -x

# This script will retrieve the list of tests to run from the freediameter website,
# and execute them one by one.

ROOTDIR=$HOME/fDtests
if [ -d $ROOTDIR ]; then
   echo "The working directory $ROOTDIR does not exist. Please create or edit the script."
   exit 1;
fi

WORKDIR=$ROOTDIR/data

# The script requires a local.cmake file to exist and define the following:
# CTEST_SITE=
#     the name of the build slave.
# Exemple: SET(CTEST_SITE "Ubuntu-Lucid-64b")
if [ ! -e $ROOTDIR/local.cmake ]; then
   echo "Missing $ROOTDIR/local.cmake file";
   exit 1;
fi

# Now, cleanup any previous data. We start from clean slate.
rm -rf $WORKDIR
mkdir $WORKDIR

# Retrieve the default parameters.
wget "http://www.freediameter.net/hg/freeDiameter/raw-file/tip/CTestConfig.cmake" -O $WORKDIR/1_default.cmake
if [ ! -e $WORKDIR/1_default.cmake ]; then
   echo "Error retrieving CTestConfig.cmake file";
   exit 1;
fi

# Retrieve the list of build names
wget "http://www.freediameter.net/hg/freeDiameter/raw-file/tip/contrib/nightly_tests/tests.list" -O $WORKDIR/2_tests.list
if [ ! -e $WORKDIR/2_tests.list ]; then
   echo "Error retrieving tests.list file";
   exit 1;
fi

# Now, for each test in the list
for t in $(cat $WORKDIR/2_tests.list); do
   # Create the work environment
   mkdir $WORKDIR/$t
   
   #### Create the script
   
   # Project name, ...
   cp $WORKDIR/1_default.cmake $WORKDIR/$t/CTestScript.cmake
   
   # Path name, build configuration, ...
   cat >> $WORKDIR/$t/CTestScript.cmake << EOF
##########################
SET(CTEST_SOURCE_DIRECTORY "$WORKDIR/source")
SET(CTEST_BINARY_DIRECTORY "$WORKDIR/$t/build")

set(CTEST_CMAKE_GENERATOR "Unix Makefiles")
set(CTEST_BUILD_CONFIGURATION "Profiling")
set(CTEST_BUILD_OPTIONS "")

set(WITH_MEMCHECK FALSE)
set(WITH_COVERAGE TRUE)
##########################
EOF   
   
   wget "http://www.freediameter.net/hg/freeDiameter/raw-file/tip/contrib/nightly_tests/$t.conf" -O $WORKDIR/$t/params.conf
   if [ ! -e $WORKDIR/$t/params.conf ]; then
      echo "Error retrieving $t.conf file";
      continue;
   fi
   
   # The retrieved parameters will overwrite the defaults
   cat $WORKDIR/$t/params.conf >> $WORKDIR/$t/CTestScript.cmake
   
   # Now, the remaining of the script
   cat >> $WORKDIR/$t/CTestScript.cmake << EOF
#######################################################################

ctest_empty_binary_directory(\${CTEST_BINARY_DIRECTORY})

find_program(CTEST_HG_COMMAND NAMES hg)
find_program(CTEST_COVERAGE_COMMAND NAMES gcov)
find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind)

# set(CTEST_MEMORYCHECK_SUPPRESSIONS_FILE \${CTEST_SOURCE_DIRECTORY}/tests/valgrind.supp)

if(NOT EXISTS "\${CTEST_SOURCE_DIRECTORY}")
  set(CTEST_CHECKOUT_COMMAND "\${CTEST_HG_COMMAND} clone http://www.freediameter.net/hg/freeDiameter \${CTEST_SOURCE_DIRECTORY}")
endif()

set(CTEST_UPDATE_COMMAND "\${CTEST_HG_COMMAND}")

set(CTEST_CONFIGURE_COMMAND "\${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE:STRING=\${CTEST_BUILD_CONFIGURATION}")
set(CTEST_CONFIGURE_COMMAND "\${CTEST_CONFIGURE_COMMAND} -DWITH_TESTING:BOOL=ON \${CTEST_BUILD_OPTIONS}")
set(CTEST_CONFIGURE_COMMAND "\${CTEST_CONFIGURE_COMMAND} \\"-G\${CTEST_CMAKE_GENERATOR}\\"")
set(CTEST_CONFIGURE_COMMAND "\${CTEST_CONFIGURE_COMMAND} \\"\${CTEST_SOURCE_DIRECTORY}\\"")

ctest_start("Nightly")
ctest_update()
ctest_configure()
ctest_build()
ctest_test()
if (WITH_MEMCHECK AND CTEST_COVERAGE_COMMAND)
  ctest_coverage()
endif (WITH_MEMCHECK AND CTEST_COVERAGE_COMMAND)
if (WITH_MEMCHECK AND CTEST_MEMORYCHECK_COMMAND)
  ctest_memcheck()
endif (WITH_MEMCHECK AND CTEST_MEMORYCHECK_COMMAND)
# ctest_submit()
EOF

   # Test until here for now...
   echo "Ready to start $WORKDIR/$t/CTestScript.cmake, abort for now"
   exit 1;
done



