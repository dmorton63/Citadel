#include "QDrvDisplayBootstrap.h"

#include "QCBuiltins.h"
#include "QCMemUtil.h"
#include "QDrvVmwareSVGA.h"

namespace QDrv
{
    namespace Display
    {
        struct DeviceOpaque
        {
        };

        struct OutputOpaque
        {
        };

        struct SwapchainOpaque
        {
        };

        namespace
        {
            constexpr QC::usize kMaxSurfaces = 16;
            bool g_vmware_probe_done = false;
            bool g_vmware_probe_present = false;
            bool g_vmware_probe_cursor = false;

            struct SurfaceRecord
            {
                bool in_use = false;
                cvd_surface_id_t id{};
                cvd_surface_desc_t desc{};
                void *ptr = nullptr;
                QC::usize pitch = 0;
                QC::usize size_bytes = 0;
                bool owned = false;
            };

            struct State
            {
                bool attached = false;
                bool vmware_present = false;
                bool vmware_cursor = false;
                cvd_boot_framebuffer_desc_t framebuffer{};
                DeviceOpaque device_opaque{};
                OutputOpaque output_opaque{};
                SwapchainOpaque swapchain_opaque{};
                cvd_device_desc_t device_desc{};
                cvd_mode_t mode{};
                cvd_surface_id_t swapchain_surface{};
                SurfaceRecord surfaces[kMaxSurfaces]{};
                QC::u64 next_surface_id = 2;
                cvd_sync_value_t next_sync_value = 1;
            };

            State g_state{};

            QC::u32 bytesPerPixel(cvd_format_t format, QC::u32 fallback_bpp)
            {
                switch (format)
                {
                case CVD_FORMAT_RGB565:
                    return 2;
                case CVD_FORMAT_ARGB8888:
                case CVD_FORMAT_XRGB8888:
                    return 4;
                default:
                    return (fallback_bpp == 16) ? 2u : 4u;
                }
            }

            void memcpy_stream64(void *dst, const void *src, QC::usize bytes)
            {
                auto *dst_bytes = static_cast<QC::u8 *>(dst);
                auto *src_bytes = static_cast<const QC::u8 *>(src);

                QC::usize offset = 0;
                for (; offset + sizeof(QC::u64) <= bytes; offset += sizeof(QC::u64))
                {
                    const QC::u64 value = *reinterpret_cast<const QC::u64 *>(src_bytes + offset);
                    asm volatile("movnti %1, %0" : "=m"(*reinterpret_cast<volatile QC::u64 *>(dst_bytes + offset)) : "r"(value));
                }

                for (; offset < bytes; ++offset)
                    dst_bytes[offset] = src_bytes[offset];

                QC::write_barrier();
            }

            void copyRowsToFrontbuffer(void *dst,
                                       const void *src,
                                       QC::u32 row_bytes,
                                       QC::u32 row_count,
                                       QC::u32 dst_pitch,
                                       QC::u32 src_pitch,
                                       bool frontbuffer_is_mmio)
            {
                auto *dst_bytes = static_cast<QC::u8 *>(dst);
                auto *src_bytes = static_cast<const QC::u8 *>(src);

                for (QC::u32 row = 0; row < row_count; ++row)
                {
                    void *row_dst = dst_bytes + static_cast<QC::usize>(row) * dst_pitch;
                    const void *row_src = src_bytes + static_cast<QC::usize>(row) * src_pitch;
                    if (frontbuffer_is_mmio)
                        memcpy(row_dst, row_src, row_bytes);
                    else
                        memcpy_stream64(row_dst, row_src, row_bytes);
                }
            }

            SurfaceRecord *findSurface(cvd_surface_id_t id)
            {
                for (QC::usize i = 0; i < kMaxSurfaces; ++i)
                {
                    if (g_state.surfaces[i].in_use && g_state.surfaces[i].id.value == id.value)
                        return &g_state.surfaces[i];
                }
                return nullptr;
            }

            const SurfaceRecord *findSurfaceConst(cvd_surface_id_t id)
            {
                for (QC::usize i = 0; i < kMaxSurfaces; ++i)
                {
                    if (g_state.surfaces[i].in_use && g_state.surfaces[i].id.value == id.value)
                        return &g_state.surfaces[i];
                }
                return nullptr;
            }

            SurfaceRecord *allocateSurfaceRecord()
            {
                for (QC::usize i = 0; i < kMaxSurfaces; ++i)
                {
                    if (!g_state.surfaces[i].in_use)
                        return &g_state.surfaces[i];
                }
                return nullptr;
            }

            void resetSurfaceRecords()
            {
                for (QC::usize i = 0; i < kMaxSurfaces; ++i)
                {
                    if (g_state.surfaces[i].in_use && g_state.surfaces[i].owned && g_state.surfaces[i].ptr)
                        delete[] static_cast<QC::u8 *>(g_state.surfaces[i].ptr);
                    g_state.surfaces[i] = SurfaceRecord{};
                }
            }

            void configureCapabilities()
            {
                if (!g_vmware_probe_done)
                {
                    auto &svga = QDrv::VmwareSVGA::instance();
                    const bool available = svga.initialize();
                    g_vmware_probe_present = available && (svga.has2D() || svga.initialize2D());
                    g_vmware_probe_cursor = available && svga.hasHardwareCursor();
                    g_vmware_probe_done = true;
                }

                g_state.vmware_present = g_vmware_probe_present;
                g_state.vmware_cursor = g_vmware_probe_cursor;

                g_state.device_desc = cvd_device_desc_t{};
                g_state.device_desc.ordinal = 0;
                const char *name = g_state.vmware_present ? "CitadelBootDisplay+VMwareSVGA" : "CitadelBootDisplay";
                for (QC::usize i = 0; name[i] != '\0' && i + 1 < sizeof(g_state.device_desc.name); ++i)
                    g_state.device_desc.name[i] = name[i];

                g_state.mode = cvd_mode_t{};
                g_state.mode.width = g_state.framebuffer.width;
                g_state.mode.height = g_state.framebuffer.height;
                g_state.mode.refresh_millihz = 60000;
            }

            cvd_caps_t buildDeviceCaps()
            {
                cvd_caps_t caps{};
                caps.max_width = g_state.framebuffer.width;
                caps.max_height = g_state.framebuffer.height;
                caps.max_swapchain_buffers = 2;
                caps.device_flags |= CVD_CAP_CPU_VISIBLE_SCANOUT;
                caps.device_flags |= CVD_CAP_TIMELINE_SYNC;
                if (g_state.vmware_present)
                    caps.device_flags |= CVD_CAP_VIRTUAL_GPU;
                if (g_state.vmware_cursor)
                    caps.device_flags |= CVD_CAP_HW_CURSOR;
                return caps;
            }

            cvd_output_caps_t buildOutputCaps()
            {
                cvd_output_caps_t caps{};
                caps.max_planes = 1;
                if (g_state.vmware_cursor)
                {
                    caps.flags |= CVD_OUTPUT_CAP_HW_CURSOR;
                    caps.max_hw_cursor_size = 64;
                }
                return caps;
            }

            cvd_result_t presentInternal(const QC::Rect *dirty_rects,
                                         QC::usize dirty_count)
            {
                if (!g_state.attached || !g_state.framebuffer.front_buffer || !g_state.framebuffer.back_buffer)
                    return CVD_ERR_NOT_READY;

                const QC::u32 pixel_bytes = bytesPerPixel(g_state.framebuffer.format, g_state.framebuffer.bpp);

                if (!dirty_rects || dirty_count == 0)
                {
                    copyRowsToFrontbuffer(g_state.framebuffer.front_buffer,
                                          g_state.framebuffer.back_buffer,
                                          g_state.framebuffer.pitch,
                                          g_state.framebuffer.height,
                                          g_state.framebuffer.pitch,
                                          g_state.framebuffer.pitch,
                                          g_state.framebuffer.frontbuffer_is_mmio);

                    if (g_state.vmware_present)
                        QDrv::VmwareSVGA::instance().updateRect(0, 0, g_state.framebuffer.width, g_state.framebuffer.height);
                    return CVD_OK;
                }

                QC::Rect clipped[128] = {};
                QC::usize clipped_count = 0;
                for (QC::usize i = 0; i < dirty_count && clipped_count < 128; ++i)
                {
                    QC::i32 x0 = dirty_rects[i].x;
                    QC::i32 y0 = dirty_rects[i].y;
                    QC::i32 x1 = dirty_rects[i].x + static_cast<QC::i32>(dirty_rects[i].width);
                    QC::i32 y1 = dirty_rects[i].y + static_cast<QC::i32>(dirty_rects[i].height);

                    if (x1 <= 0 || y1 <= 0)
                        continue;
                    if (x0 >= static_cast<QC::i32>(g_state.framebuffer.width) || y0 >= static_cast<QC::i32>(g_state.framebuffer.height))
                        continue;

                    if (x0 < 0)
                        x0 = 0;
                    if (y0 < 0)
                        y0 = 0;
                    if (x1 > static_cast<QC::i32>(g_state.framebuffer.width))
                        x1 = static_cast<QC::i32>(g_state.framebuffer.width);
                    if (y1 > static_cast<QC::i32>(g_state.framebuffer.height))
                        y1 = static_cast<QC::i32>(g_state.framebuffer.height);

                    const QC::u32 width = static_cast<QC::u32>(x1 - x0);
                    const QC::u32 height = static_cast<QC::u32>(y1 - y0);
                    if (width == 0 || height == 0)
                        continue;

                    clipped[clipped_count++] = QC::Rect{x0, y0, width, height};

                    void *dst = static_cast<QC::u8 *>(g_state.framebuffer.front_buffer) +
                                static_cast<QC::usize>(y0) * g_state.framebuffer.pitch +
                                static_cast<QC::usize>(x0) * pixel_bytes;
                    const void *src = static_cast<const QC::u8 *>(g_state.framebuffer.back_buffer) +
                                      static_cast<QC::usize>(y0) * g_state.framebuffer.pitch +
                                      static_cast<QC::usize>(x0) * pixel_bytes;
                    copyRowsToFrontbuffer(dst,
                                          src,
                                          width * pixel_bytes,
                                          height,
                                          g_state.framebuffer.pitch,
                                          g_state.framebuffer.pitch,
                                          g_state.framebuffer.frontbuffer_is_mmio);
                }

                if (g_state.vmware_present && clipped_count > 0)
                {
                    if (clipped_count == 1)
                    {
                        const QC::Rect &rect = clipped[0];
                        QDrv::VmwareSVGA::instance().updateRect(static_cast<QC::u32>(rect.x), static_cast<QC::u32>(rect.y), rect.width, rect.height);
                    }
                    else
                    {
                        QDrv::VmwareSVGA::instance().updateRects(clipped, clipped_count);
                    }
                }

                return CVD_OK;
            }
        }

        cvd_result_t cvd_attach_boot_framebuffer(const cvd_boot_framebuffer_desc_t *desc)
        {
            if (!desc || !desc->front_buffer || !desc->back_buffer || desc->width == 0 || desc->height == 0 || desc->pitch == 0)
                return CVD_ERR_INVALID_ARG;

            resetSurfaceRecords();
            g_state = State{};
            g_state.attached = true;
            g_state.framebuffer = *desc;
            configureCapabilities();

            SurfaceRecord &record = g_state.surfaces[0];
            record.in_use = true;
            record.id.value = 1;
            record.desc.size.width = desc->width;
            record.desc.size.height = desc->height;
            record.desc.format = desc->format;
            record.desc.flags = CVD_SURFACE_SCANOUT_CAPABLE | CVD_SURFACE_CPU_VISIBLE | CVD_SURFACE_LINEAR;
            record.ptr = desc->back_buffer;
            record.pitch = desc->pitch;
            record.size_bytes = static_cast<QC::usize>(desc->pitch) * desc->height;
            record.owned = false;
            g_state.swapchain_surface = record.id;
            return CVD_OK;
        }

        bool cvd_has_accelerated_present()
        {
            if (!g_vmware_probe_done)
                configureCapabilities();
            return g_vmware_probe_present;
        }

        bool cvd_has_accelerated_rect_copy()
        {
            return cvd_has_accelerated_present();
        }

        cvd_result_t cvd_get_present_debug_stats(cvd_present_debug_stats_t *out_stats)
        {
            if (!out_stats)
                return CVD_ERR_INVALID_ARG;

            out_stats->accelerated_present = cvd_has_accelerated_present();
            out_stats->accelerated_rect_copy = cvd_has_accelerated_rect_copy();
            out_stats->hardware_cursor = g_vmware_probe_done ? g_vmware_probe_cursor : false;
            out_stats->backend_stats = QDrv::VmwareSVGA::instance().updateStats();
            return CVD_OK;
        }

        void cvd_reset_present_debug_stats()
        {
            QDrv::VmwareSVGA::instance().resetUpdateStats();
        }

        cvd_result_t cvd_open_boot_swapchain(const cvd_boot_framebuffer_desc_t *desc,
                                             cvd_device_t *out_device,
                                             cvd_output_t *out_output,
                                             cvd_swapchain_t *out_swapchain,
                                             cvd_surface_id_t *out_surface,
                                             cvd_sync_value_t *out_ready_value)
        {
            if (!desc || !out_device || !out_output || !out_swapchain || !out_surface || !out_ready_value)
                return CVD_ERR_INVALID_ARG;

            cvd_result_t result = cvd_attach_boot_framebuffer(desc);
            if (result != CVD_OK)
                return result;

            result = cvd_open_device(0, out_device);
            if (result != CVD_OK)
                return result;

            QC::u32 outputCount = 1;
            result = cvd_enum_outputs(*out_device, out_output, &outputCount);
            if (result != CVD_OK || outputCount == 0)
                return (result == CVD_OK) ? CVD_ERR_NOT_READY : result;

            cvd_swapchain_desc_t swapchainDesc{};
            swapchainDesc.size.width = desc->width;
            swapchainDesc.size.height = desc->height;
            swapchainDesc.format = desc->format;
            swapchainDesc.buffer_count = 2;
            swapchainDesc.flags = CVD_SWAPCHAIN_VSYNC;
            result = cvd_swapchain_create(*out_device, *out_output, &swapchainDesc, out_swapchain);
            if (result != CVD_OK)
                return result;

            return cvd_swapchain_acquire(*out_device,
                                         *out_swapchain,
                                         out_surface,
                                         out_ready_value);
        }

        cvd_result_t cvd_present_regions(cvd_device_t device,
                                         cvd_swapchain_t swapchain,
                                         cvd_surface_id_t surface,
                                         cvd_sync_value_t wait_value,
                                         const QC::Rect *dirty_rects,
                                         QC::usize dirty_count,
                                         QC::u32 present_flags)
        {
            (void)wait_value;
            (void)present_flags;
            if (!device || !swapchain || !surface.isValid())
                return CVD_ERR_INVALID_ARG;
            if (device != &g_state.device_opaque || swapchain != &g_state.swapchain_opaque || surface.value != g_state.swapchain_surface.value)
                return CVD_ERR_INVALID_ARG;
            return presentInternal(dirty_rects, dirty_count);
        }

        cvd_result_t cvd_rect_copy(cvd_device_t device,
                                   cvd_swapchain_t swapchain,
                                   cvd_surface_id_t surface,
                                   const QC::Rect &src,
                                   const QC::Rect &dst)
        {
            if (!device || !swapchain || !surface.isValid())
                return CVD_ERR_INVALID_ARG;
            if (device != &g_state.device_opaque || swapchain != &g_state.swapchain_opaque || surface.value != g_state.swapchain_surface.value)
                return CVD_ERR_INVALID_ARG;
            if (!g_state.vmware_present)
                return CVD_ERR_UNSUPPORTED;
            if (src.width == 0 || src.height == 0 || dst.width != src.width || dst.height != src.height)
                return CVD_ERR_INVALID_ARG;

            QDrv::VmwareSVGA::instance().rectCopy(static_cast<QC::u32>(src.x),
                                                  static_cast<QC::u32>(src.y),
                                                  static_cast<QC::u32>(dst.x),
                                                  static_cast<QC::u32>(dst.y),
                                                  src.width,
                                                  src.height);
            return CVD_OK;
        }

        cvd_result_t cvd_enumerate_devices(cvd_device_desc_t *devices,
                                           QC::u32 *inout_count)
        {
            if (!inout_count)
                return CVD_ERR_INVALID_ARG;

            const QC::u32 available = g_state.attached ? 1u : 0u;
            if (!devices || *inout_count == 0)
            {
                *inout_count = available;
                return CVD_OK;
            }

            if (!g_state.attached)
            {
                *inout_count = 0;
                return CVD_OK;
            }

            devices[0] = g_state.device_desc;
            *inout_count = 1;
            return CVD_OK;
        }

        cvd_result_t cvd_open_device(QC::u32 ordinal,
                                     cvd_device_t *out_device)
        {
            if (!out_device)
                return CVD_ERR_INVALID_ARG;
            if (!g_state.attached || ordinal != 0)
                return CVD_ERR_NOT_READY;
            *out_device = &g_state.device_opaque;
            return CVD_OK;
        }

        void cvd_close_device(cvd_device_t device)
        {
            (void)device;
        }

        cvd_result_t cvd_enum_outputs(cvd_device_t device,
                                      cvd_output_t *outputs,
                                      QC::u32 *inout_count)
        {
            if (!inout_count || device != &g_state.device_opaque)
                return CVD_ERR_INVALID_ARG;

            if (!outputs || *inout_count == 0)
            {
                *inout_count = g_state.attached ? 1u : 0u;
                return CVD_OK;
            }

            outputs[0] = &g_state.output_opaque;
            *inout_count = 1;
            return CVD_OK;
        }

        cvd_result_t cvd_get_output_modes(cvd_output_t output,
                                          cvd_mode_t *modes,
                                          QC::u32 *inout_count)
        {
            if (!inout_count || output != &g_state.output_opaque)
                return CVD_ERR_INVALID_ARG;

            if (!modes || *inout_count == 0)
            {
                *inout_count = 1;
                return CVD_OK;
            }

            modes[0] = g_state.mode;
            *inout_count = 1;
            return CVD_OK;
        }

        cvd_result_t cvd_set_output_mode(cvd_output_t output,
                                         const cvd_mode_t *mode)
        {
            (void)output;
            (void)mode;
            return CVD_ERR_UNSUPPORTED;
        }

        cvd_result_t cvd_get_caps(cvd_device_t device,
                                  cvd_caps_t *out_caps)
        {
            if (!out_caps || device != &g_state.device_opaque)
                return CVD_ERR_INVALID_ARG;
            *out_caps = buildDeviceCaps();
            return CVD_OK;
        }

        cvd_result_t cvd_get_output_caps(cvd_output_t output,
                                         cvd_output_caps_t *out_caps)
        {
            if (!out_caps || output != &g_state.output_opaque)
                return CVD_ERR_INVALID_ARG;
            *out_caps = buildOutputCaps();
            return CVD_OK;
        }

        cvd_result_t cvd_poll_event(cvd_device_t device,
                                    cvd_event_t *out_event)
        {
            (void)device;
            (void)out_event;
            return CVD_ERR_NOT_READY;
        }

        cvd_result_t cvd_surface_create(cvd_device_t device,
                                        const cvd_surface_desc_t *desc,
                                        cvd_surface_id_t *out_surface)
        {
            if (!desc || !out_surface || device != &g_state.device_opaque)
                return CVD_ERR_INVALID_ARG;

            SurfaceRecord *record = allocateSurfaceRecord();
            if (!record)
                return CVD_ERR_NO_MEMORY;

            const QC::u32 pixel_bytes = bytesPerPixel(desc->format, g_state.framebuffer.bpp);
            const QC::usize pitch = static_cast<QC::usize>(desc->size.width) * pixel_bytes;
            const QC::usize size_bytes = pitch * desc->size.height;
            QC::u8 *memory = new QC::u8[size_bytes];
            if (!memory)
                return CVD_ERR_NO_MEMORY;
            memset(memory, 0, size_bytes);

            record->in_use = true;
            record->id.value = g_state.next_surface_id++;
            record->desc = *desc;
            record->ptr = memory;
            record->pitch = pitch;
            record->size_bytes = size_bytes;
            record->owned = true;
            *out_surface = record->id;
            return CVD_OK;
        }

        void cvd_surface_destroy(cvd_device_t device,
                                 cvd_surface_id_t surface)
        {
            (void)device;
            SurfaceRecord *record = findSurface(surface);
            if (!record || surface.value == g_state.swapchain_surface.value)
                return;
            if (record->owned && record->ptr)
                delete[] static_cast<QC::u8 *>(record->ptr);
            *record = SurfaceRecord{};
        }

        cvd_result_t cvd_surface_map(cvd_device_t device,
                                     cvd_surface_id_t surface,
                                     QC::u32 map_flags,
                                     cvd_surface_map_t *out_map)
        {
            (void)map_flags;
            if (!out_map || device != &g_state.device_opaque)
                return CVD_ERR_INVALID_ARG;
            const SurfaceRecord *record = findSurfaceConst(surface);
            if (!record)
                return CVD_ERR_INVALID_ARG;

            out_map->ptr = record->ptr;
            out_map->pitch = record->pitch;
            out_map->size_bytes = record->size_bytes;
            out_map->cache_flags = 0;
            return CVD_OK;
        }

        cvd_result_t cvd_surface_unmap(cvd_device_t device,
                                       cvd_surface_id_t surface)
        {
            (void)device;
            (void)surface;
            return CVD_OK;
        }

        cvd_result_t cvd_cursor_set_image(cvd_output_t output,
                                          cvd_surface_id_t surface,
                                          QC::i32 hot_x,
                                          QC::i32 hot_y)
        {
            if (output != &g_state.output_opaque || !g_state.vmware_cursor)
                return CVD_ERR_UNSUPPORTED;

            const SurfaceRecord *record = findSurfaceConst(surface);
            if (!record || !record->ptr)
                return CVD_ERR_INVALID_ARG;

            QDrv::VmwareSVGA::instance().setCursorImage(static_cast<const QC::u32 *>(record->ptr),
                                                       static_cast<QC::u16>(record->desc.size.width),
                                                       static_cast<QC::u16>(record->desc.size.height),
                                                       static_cast<QC::u16>(hot_x),
                                                       static_cast<QC::u16>(hot_y));
            return CVD_OK;
        }

        cvd_result_t cvd_cursor_set_position(cvd_output_t output,
                                             QC::i32 x,
                                             QC::i32 y)
        {
            if (output != &g_state.output_opaque || !g_state.vmware_cursor)
                return CVD_ERR_UNSUPPORTED;
            QDrv::VmwareSVGA::instance().setCursorPosition(static_cast<QC::u16>(x), static_cast<QC::u16>(y));
            return CVD_OK;
        }

        cvd_result_t cvd_cursor_show(cvd_output_t output,
                                     bool visible)
        {
            if (output != &g_state.output_opaque || !g_state.vmware_cursor)
                return CVD_ERR_UNSUPPORTED;
            QDrv::VmwareSVGA::instance().setCursorVisible(visible);
            return CVD_OK;
        }

        cvd_result_t cvd_swapchain_create(cvd_device_t device,
                                          cvd_output_t output,
                                          const cvd_swapchain_desc_t *desc,
                                          cvd_swapchain_t *out_swapchain)
        {
            (void)desc;
            if (!out_swapchain || device != &g_state.device_opaque || output != &g_state.output_opaque)
                return CVD_ERR_INVALID_ARG;
            *out_swapchain = &g_state.swapchain_opaque;
            return CVD_OK;
        }

        void cvd_swapchain_destroy(cvd_device_t device,
                                   cvd_swapchain_t swapchain)
        {
            (void)device;
            (void)swapchain;
        }

        cvd_result_t cvd_swapchain_acquire(cvd_device_t device,
                                           cvd_swapchain_t swapchain,
                                           cvd_surface_id_t *out_surface,
                                           cvd_sync_value_t *out_ready_value)
        {
            if (!out_surface || !out_ready_value || device != &g_state.device_opaque || swapchain != &g_state.swapchain_opaque)
                return CVD_ERR_INVALID_ARG;
            *out_surface = g_state.swapchain_surface;
            *out_ready_value = g_state.next_sync_value++;
            return CVD_OK;
        }

        cvd_result_t cvd_swapchain_present(cvd_device_t device,
                                           cvd_swapchain_t swapchain,
                                           cvd_surface_id_t surface,
                                           QC::u32 present_flags,
                                           cvd_sync_value_t wait_value)
        {
            return cvd_present_regions(device, swapchain, surface, wait_value, nullptr, 0, present_flags);
        }
    }
}