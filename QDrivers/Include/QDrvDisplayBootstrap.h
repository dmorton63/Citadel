#pragma once

#include "QCGeometry.h"
#include "QDrvDisplaySurface.h"
#include "QDrvVmwareSVGA.h"

namespace QDrv
{
    namespace Display
    {
        struct cvd_boot_framebuffer_desc_t
        {
            void *front_buffer = nullptr;
            void *back_buffer = nullptr;
            QC::u32 width = 0;
            QC::u32 height = 0;
            QC::u32 pitch = 0;
            QC::u32 bpp = 0;
            cvd_format_t format = CVD_FORMAT_XRGB8888;
            bool frontbuffer_is_mmio = false;
        };

        struct cvd_present_debug_stats_t
        {
            bool accelerated_present = false;
            bool accelerated_rect_copy = false;
            bool hardware_cursor = false;
            VmwareSVGAUpdateStats backend_stats{};
        };

        cvd_result_t cvd_attach_boot_framebuffer(const cvd_boot_framebuffer_desc_t *desc);
        bool cvd_has_accelerated_present();
        bool cvd_has_accelerated_rect_copy();
        cvd_result_t cvd_get_present_debug_stats(cvd_present_debug_stats_t *out_stats);
        void cvd_reset_present_debug_stats();
        cvd_result_t cvd_open_boot_swapchain(const cvd_boot_framebuffer_desc_t *desc,
                             cvd_device_t *out_device,
                             cvd_output_t *out_output,
                             cvd_swapchain_t *out_swapchain,
                             cvd_surface_id_t *out_surface,
                             cvd_sync_value_t *out_ready_value);
        cvd_result_t cvd_rect_copy(cvd_device_t device,
                       cvd_swapchain_t swapchain,
                       cvd_surface_id_t surface,
                       const QC::Rect &src,
                       const QC::Rect &dst);
        cvd_result_t cvd_present_regions(cvd_device_t device,
                                         cvd_swapchain_t swapchain,
                                         cvd_surface_id_t surface,
                                         cvd_sync_value_t wait_value,
                                         const QC::Rect *dirty_rects,
                                         QC::usize dirty_count,
                                         QC::u32 present_flags);
    }
}