#pragma once

#include "QCTypes.h"

namespace QDrv
{
    namespace Display
    {
        struct DeviceOpaque;
        struct OutputOpaque;
        struct SwapchainOpaque;

        using cvd_device_t = DeviceOpaque *;
        using cvd_output_t = OutputOpaque *;
        using cvd_swapchain_t = SwapchainOpaque *;

        using cvd_sync_value_t = QC::u64;
        using cvd_event_cookie_t = QC::u64;

        struct cvd_surface_id_t
        {
            QC::u64 value = 0;

            bool isValid() const { return value != 0; }
        };

        enum cvd_result_t
        {
            CVD_OK = 0,
            CVD_ERR_UNSUPPORTED,
            CVD_ERR_INVALID_ARG,
            CVD_ERR_NO_MEMORY,
            CVD_ERR_BUSY,
            CVD_ERR_TIMEOUT,
            CVD_ERR_DEVICE_LOST,
            CVD_ERR_NOT_READY,
            CVD_ERR_IO,
        };

        struct cvd_rect_t
        {
            QC::i32 x = 0;
            QC::i32 y = 0;
            QC::i32 w = 0;
            QC::i32 h = 0;
        };

        struct cvd_size_t
        {
            QC::u32 width = 0;
            QC::u32 height = 0;
        };

        enum cvd_format_t
        {
            CVD_FORMAT_XRGB8888 = 0,
            CVD_FORMAT_ARGB8888,
            CVD_FORMAT_RGB565,
        };

        struct cvd_device_desc_t
        {
            QC::u32 ordinal = 0;
            QC::u16 pci_vendor_id = 0;
            QC::u16 pci_device_id = 0;
            QC::u32 class_flags = 0;
            char name[64] = {};
        };

        struct cvd_mode_t
        {
            QC::u32 width = 0;
            QC::u32 height = 0;
            QC::u32 refresh_millihz = 0;
        };

        enum
        {
            CVD_SURFACE_SCANOUT_CAPABLE = 1u << 0,
            CVD_SURFACE_CPU_VISIBLE = 1u << 1,
            CVD_SURFACE_CURSOR_CAPABLE = 1u << 2,
            CVD_SURFACE_PROTECTED = 1u << 3,
            CVD_SURFACE_LINEAR = 1u << 4,
            CVD_SURFACE_TILED = 1u << 5,
        };

        struct cvd_surface_desc_t
        {
            cvd_size_t size{};
            cvd_format_t format = CVD_FORMAT_XRGB8888;
            QC::u32 flags = 0;
        };

        enum
        {
            CVD_MAP_READ = 1u << 0,
            CVD_MAP_WRITE = 1u << 1,
            CVD_MAP_WRITE_DISCARD = 1u << 2,
            CVD_MAP_UNSYNCHRONIZED = 1u << 3,
        };

        struct cvd_surface_map_t
        {
            void *ptr = nullptr;
            QC::usize pitch = 0;
            QC::usize size_bytes = 0;
            QC::u32 cache_flags = 0;
        };

        struct cvd_swapchain_desc_t
        {
            cvd_size_t size{};
            cvd_format_t format = CVD_FORMAT_XRGB8888;
            QC::u32 buffer_count = 0;
            QC::u32 flags = 0;
        };

        enum
        {
            CVD_SWAPCHAIN_VSYNC = 1u << 0,
            CVD_SWAPCHAIN_ALLOW_TEAR = 1u << 1,
            CVD_SWAPCHAIN_TRIPLE_BUF = 1u << 2,
        };

        enum
        {
            CVD_PRESENT_VSYNC = 1u << 0,
            CVD_PRESENT_ALLOW_TEAR = 1u << 1,
            CVD_PRESENT_IMMEDIATE = 1u << 2,
            CVD_PRESENT_CURSOR_ONLY = 1u << 3,
        };

        struct cvd_caps_t
        {
            QC::u64 device_flags = 0;
            QC::u64 format_mask = 0;
            QC::u32 max_width = 0;
            QC::u32 max_height = 0;
            QC::u32 max_swapchain_buffers = 0;
        };

        enum
        {
            CVD_CAP_HW_CURSOR = 1ull << 0,
            CVD_CAP_OVERLAY_PLANES = 1ull << 1,
            CVD_CAP_GAMMA_CONTROL = 1ull << 2,
            CVD_CAP_COLORSPACE_SRGB = 1ull << 3,
            CVD_CAP_COLORSPACE_HDR10 = 1ull << 4,
            CVD_CAP_PROTECTED_CONTENT = 1ull << 5,
            CVD_CAP_VIRTUAL_GPU = 1ull << 6,
            CVD_CAP_CPU_VISIBLE_SCANOUT = 1ull << 7,
            CVD_CAP_TIMELINE_SYNC = 1ull << 8,
        };

        struct cvd_output_caps_t
        {
            QC::u64 flags = 0;
            QC::u32 max_hw_cursor_size = 0;
            QC::u32 max_planes = 0;
        };

        enum
        {
            CVD_OUTPUT_CAP_HW_CURSOR = 1ull << 0,
            CVD_OUTPUT_CAP_SCALING = 1ull << 1,
            CVD_OUTPUT_CAP_ROTATION = 1ull << 2,
            CVD_OUTPUT_CAP_HDR = 1ull << 3,
            CVD_OUTPUT_CAP_VBLANK = 1ull << 4,
        };

        enum cvd_event_type_t
        {
            CVD_EVENT_HOTPLUG = 0,
            CVD_EVENT_MODE_CHANGED,
            CVD_EVENT_VBLANK,
            CVD_EVENT_DEVICE_LOST,
        };

        struct cvd_event_t
        {
            cvd_event_type_t type = CVD_EVENT_HOTPLUG;
            cvd_device_t device = nullptr;
            cvd_output_t output = nullptr;
            QC::u64 sequence = 0;
        };
    }
}