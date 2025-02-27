#include "vad.h"
#include <string.h>


AudioVadV2::AudioVadV2(const AudioVadConfigV2& config)
    : config_(config) ,
    start_queue_(config.preStartRecognizeCount + config.startRecognizeCount),
    stop_flag_list_(config_.stopRecognizeCount) 
        {
            // resize stop flag queue
            start_queue_max_ = (config.preStartRecognizeCount + config.startRecognizeCount);
            stop_flag_max_ = config_.stopRecognizeCount;
            
        }
// 辅助方法
bool AudioVadV2::isVadActive( IAudioFrameObserver::AudioFrame& frame)  {
    int voice_threshold = (current_state_ == VAD_STATE_SPEAKING) ? 
        config_.stop_voiceprob : config_.start_voiceprob;
    double rms_threshold = (current_state_ == VAD_STATE_SPEAKING) ?
        config_.stop_rms : config_.start_rms;

    return (frame.far_filed_flag == 1) && 
            (frame.voice_prob > voice_threshold) &&
            (frame.rms > rms_threshold);
}
int AudioVadV2::getStartActiveCount() {
    int count = 0;
    int index = 0;
    for (const auto& item: start_queue_) {
        index++;
        if ( index > config_.preStartRecognizeCount && item.is_activity ) {
            count++;
        }
    }
    return count;
}
int AudioVadV2::getSpeakingActiveCount() {
    int count = 0;
    for (const auto& item : stop_flag_list_) {
        if (item > 0) {
            ++count;
        }
    }
    return count;
}
int AudioVadV2::getFrameSize( IAudioFrameObserver::AudioFrame& frame) {
    return frame.bytesPerSample*frame.samplesPerSec/100*frame.channels;
    }
/**
 * @brief   
 * 
 * @param queue 
 * @param startPos 
 * @return AudioBuffer ,提供 一个释放方法，能释放对象。每次都动态申请
 * vaddaata 做成一个pool的方式来做
 */
AudioBuffer AudioVadV2::getAudioBuffer( FixedSizeQueue<VadDataV2>& queue, int startPos){
    int size = queue.size();
    int pack_num = size - startPos;
    int pack_len = getFrameSize(queue.front().audio_frame);
    // allocate mem for audio buffer
    AudioBuffer buffer(pack_num*pack_len);
    int8_t* ptrdata = reinterpret_cast<int8_t*>(buffer.data());
    int index = 0;
    for (const auto& data : queue)
    {
        index ++;
        if (index >= startPos) {
            memcpy (ptrdata , data.audio_frame.buffer, pack_len);
            ptrdata += pack_len;
        }
    }

    return std::move(buffer);
    
}
AudioBuffer AudioVadV2::getAudioBuffer( IAudioFrameObserver::AudioFrame& frame) {
    int pack_len = getFrameSize(frame);
    // allocate mem for audio buffer
    AudioBuffer buffer(pack_len);
    int8_t* ptrdata = reinterpret_cast<int8_t*>(buffer.data());
    memcpy(ptrdata , frame.buffer, pack_len);
    return std::move(buffer);
}
void AudioVadV2::resetStopFlagList() {
    stop_flag_list_.clear();
}
std::shared_ptr<VadResult> AudioVadV2::processStart(IAudioFrameObserver::AudioFrame& frame, bool is_active) {
    VadDataV2 data(frame, is_active);
    start_queue_.push(std::move(data));
    if (start_queue_.size() < start_queue_max_) {
        return std::make_shared<VadResult>(current_state_, AudioBuffer());
    }

    // 计算活动帧比例
    int total = start_queue_.size() - config_.preStartRecognizeCount;
    int active_count = 0;
    double active_percent = 0.0;
    
    // 计算活动帧比例
    active_count = getStartActiveCount();
    active_percent = static_cast<double>(active_count)/total;
    
   

    if (active_percent >= config_.activePercent) {
        // 拼接音频数据
        AudioBuffer result = getAudioBuffer(start_queue_, 0);
        
        start_queue_.clear();
        resetStopFlagList();
      
        current_state_ = VAD_STATE_SPEAKING;
        return std::make_shared<VadResult>(current_state_, std::move(result));
    }
    
    return std::make_shared<VadResult>(current_state_, AudioBuffer());
}

std::shared_ptr<VadResult> AudioVadV2::processSpeaking( IAudioFrameObserver::AudioFrame& frame, bool is_active) {
    // notice: no need to store audio data

    // validity check
    stop_flag_list_.push(is_active?1:0);
    
    AudioBuffer result = getAudioBuffer(frame);
   
    if (stop_flag_list_.size() >= stop_flag_max_) {
        // 计算静音比例
        int inactive_count = 0;
        float inactive_percent = 0.0;
        inactive_count = getSpeakingActiveCount();
        inactive_count = config_.stopRecognizeCount - inactive_count;
        inactive_percent = static_cast<double>(inactive_count)/config_.stopRecognizeCount;

        if (inactive_percent >= config_.inactivePercent) {
            stop_flag_list_.clear();
            start_queue_.clear();

            current_state_ = VAD_STATE_NONSPEAKING;
        }
    }

    return std::make_shared<VadResult>(current_state_, std::move(result));
}



std::shared_ptr<VadResult> AudioVadV2::process( IAudioFrameObserver::AudioFrame& frame) {
    bool is_active = isVadActive(frame);
    

    switch (current_state_) {
        case VAD_STATE_NONSPEAKING: {
             return processStart(frame, is_active);
        }
        
        case VAD_STATE_SPEAKING: {
            return processSpeaking(frame, is_active);
        }
        
        default:
            return std::make_shared<VadResult>(-1, AudioBuffer());
    }
}
AudioVadV2::~AudioVadV2()
{
    start_queue_.clear();
    stop_flag_list_.clear();
}


