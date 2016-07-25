#include <systemd/sd-bus.h>

#ifdef __cplusplus
extern "C" {
#endif
int mapper_get_service(sd_bus *conn, const char *obj, char **service);
#ifdef __cplusplus
}
#endif
