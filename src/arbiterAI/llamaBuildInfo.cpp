/**
 * @file llamaBuildInfo.cpp
 * @brief Build-info symbols required by llama.cpp's `common` library.
 *
 * `common.cpp` reports the llama.cpp build in its diagnostics, calling into
 * functions that upstream generates at configure time (`build-info.cpp.in`) and
 * compiles into a CMake OBJECT library.  Objects from an OBJECT library are
 * copied into each consuming target rather than archived, so they are absent
 * from the installed `libllama-common.a` and anything linking it fails to
 * resolve them.
 *
 * We supply them here instead of hacking the object into the installed archive
 * from the vcpkg port, which would depend on CMake's internal build layout.
 * The build number is the one arbiterAI already reads from the pinned port
 * manifest, so these report the llama.cpp actually linked, not a placeholder.
 *
 * Nothing in arbiterAI calls these; they exist for the linker and for any
 * diagnostic inside `common` that prints them.
 */

#include "arbiterAI/version.h"

#include <build-info.h>

#include <cstdio>
#include <cstdlib>
#include <string>

int llama_build_number(void)
{
    return std::atoi(ARBITERAI_LLAMACPP_BUILD);
}

const char *llama_commit(void)
{
    // The port pins by build tag rather than commit hash.
    return "";
}

const char *llama_compiler(void)
{
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#else
    return "unknown";
#endif
}

const char *llama_build_target(void)
{
    return "arbiterAI";
}

const char *llama_build_info(void)
{
    static std::string info="b"+std::string(ARBITERAI_LLAMACPP_BUILD);
    return info.c_str();
}

void llama_print_build_info(void)
{
    fprintf(stderr, "llama.cpp build = %s (linked into arbiterAI, built with %s)\n",
        ARBITERAI_LLAMACPP_BUILD, llama_compiler());
}
