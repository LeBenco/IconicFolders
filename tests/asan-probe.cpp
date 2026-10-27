// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Benco
#include <cstdlib>
int main(int argc,char**) {
    volatile int offset=argc+16;
    auto memory=static_cast<char*>(malloc(8));
    memory[offset]=42; // Intentional error: verifies that ASan is actually active.
    free(memory); return 0;
}
