#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ELF_OVERLAY_OK 0
#define ELF_OVERLAY_ERR_IO -1
#define ELF_OVERLAY_ERR_FORMAT -2
#define ELF_OVERLAY_ERR_MEM -3
#define ELF_OVERLAY_ERR_RANGE -4
#define ELF_OVERLAY_ERR_UNSUPPORTED -5
#define ELF_OVERLAY_ERR_NOTFOUND -6

#ifdef __cplusplus
}
#endif
