#
# project: slmem

if (NOT FIPS_IMPORT)
    cmake_minimum_required(VERSION 2.8)
    get_filename_component(FIPS_ROOT_DIR "../fips" ABSOLUTE)
    include("${FIPS_ROOT_DIR}/cmake/fips.cmake")
    fips_setup()
    fips_project(slmem)
endif()

fips_begin_lib(slmem)
    fips_files(stub.cpp)
    fips_src(include)
fips_end_lib()

#-[ execs ]---------------------------------------------------------------------

if (NOT FIPS_IMPORT)
	fips_ide_group(Tests)
	fips_add_subdirectory(tests)
endif()

#-------------------------------------------------------------------------------
