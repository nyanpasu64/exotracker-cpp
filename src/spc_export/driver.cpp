#include "driver.h"
#include "util/constinit.h"

namespace spc_export::driver {
namespace embed {
    static constinit const uint8_t driver[] = {
        #include "driver/main.inc"
    };
    static constinit const uint8_t spc_header[] = {
        #include "driver/SPCBase.inc"
    };
    static constinit const uint8_t dsp_footer[] = {
        #include "driver/SPCDSPBase.inc"
    };
}


const gsl::span<const uint8_t> driver = {embed::driver};
const gsl::span<const uint8_t, 512> spc_header = {embed::spc_header};
const gsl::span<const uint8_t, 256> dsp_footer = {embed::dsp_footer};

}
