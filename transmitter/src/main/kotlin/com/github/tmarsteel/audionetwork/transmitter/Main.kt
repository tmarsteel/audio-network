package com.github.tmarsteel.audionetwork.transmitter

import com.github.tmarsteel.audionetwork.protocol.AudioData
import com.github.tmarsteel.audionetwork.protocol.AudioReceiverAnnouncement
import com.google.protobuf.ByteString
import com.google.protobuf.InvalidProtocolBufferException
import java.io.File
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.time.Duration
import javax.sound.sampled.AudioFormat
import javax.sound.sampled.AudioSystem
import kotlin.concurrent.thread

fun main(args: Array<String>) {
    tx(File(args[0]))
}

fun tx(file: File) {
    val audioFormat = AudioSystem.getAudioFileFormat(file).format
    require(audioFormat.channels == 2)
    require(audioFormat.sampleSizeInBits == 16)
    val audioIn = AudioSystem.getAudioInputStream(file)
    BroadcastReceiver(InetSocketAddress(58765)) { announcementMessage, deviceIp ->
        println("Found receiver ${announcementMessage.deviceName} $deviceIp, starting tx")
        val txSocket = Socket(deviceIp, 58764)
        val buffer = ByteArray(audioFormat.floorToNextMultipleOfFrameSize(8192))
        val bufferDurationMillis = audioFormat.getBufferDurationInMillis(buffer.size)
        do {
            val nBytesRead = audioIn.readNBytes(buffer, 0, buffer.size)
            val message = AudioData.newBuilder()
                .setBytesPerSample(2)
                .setSampleRate(audioFormat.sampleRate.toInt())
                .setSamples(ByteString.copyFrom(buffer, 0, nBytesRead))
                .build()
            message.writeDelimitedTo(txSocket.getOutputStream())
            txSocket.getOutputStream().flush()
            Thread.sleep(bufferDurationMillis)
        } while (audioIn.available() > 0)
        txSocket.close()
        println("Tx done")
    }
}

fun AudioFormat.createBufferOfDuration(forDuration: Duration): ByteArray {
    val size = Math.toIntExact(sampleRate.toLong() * forDuration.toNanos() / 1000000000L * (sampleSizeInBits / 8).toLong() * channels.toLong())
    return ByteArray(size)
}

fun AudioFormat.floorToNextMultipleOfFrameSize(byteCount: Int): Int {
    return (byteCount / frameSize) * frameSize
}

fun AudioFormat.getBufferDurationInMillis(bufferSize: Int): Long {
    return (bufferSize.toLong() * 1000_000_000L / frameSize.toLong() / frameRate.toLong()) / 1000_000L
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