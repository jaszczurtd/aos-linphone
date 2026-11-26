/*
 * The lamp class: a subclass of Area that draws a filled circle with a dark
 * rim on the background of its group, with waves around it in the ringing
 * variant. The colours are pens obtained from
 * the screen between MUIM_Setup and MUIM_Cleanup, so the lamp works on
 * palette screens too.
 */
#include "lamp.h"
#include "mui_compat.h"

#include <exec/types.h>
#include <graphics/view.h>
#include <intuition/screens.h>

#include <SDI/SDI_hook.h>
#include <clib/alib_protos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#define PENS  6
#define RIM   3 /* index of the rim pen after the three colours */
#define WAVE1 4 /* the inner half of the waves */
#define WAVE2 5 /* the outer, fading half */

/* Red, yellow, green, rim, waves. */
static const ULONG RGB[PENS] = {0xd02020, 0xe8c000, 0x20b020, 0x303030, 0x30c030, 0xa0dca0};

struct lamp_data {
    ULONG colour;
    BOOL waves;
    ULONG frame;
    LONG pens[PENS]; /* -1 while the object has no screen */
};

static ULONG spread(ULONG byte) {
    return byte * 0x01010101UL;
}

static void obtain_pens(Object *obj, struct lamp_data *data) {
    struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
    for (int i = 0; i < PENS; ++i) {
        data->pens[i] = ObtainBestPen(cm, spread((RGB[i] >> 16) & 0xff), spread((RGB[i] >> 8) & 0xff),
                                      spread(RGB[i] & 0xff), OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    }
}

static void release_pens(Object *obj, struct lamp_data *data) {
    struct ColorMap *cm = _screen(obj)->ViewPort.ColorMap;
    for (int i = 0; i < PENS; ++i) {
        if (data->pens[i] != -1) {
            ReleasePen(cm, (ULONG)data->pens[i]);
            data->pens[i] = -1;
        }
    }
}

static int isqrt(int n) {
    int r = 0;
    while ((r + 1) * (r + 1) <= n) {
        ++r;
    }
    return r;
}

/* A circle of diameter d with its bounding box at x, y, one RectFill per
   row; rows are measured in half pixels from the centre. */
static void fill_circle(struct RastPort *rp, LONG pen, int x, int y, int d) {
    if (pen == -1 || d <= 0) {
        return;
    }
    SetAPen(rp, (ULONG)pen);
    for (int row = 0; row < d; ++row) {
        int dy = 2 * row + 1 - d;
        int w = isqrt(d * d - dy * dy);
        if (w > 0) {
            int left = x + (d - w) / 2;
            RectFill(rp, left, y + row, left + w - 1, y + row);
        }
    }
}

static IPTR lamp_new(Class *cl, Object *obj, struct opSet *msg) {
    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg);
    if (obj != NULL) {
        struct lamp_data *data = INST_DATA(cl, obj);
        data->colour = GetTagData(AOS_LAMP_COLOUR, AOS_LAMP_RED, msg->ops_AttrList);
        data->waves = GetTagData(AOS_LAMP_WAVES, FALSE, msg->ops_AttrList) ? TRUE : FALSE;
        for (int i = 0; i < PENS; ++i) {
            data->pens[i] = -1;
        }
    }
    return (IPTR)obj;
}

static IPTR lamp_set(Class *cl, Object *obj, struct opSet *msg) {
    struct lamp_data *data = INST_DATA(cl, obj);
    ULONG colour = GetTagData(AOS_LAMP_COLOUR, data->colour, msg->ops_AttrList);
    ULONG frame = GetTagData(AOS_LAMP_FRAME, data->frame, msg->ops_AttrList) % AOS_LAMP_FRAMES;
    if (colour != data->colour || frame != data->frame) {
        data->colour = colour;
        data->frame = frame;
        MUI_Redraw(obj, MADF_DRAWOBJECT);
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR lamp_ask_min_max(Class *cl, Object *obj, struct MUIP_AskMinMax *msg) {
    struct lamp_data *data = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, (Msg)msg);
    struct MUI_MinMax *mm = msg->MinMaxInfo;
    int d = _font(obj)->tf_YSize * (data->waves ? 2 : 1);
    mm->MinWidth += d + 4;
    mm->DefWidth += d + 4;
    mm->MaxWidth += d + 4;
    mm->MinHeight += d;
    mm->DefHeight += d;
    mm->MaxHeight = MUI_MAXMAX;
    return 0;
}

static IPTR lamp_draw(Class *cl, Object *obj, struct MUIP_Draw *msg) {
    struct lamp_data *data = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, (Msg)msg); /* the background */
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) {
        return 0;
    }
    int w = _mwidth(obj), h = _mheight(obj);
    int d = (w < h ? w : h) - 2;
    int d_max = _font(obj)->tf_YSize * (data->waves ? 2 : 1);
    if (d > d_max) {
        d = d_max;
    }
    int cx = _mleft(obj) + w / 2, cy = _mtop(obj) + h / 2;
    ULONG colour = data->colour <= AOS_LAMP_GREEN ? data->colour : AOS_LAMP_RED;
    if (data->waves) {
        /* Two waves half a cycle apart grow from the light to the edge and
           fade in their outer half. */
        int outer = d / 2, light = d / 6 + 1;
        for (ULONG k = 0; k < 2; ++k) {
            ULONG phase = (data->frame + k * AOS_LAMP_FRAMES / 2) % AOS_LAMP_FRAMES;
            int r = light + 2 + (outer - light - 2) * (int)phase / (AOS_LAMP_FRAMES - 1);
            LONG pen = data->pens[phase < AOS_LAMP_FRAMES / 2 ? WAVE1 : WAVE2];
            if (pen != -1) {
                SetAPen(_rp(obj), (ULONG)pen);
                DrawEllipse(_rp(obj), cx, cy, r, r);
                DrawEllipse(_rp(obj), cx, cy, r - 1, r - 1);
            }
        }
        d = 2 * light;
    }
    int x = cx - d / 2, y = cy - d / 2;
    fill_circle(_rp(obj), data->pens[RIM], x, y, d);
    fill_circle(_rp(obj), data->pens[colour], x + 1, y + 1, d - 2);
    return 0;
}

/* clang-format off */
SDISPATCHER(lamp_dispatcher) {
    switch (msg->MethodID) {
    case OM_NEW: return lamp_new(cl, obj, (struct opSet *)msg);
    case OM_SET: return lamp_set(cl, obj, (struct opSet *)msg);
    case MUIM_AskMinMax: return lamp_ask_min_max(cl, obj, (struct MUIP_AskMinMax *)msg);
    case MUIM_Draw: return lamp_draw(cl, obj, (struct MUIP_Draw *)msg);
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg)) {
            return FALSE;
        }
        obtain_pens(obj, INST_DATA(cl, obj));
        return TRUE;
    case MUIM_Cleanup:
        release_pens(obj, INST_DATA(cl, obj));
        return DoSuperMethodA(cl, obj, msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
/* clang-format on */

struct MUI_CustomClass *aos_lamp_class_create(void) {
    return MUI_CreateCustomClass(NULL, AOS_SUPERCLASS(MUIC_Area), NULL, sizeof(struct lamp_data), ENTRY(lamp_dispatcher));
}

void aos_lamp_class_delete(struct MUI_CustomClass *lamp_class) {
    if (lamp_class != NULL) {
        MUI_DeleteCustomClass(lamp_class);
    }
}
