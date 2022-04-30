package com.github.tmarsteel.audionetwork.transmitter

import com.github.tmarsteel.audionetwork.protocol.BroadcastMessage
import com.github.tmarsteel.audionetwork.protocol.DiscoveryResponse
import com.google.protobuf.InvalidProtocolBufferException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.SocketException
import java.nio.ByteBuffer
import kotlin.concurrent.thread
import kotlin.streams.asSequence
import kotlin.time.Duration
import kotlin.time.Duration.Companion.nanoseconds
import kotlin.time.Duration.Companion.seconds

private const val MAGIC_WORD = 0x2C5DA044

suspend fun discoverReceivers(
    port: UShort = 58765.toUShort(),
    timeout: Duration = 2.seconds,
): List<DiscoveredReceiver> {
    require(timeout.isFinite())

    val discoveryStartedAt = System.nanoTime()
    fun timeSince(time: Long) = (System.nanoTime() - time).nanoseconds

    return withContext(Dispatchers.IO) {
        val broadcastAddresses = NetworkInterface.networkInterfaces()
            .asSequence()
            .filter { !it.isLoopback }
            .flatMap { it.interfaceAddresses }
            .mapNotNull { it.broadcast }
            .distinct()
            .map { InetSocketAddress(it, port.toInt()) }
            .toList()

        val discoveryRequest = BroadcastMessage.newBuilder()
            .setMagicWord(MAGIC_WORD)
            .setDiscoveryRequest(true)
            .build()
            .toByteArray()

        val receivedPackets = mutableListOf<DiscoveredReceiver>()

        DatagramSocket(port.toInt()).use { socket ->
            thread(start = true, name = "audio-network-discovery-timeout-interrupt") {
                do {
                    val timeRemaining = timeout - timeSince(discoveryStartedAt)
                    if (timeRemaining.isPositive()) {
                        Thread.sleep(timeRemaining.inWholeMilliseconds)
                    }
                } while (timeRemaining.isPositive())
                socket.close()
            }
            socket.broadcast = true
            if (socket.receiveBufferSize < 1024) {
                socket.receiveBufferSize = 1024
            }

            broadcastAddresses.forEach { broadcastAddress ->
                socket.send(DatagramPacket(discoveryRequest, discoveryRequest.size, broadcastAddress))
            }

            val receivePacket = DatagramPacket(ByteArray(1024), 1024)
            while (true) {
                println("receiving...")
                try {
                    socket.receive(receivePacket)
                }
                catch (ex: SocketException) {
                    if (ex.message?.contains("closed") == true) {
                        break
                    }
                    throw ex
                }

                val broadcastMessage = try {
                    BroadcastMessage.parseFrom(receivePacket.asByteBuffer())
                } catch (ex: InvalidProtocolBufferException) {
                    continue
                }

                if (broadcastMessage.magicWord != MAGIC_WORD || broadcastMessage.messageCase != BroadcastMessage.MessageCase.DISCOVERY_RESPONSE) {
                    continue
                }

                receivedPackets.add(DiscoveredReceiver(receivePacket.address, broadcastMessage.discoveryResponse))
            }
        }

        receivedPackets
    }
}

data class DiscoveredReceiver(val inetAddress: InetAddress, val response: DiscoveryResponse)

private fun DatagramPacket.asByteBuffer() = ByteBuffer.wrap(data, offset, length)