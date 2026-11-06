// AXIOM - entry point
#include "game.h"

int main() {
    static ax::App app;   // large object: keep it off the stack
    app.run();
    return 0;
}
