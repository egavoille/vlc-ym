/*****************************************************************************
 * ymdepack.cpp : décompression LHA (-lh5-) des fichiers YM, pour le filtre
 * de flux du plugin. Réutilise le décompresseur (durci) de ST-Sound.
 *
 * Copyright (C) 2026 Emmanuel Gavoille <egavoille@gmail.com>
 * https://lesbidouillesdemonsieurm.com
 *****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "YmTypes.h"
#include "LZH/LZH.H"
#include "ymdepack.h"

#define YM_DEPACK_MAX  (64u << 20)

static uint32_t le32 (const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

extern "C" int ym_is_lh5 (const uint8_t *p, size_t len)
{
    /* En-tête LHA niveau 0/1 : taille, somme, "-lh5-", compressé, original */
    return len >= 22 && p[0] >= 20 && !memcmp (p + 2, "-lh5-", 5);
}

extern "C" int ym_depack (const uint8_t *src, size_t srclen,
                          uint8_t **dst, size_t *dstlen)
{
    *dst = NULL;
    *dstlen = 0;

    if (!ym_is_lh5 (src, srclen))
        return -1;

    size_t   hdr      = (size_t)src[0] + 2;     /* données après l'en-tête + CRC */
    uint32_t packed   = le32 (src + 7);
    uint32_t original = le32 (src + 11);

    if (original == 0 || original > YM_DEPACK_MAX || hdr >= srclen)
        return -1;
    if (packed > srclen - hdr)
        packed = (uint32_t)(srclen - hdr);

    uint8_t *out = (uint8_t *)malloc (original);
    if (out == NULL)
        return -1;

    CLzhDepacker *dep = new CLzhDepacker;
    bool ok = dep->LzUnpack ((void *)(src + hdr), (int)packed, out, (int)original);
    delete dep;

    if (!ok)
    {
        free (out);
        return -1;
    }
    *dst = out;
    *dstlen = original;
    return 0;
}
