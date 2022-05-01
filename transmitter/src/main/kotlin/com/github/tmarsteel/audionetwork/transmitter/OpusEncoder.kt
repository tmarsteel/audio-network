package com.github.tmarsteel.audionetwork.transmitter

import club.minnced.opus.util.OpusLibrary
import com.sun.jna.ptr.PointerByReference
import tomp2p.opuswrapper.Opus
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.IntBuffer
import java.nio.ShortBuffer
import java.time.Duration
import javax.sound.sampled.AudioFormat
import kotlin.math.min

class OpusEncoder(
    val inputFormat: AudioFormat,
    application: Application = Application.AUDIO,
    complexity: Int = 10,
    signal: Signal = Signal.AUTO,
    initialFrameSize: Duration = Duration.ofMillis(20),
    initialMaxEncodedFrameSizeInBytes: Int = 4096
) : AutoCloseable {
    init {
        if (inputFormat.isBigEndian) {
            throw AudioFormatNotSupportedException("Must be little-endian")
        }
        if (inputFormat.encoding != AudioFormat.Encoding.PCM_SIGNED) {
            throw AudioFormatNotSupportedException("Must be PCM signed")
        }
        if (inputFormat.sampleSizeInBits != 16) {
            throw AudioFormatNotSupportedException("Bit-depth must be 16")
        }
        if (inputFormat.channels !in 1..2) {
            throw AudioFormatNotSupportedException("Must be one or two channels")
        }
        if (inputFormat.sampleRate.toInt() !in SUPPORTED_SAMPLING_RATES) {
            throw AudioFormatNotSupportedException("Sampling rate must be one of $SUPPORTED_SAMPLING_RATES")
        }

        require(complexity in 0..10)

    }

    @Volatile
    private var closed = false
    private val nativeEncoder: PointerByReference?
    /** This many samples in the output are not relevant/can be discarded by the decoder */
    val lookeheadSampleCount: Int
    init {
        OpusLibrary.loadFromJar()
        val error = IntBuffer.allocate(1)
        nativeEncoder = Opus.INSTANCE.opus_encoder_create(inputFormat.sampleRate.toInt(), inputFormat.channels, application.value, error)
        throwOnOpusError(error.get())
        check(nativeEncoder != null && nativeEncoder.value != null)
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_BITRATE_REQUEST, 92000))
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_COMPLEXITY_REQUEST, complexity))
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_SIGNAL_REQUEST, signal.value))
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_MAX_BANDWIDTH_REQUEST, when(inputFormat.sampleRate.toInt()) {
            8000 -> Opus.OPUS_BANDWIDTH_NARROWBAND
            12000 -> Opus.OPUS_BANDWIDTH_MEDIUMBAND
            16000 -> Opus.OPUS_BANDWIDTH_WIDEBAND
            24000 -> Opus.OPUS_BANDWIDTH_SUPERWIDEBAND
            48000 -> Opus.OPUS_BANDWIDTH_FULLBAND
            else -> error("sample rate not validated?")
        }))
        val lookahead = IntBuffer.allocate(1)
        Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_GET_LOOKAHEAD_REQUEST, lookahead)
        lookeheadSampleCount = lookahead.get()
    }

    var frameSize: Duration = initialFrameSize
        set(value) {
            require(value in SUPPORTED_FRAME_SIZES)
            field = value
        }

    var maxEncodedFrameSizeBytes: Int = initialMaxEncodedFrameSizeInBytes
        set(value) {
            require(value > 0)
            field = value
        }

    /** opus requires frame size as the number of samples per channel */
    private val frameSizeInSamples: Int get() = frameSizeToSamples(inputFormat, frameSize)
    private val frameSizeInBytes: Int get() = frameSizeToBytes(inputFormat, frameSize)
    private val encodeBuffer = ByteRingBuffer(frameSizeToBytes(inputFormat, SUPPORTED_FRAME_SIZES.maxOrNull()!!))

    /**
     * Adds new audio data to be encoded and returns encoded frames.
     * @param data the data, must be in the [inputFormat]
     * @return Encoded frames; only complete frames are encoded. If the end of the input data has been reached, call [final].
     */
    fun submitAudioData(data: ByteBuffer): List<ByteBuffer> {
        val resultFrames = mutableListOf<ByteBuffer>()
        submitAudioDataInternal(data, resultFrames)
        return resultFrames
    }

    private fun submitAudioDataInternal(data: ByteBuffer, dst: MutableCollection<ByteBuffer>) {
        encodeTo(dst)

        val nBytesToTake = min(encodeBuffer.remainingWrite, data.remaining())
        encodeBuffer.put(data, nBytesToTake)

        if (data.hasRemaining()) {
            val remainingWriteBefore = encodeBuffer.remainingWrite
            encodeTo(dst)
            check(encodeBuffer.remainingWrite > remainingWriteBefore)
            submitAudioDataInternal(data, dst)
        }
    }

    /**
     * Encodes any audio data remaining in the encoding buffer, possibly padding it with silence
     * to fill a whole frame.
     */
    fun final(): List<ByteBuffer> {
        val resultFrames = mutableListOf<ByteBuffer>()
        encodeTo(resultFrames)

        if (encodeBuffer.remainingRead > 0) {
            val silence = ByteBuffer.wrap(ByteArray(frameSizeInBytes - encodeBuffer.remainingRead) { 0 })
            submitAudioDataInternal(silence, resultFrames)
        }

        check(encodeBuffer.remainingRead == 0)
        return resultFrames
    }

    /**
     * Takes a single frame out of [encodeBuffer] and converts it to a [ShortBuffer]
     * with big-endian encoding. If there is less than a full frame in [encodeBuffer], returns null.
     */
    private fun takeSingleFrame(): ShortBuffer? {
        val nBytesInBuffer = encodeBuffer.remainingRead
        if (nBytesInBuffer < frameSizeInBytes) {
            return null
        }
        val inputByteBuffer = ByteBuffer.allocate(frameSizeInBytes)
        encodeBuffer.get(inputByteBuffer)
        inputByteBuffer.flip()

        val inputBufferView = inputByteBuffer
            .order(ByteOrder.LITTLE_ENDIAN)
            .asShortBuffer()
        val actualInputBuffer = ShortBuffer.allocate(inputBufferView.limit())
        actualInputBuffer.put(inputBufferView)
        actualInputBuffer.flip()

        return actualInputBuffer
    }

    /**
     * Encodes as many full frames as possible from [encodeBuffer] and puts the
     * results into [dst].
     */
    private fun encodeTo(dst: MutableCollection<ByteBuffer>) {
        while (true) {
            val singleFrame = takeSingleFrame() ?: break
            val outputBuffer = ByteBuffer.allocate(maxEncodedFrameSizeBytes)

            val encodedLength = Opus.INSTANCE.opus_encode(nativeEncoder, singleFrame, frameSizeInSamples, outputBuffer, outputBuffer.remaining())
            if (encodedLength < 0) {
                throwOnOpusError(encodedLength)
            }

            outputBuffer.position(0)
            outputBuffer.limit(encodedLength)
            dst.add(outputBuffer)
        }
    }

    override fun close() {
        if (closed) {
            return
        }

        if (nativeEncoder != null && nativeEncoder.value != null) {
            Opus.INSTANCE.opus_encoder_destroy(nativeEncoder)
        }
    }

    enum class Application(internal val value: Int) {
        VOIP(Opus.OPUS_APPLICATION_VOIP),
        AUDIO(Opus.OPUS_APPLICATION_AUDIO),
        RESTRICTED_LOW_DELAY(Opus.OPUS_APPLICATION_RESTRICTED_LOWDELAY)
    }

    enum class Signal(internal val value: Int) {
        AUTO(Opus.OPUS_AUTO),
        VOICE(Opus.OPUS_SIGNAL_VOICE),
        MUSIC(Opus.OPUS_SIGNAL_MUSIC),
    }

    companion object {
        val SUPPORTED_SAMPLING_RATES: Set<Int> = setOf(8000, 12000, 16000, 24000, 48000)
        val SUPPORTED_FRAME_SIZES: Set<Duration> = setOf(
            Duration.ofNanos(2500000),
            Duration.ofMillis(5),
            Duration.ofMillis(10),
            Duration.ofMillis(20),
            Duration.ofMillis(40),
            Duration.ofMillis(60),
        )

        fun frameSizeToSamples(audioFormat: AudioFormat, frameSize: Duration): Int {
            return Math.toIntExact(audioFormat.sampleRate.toLong() * frameSize.toNanos() / 1000000000L)
        }

        fun frameSizeToBytes(audioFormat: AudioFormat, frameSize: Duration): Int {
            return frameSizeToSamples(audioFormat, frameSize) * audioFormat.channels * 2
        }
    }
}