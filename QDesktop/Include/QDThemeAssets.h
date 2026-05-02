#pragma once

#include "QCTypes.h"

namespace QD
{
    struct CitadelThemeAssets;

    // Resolve semantic theme asset key (or selected legacy asset paths) to a concrete VFS path.
    // Returns true when resolved and writes null-terminated output path.
    bool resolveThemeAssetKey(const char *tokenOrPath, char *out, QC::usize outCap);

    // Resolve semantic theme asset key using a loaded theme package first,
    // then fall back to built-in semantic/legacy mappings.
    bool resolveThemeAssetKey(const char *tokenOrPath,
                              const CitadelThemeAssets *assets,
                              char *out,
                              QC::usize outCap);
}
