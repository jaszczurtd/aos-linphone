/*
 * The VHI cameras for systems without uvc.class (MorphOS, until the class
 * is ported): none are registered.
 */
#include "vhi/vhi_camera.h"

int aos_vhi_cameras_register(MSFactory *factory) {
    (void)factory;
    return 0;
}
