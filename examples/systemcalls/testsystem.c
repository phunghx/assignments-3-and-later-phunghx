#include "systemcalls.h"
int main() {
    bool ret = do_exec(2, "/bin/echo","helllo world");
    printf("%d", ret);
    return 1;
}