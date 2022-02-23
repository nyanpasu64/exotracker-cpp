#include <gsl/span>
#include <cstdint>

namespace spc_export::driver {

extern const gsl::span<const uint8_t> driver;
constexpr uint16_t mainLoopPos = 0x042E;

extern const gsl::span<const uint8_t, 512> spc_header;
extern const gsl::span<const uint8_t, 256> dsp_footer;

}
