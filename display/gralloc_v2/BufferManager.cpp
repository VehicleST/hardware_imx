/*
 * Copyright (C) 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <BufferManager.h>

using namespace android;

BufferManager* BufferManager::sInstance(0);
Mutex BufferManager::sLock(Mutex::PRIVATE);

BufferManager* BufferManager::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

#ifdef IMX_GPU
    sInstance = new GPUBufferManager();
#else
    sInstance = new CPUBufferManager();
#endif

    return sInstance;
}

BufferManager::BufferManager()
{
    for (int i=0; i<MAX_DISPLAY_DEVICE; i++) {
        mDisplays[i] = new Display();
    }
}

Display* BufferManager::getDisplay(int dispid)
{
    if (dispid >= MAX_DISPLAY_DEVICE) {
        ALOGE("%s invalid dispid %d", __FUNCTION__, __LINE__);
        return NULL;
    }

    return mDisplays[dispid];
}

int BufferManager::allocFramebuffer(size_t size, int usage,
                                    buffer_handle_t* pHandle)
{
    if (!pHandle) {
        ALOGE("%s invalid parameters", __FUNCTION__);
        return -EINVAL;
    }

    if (!(usage & GRALLOC_USAGE_HW_FB)) {
        ALOGE("%s alloc framebuffer but usage is not FB", __FUNCTION__);
        return -EINVAL;
    }

    int dispid = 0;

    if (usage & GRALLOC_USAGE_HW_FBX) {
        dispid = 1;
    }
    /*else if (usage & GRALLOC_USAGE_HW_FB2X) {
        dispid = 2;
    }*/

    Display* m = mDisplays[dispid];
    return m->allocFrameBuffer(size, usage, pHandle);
}


int BufferManager::alloc(int w, int h, int format, int usage,
            buffer_handle_t* handle, int* stride)
{
    if (!handle || !stride) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    int err = 0;
    size_t size, alignedw, alignedh, bpp = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            bpp = 4;
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_RGB_565:
            if (format == HAL_PIXEL_FORMAT_RGB_565) {
                bpp = 2;
            }
            else if (format == HAL_PIXEL_FORMAT_RGB_888) {
                bpp = 3;
            }

            /*
             * XXX: Vivante HAL needs 16 pixel alignment in width and 16 pixel
             * alignment in height.
             *
             * Here we assume the buffer will be used by Vivante HAL...
             */
#ifdef IMX_8DV_ALIGN_WORKAROUND
            /* mx8dv request special align to get better performance for GPU tile
               to linear resolve, for example 1920x1080 display resolution with 32bpp,
               stride need set to 1936 */
            alignedw = ALIGN_PIXEL_128(w) + 16;
#else
            alignedw = ALIGN_PIXEL_16(w);
#endif
            alignedh = ALIGN_PIXEL_16(h);
            size = alignedw * alignedh * bpp;
            break;

        case HAL_PIXEL_FORMAT_BLOB:
            alignedw = ALIGN_PIXEL_16(w);
            alignedh = h;
            size = alignedw * alignedh;
            format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            if(h != 1)
                ALOGW("%s, BLOB format, w %d, h %d is not 1 !!!", __FUNCTION__, w, h);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            // now, take the flex format as NV12.
            // this format should only be known in framework.
            // private_handle in hal should not record this format.
            format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            alignedw = ALIGN_PIXEL_16(w);
            alignedh = ALIGN_PIXEL_4(h);
            size = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
        case HAL_PIXEL_FORMAT_YV12: {
            alignedw = ALIGN_PIXEL_32(w);
            alignedh = ALIGN_PIXEL_4(h);
            int c_stride = (alignedw/2+15)/16*16;
            size = alignedw * alignedh + c_stride * h;
            } break;

        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            alignedw = ALIGN_PIXEL_16(w);
            alignedh = ALIGN_PIXEL_4(h);
            size = alignedw * alignedh * 2;
            break;

        default:
            ALOGE("%s unsupported format:0x%x", __FUNCTION__, format);
            return -EINVAL;
    }

    return allocBuffer(w, h, format, usage, alignedw,
                       alignedh, size, handle, stride);
}

int BufferManager::free(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    return freeBuffer(handle);
}

bool BufferManager::useFSLGralloc(int format, int usage)
{
    bool bUseFSLGralloc = true;

    //RGB format and without video encoder flag go to VIV Gralloc.
    //Only READ/WRITE OFTEN flag go to VIV Gralloc, which support allocate non physical continue memory.
    if (((format >= HAL_PIXEL_FORMAT_RGBA_8888 && format <= HAL_PIXEL_FORMAT_BGRA_8888)
       && !(usage & GRALLOC_USAGE_HW_VIDEO_ENCODER)) || (usage == (GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN))) {
        bUseFSLGralloc = false;
    }

    return bUseFSLGralloc;
}
