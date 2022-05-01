package com.github.tmarsteel.audionetwork.transmitter

import com.github.tmarsteel.audionetwork.protocol.DiscoveryResponse
import com.github.tmarsteel.audionetwork.protocol.ReceiverInformation
import com.github.tmarsteel.audionetwork.protocol.ToReceiver
import com.github.tmarsteel.audionetwork.protocol.ToTransmitter
import io.kotlintest.matchers.shouldBe
import io.kotlintest.specs.FreeSpec
import kotlinx.coroutines.runBlocking
import java.io.ByteArrayOutputStream
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.channels.AsynchronousByteChannel
import java.nio.channels.CompletionHandler
import java.util.concurrent.CompletableFuture
import java.util.concurrent.Future

class AsyncProtobufTest : FreeSpec({
   "read delimited" {
       val bufferOut = ByteArrayOutputStream()
       val message = ToTransmitter.newBuilder()
           .setReceiverInformation(
               ReceiverInformation.newBuilder()
                   .setDiscoveryData(
                       DiscoveryResponse.newBuilder()
                           .setDeviceName("")
                           .setMacAddress(2)
                           .setCurrentlyStreaming(false)
                           .setOpusVersion("bla")
                           .setProtocolVersion(1)
                           .build()
                   )
                   .setMaxDecodedFrameSize(4096)
                   .setMaxEncodedFrameSize(4096)
                   .build()
           )
           .build()

       message.writeDelimitedTo(bufferOut)

       val channel = asynchronousByteChannelOf(ByteBuffer.wrap(bufferOut.toByteArray()))
       val received = runBlocking { channel.readSingleDelimited<ToTransmitter>() }
       received shouldBe message
   }

    "debug" {
        FileInputStream("C:\\Users\\tobia\\Desktop\\network-out.bin").use { inStream ->
            println(ToReceiver.parseFrom(inStream))
        }
    }
})

private fun asynchronousByteChannelOf(data: ByteBuffer) = object : AsynchronousByteChannel {
    @Volatile
    private var closed = false
    override fun close() {
        closed = true
    }

    override fun isOpen(): Boolean {
        return closed
    }

    override fun <A : Any?> read(dst: ByteBuffer, attachment: A, handler: CompletionHandler<Int, in A>) {
        handler.completed(read(dst).get(), attachment)
    }

    override fun read(dst: ByteBuffer): Future<Int> {
        if (!data.hasRemaining()) {
            return CompletableFuture.completedFuture(-1)
        }


        if (data.remaining() <= dst.remaining()) {
            val n = data.remaining()
            dst.put(data)
            return CompletableFuture.completedFuture(n)
        }

        val n = dst.remaining()
        dst.put(dst.position(), data, data.position(), n)
        dst.position(dst.position() + n)
        data.position(data.position() + n)
        return CompletableFuture.completedFuture(n)
    }

    override fun <A : Any?> write(src: ByteBuffer, attachment: A, handler: CompletionHandler<Int, in A>) {
        handler.completed( write(src).get(), attachment)
    }

    override fun write(src: ByteBuffer): Future<Int> {
        val n = src.remaining()
        src.position(src.position() + n)
        return CompletableFuture.completedFuture(n)
    }
}