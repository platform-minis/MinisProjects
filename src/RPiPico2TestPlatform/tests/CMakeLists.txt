# ---------------------------------------------------------------------------
# Tests root
# ---------------------------------------------------------------------------
include(FetchContent)

# Download Unity test framework (MIT licence, single C file)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(unity)

# Unit tests always compile for HOST
add_subdirectory(unit)
