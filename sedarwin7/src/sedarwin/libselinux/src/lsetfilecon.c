#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "selinux_internal.h"
#include "policy.h"
#include <sys/mac.h>

int lsetfilecon_raw(const char *path, security_context_t context)
{
        mac_t mac;
        char  tmp[strlen(context) + strlen("sebsd/0")];
        int   r;

        if (mac_prepare(&mac, "sebsd"))
                return -1;

        strcpy(tmp, "sebsd/");
        strcat(tmp, context);
        if (mac_from_text(&mac, tmp)) {
                mac_free(mac);
                return -1;
        }
        r = mac_set_link(path, mac);
        mac_free(mac);
        return r;
}
hidden_def(lsetfilecon_raw)

int lsetfilecon(const char *path, security_context_t context)
{
	int ret;
	security_context_t rcontext = context;

	if (context_translations && trans_to_raw_context(context, &rcontext))
		return -1;

 	ret = lsetfilecon_raw(path, rcontext);

	if (context_translations)
		freecon(rcontext);

	return ret;
}
