#pragma once
class  IAudioFrameObserver {
    public:
    IAudioFrameObserver() {}
    virtual ~IAudioFrameObserver() {}

    // inner interface
struct AudioFrame {
    /**
     * The audio frame type: #AUDIO_FRAME_TYPE.
     */
    int type;
    /**
     * The number of samples per channel in this frame.
     */
    int samplesPerChannel;
    /**
     * The number of bytes per sample: #BYTES_PER_SAMPLE
     */
    int bytesPerSample;
    /**
     * The number of audio channels (data is interleaved, if stereo).
     * - 1: Mono.
     * - 2: Stereo.
     */
    int channels;
    /**
     * The sample rate
     */
    int samplesPerSec;
    /**
     * The data buffer of the audio frame. When the audio frame uses a stereo channel, the data 
     * buffer is interleaved.
     *
     * Buffer data size: buffer = samplesPerChannel × channels × bytesPerSample.
     */
    void* buffer;
    /**
     * The timestamp to render the audio data.
     *
     * You can use this timestamp to restore the order of the captured audio frame, and synchronize 
     * audio and video frames in video scenarios, including scenarios where external video sources 
     * are used.
     */
    int64_t renderTimeMs;
    /**
     * A reserved parameter.
     * 
     * You can use this presentationMs parameter to indicate the presenation milisecond timestamp,
     * this will then filled into audio4 extension part, the remote side could use this pts in av
     * sync process with video frame.
     */
    int avsync_type;
    /**
     * The pts timestamp of this audio frame.
     *
     * This timestamp is used to indicate the origin pts time of the frame, and sync with video frame by
     * the pts time stamp
     */
    int64_t presentationMs;
     /**
     * The number of the audio track.
     */
    int audioTrackNumber;
    /**
     * RTP timestamp of the first sample in the audio frame
     */
    uint32_t rtpTimestamp;

    int far_filed_flag;
    int rms;
    int voice_prob;
    int music_prob;
    int pitch;

    AudioFrame() : type(1),
                   samplesPerChannel(160),
                   bytesPerSample(2),
                   channels(1),
                   samplesPerSec(16000),
                   buffer(NULL),
                   renderTimeMs(0),
                   avsync_type(0),
                   presentationMs(0),
                   audioTrackNumber(0),
                   rtpTimestamp(0),
                   far_filed_flag(0),
                   rms(0),
                   voice_prob(0),
                   music_prob(0),
                   pitch(0) {}
  };
};