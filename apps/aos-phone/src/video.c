/*
 * The video area class: a subclass of Area that draws its own box
 * completely (MUIA_FillArea off), the picture with WritePixelArray() and
 * the borders around it with FillPixelArray().
 */
#include "video.h"
#include "mui_compat.h"

#include <cybergraphx/cybergraphics.h>
#include <exec/types.h>
#include <libraries/mui.h>

#include <SDI/SDI_hook.h>
#include <clib/alib_protos.h>
#include <proto/cybergraphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#define BLACK 0x00000000 /* ARGB */

struct video_data {
    struct aos_video_surface *surface;
    enum aos_video_view view;
    int width; /* size given to the surface; 0 while hidden */
    int height;
};

static void tell_size(struct video_data *data, int width, int height) {
    if (width != data->width || height != data->height) {
        data->width = width;
        data->height = height;
        aos_video_surface_set_size(data->surface, data->view, width, height);
    }
}

static void fill(Object *obj, int x, int y, int width, int height) {
    if (width > 0 && height > 0) {
        FillPixelArray(_rp(obj), x, y, width, height, BLACK);
    }
}

static IPTR video_ask_min_max(Class *cl, Object *obj, struct MUIP_AskMinMax *msg) {
    struct video_data *data = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, (Msg)msg);
    struct MUI_MinMax *mm = msg->MinMaxInfo;
    /* 4:3 boxes; the preview stays narrow next to the remote picture. The
       window opens at its old size, so the minimum is what a call with video
       gets at first. Both grow in height without limit: the tallest a
       horizontal group gets is the smallest maximum height of its children. */
    int remote = data->view == AOS_VIDEO_REMOTE;
    mm->MinWidth += remote ? 240 : 80;
    mm->MinHeight += remote ? 180 : 60;
    mm->DefWidth += remote ? 320 : 120;
    mm->DefHeight += remote ? 240 : 90;
    if (remote) {
        mm->MaxWidth = MUI_MAXMAX;
    } else {
        mm->MaxWidth += 240;
    }
    mm->MaxHeight = MUI_MAXMAX;
    return 0;
}

static IPTR video_draw(Class *cl, Object *obj, struct MUIP_Draw *msg) {
    struct video_data *data = INST_DATA(cl, obj);
    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))) {
        return 0;
    }
    int x = _mleft(obj), y = _mtop(obj), w = _mwidth(obj), h = _mheight(obj);
    tell_size(data, w, h);

    struct aos_video_frame frame;
    if (!aos_video_surface_get_frame(data->surface, data->view, &frame)) {
        fill(obj, x, y, w, h);
        return 0;
    }
    /* A picture made for a larger box shows its middle part. */
    int fw = frame.width < w ? frame.width : w;
    int fh = frame.height < h ? frame.height : h;
    int fx = x + (w - fw) / 2, fy = y + (h - fh) / 2;
    WritePixelArray((APTR)frame.pixels, (frame.width - fw) / 2, (frame.height - fh) / 2, frame.stride, _rp(obj), fx,
                    fy, fw, fh, RECTFMT_ARGB);
    fill(obj, x, y, w, fy - y);
    fill(obj, x, fy + fh, w, y + h - (fy + fh));
    fill(obj, x, fy, fx - x, fh);
    fill(obj, fx + fw, fy, x + w - (fx + fw), fh);
    return 0;
}

/* clang-format off */
SDISPATCHER(video_dispatcher) {
    struct video_data *data;
    switch (msg->MethodID) {
    case MUIM_AskMinMax: return video_ask_min_max(cl, obj, (struct MUIP_AskMinMax *)msg);
    case MUIM_Draw: return video_draw(cl, obj, (struct MUIP_Draw *)msg);
    case MUIM_Show:
        data = INST_DATA(cl, obj);
        if (!DoSuperMethodA(cl, obj, msg)) {
            return FALSE;
        }
        tell_size(data, _mwidth(obj), _mheight(obj));
        return TRUE;
    case MUIM_Hide:
        data = INST_DATA(cl, obj);
        tell_size(data, 0, 0);
        return DoSuperMethodA(cl, obj, msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
/* clang-format on */

struct MUI_CustomClass *aos_video_class_create(void) {
    return MUI_CreateCustomClass(NULL, AOS_SUPERCLASS(MUIC_Area), NULL, sizeof(struct video_data), ENTRY(video_dispatcher));
}

void aos_video_class_delete(struct MUI_CustomClass *video_class) {
    if (video_class != NULL) {
        MUI_DeleteCustomClass(video_class);
    }
}

Object *aos_video_area_new(struct MUI_CustomClass *video_class, struct aos_video_surface *surface,
                           enum aos_video_view view) {
    /* Space beyond the minimum goes mostly to the remote picture. */
    Object *obj = NewObject(video_class->mcc_Class, NULL, MUIA_FillArea, FALSE, MUIA_HorizWeight,
                            view == AOS_VIDEO_REMOTE ? 300 : 100, TAG_DONE);
    if (obj != NULL) {
        struct video_data *data = INST_DATA(video_class->mcc_Class, obj);
        data->surface = surface;
        data->view = view;
    }
    return obj;
}

void aos_video_area_update(Object *area) {
    MUI_Redraw(area, MADF_DRAWUPDATE);
}
