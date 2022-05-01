package com.github.tmarsteel.audionetwork.transmitter

import club.minnced.opus.util.OpusLibrary
import kotlinx.coroutines.runBlocking
import java.io.File
import java.nio.ByteBuffer
import javax.sound.sampled.AudioSystem

fun main(args: Array<String>) {
    tx(File(args[0]), runBlocking { discoverReceivers() })
    OpusLibrary.loadFromJar()
}

fun tx(file: File, receivers: List<DiscoveredReceiver>) {
    val audioIn = AudioSystem.getAudioInputStream(file)
    MulticastAudioOutput(audioIn.format).use { multicast ->
        runBlocking {
            for (receiver in receivers) {
                multicast.addReceiver(receiver)
            }
            audioIn.copyTo(multicast.asOutputStream())
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

fun format(data: ByteArray): String {
    val output = StringBuilder()
    for (offset in 0 until data.size) {
        output.append(data[offset].toUByte().toString(16).uppercase().padStart(2, '0'))
    }

    return output.toString()
}