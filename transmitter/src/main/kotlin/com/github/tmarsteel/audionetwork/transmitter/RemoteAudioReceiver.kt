package com.github.tmarsteel.audionetwork.transmitter

import club.minnced.opus.util.OpusLibrary
import com.github.tmarsteel.audionetwork.protocol.AudioData
import com.github.tmarsteel.audionetwork.protocol.ReceiverInformation
import com.github.tmarsteel.audionetwork.protocol.ToReceiver
import com.github.tmarsteel.audionetwork.protocol.ToTransmitter
import com.google.protobuf.ByteString
import java.net.SocketAddress
import java.nio.ByteBuffer
import java.nio.channels.AsynchronousSocketChannel
import java.nio.channels.CompletionHandler
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.coroutines.suspendCoroutine

class RemoteAudioReceiver private constructor(
    private val channel: AsynchronousSocketChannel,
    val receiverInformation: ReceiverInformation,
) : AutoCloseable {
    init {
        OpusLibrary.loadFromJar()
        require(channel.isOpen)
    }

    @Volatile
    private var closed = false

    suspend fun queueEncodedOpusFrame(data: ByteBuffer) {
        require(data.remaining() <= receiverInformation.maxEncodedFrameSize)
        channel.writeSingleDelimited(
            ToReceiver.newBuilder()
                .setAudioData(
                    AudioData.newBuilder()
                      .setOpusEncodedFrame(ByteString.copyFrom(data))
                      .build()
                )
                .build()
        )
    }

    override fun close() {
        channel.close()
        closed = true
    }

    companion object {
        suspend fun connect(address: SocketAddress): RemoteAudioReceiver {
            val channel = AsynchronousSocketChannel.open()
            suspendCoroutine<Unit> { continuation ->
                channel.connect(address, null, object : CompletionHandler<Void, Nothing?> {
                    override fun completed(result: Void?, attachment: Nothing?) {
                        continuation.resume(Unit)
                    }

                    override fun failed(exc: Throwable, attachment: Nothing?) {
                        continuation.resumeWithException(exc)
                    }
                })
            }
            val receiverHello = channel.readSingleDelimited<ToTransmitter>()
            if (receiverHello.messageCase != ToTransmitter.MessageCase.RECEIVER_INFORMATION) {
                channel.shutdownInput()
                channel.shutdownOutput()
                channel.close()
                throw IllegalStateException("Did not understand hello from receiver")
            }

            return RemoteAudioReceiver(channel, receiverHello.receiverInformation)
        }
    }
}