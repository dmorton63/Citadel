#pragma once

// Minimal export macro shim for miniz.
// In this kernel build we statically compile the C sources, so we don't need
// any symbol export/import attributes.
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif
