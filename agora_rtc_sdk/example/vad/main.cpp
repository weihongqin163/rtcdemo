#include "vad.h"
int main(int argc, char* argv[]) {
    int err = 0;
    AudioVadConfigV2 config;
    config.stop_rms = -60;
    IAudioFrameObserver::AudioFrame frame;
    const int FrameSize = 320; // 
    std::pair<int, AudioBuffer> result;


    AudioVadV2 vad(config);

    //模拟audio frame
    FILE* fp = fopen("./source.pcm", "rb");
    if (fp == NULL) {
        printf("open file error\n");
        err = -1;


        goto $ERROR;
    }
    

    while (1)
    {
        frame.buffer = new char[FrameSize];
        err = fread(frame.buffer, 1, FrameSize, fp);
        if (err <= 0) {
            printf("read file error\n");
            delete []frame.buffer;
            err = -1;
            goto $ERROR;
        }
        // vad process
        result = vad.process(frame);
        // release resource
        memset (frame.buffer, 0, FrameSize);
        delete []frame.buffer;
        frame.buffer = NULL;
    }



   
    printf("Hello World!\n");

    $ERROR:
    if (fp != NULL) {
        fclose(fp);
        fp = NULL;
    }
    return 0;
}
    
