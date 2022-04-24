package com.github.tmarsteel.audionetwork.transmitter

import club.minnced.opus.util.OpusLibrary
import com.github.tmarsteel.audionetwork.protocol.AudioData
import com.github.tmarsteel.audionetwork.protocol.AudioReceiverAnnouncement
import com.google.protobuf.ByteString
import com.google.protobuf.InvalidProtocolBufferException
import tomp2p.opuswrapper.Opus
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.IntBuffer
import java.nio.ShortBuffer
import java.nio.file.Paths
import javax.sound.sampled.AudioFormat
import javax.sound.sampled.AudioSystem
import kotlin.concurrent.thread
import kotlin.io.path.readText

fun main(args: Array<String>) {
    OpusLibrary.loadFromJar()
    println(Opus.INSTANCE.opus_get_version_string())
    tx(File(args[0]))
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
    BroadcastReceiver(InetSocketAddress(58765)) { announcementMessage, deviceIp ->
        println("Found receiver ${announcementMessage.deviceName} $deviceIp, starting tx")
        val txSocket = Socket(deviceIp, 58764)

        AudioSystem.getAudioInputStream(file).use { audioIn ->
            OpusEncodingOutputStream(
                audioIn.format,
                ProtobufWrappingOpusDownstream(audioIn.format, txSocket.getOutputStream(), false),
                signal = OpusEncodingOutputStream.Signal.MUSIC,
                maxEncodedFrameSizeBytes = 4096
            ).use { opusEncoderOut ->
                audioIn.copyTo(opusEncoderOut)
                opusEncoderOut.flush()
                println("Done")
            }
        }
    }
}

class BroadcastReceiver(
    val address: InetSocketAddress,
    private val onAnnouncementReceived: (AudioReceiverAnnouncement, InetAddress) -> Unit
) {
    private val socket = DatagramSocket(address).apply {
        broadcast = true
    }
    private val thread = thread(start = true, name = "broadcast-rx", block = this::listen)
    @Volatile
    private var closed = false

    init {
        Runtime.getRuntime().addShutdownHook(thread(start = false) {
            stop()
        })
    }

    private fun listen() {
        val packet = DatagramPacket(ByteArray(1024), 1024)
        while(true) {
            try {
                socket.receive(packet)
                val message = try {
                    AudioReceiverAnnouncement.parseFrom(ByteBuffer.wrap(packet.data, 0, packet.length))
                }
                catch (ex: InvalidProtocolBufferException) {
                    println("Received invalid protobuf data, ignoring.")
                    continue
                }
                if (message.magicWord != 0x2C5DA044) {
                    println("Received what seems to be valid protobuf, but the magic word is incorrect.")
                    continue
                }

                onAnnouncementReceived(message, packet.address)
                return
            } catch (ex: InterruptedException) {
                if (closed) {
                    return
                }
            }
        }
    }

    fun stop() {
        closed = true
        thread.interrupt()
    }
}

class ProtobufWrappingOpusDownstream(
    private val audioFormat: AudioFormat,
    private val outputStream: OutputStream,
    private val closeDownstream: Boolean = true
) : OpusEncodingOutputStream.Downstream {

    override fun onEncodedFrameAvailable(data: ByteBuffer) {
        val message = AudioData.newBuilder()
            .setSampleRate(audioFormat.sampleRate.toInt())
            .setOpusEncodedFrame(ByteString.copyFrom(data))
            .build()
        message.writeDelimitedTo(outputStream)
        outputStream.flush()
    }

    override fun close() {
        if (closeDownstream) {
            outputStream.close()
        }
    }
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