package com.github.tmarsteel.audionetwork.transmitter

import com.github.tmarsteel.audionetwork.protocol.ReceiverInformation
import kotlinx.coroutines.runBlocking
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.SocketAddress
import java.nio.ByteBuffer
import java.time.Duration
import java.util.concurrent.CopyOnWriteArrayList
import javax.sound.sampled.AudioFormat
import javax.sound.sampled.AudioInputStream
import javax.sound.sampled.AudioSystem

class MulticastAudioOutput(
    val sourceFormat: AudioFormat,
    opusApplication: OpusEncoder.Application = OpusEncoder.Application.AUDIO,
    opusComplexity: Int = 10,
    opusSignal: OpusEncoder.Signal = OpusEncoder.Signal.MUSIC,
    opusFrameSize: Duration = Duration.ofMillis(60)
) : AutoCloseable {
    @Volatile
    private var closed = false

    private val opusEncoder: OpusEncoder = try {
            OpusEncoder(
                sourceFormat,
                opusApplication,
                opusComplexity,
                opusSignal,
                opusFrameSize
            )
        }
        catch (ex: AudioFormatNotSupportedException) {
            OpusEncoder(
                FALLBACK_AUDIO_FORMAT,
                opusApplication,
                opusComplexity,
                opusSignal,
                opusFrameSize
            )
        }

    init {
        if (opusEncoder.inputFormat != sourceFormat && !AudioSystem.isConversionSupported(sourceFormat, opusEncoder.inputFormat)) {
            throw AudioFormatNotSupportedException("The source format $sourceFormat is not directly supported by Opus and there is no converter available.")
        }
    }
    private val minDecodedFrameSizeInBytes = OpusEncoder.frameSizeToBytes(opusEncoder.inputFormat, OpusEncoder.SUPPORTED_FRAME_SIZES.minOf { it })

    private val actualReceivers: MutableList<RemoteAudioReceiver> = CopyOnWriteArrayList()
    val receivers: List<ReceiverInformation>
        get() = actualReceivers.map { it.receiverInformation }

    suspend fun addReceiver(receiver: DiscoveredReceiver) = addReceiver(receiver.inetAddress)
    suspend fun addReceiver(receiverAddress: InetAddress) = addReceiver(InetSocketAddress(receiverAddress, 58764))
    suspend fun addReceiver(receiverAddress: SocketAddress) {
        check(!closed)

        val receiverHandle = RemoteAudioReceiver.connect(receiverAddress)
        if (receiverHandle.receiverInformation.maxDecodedFrameSize < minDecodedFrameSizeInBytes) {
            receiverHandle.close()
            throw IllegalStateException("Cannot transmit to this receiver: decoded frame size buffer too small")
        }
        actualReceivers.add(receiverHandle)
        onReceiversChanged()
    }

    suspend fun writeAudio(rawData: ByteBuffer) {
        check(!closed)

        val rawDataInOpusInputFormat = if (sourceFormat == opusEncoder.inputFormat) rawData else convertFrame(rawData)
        sendEncodedFrames(opusEncoder.submitAudioData(rawDataInOpusInputFormat))
    }

    /**
     * The receive buffer on the receivers is pretty small. When playing back an audio file (with practically infinite
     * input data rate), it is easily overfilled. The TCP retransmissions then actually cause underflow. Sending data
     * at the same pace as it gets played back solves this problem.
     *
     * Models the buffer usage in the receivers in milliseconds of audio data.
     */
    private val sendRateLimiter = LeakyBucket(1200, 1000)

    private suspend fun sendEncodedFrames(encodedFrames: Collection<ByteBuffer>) {
        encodedFrames.forEach { encodedFrame ->
            sendRateLimiter.put(opusEncoder.frameSize.toMillis())
            actualReceivers.forEach { receiver ->
                receiver.queueEncodedOpusFrame(encodedFrame)
                encodedFrame.flip()
            }
        }
    }

    private fun convertFrame(sourceData: ByteBuffer): ByteBuffer {
        if (!sourceData.hasArray()) {
            val arrayBacked = ByteBuffer.allocate(sourceData.remaining())
            arrayBacked.put(sourceData)
            return convertFrame(arrayBacked)
        }

        check(sourceData.hasArray())
        val sourceDataLengthInBytes = sourceData.remaining()
        val audioIn = AudioSystem.getAudioInputStream(
            opusEncoder.inputFormat,
            AudioInputStream(ByteArrayInputStream(sourceData.array(), sourceData.arrayOffset(), sourceData.remaining()), sourceFormat, sourceDataLengthInBytes.toLong() / sourceFormat.frameSize)
        )
        val targetOut = ByteArrayOutputStream(sourceDataLengthInBytes)
        audioIn.copyTo(targetOut)
        return ByteBuffer.wrap(targetOut.toByteArray())
    }

    fun asOutputStream(): OutputStream = asOutputStream

    override fun close() {
        closed = true
        actualReceivers.forEach { it.close() }
    }

    private fun onReceiversChanged() {
        val maxRawFrameSizeBytes = actualReceivers.minOf { it.receiverInformation.maxDecodedFrameSize }
        opusEncoder.frameSize = OpusEncoder.SUPPORTED_FRAME_SIZES
            .map { frameSize -> frameSize to OpusEncoder.frameSizeToBytes(opusEncoder.inputFormat, frameSize) }
            .filter { (_, frameSizeInBytes) -> frameSizeInBytes <= maxRawFrameSizeBytes }
            .maxOfOrNull { (frameSize, _) -> frameSize }
            ?: throw IllegalStateException("Cannot accommodate all receivers: receive buffer too small")
        opusEncoder.maxEncodedFrameSizeBytes = actualReceivers.minOf { it.receiverInformation.maxEncodedFrameSize }
    }

    private val asOutputStream = object : OutputStream(), AutoCloseable {
        override fun write(b: Int) {
            runBlocking {
                writeAudio(ByteBuffer.wrap(ByteArray(1) { b.toByte() }))
            }
        }

        override fun write(b: ByteArray, off: Int, len: Int) {
            runBlocking {
                writeAudio(ByteBuffer.wrap(b, off, len))
            }
        }

        override fun close() {
            this@MulticastAudioOutput.close()
        }

        override fun flush() {
            runBlocking {
                sendEncodedFrames(opusEncoder.final())
            }
        }
    }

    companion object {
        val FALLBACK_AUDIO_FORMAT = AudioFormat(48000.0f, 16, 2, true, false)
    }
}