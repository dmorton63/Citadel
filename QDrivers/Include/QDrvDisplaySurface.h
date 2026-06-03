#pragma once

#include "QDrvDisplayDevice.h"

namespace QDrv
{
    namespace Display
    {
        cvd_result_t cvd_surface_create(cvd_device_t device,
                                        const cvd_surface_desc_t *desc,
                                        cvd_surface_id_t *out_surface);

        void cvd_surface_destroy(cvd_device_t device,
                                 cvd_surface_id_t surface);

        cvd_result_t cvd_surface_map(cvd_device_t device,
                                     cvd_surface_id_t surface,
                                     QC::u32 map_flags,
                                     cvd_surface_map_t *out_map);

        cvd_result_t cvd_surface_unmap(cvd_device_t device,
                                       cvd_surface_id_t surface);

        cvd_result_t cvd_cursor_set_image(cvd_output_t output,
                                          cvd_surface_id_t surface,
                                          QC::i32 hot_x,
                                          QC::i32 hot_y);

        cvd_result_t cvd_cursor_set_position(cvd_output_t output,
                                             QC::i32 x,
                                             QC::i32 y);

        cvd_result_t cvd_cursor_show(cvd_output_t output,
                                     bool visible);

        cvd_result_t cvd_swapchain_create(cvd_device_t device,
                                          cvd_output_t output,
                                          const cvd_swapchain_desc_t *desc,
                                          cvd_swapchain_t *out_swapchain);

        void cvd_swapchain_destroy(cvd_device_t device,
                                   cvd_swapchain_t swapchain);

        cvd_result_t cvd_swapchain_acquire(cvd_device_t device,
                                           cvd_swapchain_t swapchain,
                                           cvd_surface_id_t *out_surface,
                                           cvd_sync_value_t *out_ready_value);

        cvd_result_t cvd_swapchain_present(cvd_device_t device,
                                           cvd_swapchain_t swapchain,
                                           cvd_surface_id_t surface,
                                           QC::u32 present_flags,
                                           cvd_sync_value_t wait_value);
    }
}