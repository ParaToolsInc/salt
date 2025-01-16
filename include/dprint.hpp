#ifndef DPRINT_HPP
#define DPRINT_HPP

#include "llvm/Support/raw_ostream.h"

namespace salt {
    void enableVerbose();

    llvm::raw_ostream &verboseStream();
}

#ifdef DEBUG_NO_WAY
#define DPRINT(__fmt, ...) printf(__fmt, ##__VA_ARGS__)
#define DPRINT0(__fmt) printf(__fmt)
#else
#define DPRINT(__fmt, ...)
#define DPRINT0(__fmt)
#endif

#endif //DPRINT_HPP
