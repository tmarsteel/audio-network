package com.github.tmarsteel.audionetwork.transmitter

import java.nio.BufferOverflowException
import java.nio.BufferUnderflowException
import java.nio.ByteBuffer

class ByteRingBuffer(val capacity: Int) {
    private val buffer = ByteArray(capacity)
    private var dataStart = 0
    var remainingRead = 0
        private set

    val remainingWrite: Int
        get() = buffer.size - remainingRead

    fun put(source: ByteBuffer) = put(source, source.remaining())
    fun put(source: ByteBuffer, nBytes: Int) {
        require(source.remaining() >= nBytes)
        if (remainingWrite < nBytes) {
            throw BufferOverflowException()
        }

        val nBytesUntilEnd = buffer.size - (dataStart + remainingRead)
        if (nBytesUntilEnd >= nBytes) {
            source.get(this.buffer, dataStart + remainingRead, nBytes)
            remainingRead += nBytes
            return
        }

        if (nBytesUntilEnd <= 0) {
            source.get(this.buffer, (dataStart + remainingRead) % this.buffer.size, nBytes)
            remainingRead += nBytes
            return
        }

        put(source, nBytesUntilEnd)
        put(source, nBytes - nBytesUntilEnd)
    }

    fun put(source: ByteArray, offset: Int, length: Int) {
        put(ByteBuffer.wrap(source, offset, length))
    }

    fun put(source: ByteArray) = put(ByteBuffer.wrap(source))
    fun put(byte: Byte) = put(ByteArray(1) { byte }, 0, 1)

    fun get(target: ByteBuffer) = get(target, target.remaining().coerceAtMost(remainingRead))
    fun get(target: ByteBuffer, nBytes: Int) {
        require(target.remaining() >= nBytes)
        if (remainingRead < nBytes) {
            throw BufferUnderflowException()
        }

        val nBytesUntilEnd = buffer.size - dataStart
        if (nBytesUntilEnd >= nBytes) {
            target.put(this.buffer, dataStart, nBytes)
            dataStart = (dataStart + nBytes) % buffer.size
            remainingRead -= nBytes
            return
        }

        get(target, nBytesUntilEnd)
        get(target, nBytes - nBytesUntilEnd)
    }
    fun get(target: ByteArray, offset: Int, length: Int) = get(ByteBuffer.wrap(target, offset, length), length)

    fun get(target: ByteArray) = get(target, 0, target.size)
    fun get(): Byte {
        val target = ByteArray(1)
        get(target)
        return target[0]
    }
}