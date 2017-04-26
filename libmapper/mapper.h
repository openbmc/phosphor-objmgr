#include <stdbool.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct mapper_async_wait mapper_async_wait;
void mapper_wait_async_free(mapper_async_wait *);

int mapper_wait_async(sd_bus *, sd_event *, char *[], char *[],
		void (*)(int, void *), void *, mapper_async_wait **,
		bool);
int mapper_get_service(sd_bus *conn, const char *obj, char **service);
int mapper_get_object(sd_bus *conn, const char *obj, sd_bus_message **reply);
#ifdef __cplusplus
}
#endif
