package com.github.tmarsteel.audionetwork.transmitter

import club.minnced.opus.util.OpusLibrary
import com.sun.jna.ptr.PointerByReference
import tomp2p.opuswrapper.Opus
import java.io.Closeable
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.IntBuffer
import java.nio.ShortBuffer
import java.time.Duration
import javax.sound.sampled.AudioFormat
import kotlin.math.min

class OpusEncodingOutputStream(
    private val audioFormat: AudioFormat,
    private val downstream: Downstream,
    application: Application = Application.AUDIO,
    complexity: Int = 10,
    signal: Signal = Signal.AUTO,
    private val frameSize: Duration = Duration.ofMillis(20),
    private val maxEncodedFrameSizeBytes: Int = 3*1276
) : OutputStream(), AutoCloseable {
    init {
        require(!audioFormat.isBigEndian)
        require(audioFormat.encoding == AudioFormat.Encoding.PCM_SIGNED)
        require(audioFormat.sampleSizeInBits == 16)
        require(audioFormat.channels in 1..2)
        require(audioFormat.sampleRate.toInt() in setOf(8000, 12000, 16000, 24000, 48000))
        require(complexity in 0..10)
        require(frameSize.toNanos() == 2500000L || frameSize.toMillis() in setOf(5L, 10L, 20L, 40L, 60L))
    }

    private var closed = false
    private val nativeEncoder: PointerByReference?
    /** This many samples in the output are not relevant/can be discarded by the decoder */
    val lookeheadSampleCount: Int
    init {
        OpusLibrary.loadFromJar()
        val error = IntBuffer.allocate(1)
        nativeEncoder = Opus.INSTANCE.opus_encoder_create(audioFormat.sampleRate.toInt(), audioFormat.channels, application.value, error)
        throwOnOpusError(error.get())
        check(nativeEncoder != null && nativeEncoder.value != null)
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_BITRATE_REQUEST, 64000))
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_COMPLEXITY_REQUEST, complexity))
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_SIGNAL_REQUEST, signal.value))
        throwOnOpusError(Opus.INSTANCE.opus_encoder_ctl(nativeEncoder, Opus.OPUS_SET_MAX_BANDWIDTH_REQUEST, when(audioFormat.sampleRate.toInt()) {
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

    /** opus requires frame size as the number of samples per channel */
    private val frameSizeInSamples: Int = Math.toIntExact(audioFormat.sampleRate.toLong() * frameSize.toNanos() / 1000000000L)
    private val frameSizeInBytes: Int = frameSizeInSamples * audioFormat.channels * 2
    private val encodeBuffer = ByteRingBuffer(frameSizeInBytes)

    override fun write(b: Int) {
        if (encodeBuffer.remainingWrite == 0) {
            flush()
        }

        check(encodeBuffer.remainingWrite > 0)
        encodeBuffer.put(b.toByte())
    }

    override fun write(b: ByteArray, off: Int, len: Int) {
        if (encodeBuffer.remainingWrite == 0) {
            flush()
        }

        val nBytesToTake = min(encodeBuffer.remainingWrite, len)
        encodeBuffer.put(b, off, nBytesToTake)
        if (nBytesToTake < len) {
            write(b, off + nBytesToTake, len - nBytesToTake)
        }
    }

    /**
     * Converts to a [ShortBuffer] with big-endian encoding and pads with silence
     * to give a full frame of [frameSize] duration.
     */
    private fun prepareEncodeBuffer(): ShortBuffer {
        val nBytesInBuffer = encodeBuffer.remainingRead
        val inputByteArray = ByteArray(frameSizeInBytes) { 0 }
        encodeBuffer.get(inputByteArray, 0, nBytesInBuffer)

        val inputBufferView = ByteBuffer.wrap(inputByteArray)
            .order(ByteOrder.LITTLE_ENDIAN)
            .asShortBuffer()
        val actualInputBuffer = ShortBuffer.allocate(inputBufferView.limit())
        actualInputBuffer.put(inputBufferView)
        actualInputBuffer.flip()

        return actualInputBuffer
    }

    override fun flush() {
        flush(force = false)
    }

    /**
     * @param force If true and there is less than a full frame in the buffer, the frame
     *              will be padded with silence. This should not be done in the middle of
     *              a stream, only at the end.
     */
    fun flush(force: Boolean = false) {
        if (encodeBuffer.remainingRead < frameSizeInBytes && !force) {
            return
        }

        val encodeBuffer = prepareEncodeBuffer()
        val outputBuffer = ByteBuffer.allocate(maxEncodedFrameSizeBytes)

        val encodedLength = Opus.INSTANCE.opus_encode(nativeEncoder, encodeBuffer, frameSizeInSamples, outputBuffer, outputBuffer.remaining())
        if (encodedLength < 0) {
            throwOnOpusError(encodedLength)
        }

        outputBuffer.position(0)
        outputBuffer.limit(encodedLength)
        downstream.onEncodedFrameAvailable(outputBuffer)
    }

    override fun close() {
        if (closed) {
            return
        }

        doAllAndThrowCombined(
            {
                flush(force = true)
            },
            {
                closed = true
            },
            {
                downstream.close()
            },
            {
                if (nativeEncoder != null && nativeEncoder.value != null) {
                    Opus.INSTANCE.opus_encoder_destroy(nativeEncoder)
                }
            }
        )
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

    interface Downstream : Closeable {
        fun onEncodedFrameAvailable(data: ByteBuffer)
    }
}