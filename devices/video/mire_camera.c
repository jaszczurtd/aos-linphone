/*
 * A camera description whose detection adds the Mire camera of
 * mediastreamer2. Being registered, it runs again on every reload of the
 * cameras.
 */
#include "mire_camera.h"

#include <mediastreamer2/mediastream.h>
#include <mediastreamer2/mswebcam.h>

#include <string.h>

static void mire_detect(MSWebCamManager *manager) {
    MSWebCamDesc *mire = ms_mire_webcam_desc_get();
    /* mediastreamer2 lists Mire itself when DEBUG is 1. */
    for (const MSList *it = ms_web_cam_manager_get_list(manager); it != NULL; it = it->next) {
        if (((MSWebCam *)it->data)->desc == mire) {
            return;
        }
    }
    ms_web_cam_manager_add_cam(manager, ms_web_cam_new(mire));
}

static MSWebCamDesc mire_lister_desc = {
    .driver_type = "AosMire",
    .detect = mire_detect,
};

void aos_mire_register(MSFactory *factory) {
    ms_web_cam_manager_register_desc(ms_factory_get_web_cam_manager(factory), &mire_lister_desc);
}
