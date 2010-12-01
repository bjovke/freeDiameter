# The rt_debug extension
PROJECT("Interactive debug facility based on SWIG" C)


# This module is using Python
FIND_PACKAGE(PythonLibs)
INCLUDE_DIRECTORIES(${PYTHON_INCLUDE_PATH})

##### 
# Wrapper to fD in python
FIND_PACKAGE(SWIG REQUIRED)
INCLUDE(${SWIG_USE_FILE})
SET(CMAKE_SWIG_FLAGS "")

# We create the module even if we don't use it, so that intermediate values are defined
SWIG_ADD_MODULE(diwrap python diwrap.i)

#####
# Extension that embeds the python interpreter
FD_ADD_EXTENSION(dbg_interactive dbg_interactive.c ${swig_generated_sources} ${swig_other_sources})
TARGET_LINK_LIBRARIES(dbg_interactive ${PYTHON_LIBRARIES})
