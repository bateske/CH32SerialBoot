#ifndef CHGAME_APPMETA_H
#define CHGAME_APPMETA_H
#include "chgame_map.h"

#define APP_INVALID_NO_META   1
#define APP_INVALID_LENGTH    2
#define APP_INVALID_CRC       3
#define APP_VALID             0

/* Reads the metadata page and, if it is plausible, CRC-checks the whole image.
 * Returns APP_VALID only when the image is complete and intact — which is the
 * single gate on ever jumping into the application. */
int appmeta_check(void);

static inline const chgame_meta_t *appmeta(void)
{
    return (const chgame_meta_t *)CHGAME_META_ADDR;
}

#endif
