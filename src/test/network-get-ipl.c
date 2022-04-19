
#include "_assert.h"
#include "network.h"

char *Corephosts[] = {
   "usw-node.mochimap.com",
   "use-node.mochimap.com",
   "usc-node.mochimap.com",
   "sgp-node.mochimap.com",
   "deu-node.mochimap.com",
   ""
};
char **hostsp = Corephosts;

int main()
{
   int status = VERROR;
   NODE node;

   sock_startup();  /* enable socket support */
   /* try communicate with an invalid nodes */
   ASSERT_NE(get_ipl(&node, aton("example.com")), VEOK);
   /* try communicate with one of the valid mochimap nodes */
   do {
      status = get_ipl(&node, aton(*(hostsp++)));
   } while(status && **hostsp);
   ASSERT_EQ_MSG(status, VEOK, "failed to communicate with mochimap nodes");
   ASSERT_NE_MSG(*Rplist, 0, "Rplist[] should contain at least one peer");
   sock_cleanup();
}
