/*****************************************************************************
 * ym.c : lecteur de fichiers YM (Atari ST / Amstrad CPC / ZX Spectrum) pour VLC
 *****************************************************************************
 * Copyright (C) 2026 Emmanuel Gavoille <egavoille@gmail.com>
 * https://lesbidouillesdemonsieurm.com
 *
 * Basé sur la bibliothèque ST-Sound d'Arnaud Carré (licence BSD/MIT).
 * Module demux sur le modèle de modules/demux/gme.c de VLC.
 *
 * Ce programme est un logiciel libre ; vous pouvez le redistribuer et/ou le
 * modifier selon les termes de la GNU Lesser General Public License, version
 * 2.1 ou ultérieure.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _WIN32
/* vlc_threads.h utilise struct pollfd, déclarée par winsock2.h */
# include <winsock2.h>
/* Prototype normalement fourni par vlc_fixups.h (absent du SDK) ; utilisé
 * uniquement par une fonction inline de vlc_threads.h que l'on n'appelle pas */
int poll (struct pollfd *, unsigned, int);
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>
#include <vlc_meta.h>
#include <vlc_charset.h>

#include "StSoundLibrary.h"
#include "ymdepack.h"
#include "version.h"

/* Les en-têtes du SDK VLC 3.0 ne fournissent pas les macros de traduction */
#ifndef N_
# define N_(str) (str)
#endif

#define YM_RATE        44100
#define YM_SAMPLES     1024          /* échantillons par bloc (~23 ms), comme Ym2Wav */
#define YM_MAX_FILE    (16 << 20)    /* fichier .ym (compressé) : 16 Mo max */
#define YM_MAX_DEPACK  (64 << 20)    /* taille décompressée : 64 Mo max */

static int  Open (vlc_object_t *);
static void Close (vlc_object_t *);
static int  FilterOpen (vlc_object_t *);
static void FilterClose (vlc_object_t *);

#define LOWPASS_TEXT N_("Filtre passe-bas")
#define LOWPASS_LONGTEXT N_("Adoucit le son du YM2149 / AY-3-8912 " \
    "comme sur la machine d'origine.")
#define LOOP_TEXT N_("Boucler le morceau")
#define LOOP_LONGTEXT N_("Rejoue indéfiniment le morceau depuis son point " \
    "de boucle au lieu de s'arrêter à la fin.")

vlc_module_begin ()
    set_shortname ("YM")
    set_description (N_("YM chiptune demuxer (ST-Sound) - " YM_PLUGIN_AUTHOR))
    set_help (N_("Lecture des musiques .YM (YM2149 / AY-3-8912 : Amstrad CPC, "
                 "Atari ST, ZX Spectrum...).\n"
                 "Plugin v" YM_PLUGIN_VERSION " par " YM_PLUGIN_AUTHOR
                 " <" YM_PLUGIN_EMAIL "> - " YM_PLUGIN_WEBSITE "\n"
                 "Moteur ST-Sound (c) Arnaud Carre."))
    set_category (CAT_INPUT)
    set_subcategory (SUBCAT_INPUT_DEMUX)
    set_capability ("demux", 160)
    add_bool ("ym-lowpass", true, LOWPASS_TEXT, LOWPASS_LONGTEXT, false)
    add_bool ("ym-loop", false, LOOP_TEXT, LOOP_LONGTEXT, false)
    add_shortcut ("ym", "stsound")
    set_callbacks (Open, Close)

    /* Filtre de flux : décompresse les .ym LHA avant que le module
     * « archive » de VLC (libarchive) ne les intercepte comme des archives */
    add_submodule ()
    set_shortname ("YM LHA")
    set_description (N_("YM LHA depacker - " YM_PLUGIN_AUTHOR))
    set_category (CAT_INPUT)
    set_subcategory (SUBCAT_INPUT_STREAM_FILTER)
    set_capability ("stream_filter", 40)
    set_callbacks (FilterOpen, FilterClose)
vlc_module_end ()

/*****************************************************************************
 * Filtre de flux LHA
 *****************************************************************************/
struct stream_sys_t
{
    uint8_t *data;
    size_t   size;
    size_t   pos;
};

static bool IsYmPayload (const uint8_t *p, size_t len)
{
    if (len < 4)
        return false;
    return (p[0] == 'Y' && p[1] == 'M') || !memcmp (p, "MIX1", 4);
}

static ssize_t FilterRead (stream_t *s, void *buf, size_t len)
{
    stream_sys_t *sys = s->p_sys;
    size_t left = sys->size - sys->pos;
    if (len > left)
        len = left;
    if (buf != NULL)
        memcpy (buf, sys->data + sys->pos, len);
    sys->pos += len;
    return len;
}

static int FilterSeek (stream_t *s, uint64_t offset)
{
    stream_sys_t *sys = s->p_sys;
    if (offset > sys->size)
        offset = sys->size;
    sys->pos = offset;
    return VLC_SUCCESS;
}

static int FilterControl (stream_t *s, int query, va_list args)
{
    stream_sys_t *sys = s->p_sys;

    switch (query)
    {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg (args, bool *) = true;
            return VLC_SUCCESS;
        case STREAM_GET_SIZE:
            *va_arg (args, uint64_t *) = sys->size;
            return VLC_SUCCESS;
        case STREAM_GET_PTS_DELAY:
            return vlc_stream_vaControl (s->p_source, query, args);
        case STREAM_SET_PAUSE_STATE:
            return VLC_SUCCESS;
        default:
            return VLC_EGENERIC;
    }
}

static int FilterOpen (vlc_object_t *obj)
{
    stream_t *s = (stream_t *)obj;
    const uint8_t *peek;

    ssize_t peeked = vlc_stream_Peek (s->p_source, &peek, 22);
    if (peeked < 22 || !ym_is_lh5 (peek, peeked))
        return VLC_EGENERIC;

    uint64_t size;
    if (vlc_stream_GetSize (s->p_source, &size) || size > YM_MAX_FILE)
        return VLC_EGENERIC;

    uint8_t *packed = malloc (size);
    if (unlikely(packed == NULL))
        return VLC_ENOMEM;
    if (vlc_stream_Read (s->p_source, packed, size) != (ssize_t)size)
    {
        free (packed);
        return VLC_EGENERIC;
    }

    uint8_t *data;
    size_t len;
    int ret = ym_depack (packed, size, &data, &len);
    free (packed);

    /* Ce n'est pas un YM : on rend la main (le flux source doit être rembobiné) */
    if (ret || !IsYmPayload (data, len))
    {
        free (data);
        if (vlc_stream_Seek (s->p_source, 0))
            msg_Warn (s, "rembobinage du flux impossible");
        return VLC_EGENERIC;
    }

    stream_sys_t *sys = malloc (sizeof (*sys));
    if (unlikely(sys == NULL))
    {
        free (data);
        return VLC_ENOMEM;
    }
    sys->data = data;
    sys->size = len;
    sys->pos = 0;

    s->p_sys = sys;
    s->pf_read = FilterRead;
    s->pf_seek = FilterSeek;
    s->pf_control = FilterControl;
    msg_Dbg (s, "YM LHA décompressé : %zu -> %zu octets", (size_t)size, len);
    return VLC_SUCCESS;
}

static void FilterClose (vlc_object_t *obj)
{
    stream_t *s = (stream_t *)obj;
    stream_sys_t *sys = s->p_sys;
    free (sys->data);
    free (sys);
}

/* ST-Sound modifie une table globale à la création du premier lecteur
 * (ymVolumeTable) : on sérialise création et chargement, car VLC prépare
 * les éléments de la playlist dans un autre thread pendant la lecture. */
static vlc_mutex_t ym_lock = VLC_STATIC_MUTEX;

struct demux_sys_t
{
    YMMUSIC     *music;
    es_out_id_t *es;
    date_t       pts;
    int64_t      length;  /* µs, 0 si inconnu */
    bool         seekable;
    bool         loop;
    char        *title;
    char        *artist;
    char        *comment;
    char        *type;
};

static int Demux (demux_t *);
static int Control (demux_t *, int, va_list);

/* Les chaînes YM sont en ASCII étendu (Latin-1) : conversion en UTF-8. */
static char *Latin1ToUtf8 (const char *in)
{
    if (in == NULL)
        return NULL;
    while (*in == ' ')
        in++;
    size_t len = strlen (in);
    while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\r'
                    || in[len - 1] == '\n'))
        len--;
    if (len == 0)
        return NULL;

    char *out = malloc (2 * len + 1), *p = out;
    if (out == NULL)
        return NULL;
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = in[i];
        if (c < 0x20)
            *p++ = ' ';
        else if (c < 0x80)
            *p++ = c;
        else
        {
            *p++ = 0xC0 | (c >> 6);
            *p++ = 0x80 | (c & 0x3F);
        }
    }
    *p = '\0';
    return out;
}

static uint32_t GetLE32 (const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Reconnaissance : signature YMx en clair, ou archive LHA -lh5- portant
 * l'extension .ym (la grande majorité des fichiers YM sont compressés). */
static bool Probe (demux_t *demux, const uint8_t *peek, size_t len)
{
    if (len >= 4 && peek[0] == 'Y' && peek[1] == 'M'
     && ((peek[2] >= '2' && peek[2] <= '6' && (peek[3] == '!' || peek[3] == 'b'))
      || (peek[2] == 'T' && (peek[3] == '1' || peek[3] == '2'))))
        return true;
    if (len >= 4 && !memcmp (peek, "MIX1", 4))
        return demux_IsPathExtension (demux, ".ym") || demux->obj.force;

    if (len >= 22 && !memcmp (peek + 2, "-lh5-", 5))
        return demux_IsPathExtension (demux, ".ym") || demux->obj.force;

    return false;
}

static int Open (vlc_object_t *obj)
{
    demux_t *demux = (demux_t *)obj;
    const uint8_t *peek;

    ssize_t peeked = vlc_stream_Peek (demux->s, &peek, 22);
    if (peeked < 4 || !Probe (demux, peek, peeked))
        return VLC_EGENERIC;

    uint64_t size;
    if (vlc_stream_GetSize (demux->s, &size) || size < 22 || size > YM_MAX_FILE)
        return VLC_EGENERIC;

    /* Garde-fou sur la taille décompressée annoncée par l'en-tête LHA */
    if (!memcmp (peek + 2, "-lh5-", 5))
    {
        uint32_t orig = GetLE32 (peek + 11);
        if (orig == 0 || orig > YM_MAX_DEPACK || peek[0] < 20)
            return VLC_EGENERIC;
    }

    /* Chargement complet du fichier en mémoire */
    uint8_t *buf = malloc (size);
    if (unlikely(buf == NULL))
        return VLC_ENOMEM;
    if (vlc_stream_Read (demux->s, buf, size) != (ssize_t)size)
    {
        free (buf);
        return VLC_EGENERIC;
    }

    vlc_mutex_lock (&ym_lock);
    YMMUSIC *music = ymMusicCreateWithRate (YM_RATE);
    ymbool ok = music != NULL && ymMusicLoadMemory (music, buf, size);
    vlc_mutex_unlock (&ym_lock);
    free (buf);

    if (!ok)
    {
        if (music != NULL)
        {
            msg_Dbg (demux, "ST-Sound: %s", ymMusicGetLastError (music));
            ymMusicDestroy (music);
        }
        return VLC_EGENERIC;
    }

    demux_sys_t *sys = calloc (1, sizeof (*sys));
    if (unlikely(sys == NULL))
    {
        ymMusicDestroy (music);
        return VLC_ENOMEM;
    }
    sys->music = music;
    sys->loop = var_InheritBool (demux, "ym-loop");
    sys->seekable = ymMusicIsSeekable (music);

    ymMusicSetLoopMode (music, sys->loop);
    ymMusicSetLowpassFiler (music, var_InheritBool (demux, "ym-lowpass"));

    ymMusicInfo_t info;
    ymMusicGetInfo (music, &info);
    if (!sys->loop && info.musicTimeInMs > 0)
        sys->length = (int64_t)info.musicTimeInMs * 1000;
    sys->title   = Latin1ToUtf8 (info.pSongName);
    sys->artist  = Latin1ToUtf8 (info.pSongAuthor);
    sys->comment = Latin1ToUtf8 (info.pSongComment);
    sys->type    = Latin1ToUtf8 (info.pSongType);

    msg_Dbg (demux, "YM: \"%s\" by \"%s\" (%s), %d ms, %sseekable",
             sys->title ? sys->title : "?", sys->artist ? sys->artist : "?",
             sys->type ? sys->type : "?", (int)info.musicTimeInMs,
             sys->seekable ? "" : "not ");

    ymMusicPlay (music);

    es_format_t fmt;
    es_format_Init (&fmt, AUDIO_ES, VLC_CODEC_S16N);
    fmt.audio.i_rate = YM_RATE;
    fmt.audio.i_channels = 1;
    fmt.audio.i_physical_channels = AOUT_CHAN_CENTER;
    fmt.audio.i_bitspersample = 16;
    fmt.audio.i_bytes_per_frame = 2;
    fmt.audio.i_frame_length = 1;
    fmt.audio.i_blockalign = 2;
    fmt.i_bitrate = YM_RATE * 16;

    sys->es = es_out_Add (demux->out, &fmt);
    date_Init (&sys->pts, YM_RATE, 1);
    date_Set (&sys->pts, 0);

    demux->pf_demux = Demux;
    demux->pf_control = Control;
    demux->p_sys = sys;
    return VLC_SUCCESS;
}

static void Close (vlc_object_t *obj)
{
    demux_t *demux = (demux_t *)obj;
    demux_sys_t *sys = demux->p_sys;

    vlc_mutex_lock (&ym_lock);
    ymMusicDestroy (sys->music);
    vlc_mutex_unlock (&ym_lock);
    free (sys->title);
    free (sys->artist);
    free (sys->comment);
    free (sys->type);
    free (sys);
}

static int Demux (demux_t *demux)
{
    demux_sys_t *sys = demux->p_sys;

    block_t *block = block_Alloc (YM_SAMPLES * sizeof (ymsample));
    if (unlikely(block == NULL))
        return VLC_DEMUXER_EOF;

    if (!ymMusicCompute (sys->music, (ymsample *)block->p_buffer, YM_SAMPLES))
    {
        block_Release (block);
        /* PCR final : sans lui, le dernier bloc envoyé peut ne pas être joué */
        es_out_SetPCR (demux->out, VLC_TS_0 + date_Get (&sys->pts));
        return VLC_DEMUXER_EOF;
    }

    block->i_pts = block->i_dts = VLC_TS_0 + date_Get (&sys->pts);
    es_out_SetPCR (demux->out, block->i_pts);
    es_out_Send (demux->out, sys->es, block);
    date_Increment (&sys->pts, YM_SAMPLES);
    return VLC_DEMUXER_SUCCESS;
}

static int SeekTo (demux_t *demux, int64_t time)
{
    demux_sys_t *sys = demux->p_sys;

    if (!sys->seekable || time < 0)
        return VLC_EGENERIC;
    if (sys->length > 0 && time > sys->length)
        time = sys->length;

    ymMusicSeek (sys->music, (ymu32)(time / 1000));
    date_Set (&sys->pts, (int64_t)ymMusicGetPos (sys->music) * 1000);
    return VLC_SUCCESS;
}

static int Control (demux_t *demux, int query, va_list args)
{
    demux_sys_t *sys = demux->p_sys;

    switch (query)
    {
        case DEMUX_CAN_SEEK:
            *va_arg (args, bool *) = sys->seekable;
            return VLC_SUCCESS;

        case DEMUX_GET_POSITION:
        {
            double *pos = va_arg (args, double *);
            if (sys->length <= 0)
                return VLC_EGENERIC;
            *pos = (double)date_Get (&sys->pts) / (double)sys->length;
            if (*pos > 1.)
                *pos = 1.;
            return VLC_SUCCESS;
        }

        case DEMUX_SET_POSITION:
        {
            double pos = va_arg (args, double);
            if (sys->length <= 0)
                return VLC_EGENERIC;
            return SeekTo (demux, (int64_t)(pos * sys->length));
        }

        case DEMUX_GET_LENGTH:
            if (sys->length <= 0)
                return VLC_EGENERIC;
            *va_arg (args, int64_t *) = sys->length;
            return VLC_SUCCESS;

        case DEMUX_GET_TIME:
            *va_arg (args, int64_t *) = date_Get (&sys->pts);
            return VLC_SUCCESS;

        case DEMUX_SET_TIME:
            return SeekTo (demux, va_arg (args, int64_t));

        case DEMUX_GET_META:
        {
            vlc_meta_t *meta = va_arg (args, vlc_meta_t *);
            if (sys->title)
                vlc_meta_SetTitle (meta, sys->title);
            if (sys->artist)
                vlc_meta_SetArtist (meta, sys->artist);
            if (sys->comment)
                vlc_meta_SetDescription (meta, sys->comment);
            if (sys->type)
                vlc_meta_SetGenre (meta, sys->type);
            return VLC_SUCCESS;
        }

        case DEMUX_CAN_PAUSE:
        case DEMUX_CAN_CONTROL_PACE:
        case DEMUX_GET_PTS_DELAY:
            return demux_vaControlHelper (demux->s, 0, -1, 0, 1, query, args);

        case DEMUX_SET_PAUSE_STATE:
            return VLC_SUCCESS;

        default:
            return VLC_EGENERIC;
    }
}
