#include "audio.h"
#include <portaudio.h>

#include <thread>

namespace audio {

//struct AudioThreadHandle {
//    stream: pa::Stream<pa::NonBlocking, pa::Output<Amplitude>>;

//    // impl AudioThreadHandle
//    AudioThreadHandle(
//            pa: &pa::PortAudio,
//            messages: &'static SynthMessageQueue,
//            stop_synth: &'static StopSynthQueue,
//            ) {
//        use crossbeam::channel::bounded;

//        let stereo_nchan: usize = 2;
//        let mono_smp_per_block: usize = 64;

//        let settings = pa.default_output_stream_settings::<Amplitude>(
//                    stereo_nchan as i32,
//                    48000.0,
//                    mono_smp_per_block as u32,
//                    )?;

//        let queue_nblocks = 1;
//        let (sender, receiver) = bounded(queue_nblocks);
//        let mut x = AudioOutput::new(receiver);

//        let mut left_saw = 0.0;
//        let mut right_saw = 0.0;

//        // The type of callback passed in does not appear in the type of the returned stream.
//        let mut stream = pa.open_non_blocking_stream(settings, move |args| x.callback(args))?;

//        stream.start()?;

//        let mut synth = AudioSynth::new(
//                    messages,
//                    stop_synth,
//                    stereo_nchan,
//                    mono_smp_per_block,
//                    sender,
//                    );
//        let synth_thread = thread::Builder::new()
//                .name("synth_thread".to_string())
//                .spawn(move || {
//                           synth.run();
//                       })
//                .unwrap();

//        Ok(AudioThreadHandle {
//               synth_thread,
//               stream,
//           })
//    }
//}


}
