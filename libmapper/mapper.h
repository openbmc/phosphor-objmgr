#include <systemd/sd-bus.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct mapper_async_wait mapper_async_wait;
void mapper_wait_async_free(mapper_async_wait *);

int mapper_wait_async(sd_bus *, char *[],
		void (*)(int, void *), void *, mapper_async_wait **);
int mapper_get_service(sd_bus *conn, const char *obj, char **service);
#ifdef __cplusplus
}
#endif
