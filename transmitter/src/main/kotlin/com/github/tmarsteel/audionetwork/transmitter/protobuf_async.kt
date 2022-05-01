package com.github.tmarsteel.audionetwork.transmitter

import com.google.protobuf.GeneratedMessageV3
import java.io.IOException
import java.io.UncheckedIOException
import java.lang.reflect.InvocationTargetException
import java.nio.ByteBuffer
import java.nio.channels.AsynchronousByteChannel
import java.nio.channels.CompletionHandler
import kotlin.coroutines.Continuation
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.coroutines.suspendCoroutine
import kotlin.experimental.and

private object AsynchronousByteChannelCoroutineCompletionHandler : CompletionHandler<Int, Continuation<UInt>> {
    override fun completed(result: Int, attachment: Continuation<UInt>) {
        if (result < 0) {
            attachment.resumeWithException(UncheckedIOException(IOException("Unexpected EOF")))
            return
        }

        attachment.resume(result.toUInt())
    }

    override fun failed(exc: Throwable, attachment: Continuation<UInt>) {
        attachment.resumeWithException(exc)
    }
}

suspend fun AsynchronousByteChannel.readAsync(dst: ByteBuffer): UInt {
    return suspendCoroutine { continuation ->
        read(dst, continuation, AsynchronousByteChannelCoroutineCompletionHandler)
    }
}
suspend fun AsynchronousByteChannel.writeAsync(src: ByteBuffer) {
    suspendCoroutine<UInt> { continuation ->
        write(src, continuation, AsynchronousByteChannelCoroutineCompletionHandler)
    }
}

suspend fun AsynchronousByteChannel.readVarUInt32(): UInt {
    var carry = 0.toUInt()
    var digitSignificance = 1.toUInt()

    val buffer = ByteBuffer.allocate(1)

    while (true) {
        buffer.clear()
        readAsync(buffer)
        buffer.flip()

        val byte = buffer.get()
        val value = if (byte >= 0) byte else byte and 0b01111111
        carry += value.toUInt() * digitSignificance
        digitSignificance = digitSignificance shl 7

        if (carry and UInt.MAX_VALUE != carry) {
            // larger than 32 bits
            throw UncheckedIOException(IOException("Variable uint larger than 32 bits!"))
        }

        if (byte >= 0) {
            // last
            return carry
        }
    }
}
suspend fun AsynchronousByteChannel.writeVarUInt32(value: UInt) {
    val buffer = ByteBuffer.allocate((value.nBitsRequired ceilDivide 7.toUByte()).toInt())
    var carry = value
    while (carry > 0.toUInt()) {
        val hasMoreBytesFlag = (if (carry > 0b01111111.toUInt()) 0b10000000 else 0).toUInt()
        val byte = (hasMoreBytesFlag or (carry and 0b01111111.toUInt())).toByte()
        buffer.put(byte)
        carry = carry shr 7
    }
    buffer.flip()
    writeAsync(buffer)
}

suspend inline fun <reified T: GeneratedMessageV3> AsynchronousByteChannel.readSingleDelimited() = readSingleDelimited(T::class.java)
suspend fun <T : GeneratedMessageV3> AsynchronousByteChannel.readSingleDelimited(typeClass: Class<T>): T {
    if (typeClass == GeneratedMessageV3::class.java) {
        throw IllegalArgumentException("\$typeClass must be a subclass of ${GeneratedMessageV3::class.java.name}")
    }

    val messageLength = Math.toIntExact(readVarUInt32().toLong())

    val buffer = ByteBuffer.allocate(messageLength)
    while (buffer.remaining() > 0) {
        val nBytesRead = readAsync(buffer)
        if (nBytesRead == 0.toUInt()) {
            throw UncheckedIOException(IOException("Unexpected EOF within ${typeClass.name}"))
        }
    }
    buffer.flip()

    try {
        @Suppress("UNCHECKED_CAST")
        return typeClass
            .getMethod("parseFrom", ByteBuffer::class.java)
            .invoke(null, buffer) as T
    }
    catch (ex: InvocationTargetException) {
        throw ex.cause!!
    }
}

suspend fun AsynchronousByteChannel.writeSingleDelimited(message: GeneratedMessageV3) {
    val messageBytes = message.toByteArray()
    writeVarUInt32(messageBytes.size.toUInt())
    writeAsync(ByteBuffer.wrap(messageBytes))
}

private val UInt.nBitsRequired: UByte get() {
    var carry = this
    var nBits = 0.toUByte()
    while (carry > 0.toUInt()) {
        carry = carry shr 1
        nBits++
    }
    return nBits
}

private infix fun UByte.ceilDivide(divisor: UByte): UInt {
    return (this + divisor - 1.toUByte()) / divisor
}