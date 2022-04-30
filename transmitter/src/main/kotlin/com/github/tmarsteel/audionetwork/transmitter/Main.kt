package com.github.tmarsteel.audionetwork.transmitter

import com.github.tmarsteel.audionetwork.protocol.AudioData
import com.google.protobuf.ByteString
import kotlinx.coroutines.runBlocking
import tomp2p.opuswrapper.Opus
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.OutputStream
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.IntBuffer
import java.nio.ShortBuffer
import java.nio.file.Paths
import java.time.Duration
import javax.sound.sampled.AudioFormat
import javax.sound.sampled.AudioSystem
import kotlin.io.path.readText

fun main(args: Array<String>) {
    println(runBlocking { discoverReceivers() })
}

fun recode() {
    val wavHeader = ByteArray(0x2C)
    FileInputStream("C:\\Users\\tobia\\Desktop\\EMOTIONAL DAMAGE 48khz.wav").use {
        it.read(wavHeader)
    }

    val monitoredData = Paths.get("C:\\Users\\tobia\\Desktop\\com4_output.log").readText()
        .replace("\n", "")
        .replace("\r", "")
        .replace(" ", "")
        .windowed(2, 2) { Integer.parseInt(it.toString(), 16).toByte() }
        .toByteArray()

    FileOutputStream("C:\\Users\\tobia\\Desktop\\received.wav").use { out ->
        out.write(wavHeader)
        out.write(monitoredData)
    }
}

fun recode2() {
    val file = File("C:\\Users\\tobia\\Desktop\\EMOTIONAL DAMAGE 48khz.wav")
    val sourceFormat = AudioSystem.getAudioFileFormat(file).format
    val wavHeader = ByteArray(0x2C)
    val encodedFrames = mutableListOf<ByteBuffer>()

    FileInputStream(file).use { sourceIn ->
        sourceIn.read(wavHeader)

        val opusOut = OpusEncodingOutputStream(
            sourceFormat,
            object : OpusEncodingOutputStream.Downstream {
                override fun onEncodedFrameAvailable(data: ByteBuffer) {
                    encodedFrames.add(data)
                }

                override fun close() {}
            },
        )
        sourceIn.copyTo(opusOut)
    }

    val outputBuffer = ShortBuffer.allocate(4096)
    val opusError = IntBuffer.allocate(1)
    val opusDecoder = Opus.INSTANCE.opus_decoder_create(sourceFormat.sampleRate.toInt(), sourceFormat.channels, opusError)
    throwOnOpusError(opusError.get())

    FileOutputStream("$file.recode.wav").use { fileOut ->
        fileOut.write(wavHeader)
        for (encodedFrame in encodedFrames) {
            require(encodedFrame.position() == 0)
            outputBuffer.clear()
            val nDecodedSamples = Opus.INSTANCE.opus_decode(
                opusDecoder,
                encodedFrame.array(),
                encodedFrame.limit(),
                outputBuffer,
                outputBuffer.capacity() / 2,
                0
            )
            if (nDecodedSamples < 0) {
                throwOnOpusError(nDecodedSamples)
            }

            outputBuffer.position(0)
            outputBuffer.limit(nDecodedSamples * sourceFormat.channels)
            while (outputBuffer.hasRemaining()) {
                val short = outputBuffer.get()
                val firstByte = short.toInt() and 0xFF
                val secondByte = (short.toInt() shr 8) and 0xFF
                fileOut.write(firstByte)
                fileOut.write(secondByte)
            }
        }
    }
}

fun tx(file: File) {
    val receiver = runBlocking { discoverReceivers() }
        .firstOrNull()
        ?: error("Found no receivers")

    println("Transmitting to ${receiver.inetAddress}")

    val txSocket = Socket(receiver.inetAddress, 58764)

    AudioSystem.getAudioInputStream(file).use { audioIn ->
        OpusEncodingOutputStream(
            audioIn.format,
            ProtobufWrappingOpusDownstream(audioIn.format, txSocket.getOutputStream(), false),
            signal = OpusEncodingOutputStream.Signal.MUSIC,
            maxEncodedFrameSizeBytes = 4096,
            frameSize = Duration.ofMillis(60),
        ).use { opusEncoderOut ->
            audioIn.copyTo(opusEncoderOut)
            Thread.sleep(10000)
            println("Done")
        }
    }
}

class ProtobufWrappingOpusDownstream(
    private val audioFormat: AudioFormat,
    private val outputStream: OutputStream,
    private val closeDownstream: Boolean = true
) : OpusEncodingOutputStream.Downstream {

    var nFrames = 0
    override fun onEncodedFrameAvailable(data: ByteBuffer) {
        val message = AudioData.newBuilder()
            .setOpusEncodedFrame(ByteString.copyFrom(data))
            .build()
        message.writeDelimitedTo(outputStream)
        if (nFrames >= 5) {
            nFrames = 0
            Thread.sleep(100)
        }
        outputStream.flush()
    }

    override fun close() {
        if (closeDownstream) {
            outputStream.close()
        }
    }
}

fun format(data: ByteBuffer): String {
    val output = StringBuilder()
    while (data.hasRemaining()) {
        output.append(data.get().toUByte().toString(16).uppercase().padStart(2, '0'))
    }
    data.flip()
    return output.toString()
}

fun format(data: ByteArray, length: Int, nColumns: Int = 10): String {
    val output = StringBuilder()
    var currentColumn = 0
    for (offset in 0 until length) {
        output.append(data[offset].toUByte().toString(16).uppercase().padStart(2, '0'))
        if (currentColumn < nColumns) {
            output.append(' ')
            currentColumn++
        } else {
            output.append('\n')
            currentColumn = 0
        }
    }

    return output.toString()
}