#include <sndfile.hh>
#include <Blip_Buffer/Blip_Buffer.h>
#include <fmt/core.h>

int main(int argc, char ** argv) {
    if (argc != 3) {
        fmt::print(stderr, "Error! Usage: blip-resampler in.wav [out.wav]\n\n");
        return 1;
    }

    auto input = SndfileHandle(argv[1]);
    auto output = SndfileHandle(argv[2], SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_16, 1, 44100);

    auto buf = Blip_Buffer(44100, input.samplerate());
    buf.bass_freq(0);

    auto synth = Blip_Synth<blip_high_quality>(1, 65536);
//    synth.treble_eq(blip_eq_t(-13., 20000, 44100, 22050));
    synth.treble_eq(blip_eq_t(0, 0, 44100, 0));

    int chunk_size = input.samplerate() / 10;

    // TODO test blip buffer stereo
    if (input.channels() != 1) {
        throw "up";
    }

    std::vector<short> in_data;
    std::vector<blip_amplitude_t> out_data;
    in_data.resize(chunk_size);
    out_data.reserve(chunk_size);

    while (true) {
        auto nsamp_read = input.read(in_data.data(), chunk_size);
        if (nsamp_read) {
            for (int i = 0; i < nsamp_read; i++) {
                synth.update(i, in_data[i], &buf);
            }
            buf.end_frame(nsamp_read);
        } else {
            synth.update(0, 0, &buf);
            buf.offset_ += BLIP_BUFFER_ACCURACY << BLIP_PHASE_BITS;
        }

        out_data.resize(buf.samples_avail());
        auto nsamp_out = buf.read_samples(out_data.data(), out_data.size());
        output.write(out_data.data(), nsamp_out);

        if (!nsamp_read) break;
    }
}
