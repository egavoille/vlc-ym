#ifndef YMDEPACK_H
#define YMDEPACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Vrai si le buffer commence par un en-tête LHA -lh5- */
int ym_is_lh5 (const uint8_t *p, size_t len);

/* Décompresse une archive LHA -lh5- d'un seul fichier.
 * Renvoie 0 et un buffer malloc() dans *dst, ou -1 en cas d'erreur. */
int ym_depack (const uint8_t *src, size_t srclen, uint8_t **dst, size_t *dstlen);

#ifdef __cplusplus
}
#endif

#endif
