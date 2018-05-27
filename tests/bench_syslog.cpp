#include <stddef.h>
#include <syslog.h>


int main(int argc, char **argv) {
    openlog(NULL, LOG_PERROR | LOG_CONS, LOG_USER);

    for (size_t i = 0; i < 16; ++i) {
        syslog(LOG_INFO, "syslog message (%zu, %zu) : This is some text for your pleasure", 0lu, i);
    }
    return 0;
}
