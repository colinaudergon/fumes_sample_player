
#include "AudioPlayer.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>

#include <cstdio>
int app::audio::AudioPlayer::Init(AudioPlayerConfiguration &configuration)
{
    n_channels_ = configuration.n_channels;
    playback_speed_ = configuration.playback_speed;
    return 0;
}

int app::audio::AudioPlayer::LoadFile(const char *path)
{
    if (path == nullptr)
    {
        return static_cast<int>(FsResult::kInvalidParameter);
    }

    is_playing_ = false;

    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
    }

    FsFile *file = nullptr;
    FsResult open_result = file_system_.Open(&file, path, FsOpenMode::kRead);
    if (open_result != FsResult::kOk)
    {
        return static_cast<int>(open_result);
    }

    uint8_t header_buffer[kWavHeaderSize];
    size_t bytes_read = 0;
    FsResult read_result = file_system_.Read(file, header_buffer, sizeof(header_buffer), &bytes_read);
    if (read_result != FsResult::kOk || bytes_read != sizeof(header_buffer))
    {
        file_system_.Close(file);
        return (read_result != FsResult::kOk) ? static_cast<int>(read_result) : static_cast<int>(FsResult::kNoFile);
    }

    int header_result = wav_file_handler_.ReadHeader(header_buffer, bytes_read);
    if (header_result != 0)
    {
        file_system_.Close(file);
        return header_result;
    }

    int data_chunk_result = wav_file_handler_.FindDataChunk(header_buffer, bytes_read);
    if (data_chunk_result != 0)
    {
        file_system_.Close(file);
        return data_chunk_result;
    }

    // Keep the file open (positioned right after the header, at the start of the data chunk)
    // so Read() can stream sample data from it.
    file_ = file;

    std::strncpy(loaded_file_path_, path, kMaxFilePathLength - 1);
    loaded_file_path_[kMaxFilePathLength - 1] = '\0';
    bytes_per_sample_ = static_cast<size_t>(wav_file_handler_.GetBitsPerSample()) / 8;
    frame_bytes_ = bytes_per_sample_ * wav_file_handler_.GetNumChannels();

    if (frame_bytes_ == 0)
    {
        file_system_.Close(file);
        return -1;
    }

    RestartGlitchEngine();

    // Seed current_frame_index_/file_read_index_ from start_marker_, honoring is_reverse_.
    SeekStartMarker();

    return 0;
}

int app::audio::AudioPlayer::Read(wav::audio_frame_t &output, size_t n_frames)
{
    if (!IsOutpuBufferValid(output))
    {
        return -1;
    }

    n_frames_out_ = static_cast<size_t>(static_cast<float>(n_frames) * playback_speed_);

    // Clamp to the pre-resample scratch buffers' capacity; guards against overflow if a caller
    // requests more than kMaxOutputFrames.
    if (n_frames_out_ > kMaxSourceFrames)
    {
        n_frames_out_ = kMaxSourceFrames;
    }

    if (IsNewAudioDataRequired())
    {
        is_playing_ = false;
        // Nothing left to stream: fill the whole buffer with silence.
        FillWithZeros(output, n_frames);
        return static_cast<int>(n_frames);
    }

    SeekStartChunk();
    size_t total_frames_read = FetchData();
    TrackCurrentFrameIndex(total_frames_read);

    if (WrapMarker())
    {
        // Continue filling this same output buffer across the wrap, now bounded by the
        // opposite marker.
        n_frames_out_ -= total_frames_read;
        SeekStartChunk();
        size_t wrapped_frames_read = FetchData(total_frames_read);
        TrackCurrentFrameIndex(wrapped_frames_read);
        total_frames_read += wrapped_frames_read;
    }

    if (!HandleEndOfFile(output, n_frames, total_frames_read))
    {
        // Stretches/compresses total_frames_read source frames into exactly n_frames output
        // frames, implementing playback_speed_.
        wav::audio_frame_t source_frame{time_adjust_source_l_, time_adjust_source_r_, total_frames_read};
        AdjustTime(source_frame, output, n_frames);

        if (is_reverse_)
        {
            ReverseFrames(output, n_frames);
        }

        glitch_engine_.ProcessFrame(output, output, n_frames, GetPlayheadFrame());

        if (glitch_enable_ && glitch_engine_.IsPitchModRequired())
        {
            playback_speed_ = glitch_engine_.GetNextPlaybackSpeed();
        }
        else if (glitch_enable_)
        {
            playback_speed_ = glitch_engine_.GetPreviousPlaybackSpeed();
        }
    }
    return static_cast<int>(output.n_frames);
}

void app::audio::AudioPlayer::SetReverse(bool enable)
{
    const bool should_seek = enable && !is_reverse_ && current_frame_index_ == 0;
    is_reverse_ = enable;
    if (should_seek)
    {
        SeekStartMarker();
    }
}

void app::audio::AudioPlayer::SetStartMarker(float relative_position)
{
    if (file_ == nullptr)
    {
        // There is no file loaded, so there is no start nor stop marker available
        return;
    }

    SetMarker(relative_position, true);
}

void app::audio::AudioPlayer::SetStopMarker(float relative_position)
{
    if (file_ == nullptr)
    {
        // There is no file loaded, so there is no start nor stop marker available
        return;
    }
    SetMarker(relative_position, false);
}

size_t app::audio::AudioPlayer::GetStartMarker()
{
    return start_marker_;
}

size_t app::audio::AudioPlayer::GetStopMarker()
{
    return stop_marker_;
}

uint32_t app::audio::AudioPlayer::GetStartMarkerMs()
{
    const int sample_rate = wav_file_handler_.GetSampleRate();
    if (sample_rate == 0)
    {
        return 0;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(start_marker_) * kMillisMultiplier) / static_cast<uint64_t>(sample_rate));
}

uint32_t app::audio::AudioPlayer::GetStopMarkerMs()
{
    const int sample_rate = wav_file_handler_.GetSampleRate();
    if (sample_rate == 0)
    {
        return 0;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(stop_marker_) * kMillisMultiplier) / static_cast<uint64_t>(sample_rate));
}

void app::audio::AudioPlayer::FillWithZeros(wav::audio_frame_t &output, size_t n_frames)
{
    if (output.audio_l != nullptr)
    {
        std::fill(output.audio_l, output.audio_l + n_frames, 0.0f);
    }
    if (output.audio_r != nullptr)
    {
        std::fill(output.audio_r, output.audio_r + n_frames, 0.0f);
    }
    output.n_frames = n_frames;
}

void app::audio::AudioPlayer::AdjustTime(wav::audio_frame_t &input, wav::audio_frame_t &output, size_t n_frames)
{
    // n_frames is the desired output frame count; the source frame count is input.n_frames.
    size_t num_src_samples = input.n_frames;

    if (input.audio_l == nullptr || input.audio_r == nullptr ||
        output.audio_l == nullptr || output.audio_r == nullptr ||
        num_src_samples == 0 || n_frames == 0)
    {
        output.n_frames = 0;
        return;
    }

    for (size_t out_sample = 0; out_sample < n_frames; out_sample++)
    {
        // Spreads output samples evenly across the source range [0, num_src_samples]. With a
        // single output frame there's no span to spread across, so sample the start.
        float percent = (n_frames > 1)
                            ? static_cast<float>(out_sample) / static_cast<float>(n_frames - 1)
                            : 0.0f;

        float src_sample_float = static_cast<float>(num_src_samples) * percent;

        // ApplyInterpolation() clamps src_sample_float internally, so percent == 1.0 is safe.
        output.audio_l[out_sample] = ApplyInterpolation(input.audio_l, src_sample_float, num_src_samples);
        output.audio_r[out_sample] = ApplyInterpolation(input.audio_r, src_sample_float, num_src_samples);
    }

    output.n_frames = n_frames;
}

void app::audio::AudioPlayer::TrackCurrentFrameIndex(size_t total_frames_read)
{
    if (is_freezed_)
    {
        return;
    }

    if (is_reverse_)
    {
        current_frame_index_ -= total_frames_read;
    }
    else
    {
        current_frame_index_ += total_frames_read;
    }
}

void app::audio::AudioPlayer::TrackFileReadIndex(size_t bytes_read)
{
    if (!is_freezed_)
    {
        file_read_index_ += bytes_read;
    }
}

void app::audio::AudioPlayer::SeekDataChunkStart()
{
    file_system_.Lseek(file_, kWavHeaderSize);
}

void app::audio::AudioPlayer::SeekStartChunk()
{
    if (glitch_enable_ && glitch_engine_.IsGlitchFetchRequired())
    {
        // Instead of reading the next contiguous chunk, jump to a randomly-picked previously
        // detected beat position, producing the glitch/beat-repeat stutter effect.
        current_frame_index_ = std::min(glitch_engine_.GlitchFetchFramePosition(), GetTotalFrames());
    }

    // Read up to n_frames_out raw source frames (at the file's native sample rate, i.e. before
    // any speed adjustment) into the pre-resample scratch buffers.
    frames_remaining_ = n_frames_out_;

    if (is_reverse_)
    {
        // Reverse playback must not read past stop_marker_ (0 means "no stop marker set", i.e.
        // play down to frame 0). If wraparound is enabled and hasn't happened yet, the readable
        // range extends down to frame 0 first; once wrapped, it's bounded by stop_marker_.
        const size_t total_frames = GetTotalFrames();
        const size_t effective_stop = (IsWrapEnabled() && !has_wrapped_) ? 0 : std::min(stop_marker_, total_frames);
        const size_t frames_until_stop = (current_frame_index_ > effective_stop) ? (current_frame_index_ - effective_stop) : 0;
        frames_remaining_ = std::min(frames_remaining_, frames_until_stop);
        n_frames_out_ = frames_remaining_;

        size_t start_frame = current_frame_index_ - frames_remaining_;
        size_t offset = kWavHeaderSize + start_frame * frame_bytes_;
        file_system_.Lseek(file_, offset);
    }
    else
    {
        // Forward playback must not read past stop_marker_. A stop marker smaller than
        // start_marker_ means the playable range wraps past the end of the file (see
        // IsWrapEnabled()/WrapMarker()); 0 means "no stop marker set", i.e. play to the actual
        // end of the file.
        const size_t total_frames = GetTotalFrames();
        const size_t effective_stop = (IsWrapEnabled() && !has_wrapped_)
                                          ? total_frames
                                          : ((stop_marker_ != 0) ? std::min(stop_marker_, total_frames) : total_frames);
        const size_t frames_until_stop = (current_frame_index_ < effective_stop) ? (effective_stop - current_frame_index_) : 0;
        frames_remaining_ = std::min(frames_remaining_, frames_until_stop);

        // Re-sync the file cursor to current_frame_index_: needed after any marker-seed jump
        // (LoadFile()/looping wrap in HandleEndOfFile()), since those only update AudioPlayer's
        // own bookkeeping, not the underlying file's read position.
        size_t offset = kWavHeaderSize + current_frame_index_ * frame_bytes_;
        file_system_.Lseek(file_, offset);
    }

    if (is_freezed_)
    {
        size_t offset = kWavHeaderSize + current_frame_index_ * frame_bytes_;
        file_system_.Lseek(file_, offset);
    }
}

void app::audio::AudioPlayer::SeekStartMarker()
{
    const size_t total_frames = GetTotalFrames();
    const size_t clamped_start_marker = std::min(start_marker_, total_frames);

    if (is_reverse_)
    {
        // start_marker_ == 0 means "unset": reverse then starts at the true end of the file,
        // mirroring how stop_marker_ == 0 means "no stop" (play to true end) in forward mode.
        current_frame_index_ = (start_marker_ == 0) ? total_frames : std::min(clamped_start_marker + 1, total_frames);
    }
    else
    {
        current_frame_index_ = clamped_start_marker;
    }
    file_read_index_ = kWavHeaderSize + current_frame_index_ * frame_bytes_;
    has_wrapped_ = false;
}

size_t app::audio::AudioPlayer::GetTotalFrames()
{
    return (frame_bytes_ != 0) ? static_cast<size_t>(wav_file_handler_.GetDataSize()) / frame_bytes_ : 0;
}

bool app::audio::AudioPlayer::IsWrapEnabled()
{
    // Forward: a stop marker before the start marker means the playable range wraps past the
    // end of the file. Reverse: a stop marker after the start marker means it wraps past the
    // start of the file. stop_marker_ == 0 in the forward case means "unset", never wraps.
    return is_reverse_ ? (stop_marker_ > start_marker_) : (stop_marker_ != 0 && stop_marker_ < start_marker_);
}

bool app::audio::AudioPlayer::WrapMarker()
{
    if (has_wrapped_ || !IsWrapEnabled())
    {
        return false;
    }

    const size_t total_frames = GetTotalFrames();
    if (is_reverse_)
    {
        if (current_frame_index_ > 0)
        {
            return false; // haven't reached frame 0 yet
        }
        current_frame_index_ = total_frames;
    }
    else
    {
        if (current_frame_index_ < total_frames)
        {
            return false; // haven't reached the physical end of the file yet
        }
        current_frame_index_ = 0;
    }

    has_wrapped_ = true;
    file_read_index_ = kWavHeaderSize + current_frame_index_ * frame_bytes_;
    return true;
}

size_t app::audio::AudioPlayer::FetchData(size_t buffer_offset)
{
    size_t total_frames_read = 0;
    while (frames_remaining_ > 0)
    {
        const size_t chunk_frames = std::min(frames_remaining_, kMaxReadFrames);
        const size_t bytes_to_read = chunk_frames * frame_bytes_;

        size_t bytes_read = 0;
        FsResult read_result = file_system_.Read(file_, read_scratch_buffer_, bytes_to_read, &bytes_read);
        if (read_result != FsResult::kOk)
        {
            break;
        }

        TrackFileReadIndex(bytes_read);

        // Convert + copy happen in the same pass: WavFileHandler::ReadData() writes straight
        // from the raw byte buffer into these scratch slices, no intermediate float buffer.
        wav::audio_frame_t chunk_output{
            time_adjust_source_l_ + buffer_offset + total_frames_read,
            time_adjust_source_r_ + buffer_offset + total_frames_read,
            0};

        int converted = wav_file_handler_.ReadData(read_scratch_buffer_, bytes_read, chunk_output, chunk_frames);
        if (converted <= 0)
        {
            break;
        }

        total_frames_read += static_cast<size_t>(converted);
        frames_remaining_ -= chunk_frames;

        if (static_cast<size_t>(converted) < chunk_frames)
        {
            // Short read: end of data chunk reached partway through this buffer.
            break;
        }
    }
    return total_frames_read;
}

bool app::audio::AudioPlayer::HandleEndOfFile(wav::audio_frame_t &output, size_t n_frames, size_t total_frames_read)
{
    if (total_frames_read == 0 && !is_looping_)
    {
        // Hit EOF immediately: nothing to resample, output is silence.
        is_playing_ = false;
        FillWithZeros(output, n_frames);
        return true;
    }
    if (total_frames_read == 0 && is_looping_)
    {
        SeekStartMarker();
        SeekStartChunk();
        FillWithZeros(output, n_frames);
        return true;
    }
    return false;
}

float app::audio::AudioPlayer::ApplyInterpolation(float *input, float single_value, size_t n_frames)
{
    if (input == nullptr || n_frames == 0)
    {
        return 0.0f;
    }

    size_t sample = static_cast<size_t>(single_value);
    if (sample >= n_frames)
    {
        sample = n_frames - 1;
    }

    float fraction = single_value - std::floorf(single_value);

    size_t next_sample = sample + 1;
    if (next_sample >= n_frames)
    {
        // No next sample available (last frame in the buffer): fall back to the current sample
        // so we never read/interpolate past the end of the buffer.
        next_sample = sample;
    }

    return input[sample] + fraction * (input[next_sample] - input[sample]);
}

void app::audio::AudioPlayer::ReverseFrames(wav::audio_frame_t &input, size_t n_frames)
{
    for (size_t i = 0, j = n_frames - 1; i < j; ++i, --j)
    {
        std::swap(input.audio_l[i], input.audio_l[j]);
        std::swap(input.audio_r[i], input.audio_r[j]);
    }
}

void app::audio::AudioPlayer::SetMarker(float relative_position, bool is_start_marker)
{
    const float clamped_position = std::clamp(relative_position, 0.0f, 1.0f);
    const size_t frame_index = static_cast<size_t>(clamped_position * static_cast<float>(GetTotalFrames()));

    if (is_start_marker)
    {
        start_marker_ = frame_index;
        return;
    }
    stop_marker_ = frame_index;
}

bool app::audio::AudioPlayer::IsOutpuBufferValid(wav::audio_frame_t &output)
{
    if (output.audio_l == nullptr || output.audio_r == nullptr)
    {
        output.n_frames = 0;
        return false;
    }

    return true;
}

void app::audio::AudioPlayer::RestartGlitchEngine()
{
    glitch_engine_.OnNewFile(wav_file_handler_.GetSampleRate(), wav_file_handler_.GetDataSize());

    // Pre-scan the whole file through the beat tracker so BeatTracker::beat_frame_tracker_ is
    // already populated with detected beat positions (see GlitchEngine::TrackerProcessFrame())
    // before playback/glitching starts, rather than only discovering them as Read() streams
    // through the file for the first time. Reuses the time_adjust_source_l_/r_ scratch buffers
    // the same way GetAudioDataToDisplay() does below, since nothing is streaming yet.
    const size_t total_frames = GetTotalFrames();
    size_t frames_scanned = 0;
    while (frames_scanned < total_frames)
    {
        const size_t chunk_frames = std::min(total_frames - frames_scanned, kMaxReadFrames);
        const size_t bytes_to_read = chunk_frames * frame_bytes_;

        size_t bytes_read = 0;
        FsResult read_result = file_system_.Read(file_, read_scratch_buffer_, bytes_to_read, &bytes_read);
        if (read_result != FsResult::kOk || bytes_read == 0)
        {
            break; // short read/error: nothing more to scan.
        }

        wav::audio_frame_t chunk_output{time_adjust_source_l_, time_adjust_source_r_, 0};
        int converted = wav_file_handler_.ReadData(read_scratch_buffer_, bytes_read, chunk_output, chunk_frames);
        if (converted <= 0)
        {
            break;
        }

        glitch_engine_.TrackerProcessFrame(chunk_output, static_cast<size_t>(converted), frames_scanned);

        frames_scanned += static_cast<size_t>(converted);
        if (static_cast<size_t>(converted) < chunk_frames)
        {
            break; // short read: end of data chunk reached partway through.
        }
    }

    // Restore the file cursor to LoadFile()'s postcondition (start of the data chunk) before
    // SeekStartChunk()/Read() take over for actual playback.
    SeekDataChunkStart();
}

int app::audio::AudioPlayer::Start()
{
    is_playing_ = true;
    is_freezed_ = false;
    return 0;
}

int app::audio::AudioPlayer::Stop()
{
    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
        is_playing_ = false;
        is_freezed_ = false;
    }
    return 0;
}

int app::audio::AudioPlayer::SetLooping(bool looping)
{
    is_looping_ = looping;
    return 0;
}

int app::audio::AudioPlayer::SetPlaybackSpeed(float speed)
{
    playback_speed_ = speed;
    if (playback_speed_ <= kMinPlayBackSpeed)
    {
        playback_speed_ = kMinPlayBackSpeed;
    }
    else if (playback_speed_ > kMaxPlaybackSpeed)
    {
        playback_speed_ = kMaxPlaybackSpeed;
    }

    glitch_engine_.SetPlaybackSpeed(speed);
    glitch_engine_.SavePreviousPlaybackSpeed(playback_speed_);

    return 0;
}

float app::audio::AudioPlayer::GetPlaybackSpeed()
{
    return playback_speed_;
}

void app::audio::AudioPlayer::Freeze(bool enable)
{
    is_freezed_ = enable && is_playing_;
}

void app::audio::AudioPlayer::SetGlitching(bool enable)
{
    glitch_enable_ = enable;
}

void app::audio::AudioPlayer::EnableNoiseOutput(bool enable)
{
    glitch_engine_.EnableNoiseOutput(enable);
}

void app::audio::AudioPlayer::EnablePitchMod(bool enable)
{
    glitch_engine_.EnablePitchMod(enable);
}

void app::audio::AudioPlayer::SetBitcrushEnable(bool enable)
{
    glitch_engine_.SetBitcrushEnable(enable);
}

void app::audio::AudioPlayer::SetPitchModProbability(float value)
{
    glitch_engine_.SetPitchModProbability(value);
}

void app::audio::AudioPlayer::SetStutterProbability(float value)
{
    glitch_engine_.SetStutterProbability(value);
}

void app::audio::AudioPlayer::SetSampleRateReduction(int value)
{
    glitch_engine_.SetSampleRateReduction(value);
}

void app::audio::AudioPlayer::SetReductionFactor(int value)
{
    glitch_engine_.SetReductionFactor(value);
}

void app::audio::AudioPlayer::EnableClickOutput(bool enable)
{
    glitch_engine_.EnableClickOutput(enable);
}

void app::audio::AudioPlayer::SetClickDensity(float value)
{
    glitch_engine_.SetClickDensity(value);
}

int app::audio::AudioPlayer::GetSampleRate()
{
    return wav_file_handler_.GetSampleRate();
}

size_t app::audio::AudioPlayer::GetNumChannels()
{
    return wav_file_handler_.GetNumChannels();
}

int app::audio::AudioPlayer::GetBitsPerSample()
{
    return wav_file_handler_.GetBitsPerSample();
}

int app::audio::AudioPlayer::GetDataSize()
{
    return wav_file_handler_.GetDataSize();
}

bool app::audio::AudioPlayer::IsNewAudioDataRequired()
{
    return (file_ == nullptr || is_playing_ == false);
}

uint32_t app::audio::AudioPlayer::GetDurationMs()
{
    const size_t bytes_per_sample = static_cast<size_t>(wav_file_handler_.GetBitsPerSample()) / 8;
    const size_t channels = wav_file_handler_.GetNumChannels();
    const int sample_rate = wav_file_handler_.GetSampleRate();
    const size_t bytes_per_second = bytes_per_sample * channels * static_cast<size_t>(sample_rate);
    if (bytes_per_second == 0)
    {
        return 0;
    }

    const uint64_t data_size = static_cast<uint64_t>(wav_file_handler_.GetDataSize());
    return static_cast<uint32_t>((data_size * kMillisMultiplier) / bytes_per_second);
}

size_t app::audio::AudioPlayer::GetPlayheadFrame()
{
    // current_frame_index_ is "next frame to read" (forward) or "one past last frame played"
    // (reverse); normalize both to the frame currently at the playhead.
    return (is_reverse_ && current_frame_index_ > 0) ? current_frame_index_ - 1 : current_frame_index_;
}

uint32_t app::audio::AudioPlayer::GetPlayheadMs()
{
    const int sample_rate = wav_file_handler_.GetSampleRate();
    if (sample_rate == 0)
    {
        return 0;
    }

    const uint64_t playhead_frame = static_cast<uint64_t>(GetPlayheadFrame());
    return static_cast<uint32_t>((playhead_frame * kMillisMultiplier) / static_cast<uint64_t>(sample_rate));
}

const char *app::audio::AudioPlayer::GetAudioFile()
{
    return loaded_file_path_;
}

int app::audio::AudioPlayer::GetAudioDataToDisplay(uint16_t *data, size_t n_frames)
{
    // Only callable while nothing is streaming through Read(): lets this reuse the existing
    // time_adjust_source_l_/r_ scratch buffers below instead of needing its own.
    if (data == nullptr || n_frames == 0 || file_ == nullptr || frame_bytes_ == 0 || is_playing_)
    {
        return -1;
    }

    const size_t total_frames = GetTotalFrames();
    if (total_frames == 0)
    {
        return -1;
    }

    SeekDataChunkStart();

    size_t frames_consumed = 0;
    for (size_t column = 0; column < n_frames; column++)
    {
        // Distribute total_frames evenly across n_frames columns; the last column absorbs any
        // remainder from the integer division.
        const size_t bucket_end = ((column + 1) * total_frames) / n_frames;
        size_t bucket_frames_remaining = (bucket_end > frames_consumed) ? (bucket_end - frames_consumed) : 0;

        float peak = 0.0f;
        while (bucket_frames_remaining > 0)
        {
            const size_t chunk_frames = std::min(bucket_frames_remaining, kMaxReadFrames);
            const size_t bytes_to_read = chunk_frames * frame_bytes_;

            size_t bytes_read = 0;
            FsResult read_result = file_system_.Read(file_, read_scratch_buffer_, bytes_to_read, &bytes_read);
            if (read_result != FsResult::kOk || bytes_read == 0)
            {
                break; // short read/error: nothing more to scan.
            }

            wav::audio_frame_t chunk_output{time_adjust_source_l_, time_adjust_source_r_, 0};
            int converted = wav_file_handler_.ReadData(read_scratch_buffer_, bytes_read, chunk_output, chunk_frames);
            if (converted <= 0)
            {
                break;
            }

            for (int i = 0; i < converted; i++)
            {
                // Mix down to mono locally: the display data is monophonic, one value per
                // column, regardless of how many channels the source file has.
                const float mono_sample = 0.5f * (time_adjust_source_l_[i] + time_adjust_source_r_[i]);
                peak = std::max(peak, std::fabs(mono_sample));
            }

            frames_consumed += static_cast<size_t>(converted);
            bucket_frames_remaining -= static_cast<size_t>(converted);

            if (static_cast<size_t>(converted) < chunk_frames)
            {
                break; // short read: end of data chunk reached partway through this bucket.
            }
        }

        data[column] = static_cast<uint16_t>(std::min(peak, 1.0f) * static_cast<float>(std::numeric_limits<uint16_t>::max()));
    }

    // Restore the file cursor to LoadFile()'s postcondition (start of the data chunk), so
    // playback (which always re-syncs the cursor from current_frame_index_ before reading, see
    // SeekStartChunk()) is unaffected by this scan.
    SeekDataChunkStart();
    return 0;
}

bool app::audio::AudioPlayer::IsPlaying()
{
    return is_playing_;
}

bool app::audio::AudioPlayer::IsReverse()
{
    return is_reverse_;
}

bool app::audio::AudioPlayer::IsLooping()
{
    return is_looping_;
}

bool app::audio::AudioPlayer::IsFrozen()
{
    return is_freezed_;
}

app::audio::AudioPlayer::~AudioPlayer()
{
    if (file_ != nullptr)
    {
        file_system_.Close(file_);
        file_ = nullptr;
    }
}
