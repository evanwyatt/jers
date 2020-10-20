#include "jers.h"

#define CHECK_SIZE(x, n) _Static_assert(sizeof(x) == n, "Expected sizeof(" #x ") to be " #n)

CHECK_SIZE(jersJob, 256);
CHECK_SIZE(jersJobInfo, 16);
CHECK_SIZE(jersJobFilter, 136);
CHECK_SIZE(jersJobAdd, 256);
CHECK_SIZE(jersJobMod, 256);

CHECK_SIZE(jersQueue, 108);
CHECK_SIZE(jersQueueInfo, 16);
CHECK_SIZE(jersQueueFilter, 16);
CHECK_SIZE(jersQueueAdd, 64);
CHECK_SIZE(jersQueueMod, 64);
CHECK_SIZE(jersQueueDel, 8);

CHECK_SIZE(jersResource, 16);
CHECK_SIZE(jersResourceInfo, 16);
CHECK_SIZE(jersResourceFilter, 20);
CHECK_SIZE(jersResourceAdd, 32);
CHECK_SIZE(jersResourceMod, 32);
CHECK_SIZE(jersResourceDel, 8);

CHECK_SIZE(jers_tag_t, 16);
CHECK_SIZE(jersAgentFilter, 8);
CHECK_SIZE(jersAgent, 12);
CHECK_SIZE(struct jobStats, 64);
CHECK_SIZE(jersStats, 112);
