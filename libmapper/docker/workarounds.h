#ifdef IS_DOCKER_IMG
#include <systemd/sd-bus.h>

int sd_bus_message_append_cmdline(sd_bus_message *m, const char *signature,
    char ***x) { return 0; };
#endif
