#pragma once

#include "QDrvDisplayTypes.h"

namespace QDrv
{
    namespace Display
    {
        cvd_result_t cvd_enumerate_devices(cvd_device_desc_t *devices,
                                           QC::u32 *inout_count);

        cvd_result_t cvd_open_device(QC::u32 ordinal,
                                     cvd_device_t *out_device);

        void cvd_close_device(cvd_device_t device);

        cvd_result_t cvd_enum_outputs(cvd_device_t device,
                                      cvd_output_t *outputs,
                                      QC::u32 *inout_count);

        cvd_result_t cvd_get_output_modes(cvd_output_t output,
                                          cvd_mode_t *modes,
                                          QC::u32 *inout_count);

        cvd_result_t cvd_set_output_mode(cvd_output_t output,
                                         const cvd_mode_t *mode);

        cvd_result_t cvd_get_caps(cvd_device_t device,
                                  cvd_caps_t *out_caps);

        cvd_result_t cvd_get_output_caps(cvd_output_t output,
                                         cvd_output_caps_t *out_caps);

        cvd_result_t cvd_poll_event(cvd_device_t device,
                                    cvd_event_t *out_event);
    }
}